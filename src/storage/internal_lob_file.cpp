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

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int
internal_lob_format_locator (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size)
{
  return snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%d|%d|%d:%lld", (int) locator.oid.volid,
                   (int) locator.oid.pageid, (int) locator.oid.slotid, (long long) locator.length);
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

  err = oos_insert (thread_p, lob_vfid, src, locator.oid);
  if (err != NO_ERROR)
    {
      return err;
    }

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

  return oos_read (thread_p, locator.oid, dest);
}

int
internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator)
{
  return oos_delete (thread_p, lob_vfid, locator.oid);
}

int
internal_lob_get_length (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator)
{
  (void) thread_p;
  return (int) locator.length;
}

bool
internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator)
{
  char locator_buf[128];
  int volid, pageid, slotid;
  long long length;
  int consumed = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_LOCATOR_PREFIX);

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

  if (sscanf (locator_buf + prefix_len, "%d|%d|%d:%lld%n", &volid, &pageid, &slotid, &length, &consumed) != 4)
    {
      return false;
    }
  if (prefix_len + consumed != size || length < 0)
    {
      return false;
    }

  if (locator != NULL)
    {
      locator->oid.volid = (VOLID) volid;
      locator->oid.pageid = (PAGEID) pageid;
      locator->oid.slotid = (PGSLOTID) slotid;
      locator->length = (DB_BIGINT) length;
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
  char *disk_value = NULL;
  OR_BUF buf;
  const PR_TYPE *pr_type;
  TP_DOMAIN *read_domain;
  int err;

  if (locator.length <= 0 || locator.length > (DB_BIGINT) INT_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  disk_value = (char *) db_private_alloc (NULL, (size_t) locator.length);
  if (disk_value == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  err = internal_lob_read (thread_p, locator, oos_buffer (disk_value, (std::size_t) locator.length));
  if (err != NO_ERROR)
    {
      db_private_free_and_init (NULL, disk_value);
      return err;
    }

  pr_type = pr_type_from_id (lob_type);
  read_domain = (domain != NULL) ? domain : tp_domain_resolve_default (lob_type);
  if (pr_type == NULL || read_domain == NULL)
    {
      db_private_free_and_init (NULL, disk_value);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  or_init (&buf, disk_value, (int) locator.length);
  err = pr_type->data_readval (&buf, value, read_domain, (int) locator.length, true, NULL, 0);
  db_private_free_and_init (NULL, disk_value);

  return err;
}
