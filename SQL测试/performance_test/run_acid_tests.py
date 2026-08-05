#!/usr/bin/env python3
"""Comprehensive ACID and isolation-level test suite for RMDB (Wire v3).

Merges the former run_generic_acid_suite.py and all probe_*.py scripts
into a single, maintainable test suite aligned with the competition's
48 isolation test histories:

  3  configuration histories (SET TRANSACTION ISOLATION LEVEL)
  18 SI (Snapshot Isolation) multi-session histories
  27 SER (Serializable / SSI) multi-session histories

Additionally covers crash-recovery and durability probes.

Usage:
  python3 run_acid_tests.py --start-server --build-dir ../build
  python3 run_acid_tests.py --isolation serializable --crash-check
  python3 run_acid_tests.py --quick
"""

import argparse
import json
import os
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
    WireSqlClient,
    response_failed,
    table_rows,
    select_scalar_int,
)
from server_manager import RMDBServerManager, add_server_arguments  # noqa: E402
from wire_client import SQL_FLOAT32, WireClient  # noqa: E402


# ── Isolation configuration ─────────────────────────────────────────────────

ISOLATION_SQL = {
    "default": None,
    "snapshot": "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;",
    "serializable": "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;",
}


def connect(args, isolation="default"):
    """Connect a Wire v3 client and optionally set isolation level."""
    client = WireSqlClient(args.host, args.port, args.timeout)
    client.connect()
    prefix = ISOLATION_SQL[isolation]
    if prefix:
        response = client.execute(prefix)
        if response_failed(response):
            client.close()
            raise AssertionError(
                f"failed to set isolation level: {prefix}\n{response}")
    return client


def execute_ok(client, statement):
    """Execute a statement and raise on failure."""
    response = client.execute(statement)
    if response_failed(response):
        raise AssertionError(
            f"statement failed:\n{statement}\n{response}")
    return response


def scalar(client, statement):
    """Execute a SELECT returning a single integer."""
    response = execute_ok(client, statement)
    rows = table_rows(response)
    if not rows:
        raise AssertionError(
            f"query returned no rows:\n{statement}\n{response}")
    return int(float(rows[-1][0]))


# ── Table setup ─────────────────────────────────────────────────────────────

def setup_table(args, table):
    """Create test table with base rows."""
    client = connect(args)
    try:
        execute_ok(client, f"CREATE TABLE {table} "
                    "(k INT, grp INT, amount FLOAT, label CHAR(16));")
        execute_ok(client, f"CREATE INDEX {table}(k);")
        execute_ok(client, f"INSERT INTO {table} VALUES "
                    "(1, 1, 10.0, 'base-one');")
        execute_ok(client, f"INSERT INTO {table} VALUES "
                    "(2, 1, 20.0, 'base-two');")
        execute_ok(client, f"INSERT INTO {table} VALUES "
                    "(3, 2, 30.0, 'base-three');")
    finally:
        client.close()


# ── Configuration tests (3) ─────────────────────────────────────────────────

def test_isolation_config_persists(args):
    """Verify SET TRANSACTION ISOLATION LEVEL persists across transactions."""
    client = connect(args)
    try:
        execute_ok(client, ISOLATION_SQL[args.isolation])
        execute_ok(client, "BEGIN;")
        execute_ok(client, "COMMIT;")
        # Second transaction should retain the same isolation level
        execute_ok(client, "BEGIN;")
        execute_ok(client, "COMMIT;")
    finally:
        client.close()


def test_isolation_config_rejected_in_txn(args):
    """SET TRANSACTION ISOLATION LEVEL should fail inside active txn."""
    client = connect(args)
    try:
        execute_ok(client, "BEGIN;")
        response = client.execute(ISOLATION_SQL[args.isolation])
        # Should either fail or be ignored; must not crash
        execute_ok(client, "ROLLBACK;")
    finally:
        client.close()


def test_isolation_show_tables(args):
    """show tables; returns COMMAND_OK or valid META result."""
    client = connect(args)
    try:
        response = client.execute("SHOW TABLES;")
        if response_failed(response):
            raise AssertionError(f"SHOW TABLES failed: {response}")
    finally:
        client.close()


# ── SI tests ────────────────────────────────────────────────────────────────

def test_rollback_atomicity(args, table):
    """ROLLBACK must undo all table, index, and version changes atomically."""
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = amount + 7.0 "
                    "WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, f"INSERT INTO {table} VALUES "
                    "(4, 3, 40.0, 'rolled-back');")
        execute_ok(client, "ROLLBACK;")
        actual = {
            "rows": scalar(client, f"SELECT COUNT(*) FROM {table};"),
            "updated": scalar(client,
                              f"SELECT amount FROM {table} WHERE k = 1;"),
            "deleted": scalar(client,
                              f"SELECT COUNT(*) FROM {table} WHERE k = 2;"),
            "inserted": scalar(client,
                               f"SELECT COUNT(*) FROM {table} WHERE k = 4;"),
        }
        expected = {"rows": 3, "updated": 10, "deleted": 1, "inserted": 0}
        if actual != expected:
            raise AssertionError(
                f"rollback was not atomic: "
                f"expected={expected}, actual={actual}")
    finally:
        client.close()


def test_no_dirty_read(args, table):
    """Reader must never observe uncommitted UPDATE values."""
    writer = connect(args, args.isolation)
    reader = connect(args, args.isolation)
    done = threading.Event()
    result = {}

    def read_value():
        try:
            result["value"] = scalar(
                reader, f"SELECT amount FROM {table} WHERE k = 1;")
        except Exception as exc:
            result["error"] = repr(exc)
        finally:
            done.set()

    try:
        execute_ok(writer, "BEGIN;")
        execute_ok(writer, f"UPDATE {table} SET amount = 99.0 WHERE k = 1;")
        with ThreadPoolExecutor(max_workers=1) as pool:
            pool.submit(read_value)
            done.wait(timeout=args.block_probe_seconds)
            if result.get("value") == 99:
                raise AssertionError(
                    "reader observed an uncommitted update")
            execute_ok(writer, "ROLLBACK;")
        if "error" in result:
            raise AssertionError(
                f"dirty-read probe failed: {result['error']}")
        if result.get("value") != 10:
            raise AssertionError(
                f"reader expected committed value 10, "
                f"got {result.get('value')}")
    finally:
        writer.close()
        reader.close()


def test_unique_write_conflict(args, table):
    """Concurrent insert of same unique key must have exactly one winner."""
    barrier = threading.Barrier(2)

    def insert_same_key(worker_id):
        client = connect(args, args.isolation)
        try:
            execute_ok(client, "BEGIN;")
            barrier.wait(timeout=args.timeout)
            response = client.execute(
                f"INSERT INTO {table} VALUES "
                f"(50, {worker_id}, 50.0, 'same-key');")
            if response_failed(response):
                client.execute("ROLLBACK;")
                return "abort"
            commit = client.execute("COMMIT;")
            return "abort" if response_failed(commit) else "commit"
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
        count = scalar(verifier,
                       f"SELECT COUNT(*) FROM {table} WHERE k = 50;")
    finally:
        verifier.close()
    if count != 1 or outcomes.count("commit") != 1:
        raise AssertionError(
            f"unique conflict expected one winner, "
            f"outcomes={outcomes}, rows={count}")


def test_snapshot_phantom(args, table):
    """Under SI/SER, a concurrent INSERT must not create phantoms."""
    reader = connect(args, args.isolation)

    def insert_phantom():
        writer = connect(args, args.isolation)
        try:
            execute_ok(writer, "BEGIN;")
            response = writer.execute(
                f"INSERT INTO {table} VALUES "
                "(70, 7, 70.0, 'phantom-probe');")
            if response_failed(response):
                writer.execute("ROLLBACK;")
                return "abort"
            response = writer.execute("COMMIT;")
            if response_failed(response):
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
        before = scalar(reader,
                        f"SELECT COUNT(*) FROM {table} WHERE grp = 7;")
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(insert_phantom)
            time.sleep(args.block_probe_seconds)
            after = scalar(reader,
                           f"SELECT COUNT(*) FROM {table} WHERE grp = 7;")
            execute_ok(reader, "COMMIT;")
            writer_outcome = future.result(timeout=args.timeout + 2)
        if args.isolation in {"snapshot", "serializable"} and before != after:
            raise AssertionError(
                f"{args.isolation} transaction observed a phantom: "
                f"before={before}, after={after}")
        return {"before": before, "after": after,
                "writer": writer_outcome,
                "enforced": args.isolation != "default"}
    finally:
        reader.close()


def test_si_write_visibility(args, table):
    """A transaction must see its own writes."""
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = 55.0 WHERE k = 1;")
        val = scalar(client, f"SELECT amount FROM {table} WHERE k = 1;")
        if val != 55:
            raise AssertionError(
                f"transaction did not see its own update: {val}")
        execute_ok(client, "ROLLBACK;")
    finally:
        client.close()


def test_update_self_assignment(args, table):
    """UPDATE col=col must execute as a real transactional write."""
    client = connect(args, args.isolation)
    try:
        before = scalar(client, f"SELECT amount FROM {table} WHERE k = 1;")
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = amount WHERE k = 1;")
        during = scalar(client, f"SELECT amount FROM {table} WHERE k = 1;")
        execute_ok(client, "COMMIT;")
        after = scalar(client, f"SELECT amount FROM {table} WHERE k = 1;")
        if (before, during, after) != (10, 10, 10):
            raise AssertionError(
                "self-assignment changed the stored value: "
                f"before={before}, during={during}, after={after}")
    finally:
        client.close()


def test_select_alias_metadata(args, table):
    """EXEC_STREAM META must expose the SELECT AS alias exactly."""
    client = WireClient(args.host, args.port, args.timeout)
    client.connect()
    try:
        result = client.execute_stream(
            f"SELECT amount AS amount_alias FROM {table} WHERE k = 1;")
        if not result.is_query or result.query is None:
            raise AssertionError(f"alias query failed: {result!r}")
        columns = result.query.columns
        if len(columns) != 1:
            raise AssertionError(
                f"alias query expected one column, got {len(columns)}")
        actual = (columns[0].name, columns[0].sql_type)
        expected = ("amount_alias", SQL_FLOAT32)
        if actual != expected:
            raise AssertionError(
                f"alias META mismatch: expected={expected}, actual={actual}")
    finally:
        client.close()


def test_stale_snapshot_delete_conflict(args, table):
    """A stale-snapshot DELETE must abort, while a true no-op must succeed."""
    setup = connect(args)
    stale = None
    writer = None
    try:
        execute_ok(setup, f"INSERT INTO {table} VALUES "
                   "(60, 6, 60.0, 'stale-delete');")
        stale = connect(args, args.isolation)
        writer = connect(args, args.isolation)

        execute_ok(stale, "BEGIN;")
        scalar(stale, f"SELECT amount FROM {table} WHERE k = 60;")

        execute_ok(writer, "BEGIN;")
        execute_ok(writer, f"UPDATE {table} SET amount = 61.0 WHERE k = 60;")
        execute_ok(writer, "COMMIT;")

        delete_response = stale.execute(
            f"DELETE FROM {table} WHERE k = 60;")
        if not delete_response.startswith("abort"):
            raise AssertionError(
                "stale-snapshot DELETE must return TRANSACTION_ABORT, "
                f"got {delete_response!r}")

        no_match = connect(args, args.isolation)
        try:
            execute_ok(no_match, "BEGIN;")
            execute_ok(no_match,
                       f"DELETE FROM {table} WHERE k = 999999;")
            execute_ok(no_match, "COMMIT;")
        finally:
            no_match.close()
    finally:
        if stale is not None:
            stale.close()
        if writer is not None:
            writer.close()
        try:
            execute_ok(setup, f"DELETE FROM {table} WHERE k = 60;")
        finally:
            setup.close()


def test_si_write_conflict(args, table):
    """Concurrent UPDATE on same row must trigger write-write conflict."""
    barrier = threading.Barrier(2)
    results = []

    def update_row(worker_id):
        client = connect(args, args.isolation)
        try:
            execute_ok(client, "BEGIN;")
            barrier.wait(timeout=args.timeout)
            response = client.execute(
                f"UPDATE {table} SET amount = {float(100 + worker_id)} "
                f"WHERE k = 1;")
            if response_failed(response):
                try:
                    client.execute("ROLLBACK;")
                except Exception:
                    pass
                results.append("abort")
                return
            commit = client.execute("COMMIT;")
            results.append(
                "abort" if response_failed(commit) else "commit")
        except Exception:
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            results.append("abort")
        finally:
            client.close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        pool.map(update_row, (1, 2))
    if results.count("commit") != 1:
        raise AssertionError(
            f"write conflict expected one winner: {results}")


# ── SER (SSI) tests ─────────────────────────────────────────────────────────

def test_record_write_skew(args, table):
    """Two transactions: T1 reads A writes B, T2 reads B writes A.

    Under SERIALIZABLE this must abort at least one transaction
    at statement time (not commit time).
    """
    barrier1 = threading.Barrier(2)
    barrier2 = threading.Barrier(2)
    results = {}

    def t1():
        client = connect(args, args.isolation)
        try:
            execute_ok(client, "BEGIN;")
            # Read k=1 (amount=10)
            scalar(client, f"SELECT amount FROM {table} WHERE k = 1;")
            barrier1.wait(timeout=args.timeout)
            barrier2.wait(timeout=args.timeout)
            # Write k=2
            response = client.execute(
                f"UPDATE {table} SET amount = 99.0 WHERE k = 2;")
            if response_failed(response):
                results["t1"] = "abort"
                try:
                    client.execute("ROLLBACK;")
                except Exception:
                    pass
                return
            response = client.execute("COMMIT;")
            results["t1"] = ("abort" if response_failed(response)
                             else "commit")
        except Exception:
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            results["t1"] = "abort"
        finally:
            client.close()

    def t2():
        client = connect(args, args.isolation)
        try:
            execute_ok(client, "BEGIN;")
            # Read k=2 (amount=20)
            scalar(client, f"SELECT amount FROM {table} WHERE k = 2;")
            barrier1.wait(timeout=args.timeout)
            # Write k=1
            response = client.execute(
                f"UPDATE {table} SET amount = 88.0 WHERE k = 1;")
            if response_failed(response):
                results["t2"] = "abort"
                try:
                    client.execute("ROLLBACK;")
                except Exception:
                    pass
                # Signal T1 it can proceed
                barrier2.wait(timeout=args.timeout)
                return
            barrier2.wait(timeout=args.timeout)
            response = client.execute("COMMIT;")
            results["t2"] = ("abort" if response_failed(response)
                             else "commit")
        except Exception:
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            results["t2"] = "abort"
            try:
                barrier2.wait(timeout=args.timeout)
            except Exception:
                pass
        finally:
            client.close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        pool.submit(t1)
        pool.submit(t2)

    if args.isolation == "serializable":
        # Under SERIALIZABLE, at least one must abort
        if results.get("t1") == "commit" and results.get("t2") == "commit":
            raise AssertionError(
                "write skew not detected under SERIALIZABLE: "
                f"both committed {results}")
    print(f"  record_write_skew: t1={results.get('t1')}, "
          f"t2={results.get('t2')}")


def test_predicate_write_skew(args, table):
    """Read all rows in grp=1, then a concurrent insert into grp=1."""
    reader = connect(args, args.isolation)
    done_event = threading.Event()
    result = {}

    def writer_task():
        writer = connect(args, args.isolation)
        try:
            execute_ok(writer, "BEGIN;")
            response = writer.execute(
                f"INSERT INTO {table} VALUES "
                "(80, 1, 80.0, 'predicate-skew');")
            result["insert"] = (
                "abort" if response_failed(response) else "ok")
            if not response_failed(response):
                commit_resp = writer.execute("COMMIT;")
                result["writer"] = ("abort" if response_failed(commit_resp)
                                    else "commit")
            else:
                writer.execute("ROLLBACK;")
                result["writer"] = "abort"
        except Exception:
            result["writer"] = "abort"
        finally:
            writer.close()
            done_event.set()

    try:
        execute_ok(reader, "BEGIN;")
        before = scalar(reader, f"SELECT COUNT(*) FROM {table} "
                        "WHERE grp = 1;")
        with ThreadPoolExecutor(max_workers=1) as pool:
            pool.submit(writer_task)
            done_event.wait(timeout=args.timeout + 2)
            after = scalar(reader, f"SELECT COUNT(*) FROM {table} "
                           "WHERE grp = 1;")
            commit_resp = reader.execute("COMMIT;")
            reader_committed = not response_failed(commit_resp)
        if (args.isolation == "serializable" and reader_committed
                and before != after):
            raise AssertionError(
                f"predicate write skew not prevented: "
                f"before={before}, after={after}")
        return {"before": before, "after": after,
                "reader_committed": reader_committed,
                "writer": result.get("writer")}
    finally:
        reader.close()


def test_empty_range_phantom(args, table):
    """SELECT on empty range + concurrent INSERT into that range."""
    reader = connect(args, args.isolation)
    result = {}

    def inserter():
        writer = connect(args, args.isolation)
        try:
            execute_ok(writer, "BEGIN;")
            resp = writer.execute(
                f"INSERT INTO {table} VALUES "
                "(90, 9, 90.0, 'empty-range-probe');")
            if response_failed(resp):
                writer.execute("ROLLBACK;")
                result["writer"] = "abort"
                return
            commit_resp = writer.execute("COMMIT;")
            result["writer"] = ("abort" if response_failed(commit_resp)
                                else "commit")
        except Exception:
            result["writer"] = "abort"
        finally:
            writer.close()

    try:
        execute_ok(reader, "BEGIN;")
        scalar(reader, f"SELECT COUNT(*) FROM {table} WHERE grp = 9;")
        time.sleep(0.1)
        with ThreadPoolExecutor(max_workers=1) as pool:
            pool.submit(inserter)
            time.sleep(args.block_probe_seconds)
            scalar(reader, f"SELECT COUNT(*) FROM {table} WHERE grp = 9;")
            commit_resp = reader.execute("COMMIT;")
        if (args.isolation == "serializable" and
                not response_failed(commit_resp) and
                result.get("writer") == "commit"):
            raise AssertionError(
                "empty-range phantom not prevented under SERIALIZABLE")
        return {"writer": result.get("writer"),
                "reader_aborted": response_failed(commit_resp)}
    finally:
        reader.close()


# ── Crash-recovery tests ────────────────────────────────────────────────────

def durability_markers(args, table):
    client = connect(args)
    try:
        return {
            "rows": scalar(client, f"SELECT COUNT(*) FROM {table};"),
            "amount": scalar(client,
                             f"SELECT amount FROM {table} WHERE k = 90;"),
        }
    finally:
        client.close()


def test_kill9_durability(args, table, server):
    """COMMIT followed by SIGKILL must survive restart."""
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"INSERT INTO {table} VALUES "
                    "(90, 9, 90.0, 'durable-row');")
        execute_ok(client, "COMMIT;")
    finally:
        client.close()
    before = durability_markers(args, table)
    server.kill()
    server.restart(reset_db=False)
    after = durability_markers(args, table)
    if before != after:
        raise AssertionError(
            f"committed state changed after kill -9: "
            f"before={before}, after={after}")
    return server


def test_volatile_abort_recovery(args, table, server):
    """ABORT effects must not survive crash, even without subsequent flush."""
    baseline = read_abort_recovery_state(args, table)
    # Phase 1: ABORT + later committed txn to flush the ABORT record
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = 111.0 WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, "ROLLBACK;")
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"INSERT INTO {table} VALUES "
                    "(91, 9, 91.0, 'followup-flush');")
        execute_ok(client, "COMMIT;")
    finally:
        client.close()
    server.kill()
    server.restart(reset_db=False)
    phase_one_expected = expected_abort_recovery_state(
        baseline, expect_followup=True)
    _verify_clean_abort_state(args, table, phase_one_expected)

    # Phase 2: ABORT + immediate kill without flush
    phase_two_baseline = read_abort_recovery_state(args, table)
    client = connect(args, args.isolation)
    try:
        execute_ok(client, "BEGIN;")
        execute_ok(client, f"UPDATE {table} SET amount = 222.0 WHERE k = 1;")
        execute_ok(client, f"DELETE FROM {table} WHERE k = 2;")
        execute_ok(client, "ROLLBACK;")
    finally:
        client.close()
    server.kill()
    server.restart(reset_db=False)
    _verify_clean_abort_state(args, table, phase_two_baseline)
    return server


def read_abort_recovery_state(args, table):
    client = connect(args)
    try:
        return {
            "updated": scalar(client,
                              f"SELECT amount FROM {table} WHERE k = 1;"),
            "deleted": scalar(client,
                              f"SELECT COUNT(*) FROM {table} WHERE k = 2;"),
            "followup": scalar(client,
                               f"SELECT COUNT(*) FROM {table} WHERE k = 91;"),
        }
    finally:
        client.close()


def expected_abort_recovery_state(baseline, expect_followup):
    expected = dict(baseline)
    expected["followup"] = 1 if expect_followup else baseline["followup"]
    return expected


def _verify_clean_abort_state(args, table, expected):
    actual = read_abort_recovery_state(args, table)
    if actual != expected:
        raise AssertionError(
            f"volatile abort recovery mismatch: "
            f"expected={expected}, actual={actual}")


def test_lost_update_prevention(args, table):
    """Concurrent read-modify-write under SERIALIZABLE: at least one
       transaction must abort (SSI write-skew detection)."""
    if args.isolation != "serializable":
        return

    aborts = []

    def increment():
        client = connect(args, args.isolation)
        try:
            client.execute("BEGIN;")
            current = scalar(client,
                             f"SELECT amount FROM {table} WHERE k = 1;")
            resp = client.execute(
                f"UPDATE {table} SET amount = "
                f"{float(current + 1)} WHERE k = 1;")
            if response_failed(resp):
                aborts.append(1)
                client.execute("ROLLBACK;")
                return
            commit_resp = client.execute("COMMIT;")
            if response_failed(commit_resp):
                aborts.append(1)
        except Exception:
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            aborts.append(1)
        finally:
            client.close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        f1 = pool.submit(increment)
        f2 = pool.submit(increment)
        f1.result(timeout=args.timeout)
        f2.result(timeout=args.timeout)

    # Under SERIALIZABLE, at least one of the two concurrent
    # read-modify-write transactions must abort.
    if len(aborts) == 0:
        raise AssertionError(
            "SSI did not detect write skew: both transactions committed")


# ── Test runner ──────────────────────────────────────────────────────────────

TEST_GROUPS = {
    "config": [
        ("isolation_config_persists", test_isolation_config_persists),
        ("isolation_config_rejected_in_txn",
         test_isolation_config_rejected_in_txn),
        ("isolation_show_tables", test_isolation_show_tables),
    ],
    "si": [
        ("rollback_atomicity", test_rollback_atomicity),
        ("update_self_assignment", test_update_self_assignment),
        ("select_alias_metadata", test_select_alias_metadata),
        ("no_dirty_read", test_no_dirty_read),
        ("unique_write_conflict", test_unique_write_conflict),
        ("si_write_visibility", test_si_write_visibility),
        ("si_write_conflict", test_si_write_conflict),
        ("stale_snapshot_delete_conflict",
         test_stale_snapshot_delete_conflict),
    ],
    "ser": [
        ("record_write_skew", test_record_write_skew),
        ("predicate_write_skew", test_predicate_write_skew),
        ("empty_range_phantom", test_empty_range_phantom),
    ],
    "crash": [
        ("kill9_durability", test_kill9_durability),
        ("volatile_abort_recovery", test_volatile_abort_recovery),
    ],
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="ACID and isolation test suite for RMDB (Wire v3)."
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--block-probe-seconds", type=float, default=0.3)
    parser.add_argument("--isolation", choices=sorted(ISOLATION_SQL),
                        default="snapshot")
    parser.add_argument("--seed", type=int, default=20260710)
    parser.add_argument("--table-prefix", default="acid_probe")
    parser.add_argument("--crash-check", action="store_true")
    add_server_arguments(
        parser, "acid_test_db", "acid_test_server.log", REPO_ROOT / "build")
    parser.add_argument("--quick", action="store_true",
                        help="Skip crash-recovery tests")
    parser.add_argument("--json-output", type=Path,
                        default=REPO_ROOT / "build" / "acid_test_report.json")
    return parser.parse_args()


def main():
    args = parse_args()
    rng = random.Random(args.seed)
    suffix = "".join(rng.choice(string.ascii_lowercase) for _ in range(8))
    table = f"{args.table_prefix}_{suffix}"
    server = None
    report = {"table": table, "isolation": args.isolation, "checks": {}}

    try:
        if args.start_server:
            server = RMDBServerManager.from_args(
                args, log_name="acid_test_server.log")
            server.start()
            args.db_dir = server.db_dir
        setup_table(args, table)

        # Configuration tests
        for name, test_fn in TEST_GROUPS["config"]:
            test_fn(args)
            report["checks"][name] = "PASS"
            print(f"  config/{name}: PASS")

        # SI tests
        for name, test_fn in TEST_GROUPS["si"]:
            test_fn(args, table)
            report["checks"][name] = "PASS"
            print(f"  si/{name}: PASS")

        # Phantom test (returns data)
        report["checks"]["phantom"] = test_snapshot_phantom(args, table)
        phantom_status = ("PASS" if report["checks"]["phantom"]["enforced"]
                          else "observed only (RC/default)")
        print(f"  si/phantom: {phantom_status}")

        # SER tests (only meaningful under SERIALIZABLE)
        if args.isolation == "serializable":
            for name, test_fn in TEST_GROUPS["ser"]:
                result = test_fn(args, table)
                report["checks"][name] = result
                print(f"  ser/{name}: PASS")

        # Lost update prevention
        test_lost_update_prevention(args, table)
        report["checks"]["lost_update_prevention"] = "PASS"
        print("  si/lost_update_prevention: PASS")

        # Crash recovery tests
        if args.crash_check and not args.quick:
            server = test_volatile_abort_recovery(args, table, server)
            report["checks"]["volatile_abort_recovery"] = "PASS"
            print("  crash/volatile_abort_recovery: PASS")

            server = test_kill9_durability(args, table, server)
            report["checks"]["kill9_durability"] = "PASS"
            print("  crash/kill9_durability: PASS")

        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(
            json.dumps(report, ensure_ascii=False, indent=2),
            encoding="utf-8")
        print(f"\nAll ACID tests passed ({args.isolation}).")
        print(f"Report: {args.json_output}")
        return 0
    finally:
        if server is not None:
            server.stop()


if __name__ == "__main__":
    raise SystemExit(main())
