#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

db_name="single_thread_state_db"
log_file="/tmp/rmdb_single_thread_state.log"
server_pid=""

cleanup() {
    if [[ -n "${server_pid}" ]]; then
        kill -9 "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

pkill -9 -x rmdb 2>/dev/null || true
rm -rf "${db_name}"

./build/bin/rmdb "${db_name}" >"${log_file}" 2>&1 &
server_pid=$!
sleep 2
python3 sql_test/run_sql.py \
    sql_test/18_single_thread_state_before_crash.sql
wait "${server_pid}" 2>/dev/null || true
server_pid=""

./build/bin/rmdb "${db_name}" >"${log_file}" 2>&1 &
server_pid=$!
sleep 2
python3 sql_test/run_sql.py \
    sql_test/19_single_thread_state_verify.sql
python3 sql_test/verify_single_thread_state.py "${db_name}"

echo "=== visible characters ==="
sed -n l "${db_name}/output.txt"
echo "=== server log ==="
cat "${log_file}"
