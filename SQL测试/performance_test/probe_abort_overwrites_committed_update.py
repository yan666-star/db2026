#!/usr/bin/env python3
import argparse
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
    remove_db_dir,
    start_server,
    stop_server,
    table_rows,
)


def fail_if_response_failed(statement, response):
    lowered = response.lower()
    if "failure" in lowered or "abort" in lowered:
        raise AssertionError(f"statement failed: {statement}\n{response}")


def execute(client, statement):
    response = client.execute(statement)
    fail_if_response_failed(statement, response)
    return response


def select_scalar(client, statement):
    response = execute(client, statement)
    rows = table_rows(response)
    if not rows or not rows[-1]:
        raise AssertionError(f"no scalar returned for {statement}\n{response}")
    return rows[-1][0]


def setup_probe_table(args):
    client = SqlClient(args.host, args.port, args.timeout)
    try:
        client.connect()
        execute(client, "CREATE TABLE abort_probe (id INT, qty INT, ytd FLOAT);")
        execute(client, "CREATE INDEX abort_probe(id);")
        execute(client, "INSERT INTO abort_probe VALUES (1, 100, 100.0);")
    finally:
        client.close()


def run_abort_overwrite_probe(args):
    t2_error = []
    t2_done = threading.Event()

    def t2_worker():
        client = SqlClient(args.host, args.port, args.timeout)
        try:
            client.connect()
            execute(client, "BEGIN;")
            execute(
                client,
                "UPDATE abort_probe SET qty = qty - 3 WHERE id = 1;",
            )
            execute(
                client,
                "UPDATE abort_probe SET ytd = ytd + 3.0 WHERE id = 1;",
            )
            execute(client, "COMMIT;")
        except Exception as exc:  # pragma: no cover - diagnostic path
            t2_error.append(exc)
        finally:
            client.close()
            t2_done.set()

    t1 = SqlClient(args.host, args.port, args.timeout)
    try:
        t1.connect()
        execute(t1, "BEGIN;")
        execute(t1, "UPDATE abort_probe SET qty = qty - 5 WHERE id = 1;")
        execute(t1, "UPDATE abort_probe SET ytd = ytd + 5.0 WHERE id = 1;")

        worker = threading.Thread(target=t2_worker)
        worker.start()
        time.sleep(args.interleave_delay)
        execute(t1, "ROLLBACK;")
        worker.join(timeout=args.timeout)
        if worker.is_alive():
            raise AssertionError("T2 did not finish after T1 rollback")
        if t2_error:
            raise t2_error[0]
    finally:
        t1.close()

    verifier = SqlClient(args.host, args.port, args.timeout)
    try:
        verifier.connect()
        qty = int(float(select_scalar(verifier, "SELECT qty FROM abort_probe WHERE id = 1;")))
        ytd = float(select_scalar(verifier, "SELECT ytd FROM abort_probe WHERE id = 1;"))
    finally:
        verifier.close()

    if qty != 97:
        raise AssertionError(f"abort overwrite detected for int: expected qty 97, got {qty}")
    if abs(ytd - 103.0) > 0.001:
        raise AssertionError(f"abort overwrite detected for float: expected ytd 103.0, got {ytd}")

    print(f"abort-overwrite probe passed: qty={qty}, ytd={ytd:.6f}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Probe whether an aborted READ COMMITTED transaction overwrites a committed update."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--startup-timeout", type=float, default=10.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="abort_overwrite_probe_db")
    parser.add_argument(
        "--server-log",
        type=Path,
        default=REPO_ROOT / "build" / "abort_overwrite_probe_server.log",
    )
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    parser.add_argument("--skip-setup", action="store_true")
    parser.add_argument("--interleave-delay", type=float, default=0.5)
    return parser.parse_args()


def main():
    args = parse_args()
    process = None
    log_handle = None
    try:
        if args.start_server:
            build_dir = args.build_dir.resolve()
            find_rmdb(build_dir)
            if args.reset_db:
                remove_db_dir(build_dir / args.db_name, build_dir)
            process, log_handle, _ = start_server(args)
        if not args.skip_setup:
            setup_probe_table(args)
        run_abort_overwrite_probe(args)
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
