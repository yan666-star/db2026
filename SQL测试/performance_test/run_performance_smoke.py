#!/usr/bin/env python3
"""Performance smoke test suite for RMDB (Wire Protocol v3).

This module provides both:
  - Standalone smoke tests (SQL files + concurrent consistency probes)
  - Shared utility functions imported by other test scripts

All network communication uses the Wire v3 protocol via wire_client.py.
"""

import argparse
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_SQL_FILES = [
    "00_create_tpcc_tables.sql",
    "01_load_tpcc_tables.sql",
    "02_create_primary_indexes.sql",
    "03_output_and_aggregate_smoke.sql",
    "04_tpcc_new_order_smoke.sql",
    "05_transaction_syntax_smoke.sql",
]

# ── Wire v3 client import ──────────────────────────────────────────────────

from wire_client import (
    WireClient,
    WireSqlClient,
    response_failed,
    table_rows,
    execute_explicit_txn as _wire_execute_explicit_txn,
)
from server_manager import RMDBServerManager, add_server_arguments

# Backward-compatible alias for scripts that import SqlClient from this module
SqlClient = WireSqlClient


# ── SQL file reading ────────────────────────────────────────────────────────

def read_statements(path: Path):
    """Read semicolon-terminated SQL statements from a file.

    Lines starting with '--' are treated as comments and skipped.
    The old 'set output_file off' command is silently ignored since
    output_file support was removed in Wire v3.
    """
    statements = []
    buffer = []

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("--"):
            continue

        lowered = line.lower()
        # Silently skip removed output_file commands
        if lowered == "set output_file off" or lowered == "set output_file on":
            continue

        buffer.append(line)
        joined = " ".join(buffer)
        while ";" in joined:
            statement, joined = joined.split(";", 1)
            statement = statement.strip()
            if statement:
                statements.append(statement + ";")
            joined = joined.strip()
        buffer = [joined] if joined else []

    if buffer:
        statement = " ".join(buffer).strip()
        raise ValueError(
            f"missing semicolon at end of statement in {path}: {statement}")

    return statements


# ── Query helpers ───────────────────────────────────────────────────────────

def select_scalar_int(client: WireSqlClient, statement: str):
    """Execute a SELECT that returns a single integer and return it."""
    response = client.execute(statement)
    if response_failed(response):
        raise AssertionError(f"select failed: {statement}\n{response}")
    rows = table_rows(response)
    if rows and rows[-1]:
        return int(float(rows[-1][0]))
    values = re.findall(r"-?\d+", response)
    if not values:
        raise AssertionError(
            f"no integer value returned for: {statement}\n{response}")
    return int(values[0])


def execute_explicit_txn(statements, host, port, timeout,
                         isolation_prefix=None):
    """Execute a sequence of statements within an explicit transaction.

    Returns (success: bool, [(statement, response), ...]).
    Compatible with the old API.
    """
    client = WireSqlClient(host, port, timeout)
    try:
        responses = []
        if isolation_prefix:
            response = client.execute(isolation_prefix)
            responses.append((isolation_prefix, response))
            if response_failed(response):
                return False, responses
        for statement in statements:
            response = client.execute(statement)
            responses.append((statement, response))
            if response_failed(response):
                try:
                    responses.append(
                        ("ROLLBACK;", client.execute("ROLLBACK;")))
                except Exception as exc:
                    responses.append(("ROLLBACK;", f"rollback error: {exc}"))
                return False, responses
        return True, responses
    finally:
        client.close()


# ── TPC-C transaction statement builders ────────────────────────────────────

SI_PREFIX = "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;"


def new_order_statements(order_id, district_id, quantities):
    """Build NewOrder transaction statements."""
    statements = [
        "BEGIN;",
        f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district_id};",
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
        f"WHERE d_w_id = 1 AND d_id = {district_id};",
        f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 2, "
        f"'2026-07-02 12:00:00', 0, {len(quantities)}, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
    ]
    for line_no, (item_id, qty) in enumerate(quantities, start=1):
        statements.extend([
            f"SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = {item_id};",
            f"UPDATE stock SET s_quantity = s_quantity - {qty} "
            f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
            f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, "
            f"{line_no}, {item_id}, 1, '2026-07-02 12:00:00', {qty}, "
            f"{float(qty) * 10.0}, 'probe-line-{line_no}');",
        ])
    statements.append("COMMIT;")
    return statements


def illegal_item_rollback_statements(order_id, district_id, item_id):
    """New-Order that rolls back after an invalid item lookup under SI."""
    return [
        "BEGIN;",
        f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district_id};",
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
        f"WHERE d_w_id = 1 AND d_id = {district_id};",
        f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 2, "
        f"'2026-07-02 12:00:00', 0, 1, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
        f"SELECT i_id FROM item WHERE i_id = {item_id};",
        "ROLLBACK;",
    ]


def failure_probe_statements(order_id, district_id, stage):
    """Build statements that intentionally fail at various stages."""
    statements = [
        "BEGIN;",
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
        f"WHERE d_w_id = 1 AND d_id = {district_id};",
    ]
    if stage == "district":
        statements.append(
            "INSERT INTO orders VALUES (11, 1, 1, 2, "
            "'2026-07-02 12:10:00', 0, 1, 1);")
        return statements

    statements.extend([
        f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 2, "
        f"'2026-07-02 12:10:00', 0, 2, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
    ])
    if stage == "orders":
        statements.append("INSERT INTO new_orders VALUES (11, 1, 1);")
        return statements

    statements.extend([
        "UPDATE stock SET s_quantity = s_quantity - 7 "
        "WHERE s_w_id = 1 AND s_i_id = 1;",
    ])
    if stage == "stock":
        statements.append(
            "INSERT INTO order_line VALUES (11, 1, 1, 1, 1, 1, "
            "'2026-07-02 12:10:00', 1, 1.0, 'dup-line');")
        return statements

    statements.extend([
        f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, "
        f"1, 1, 1, '2026-07-02 12:10:00', 7, 70.0, 'partial-line');",
        f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, "
        f"1, 1, 1, '2026-07-02 12:10:00', 7, 70.0, 'dup-partial-line');",
    ])
    return statements


# ── Smoke test runner ───────────────────────────────────────────────────────

def run_files(args):
    """Execute SQL files sequentially, validating responses."""
    sql_files = args.sql_files or [SCRIPT_DIR / name
                                   for name in DEFAULT_SQL_FILES]
    client = WireSqlClient(args.host, args.port, args.timeout)
    failed_explicit_txn = False

    try:
        for sql_file in sql_files:
            sql_file = sql_file.resolve()
            print(f"=== {sql_file.name} ===")
            for statement in read_statements(sql_file):
                if statement.lower().startswith("set output_file"):
                    continue

                print(f">>> {statement}")
                response = client.execute(statement)
                if response:
                    print(response.rstrip())

                lowered = statement.lower()
                expected_failure = False
                if (lowered.startswith("insert into orders") and
                        "'2026-07-01 10:00:03'" in lowered):
                    expected_failure = True
                    failed_explicit_txn = True
                elif (failed_explicit_txn and
                      lowered.startswith("insert into history")):
                    expected_failure = True
                elif failed_explicit_txn and lowered.startswith("commit"):
                    expected_failure = True
                    failed_explicit_txn = False
                elif failed_explicit_txn and (
                    lowered.startswith("rollback") or
                        lowered.startswith("abort")):
                    failed_explicit_txn = False

                if "failure" in response.lower() and not expected_failure:
                    raise AssertionError(
                        f"statement failed: {statement}")

                if lowered.startswith("select min(name)"):
                    if ("apple" not in response or
                            "pear" not in response):
                        raise AssertionError(
                            "string MIN/MAX probe did not return "
                            "apple and pear")

                if (lowered.startswith("select h_data") and
                        "2026-07-01 10:00:01" in lowered):
                    if "syntax-commit" not in response:
                        raise AssertionError(
                            "COMMIT TRANSACTION did not persist "
                            "the inserted row")

                if (lowered.startswith("select h_data") and
                        "2026-07-01 10:00:02" in lowered):
                    if "syntax-rollback" in response:
                        raise AssertionError(
                            "ROLLBACK WORK left an inserted row behind")

                if (lowered.startswith("select h_data") and
                        "2026-07-01 10:00:03" in lowered):
                    if "failed-before-conflict" in response:
                        raise AssertionError(
                            "failed transaction kept writes before "
                            "the conflict")

                if (lowered.startswith("select count(*)") and
                        "2026-07-01 10:00:04" in lowered):
                    rows = table_rows(response)
                    actual = (int(float(rows[-1][0]))
                              if rows and rows[-1] else -1)
                    if actual != 0:
                        raise AssertionError(
                            "failed transaction executed statements "
                            f"after abort: expected 0 rows, got {actual}")

                time.sleep(args.delay)
    finally:
        client.close()


def run_concurrent_consistency_probe(args):
    """Concurrent NewOrder execution + rollback validation probe."""
    if args.skip_concurrent_probe:
        return

    verifier = WireSqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        district_keys = [(1, 1), (1, 2)]
        initial_next = {
            key: select_scalar_int(
                verifier,
                f"SELECT d_next_o_id FROM district "
                f"WHERE d_w_id = {key[0]} AND d_id = {key[1]};",
            )
            for key in district_keys
        }
        stock_items = [(1, 1), (1, 2)]
        initial_stock = {
            key: select_scalar_int(
                verifier,
                f"SELECT s_quantity FROM stock "
                f"WHERE s_w_id = {key[0]} AND s_i_id = {key[1]};",
            )
            for key in stock_items
        }
    finally:
        verifier.close()

    workloads = [
        {"order_id": 2101, "district_id": 1,
         "quantities": [(1, 1), (2, 2)]},
        {"order_id": 2102, "district_id": 1,
         "quantities": [(1, 2), (2, 1)]},
        {"order_id": 2103, "district_id": 1,
         "quantities": [(1, 3), (2, 1)]},
        {"order_id": 2201, "district_id": 2,
         "quantities": [(1, 1), (2, 1)]},
        {"order_id": 2202, "district_id": 2,
         "quantities": [(1, 2), (2, 2)]},
        {"order_id": 2203, "district_id": 2,
         "quantities": [(1, 1), (2, 3)]},
    ]
    committed = []
    aborted = []
    with ThreadPoolExecutor(max_workers=6) as executor:
        future_to_workload = {
            executor.submit(
                execute_explicit_txn,
                new_order_statements(
                    w["order_id"], w["district_id"], w["quantities"]),
                args.host,
                args.port,
                args.timeout,
                SI_PREFIX,
            ): w
            for w in workloads
        }
        for future in as_completed(future_to_workload):
            workload = future_to_workload[future]
            ok, responses = future.result()
            if ok:
                committed.append(workload)
            else:
                aborted.append((workload, responses))

    # Retry aborted transactions sequentially
    next_order_id = 2301
    retry_attempts = 0
    max_retries = len(workloads) * 10
    while aborted and retry_attempts < max_retries:
        workload, _responses = aborted.pop(0)
        retry = {**workload, "order_id": next_order_id}
        next_order_id += 1
        retry_attempts += 1
        ok, responses = execute_explicit_txn(
            new_order_statements(
                retry["order_id"], retry["district_id"],
                retry["quantities"]),
            args.host,
            args.port,
            args.timeout,
            SI_PREFIX,
        )
        if ok:
            committed.append(retry)
        else:
            aborted.append((retry, responses))

    if aborted:
        rendered = []
        for workload, responses in aborted:
            rendered.append(f"workload={workload}")
            rendered.extend(
                f"  {sql} => {resp!r}" for sql, resp in responses)
        raise AssertionError(
            "snapshot-isolation new-order probe could not commit "
            "after retries\n" + "\n".join(rendered))

    failure_probes = [
        (3101, 1, "district"),
        (3102, 1, "orders"),
        (3103, 2, "stock"),
        (3104, 2, "order_line"),
    ]
    for order_id, district_id, stage in failure_probes:
        ok, responses = execute_explicit_txn(
            failure_probe_statements(order_id, district_id, stage),
            args.host,
            args.port,
            args.timeout,
        )
        if ok:
            rendered = "\n".join(
                f"{sql} => {resp!r}" for sql, resp in responses)
            raise AssertionError(
                f"failure probe unexpectedly committed at {stage}\n"
                f"{rendered}")

    # Verify committed orders and stock levels
    verifier = WireSqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        committed_by_district = {key: 0 for key in district_keys}
        stock_delta = {key: 0 for key in stock_items}
        for workload in committed:
            committed_by_district[
                (1, workload["district_id"])] += 1
            for item_id, qty in workload["quantities"]:
                stock_delta[(1, item_id)] += qty

        for key, initial in initial_next.items():
            actual = select_scalar_int(
                verifier,
                f"SELECT d_next_o_id FROM district "
                f"WHERE d_w_id = {key[0]} AND d_id = {key[1]};",
            )
            expected = initial + committed_by_district[key]
            if actual != expected:
                raise AssertionError(
                    f"district {key} next_o_id mismatch: "
                    f"expected {expected}, got {actual}")

        for key, initial in initial_stock.items():
            actual = select_scalar_int(
                verifier,
                f"SELECT s_quantity FROM stock "
                f"WHERE s_w_id = {key[0]} AND s_i_id = {key[1]};",
            )
            expected = initial - stock_delta[key]
            if actual != expected:
                raise AssertionError(
                    f"stock {key} mismatch: "
                    f"expected {expected}, got {actual}")

        for workload in committed:
            order_id = workload["order_id"]
            district_id = workload["district_id"]
            full_key_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM orders "
                f"WHERE o_w_id = 1 AND o_d_id = {district_id} "
                f"AND o_id = {order_id};",
            )
            scan_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM orders WHERE o_id = {order_id};",
            )
            if full_key_count != 1 or scan_count != 1:
                raise AssertionError(
                    f"committed order {order_id} is not visible "
                    f"through both index and scan predicates")
            new_order_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM new_orders "
                f"WHERE no_w_id = 1 AND no_d_id = {district_id} "
                f"AND no_o_id = {order_id};",
            )
            line_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM order_line "
                f"WHERE ol_w_id = 1 AND ol_d_id = {district_id} "
                f"AND ol_o_id = {order_id};",
            )
            if (new_order_count != 1 or
                    line_count != len(workload["quantities"])):
                raise AssertionError(
                    f"committed order {order_id} has incomplete "
                    f"child rows")

        # Verify failed orders left no partial rows
        failed_order_ids = (
            [w["order_id"] for w, _ in aborted] +
            [oid for oid, _, _ in failure_probes])
        for order_id in failed_order_ids:
            for table, col in [
                ("orders", "o_id"),
                ("new_orders", "no_o_id"),
                ("order_line", "ol_o_id"),
            ]:
                count = select_scalar_int(
                    verifier,
                    f"SELECT count(*) FROM {table} "
                    f"WHERE {col} = {order_id};")
                if count != 0:
                    raise AssertionError(
                        f"aborted order {order_id} left rows in {table}")
    finally:
        verifier.close()

    print(f"concurrent consistency probe passed "
          f"({len(committed)} committed, {len(aborted)} aborted)")


def run_illegal_item_rollback_probe(args):
    """Verify illegal-item NewOrder rollback leaves no state behind."""
    if args.skip_concurrent_probe:
        return

    verifier = WireSqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        district_id = 1
        order_id = 4101
        invalid_item_id = 99999
        initial_next = select_scalar_int(
            verifier,
            f"SELECT d_next_o_id FROM district "
            f"WHERE d_w_id = 1 AND d_id = {district_id};",
        )
    finally:
        verifier.close()

    ok, responses = execute_explicit_txn(
        illegal_item_rollback_statements(
            order_id, district_id, invalid_item_id),
        args.host,
        args.port,
        args.timeout,
        SI_PREFIX,
    )
    rollback_seen = any(
        stmt.strip().upper().startswith("ROLLBACK")
        for stmt, _resp in responses
    )
    if not rollback_seen:
        rendered = "\n".join(
            f"{sql} => {resp!r}" for sql, resp in responses)
        raise AssertionError(
            "illegal-item new-order probe did not reach ROLLBACK\n" +
            rendered)

    verifier = WireSqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        actual_next = select_scalar_int(
            verifier,
            f"SELECT d_next_o_id FROM district "
            f"WHERE d_w_id = 1 AND d_id = {district_id};",
        )
        if actual_next != initial_next:
            raise AssertionError(
                f"illegal-item rollback changed district counter: "
                f"expected {initial_next}, got {actual_next}")

        for table, col in [
            ("orders", "o_id"),
            ("new_orders", "no_o_id"),
            ("order_line", "ol_o_id"),
        ]:
            count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM {table} "
                f"WHERE {col} = {order_id};")
            if count != 0:
                raise AssertionError(
                    f"illegal-item rollback left rows in {table} "
                    f"for order {order_id}")
    finally:
        verifier.close()

    print("illegal-item rollback probe passed")


# ── CLI ─────────────────────────────────────────────────────────────────────

def parse_args():
    parser = argparse.ArgumentParser(
        description="Run performance smoke suite for RMDB (Wire v3)."
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--delay", type=float, default=0.02)
    add_server_arguments(
        parser, "performance_smoke_db", "performance_smoke_server.log",
        REPO_ROOT / "build")
    parser.add_argument("--skip-concurrent-probe", action="store_true")
    parser.add_argument("sql_files", nargs="*", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    server = None

    try:
        if args.start_server:
            server = RMDBServerManager.from_args(
                args, log_name="performance_smoke_server.log")
            server.start()
            args.db_dir = server.db_dir
        run_files(args)
        run_concurrent_consistency_probe(args)
        run_illegal_item_rollback_probe(args)
        print("performance smoke suite passed")
        return 0
    finally:
        if server is not None:
            server.stop()


if __name__ == "__main__":
    raise SystemExit(main())
