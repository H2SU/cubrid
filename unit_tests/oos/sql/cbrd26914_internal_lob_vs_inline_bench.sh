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
# Both sides build their value server-side with REPEAT() so that neither is penalised by SQL statement
# size limits and neither reads a file the other does not.  Measured per value size:
#   INSERT / full-scan SELECT / UPDATE wall clock, and the server's resident size after each case.
#
# Run against a RELEASE build; a Debug build's numbers are not comparable to anything.
#
# Usage: cbrd26914_internal_lob_vs_inline_bench.sh [db_name] [total_mib_per_case]
#

set -euo pipefail

: "${CUBRID:=/home/heexoo/CUBRID}"
export CUBRID
export PATH="$CUBRID/bin:$PATH"
export LD_LIBRARY_PATH="$CUBRID/lib:${LD_LIBRARY_PATH:-}"

DB_NAME="${1:-ilbenchdb}"
TOTAL_MIB="${2:-32}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ilbench.XXXXXX")"
DB_DIR="$WORK_DIR/db"
RESULT_MD="$WORK_DIR/result.md"

# value sizes in KiB
SIZES_KIB=(4 64 1024)

cleanup ()
{
  cubrid server stop "$DB_NAME" >/dev/null 2>&1 || true
  cubrid deletedb "$DB_NAME" >/dev/null 2>&1 || true
  rm -rf "$WORK_DIR"
}

# keep the result file; drop everything else
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

run_sql ()
{
  csql --CS-mode --no-auto-commit -u dba -c "$1" "$DB_NAME" >/dev/null
}

run_sql_file ()
{
  csql --CS-mode --no-auto-commit -u dba -i "$1" "$DB_NAME" >/dev/null
}

# wall clock of a sql file, in milliseconds
timed_sql_file ()
{
  local file="$1" start end
  start=$(date +%s%N)
  run_sql_file "$file"
  end=$(date +%s%N)
  echo $(( (end - start) / 1000000 ))
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

# emit "INSERT INTO <tbl> VALUES (i, <expr>);" for i in 1..rows
gen_insert_script ()
{
  local file="$1" tbl="$2" expr="$3" rows="$4" i
  : > "$file"
  for ((i = 1; i <= rows; i++)); do
    printf 'INSERT INTO %s VALUES (%d, %s);\n' "$tbl" "$i" "$expr" >> "$file"
  done
  printf 'COMMIT;\n' >> "$file"
}

echo "# Internal LOB vs inline storage benchmark" > "$RESULT_MD"
echo >> "$RESULT_MD"
echo "- build: $(cubrid --version 2>&1 | head -1)" >> "$RESULT_MD"
echo "- host: $(uname -srm)" >> "$RESULT_MD"
echo "- payload per case: ${TOTAL_MIB} MiB total" >> "$RESULT_MD"
echo >> "$RESULT_MD"
echo "| value size | column type | rows | INSERT ms | SELECT ms | UPDATE ms | server RSS MiB |" >> "$RESULT_MD"
echo "|---:|---|---:|---:|---:|---:|---:|" >> "$RESULT_MD"

mkdir -p "$DB_DIR"
cubrid createdb --db-volume-size=512M --log-volume-size=256M "$DB_NAME" en_US.utf8 -F "$DB_DIR" >/dev/null
cubrid server start "$DB_NAME" >/dev/null

for kib in "${SIZES_KIB[@]}"; do
  bytes=$((kib * 1024))
  rows=$(( (TOTAL_MIB * 1024) / kib ))
  [ "$rows" -lt 1 ] && rows=1
  # REPEAT('ab', n) yields 2n characters
  half=$((bytes / 2))
  text_expr="REPEAT('ab', $half)"

  # ---- CLOB (Internal LOB) vs VARCHAR (inline) ----
  for pair in "clob:CLOB:CHAR_TO_CLOB($text_expr):clob_length(v)" \
              "varchar:VARCHAR($bytes):$text_expr:length(v)"; do
    IFS=':' read -r tag coltype insert_expr len_expr <<< "$pair"
    tbl="t_bench_$tag"

    run_sql "DROP TABLE IF EXISTS $tbl; COMMIT;"
    run_sql "CREATE TABLE $tbl (id INT PRIMARY KEY, v $coltype); COMMIT;"

    gen_insert_script "$WORK_DIR/ins.sql" "$tbl" "$insert_expr" "$rows"
    ins_ms=$(timed_sql_file "$WORK_DIR/ins.sql")

    printf 'SELECT SUM (%s) FROM %s;\nCOMMIT;\n' "$len_expr" "$tbl" > "$WORK_DIR/sel.sql"
    sel_ms=$(timed_sql_file "$WORK_DIR/sel.sql")

    printf 'UPDATE %s SET v = %s WHERE id <= %d;\nCOMMIT;\n' "$tbl" "$insert_expr" "$rows" \
      > "$WORK_DIR/upd.sql"
    upd_ms=$(timed_sql_file "$WORK_DIR/upd.sql")

    rss_kib=$(server_rss_kib)
    rss_mib=$((rss_kib / 1024))

    printf '| %d KiB | %s | %d | %d | %d | %d | %d |\n' \
      "$kib" "$coltype" "$rows" "$ins_ms" "$sel_ms" "$upd_ms" "$rss_mib" >> "$RESULT_MD"

    run_sql "DROP TABLE $tbl; COMMIT;"
  done
done

cubrid server stop "$DB_NAME" >/dev/null
cat "$RESULT_MD"
