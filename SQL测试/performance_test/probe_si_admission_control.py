#!/usr/bin/env python3
"""Verify that SI admission happens before snapshot assignment."""

import argparse
import os
import threading
import time
from pathlib import Path

from run_generic_acid_suite import connect, execute_ok
from run_performance_smoke import start_server, stop_server


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--startup-timeout", type=float, default=120.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="si_admission_probe_db")
    parser.add_argument(
        "--server-log", type=Path,
        default=REPO_ROOT / "build" / "si_admission_probe.log")
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--db-dir", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    process = None
    log_handle = None
    previous_limit = os.environ.get("RMDB_SI_MAX_ACTIVE")
    clients = []
    try:
        os.environ["RMDB_SI_MAX_ACTIVE"] = "2"
        if args.start_server:
            process, log_handle, args.db_dir = start_server(args)

        clients = [connect(args, "snapshot") for _ in range(3)]
        execute_ok(clients[0], "BEGIN;")
        execute_ok(clients[1], "BEGIN;")

        third_started = threading.Event()
        third_finished = threading.Event()
        third_error = []

        def begin_third():
            third_started.set()
            try:
                execute_ok(clients[2], "BEGIN;")
            except Exception as exc:  # surfaced in the main thread
                third_error.append(exc)
            finally:
                third_finished.set()

        thread = threading.Thread(target=begin_third)
        thread.start()
        third_started.wait(timeout=1.0)
        time.sleep(0.3)
        if third_finished.is_set():
            raise AssertionError(
                "third SI transaction entered before an admission slot was released")

        execute_ok(clients[0], "COMMIT;")
        if not third_finished.wait(timeout=args.timeout):
            raise AssertionError("third SI transaction did not enter after slot release")
        thread.join(timeout=1.0)
        if third_error:
            raise third_error[0]

        execute_ok(clients[1], "ROLLBACK;")
        execute_ok(clients[2], "ROLLBACK;")
        print("SI admission control probe passed: limit=2, third BEGIN queued")
        return 0
    finally:
        for client in clients:
            try:
                client.close()
            except Exception:
                pass
        stop_server(process, log_handle)
        if previous_limit is None:
            os.environ.pop("RMDB_SI_MAX_ACTIVE", None)
        else:
            os.environ["RMDB_SI_MAX_ACTIVE"] = previous_limit


if __name__ == "__main__":
    raise SystemExit(main())
