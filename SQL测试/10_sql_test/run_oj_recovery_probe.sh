#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

server_pid=""
cleanup() {
    if [[ -n "${server_pid}" ]]; then
        kill -9 "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

start_server() {
    local db_name=$1
    local log_file=$2
    ./build/bin/rmdb "${db_name}" >"${log_file}" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 100); do
        if ! kill -0 "${server_pid}" 2>/dev/null; then
            echo "server exited while starting ${db_name}" >&2
            cat "${log_file}" >&2
            return 1
        fi
        if python3 -c \
            "import socket; s=socket.create_connection(('127.0.0.1',8765),.1); s.close()" \
            2>/dev/null; then
            return 0
        fi
        sleep 0.05
    done
    echo "server did not become ready: ${db_name}" >&2
    cat "${log_file}" >&2
    return 1
}

stop_after_crash() {
    wait "${server_pid}" 2>/dev/null || true
    server_pid=""
}

kill_server() {
    kill -9 "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
    server_pid=""
}

run_case() {
    local name=$1
    local setup_sql=$2
    local crash_sql=$3
    local verify_sql=$4
    local db_name="oj_${name}_db"
    local log_file="/tmp/rmdb_${name}.log"

    echo "===== ${name}: prepare ====="
    rm -rf "${db_name}"
    start_server "${db_name}" "${log_file}"
    if [[ -n "${setup_sql}" ]]; then
        python3 sql_test/run_sql.py "${setup_sql}"
    fi
    python3 sql_test/run_sql.py "${crash_sql}"
    stop_after_crash

    echo "===== ${name}: recover ====="
    start_server "${db_name}" "${log_file}"
    python3 sql_test/run_sql.py "${verify_sql}"
    kill_server

    echo "===== ${name}: output.txt ====="
    cat "${db_name}/output.txt"
    echo "===== ${name}: server log ====="
    cat "${log_file}"
}

pkill -9 -x rmdb 2>/dev/null || true

run_case \
    single \
    "" \
    sql_test/10_oj_single_before_crash.sql \
    sql_test/11_oj_single_verify.sql

run_case \
    without_checkpoint \
    sql_test/12_oj_wide_before_crash.sql \
    sql_test/13_oj_wide_no_checkpoint_before_crash.sql \
    sql_test/15_oj_wide_verify.sql

run_case \
    with_checkpoint \
    sql_test/12_oj_wide_before_crash.sql \
    sql_test/14_oj_wide_checkpoint_before_crash.sql \
    sql_test/15_oj_wide_verify.sql
