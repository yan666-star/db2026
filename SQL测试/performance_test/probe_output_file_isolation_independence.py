#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import (  # noqa: E402
    SqlClient,
    response_failed,
    select_scalar_int,
    start_server,
    stop_server,
)


def execute_ok(client, sql):
    response = client.execute(sql)
    if response_failed(response):
        raise AssertionError(f"SQL failed:\n{sql}\n{response}")
    return response


def connect(args):
    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    return client


TABLE_NAME = "output_isolation_probe"


def read_value(client):
    return select_scalar_int(
        client,
        f"SELECT value FROM {TABLE_NAME} WHERE id = 1;",
    )


def update_value(args, value):
    writer = connect(args)
    try:
        execute_ok(writer, "BEGIN;")
        execute_ok(
            writer,
            f"UPDATE {TABLE_NAME} SET value = {value} WHERE id = 1;",
        )
        execute_ok(writer, "COMMIT;")
    finally:
        writer.close()


def setup(args):
    client = connect(args)
    try:
        execute_ok(client, f"CREATE TABLE {TABLE_NAME} (id INT, value INT);")
        execute_ok(client, f"INSERT INTO {TABLE_NAME} VALUES (1, 10);")
        execute_ok(client, "set output_file off")
    finally:
        client.close()


def verify_default_read_committed(args):
    reader = connect(args)
    try:
        execute_ok(reader, "BEGIN;")
        before = read_value(reader)
        update_value(args, 20)
        after = read_value(reader)
        execute_ok(reader, "COMMIT;")
        if before != 10 or after != 20:
            raise AssertionError(
                "set output_file off changed the default isolation: "
                f"READ COMMITTED expected 10 then 20, got {before} then {after}"
            )
    finally:
        try:
            reader.execute("ROLLBACK;")
        except Exception:
            pass
        reader.close()


def verify_explicit_snapshot(args):
    reader = connect(args)
    try:
        execute_ok(
            reader,
            "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;",
        )
        execute_ok(reader, "BEGIN;")
        before = read_value(reader)
        update_value(args, 30)
        after = read_value(reader)
        execute_ok(reader, "COMMIT;")
        if before != 20 or after != 20:
            raise AssertionError(
                "explicit snapshot isolation lost repeatable-read semantics: "
                f"expected 20 then 20, got {before} then {after}"
            )
    finally:
        try:
            reader.execute("ROLLBACK;")
        except Exception:
            pass
        reader.close()

    verifier = connect(args)
    try:
        latest = read_value(verifier)
        if latest != 30:
            raise AssertionError(
                "a completed snapshot transaction hid a later committed RC "
                f"update from new readers: expected 30, got {latest}"
            )
    finally:
        verifier.close()

    fresh_snapshot = connect(args)
    try:
        execute_ok(
            fresh_snapshot,
            "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;",
        )
        execute_ok(fresh_snapshot, "BEGIN;")
        latest = read_value(fresh_snapshot)
        execute_ok(fresh_snapshot, "COMMIT;")
        if latest != 30:
            raise AssertionError(
                "a new snapshot did not observe a committed RC update: "
                f"expected 30, got {latest}"
            )
    finally:
        try:
            fresh_snapshot.execute("ROLLBACK;")
        except Exception:
            pass
        fresh_snapshot.close()


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Verify that set output_file off does not change isolation for "
            "later connections, while explicit snapshot isolation still works."
        )
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="output_file_isolation_probe_db")
    parser.add_argument(
        "--server-log",
        type=Path,
        default=REPO_ROOT / "build" / "output_file_isolation_probe.log",
    )
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    return parser.parse_args()


def main():
    args = parse_args()
    process = None
    log_handle = None
    try:
        if args.start_server:
            process, log_handle, _ = start_server(args)
        setup(args)
        verify_default_read_committed(args)
        verify_explicit_snapshot(args)
        print(
            "output-file isolation independence probe passed: "
            "default=READ_COMMITTED, explicit=SNAPSHOT_ISOLATION"
        )
        return 0
    finally:
        if args.start_server:
            stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
