#!/usr/bin/env bash
#
#  Copyright 2026 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

set -euo pipefail

: "${CUBRID:=/home/heexoo/CUBRID}"
export CUBRID
export PATH="$CUBRID/bin:$PATH"
export LD_LIBRARY_PATH="$CUBRID/lib:${LD_LIBRARY_PATH:-}"

ROOT=${1:-/home/heexoo/tmp/cbrd26914_large_lob_csql_stream_$(date +%Y%m%d_%H%M%S)}
LOGDIR="$ROOT/logs"
mkdir -p "$LOGDIR"
SUMMARY="$LOGDIR/summary.log"
: > "$SUMMARY"
RUN_ID=${CBRD26914_RUN_ID:-$$}

log()
{
  printf '[%(%F %T)T] %s\n' -1 "$*" | tee -a "$SUMMARY"
}

run_case()
{
  local label="$1"
  local lob_kind="$2"
  local size="$3"
  local db_base="$4"
  local db="${db_base}_${RUN_ID}"
  local dbdir="$ROOT/$db"
  local src="$ROOT/${label}_${lob_kind}.dat"
  local sql="$LOGDIR/${label}_${lob_kind}.sql"
  local insert_log="$LOGDIR/${label}_${lob_kind}_insert.out"
  local insert_err="$LOGDIR/${label}_${lob_kind}_insert.err"
  local length_err="$LOGDIR/${label}_${lob_kind}_length.err"
  local select_err="$LOGDIR/${label}_${lob_kind}_select.err"
  local expected_stream_count expected_length length_value stream_count start_time end_time

  rm -rf "$dbdir" "$src"
  mkdir -p "$dbdir"

  log "START $label $lob_kind size=$size db=$db"
  truncate -s "$size" "$src"
  (cd "$dbdir" && cubrid createdb --db-volume-size=512M --log-volume-size=512M "$db" en_US >/dev/null)

  if [[ "$lob_kind" == "clob" ]]; then
    cat > "$sql" <<SQL
CREATE TABLE t (id int, c clob);
INSERT INTO t VALUES (1, clob_from_file('$src'));
SQL
    expected_length=$size
    expected_stream_count=$((size + 2))
    start_time=$(date +%s)
    (cd "$dbdir" && csql -S -u dba -i "$sql" "$db" > "$insert_log" 2> "$insert_err")
    end_time=$(date +%s)
    log "INSERT_OK $label $lob_kind elapsed=$((end_time - start_time))s"

    length_value=$((cd "$dbdir" && csql -S -u dba -t -N -c "SELECT clob_length(c) FROM t WHERE id=1;" "$db" 2> "$length_err") | tr -d '[:space:]')
    stream_count=$((cd "$dbdir" && csql -S -u dba -t -N -c "SELECT clob_to_char(c) FROM t WHERE id=1;" "$db" 2> "$select_err") | wc -c)
  else
    cat > "$sql" <<SQL
CREATE TABLE t (id int, b blob);
INSERT INTO t VALUES (1, blob_from_file('$src'));
SQL
    expected_length=$((size * 8))
    expected_stream_count=$((size * 2 + 2))
    start_time=$(date +%s)
    (cd "$dbdir" && csql -S -u dba -i "$sql" "$db" > "$insert_log" 2> "$insert_err")
    end_time=$(date +%s)
    log "INSERT_OK $label $lob_kind elapsed=$((end_time - start_time))s"

    length_value=$((cd "$dbdir" && csql -S -u dba -t -N -c "SELECT blob_length(b) FROM t WHERE id=1;" "$db" 2> "$length_err") | tr -d '[:space:]')
    stream_count=$((cd "$dbdir" && csql -S -u dba -t -N -c "SELECT blob_to_bit(b) FROM t WHERE id=1;" "$db" 2> "$select_err") | wc -c)
  fi

  log "LENGTH_DONE $label $lob_kind length=$length_value expected=$expected_length"
  if [[ "$length_value" != "$expected_length" ]]; then
    log "FAIL_LENGTH $label $lob_kind length=$length_value expected=$expected_length"
    return 1
  fi

  log "STREAM_DONE $label $lob_kind count=$stream_count expected=$expected_stream_count"
  if [[ "$stream_count" != "$expected_stream_count" ]]; then
    log "FAIL_STREAM $label $lob_kind count=$stream_count expected=$expected_stream_count"
    return 1
  fi

  (cd "$dbdir" 2>/dev/null && cubrid deletedb "$db" >/dev/null 2>&1) || true
  rm -rf "$dbdir" "$src"
  log "PASS $label $lob_kind"
}

should_run_case()
{
  local label="$1"
  local lob_kind="$2"
  local filter="${CBRD26914_CASES:-}"

  if [[ -z "$filter" ]]; then
    return 0
  fi

  [[ ",$filter," == *",$label,"* || ",$filter," == *",$label:$lob_kind,"* ]]
}

should_run_case 2GiB clob && run_case 2GiB clob 2147483648 d2c
should_run_case 2GiB blob && run_case 2GiB blob 2147483648 d2b
should_run_case 4GiB_minus_1 clob && run_case 4GiB_minus_1 clob 4294967295 d4c
should_run_case 4GiB_minus_1 blob && run_case 4GiB_minus_1 blob 4294967295 d4b
log "ALL_PASS root=$ROOT"
