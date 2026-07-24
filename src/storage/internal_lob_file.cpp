/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "internal_lob_file.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <vector>

#include "dbtype.h"
#include "error_manager.h"
#include "file_manager.h"
#include "filesys_temp.hpp"
#include "internal_lob_marker.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static const int INTERNAL_LOB_CHAIN_FLAG_BLOB = 0x00000001;
static const int INTERNAL_LOB_CHAIN_FLAG_CLOB = 0x00000002;
static const int INTERNAL_LOB_HEAD_HEADER_SIZE = OR_BIGINT_SIZE + OR_INT_SIZE + OR_OID_SIZE;
static const int INTERNAL_LOB_CHUNK_HEADER_SIZE = OR_OID_SIZE;

static int internal_lob_make_locator_db_value_internal (DB_VALUE *value, DB_TYPE lob_type,
    const INTERNAL_LOB_LOCATOR &locator, bool adopted);

static int
internal_lob_set_generic_error (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  return ER_GENERIC_ERROR;
}

static int
internal_lob_lob_type_to_flags (DB_TYPE lob_type, int &flags)
{
  if (lob_type == DB_TYPE_BLOB)
    {
      flags = INTERNAL_LOB_CHAIN_FLAG_BLOB;
      return NO_ERROR;
    }
  if (lob_type == DB_TYPE_CLOB)
    {
      flags = INTERNAL_LOB_CHAIN_FLAG_CLOB;
      return NO_ERROR;
    }
  return internal_lob_set_generic_error ();
}

static int
internal_lob_payload_bytes_from_flags (DB_BIGINT logical_length, int flags, DB_BIGINT &payload_bytes)
{
  if (logical_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (flags == INTERNAL_LOB_CHAIN_FLAG_BLOB)
    {
      if (logical_length > DB_BIGINT_MAX - 7)
	{
	  return internal_lob_set_generic_error ();
	}
      payload_bytes = (logical_length + 7) / 8;
      return NO_ERROR;
    }

  if (flags == INTERNAL_LOB_CHAIN_FLAG_CLOB)
    {
      payload_bytes = logical_length;
      return NO_ERROR;
    }

  return internal_lob_set_generic_error ();
}

static int
internal_lob_blob_physical_bytes (DB_BIGINT bit_length, DB_BIGINT &payload_bytes)
{
  if (bit_length < 0 || bit_length > DB_BIGINT_MAX - 7)
    {
      return internal_lob_set_generic_error ();
    }
  payload_bytes = (bit_length + 7) / 8;
  return NO_ERROR;
}

static void
internal_lob_writer_clear (INTERNAL_LOB_WRITER &writer)
{
  if (writer.spill_file != NULL)
    {
      fclose (writer.spill_file);
      writer.spill_file = NULL;
    }
  if (!writer.spill_path.empty ())
    {
      (void) remove (writer.spill_path.c_str ());
      writer.spill_path.clear ();
    }
  if (writer.segment_buffer != NULL)
    {
      db_private_free_and_init (NULL, writer.segment_buffer);
    }
  writer.segment_size = 0;
  writer.total_bytes = 0;
  VFID_SET_NULL (&writer.lob_vfid);
}

void
internal_lob_insert_abort (INTERNAL_LOB_WRITER &writer)
{
  internal_lob_writer_clear (writer);
}

static int
internal_lob_get_segment_size (int &segment_size)
{
  UINT64 prm_segment_size = prm_get_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE);

  if (prm_segment_size == 0 || prm_segment_size > (UINT64) INT_MAX)
    {
      return internal_lob_set_generic_error ();
    }

  segment_size = (int) prm_segment_size;
  return NO_ERROR;
}

static void
internal_lob_reverse_writer_clear (INTERNAL_LOB_REVERSE_WRITER &writer)
{
  if (writer.segment_buffer != NULL)
    {
      free_and_init (writer.segment_buffer);
    }

  VFID_SET_NULL (&writer.lob_vfid);
  writer.segment_size = 0;
  writer.total_bytes = 0;
  writer.logical_length = 0;
  writer.expected_offset = 0;
  writer.segment_start = 0;
  writer.segment_end = 0;
  writer.segment_received = 0;
  writer.lob_type = DB_TYPE_NULL;
  OID_SET_NULL (&writer.next_oid);
  OID_SET_NULL (&writer.locator.oid);
  writer.locator.length = 0;
  writer.locator.adopted = false;
  writer.initialized = false;
  writer.finished = false;
}

static unsigned long long
internal_lob_mix_u64 (unsigned long long value)
{
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33;
  return value;
}

static unsigned long long
internal_lob_locator_secret (void)
{
  return 0x26914cbfd15cafe1ULL;
}

static unsigned long long
internal_lob_locator_token (const INTERNAL_LOB_LOCATOR &locator, bool adopted)
{
  unsigned long long token = internal_lob_locator_secret ();

  token ^= (unsigned long long) (unsigned short) locator.oid.volid;
  token = internal_lob_mix_u64 (token);
  token ^= (unsigned long long) (unsigned int) locator.oid.pageid;
  token = internal_lob_mix_u64 (token);
  token ^= (unsigned long long) (unsigned short) locator.oid.slotid;
  token = internal_lob_mix_u64 (token);
  token ^= (unsigned long long) locator.length;
  token = internal_lob_mix_u64 (token);
  token ^= adopted ? 0xad0f7edULL : 0x10c07edULL;
  token = internal_lob_mix_u64 (token);

  if (token == 0)
    {
      token = 1;
    }

  return token;
}

int
internal_lob_format_locator_string (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size, bool adopted)
{
  const char *adopt_marker = adopted ? "A:" : "";
  unsigned long long token;

  token = internal_lob_locator_token (locator, adopted);

  return snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%s%d|%d|%d:%lld:%016llx", adopt_marker,
		   (int) locator.oid.volid, (int) locator.oid.pageid, (int) locator.oid.slotid,
		   (long long) locator.length, token);
}

static void
internal_lob_pack_head_header (char *buf, DB_BIGINT logical_length, int flags, const OID &next_oid)
{
  OR_PUT_BIGINT (buf, &logical_length);
  OR_PUT_INT (buf + OR_BIGINT_SIZE, flags);
  OR_PUT_OID (buf + OR_BIGINT_SIZE + OR_INT_SIZE, &next_oid);
}

static int
internal_lob_unpack_head_header (const char *buf, DB_BIGINT &logical_length, int &flags, OID &next_oid)
{
  OR_GET_BIGINT (buf, &logical_length);
  flags = OR_GET_INT (buf + OR_BIGINT_SIZE);
  OR_GET_OID (buf + OR_BIGINT_SIZE + OR_INT_SIZE, &next_oid);

  if (logical_length < 0 || (flags != INTERNAL_LOB_CHAIN_FLAG_BLOB && flags != INTERNAL_LOB_CHAIN_FLAG_CLOB))
    {
      return internal_lob_set_generic_error ();
    }

  return NO_ERROR;
}

static void
internal_lob_pack_chunk_header (char *buf, const OID &next_oid)
{
  OR_PUT_OID (buf, &next_oid);
}

static void
internal_lob_unpack_chunk_header (const char *buf, OID &next_oid)
{
  OR_GET_OID (buf, &next_oid);
}

static int
internal_lob_read_exact_from_oos (THREAD_ENTRY *thread_p, OOS_READER &oos_reader, char *buf, int size)
{
  int total = 0;

  while (total < size)
    {
      int nread = 0;
      int err = oos_read_pull (thread_p, oos_reader, oos_buffer (buf + total, (std::size_t) (size - total)), nread);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (nread <= 0 || nread > size - total)
	{
	  return internal_lob_set_generic_error ();
	}
      total += nread;
    }

  return NO_ERROR;
}

static int
internal_lob_reader_open_lob_node (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, const OID &oid, bool is_head)
{
  char header[INTERNAL_LOB_HEAD_HEADER_SIZE];
  int err;

  if (OID_ISNULL (&oid))
    {
      return internal_lob_set_generic_error ();
    }

  err = oos_read_open (thread_p, oid, reader.oos_reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (is_head)
    {
      DB_BIGINT payload_bytes = 0;

      err = internal_lob_read_exact_from_oos (thread_p, reader.oos_reader, header, INTERNAL_LOB_HEAD_HEADER_SIZE);
      if (err != NO_ERROR)
	{
	  return err;
	}
      err = internal_lob_unpack_head_header (header, reader.logical_length, reader.flags, reader.next_lob_oid);
      if (err != NO_ERROR)
	{
	  return err;
	}
      err = internal_lob_payload_bytes_from_flags (reader.logical_length, reader.flags, payload_bytes);
      if (err != NO_ERROR)
	{
	  return err;
	}
      reader.total_bytes = payload_bytes;
    }
  else
    {
      err = internal_lob_read_exact_from_oos (thread_p, reader.oos_reader, header, INTERNAL_LOB_CHUNK_HEADER_SIZE);
      if (err != NO_ERROR)
	{
	  return err;
	}
      internal_lob_unpack_chunk_header (header, reader.next_lob_oid);
    }

  reader.current_open = true;
  return NO_ERROR;
}

static int
internal_lob_read_next_oid_for_delete (THREAD_ENTRY *thread_p, const OID &oid, bool is_head, DB_BIGINT expected_length,
				       OID &next_oid)
{
  INTERNAL_LOB_READER reader;
  int err;

  reader = INTERNAL_LOB_READER ();
  err = internal_lob_reader_open_lob_node (thread_p, reader, oid, is_head);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (is_head && reader.logical_length != expected_length)
    {
      return internal_lob_set_generic_error ();
    }

  next_oid = reader.next_lob_oid;
  return NO_ERROR;
}

static int
internal_lob_read_spill (INTERNAL_LOB_WRITER &writer, DB_BIGINT offset, char *buf, int size)
{
  if (offset < 0 || size < 0 || offset > (DB_BIGINT) LONG_MAX)
    {
      return internal_lob_set_generic_error ();
    }

  if (fseek (writer.spill_file, (long) offset, SEEK_SET) != 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (size > 0 && fread (buf, 1, (size_t) size, writer.spill_file) != (size_t) size)
    {
      return internal_lob_set_generic_error ();
    }

  return NO_ERROR;
}

int
internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid)
{
  return oos_create_file_with_type (thread_p, FILE_INTERNAL_LOB, lob_vfid);
}

int
internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid)
{
  return oos_remove_file (thread_p, lob_vfid);
}

int
internal_lob_insert (THREAD_ENTRY *thread_p, const VFID &lob_vfid, oos_buffer src, INTERNAL_LOB_LOCATOR &locator)
{
  INTERNAL_LOB_WRITER writer;
  int err;

  err = internal_lob_insert_begin (thread_p, lob_vfid, writer);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (src.size () > 0)
    {
      err = internal_lob_insert_append (thread_p, writer, src);
      if (err != NO_ERROR)
	{
	  internal_lob_writer_clear (writer);
	  return err;
	}
    }

  return internal_lob_insert_end (thread_p, writer, locator, DB_TYPE_CLOB, (DB_BIGINT) src.size ());
}

int
internal_lob_insert_begin (THREAD_ENTRY *thread_p, const VFID &lob_vfid, INTERNAL_LOB_WRITER &writer)
{
  int segment_size;
  int err;
  std::string filename;
  FILE *fileptr = NULL;

  (void) thread_p;

  internal_lob_writer_clear (writer);

  err = internal_lob_get_segment_size (segment_size);
  if (err != NO_ERROR)
    {
      return err;
    }

  std::tie (filename, fileptr) = filesys::open_temp_file ("ilob_", "w+b");
  if (fileptr == NULL)
    {
      return internal_lob_set_generic_error ();
    }

  writer.segment_buffer = (char *) db_private_alloc (NULL, (size_t) segment_size + INTERNAL_LOB_HEAD_HEADER_SIZE);
  if (writer.segment_buffer == NULL)
    {
      fclose (fileptr);
      (void) remove (filename.c_str ());
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  writer.spill_file = fileptr;
  writer.spill_path = filename;
  writer.lob_vfid = lob_vfid;
  writer.segment_size = segment_size;
  writer.total_bytes = 0;
  return NO_ERROR;
}

int
internal_lob_insert_append (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer, oos_buffer chunk)
{
  (void) thread_p;

  if (writer.spill_file == NULL || writer.segment_buffer == NULL || writer.segment_size <= 0
      || (chunk.data () == NULL && chunk.size () > 0))
    {
      internal_lob_writer_clear (writer);
      return internal_lob_set_generic_error ();
    }

  if (chunk.size () > 0)
    {
      if (writer.total_bytes > DB_BIGINT_MAX - (DB_BIGINT) chunk.size ())
	{
	  internal_lob_writer_clear (writer);
	  return internal_lob_set_generic_error ();
	}

      if (fwrite (chunk.data (), 1, chunk.size (), writer.spill_file) != chunk.size ())
	{
	  internal_lob_writer_clear (writer);
	  return internal_lob_set_generic_error ();
	}
      writer.total_bytes += (DB_BIGINT) chunk.size ();
    }

  return NO_ERROR;
}

int
internal_lob_insert_end (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer, INTERNAL_LOB_LOCATOR &locator,
			 DB_TYPE lob_type, DB_BIGINT logical_length)
{
  OID next_oid;
  int flags;
  DB_BIGINT expected_bytes = 0;
  DB_BIGINT remaining;
  int err;

  OID_SET_NULL (&locator.oid);
  locator.length = 0;
  locator.adopted = false;
  OID_SET_NULL (&next_oid);

  if (writer.spill_file == NULL || writer.segment_buffer == NULL || writer.segment_size <= 0)
    {
      internal_lob_writer_clear (writer);
      return internal_lob_set_generic_error ();
    }

  err = internal_lob_lob_type_to_flags (lob_type, flags);
  if (err != NO_ERROR)
    {
      internal_lob_writer_clear (writer);
      return err;
    }

  if (logical_length < 0)
    {
      if (lob_type == DB_TYPE_BLOB)
	{
	  if (writer.total_bytes > DB_BIGINT_MAX / 8)
	    {
	      internal_lob_writer_clear (writer);
	      return internal_lob_set_generic_error ();
	    }
	  logical_length = writer.total_bytes * 8;
	}
      else
	{
	  logical_length = writer.total_bytes;
	}
    }

  err = internal_lob_payload_bytes_from_flags (logical_length, flags, expected_bytes);
  if (err != NO_ERROR || expected_bytes != writer.total_bytes)
    {
      internal_lob_writer_clear (writer);
      return (err != NO_ERROR) ? err : internal_lob_set_generic_error ();
    }

  locator.length = logical_length;
  if (writer.total_bytes == 0)
    {
      /* Empty LOB: still store one header-only head chunk so the locator carries a real OID.
       * The heap OOS-column invariant requires a non-null OID for every internal LOB column
       * value (see the assertion in heap_attrinfo_transform_columns_to_disk). next_oid is NULL
       * here, so this single head chunk is a self-terminating chain that reads back as 0 bytes. */
      OID head_oid;

      internal_lob_pack_head_header (writer.segment_buffer, logical_length, flags, next_oid);
      err = oos_insert (thread_p, writer.lob_vfid,
			oos_buffer (writer.segment_buffer, (std::size_t) INTERNAL_LOB_HEAD_HEADER_SIZE), head_oid);
      if (err != NO_ERROR)
	{
	  internal_lob_writer_clear (writer);
	  return err;
	}
      locator.oid = head_oid;
      internal_lob_writer_clear (writer);
      return NO_ERROR;
    }

  if (fflush (writer.spill_file) != 0)
    {
      internal_lob_writer_clear (writer);
      return internal_lob_set_generic_error ();
    }

  remaining = writer.total_bytes;
  while (remaining > 0)
    {
      const int chunk_len = (remaining > (DB_BIGINT) writer.segment_size) ? writer.segment_size : (int) remaining;
      OID current_oid;
      bool is_head;
      int header_size;
      char *record;
      char *payload;

      remaining -= (DB_BIGINT) chunk_len;
      is_head = (remaining == 0);
      header_size = is_head ? INTERNAL_LOB_HEAD_HEADER_SIZE : INTERNAL_LOB_CHUNK_HEADER_SIZE;
      record = writer.segment_buffer;
      payload = record + header_size;

      err = internal_lob_read_spill (writer, remaining, payload, chunk_len);
      if (err != NO_ERROR)
	{
	  internal_lob_writer_clear (writer);
	  return err;
	}

      if (is_head)
	{
	  internal_lob_pack_head_header (record, logical_length, flags, next_oid);
	}
      else
	{
	  internal_lob_pack_chunk_header (record, next_oid);
	}

      err = oos_insert (thread_p, writer.lob_vfid, oos_buffer (record, (std::size_t) header_size + chunk_len),
			current_oid);
      if (err != NO_ERROR)
	{
	  internal_lob_writer_clear (writer);
	  return err;
	}
      oos_push_oos_oid (thread_p, &current_oid);
      next_oid = current_oid;
    }

  locator.oid = next_oid;
  internal_lob_writer_clear (writer);
  return NO_ERROR;
}

static int
internal_lob_reverse_allocate_segment (INTERNAL_LOB_REVERSE_WRITER &writer)
{
  int header_size;
  DB_BIGINT payload_size;
  std::size_t allocation_size;

  if (writer.segment_buffer != NULL)
    {
      return NO_ERROR;
    }

  payload_size = writer.segment_end - writer.segment_start;
  header_size = writer.segment_start == 0 ? INTERNAL_LOB_HEAD_HEADER_SIZE : INTERNAL_LOB_CHUNK_HEADER_SIZE;
  if (payload_size <= 0 || payload_size > writer.segment_size)
    {
      return internal_lob_set_generic_error ();
    }

  allocation_size = (std::size_t) payload_size + (std::size_t) header_size;
  /*
   * The reverse writer survives across STREAM_SEND_DATA requests.  Do not use
   * db_private_alloc here because its resource tracking is request-thread
   * scoped and a later request may flush/free this buffer on another thread.
   */
  writer.segment_buffer = (char *) malloc (allocation_size);
  if (writer.segment_buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, allocation_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  return NO_ERROR;
}

static int
internal_lob_reverse_flush_segment (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer)
{
  DB_BIGINT payload_size;
  OID current_oid;
  int header_size;
  int error;

  payload_size = writer.segment_end - writer.segment_start;
  if (writer.segment_buffer == NULL || payload_size <= 0 || writer.segment_received != payload_size)
    {
      return internal_lob_set_generic_error ();
    }

  header_size = writer.segment_start == 0 ? INTERNAL_LOB_HEAD_HEADER_SIZE : INTERNAL_LOB_CHUNK_HEADER_SIZE;
  if (writer.segment_start == 0)
    {
      int flags;

      error = internal_lob_lob_type_to_flags (writer.lob_type, flags);
      if (error != NO_ERROR)
	{
	  return error;
	}
      internal_lob_pack_head_header (writer.segment_buffer, writer.logical_length, flags, writer.next_oid);
    }
  else
    {
      internal_lob_pack_chunk_header (writer.segment_buffer, writer.next_oid);
    }

  error = oos_insert (thread_p, writer.lob_vfid,
		      oos_buffer (writer.segment_buffer, (std::size_t) header_size + (std::size_t) payload_size),
		      current_oid);
  if (error != NO_ERROR)
    {
      return error;
    }

  oos_push_oos_oid (thread_p, &current_oid);
  writer.next_oid = current_oid;
  free_and_init (writer.segment_buffer);
  writer.segment_received = 0;

  if (writer.segment_start == 0)
    {
      writer.locator.oid = current_oid;
      writer.locator.length = writer.logical_length;
      writer.locator.adopted = false;
      writer.finished = true;
      return NO_ERROR;
    }

  writer.segment_end = writer.segment_start;
  writer.segment_start = writer.segment_end > writer.segment_size ? writer.segment_end - writer.segment_size : 0;
  return NO_ERROR;
}

int
internal_lob_reverse_insert_begin (THREAD_ENTRY *thread_p, const VFID &lob_vfid, DB_TYPE lob_type,
				   DB_BIGINT total_bytes, DB_BIGINT logical_length,
				   INTERNAL_LOB_REVERSE_WRITER &writer)
{
  int segment_size;
  int flags;
  DB_BIGINT expected_bytes;
  int error;

  (void) thread_p;
  internal_lob_reverse_writer_clear (writer);

  if (VFID_ISNULL (&lob_vfid) || total_bytes < 0 || total_bytes > DB_MAX_INTERNAL_LOB_LENGTH
      || logical_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  error = internal_lob_lob_type_to_flags (lob_type, flags);
  if (error != NO_ERROR)
    {
      return error;
    }
  error = internal_lob_payload_bytes_from_flags (logical_length, flags, expected_bytes);
  if (error != NO_ERROR || expected_bytes != total_bytes)
    {
      return error != NO_ERROR ? error : internal_lob_set_generic_error ();
    }
  error = internal_lob_get_segment_size (segment_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  writer.lob_vfid = lob_vfid;
  writer.segment_size = segment_size;
  writer.total_bytes = total_bytes;
  writer.logical_length = logical_length;
  writer.expected_offset = total_bytes;
  writer.segment_end = total_bytes;
  writer.segment_start = total_bytes > segment_size ? total_bytes - segment_size : 0;
  writer.lob_type = lob_type;
  OID_SET_NULL (&writer.next_oid);
  OID_SET_NULL (&writer.locator.oid);
  writer.locator.length = logical_length;
  writer.locator.adopted = false;
  writer.initialized = true;
  return NO_ERROR;
}

int
internal_lob_reverse_insert_append (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer,
				    DB_BIGINT offset, oos_buffer chunk)
{
  DB_BIGINT range_end;
  int error;

  if (!writer.initialized || writer.finished || writer.total_bytes == 0 || chunk.data () == NULL
      || chunk.size () == 0 || chunk.size () > (std::size_t) DB_BIGINT_MAX
      || offset < 0 || offset > DB_BIGINT_MAX - (DB_BIGINT) chunk.size ())
    {
      return internal_lob_set_generic_error ();
    }

  range_end = offset + (DB_BIGINT) chunk.size ();
  if (range_end != writer.expected_offset)
    {
      return internal_lob_set_generic_error ();
    }

  while (range_end > offset)
    {
      DB_BIGINT piece_start;
      DB_BIGINT piece_size;
      DB_BIGINT payload_offset;
      DB_BIGINT source_offset;
      int header_size;

      if (writer.segment_end <= writer.segment_start || range_end > writer.segment_end
	  || range_end <= writer.segment_start)
	{
	  return internal_lob_set_generic_error ();
	}

      error = internal_lob_reverse_allocate_segment (writer);
      if (error != NO_ERROR)
	{
	  return error;
	}

      piece_start = offset > writer.segment_start ? offset : writer.segment_start;
      piece_size = range_end - piece_start;
      payload_offset = piece_start - writer.segment_start;
      source_offset = piece_start - offset;
      header_size = writer.segment_start == 0 ? INTERNAL_LOB_HEAD_HEADER_SIZE : INTERNAL_LOB_CHUNK_HEADER_SIZE;

      memcpy (writer.segment_buffer + header_size + (std::size_t) payload_offset,
	      chunk.data () + (std::size_t) source_offset, (std::size_t) piece_size);
      writer.segment_received += piece_size;
      range_end = piece_start;

      if (range_end == writer.segment_start)
	{
	  error = internal_lob_reverse_flush_segment (thread_p, writer);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  writer.expected_offset = offset;
  return NO_ERROR;
}

int
internal_lob_reverse_insert_end (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer,
				 INTERNAL_LOB_LOCATOR &locator)
{
  if (!writer.initialized || writer.expected_offset != 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (writer.total_bytes == 0 && !writer.finished)
    {
      char header[INTERNAL_LOB_HEAD_HEADER_SIZE];
      OID head_oid;
      OID next_oid;
      int flags;
      int error;

      OID_SET_NULL (&next_oid);
      error = internal_lob_lob_type_to_flags (writer.lob_type, flags);
      if (error != NO_ERROR)
	{
	  return error;
	}
      internal_lob_pack_head_header (header, writer.logical_length, flags, next_oid);
      error = oos_insert (thread_p, writer.lob_vfid, oos_buffer (header, sizeof (header)), head_oid);
      if (error != NO_ERROR)
	{
	  return error;
	}
      oos_push_oos_oid (thread_p, &head_oid);
      writer.locator.oid = head_oid;
      writer.locator.length = writer.logical_length;
      writer.locator.adopted = false;
      writer.finished = true;
    }

  if (!writer.finished || OID_ISNULL (&writer.locator.oid) || writer.segment_buffer != NULL)
    {
      return internal_lob_set_generic_error ();
    }

  locator = writer.locator;
  return NO_ERROR;
}

void
internal_lob_reverse_insert_abort (INTERNAL_LOB_REVERSE_WRITER &writer)
{
  internal_lob_reverse_writer_clear (writer);
}

struct internal_lob_clone_node
{
  OID oid;
  int record_length;
};

static bool
internal_lob_clone_contains_oid (const std::vector<internal_lob_clone_node> &nodes, const OID &oid)
{
  for (const internal_lob_clone_node &node : nodes)
    {
      if (OID_EQ (&node.oid, &oid))
	{
	  return true;
	}
    }
  return false;
}

int
internal_lob_clone (THREAD_ENTRY *thread_p, const VFID &target_lob_vfid, DB_TYPE lob_type,
		    const INTERNAL_LOB_LOCATOR &source_locator, INTERNAL_LOB_LOCATOR &target_locator)
{
  std::vector<internal_lob_clone_node> nodes;
  INTERNAL_LOB_REVERSE_WRITER empty_writer;
  DB_BIGINT expected_payload_bytes;
  DB_BIGINT expected_head_payload;
  DB_BIGINT copied_payload_bytes = 0;
  OID source_oid;
  OID target_next_oid;
  char *record = NULL;
  std::size_t expected_node_count;
  std::size_t max_record_length = 0;
  int expected_flags;
  int segment_size;
  int error;

  OID_SET_NULL (&target_locator.oid);
  target_locator.length = 0;
  target_locator.adopted = false;

  if (thread_p == NULL || VFID_ISNULL (&target_lob_vfid) || source_locator.length < 0
      || (lob_type != DB_TYPE_BLOB && lob_type != DB_TYPE_CLOB))
    {
      return internal_lob_set_generic_error ();
    }

  error = internal_lob_lob_type_to_flags (lob_type, expected_flags);
  if (error != NO_ERROR)
    {
      return error;
    }
  error = internal_lob_payload_bytes_from_flags (source_locator.length, expected_flags, expected_payload_bytes);
  if (error != NO_ERROR)
    {
      return error;
    }
  error = internal_lob_get_segment_size (segment_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (OID_ISNULL (&source_locator.oid))
    {
      if (expected_payload_bytes != 0)
	{
	  return internal_lob_set_generic_error ();
	}
      error = internal_lob_reverse_insert_begin (thread_p, target_lob_vfid, lob_type, 0, source_locator.length,
						 empty_writer);
      if (error != NO_ERROR)
	{
	  return error;
	}
      return internal_lob_reverse_insert_end (thread_p, empty_writer, target_locator);
    }

  expected_node_count = expected_payload_bytes == 0
			? 1
			: (std::size_t) ((expected_payload_bytes - 1) / segment_size + 1);
  expected_head_payload = expected_payload_bytes - (DB_BIGINT) (expected_node_count - 1) * segment_size;
  source_oid = source_locator.oid;

  while (!OID_ISNULL (&source_oid))
    {
      INTERNAL_LOB_READER node_reader;
      DB_BIGINT expected_node_payload;
      int header_size;
      int node_payload;
      int record_length;
      bool is_head = nodes.empty ();

      if (nodes.size () >= expected_node_count || internal_lob_clone_contains_oid (nodes, source_oid))
	{
	  return internal_lob_set_generic_error ();
	}

      record_length = oos_get_length (thread_p, source_oid);
      if (record_length < 0)
	{
	  error = er_errid ();
	  return error != NO_ERROR ? error : internal_lob_set_generic_error ();
	}

      header_size = is_head ? INTERNAL_LOB_HEAD_HEADER_SIZE : INTERNAL_LOB_CHUNK_HEADER_SIZE;
      expected_node_payload = is_head ? expected_head_payload : segment_size;
      node_payload = record_length - header_size;
      if (node_payload < 0 || (DB_BIGINT) node_payload != expected_node_payload)
	{
	  return internal_lob_set_generic_error ();
	}

      node_reader = INTERNAL_LOB_READER ();
      error = internal_lob_reader_open_lob_node (thread_p, node_reader, source_oid, is_head);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (is_head
	  && (node_reader.logical_length != source_locator.length || node_reader.flags != expected_flags))
	{
	  return internal_lob_set_generic_error ();
	}

      nodes.push_back ({source_oid, record_length});
      copied_payload_bytes += node_payload;
      if (copied_payload_bytes > expected_payload_bytes)
	{
	  return internal_lob_set_generic_error ();
	}
      if ((std::size_t) record_length > max_record_length)
	{
	  max_record_length = (std::size_t) record_length;
	}
      source_oid = node_reader.next_lob_oid;
    }

  if (nodes.size () != expected_node_count || copied_payload_bytes != expected_payload_bytes
      || max_record_length == 0)
    {
      return internal_lob_set_generic_error ();
    }

  record = (char *) malloc (max_record_length);
  if (record == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, max_record_length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  OID_SET_NULL (&target_next_oid);
  for (std::size_t reverse_index = nodes.size (); reverse_index > 0; reverse_index--)
    {
      const std::size_t index = reverse_index - 1;
      const bool is_head = index == 0;
      OID source_next_oid;
      OID target_current_oid;

      error = oos_read (thread_p, nodes[index].oid,
			oos_buffer (record, (std::size_t) nodes[index].record_length));
      if (error != NO_ERROR)
	{
	  free_and_init (record);
	  return error;
	}

      if (is_head)
	{
	  DB_BIGINT logical_length;
	  int flags;

	  error = internal_lob_unpack_head_header (record, logical_length, flags, source_next_oid);
	  if (error != NO_ERROR || logical_length != source_locator.length || flags != expected_flags)
	    {
	      free_and_init (record);
	      return error != NO_ERROR ? error : internal_lob_set_generic_error ();
	    }
	  internal_lob_pack_head_header (record, logical_length, flags, target_next_oid);
	}
      else
	{
	  internal_lob_unpack_chunk_header (record, source_next_oid);
	  internal_lob_pack_chunk_header (record, target_next_oid);
	}

      if ((index + 1 < nodes.size () && !OID_EQ (&source_next_oid, &nodes[index + 1].oid))
	  || (index + 1 == nodes.size () && !OID_ISNULL (&source_next_oid)))
	{
	  free_and_init (record);
	  return internal_lob_set_generic_error ();
	}

      error = oos_insert (thread_p, target_lob_vfid,
			  oos_buffer (record, (std::size_t) nodes[index].record_length), target_current_oid);
      if (error != NO_ERROR)
	{
	  free_and_init (record);
	  return error;
	}
      oos_push_oos_oid (thread_p, &target_current_oid);
      target_next_oid = target_current_oid;
    }

  free_and_init (record);
  target_locator.oid = target_next_oid;
  target_locator.length = source_locator.length;
  target_locator.adopted = false;
  return NO_ERROR;
}

int
internal_lob_read (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, oos_buffer dest)
{
  INTERNAL_LOB_READER reader;
  std::size_t total_read = 0;
  int err;

  err = internal_lob_read_open (thread_p, locator, reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (reader.total_bytes != (DB_BIGINT) dest.size ())
    {
      return internal_lob_set_generic_error ();
    }

  while (total_read < dest.size ())
    {
      int nread = 0;
      err = internal_lob_read_pull (thread_p, reader, dest.subspan (total_read), nread);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (nread <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      total_read += (std::size_t) nread;
    }

  return NO_ERROR;
}

int
internal_lob_read_range (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT offset, oos_buffer dest,
			 int &nread)
{
  char skip_buffer[64 * 1024];
  INTERNAL_LOB_READER reader;
  DB_BIGINT to_skip;
  DB_BIGINT max_to_read;
  int err;

  nread = 0;
  if (offset < 0 || (dest.data () == NULL && dest.size () > 0))
    {
      return internal_lob_set_generic_error ();
    }

  err = internal_lob_read_open (thread_p, locator, reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dest.size () == 0 || offset >= reader.total_bytes)
    {
      return NO_ERROR;
    }

  to_skip = offset;
  while (to_skip > 0)
    {
      int skipped = 0;
      int skip_size = (to_skip > (DB_BIGINT) sizeof (skip_buffer)) ? (int) sizeof (skip_buffer) : (int) to_skip;

      err = internal_lob_read_pull (thread_p, reader, oos_buffer (skip_buffer, (std::size_t) skip_size), skipped);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (skipped <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      to_skip -= (DB_BIGINT) skipped;
    }

  max_to_read = reader.total_bytes - offset;
  if (max_to_read > (DB_BIGINT) dest.size ())
    {
      max_to_read = (DB_BIGINT) dest.size ();
    }
  if (max_to_read > (DB_BIGINT) INT_MAX)
    {
      max_to_read = (DB_BIGINT) INT_MAX;
    }

  while (nread < (int) max_to_read)
    {
      int pulled = 0;
      const int pull_size = (int) max_to_read - nread;

      err = internal_lob_read_pull (thread_p, reader, dest.subspan ((std::size_t) nread, (std::size_t) pull_size),
				    pulled);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (pulled <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      nread += pulled;
    }

  return NO_ERROR;
}

int
internal_lob_read_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, INTERNAL_LOB_READER &reader)
{
  int err;

  reader = INTERNAL_LOB_READER ();
  reader.logical_length = locator.length;

  if (locator.length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (locator.length == 0 && OID_ISNULL (&locator.oid))
    {
      reader.flags = INTERNAL_LOB_CHAIN_FLAG_CLOB;
      return NO_ERROR;
    }

  if (OID_ISNULL (&locator.oid))
    {
      return internal_lob_set_generic_error ();
    }

  err = internal_lob_reader_open_lob_node (thread_p, reader, locator.oid, true);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (reader.logical_length != locator.length)
    {
      return internal_lob_set_generic_error ();
    }

  return NO_ERROR;
}

int
internal_lob_read_pull (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, oos_buffer dest, int &nread)
{
  nread = 0;

  if (dest.size () == 0 || reader.total_read >= reader.total_bytes)
    {
      return NO_ERROR;
    }

  while ((std::size_t) nread < dest.size () && nread < INT_MAX && reader.total_read < reader.total_bytes)
    {
      DB_BIGINT remaining_total = reader.total_bytes - reader.total_read;
      std::size_t dest_remaining = dest.size () - (std::size_t) nread;
      int request_size;
      int pulled = 0;
      int err;

      if (!reader.current_open)
	{
	  if (OID_ISNULL (&reader.next_lob_oid))
	    {
	      return internal_lob_set_generic_error ();
	    }
	  err = internal_lob_reader_open_lob_node (thread_p, reader, reader.next_lob_oid, false);
	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	}

      if (dest_remaining > (std::size_t) INT_MAX)
	{
	  request_size = INT_MAX;
	}
      else
	{
	  request_size = (int) dest_remaining;
	}
      if ((DB_BIGINT) request_size > remaining_total)
	{
	  request_size = (int) remaining_total;
	}

      err = oos_read_pull (thread_p, reader.oos_reader,
			   dest.subspan ((std::size_t) nread, (std::size_t) request_size), pulled);
      if (err != NO_ERROR)
	{
	  return err;
	}

      if (pulled == 0)
	{
	  reader.current_open = false;
	  continue;
	}
      if (pulled < 0 || pulled > request_size)
	{
	  return internal_lob_set_generic_error ();
	}

      reader.total_read += (DB_BIGINT) pulled;
      nread += pulled;
    }

  return NO_ERROR;
}

int
internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator)
{
  OID current_oid;
  bool is_head = true;
  int err;

  if (locator.length == 0 && OID_ISNULL (&locator.oid))
    {
      return NO_ERROR;
    }

  if (locator.length < 0 || OID_ISNULL (&locator.oid))
    {
      return internal_lob_set_generic_error ();
    }

  current_oid = locator.oid;
  while (!OID_ISNULL (&current_oid))
    {
      OID next_oid;

      OID_SET_NULL (&next_oid);
      err = internal_lob_read_next_oid_for_delete (thread_p, current_oid, is_head, locator.length, next_oid);
      if (err != NO_ERROR)
	{
	  return err;
	}

      err = oos_delete (thread_p, lob_vfid, current_oid);
      if (err != NO_ERROR)
	{
	  return err;
	}

      current_oid = next_oid;
      is_head = false;
    }

  return NO_ERROR;
}

int
internal_lob_get_length (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator)
{
  (void) thread_p;
  if (locator.length > (DB_BIGINT) INT_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return -1;
    }
  return (int) locator.length;
}

bool
internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator)
{
  char locator_buf[128];
  int volid, pageid, slotid;
  long long length;
  unsigned long long parsed_token = 0;
  int consumed = 0;
  int token_consumed = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_LOCATOR_PREFIX);
  int marker_len = 0;
  bool adopted = false;
  char *oid_part;
  INTERNAL_LOB_LOCATOR parsed_locator;

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (locator_buf))
    {
      return false;
    }
  if (memcmp (data, INTERNAL_LOB_LOCATOR_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  memcpy (locator_buf, data, size);
  locator_buf[size] = '\0';

  oid_part = locator_buf + prefix_len;
  if (oid_part[0] == 'A' && oid_part[1] == ':')
    {
      adopted = true;
      marker_len += 2;
      oid_part += 2;
    }

  if (sscanf (oid_part, "%d|%d|%d:%lld%n", &volid, &pageid, &slotid, &length, &consumed) != 4)
    {
      return false;
    }
  if (length < 0 || oid_part[consumed] != ':')
    {
      return false;
    }
  if (sscanf (oid_part + consumed + 1, "%llx%n", &parsed_token, &token_consumed) != 1 || parsed_token == 0
      || oid_part[consumed + 1 + token_consumed] != '\0'
      || prefix_len + marker_len + consumed + 1 + token_consumed != size)
    {
      return false;
    }

  parsed_locator.oid.volid = (VOLID) volid;
  parsed_locator.oid.pageid = (PAGEID) pageid;
  parsed_locator.oid.slotid = (PGSLOTID) slotid;
  parsed_locator.length = (DB_BIGINT) length;
  parsed_locator.adopted = adopted;
  if (parsed_token != internal_lob_locator_token (parsed_locator, adopted))
    {
      return false;
    }

  if (locator != NULL)
    {
      *locator = parsed_locator;
    }
  return true;
}

bool
internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;

  if (value == NULL || DB_IS_NULL (value)
      || !db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;
      data = (const char *) db_get_bit (value, &bit_length);
      size = (bit_length + 7) / 8;
    }
  else
    {
      return false;
    }

  return internal_lob_parse_locator_string (data, size, locator);
}

bool
internal_lob_db_value_is_pending (const DB_VALUE *value, INTERNAL_LOB_PENDING *pending)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_PENDING_PREFIX);
  char marker_buf[PATH_MAX + 64];
  char type_char;
  long long pending_size;
  int delete_after_read;
  int locator_offset = 0;
  const char *locator;
  size_t locator_len;

  if (value == NULL || DB_IS_NULL (value))
    {
      return false;
    }
  if (!db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_PENDING))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;

      data = (const char *) db_get_bit (value, &bit_length);
      if (bit_length < 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      size = bit_length / 8;
    }
  else
    {
      return false;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (marker_buf)
      || memcmp (data, INTERNAL_LOB_PENDING_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  memcpy (marker_buf, data, size);
  marker_buf[size] = '\0';

  if (sscanf (marker_buf + prefix_len, "%c:%lld:%d:%n", &type_char, &pending_size, &delete_after_read,
	      &locator_offset) < 3
      || locator_offset <= 0 || pending_size < 0 || pending_size > DB_MAX_INTERNAL_LOB_LENGTH
      || (delete_after_read != 0 && delete_after_read != 1) || (type_char != 'C' && type_char != 'B'))
    {
      return false;
    }

  if ((type_char == 'C' && type != DB_TYPE_CLOB) || (type_char == 'B' && type != DB_TYPE_BLOB))
    {
      return false;
    }

  locator = marker_buf + prefix_len + locator_offset;
  locator_len = strlen (locator);
  if (locator_len == 0 || locator_len >= PATH_MAX + 16)
    {
      return false;
    }

  if (pending != NULL)
    {
      pending->lob_type = (type_char == 'C') ? DB_TYPE_CLOB : DB_TYPE_BLOB;
      pending->size = (DB_BIGINT) pending_size;
      pending->delete_after_read = delete_after_read != 0;
      memcpy (pending->locator, locator, locator_len + 1);
    }

  return true;
}

bool
internal_lob_db_value_is_upload (const DB_VALUE *value, INTERNAL_LOB_UPLOAD_TOKEN *upload)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_UPLOAD_PREFIX);
  char marker_buf[160];
  char type_char;
  long long token;
  long long data_length;
  long long logical_length;
  int consumed = 0;

  if (value == NULL || DB_IS_NULL (value)
      || !db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_UPLOAD))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;
      data = (const char *) db_get_bit (value, &bit_length);
      if (bit_length < 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      size = bit_length / 8;
    }
  else
    {
      return false;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (marker_buf)
      || memcmp (data, INTERNAL_LOB_UPLOAD_PREFIX, prefix_len) != 0)
    {
      return false;
    }
  memcpy (marker_buf, data, size);
  marker_buf[size] = '\0';

  if (sscanf (marker_buf + prefix_len, "%c:%lld:%lld:%lld%n", &type_char, &token, &data_length,
	      &logical_length, &consumed) != 4
      || marker_buf[prefix_len + consumed] != '\0' || token <= 0 || data_length < 0
      || data_length > DB_MAX_INTERNAL_LOB_LENGTH || logical_length < 0 || (type_char != 'B' && type_char != 'C')
      || (type_char == 'B' && type != DB_TYPE_BLOB) || (type_char == 'C' && type != DB_TYPE_CLOB))
    {
      return false;
    }

  if (upload != NULL)
    {
      upload->lob_type = type;
      upload->token = (INT64) token;
      upload->data_length = (DB_BIGINT) data_length;
      upload->logical_length = (DB_BIGINT) logical_length;
    }
  return true;
}

bool
internal_lob_db_value_is_dml_slot (const DB_VALUE *value, INTERNAL_LOB_DML_SLOT *dml_slot)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_DML_SLOT_PREFIX);
  char marker_buf[80];
  int slot;
  int consumed = 0;

  if (value == NULL || DB_IS_NULL (value)
      || !db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_DML_SLOT))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;

      data = (const char *) db_get_bit (value, &bit_length);
      if (bit_length < 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      size = bit_length / 8;
    }
  else
    {
      return false;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (marker_buf)
      || memcmp (data, INTERNAL_LOB_DML_SLOT_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  memcpy (marker_buf, data, size);
  marker_buf[size] = '\0';
  if (sscanf (marker_buf + prefix_len, "%d%n", &slot, &consumed) != 1
      || marker_buf[prefix_len + consumed] != '\0' || slot < 0)
    {
      return false;
    }

  if (dml_slot != NULL)
    {
      dml_slot->slot = slot;
    }
  return true;
}

int
internal_lob_encode_disk_length (const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT &disk_length)
{
  if (locator.length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  disk_length = locator.length;
  return NO_ERROR;
}

int
internal_lob_decode_disk_length (INTERNAL_LOB_LOCATOR &locator, DB_BIGINT disk_length)
{
  if (disk_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  locator.length = disk_length;
  locator.adopted = false;
  return NO_ERROR;
}

int
internal_lob_make_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator)
{
  return internal_lob_make_locator_db_value_internal (value, lob_type, locator, false);
}

int
internal_lob_make_adopt_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator)
{
  return internal_lob_make_locator_db_value_internal (value, lob_type, locator, true);
}

static int
internal_lob_make_locator_db_value_internal (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator,
    bool adopted)
{
  char stack_buf[128];
  char *locator_buf = NULL;
  int locator_len;
  int err;

  locator_len = internal_lob_format_locator_string (locator, stack_buf, sizeof (stack_buf), adopted);
  if (locator_len <= 0 || locator_len >= (int) sizeof (stack_buf))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  locator_buf = (char *) db_private_alloc (NULL, locator_len + 1);
  if (locator_buf == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }
  memcpy (locator_buf, stack_buf, locator_len + 1);

  if (lob_type == DB_TYPE_CLOB)
    {
      err = db_make_clob (value, DB_MAX_LOB_PRECISION, locator_buf, locator_len);
    }
  else if (lob_type == DB_TYPE_BLOB)
    {
      err = db_make_blob (value, DB_MAX_LOB_PRECISION, (DB_CONST_C_BIT) locator_buf, locator_len * 8);
    }
  else
    {
      err = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
    }

  if (err != NO_ERROR)
    {
      db_private_free_and_init (NULL, locator_buf);
      return err;
    }

  value->need_clear = true;
  db_value_mark_internal_lob (value, DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR);
  return NO_ERROR;
}

int
internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
			    DB_VALUE *value, TP_DOMAIN *domain)
{
  char *raw_value = NULL;
  DB_BIGINT raw_length_bigint = 0;
  int raw_length;
  int precision;
  int err;

  if (locator.length < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  if (lob_type == DB_TYPE_BLOB)
    {
      err = internal_lob_blob_physical_bytes (locator.length, raw_length_bigint);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }
  else if (lob_type == DB_TYPE_CLOB)
    {
      raw_length_bigint = locator.length;
    }
  else
    {
      return internal_lob_set_generic_error ();
    }

  if (raw_length_bigint > (DB_BIGINT) INT_MAX || locator.length > (DB_BIGINT) INT_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  raw_length = (int) raw_length_bigint;
  raw_value = (char *) db_private_alloc (NULL, (size_t) (raw_length > 0 ? raw_length : 1));
  if (raw_value == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  if (raw_length > 0)
    {
      err = internal_lob_read (thread_p, locator, oos_buffer (raw_value, (std::size_t) raw_length));
      if (err != NO_ERROR)
	{
	  db_private_free_and_init (NULL, raw_value);
	  return err;
	}
    }

  precision = (domain != NULL) ? domain->precision : DB_MAX_LOB_PRECISION;
  if (lob_type == DB_TYPE_CLOB)
    {
      err = db_make_clob (value, precision, raw_value, raw_length);
    }
  else
    {
      err = db_make_blob (value, precision, (DB_CONST_C_BIT) raw_value, (int) locator.length);
    }

  if (err != NO_ERROR)
    {
      db_private_free_and_init (NULL, raw_value);
      return err;
    }

  value->need_clear = true;
  return NO_ERROR;
}
