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

#include <climits>
#include <cstdio>
#include <cstring>

#include "dbtype.h"
#include "error_manager.h"
#include "file_manager.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static const int INTERNAL_LOB_MANIFEST_MAGIC = 0x494c4f42;	/* "ILOB" */
static const int INTERNAL_LOB_MANIFEST_HEADER_SIZE = OR_INT_SIZE + OR_BIGINT_SIZE + OR_INT_SIZE;
static const int INTERNAL_LOB_MANIFEST_SEGMENT_SIZE = OR_OID_SIZE + OR_INT_SIZE;

static int
internal_lob_format_locator (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size)
{
  const bool has_bit_length = locator.bit_length >= 0;

  if (locator.is_manifest)
    {
      return has_bit_length ?
	     snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "M:%d|%d|%d:%lld:%lld", (int) locator.oid.volid,
		       (int) locator.oid.pageid, (int) locator.oid.slotid, (long long) locator.length,
		       (long long) locator.bit_length)
	     : snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "M:%d|%d|%d:%lld", (int) locator.oid.volid,
			 (int) locator.oid.pageid, (int) locator.oid.slotid, (long long) locator.length);
    }

  return has_bit_length ?
	 snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%d|%d|%d:%lld:%lld", (int) locator.oid.volid,
		   (int) locator.oid.pageid, (int) locator.oid.slotid, (long long) locator.length,
		   (long long) locator.bit_length)
	 : snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%d|%d|%d:%lld", (int) locator.oid.volid,
		     (int) locator.oid.pageid, (int) locator.oid.slotid, (long long) locator.length);
}

static int
internal_lob_set_generic_error (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  return ER_GENERIC_ERROR;
}

static void
internal_lob_writer_clear (INTERNAL_LOB_WRITER &writer)
{
  if (writer.segment_buffer != NULL)
    {
      db_private_free_and_init (NULL, writer.segment_buffer);
    }
  writer.segment_size = 0;
  writer.segment_buffer_length = 0;
  writer.total_length = 0;
  writer.segments.clear ();
  VFID_SET_NULL (&writer.lob_vfid);
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

static int
internal_lob_get_bit_remainder (const INTERNAL_LOB_LOCATOR &locator, int &bit_remainder)
{
  bit_remainder = 0;

  if (locator.bit_length < 0)
    {
      return NO_ERROR;
    }

  if (locator.length == 0)
    {
      if (locator.bit_length != 0)
	{
	  return internal_lob_set_generic_error ();
	}
      return NO_ERROR;
    }

  if (locator.length > DB_BIGINT_MAX / 8 || locator.bit_length <= (locator.length - 1) * 8
      || locator.bit_length > locator.length * 8)
    {
      return internal_lob_set_generic_error ();
    }

  if (locator.bit_length != locator.length * 8)
    {
      bit_remainder = (int) (locator.bit_length - (locator.length - 1) * 8);
      if (bit_remainder <= 0 || bit_remainder >= 8)
	{
	  return internal_lob_set_generic_error ();
	}
    }

  return NO_ERROR;
}

static int
internal_lob_flush_segment (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer)
{
  INTERNAL_LOB_SEGMENT segment;
  int err;

  if (writer.segment_buffer_length == 0)
    {
      return NO_ERROR;
    }

  segment.length = writer.segment_buffer_length;
  err = oos_insert (thread_p, writer.lob_vfid, oos_buffer (writer.segment_buffer, (std::size_t) segment.length),
		    segment.oid);
  if (err != NO_ERROR)
    {
      return err;
    }

  writer.segments.push_back (segment);
  writer.segment_buffer_length = 0;
  return NO_ERROR;
}

static int
internal_lob_serialize_manifest (const std::vector<INTERNAL_LOB_SEGMENT> &segments, DB_BIGINT total_length,
				 std::vector<char> &manifest)
{
  OR_BUF buf;
  int count;
  int manifest_size;

  if (segments.empty () || segments.size () > (std::size_t) INT_MAX)
    {
      return internal_lob_set_generic_error ();
    }

  count = (int) segments.size ();
  if (count > (INT_MAX - INTERNAL_LOB_MANIFEST_HEADER_SIZE) / INTERNAL_LOB_MANIFEST_SEGMENT_SIZE)
    {
      return internal_lob_set_generic_error ();
    }

  manifest_size = INTERNAL_LOB_MANIFEST_HEADER_SIZE + count * INTERNAL_LOB_MANIFEST_SEGMENT_SIZE;
  manifest.resize ((std::size_t) manifest_size);

  or_init (&buf, manifest.data (), manifest_size);
  (void) or_put_int (&buf, INTERNAL_LOB_MANIFEST_MAGIC);
  (void) or_put_bigint (&buf, total_length);
  (void) or_put_int (&buf, count);

  for (const INTERNAL_LOB_SEGMENT &segment : segments)
    {
      if (segment.length <= 0 || OID_ISNULL (&segment.oid))
	{
	  return internal_lob_set_generic_error ();
	}
      (void) or_put_oid (&buf, &segment.oid);
      (void) or_put_int (&buf, segment.length);
    }

  assert (buf.ptr == buf.endptr);
  return NO_ERROR;
}

static int
internal_lob_read_manifest (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator,
			    std::vector<INTERNAL_LOB_SEGMENT> &segments, DB_BIGINT &total_length)
{
  std::vector<char> manifest;
  OR_BUF buf;
  DB_BIGINT decoded_total_length;
  DB_BIGINT segment_length_sum = 0;
  int manifest_length;
  int magic;
  int count;
  int rc = NO_ERROR;
  int err;

  if (!locator.is_manifest || locator.length < 0 || OID_ISNULL (&locator.oid))
    {
      return internal_lob_set_generic_error ();
    }

  manifest_length = oos_get_length (thread_p, locator.oid);
  if (manifest_length < INTERNAL_LOB_MANIFEST_HEADER_SIZE)
    {
      if (manifest_length >= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  if ((manifest_length - INTERNAL_LOB_MANIFEST_HEADER_SIZE) % INTERNAL_LOB_MANIFEST_SEGMENT_SIZE != 0)
    {
      return internal_lob_set_generic_error ();
    }

  manifest.resize ((std::size_t) manifest_length);
  err = oos_read (thread_p, locator.oid, oos_buffer (manifest.data (), manifest.size ()));
  if (err != NO_ERROR)
    {
      return err;
    }

  or_init (&buf, manifest.data (), manifest_length);
  magic = or_get_int (&buf, &rc);
  if (rc != NO_ERROR || magic != INTERNAL_LOB_MANIFEST_MAGIC)
    {
      return internal_lob_set_generic_error ();
    }

  decoded_total_length = or_get_bigint (&buf, &rc);
  if (rc != NO_ERROR || decoded_total_length != locator.length || decoded_total_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  count = or_get_int (&buf, &rc);
  if (rc != NO_ERROR || count <= 0
      || manifest_length != INTERNAL_LOB_MANIFEST_HEADER_SIZE + count * INTERNAL_LOB_MANIFEST_SEGMENT_SIZE)
    {
      return internal_lob_set_generic_error ();
    }

  segments.clear ();
  segments.reserve ((std::size_t) count);
  for (int i = 0; i < count; i++)
    {
      INTERNAL_LOB_SEGMENT segment;

      err = or_get_oid (&buf, &segment.oid);
      if (err != NO_ERROR)
	{
	  return err;
	}
      segment.length = or_get_int (&buf, &rc);
      if (rc != NO_ERROR || segment.length <= 0 || OID_ISNULL (&segment.oid))
	{
	  return internal_lob_set_generic_error ();
	}
      if (segment_length_sum > DB_BIGINT_MAX - (DB_BIGINT) segment.length)
	{
	  return internal_lob_set_generic_error ();
	}
      segment_length_sum += (DB_BIGINT) segment.length;
      segments.push_back (segment);
    }

  if (buf.ptr != buf.endptr || segment_length_sum != decoded_total_length)
    {
      return internal_lob_set_generic_error ();
    }

  total_length = decoded_total_length;
  return NO_ERROR;
}

static int
internal_lob_reader_open_current_segment (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader)
{
  if (reader.current_segment >= (int) reader.segments.size ())
    {
      return NO_ERROR;
    }

  reader.current_segment_read = 0;
  return oos_read_open (thread_p, reader.segments[static_cast<std::size_t> (reader.current_segment)].oid,
			reader.oos_reader);
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
  int err;

  OID_SET_NULL (&locator.oid);
  locator.length = (DB_BIGINT) src.size ();
  locator.bit_length = -1;
  locator.is_manifest = false;

  if (src.size () == 0)
    {
      return NO_ERROR;
    }

  err = oos_insert (thread_p, lob_vfid, src, locator.oid);
  if (err != NO_ERROR)
    {
      return err;
    }

  return NO_ERROR;
}

int
internal_lob_insert_begin (THREAD_ENTRY *thread_p, const VFID &lob_vfid, INTERNAL_LOB_WRITER &writer)
{
  int segment_size;
  int err;

  (void) thread_p;

  internal_lob_writer_clear (writer);

  err = internal_lob_get_segment_size (segment_size);
  if (err != NO_ERROR)
    {
      return err;
    }

  writer.segment_buffer = (char *) db_private_alloc (NULL, (size_t) segment_size);
  if (writer.segment_buffer == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  writer.lob_vfid = lob_vfid;
  writer.segment_size = segment_size;
  writer.segment_buffer_length = 0;
  writer.total_length = 0;
  writer.segments.clear ();
  return NO_ERROR;
}

int
internal_lob_insert_append (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer, oos_buffer chunk)
{
  std::size_t consumed = 0;
  int err;

  if (writer.segment_buffer == NULL || writer.segment_size <= 0 || writer.segment_buffer_length < 0
      || writer.segment_buffer_length > writer.segment_size || (chunk.data () == NULL && chunk.size () > 0))
    {
      internal_lob_writer_clear (writer);
      return internal_lob_set_generic_error ();
    }

  while (consumed < chunk.size ())
    {
      const int available = writer.segment_size - writer.segment_buffer_length;
      const std::size_t remaining = chunk.size () - consumed;
      const std::size_t to_copy = (remaining < (std::size_t) available) ? remaining : (std::size_t) available;

      if (to_copy == 0)
	{
	  err = internal_lob_flush_segment (thread_p, writer);
	  if (err != NO_ERROR)
	    {
	      internal_lob_writer_clear (writer);
	      return err;
	    }
	  continue;
	}

      if (writer.total_length > DB_BIGINT_MAX - (DB_BIGINT) to_copy)
	{
	  internal_lob_writer_clear (writer);
	  return internal_lob_set_generic_error ();
	}

      std::memcpy (writer.segment_buffer + writer.segment_buffer_length, chunk.data () + consumed, to_copy);
      writer.segment_buffer_length += (int) to_copy;
      writer.total_length += (DB_BIGINT) to_copy;
      consumed += to_copy;

      if (writer.segment_buffer_length == writer.segment_size)
	{
	  err = internal_lob_flush_segment (thread_p, writer);
	  if (err != NO_ERROR)
	    {
	      internal_lob_writer_clear (writer);
	      return err;
	    }
	}
    }

  return NO_ERROR;
}

int
internal_lob_insert_end (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer, INTERNAL_LOB_LOCATOR &locator)
{
  std::vector<char> manifest;
  OID manifest_oid;
  int err;

  OID_SET_NULL (&locator.oid);
  locator.length = 0;
  locator.bit_length = -1;
  locator.is_manifest = false;

  if (writer.segment_buffer == NULL || writer.segment_size <= 0)
    {
      internal_lob_writer_clear (writer);
      return internal_lob_set_generic_error ();
    }

  err = internal_lob_flush_segment (thread_p, writer);
  if (err != NO_ERROR)
    {
      internal_lob_writer_clear (writer);
      return err;
    }

  locator.length = writer.total_length;

  if (writer.segments.empty ())
    {
      internal_lob_writer_clear (writer);
      return NO_ERROR;
    }

  if (writer.segments.size () == 1)
    {
      locator.oid = writer.segments[0].oid;
      internal_lob_writer_clear (writer);
      return NO_ERROR;
    }

  err = internal_lob_serialize_manifest (writer.segments, writer.total_length, manifest);
  if (err != NO_ERROR)
    {
      internal_lob_writer_clear (writer);
      return err;
    }

  err = oos_insert (thread_p, writer.lob_vfid, oos_buffer (manifest.data (), manifest.size ()), manifest_oid);
  if (err != NO_ERROR)
    {
      internal_lob_writer_clear (writer);
      return err;
    }

  locator.oid = manifest_oid;
  locator.is_manifest = true;
  internal_lob_writer_clear (writer);
  return NO_ERROR;
}

int
internal_lob_read (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, oos_buffer dest)
{
  if (locator.length != (DB_BIGINT) dest.size ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  if (dest.size () == 0)
    {
      return NO_ERROR;
    }

  INTERNAL_LOB_READER reader;
  int err = internal_lob_read_open (thread_p, locator, reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  std::size_t total_read = 0;
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
internal_lob_read_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, INTERNAL_LOB_READER &reader)
{
  int err;

  reader = INTERNAL_LOB_READER ();
  reader.total_length = locator.length;

  if (locator.length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (locator.length == 0)
    {
      return NO_ERROR;
    }

  if (OID_ISNULL (&locator.oid))
    {
      return internal_lob_set_generic_error ();
    }

  if (locator.is_manifest)
    {
      DB_BIGINT total_length = 0;
      err = internal_lob_read_manifest (thread_p, locator, reader.segments, total_length);
      if (err != NO_ERROR)
	{
	  return err;
	}
      reader.total_length = total_length;
    }
  else
    {
      INTERNAL_LOB_SEGMENT segment;

      if (locator.length > (DB_BIGINT) INT_MAX)
	{
	  return internal_lob_set_generic_error ();
	}

      segment.oid = locator.oid;
      segment.length = (int) locator.length;
      reader.segments.push_back (segment);
    }

  reader.current_segment = 0;
  return internal_lob_reader_open_current_segment (thread_p, reader);
}

int
internal_lob_read_pull (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, oos_buffer dest, int &nread)
{
  nread = 0;

  if (dest.size () == 0 || reader.total_read >= reader.total_length)
    {
      return NO_ERROR;
    }

  while ((std::size_t) nread < dest.size () && nread < INT_MAX
	 && reader.current_segment < (int) reader.segments.size ())
    {
      INTERNAL_LOB_SEGMENT &segment = reader.segments[static_cast<std::size_t> (reader.current_segment)];
      const int segment_remaining = segment.length - reader.current_segment_read;
      const std::size_t dest_remaining = dest.size () - (std::size_t) nread;
      int pull_size;
      int pulled = 0;
      int err;

      if (segment_remaining < 0)
	{
	  return internal_lob_set_generic_error ();
	}
      if (segment_remaining == 0)
	{
	  if (!OID_ISNULL (&reader.oos_reader.current))
	    {
	      return internal_lob_set_generic_error ();
	    }
	  reader.current_segment++;
	  if (reader.current_segment < (int) reader.segments.size ())
	    {
	      err = internal_lob_reader_open_current_segment (thread_p, reader);
	      if (err != NO_ERROR)
		{
		  return err;
		}
	    }
	  continue;
	}

      pull_size = (dest_remaining > (std::size_t) INT_MAX) ? INT_MAX : (int) dest_remaining;
      if (pull_size > segment_remaining)
	{
	  pull_size = segment_remaining;
	}

      err = oos_read_pull (thread_p, reader.oos_reader,
			   dest.subspan ((std::size_t) nread, (std::size_t) pull_size), pulled);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (pulled <= 0)
	{
	  return internal_lob_set_generic_error ();
	}

      reader.current_segment_read += pulled;
      reader.total_read += (DB_BIGINT) pulled;
      nread += pulled;

      if (reader.current_segment_read == segment.length)
	{
	  if (!OID_ISNULL (&reader.oos_reader.current))
	    {
	      return internal_lob_set_generic_error ();
	    }
	  reader.current_segment++;
	  if (reader.current_segment < (int) reader.segments.size ())
	    {
	      err = internal_lob_reader_open_current_segment (thread_p, reader);
	      if (err != NO_ERROR)
		{
		  return err;
		}
	    }
	}
    }

  return NO_ERROR;
}

int
internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator)
{
  if (locator.length == 0 && OID_ISNULL (&locator.oid))
    {
      return NO_ERROR;
    }

  if (locator.is_manifest)
    {
      std::vector<INTERNAL_LOB_SEGMENT> segments;
      DB_BIGINT total_length;
      int err = internal_lob_read_manifest (thread_p, locator, segments, total_length);
      if (err != NO_ERROR)
	{
	  return err;
	}

      for (const INTERNAL_LOB_SEGMENT &segment : segments)
	{
	  err = oos_delete (thread_p, lob_vfid, segment.oid);
	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	}
    }

  return oos_delete (thread_p, lob_vfid, locator.oid);
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
  long long bit_length = -1;
  int consumed = 0;
  int bit_length_consumed = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_LOCATOR_PREFIX);
  int marker_len = 0;
  bool is_manifest = false;
  char *oid_part;

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
  if (oid_part[0] == 'M' && oid_part[1] == ':')
    {
      is_manifest = true;
      marker_len = 2;
      oid_part += marker_len;
    }

  if (sscanf (oid_part, "%d|%d|%d:%lld%n", &volid, &pageid, &slotid, &length, &consumed) != 4)
    {
      return false;
    }
  if (length < 0)
    {
      return false;
    }
  if (prefix_len + marker_len + consumed != size)
    {
      if (oid_part[consumed] != ':'
	  || sscanf (oid_part + consumed + 1, "%lld%n", &bit_length, &bit_length_consumed) != 1
	  || bit_length < 0 || prefix_len + marker_len + consumed + 1 + bit_length_consumed != size)
	{
	  return false;
	}
    }

  if (locator != NULL)
    {
      locator->oid.volid = (VOLID) volid;
      locator->oid.pageid = (PAGEID) pageid;
      locator->oid.slotid = (PGSLOTID) slotid;
      locator->length = (DB_BIGINT) length;
      locator->bit_length = (DB_BIGINT) bit_length;
      locator->is_manifest = is_manifest;
    }
  return true;
}

bool
internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;

  if (value == NULL || DB_IS_NULL (value))
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

int
internal_lob_encode_disk_length (const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT &disk_length)
{
  int bit_remainder;
  DB_BIGINT encoded;

  if (locator.length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (internal_lob_get_bit_remainder (locator, bit_remainder) != NO_ERROR)
    {
      return ER_GENERIC_ERROR;
    }

  if (!locator.is_manifest && bit_remainder == 0)
    {
      disk_length = locator.length;
      return NO_ERROR;
    }

  /* Keep the heap inline locator at OR_OOS_INLINE_SIZE.  Non-negative values are the legacy raw-byte length.
   * Negative values pack raw-byte length, manifest flag and BLOB tail bit count:
   *   -((length * 18) + (manifest ? 9 : 0) + bit_remainder + 1)
   */
  if (locator.length > (DB_BIGINT_MAX - 17) / 18)
    {
      return internal_lob_set_generic_error ();
    }

  encoded = locator.length * 18 + (locator.is_manifest ? 9 : 0) + bit_remainder;
  disk_length = -encoded - 1;
  return NO_ERROR;
}

int
internal_lob_decode_disk_length (INTERNAL_LOB_LOCATOR &locator, DB_BIGINT disk_length)
{
  DB_BIGINT encoded;
  int bit_remainder;

  locator.is_manifest = false;
  locator.bit_length = -1;

  if (disk_length >= 0)
    {
      locator.length = disk_length;
      return NO_ERROR;
    }

  if (disk_length == DB_BIGINT_MIN)
    {
      return internal_lob_set_generic_error ();
    }

  encoded = -disk_length - 1;
  bit_remainder = (int) (encoded % 9);
  encoded /= 9;
  locator.is_manifest = (encoded % 2) != 0;
  locator.length = encoded / 2;

  if (bit_remainder != 0)
    {
      if (locator.length <= 0 || locator.length > DB_BIGINT_MAX / 8)
	{
	  return internal_lob_set_generic_error ();
	}
      locator.bit_length = (locator.length - 1) * 8 + bit_remainder;
    }

  return NO_ERROR;
}

int
internal_lob_make_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator)
{
  char stack_buf[128];
  char *locator_buf = NULL;
  int locator_len;
  int err;

  locator_len = internal_lob_format_locator (locator, stack_buf, sizeof (stack_buf));
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
  return NO_ERROR;
}

int
internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
			    DB_VALUE *value, TP_DOMAIN *domain)
{
  char *raw_value = NULL;
  int raw_length;
  int precision;
  int err;

  if (locator.length < 0 || locator.length > (DB_BIGINT) INT_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  raw_length = (int) locator.length;
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
  else if (lob_type == DB_TYPE_BLOB)
    {
      DB_BIGINT bit_length = (locator.bit_length >= 0) ? locator.bit_length : (DB_BIGINT) raw_length * 8;

      if (bit_length < 0 || bit_length > (DB_BIGINT) INT_MAX)
	{
	  db_private_free_and_init (NULL, raw_value);
	  return internal_lob_set_generic_error ();
	}
      err = db_make_blob (value, precision, (DB_CONST_C_BIT) raw_value, (int) bit_length);
    }
  else
    {
      err = internal_lob_set_generic_error ();
    }

  if (err != NO_ERROR)
    {
      db_private_free_and_init (NULL, raw_value);
      return err;
    }

  value->need_clear = true;
  return NO_ERROR;
}
