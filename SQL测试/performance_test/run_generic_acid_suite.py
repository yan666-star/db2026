#!/usr/bin/env python3
"""Generic ACID/concurrency checks using generated table and index names."""

import argparse
import json
import random
import string
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import (  # noqa: E402
    SqlClient,
    response_failed,
    start_server,
    stop_server,
    table_rows,
)


ISOLATION_SQL = {
    "default": None,
    "snapshot": "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;",
    "serializable": "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;",
}


def failed(response):
    lowered = response.lower()
    return response_failed(response) or "error" in lowered or "exception" in lowered


def execute_ok(client, statement):
    response = client.execute(statement)
    if failed(response):
        raise AssertionError(f"statement failed:\n{statement}\n{response}")
    return response


def scalar(client, statement):
    response = execute_ok(client, statement)
    rows = table_rows(response)
    if not rows:
        raise AssertionError(f"query returned no rows:\n{statement}\n{response}")
    return int(float(rows[-1][0]))


def connect(args, isolation="default"):
    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    prefix = ISOLATION_SQL[isolation]
    if prefix:
        execute_ok(client, prefix)
    execute_ok(client, "set output_file off")
    return client


def setup_table(args, table):
    client = connect(args)
    try:
        execute_ok(
            client,
            f"CREATE TABLE {table} (k INT, grp INT, amount FLOAT, label CHAR(16));",
        )
        execute_ok(client, f"CREATE INDEX {table}(k);")
        execute_ok(client, f"INSERT INTO {table} VALUES (1, 1, 10.0, 'base-one');")
        execute_ok(client, f"INSERT INTO {table} VALUES (2, 1, 20.0, 'base-two');")
        execute_ok(client, f"INSERT INTO {table} VALUES (3, 2, 30.0, 'base-three');")
    finally:
        client.close()


def test_rollback_atomicity(args, table):
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = amount + 7.0 WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, f"INSERT INTO {table} VALUES (4, 3, 40.0, 'rolled-back');")
        execute_ok(client, "ROLLBACK;")
        actual = {
            "rows": scalar(client, f"SELECT COUNT(*) FROM {table};"),
            "updated_value": scalar(client, f"SELECT amount FROM {table} WHERE k = 1;"),
            "deleted_row": scalar(client, f"SELECT COUNT(*) FROM {table} WHERE k = 2;"),
            "inserted_row": scalar(client, f"SELECT COUNT(*) FROM {table} WHERE k = 4;"),
        }
        expected = {"rows": 3, "updated_value": 10, "deleted_row": 1, "inserted_row": 0}
        if actual != expected:
            raise AssertionError(f"rollback was not atomic: expected={expected}, actual={actual}")
    finally:
        client.close()


def test_volatile_update_delete_abort(args, table):
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = 77.0 WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, "ROLLBACK;")
        actual = {
            "updated_value": scalar(client, f"SELECT amount FROM {table} WHERE k = 1;"),
            "deleted_row": scalar(client, f"SELECT COUNT(*) FROM {table} WHERE k = 2;"),
        }
        expected = {"updated_value": 10, "deleted_row": 1}
        if actual != expected:
            raise AssertionError(
                f"volatile update/delete abort leaked state: expected={expected}, actual={actual}"
            )
    finally:
        client.close()


def test_no_dirty_read(args, table):
    writer = connect(args, args.isolation)
    reader = connect(args, args.isolation)
    done = threading.Event()
    result = {}

    def read_value():
        try:
            result["value"] = scalar(reader, f"SELECT amount FROM {table} WHERE k = 1;")
        except Exception as exc:  # diagnostic is reported by the main thread
            result["error"] = repr(exc)
        finally:
            done.set()

    try:
        execute_ok(writer, "BEGIN;")
        execute_ok(writer, f"UPDATE {table} SET amount = 99.0 WHERE k = 1;")
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(read_value)
            done.wait(timeout=args.block_probe_seconds)
            if result.get("value") == 99:
                raise AssertionError("reader observed an uncommitted update")
            execute_ok(writer, "ROLLBACK;")
            future.result(timeout=args.timeout + 2)
        if "error" in result:
            raise AssertionError(f"dirty-read probe failed: {result['error']}")
        if result.get("value") != 10:
            raise AssertionError(f"reader expected committed value 10, got {result.get('value')}")
    finally:
        writer.close()
        reader.close()


def test_unique_write_conflict(args, table):
    barrier = threading.Barrier(2)

    def insert_same_key(worker_id):
        client = connect(args, args.isolation)
        try:
            execute_ok(client, "BEGIN;")
            barrier.wait(timeout=args.timeout)
            response = client.execute(
                f"INSERT INTO {table} VALUES (50, {worker_id}, 50.0, 'same-key');"
            )
            if failed(response):
                client.execute("ROLLBACK;")
                return "abort"
            commit = client.execute("COMMIT;")
            return "abort" if failed(commit) else "commit"
        except Exception:
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            return "abort"
        finally:
            client.close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        outcomes = list(pool.map(insert_same_key, (1, 2)))
    verifier = connect(args)
    try:
        count = scalar(verifier, f"SELECT COUNT(*) FROM {table} WHERE k = 50;")
    finally:
        verifier.close()
    if count != 1 or outcomes.count("commit") != 1:
        raise AssertionError(f"unique conflict expected one winner, outcomes={outcomes}, rows={count}")


def test_snapshot_phantom(args, table):
    reader = connect(args, args.isolation)

    def insert_phantom():
        writer = connect(args, args.isolation)
        try:
            execute_ok(writer, "BEGIN;")
            response = writer.execute(
                f"INSERT INTO {table} VALUES (70, 7, 70.0, 'phantom-probe');"
            )
            if failed(response):
                writer.execute("ROLLBACK;")
                return "abort"
            response = writer.execute("COMMIT;")
            if failed(response):
                writer.execute("ROLLBACK;")
                return "abort"
            return "commit"
        except Exception:
            try:
                writer.execute("ROLLBACK;")
            except Exception:
                pass
            return "abort"
        finally:
            writer.close()

    try:
        execute_ok(reader, "BEGIN;")
        before = scalar(reader, f"SELECT COUNT(*) FROM {table} WHERE grp = 7;")
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(insert_phantom)
            time.sleep(args.block_probe_seconds)
            after = scalar(reader, f"SELECT COUNT(*) FROM {table} WHERE grp = 7;")
            execute_ok(reader, "COMMIT;")
            writer_outcome = future.result(timeout=args.timeout + 2)
        if args.isolation in {"snapshot", "serializable"} and before != after:
            raise AssertionError(
                f"{args.isolation} transaction observed a phantom: before={before}, after={after}"
            )
        return {
            "before": before,
            "after": after,
            "writer": writer_outcome,
            "enforced": args.isolation != "default",
        }
    finally:
        reader.close()


def durability_markers(args, table):
    client = connect(args)
    try:
        return {
            "rows": scalar(client, f"SELECT COUNT(*) FROM {table};"),
            "amount": scalar(client, f"SELECT amount FROM {table} WHERE k = 90;"),
        }
    finally:
        client.close()


def test_kill9_durability(args, table, process, log_handle):
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"INSERT INTO {table} VALUES (90, 9, 90.0, 'durable-row');")
        execute_ok(client, "COMMIT;")
    finally:
        client.close()
    before = durability_markers(args, table)
    process.kill()
    process.wait(timeout=5)
    log_handle.close()
    old_reset = args.reset_db
    args.reset_db = False
    try:
        process, log_handle, _ = start_server(args)
    finally:
        args.reset_db = old_reset
    after = durability_markers(args, table)
    if before != after:
        raise AssertionError(f"committed state changed after kill -9: before={before}, after={after}")
    return process, log_handle


def restart_after_kill(args, process, log_handle):
    process.kill()
    process.wait(timeout=5)
    log_handle.close()
    old_reset = args.reset_db
    args.reset_db = False
    try:
        return start_server(args)[:2]
    finally:
        args.reset_db = old_reset


def verify_volatile_abort_state(args, table, expect_followup):
    client = connect(args)
    try:
        actual = {
            "updated_value": scalar(client, f"SELECT amount FROM {table} WHERE k = 1;"),
            "deleted_row": scalar(client, f"SELECT COUNT(*) FROM {table} WHERE k = 2;"),
            "followup_row": scalar(client, f"SELECT COUNT(*) FROM {table} WHERE k = 91;"),
        }
    finally:
        client.close()
    expected = {
        "updated_value": 10,
        "deleted_row": 1,
        "followup_row": 1 if expect_followup else 0,
    }
    if actual != expected:
        raise AssertionError(
            f"volatile abort recovery mismatch: expected={expected}, actual={actual}"
        )


def test_volatile_abort_recovery(args, table, process, log_handle):
    # First let a later committed transaction flush the buffered ABORT and its
    # preceding records, then crash and verify both rollback and durability.
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = 111.0 WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, "ROLLBACK;")
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"INSERT INTO {table} VALUES (91, 9, 91.0, 'followup-flush');")
        execute_ok(client, "COMMIT;")
    finally:
        client.close()
    process, log_handle = restart_after_kill(args, process, log_handle)
    verify_volatile_abort_state(args, table, True)

    # Then abort another pending UPDATE/DELETE and kill immediately, before any
    # later transaction is allowed to force its ABORT record to disk.
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = 222.0 WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, "ROLLBACK;")
    finally:
        client.close()
    process, log_handle = restart_after_kill(args, process, log_handle)
    verify_volatile_abort_state(args, table, True)
    return process, log_handle


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run table-name-independent transaction, isolation and recovery checks."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--block-probe-seconds", type=float, default=0.3)
    parser.add_argument("--isolation", choices=sorted(ISOLATION_SQL), default="snapshot")
    parser.add_argument("--seed", type=int, default=20260710)
    parser.add_argument("--table-prefix", default="acid_probe")
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--crash-check", action="store_true")
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="generic_acid_suite_db")
    parser.add_argument("--db-dir", type=Path)
    parser.add_argument("--server-log", type=Path, default=REPO_ROOT / "build" / "generic_acid_suite.log")
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    args = parser.parse_args()
    if args.crash_check and not args.start_server:
        parser.error("--crash-check requires --start-server")
    return args


def main():
    args = parse_args()
    rng = random.Random(args.seed)
    suffix = "".join(rng.choice(string.ascii_lowercase) for _ in range(8))
    table = f"{args.table_prefix}_{suffix}"
    process = None
    log_handle = None
    report = {"table": table, "isolation": args.isolation, "checks": {}}
    try:
        if args.start_server:
            process, log_handle, args.db_dir = start_server(args)
        setup_table(args, table)
        checks = [
            ("rollback_atomicity", test_rollback_atomicity),
            ("volatile_update_delete_abort", test_volatile_update_delete_abort),
            ("no_dirty_read", test_no_dirty_read),
            ("unique_write_conflict", test_unique_write_conflict),
        ]
        for name, check in checks:
            check(args, table)
            report["checks"][name] = "PASS"
            print(f"{name}: PASS")
        report["checks"]["phantom"] = test_snapshot_phantom(args, table)
        print("phantom: PASS" if report["checks"]["phantom"]["enforced"] else "phantom: observed only (RC/default)")
        if args.crash_check:
            process, log_handle = test_volatile_abort_recovery(
                args, table, process, log_handle
            )
            report["checks"]["volatile_abort_recovery"] = "PASS"
            print("volatile_abort_recovery: PASS")
            process, log_handle = test_kill9_durability(args, table, process, log_handle)
            report["checks"]["kill9_durability"] = "PASS"
            print("kill9_durability: PASS")
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
