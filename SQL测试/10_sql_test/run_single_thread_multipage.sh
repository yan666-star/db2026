#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

db_name="single_thread_multipage_db"
server_log="/tmp/rmdb_single_thread_multipage.log"
server_pid=""

cleanup() {
    if [[ -n "${server_pid}" ]]; then
        kill -9 "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

start_server() {
    ./build/bin/rmdb "${db_name}" >"${server_log}" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 100); do
        if ! kill -0 "${server_pid}" 2>/dev/null; then
            echo "server exited during startup" >&2
            cat "${server_log}" >&2
            return 1
        fi
        if python3 -c \
            "import socket; s=socket.create_connection(('127.0.0.1',8765),.1); s.close()" \
            2>/dev/null; then
            return 0
        fi
        sleep 0.05
    done
    echo "server did not become ready" >&2
    cat "${server_log}" >&2
    return 1
}

pkill -9 -x rmdb 2>/dev/null || true
rm -rf "${db_name}"

echo "=== execute cross-page transactions and crash ==="
start_server
python3 sql_test/run_sql.py sql_test/22_single_thread_multipage_before_crash.sql
wait "${server_pid}" 2>/dev/null || true
server_pid=""

echo "=== restart and verify ==="
start_server
python3 sql_test/run_sql.py sql_test/23_single_thread_multipage_verify.sql
python3 sql_test/verify_single_thread_multipage.py "${db_name}/output.txt"

echo "=== database files ==="
ls -lh "${db_name}"
echo "=== server log ==="
cat "${server_log}"
