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

#
# cbrd26914_internal_lob_vs_inline_bench.sh - Internal LOB vs inline VARCHAR throughput
#
# Answers the "performance comparison" section of the Internal LOB design review: how does storing a
# value as an Internal LOB (CLOB, chunk chain in FILE_INTERNAL_LOB) compare with storing the same bytes
# in a VARCHAR column (which the OOS layer may still demote out of row)?
#
# Two families of case:
#
#   repeat - both sides build the value server-side with REPEAT (), so neither is penalised by SQL
#            statement size limits and neither reads a file the other does not.  A string value cannot
#            exceed string_max_size_bytes, whose own maximum is 32 MiB, so this family stops there.
#   file   - the value is streamed in with clob_from_file (), the route Internal LOB is designed for.
#            VARCHAR has no counterpart above the 32 MiB string ceiling; the table records that.
#
# Measured per case: INSERT / full-scan SELECT / UPDATE wall clock, and the server's resident size.
#
# Run against a RELEASE build; a Debug build's numbers are not comparable to anything.
#
# Usage: cbrd26914_internal_lob_vs_inline_bench.sh [db_name]
#

set -euo pipefail

: "${CUBRID:=/home/heexoo/CUBRID}"
export CUBRID
export PATH="$CUBRID/bin:$PATH"
export LD_LIBRARY_PATH="$CUBRID/lib:${LD_LIBRARY_PATH:-}"

DB_NAME="${1:-ilbenchdb}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ilbench.XXXXXX")"
DB_DIR="$WORK_DIR/db"
RESULT_MD="$WORK_DIR/result.md"

# server-built values: "<value KiB>:<rows>".  Capped by string_max_size_bytes (max 32 MiB).
REPEAT_CASES=("4:2048" "64:128" "1024:16" "10240:4")
# streamed from a file: "<value KiB>:<rows>".  No string ceiling applies to the Internal LOB side.
FILE_CASES=("10240:4" "102400:2")

STRING_MAX="32M"

cleanup ()
{
  cubrid server stop "$DB_NAME" >/dev/null 2>&1 || true
  cubrid deletedb "$DB_NAME" >/dev/null 2>&1 || true
  rm -rf "$WORK_DIR"
}

finish ()
{
  local rc=$?
  if [ -f "$RESULT_MD" ]; then
    cp "$RESULT_MD" "./cbrd26914_internal_lob_bench_result.md" 2>/dev/null || true
  fi
  cleanup
  exit $rc
}
trap finish EXIT

# string_max_size_bytes is a session parameter and every csql invocation is a fresh session, so it has to
# be re-applied on each one; setting it once up front silently reverts and large values then fail.
SET_PRM="SET SYSTEM PARAMETERS 'string_max_size_bytes=$STRING_MAX';"

run_sql ()
{
  csql --CS-mode --no-auto-commit -u dba -c "$SET_PRM $1" "$DB_NAME" >/dev/null
}

# $1 is a sql file; it is executed with the session parameter prepended
run_sql_file ()
{
  printf '%s\n' "$SET_PRM" > "$WORK_DIR/_run.sql"
  cat "$1" >> "$WORK_DIR/_run.sql"
  csql --CS-mode --no-auto-commit -u dba -i "$WORK_DIR/_run.sql" "$DB_NAME" >/dev/null
}

# Run a sql file and print its wall clock in milliseconds, or -1 if the statement failed.  A failure is
# a result here, not an accident: it is how the table records "this type cannot hold that value".
timed_sql_file ()
{
  local file="$1" start end
  start=$(date +%s%N)
  if ! run_sql_file "$file" 2>/dev/null; then
    echo -1
    return
  fi
  end=$(date +%s%N)
  echo $(( (end - start) / 1000000 ))
}

# Print the first integer of a single-value query result.  csql exits 0 even when a statement fails, so
# every case is verified by reading back what was actually stored instead of trusting the exit code.
query_scalar ()
{
  local out=""
  # grep exits non-zero when the query returned NULL or errored, which is exactly the case this has to
  # report rather than abort on, so the pipeline failure is swallowed and reported as 0.
  out=$(csql --CS-mode --no-auto-commit -u dba -c "$SET_PRM $1" "$DB_NAME" 2>/dev/null \
	  | grep -oE '^[[:space:]]*-?[0-9]+[[:space:]]*$' | tail -1 | tr -d '[:space:]') || true
  echo "${out:-0}"
}

# Current resident size of the server.  VmHWM is a high-water mark that never falls, so with a shared
# buffer pool it reads the same for every case and cannot discriminate; VmRSS moves with the workload.
server_rss_kib ()
{
  local pid
  pid=$(pgrep -f "cub_server $DB_NAME" | head -1 || true)
  if [ -z "$pid" ]; then
    echo 0
    return
  fi
  awk '/^VmRSS:/ { print $2 }' "/proc/$pid/status" 2>/dev/null || echo 0
}

emit_row ()
{
  printf '| %s | %s | %s | %s | %s | %s | %s | %s |\n' "$@" >> "$RESULT_MD"
}

fmt_ms ()
{
  if [ "$1" -lt 0 ]; then
    echo "n/a"
  else
    echo "$1"
  fi
}

# One measured case.
#   $5 value expression, $6 cheap length expression used to verify what was stored,
#   $7 expected total bytes, $8 expression that forces a full read for the SELECT timing.
# The verify and the read expression differ on purpose: clob_length () answers from the locator without
# touching the payload, so timing it would compare an O(1) metadata lookup against a full VARCHAR read.
measure ()
{
  local label="$1" route="$2" coltype="$3" rows="$4" value_expr="$5" len_expr="$6" expect_len="$7"
  local read_expr="$8"
  local tbl="t_bench_case" ins_ms sel_ms upd_ms rss_mib i stored_rows stored_len read_ok

  run_sql "DROP TABLE IF EXISTS $tbl; COMMIT;" || true
  if ! run_sql "CREATE TABLE $tbl (id INT PRIMARY KEY, v $coltype); COMMIT;" 2>/dev/null; then
    emit_row "$label" "$route" "$coltype" "$rows" "n/a" "n/a" "n/a" "column type rejected"
    return
  fi

  : > "$WORK_DIR/ins.sql"
  for ((i = 1; i <= rows; i++)); do
    printf 'INSERT INTO %s VALUES (%d, %s);\n' "$tbl" "$i" "$value_expr" >> "$WORK_DIR/ins.sql"
  done
  printf 'COMMIT;\n' >> "$WORK_DIR/ins.sql"
  ins_ms=$(timed_sql_file "$WORK_DIR/ins.sql")

  # verify what actually landed; a failed statement leaves fewer rows or a short value
  stored_rows=$(query_scalar "SELECT COUNT (*) FROM $tbl;")
  stored_len=$(query_scalar "SELECT SUM ($len_expr) FROM $tbl;")
  : "${stored_rows:=0}"
  : "${stored_len:=0}"
  if [ "$ins_ms" -lt 0 ] || [ "$stored_rows" != "$rows" ] || [ "$stored_len" != "$expect_len" ]; then
    emit_row "$label" "$route" "$coltype" "$rows" "n/a" "n/a" "n/a" \
	     "not stored (rows $stored_rows/$rows, bytes $stored_len/$expect_len)"
    run_sql "DROP TABLE IF EXISTS $tbl; COMMIT;" || true
    return
  fi

  printf 'SELECT SUM (%s) FROM %s;\nCOMMIT;\n' "$read_expr" "$tbl" > "$WORK_DIR/sel.sql"
  sel_ms=$(timed_sql_file "$WORK_DIR/sel.sql")
  # a read that cannot be expressed (a CLOB above the string ceiling cannot be materialized) is not a
  # failure of the case, only of that one column
  read_ok=$(query_scalar "SELECT SUM ($read_expr) FROM $tbl;")
  if [ "$read_ok" != "$expect_len" ]; then
    sel_ms=-1
  fi

  printf 'UPDATE %s SET v = %s WHERE id <= %d;\nCOMMIT;\n' "$tbl" "$value_expr" "$rows" > "$WORK_DIR/upd.sql"
  upd_ms=$(timed_sql_file "$WORK_DIR/upd.sql")

  rss_mib=$(( $(server_rss_kib) / 1024 ))
  emit_row "$label" "$route" "$coltype" "$rows" "$(fmt_ms "$ins_ms")" "$(fmt_ms "$sel_ms")" \
	   "$(fmt_ms "$upd_ms")" "${rss_mib} MiB"

  run_sql "DROP TABLE IF EXISTS $tbl; COMMIT;" || true
}

mkdir -p "$DB_DIR"
cubrid createdb --db-volume-size=2G --log-volume-size=512M "$DB_NAME" en_US.utf8 -F "$DB_DIR" >/dev/null
cubrid server start "$DB_NAME" >/dev/null

{
  echo "# Internal LOB vs inline storage benchmark"
  echo
  echo "- host: $(uname -srm)"
  echo "- string_max_size_bytes: $STRING_MAX (parameter maximum; default is 1 MiB)"
  echo
  echo "| value size | route | column type | rows | INSERT ms | SELECT ms | UPDATE ms | server RSS |"
  echo "|---:|---|---|---:|---:|---:|---:|---:|"
} > "$RESULT_MD"

for spec in "${REPEAT_CASES[@]}"; do
  IFS=':' read -r kib rows <<< "$spec"
  bytes=$((kib * 1024))
  # REPEAT ('ab', n) yields 2n characters
  text_expr="REPEAT('ab', $((bytes / 2)))"
  label="$((kib / 1024)) MiB"
  [ "$kib" -lt 1024 ] && label="$kib KiB"

  measure "$label" "REPEAT()" "CLOB" "$rows" "CHAR_TO_CLOB($text_expr)" "clob_length(v)" \
	  "$((bytes * rows))" "LENGTH(CLOB_TO_CHAR(v))"
  measure "$label" "REPEAT()" "VARCHAR($bytes)" "$rows" "$text_expr" "length(v)" "$((bytes * rows))" \
	  "LENGTH(v)"
done

for spec in "${FILE_CASES[@]}"; do
  IFS=':' read -r kib rows <<< "$spec"
  payload="$WORK_DIR/payload_${kib}.txt"
  # printable payload; head closing the pipe makes yes exit on SIGPIPE, which pipefail would treat as fatal
  set +o pipefail
  yes 'abcdefghijklmnopqrstuvwxyz0123456789' 2>/dev/null | head -c "$((kib * 1024))" > "$payload"
  set -o pipefail
  label="$((kib / 1024)) MiB"

  measure "$label" "clob_from_file()" "CLOB" "$rows" "clob_from_file('$payload')" "clob_length(v)" \
	  "$((kib * 1024 * rows))" "LENGTH(CLOB_TO_CHAR(v))"
  # A VARCHAR has no file-loading counterpart and cannot exceed the 32 MiB string ceiling anyway.
  measure "$label" "REPEAT()" "VARCHAR($((kib * 1024)))" "$rows" \
	  "REPEAT('ab', $((kib * 1024 / 2)))" "length(v)" "$((kib * 1024 * rows))" "LENGTH(v)"
done

cubrid server stop "$DB_NAME" >/dev/null
cat "$RESULT_MD"
