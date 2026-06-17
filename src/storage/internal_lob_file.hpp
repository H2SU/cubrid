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

#ifndef _INTERNAL_LOB_FILE_HPP_
#define _INTERNAL_LOB_FILE_HPP_

#include <limits.h>
#include <vector>

#include "dbtype_def.h"
#include "oos_file.hpp"
#include "object_domain.h"

struct internal_lob_locator
{
  OID oid;
  DB_BIGINT length;
  DB_BIGINT bit_length = -1;
  bool is_manifest = false;
  bool adopted = false;
};
using INTERNAL_LOB_LOCATOR = struct internal_lob_locator;

struct internal_lob_segment
{
  OID oid;
  int length;
};
using INTERNAL_LOB_SEGMENT = struct internal_lob_segment;

struct internal_lob_writer
{
  VFID lob_vfid;
  char *segment_buffer = NULL;
  int segment_size = 0;
  int segment_buffer_length = 0;
  DB_BIGINT total_length = 0;
  std::vector<INTERNAL_LOB_SEGMENT> segments;
};
using INTERNAL_LOB_WRITER = struct internal_lob_writer;

struct internal_lob_reader
{
  std::vector<INTERNAL_LOB_SEGMENT> segments;
  int current_segment = 0;
  int current_segment_read = 0;
  DB_BIGINT total_length = 0;
  DB_BIGINT total_read = 0;
  OOS_READER oos_reader;
};
using INTERNAL_LOB_READER = struct internal_lob_reader;

#define INTERNAL_LOB_LOCATOR_PREFIX "@internal_lob:"
#define INTERNAL_LOB_FILE_SOURCE_PREFIX "@internal_lob_file:"
#define INTERNAL_LOB_PENDING_PREFIX "@internal_lob_pending:"

struct internal_lob_pending
{
  DB_TYPE lob_type;
  DB_BIGINT size;
  char locator[PATH_MAX + 16];
};
using INTERNAL_LOB_PENDING = struct internal_lob_pending;

extern int internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid);
extern int internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid);
extern int internal_lob_insert (THREAD_ENTRY *thread_p, const VFID &lob_vfid, oos_buffer src,
				INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_insert_begin (THREAD_ENTRY *thread_p, const VFID &lob_vfid, INTERNAL_LOB_WRITER &writer);
extern int internal_lob_insert_append (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer, oos_buffer chunk);
extern int internal_lob_insert_end (THREAD_ENTRY *thread_p, INTERNAL_LOB_WRITER &writer,
				    INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_read (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, oos_buffer dest);
extern int internal_lob_read_range (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT offset,
				    oos_buffer dest, int &nread);
extern int internal_lob_read_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator,
				   INTERNAL_LOB_READER &reader);
extern int internal_lob_read_pull (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, oos_buffer dest, int &nread);
extern int internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_get_length (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator);

extern bool internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator);
extern bool internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator);
extern bool internal_lob_db_value_is_pending (const DB_VALUE *value, INTERNAL_LOB_PENDING *pending);
inline bool
internal_lob_is_valid_blob_bit_length (DB_BIGINT data_length, DB_BIGINT bit_length)
{
  if (data_length < 0 || bit_length < 0)
    {
      return false;
    }

  if (data_length == 0)
    {
      return bit_length == 0;
    }

  if (data_length > DB_BIGINT_MAX / 8)
    {
      return false;
    }

  return bit_length > (data_length - 1) * 8 && bit_length <= data_length * 8;
}
extern int internal_lob_encode_disk_length (const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT &disk_length);
extern int internal_lob_decode_disk_length (INTERNAL_LOB_LOCATOR &locator, DB_BIGINT disk_length);
extern int internal_lob_make_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_make_adopt_locator_db_value (DB_VALUE *value, DB_TYPE lob_type,
    const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
				       DB_VALUE *value, TP_DOMAIN *domain);

#endif /* _INTERNAL_LOB_FILE_HPP_ */
