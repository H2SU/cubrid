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

/*
 * test_oos_sql_internal_lob_locator.cpp - Internal LOB locator PoC tests
 *
 * CBRD-26914
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "db_elo.h"
#include "elo.h"
#include "es_common.h"
#include "heap_file.h"
#include "internal_lob_file.hpp"
#include "internal_lob_marker.h"
#include "object_primitive.h"
#include "parser.h"
#include "string_opfunc.h"
#include "system_parameter.h"
#include "work_space.h"
#include "test_oos_sql_common.hpp"

class OosSqlInternalLobLocator : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_internal_lob_locator_copy");
      exec_sql ("DROP TABLE IF EXISTS t_internal_lob_locator");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_internal_lob_locator_copy");
      exec_sql ("DROP TABLE IF EXISTS t_internal_lob_locator");
      db_commit_transaction ();
    }
};

static int
fetch_internal_lob_pair (const char *sql, DB_VALUE *first, DB_VALUE *second)
{
  DB_QUERY_RESULT *result = nullptr;
  int rc;

  db_make_null (first);
  db_make_null (second);

  rc = exec_sql_with_result (sql, &result);
  if (rc < 0)
    {
      return rc;
    }
  if (result == nullptr)
    {
      return ER_FAILED;
    }

  rc = db_query_first_tuple (result);
  if (rc != DB_CURSOR_SUCCESS)
    {
      db_query_end (result);
      return ER_FAILED;
    }

  rc = db_query_get_tuple_value (result, 0, first);
  if (rc != NO_ERROR)
    {
      db_query_end (result);
      return rc;
    }

  rc = db_query_get_tuple_value (result, 1, second);
  db_query_end (result);
  return rc;
}

struct internal_lob_string_reader_context
{
  const std::string *payload = nullptr;
  std::size_t offset = 0;
};

static int
internal_lob_string_reader (void *ctx, char *buf, int buf_size, int *nread)
{
  internal_lob_string_reader_context *reader_ctx = (internal_lob_string_reader_context *) ctx;
  std::size_t remaining;
  std::size_t read_size;

  if (reader_ctx == nullptr || reader_ctx->payload == nullptr || buf == nullptr || buf_size < 0 || nread == nullptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  *nread = 0;
  if (reader_ctx->offset >= reader_ctx->payload->size () || buf_size == 0)
    {
      return NO_ERROR;
    }

  remaining = reader_ctx->payload->size () - reader_ctx->offset;
  read_size = remaining < (std::size_t) buf_size ? remaining : (std::size_t) buf_size;
  std::memcpy (buf, reader_ctx->payload->data () + reader_ctx->offset, read_size);
  reader_ctx->offset += read_size;
  *nread = (int) read_size;
  return NO_ERROR;
}

static std::string
internal_lob_test_path (const char *suffix)
{
  return std::string ("/tmp/cbrd26914_internal_lob_") + std::to_string ((long long) getpid ()) + "_" + suffix;
}

static void
write_test_file (const std::string &path, const std::string &payload)
{
  FILE *fp = fopen (path.c_str (), "wb");
  ASSERT_NE (fp, nullptr);
  if (!payload.empty ())
    {
      ASSERT_EQ (fwrite (payload.data (), 1, payload.size (), fp), payload.size ());
    }
  ASSERT_EQ (fclose (fp), 0);
}

static void
create_sparse_test_file (const std::string &path, long long size)
{
  FILE *fp = fopen (path.c_str (), "wb");
  ASSERT_NE (fp, nullptr);
  if (size > 0)
    {
      ASSERT_EQ (fseeko (fp, (off_t) size - 1, SEEK_SET), 0);
      ASSERT_NE (fputc ('\0', fp), EOF);
    }
  ASSERT_EQ (fclose (fp), 0);
}

static DB_VALUE
make_path_value (const std::string &path)
{
  DB_VALUE value;

  db_make_varchar (&value, DB_MAX_VARCHAR_PRECISION, path.c_str (), (int) path.size (), LANG_SYS_CODESET,
		   LANG_COLL_DEFAULT);
  return value;
}

static DB_VALUE
make_external_lob_value (DB_TYPE type, const std::string &locator, DB_BIGINT size)
{
  DB_ELO elo;
  DB_VALUE value;

  elo_init_structure (&elo);
  elo.type = ELO_FBO;
  elo.locator = const_cast<char *> (locator.c_str ());
  elo.es_type = es_get_type (locator.c_str ());
  elo.size = size;
  db_make_elo (&value, type, &elo);
  return value;
}

static void
delete_pending_lob_for_test (const INTERNAL_LOB_PENDING &pending)
{
  DB_ELO elo;

  elo_init_structure (&elo);
  elo.type = ELO_FBO;
  elo.locator = (char *) pending.locator;
  elo.es_type = es_get_type (pending.locator);
  EXPECT_EQ (db_elo_delete (&elo), NO_ERROR);
}

TEST_F (OosSqlInternalLobLocator, RawSelectReturnsLocators)
{
  int rc;
  DB_VALUE clob_value, blob_value;
  INTERNAL_LOB_LOCATOR clob_locator, blob_locator;

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_internal_lob_locator VALUES "
		 "(1, char_to_clob('hello internal lob'), bit_to_blob(X'ABCD'))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT c, b FROM t_internal_lob_locator WHERE id = 1", &clob_value, &blob_value);
  ASSERT_EQ (rc, NO_ERROR);

  EXPECT_EQ (DB_VALUE_DOMAIN_TYPE (&clob_value), DB_TYPE_CLOB);
  EXPECT_EQ (DB_VALUE_DOMAIN_TYPE (&blob_value), DB_TYPE_BLOB);
  EXPECT_TRUE (internal_lob_db_value_is_locator (&clob_value, &clob_locator));
  EXPECT_TRUE (internal_lob_db_value_is_locator (&blob_value, &blob_locator));
  EXPECT_FALSE (OID_ISNULL (&clob_locator.oid));
  EXPECT_FALSE (OID_ISNULL (&blob_locator.oid));
  EXPECT_GT (clob_locator.length, 0);
  EXPECT_GT (blob_locator.length, 0);

  pr_clear_value (&clob_value);
  pr_clear_value (&blob_value);
}

TEST_F (OosSqlInternalLobLocator, ConversionFunctionsReadActualData)
{
  int rc;
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_internal_lob_locator VALUES "
		 "(1, char_to_clob('hello internal lob'), bit_to_blob(X'ABCD'))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) "
				"FROM t_internal_lob_locator WHERE id = 1", &char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  EXPECT_STREQ (text, "hello internal lob");

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, 16);
  EXPECT_EQ ((unsigned char) bits[0], 0xab);
  EXPECT_EQ ((unsigned char) bits[1], 0xcd);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
}

TEST_F (OosSqlInternalLobLocator, InsertSelectCopiesLocatorPayload)
{
  int rc;
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("CREATE TABLE t_internal_lob_locator_copy (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_internal_lob_locator VALUES "
		 "(1, char_to_clob('ORIGINAL_CLOB_DATA'), bit_to_blob(X'ABCD'))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_internal_lob_locator_copy SELECT id, c, b FROM t_internal_lob_locator");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DROP TABLE t_internal_lob_locator");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) "
				"FROM t_internal_lob_locator_copy WHERE id = 1", &char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  EXPECT_STREQ (text, "ORIGINAL_CLOB_DATA");

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, 16);
  EXPECT_EQ ((unsigned char) bits[0], 0xab);
  EXPECT_EQ ((unsigned char) bits[1], 0xcd);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
}

TEST_F (OosSqlInternalLobLocator, DirectFromFileInsertUsesPendingMarkerAndStreamsPayload)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  std::string clob_path = internal_lob_test_path ("direct_clob.txt");
  std::string blob_path = internal_lob_test_path ("direct_blob.bin");
  std::string clob_payload (3000, 'p');
  std::string blob_payload;
  DB_VALUE path_value, pending_value;
  INTERNAL_LOB_PENDING pending;
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;
  int rc;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);

  for (std::size_t i = 0; i < clob_payload.size (); i++)
    {
      clob_payload[i] = (char) ('a' + (i % 26));
    }
  blob_payload.push_back ((char) 0xde);
  blob_payload.push_back ((char) 0xad);
  blob_payload.push_back ((char) 0xbe);
  blob_payload.push_back ((char) 0xef);

  write_test_file (clob_path, clob_payload);
  write_test_file (blob_path, blob_payload);

  path_value = make_path_value (clob_path);
  db_make_null (&pending_value);
  rc = db_clob_from_file_pending (&path_value, &pending_value);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_pending (&pending_value, &pending));
  EXPECT_EQ (pending.lob_type, DB_TYPE_CLOB);
  EXPECT_EQ (pending.size, (DB_BIGINT) clob_payload.size ());
  delete_pending_lob_for_test (pending);
  pr_clear_value (&pending_value);

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string insert_sql = "INSERT INTO t_internal_lob_locator VALUES (1, clob_from_file('" + clob_path
			   + "'), blob_from_file('" + blob_path + "'))";
  rc = exec_sql (insert_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) FROM t_internal_lob_locator WHERE id = 1",
				&char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  ASSERT_EQ (db_get_string_size (&char_value), (int) clob_payload.size ());
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), clob_payload);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) blob_payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
  std::remove (clob_path.c_str ());
  std::remove (blob_path.c_str ());
}

TEST_F (OosSqlInternalLobLocator, DirectFromFileUpdateUsesPendingMarkerAndStreamsPayload)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  std::string clob_path = internal_lob_test_path ("update_clob.txt");
  std::string blob_path = internal_lob_test_path ("update_blob.bin");
  std::string clob_payload (4096, 'u');
  std::string blob_payload;
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;
  int rc;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);

  for (std::size_t i = 0; i < clob_payload.size (); i++)
    {
      clob_payload[i] = (char) ('A' + (i % 26));
    }
  blob_payload.push_back ((char) 0xca);
  blob_payload.push_back ((char) 0xfe);
  blob_payload.push_back ((char) 0xba);
  blob_payload.push_back ((char) 0xbe);

  write_test_file (clob_path, clob_payload);
  write_test_file (blob_path, blob_payload);

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('before'), bit_to_blob(X'ABCD'))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string update_sql = "UPDATE t_internal_lob_locator SET c = clob_from_file('" + clob_path
			   + "'), b = blob_from_file('" + blob_path + "') WHERE id = 1";
  rc = exec_sql (update_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) FROM t_internal_lob_locator WHERE id = 1",
				&char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  ASSERT_EQ (db_get_string_size (&char_value), (int) clob_payload.size ());
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), clob_payload);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) blob_payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
  std::remove (clob_path.c_str ());
  std::remove (blob_path.c_str ());
}

TEST_F (OosSqlInternalLobLocator, DirectLobfileConversionStreamsPayload)
{
  std::string clob_path = internal_lob_test_path ("bridge_clob.txt");
  std::string blob_path = internal_lob_test_path ("bridge_blob.bin");
  std::string clob_payload = "bridge clob payload";
  std::string blob_payload ("\x01\x23\x45\x67", 4);
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;
  int rc;

  write_test_file (clob_path, clob_payload);
  write_test_file (blob_path, blob_payload);

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);

  std::string insert_sql = "INSERT INTO t_internal_lob_locator VALUES (1, cfile_to_clob(cfile_from_file('"
			   + clob_path + "')), bfile_to_blob(bfile_from_file('" + blob_path + "')))";
  rc = exec_sql (insert_sql.c_str ());
  ASSERT_GE (rc, 0);

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) "
				"FROM t_internal_lob_locator WHERE id = 1", &char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);
  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), clob_payload);
  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) blob_payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);
  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);

  std::string update_sql = "UPDATE t_internal_lob_locator SET c = cfile_to_clob(cfile_from_file('" + clob_path
			   + "')), b = bfile_to_blob(bfile_from_file('" + blob_path + "')) WHERE id = 1";
  rc = exec_sql (update_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) "
				"FROM t_internal_lob_locator WHERE id = 1", &char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);
  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), clob_payload);
  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) blob_payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);
  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);

  std::remove (clob_path.c_str ());
  std::remove (blob_path.c_str ());
}

TEST_F (OosSqlInternalLobLocator, StreamingFunctionsEnforceFourGiBPhysicalLimit)
{
  const DB_BIGINT limit = DB_MAX_INTERNAL_LOB_LENGTH;
  std::string locator = std::string (ES_LOCAL_PATH_PREFIX) + "/tmp/internal_lob_size_boundary";
  DB_VALUE source_value, result_value;
  INTERNAL_LOB_PENDING pending;
  int rc;

  source_value = make_external_lob_value (DB_TYPE_CFILE, locator, limit);
  db_make_null (&result_value);
  rc = db_cfile_to_clob_pending (&source_value, &result_value);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_pending (&result_value, &pending));
  EXPECT_EQ (pending.size, limit);
  EXPECT_FALSE (pending.delete_after_read);
  pr_clear_value (&result_value);

  source_value = make_external_lob_value (DB_TYPE_BFILE, locator, limit);
  db_make_null (&result_value);
  rc = db_bfile_to_blob_pending (&source_value, &result_value);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_pending (&result_value, &pending));
  EXPECT_EQ (pending.size, limit);
  EXPECT_FALSE (pending.delete_after_read);
  pr_clear_value (&result_value);

  source_value = make_external_lob_value (DB_TYPE_CFILE, locator, limit + 1);
  db_make_null (&result_value);
  rc = db_cfile_to_clob_pending (&source_value, &result_value);
  EXPECT_EQ (rc, ER_ES_GENERAL);
  EXPECT_FALSE (internal_lob_db_value_is_pending (&result_value, &pending));
  er_clear ();

  source_value = make_external_lob_value (DB_TYPE_BFILE, locator, limit + 1);
  db_make_null (&result_value);
  rc = db_bfile_to_blob_pending (&source_value, &result_value);
  EXPECT_EQ (rc, ER_ES_GENERAL);
  EXPECT_FALSE (internal_lob_db_value_is_pending (&result_value, &pending));
  er_clear ();
}

TEST_F (OosSqlInternalLobLocator, DirectSqlTextRejectsValuesBeyondStringLimit)
{
  PARSER_CONTEXT *parser = parser_create_parser ();
  PARSER_VARCHAR existing = { DB_MAX_STRING_LENGTH, { 0 } };

  ASSERT_NE (parser, nullptr);
  er_clear ();
  EXPECT_EQ (pt_append_bytes (parser, &existing, "x", 1), nullptr);
  EXPECT_EQ (er_errid (), ER_QPROC_STRING_SIZE_TOO_BIG);
  er_clear ();
  parser_free_parser (parser);
}

TEST_F (OosSqlInternalLobLocator, DirectFromFileOdkuUsesPendingMarkerAndStreamsPayload)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  std::string clob_path = internal_lob_test_path ("odku_clob.txt");
  std::string blob_path = internal_lob_test_path ("odku_blob.bin");
  std::string clob_payload (3072, 'o');
  std::string blob_payload;
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;
  int rc;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);

  for (std::size_t i = 0; i < clob_payload.size (); i++)
    {
      clob_payload[i] = (char) ('k' + (i % 13));
    }
  blob_payload.push_back ((char) 0x10);
  blob_payload.push_back ((char) 0x20);
  blob_payload.push_back ((char) 0x30);
  blob_payload.push_back ((char) 0x40);

  write_test_file (clob_path, clob_payload);
  write_test_file (blob_path, blob_payload);

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('before'), bit_to_blob(X'ABCD'))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string odku_sql = "INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('ignored'), bit_to_blob(X'00')) "
			 "ON DUPLICATE KEY UPDATE c = clob_from_file('" + clob_path
			 + "'), b = blob_from_file('" + blob_path + "')";
  rc = exec_sql (odku_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) FROM t_internal_lob_locator WHERE id = 1",
				&char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  ASSERT_EQ (db_get_string_size (&char_value), (int) clob_payload.size ());
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), clob_payload);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) blob_payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
  std::remove (clob_path.c_str ());
  std::remove (blob_path.c_str ());
}

TEST_F (OosSqlInternalLobLocator, UserPayloadLocatorPrefixDoesNotCollideWithInternalMarker)
{
  std::string payload = "@internal_lob:1|2|3:4:0000000000000001";
  std::string payload_hex;
  const char *hex = "0123456789ABCDEF";
  std::string insert_sql;
  DB_VALUE fake_value, char_value, bit_value;
  INTERNAL_LOB_LOCATOR fake_locator;
  const char *text;
  const char *bits;
  int bit_length = 0;
  int rc;

  rc = db_make_clob (&fake_value, DB_MAX_LOB_PRECISION, payload.c_str (), (int) payload.size ());
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (internal_lob_db_value_is_locator (&fake_value, &fake_locator));
  pr_clear_value (&fake_value);

  payload_hex.reserve (payload.size () * 2);
  for (std::size_t i = 0; i < payload.size (); i++)
    {
      unsigned char ch = (unsigned char) payload[i];

      payload_hex.push_back (hex[ch >> 4]);
      payload_hex.push_back (hex[ch & 0x0f]);
    }

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);

  insert_sql = "INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('" + payload
	       + "'), bit_to_blob(X'" + payload_hex + "'))";
  rc = exec_sql (insert_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) FROM t_internal_lob_locator WHERE id = 1",
				&char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  ASSERT_EQ (db_get_string_size (&char_value), (int) payload.size ());
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), payload);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, payload.data (), payload.size ()), 0);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
}

TEST_F (OosSqlInternalLobLocator, NormalPayloadHeaderBoundariesDoNotCollideWithInternalMarker)
{
  std::string clob_payload = "123456789012345";
  std::string blob_payload;
  std::string blob_hex;
  const char *hex = "0123456789ABCDEF";
  DB_VALUE char_value, bit_value;
  const char *text;
  const char *bits;
  int bit_length = 0;
  int rc;

  for (int i = 0; i < 40; i++)
    {
      blob_payload.push_back ((char) (i + 1));
    }

  blob_hex.reserve (blob_payload.size () * 2);
  for (std::size_t i = 0; i < blob_payload.size (); i++)
    {
      unsigned char ch = (unsigned char) blob_payload[i];

      blob_hex.push_back (hex[ch >> 4]);
      blob_hex.push_back (hex[ch & 0x0f]);
    }

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);

  std::string insert_sql = "INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('" + clob_payload
			   + "'), bit_to_blob(X'" + blob_hex + "'))";
  rc = exec_sql (insert_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) FROM t_internal_lob_locator WHERE id = 1",
				&char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  ASSERT_EQ (db_get_string_size (&char_value), (int) clob_payload.size ());
  EXPECT_EQ (std::string (text, (std::size_t) db_get_string_size (&char_value)), clob_payload);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, (int) blob_payload.size () * 8);
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
}

TEST_F (OosSqlInternalLobLocator, NonDirectLargeFromFileDoesNotReturnStreamingMarker)
{
  std::string clob_path = internal_lob_test_path ("large_non_direct_clob.dat");
  std::string blob_path = internal_lob_test_path ("large_non_direct_blob.dat");
  DB_VALUE path_value, result_value;
  INTERNAL_LOB_PENDING pending;
  INTERNAL_LOB_LOCATOR locator;
  int rc;

  create_sparse_test_file (clob_path, (long long) DB_MAX_LOB_PRECISION + 1LL);
  create_sparse_test_file (blob_path, ((long long) DB_MAX_LOB_PRECISION / 8LL) + 1LL);

  path_value = make_path_value (clob_path);
  db_make_null (&result_value);
  rc = db_clob_from_file (&path_value, &result_value);
  EXPECT_EQ (rc, ER_QPROC_STRING_SIZE_TOO_BIG);
  EXPECT_FALSE (internal_lob_db_value_is_pending (&result_value, &pending));
  EXPECT_FALSE (internal_lob_db_value_is_locator (&result_value, &locator));
  pr_clear_value (&result_value);
  er_clear ();

  path_value = make_path_value (blob_path);
  db_make_null (&result_value);
  rc = db_blob_from_file (&path_value, &result_value);
  EXPECT_EQ (rc, ER_QPROC_STRING_SIZE_TOO_BIG);
  EXPECT_FALSE (internal_lob_db_value_is_pending (&result_value, &pending));
  EXPECT_FALSE (internal_lob_db_value_is_locator (&result_value, &locator));
  pr_clear_value (&result_value);
  er_clear ();

  std::remove (clob_path.c_str ());
  std::remove (blob_path.c_str ());
}

TEST_F (OosSqlInternalLobLocator, CharToBlobBitLengthIsPreserved)
{
  int rc;
  DB_VALUE length_value, bit_value;
  const char *bits;
  int bit_length = 0;

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_internal_lob_locator VALUES (1, char_to_blob('binary-small'))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT blob_length(b), blob_to_bit(b) "
				"FROM t_internal_lob_locator WHERE id = 1", &length_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  ASSERT_EQ (DB_VALUE_DOMAIN_TYPE (&length_value), DB_TYPE_BIGINT);
  EXPECT_EQ (db_get_bigint (&length_value), 12);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, 12);

  pr_clear_value (&length_value);
  pr_clear_value (&bit_value);
}

TEST_F (OosSqlInternalLobLocator, SegmentedRawStorageRoundTrip)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  const int payload_size = 5000;
  std::string clob_payload ((std::size_t) payload_size, 'x');
  std::string blob_hex_payload;
  int rc;
  DB_VALUE clob_locator_value, blob_locator_value;
  DB_VALUE char_value, bit_value;
  INTERNAL_LOB_LOCATOR clob_locator, blob_locator;
  const char *text;
  const char *bits;
  int bit_length = 0;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);

  blob_hex_payload.reserve ((std::size_t) payload_size * 2);
  for (int i = 0; i < payload_size; i++)
    {
      blob_hex_payload += "AB";
    }

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string insert_sql = "INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('" + clob_payload
			   + "'), bit_to_blob(X'" + blob_hex_payload + "'))";
  rc = exec_sql (insert_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT c, b FROM t_internal_lob_locator WHERE id = 1", &clob_locator_value,
				&blob_locator_value);
  ASSERT_EQ (rc, NO_ERROR);

  ASSERT_TRUE (internal_lob_db_value_is_locator (&clob_locator_value, &clob_locator));
  ASSERT_TRUE (internal_lob_db_value_is_locator (&blob_locator_value, &blob_locator));
  EXPECT_EQ (clob_locator.length, (DB_BIGINT) payload_size);
  EXPECT_EQ (blob_locator.length, (DB_BIGINT) payload_size * 8);
  pr_clear_value (&clob_locator_value);
  pr_clear_value (&blob_locator_value);

  rc = fetch_internal_lob_pair ("SELECT clob_to_char(c), blob_to_bit(b) "
				"FROM t_internal_lob_locator WHERE id = 1", &char_value, &bit_value);
  ASSERT_EQ (rc, NO_ERROR);

  text = db_get_string (&char_value);
  ASSERT_NE (text, nullptr);
  ASSERT_EQ (db_get_string_size (&char_value), payload_size);
  EXPECT_EQ (std::string (text, (std::size_t) payload_size), clob_payload);

  bits = (const char *) db_get_bit (&bit_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, payload_size * 8);
  EXPECT_EQ (db_get_string_size (&bit_value), payload_size);
  for (int i = 0; i < payload_size; i += 997)
    {
      EXPECT_EQ ((unsigned char) bits[i], 0xab);
    }

  pr_clear_value (&char_value);
  pr_clear_value (&bit_value);
}

TEST_F (OosSqlInternalLobLocator, StreamedAdoptLocatorPreservesPayload)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  const int payload_size = 4097;
  std::string clob_payload ((std::size_t) payload_size, 's');
  std::string blob_payload;
  DB_OBJECT *class_obj;
  OID *class_oid;
  INTERNAL_LOB_LOCATOR streamed_clob_locator, adopted_clob_locator, parsed_locator;
  INTERNAL_LOB_LOCATOR streamed_blob_locator, adopted_blob_locator;
  DB_VALUE adopt_value, materialized_value;
  internal_lob_string_reader_context reader_ctx;
  int rc;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);

  for (int i = 0; i < payload_size; i++)
    {
      clob_payload[ (std::size_t) i] = (char) ('a' + (i % 26));
    }

  blob_payload.push_back ((char) 0xab);
  blob_payload.push_back ((char) 0xcd);
  blob_payload.push_back ((char) 0x80);

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  class_obj = db_find_class ("t_internal_lob_locator");
  ASSERT_NE (class_obj, nullptr);
  class_oid = WS_OID (class_obj);
  ASSERT_NE (class_oid, nullptr);
  ASSERT_FALSE (OID_ISNULL (class_oid));

  reader_ctx.payload = &clob_payload;
  reader_ctx.offset = 0;
  rc = heap_internal_lob_insert_stream (thread_get_thread_entry_info (), class_oid, internal_lob_string_reader,
					&reader_ctx, -1, &streamed_clob_locator);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (streamed_clob_locator.length, (DB_BIGINT) clob_payload.size ());

  db_make_null (&adopt_value);
  rc = internal_lob_make_adopt_locator_db_value (&adopt_value, DB_TYPE_CLOB, streamed_clob_locator);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_locator (&adopt_value, &parsed_locator));
  ASSERT_TRUE (parsed_locator.adopted);

  rc = heap_internal_lob_insert_value (thread_get_thread_entry_info (), class_oid, &adopt_value, &adopted_clob_locator);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (OID_EQ (&adopted_clob_locator.oid, &streamed_clob_locator.oid));
  EXPECT_FALSE (adopted_clob_locator.adopted);
  pr_clear_value (&adopt_value);

  db_make_null (&materialized_value);
  rc = internal_lob_read_db_value (thread_get_thread_entry_info (), adopted_clob_locator, DB_TYPE_CLOB,
				   &materialized_value, NULL);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (db_get_string_size (&materialized_value), payload_size);
  ASSERT_EQ (std::memcmp (db_get_string (&materialized_value), clob_payload.data (), clob_payload.size ()), 0);
  pr_clear_value (&materialized_value);

  reader_ctx.payload = &blob_payload;
  reader_ctx.offset = 0;
  rc = heap_internal_lob_insert_stream (thread_get_thread_entry_info (), class_oid, internal_lob_string_reader,
					&reader_ctx, 17, &streamed_blob_locator);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (streamed_blob_locator.length, 17);

  db_make_null (&adopt_value);
  rc = internal_lob_make_adopt_locator_db_value (&adopt_value, DB_TYPE_BLOB, streamed_blob_locator);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_locator (&adopt_value, &parsed_locator));
  ASSERT_TRUE (parsed_locator.adopted);
  ASSERT_EQ (parsed_locator.length, 17);

  rc = heap_internal_lob_insert_value (thread_get_thread_entry_info (), class_oid, &adopt_value, &adopted_blob_locator);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (OID_EQ (&adopted_blob_locator.oid, &streamed_blob_locator.oid));
  EXPECT_FALSE (adopted_blob_locator.adopted);
  EXPECT_EQ (adopted_blob_locator.length, 17);
  pr_clear_value (&adopt_value);

  db_make_null (&materialized_value);
  rc = internal_lob_read_db_value (thread_get_thread_entry_info (), adopted_blob_locator, DB_TYPE_BLOB,
				   &materialized_value, NULL);
  ASSERT_EQ (rc, NO_ERROR);
  int bit_length = 0;
  const char *bits = (const char *) db_get_bit (&materialized_value, &bit_length);
  ASSERT_NE (bits, nullptr);
  EXPECT_EQ (bit_length, 17);
  ASSERT_EQ (db_get_string_size (&materialized_value), (int) blob_payload.size ());
  EXPECT_EQ (std::memcmp (bits, blob_payload.data (), blob_payload.size ()), 0);
  pr_clear_value (&materialized_value);
}

TEST_F (OosSqlInternalLobLocator, InvalidStreamedBlobBitLengthIsRejected)
{
  std::string blob_payload;
  DB_OBJECT *class_obj;
  OID *class_oid;
  INTERNAL_LOB_LOCATOR locator;
  internal_lob_string_reader_context reader_ctx;
  int rc;

  EXPECT_TRUE (internal_lob_is_valid_blob_bit_length (1, 1));
  EXPECT_TRUE (internal_lob_is_valid_blob_bit_length (1, 8));
  EXPECT_FALSE (internal_lob_is_valid_blob_bit_length (1, 0));
  EXPECT_FALSE (internal_lob_is_valid_blob_bit_length (1, 9));
  EXPECT_FALSE (internal_lob_is_valid_blob_bit_length (0, 1));

  blob_payload.push_back ((char) 0xff);

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  class_obj = db_find_class ("t_internal_lob_locator");
  ASSERT_NE (class_obj, nullptr);
  class_oid = WS_OID (class_obj);
  ASSERT_NE (class_oid, nullptr);
  ASSERT_FALSE (OID_ISNULL (class_oid));

  reader_ctx.payload = &blob_payload;
  reader_ctx.offset = 0;
  rc = heap_internal_lob_insert_stream (thread_get_thread_entry_info (), class_oid, internal_lob_string_reader,
					&reader_ctx, 9, &locator);
  EXPECT_NE (rc, NO_ERROR);
  er_clear ();
}

TEST_F (OosSqlInternalLobLocator, RangedReadReturnsRequestedSlices)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  const int payload_size = 4096;
  std::string clob_payload;
  std::string blob_hex_payload;
  int rc;
  DB_VALUE clob_locator_value, blob_locator_value;
  INTERNAL_LOB_LOCATOR clob_locator, blob_locator;
  std::vector<char> buffer (777);
  int nread = 0;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);

  clob_payload.reserve ((std::size_t) payload_size);
  blob_hex_payload.reserve ((std::size_t) payload_size * 2);
  for (int i = 0; i < payload_size; i++)
    {
      static const char hex[] = "0123456789ABCDEF";

      clob_payload.push_back ((char) ('a' + (i % 26)));
      blob_hex_payload.push_back (hex[ (i >> 4) & 0x0f]);
      blob_hex_payload.push_back (hex[i & 0x0f]);
    }

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB, b BLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string insert_sql = "INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('" + clob_payload
			   + "'), bit_to_blob(X'" + blob_hex_payload + "'))";
  rc = exec_sql (insert_sql.c_str ());
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_internal_lob_pair ("SELECT c, b FROM t_internal_lob_locator WHERE id = 1", &clob_locator_value,
				&blob_locator_value);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_locator (&clob_locator_value, &clob_locator));
  ASSERT_TRUE (internal_lob_db_value_is_locator (&blob_locator_value, &blob_locator));

  rc = internal_lob_read_range (thread_get_thread_entry_info (), clob_locator, 123,
				oos_buffer (buffer.data (), buffer.size ()), nread);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (nread, 777);
  EXPECT_EQ (std::string (buffer.data (), buffer.data () + nread), clob_payload.substr (123, (std::size_t) nread));

  rc = internal_lob_read_range (thread_get_thread_entry_info (), blob_locator, 2048,
				oos_buffer (buffer.data (), buffer.size ()), nread);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (nread, 777);
  for (int i = 0; i < nread; i++)
    {
      EXPECT_EQ ((unsigned char) buffer[ (std::size_t) i], (unsigned char) ((2048 + i) & 0xff));
    }

  rc = internal_lob_read_range (thread_get_thread_entry_info (), clob_locator, payload_size - 5,
				oos_buffer (buffer.data (), buffer.size ()), nread);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (nread, 5);
  EXPECT_EQ (std::string (buffer.data (), buffer.data () + nread), clob_payload.substr (payload_size - 5));

  rc = internal_lob_read_range (thread_get_thread_entry_info (), clob_locator, payload_size,
				oos_buffer (buffer.data (), buffer.size ()), nread);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (nread, 0);

  pr_clear_value (&clob_locator_value);
  pr_clear_value (&blob_locator_value);
}

/*
 * CLOB_LENGTH must report the same unit no matter whether the value still carries an Internal LOB locator or has
 * already been materialized.  The two branches of db_clob_length () used to disagree under a multi-byte codeset:
 * the locator carries a byte length while db_get_string_length () counts characters.
 */
TEST_F (OosSqlInternalLobLocator, ClobLengthUnitAgreesForLocatorAndMaterialized)
{
  /* six U+AC00..U+AC05 syllables: 6 characters, 18 bytes in UTF-8 */
  const char *utf8_text = "\xea\xb0\x80\xea\xb0\x81\xea\xb0\x82\xea\xb0\x83\xea\xb0\x84\xea\xb0\x85";
  char sql[512];
  int rc;
  int stored_length = 0;
  int inline_length = 0;

  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  snprintf (sql, sizeof (sql), "INSERT INTO t_internal_lob_locator VALUES (1, char_to_clob('%s'))", utf8_text);
  rc = exec_sql (sql);
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* read back from the heap: the value is still a locator here */
  rc = fetch_single_int ("SELECT CAST (clob_length (c) AS INT) FROM t_internal_lob_locator WHERE id = 1",
			 &stored_length);
  ASSERT_EQ (rc, NO_ERROR);

  /* never stored: the value is materialized in place */
  snprintf (sql, sizeof (sql),
	    "SELECT CAST (clob_length (char_to_clob ('%s')) AS INT) FROM t_internal_lob_locator WHERE id = 1",
	    utf8_text);
  rc = fetch_single_int (sql, &inline_length);
  ASSERT_EQ (rc, NO_ERROR);

  EXPECT_EQ (stored_length, inline_length);
}

/*
 * Internal LOB writes are resolved only by the server heap path (heap_internal_lob_insert_value ()).  When a trigger
 * on the table forces the client object path instead, the transport envelope used to be serialized as if it were the
 * user payload, storing "@internal_lob_pending:..." (source file path included) in place of the LOB.  The write must
 * be refused instead.  (CUBRID cannot reference the new record from BEFORE INSERT, so this uses AFTER INSERT.)
 */
TEST_F (OosSqlInternalLobLocator, ClientObjectPathRefusesUnresolvedInternalLob)
{
  struct segment_size_guard
  {
    ~segment_size_guard ()
    {
      prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 128ULL * 1024ULL * 1024ULL);
    }
  } guard;

  std::string clob_path = internal_lob_test_path ("trigger_clob.txt");
  std::string clob_payload (3000, 'q');
  int row_count = -1;
  int rc;

  prm_set_bigint_value (PRM_ID_INTERNAL_LOB_SEGMENT_SIZE, 1024ULL);
  for (std::size_t i = 0; i < clob_payload.size (); i++)
    {
      clob_payload[i] = (char) ('a' + (i % 26));
    }
  write_test_file (clob_path, clob_payload);

  exec_sql ("DROP TRIGGER trg_internal_lob_marker");
  exec_sql ("DROP TABLE IF EXISTS t_internal_lob_trigger_log");
  db_commit_transaction ();

  rc = exec_sql ("CREATE TABLE t_internal_lob_trigger_log (id INT AUTO_INCREMENT PRIMARY KEY, seen VARCHAR (128))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("CREATE TABLE t_internal_lob_locator (id INT PRIMARY KEY, c CLOB)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* the trigger is what pushes the statement onto the client object path */
  rc = exec_sql ("CREATE TRIGGER trg_internal_lob_marker AFTER INSERT ON t_internal_lob_locator "
		 "EXECUTE INSERT INTO t_internal_lob_trigger_log (seen) "
		 "VALUES (SUBSTRING (clob_to_char (obj.c), 1, 40))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql (("INSERT INTO t_internal_lob_locator VALUES (1, clob_from_file('" + clob_path + "'))").c_str ());
  EXPECT_LT (rc, 0) << "the unresolved Internal LOB envelope was accepted instead of being refused";
  EXPECT_EQ (er_errid (), ER_STREAM_SESSION_ERROR);
  db_abort_transaction ();

  /* nothing may be stored: neither a corrupted row nor a half-applied trigger effect */
  rc = fetch_single_int ("SELECT COUNT (*) FROM t_internal_lob_locator", &row_count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (row_count, 0);

  exec_sql ("DROP TRIGGER trg_internal_lob_marker");
  exec_sql ("DROP TABLE IF EXISTS t_internal_lob_trigger_log");
  db_commit_transaction ();
  std::remove (clob_path.c_str ());
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
