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

#include "internal_lob_file.hpp"
#include "object_primitive.h"
#include "test_oos_sql_common.hpp"

class OosSqlInternalLobLocator : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_internal_lob_locator");
      db_commit_transaction ();
    }
    void TearDown () override
    {
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

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
