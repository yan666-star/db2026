#!/usr/bin/env python3
import argparse
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import (  # noqa: E402
    SqlClient,
    execute_explicit_txn,
    find_rmdb,
    read_statements,
    remove_db_dir,
    select_scalar_int,
    start_server,
    stop_server,
)


def execute_sql_file(path, host, port, timeout):
    client = SqlClient(host, port, timeout)
    try:
        client.connect()
        for statement in read_statements(path):
            response = client.execute(statement)
            lowered = response.lower()
            if "failure" in lowered or "abort" in lowered:
                raise AssertionError(f"setup statement failed: {statement}\n{response}")
    finally:
        client.close()


def setup_tpcc_smoke(args):
    for name in [
        "00_create_tpcc_tables.sql",
        "01_load_tpcc_tables.sql",
        "02_create_primary_indexes.sql",
    ]:
        execute_sql_file(SCRIPT_DIR / name, args.host, args.port, args.timeout)


def new_order_txn(order_id, qty1, qty2):
    return [
        "BEGIN;",
        "SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = 1;",
        "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 1;",
        "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 2;",
        "UPDATE district SET d_next_o_id = d_next_o_id + 1 WHERE d_w_id = 1 AND d_id = 1;",
        f"INSERT INTO orders VALUES ({order_id}, 1, 1, 2, '2026-07-03 10:00:00', 0, 2, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, 1, 1);",
        f"UPDATE stock SET s_quantity = s_quantity - {qty1} WHERE s_w_id = 1 AND s_i_id = 1;",
        (
            f"INSERT INTO order_line VALUES ({order_id}, 1, 1, 1, 1, 1, "
            f"'2026-07-03 10:00:00', {qty1}, {float(qty1) * 10.0}, 'probe-line-1');"
        ),
        f"UPDATE stock SET s_quantity = s_quantity - {qty2} WHERE s_w_id = 1 AND s_i_id = 2;",
        (
            f"INSERT INTO order_line VALUES ({order_id}, 1, 1, 2, 2, 1, "
            f"'2026-07-03 10:00:00', {qty2}, {float(qty2) * 10.0}, 'probe-line-2');"
        ),
        "COMMIT;",
    ]


def aborted_order_txn(order_id):
    return [
        "BEGIN;",
        "UPDATE district SET d_next_o_id = d_next_o_id + 1 WHERE d_w_id = 1 AND d_id = 1;",
        "UPDATE stock SET s_quantity = s_quantity - 7 WHERE s_w_id = 1 AND s_i_id = 1;",
        f"INSERT INTO orders VALUES ({order_id}, 1, 1, 2, '2026-07-03 10:05:00', 0, 1, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, 1, 1);",
        (
            f"INSERT INTO order_line VALUES ({order_id}, 1, 1, 1, 1, 1, "
            "'2026-07-03 10:05:00', 7, 70.0, 'abort-line-1');"
        ),
        (
            f"INSERT INTO order_line VALUES ({order_id}, 1, 1, 1, 1, 1, "
            "'2026-07-03 10:05:00', 7, 70.0, 'abort-line-dup');"
        ),
        "COMMIT;",
    ]


def verify_absent(client, table, column, order_id):
    count = select_scalar_int(
        client, f"SELECT count(*) FROM {table} WHERE {column} = {order_id};"
    )
    if count != 0:
        raise AssertionError(f"aborted order {order_id} left {count} rows in {table}")


def run_probe(args):
    verifier = SqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        initial_next = select_scalar_int(
            verifier,
            "SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = 1;",
        )
        initial_stock_1 = select_scalar_int(
            verifier,
            "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 1;",
        )
        initial_stock_2 = select_scalar_int(
            verifier,
            "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 2;",
        )
    finally:
        verifier.close()

    workloads = []
    for i in range(args.transactions):
        workloads.append(
            {
                "order_id": args.order_base + i,
                "qty1": (i % 3) + 1,
                "qty2": (i % 4) + 1,
            }
        )

    committed = []
    aborted = []
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {
            executor.submit(
                execute_explicit_txn,
                new_order_txn(w["order_id"], w["qty1"], w["qty2"]),
                args.host,
                args.port,
                args.timeout,
            ): w
            for w in workloads
        }
        for future in as_completed(futures):
            workload = futures[future]
            ok, responses = future.result()
            if ok:
                committed.append(workload)
            else:
                aborted.append((workload, responses))

    if aborted:
        rendered = []
        for workload, responses in aborted:
            rendered.append(f"unexpected abort: {workload}")
            rendered.extend(f"  {sql} => {resp!r}" for sql, resp in responses)
        raise AssertionError("\n".join(rendered))

    expected_next = initial_next + len(committed)
    expected_stock_1 = initial_stock_1 - sum(w["qty1"] for w in committed)
    expected_stock_2 = initial_stock_2 - sum(w["qty2"] for w in committed)

    verifier = SqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        actual_next = select_scalar_int(
            verifier,
            "SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = 1;",
        )
        actual_stock_1 = select_scalar_int(
            verifier,
            "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 1;",
        )
        actual_stock_2 = select_scalar_int(
            verifier,
            "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 2;",
        )

        if actual_next != expected_next:
            raise AssertionError(
                f"district d_next_o_id mismatch: expected {expected_next}, got {actual_next}"
            )
        if actual_stock_1 != expected_stock_1:
            raise AssertionError(
                f"stock item 1 mismatch: expected {expected_stock_1}, got {actual_stock_1}"
            )
        if actual_stock_2 != expected_stock_2:
            raise AssertionError(
                f"stock item 2 mismatch: expected {expected_stock_2}, got {actual_stock_2}"
            )

        for workload in committed:
            order_id = workload["order_id"]
            orders = select_scalar_int(
                verifier, f"SELECT count(*) FROM orders WHERE o_id = {order_id};"
            )
            new_orders = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM new_orders WHERE no_o_id = {order_id};",
            )
            lines = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM order_line WHERE ol_o_id = {order_id};",
            )
            if orders != 1 or new_orders != 1 or lines != 2:
                raise AssertionError(
                    f"committed order {order_id} incomplete: "
                    f"orders={orders}, new_orders={new_orders}, order_line={lines}"
                )

        before_abort_next = actual_next
        before_abort_stock_1 = actual_stock_1
    finally:
        verifier.close()

    abort_order_id = args.order_base + args.transactions + 1000
    ok, responses = execute_explicit_txn(
        aborted_order_txn(abort_order_id), args.host, args.port, args.timeout
    )
    if ok:
        rendered = "\n".join(f"{sql} => {resp!r}" for sql, resp in responses)
        raise AssertionError(f"abort probe unexpectedly committed\n{rendered}")

    verifier = SqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        after_abort_next = select_scalar_int(
            verifier,
            "SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = 1;",
        )
        after_abort_stock_1 = select_scalar_int(
            verifier,
            "SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = 1;",
        )
        if after_abort_next != before_abort_next:
            raise AssertionError(
                f"aborted transaction changed district: before {before_abort_next}, after {after_abort_next}"
            )
        if after_abort_stock_1 != before_abort_stock_1:
            raise AssertionError(
                f"aborted transaction changed stock: before {before_abort_stock_1}, after {after_abort_stock_1}"
            )
        verify_absent(verifier, "orders", "o_id", abort_order_id)
        verify_absent(verifier, "new_orders", "no_o_id", abort_order_id)
        verify_absent(verifier, "order_line", "ol_o_id", abort_order_id)
    finally:
        verifier.close()

    print(
        "READ COMMITTED lost-update probe passed: "
        f"{len(committed)} committed transactions, "
        f"d_next_o_id {initial_next}->{expected_next}, "
        f"stock1 {initial_stock_1}->{expected_stock_1}, "
        f"stock2 {initial_stock_2}->{expected_stock_2}"
    )


def parse_args():
    parser = argparse.ArgumentParser(
        description="Probe READ COMMITTED lost updates on TPC-C-shaped district/stock rows."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--startup-timeout", type=float, default=10.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="read_committed_probe_db")
    parser.add_argument("--db-dir", type=Path)
    parser.add_argument(
        "--server-log",
        type=Path,
        default=REPO_ROOT / "build" / "read_committed_probe_server.log",
    )
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    parser.add_argument("--skip-setup", action="store_true")
    parser.add_argument("--transactions", type=int, default=32)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--order-base", type=int, default=500000)
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
            setup_tpcc_smoke(args)
        run_probe(args)
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
