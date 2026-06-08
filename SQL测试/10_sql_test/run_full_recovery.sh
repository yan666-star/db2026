#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

db_name="recovery_full_db"
log_file="/tmp/rmdb_recovery_full.log"
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
python3 sql_test/run_full_recovery_crash.py \
    sql_test/07_full_recovery_before_crash.sql
wait "${server_pid}" 2>/dev/null || true
server_pid=""

./build/bin/rmdb "${db_name}" >"${log_file}" 2>&1 &
server_pid=$!
sleep 2
python3 sql_test/run_sql.py sql_test/08_full_recovery_verify.sql
python3 sql_test/verify_full_recovery.py "${db_name}"
python3 sql_test/run_sql.py sql_test/09_post_recovery_commit.sql
kill -9 "${server_pid}" 2>/dev/null || true
wait "${server_pid}" 2>/dev/null || true
server_pid=""

# Recovery must be idempotent: a second crash/restart cannot duplicate rows,
# reapply a loser transaction, or leave stale index entries behind.
./build/bin/rmdb "${db_name}" >"${log_file}" 2>&1 &
server_pid=$!
sleep 2
python3 sql_test/run_sql.py sql_test/08_full_recovery_verify.sql
python3 sql_test/verify_full_recovery.py "${db_name}" 36
kill -9 "${server_pid}" 2>/dev/null || true
wait "${server_pid}" 2>/dev/null || true
server_pid=""

# Remove the restart pointer to exercise full-log recovery and index rebuild,
# then simulate a torn final record. Startup must discard the incomplete tail
# without losing the preceding valid WAL records.
rm -f "${db_name}/db.restart"
log_size_before_torn_tail=$(stat -c%s "${db_name}/db.log")
printf '\x01\x02\x03\x04\x05\x06\x07' >>"${db_name}/db.log"

./build/bin/rmdb "${db_name}" >"${log_file}" 2>&1 &
server_pid=$!
sleep 2
log_size_after_recovery=$(stat -c%s "${db_name}/db.log")
if [[ "${log_size_after_recovery}" -ne "${log_size_before_torn_tail}" ]]; then
    echo "torn WAL tail was not truncated: before=${log_size_before_torn_tail}, after=${log_size_after_recovery}" >&2
    exit 1
fi
python3 sql_test/run_sql.py sql_test/08_full_recovery_verify.sql
python3 sql_test/verify_full_recovery.py "${db_name}" 36

echo "=== output.txt ==="
cat "${db_name}/output.txt"
echo "=== server log ==="
cat "${log_file}"
