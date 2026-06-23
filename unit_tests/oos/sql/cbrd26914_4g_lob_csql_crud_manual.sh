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

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
BUILD_DIR=${CBRD26914_BUILD_DIR:-$REPO_ROOT/build_preset_debug}
INSTALL_CUBRID=${CUBRID:-/home/heexoo/CUBRID}

: "${CUBRID:=$INSTALL_CUBRID}"
export CUBRID
export PATH="$BUILD_DIR/bin:$CUBRID/bin:$PATH"
export LD_LIBRARY_PATH="$BUILD_DIR/lib:$CUBRID/lib:${LD_LIBRARY_PATH:-}"

SIZE=${CBRD26914_SIZE:-4294967296}
ROOT=${1:-${CBRD26914_ROOT:-/home/heexoo/tmp/cbrd26914_4g_lob_csql_crud_$(date +%Y%m%d_%H%M%S)}}
LOGDIR="$ROOT/logs"
mkdir -p "$LOGDIR"
SUMMARY="$LOGDIR/summary.log"
: > "$SUMMARY"
RUN_ID=${CBRD26914_RUN_ID:-$$}
DB_SUFFIX=$(printf '%s' "$RUN_ID" | tr -cd '[:alnum:]' | tail -c 8)
if [[ -z "$DB_SUFFIX" ]]; then
  DB_SUFFIX=$$
fi
DB_VOLUME_SIZE=${CBRD26914_DB_VOLUME_SIZE:-512M}
LOG_VOLUME_SIZE=${CBRD26914_LOG_VOLUME_SIZE:-512M}
TAIL_BYTES=4
DENSE_SOURCE_LIMIT=${CBRD26914_DENSE_SOURCE_LIMIT:-1048576}

log()
{
  printf '[%(%F %T)T] %s\n' -1 "$*" | tee -a "$SUMMARY"
}

fail()
{
  log "FAIL $*"
  exit 1
}

make_file_with_tail_byte()
{
  local path="$1"
  local size="$2"
  local tail_hex="$3"
  local fill_hex="$4"

  rm -f "$path"
  if (( size <= DENSE_SOURCE_LIMIT )); then
    python3 - "$path" "$size" "$tail_hex" "$fill_hex" <<'PYGEN'
import sys

path = sys.argv[1]
size = int(sys.argv[2])
tail = int(sys.argv[3], 16)
fill = int(sys.argv[4], 16)
chunk = bytes([fill]) * 65536

with open(path, 'wb') as fp:
    remaining = size
    while remaining > 0:
        nbytes = min(remaining, len(chunk))
        fp.write(chunk[:nbytes])
        remaining -= nbytes
    if size > 0:
        fp.seek(size - 1)
        fp.write(bytes([tail]))
PYGEN
  else
    truncate -s "$size" "$path"
    if (( size > 0 )); then
      printf "\\x$tail_hex" | dd of="$path" bs=1 seek=$((size - 1)) conv=notrunc status=none
    fi
  fi
}

run_sql_file()
{
  local dbdir="$1"
  local db="$2"
  local sql="$3"
  local label="$4"
  local out="$LOGDIR/${label}.out"
  local err="$LOGDIR/${label}.err"

  if ! (cd "$dbdir" && csql -S -u dba -i "$sql" "$db" > "$out" 2> "$err"); then
    tail -80 "$out" "$err" | tee -a "$SUMMARY" || true
    fail "$label sql execution failed; see $out $err"
  fi
}

query_scalar()
{
  local dbdir="$1"
  local db="$2"
  local sql="$3"
  local label="$4"
  local err="$LOGDIR/${label}.err"
  local value

  value=$((cd "$dbdir" && csql -S -u dba -t -N -c "$sql" "$db" 2> "$err") | tr -d '[:space:]')
  printf '%s' "$value"
}

capture_stream_count_and_tail()
{
  local dbdir="$1"
  local db="$2"
  local sql="$3"
  local label="$4"
  local count_file="$LOGDIR/${label}.count"
  local tail_file="$LOGDIR/${label}.tail"
  local err="$LOGDIR/${label}.err"

  rm -f "$count_file" "$tail_file"
  if ! (cd "$dbdir" && csql -S -u dba -t -N -c "$sql" "$db" 2> "$err" \
       | tee >(tail -c "$TAIL_BYTES" > "$tail_file") \
       | wc -c > "$count_file"); then
    tail -80 "$err" | tee -a "$SUMMARY" || true
    fail "$label stream failed; see $err"
  fi
}

stream_count_value()
{
  tr -d '[:space:]' < "$1"
}

stream_tail_hex_value()
{
  od -An -tx1 -v "$1" | tr -d ' \n'
}

verify_value()
{
  local label="$1"
  local actual="$2"
  local expected="$3"

  log "VERIFY $label actual=$actual expected=$expected"
  if [[ "$actual" != "$expected" ]]; then
    fail "$label actual=$actual expected=$expected"
  fi
}

verify_tail_value()
{
  local label="$1"
  local actual="$2"
  local expected="$3"
  local expected_alt="${4:-}"

  if [[ -n "$expected_alt" ]]; then
    log "VERIFY $label actual=$actual expected=$expected or $expected_alt"
    if [[ "$actual" != "$expected" && "$actual" != "$expected_alt" ]]; then
      fail "$label actual=$actual expected=$expected or $expected_alt"
    fi
  else
    verify_value "$label" "$actual" "$expected"
  fi
}

verify_lob_state()
{
  local dbdir="$1"
  local db="$2"
  local lob_kind="$3"
  local phase="$4"
  local expected_tail_ascii_hex="$5"
  local length_sql stream_sql expected_length expected_count count_file tail_file count tail_hex length_value tail_alt

  if [[ "$lob_kind" == "clob" ]]; then
    length_sql="SELECT clob_length(c) FROM t WHERE id = 1;"
    stream_sql="SELECT clob_to_char(c) FROM t WHERE id = 1;"
    expected_length="$SIZE"
    expected_count=$((SIZE + 2))
  else
    length_sql="SELECT blob_length(b) FROM t WHERE id = 1;"
    stream_sql="SELECT blob_to_bit(b) FROM t WHERE id = 1;"
    expected_length=$((SIZE * 8))
    expected_count=$((SIZE * 2 + 2))
  fi

  length_value=$(query_scalar "$dbdir" "$db" "$length_sql" "${lob_kind}_${phase}_length")
  verify_value "$lob_kind $phase length" "$length_value" "$expected_length"

  capture_stream_count_and_tail "$dbdir" "$db" "$stream_sql" "${lob_kind}_${phase}_stream"
  count_file="$LOGDIR/${lob_kind}_${phase}_stream.count"
  tail_file="$LOGDIR/${lob_kind}_${phase}_stream.tail"
  count=$(stream_count_value "$count_file")
  tail_hex=$(stream_tail_hex_value "$tail_file")
  verify_value "$lob_kind $phase stream_count" "$count" "$expected_count"
  tail_alt=""
  if [[ "$lob_kind" == "blob" && "$expected_tail_ascii_hex" == "41420a0a" ]]; then
    tail_alt="61620a0a"
  elif [[ "$lob_kind" == "blob" && "$expected_tail_ascii_hex" == "43440a0a" ]]; then
    tail_alt="63640a0a"
  fi
  verify_tail_value "$lob_kind $phase stream_tail" "$tail_hex" "$expected_tail_ascii_hex" "$tail_alt"
}

cleanup_case()
{
  local dbdir="$1"
  local db="$2"
  shift 2

  (cd "$dbdir" 2>/dev/null && cubrid deletedb "$db" >/dev/null 2>&1) || true
  rm -rf "$dbdir" "$@"
}

run_insert_scenario()
{
  local lob_kind="$1"
  local db="i${lob_kind:0:1}${DB_SUFFIX}"
  local dbdir="$ROOT/$db"
  local src_insert="$ROOT/${lob_kind}_insert_4g.dat"
  local create_sql="$LOGDIR/${lob_kind}_insert_create.sql"
  local insert_sql="$LOGDIR/${lob_kind}_insert.sql"
  local delete_sql="$LOGDIR/${lob_kind}_insert_delete.sql"
  local start_time end_time row_count clob_insert_tail

  if [[ -d "$dbdir" ]]; then
    (cd "$dbdir" && cubrid deletedb "$db" >/dev/null 2>&1) || true
  fi
  rm -rf "$dbdir" "$src_insert"
  mkdir -p "$dbdir"
  (cd "$dbdir" && cubrid deletedb "$db" >/dev/null 2>&1) || true

  log "START_INSERT $lob_kind size=$SIZE db=$db root=$ROOT build=$BUILD_DIR cubrid=$CUBRID"

  if [[ "$lob_kind" == "clob" ]]; then
    make_file_with_tail_byte "$src_insert" "$SIZE" "41" "61"
  else
    make_file_with_tail_byte "$src_insert" "$SIZE" "ab" "00"
  fi

  (cd "$dbdir" && cubrid createdb --db-volume-size="$DB_VOLUME_SIZE" --log-volume-size="$LOG_VOLUME_SIZE" "$db" en_US \
    > "$LOGDIR/${lob_kind}_insert_createdb.out" 2> "$LOGDIR/${lob_kind}_insert_createdb.err") \
    || fail "$lob_kind insert createdb failed; see $LOGDIR/${lob_kind}_insert_createdb.err"

  if [[ "$lob_kind" == "clob" ]]; then
    cat > "$create_sql" <<SQL
CREATE TABLE t (id INT PRIMARY KEY, c CLOB);
SQL
    cat > "$insert_sql" <<SQL
INSERT INTO t VALUES (1, clob_from_file('$src_insert'));
SQL
  else
    cat > "$create_sql" <<SQL
CREATE TABLE t (id INT PRIMARY KEY, b BLOB);
SQL
    cat > "$insert_sql" <<SQL
INSERT INTO t VALUES (1, blob_from_file('$src_insert'));
SQL
  fi
  cat > "$delete_sql" <<SQL
DELETE FROM t WHERE id = 1;
SQL

  run_sql_file "$dbdir" "$db" "$create_sql" "${lob_kind}_insert_create"

  start_time=$(date +%s)
  run_sql_file "$dbdir" "$db" "$insert_sql" "${lob_kind}_insert"
  end_time=$(date +%s)
  log "INSERT_OK $lob_kind elapsed=$((end_time - start_time))s"

  if [[ "$lob_kind" == "clob" ]]; then
    if (( SIZE <= DENSE_SOURCE_LIMIT )); then
      clob_insert_tail="61410a0a"
    else
      clob_insert_tail="00410a0a"
    fi
    verify_lob_state "$dbdir" "$db" "$lob_kind" "after_insert" "$clob_insert_tail"
  else
    verify_lob_state "$dbdir" "$db" "$lob_kind" "after_insert" "41420a0a"
  fi

  run_sql_file "$dbdir" "$db" "$delete_sql" "${lob_kind}_insert_delete"
  log "DELETE_OK $lob_kind after_insert"
  row_count=$(query_scalar "$dbdir" "$db" "SELECT COUNT(*) FROM t;" "${lob_kind}_insert_after_delete_count")
  verify_value "$lob_kind insert after_delete row_count" "$row_count" "0"

  cleanup_case "$dbdir" "$db" "$src_insert"
  log "PASS_INSERT $lob_kind"
}

run_update_scenario()
{
  local lob_kind="$1"
  local db="u${lob_kind:0:1}${DB_SUFFIX}"
  local dbdir="$ROOT/$db"
  local src_update="$ROOT/${lob_kind}_update_4g.dat"
  local create_sql="$LOGDIR/${lob_kind}_update_create.sql"
  local seed_sql="$LOGDIR/${lob_kind}_update_seed.sql"
  local update_sql="$LOGDIR/${lob_kind}_update.sql"
  local delete_sql="$LOGDIR/${lob_kind}_update_delete.sql"
  local start_time end_time row_count clob_update_tail

  if [[ -d "$dbdir" ]]; then
    (cd "$dbdir" && cubrid deletedb "$db" >/dev/null 2>&1) || true
  fi
  rm -rf "$dbdir" "$src_update"
  mkdir -p "$dbdir"
  (cd "$dbdir" && cubrid deletedb "$db" >/dev/null 2>&1) || true

  log "START_UPDATE $lob_kind size=$SIZE db=$db root=$ROOT build=$BUILD_DIR cubrid=$CUBRID"

  if [[ "$lob_kind" == "clob" ]]; then
    make_file_with_tail_byte "$src_update" "$SIZE" "5a" "79"
  else
    make_file_with_tail_byte "$src_update" "$SIZE" "cd" "00"
  fi

  (cd "$dbdir" && cubrid createdb --db-volume-size="$DB_VOLUME_SIZE" --log-volume-size="$LOG_VOLUME_SIZE" "$db" en_US \
    > "$LOGDIR/${lob_kind}_update_createdb.out" 2> "$LOGDIR/${lob_kind}_update_createdb.err") \
    || fail "$lob_kind update createdb failed; see $LOGDIR/${lob_kind}_update_createdb.err"

  if [[ "$lob_kind" == "clob" ]]; then
    cat > "$create_sql" <<SQL
CREATE TABLE t (id INT PRIMARY KEY, c CLOB);
SQL
    cat > "$seed_sql" <<SQL
INSERT INTO t VALUES (1, char_to_clob('seed'));
SQL
    cat > "$update_sql" <<SQL
UPDATE t SET c = clob_from_file('$src_update') WHERE id = 1;
SQL
  else
    cat > "$create_sql" <<SQL
CREATE TABLE t (id INT PRIMARY KEY, b BLOB);
SQL
    cat > "$seed_sql" <<SQL
INSERT INTO t VALUES (1, bit_to_blob(X'AA'));
SQL
    cat > "$update_sql" <<SQL
UPDATE t SET b = blob_from_file('$src_update') WHERE id = 1;
SQL
  fi
  cat > "$delete_sql" <<SQL
DELETE FROM t WHERE id = 1;
SQL

  run_sql_file "$dbdir" "$db" "$create_sql" "${lob_kind}_update_create"
  run_sql_file "$dbdir" "$db" "$seed_sql" "${lob_kind}_update_seed"

  start_time=$(date +%s)
  run_sql_file "$dbdir" "$db" "$update_sql" "${lob_kind}_update"
  end_time=$(date +%s)
  log "UPDATE_OK $lob_kind elapsed=$((end_time - start_time))s"

  if [[ "$lob_kind" == "clob" ]]; then
    if (( SIZE <= DENSE_SOURCE_LIMIT )); then
      clob_update_tail="795a0a0a"
    else
      clob_update_tail="005a0a0a"
    fi
    verify_lob_state "$dbdir" "$db" "$lob_kind" "after_update" "$clob_update_tail"
  else
    verify_lob_state "$dbdir" "$db" "$lob_kind" "after_update" "43440a0a"
  fi

  run_sql_file "$dbdir" "$db" "$delete_sql" "${lob_kind}_update_delete"
  log "DELETE_OK $lob_kind after_update"
  row_count=$(query_scalar "$dbdir" "$db" "SELECT COUNT(*) FROM t;" "${lob_kind}_update_after_delete_count")
  verify_value "$lob_kind update after_delete row_count" "$row_count" "0"

  cleanup_case "$dbdir" "$db" "$src_update"
  log "PASS_UPDATE $lob_kind"
}

run_case()
{
  local lob_kind="$1"

  run_insert_scenario "$lob_kind"
  run_update_scenario "$lob_kind"
  log "PASS $lob_kind"
}

should_run_case()
{
  local lob_kind="$1"
  local filter="${CBRD26914_CASES:-}"

  if [[ -z "$filter" ]]; then
    return 0
  fi
  [[ ",$filter," == *",$lob_kind,"* ]]
}

log "CBRD-26914 4GiB csql CRUD validation started"
should_run_case clob && run_case clob
should_run_case blob && run_case blob
log "ALL_PASS root=$ROOT"
