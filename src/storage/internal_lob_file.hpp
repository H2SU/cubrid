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

#include "dbtype_def.h"
#include "oos_file.hpp"
#include "object_domain.h"

struct internal_lob_locator
{
  OID oid;
  DB_BIGINT length;
};
using INTERNAL_LOB_LOCATOR = struct internal_lob_locator;

#define INTERNAL_LOB_LOCATOR_PREFIX "@internal_lob:"

extern int internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid);
extern int internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid);
extern int internal_lob_insert (THREAD_ENTRY *thread_p, const VFID &lob_vfid, oos_buffer src,
                                INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_read (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, oos_buffer dest);
extern int internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_get_length (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator);

extern bool internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator);
extern bool internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator);
extern int internal_lob_make_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator);
extern int internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
                                       DB_VALUE *value, TP_DOMAIN *domain);

#endif /* _INTERNAL_LOB_FILE_HPP_ */
