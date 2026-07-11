#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import (  # noqa: E402
    SqlClient,
    find_rmdb,
    response_failed,
    table_rows,
    wait_for_server,
)


CRASH_MARKER = "RMDB_ABORT_CRASH_WINDOW"


def execute_ok(client, statement):
    response = client.execute(statement)
    if response_failed(response):
        raise AssertionError(f"statement failed:\n{statement}\n{response}")
    return response


def scalar(client, statement):
    rows = table_rows(execute_ok(client, statement))
    if not rows:
        raise AssertionError(f"query returned no rows: {statement}")
    return int(float(rows[-1][0]))


def launch(args, pause_us=0):
    env = os.environ.copy()
    env["RMDB_PERF_DIAG"] = "1"
    if pause_us:
        env["RMDB_ABORT_CRASH_PAUSE_US"] = str(pause_us)
    else:
        env.pop("RMDB_ABORT_CRASH_PAUSE_US", None)
    log_handle = args.server_log.open("a", encoding="utf-8", errors="replace")
    process = subprocess.Popen(
        [str(find_rmdb(args.build_dir)), args.db_name],
        cwd=str(args.build_dir),
        stdout=log_handle,
        stderr=subprocess.STDOUT,
        env=env,
    )
    wait_for_server(args.host, args.port, args.startup_timeout, process, args.server_log)
    return process, log_handle


def connect(args, snapshot=False):
    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    if snapshot:
        execute_ok(client, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
    execute_ok(client, "set output_file off")
    return client


def wait_for_marker(path, process, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError("server exited before reaching abort crash window")
        if path.exists() and CRASH_MARKER in path.read_text(
            encoding="utf-8", errors="replace"
        ):
            return
        time.sleep(0.01)
    raise AssertionError("server did not reach checkpoint-to-ABORT crash window")


def main():
    parser = argparse.ArgumentParser(
        description="Crash after abort rollback is durable but before its ABORT log."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    parser.add_argument("--window-timeout", type=float, default=10.0)
    parser.add_argument("--pause-us", type=int, default=5_000_000)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="abort_checkpoint_crash_probe_db")
    parser.add_argument(
        "--server-log",
        type=Path,
        default=REPO_ROOT / "build" / "abort_checkpoint_crash_probe.log",
    )
    args = parser.parse_args()
    args.build_dir = args.build_dir.resolve()
    args.server_log = args.server_log.resolve()
    db_dir = args.build_dir / args.db_name
    if db_dir.exists():
        shutil.rmtree(db_dir)
    args.server_log.parent.mkdir(parents=True, exist_ok=True)
    args.server_log.write_text("", encoding="utf-8")

    process = None
    log_handle = None
    try:
        process, log_handle = launch(args, args.pause_us)
        client = connect(args)
        try:
            execute_ok(client, "CREATE TABLE abort_crash_probe (k INT, v INT);")
            execute_ok(client, "CREATE INDEX abort_crash_probe(k);")
            execute_ok(client, "INSERT INTO abort_crash_probe VALUES (1, 10);")
        finally:
            client.close()

        def rollback_worker():
            txn = connect(args, snapshot=True)
            try:
                execute_ok(txn, "BEGIN;")
                execute_ok(txn, "INSERT INTO abort_crash_probe VALUES (2, 20);")
                txn.execute("ROLLBACK;")
            except Exception as exc:
                # The expected kill closes this connection while ROLLBACK is
                # paused between durable rollback and its ABORT record.
                if process is not None and process.poll() is None:
                    raise exc
            finally:
                txn.close()

        worker = threading.Thread(target=rollback_worker, daemon=True)
        worker.start()
        wait_for_marker(args.server_log, process, args.window_timeout)
        process.kill()
        process.wait(timeout=5)
        log_handle.close()
        process = None
        log_handle = None
        worker.join(timeout=2)

        process, log_handle = launch(args)
        verifier = connect(args)
        try:
            if scalar(verifier, "SELECT count(*) FROM abort_crash_probe WHERE k = 1;") != 1:
                raise AssertionError("committed baseline row was lost after recovery")
            if scalar(verifier, "SELECT count(*) FROM abort_crash_probe WHERE k = 2;") != 0:
                raise AssertionError("aborted INSERT was resurrected after recovery")
            execute_ok(verifier, "INSERT INTO abort_crash_probe VALUES (2, 21);")
            if scalar(verifier, "SELECT count(*) FROM abort_crash_probe WHERE k = 2;") != 1:
                raise AssertionError("index/table state rejected key reuse after recovery")
        finally:
            verifier.close()
        print("abort checkpoint-to-log crash probe passed")
        return 0
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        if log_handle is not None:
            log_handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
