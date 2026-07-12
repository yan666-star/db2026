#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

db_name="statement_recovery_db"
server_log="/tmp/rmdb_statement_recovery.log"
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

echo "=== execute statement sample and crash ==="
start_server
python3 sql_test/run_sql.py sql_test/01_without_checkpoint_before_crash.sql
wait "${server_pid}" 2>/dev/null || true
server_pid=""

echo "=== restart and verify ==="
start_server
python3 sql_test/run_sql.py sql_test/02_without_checkpoint_verify.sql

python3 - "${db_name}/output.txt" <<'PY'
import sys
from pathlib import Path

actual = Path(sys.argv[1]).read_text(encoding="utf-8")
expected = "| id | num |\n| 1 | 1 |\n"
if actual != expected:
    print("statement sample mismatch", file=sys.stderr)
    print(f"expected: {expected!r}", file=sys.stderr)
    print(f"actual:   {actual!r}", file=sys.stderr)
    raise SystemExit(1)
print("statement sample passed exactly")
PY

echo "=== visible output ==="
sed -n l "${db_name}/output.txt"
echo "=== server log ==="
cat "${server_log}"
