/*
 * Copyright 2016 CUBRID Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gtest/gtest.h"

#include "dbtype_function.h"
#include "error_manager.h"
#include "internal_lob_marker.h"
#include "internal_lob_dml_session.hpp"
#include "network_interface_cl.h"
#include "object_representation.h"
#include "object_primitive.h"

#include <cstring>
#include <string>

struct dml_context_trace
{
  int slot = -1;
  int consumed_slot = -1;
  std::string bytes;
  bool finished = false;
  bool aborted = false;
  bool destroyed = false;
};

class fake_internal_lob_dml_context : public internal_lob_dml_context
{
  public:
    explicit fake_internal_lob_dml_context (dml_context_trace &trace)
      : m_trace (trace)
    {
    }

    ~fake_internal_lob_dml_context () override
    {
      m_trace.destroyed = true;
    }

    int receive_lob_chunk (THREAD_ENTRY *, int slot, DB_BIGINT offset, const char *data, int data_len) override
    {
      m_trace.slot = slot;
      EXPECT_EQ (offset, (DB_BIGINT) m_trace.bytes.size ());
      m_trace.bytes.append (data, (size_t) data_len);
      return NO_ERROR;
    }

    int consume_lob_slot (THREAD_ENTRY *, int slot, const OID *, DB_TYPE expected_type,
			  INTERNAL_LOB_LOCATOR &locator) override
    {
      m_trace.consumed_slot = slot;
      EXPECT_EQ (expected_type, DB_TYPE_CLOB);
      locator.length = (DB_BIGINT) m_trace.bytes.size ();
      return NO_ERROR;
    }

    int finish_dml (THREAD_ENTRY *, std::int64_t &affected_rows) override
    {
      m_trace.finished = true;
      affected_rows = 7;
      return NO_ERROR;
    }

    void abort_dml (THREAD_ENTRY *) override
    {
      m_trace.aborted = true;
    }

  private:
    dml_context_trace &m_trace;
};

class fake_staging_dml_context : public internal_lob_dml_staging_context
{
  public:
    explicit fake_staging_dml_context (const std::vector<internal_lob_dml_slot_config> &slot_configs)
      : internal_lob_dml_staging_context (slot_configs)
    {
    }

    bool executed = false;

  protected:
    int execute_dml (THREAD_ENTRY *, std::int64_t &affected_rows) override
    {
      executed = true;
      affected_rows = 11;
      return NO_ERROR;
    }
};

TEST (InternalLobDmlSession, RoutesFramedChunkAndReturnsAffectedRows)
{
  dml_context_trace trace;
  stream_result result = { 0 };
  char frame[INTERNAL_LOB_DML_FRAME_HEADER_SIZE + 3];
  INT64 offset = 0;

  OR_PUT_INT (frame, 1);
  OR_PUT_INT64 (frame + OR_INT_SIZE, &offset);
  memcpy (frame + INTERNAL_LOB_DML_FRAME_HEADER_SIZE, "lob", 3);

  {
    internal_lob_dml_session session;
    ASSERT_EQ (session.init (new fake_internal_lob_dml_context (trace), 2), NO_ERROR);
    ASSERT_EQ (session.receive_chunk (NULL, frame, sizeof (frame)), NO_ERROR);
    INTERNAL_LOB_LOCATOR locator;
    ASSERT_EQ (session.consume_lob_slot (NULL, 1, NULL, DB_TYPE_CLOB, locator), NO_ERROR);
    ASSERT_EQ (session.finish (NULL, &result), NO_ERROR);

    EXPECT_EQ (trace.slot, 1);
    EXPECT_EQ (trace.consumed_slot, 1);
    EXPECT_EQ (trace.bytes, "lob");
    EXPECT_EQ (locator.length, 3);
    EXPECT_TRUE (trace.finished);
    EXPECT_FALSE (trace.aborted);
    EXPECT_EQ (result.count, 7);
  }

  EXPECT_TRUE (trace.destroyed);
}

TEST (InternalLobDmlSession, RejectsUnknownSlotAndAbortsOwnedDml)
{
  dml_context_trace trace;
  char frame[INTERNAL_LOB_DML_FRAME_HEADER_SIZE];
  INT64 offset = 0;

  er_clear ();
  OR_PUT_INT (frame, 2);
  OR_PUT_INT64 (frame + OR_INT_SIZE, &offset);

  {
    internal_lob_dml_session session;
    ASSERT_EQ (session.init (new fake_internal_lob_dml_context (trace), 2), NO_ERROR);
    EXPECT_EQ (session.receive_chunk (NULL, frame, sizeof (frame)), ER_STREAM_SESSION_ERROR);
    session.abort (NULL);
    EXPECT_TRUE (trace.aborted);
    EXPECT_FALSE (trace.finished);
  }

  EXPECT_TRUE (trace.destroyed);
  er_clear ();
}

TEST (InternalLobDmlSession, ParsesOnlyExplicitlyMarkedDmlSlots)
{
  const char *slot_text = INTERNAL_LOB_DML_SLOT_PREFIX "3";
  DB_VALUE value;
  INTERNAL_LOB_DML_SLOT slot;

  db_make_blob (&value, DB_MAX_LOB_PRECISION, (DB_CONST_C_BIT) slot_text, (int) strlen (slot_text) * 8);
  EXPECT_FALSE (internal_lob_db_value_is_dml_slot (&value, &slot));
  pr_clear_value (&value);

  ASSERT_EQ (internal_lob_dml_make_slot_value (&value, DB_TYPE_BLOB, 3), NO_ERROR);
  ASSERT_TRUE (internal_lob_db_value_is_dml_slot (&value, &slot));
  EXPECT_EQ (slot.slot, 3);
  pr_clear_value (&value);
}

TEST (InternalLobDmlSession, StagingContextValidatesAllPayloadsBeforeDml)
{
  internal_lob_dml_slot_config config;
  std::vector<internal_lob_dml_slot_config> configs;
  std::int64_t affected_rows = 0;

  config.type = DB_TYPE_CLOB;
  config.data_length = 3;
  config.logical_length = 3;
  configs.push_back (config);

  fake_staging_dml_context complete (configs);
  ASSERT_EQ (complete.init (NULL), NO_ERROR);
  ASSERT_EQ (complete.receive_lob_chunk (NULL, 0, 0, "lob", 3), NO_ERROR);
  ASSERT_EQ (complete.finish_dml (NULL, affected_rows), NO_ERROR);
  EXPECT_TRUE (complete.executed);
  EXPECT_EQ (affected_rows, 11);

  fake_staging_dml_context incomplete (configs);
  ASSERT_EQ (incomplete.init (NULL), NO_ERROR);
  ASSERT_EQ (incomplete.receive_lob_chunk (NULL, 0, 0, "lo", 2), NO_ERROR);
  EXPECT_EQ (incomplete.finish_dml (NULL, affected_rows), ER_STREAM_SESSION_ERROR);
  EXPECT_FALSE (incomplete.executed);
  incomplete.abort_dml (NULL);
}

TEST (InternalLobDmlSession, AcceptsExactFourGiBMetadataAndRejectsLargerPayloads)
{
  internal_lob_dml_slot_config config;
  std::vector<internal_lob_dml_slot_config> configs;

  config.type = DB_TYPE_CLOB;
  config.data_length = DB_MAX_INTERNAL_LOB_LENGTH;
  config.logical_length = DB_MAX_INTERNAL_LOB_LENGTH;
  configs.push_back (config);

  fake_staging_dml_context exact_limit (configs);
  ASSERT_EQ (exact_limit.init (NULL), NO_ERROR);
  exact_limit.abort_dml (NULL);

  configs[0].data_length = DB_MAX_INTERNAL_LOB_LENGTH + 1;
  configs[0].logical_length = DB_MAX_INTERNAL_LOB_LENGTH + 1;
  fake_staging_dml_context above_limit (configs);
  EXPECT_EQ (above_limit.init (NULL), ER_STREAM_SESSION_ERROR);
  er_clear ();
}

TEST (InternalLobDmlSession, RejectsInvalidBlobBitLengthBeforeDml)
{
  internal_lob_dml_slot_config config;
  std::vector<internal_lob_dml_slot_config> configs;
  std::int64_t affected_rows = 0;

  config.type = DB_TYPE_BLOB;
  config.data_length = 2;
  config.logical_length = 8;
  configs.push_back (config);

  fake_staging_dml_context invalid_bits (configs);
  ASSERT_EQ (invalid_bits.init (NULL), NO_ERROR);
  ASSERT_EQ (invalid_bits.receive_lob_chunk (NULL, 0, 0, "ab", 2), NO_ERROR);
  EXPECT_EQ (invalid_bits.finish_dml (NULL, affected_rows), ER_STREAM_SESSION_ERROR);
  EXPECT_FALSE (invalid_bits.executed);
  invalid_bits.abort_dml (NULL);
  er_clear ();
}

int
main (int argc, char **argv)
{
  if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
    {
      return 1;
    }
  ::testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
