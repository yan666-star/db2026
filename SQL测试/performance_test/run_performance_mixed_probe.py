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
    response_failed,
    select_scalar_int,
    start_server,
    stop_server,
    table_rows,
)


def execute_sql_file(path, host, port, timeout):
    client = SqlClient(host, port, timeout)
    try:
        client.connect()
        for statement in read_statements(path):
            response = client.execute(statement)
            if response_failed(response):
                raise AssertionError(f"setup statement failed: {statement}\n{response}")
    finally:
        client.close()


def setup_tpcc(args):
    for name in [
        "00_create_tpcc_tables.sql",
        "01_load_tpcc_tables.sql",
        "02_create_primary_indexes.sql",
    ]:
        execute_sql_file(SCRIPT_DIR / name, args.host, args.port, args.timeout)


def query_rows(client, statement):
    response = client.execute(statement)
    if response_failed(response):
        raise AssertionError(f"query failed: {statement}\n{response}")
    return table_rows(response)


def scalar_float(client, statement):
    rows = query_rows(client, statement)
    if not rows:
        raise AssertionError(f"query returned no rows: {statement}")
    return float(rows[-1][0])


def run_txn(statements, host, port, timeout, snapshot):
    client = SqlClient(host, port, timeout)
    responses = []
    failed = False
    try:
        client.connect()
        if snapshot:
            response = client.execute(
                "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;"
            )
            responses.append(("SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;", response))
            if response_failed(response):
                return False, responses
        for statement in statements:
            response = client.execute(statement)
            responses.append((statement, response))
            if response_failed(response):
                failed = True
                break
        if failed:
            try:
                responses.append(("ROLLBACK;", client.execute("ROLLBACK;")))
            except Exception as exc:
                responses.append(("ROLLBACK;", f"rollback error: {exc}"))
    finally:
        client.close()
    return not failed, responses


def new_order_txn(order_id, district_id, customer_id, items):
    statements = [
        "BEGIN;",
        f"SELECT c_discount, c_last, c_credit FROM customer "
        f"WHERE c_w_id = 1 AND c_d_id = {district_id} AND c_id = {customer_id};",
        "SELECT w_tax FROM warehouse WHERE w_id = 1;",
        f"SELECT d_next_o_id, d_tax FROM district "
        f"WHERE d_w_id = 1 AND d_id = {district_id};",
    ]
    for item_id, _qty in items:
        statements.append(f"SELECT i_price FROM item WHERE i_id = {item_id};")
    statements.extend(
        [
            f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
            f"WHERE d_w_id = 1 AND d_id = {district_id};",
            f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, {customer_id}, "
            f"'2026-07-07 10:00:00', 0, {len(items)}, 1);",
            f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
        ]
    )
    for line_no, (item_id, qty) in enumerate(items, start=1):
        statements.extend(
            [
                f"SELECT s_quantity, s_ytd, s_order_cnt, s_remote_cnt FROM stock "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
                f"UPDATE stock SET s_quantity = s_quantity - {qty} "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
                f"UPDATE stock SET s_ytd = s_ytd + {float(qty):.1f} "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
                f"UPDATE stock SET s_order_cnt = s_order_cnt + 1 "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
                f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, "
                f"{line_no}, {item_id}, 1, '2026-07-07 10:00:00', {qty}, "
                f"{float(qty) * 10.0:.1f}, 'mixed-new-order-{line_no}');",
            ]
        )
    statements.append("COMMIT;")
    return statements


def aborting_new_order_txn(order_id, district_id):
    return [
        "BEGIN;",
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
        f"WHERE d_w_id = 1 AND d_id = {district_id};",
        "UPDATE stock SET s_quantity = s_quantity - 9 WHERE s_w_id = 1 AND s_i_id = 1;",
        f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 1, "
        "'2026-07-07 10:03:00', 0, 1, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
        f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, 1, 1, 1, "
        "'2026-07-07 10:03:00', 9, 90.0, 'mixed-abort-line');",
        f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, 1, 1, 1, "
        "'2026-07-07 10:03:00', 9, 90.0, 'mixed-abort-dup');",
        "COMMIT;",
    ]


def payment_txn(history_id, district_id, customer_id, amount):
    return [
        "BEGIN;",
        f"UPDATE warehouse SET w_ytd = w_ytd + {amount:.1f} WHERE w_id = 1;",
        f"UPDATE district SET d_ytd = d_ytd + {amount:.1f} "
        f"WHERE d_w_id = 1 AND d_id = {district_id};",
        f"UPDATE customer SET c_balance = c_balance - {amount:.1f} "
        f"WHERE c_w_id = 1 AND c_d_id = {district_id} AND c_id = {customer_id};",
        f"UPDATE customer SET c_ytd_payment = c_ytd_payment + {amount:.1f} "
        f"WHERE c_w_id = 1 AND c_d_id = {district_id} AND c_id = {customer_id};",
        f"UPDATE customer SET c_payment_cnt = c_payment_cnt + 1 "
        f"WHERE c_w_id = 1 AND c_d_id = {district_id} AND c_id = {customer_id};",
        f"INSERT INTO history VALUES ({customer_id}, {district_id}, 1, {district_id}, 1, "
        f"'2026-07-07 10:05:{history_id % 60:02d}', {amount:.1f}, 'mixed-payment-{history_id}');",
        "COMMIT;",
    ]


def delivery_txn(district_id, order_id, carrier_id):
    return [
        "BEGIN;",
        f"SELECT MIN(no_o_id) FROM new_orders WHERE no_w_id = 1 AND no_d_id = {district_id};",
        f"DELETE FROM new_orders WHERE no_w_id = 1 AND no_d_id = {district_id} "
        f"AND no_o_id = {order_id};",
        f"UPDATE orders SET o_carrier_id = {carrier_id} "
        f"WHERE o_w_id = 1 AND o_d_id = {district_id} AND o_id = {order_id};",
        f"UPDATE order_line SET ol_delivery_d = '2026-07-07 10:10:00' "
        f"WHERE ol_w_id = 1 AND ol_d_id = {district_id} AND ol_o_id = {order_id};",
        f"UPDATE customer SET c_delivery_cnt = c_delivery_cnt + 1 "
        f"WHERE c_w_id = 1 AND c_d_id = {district_id} AND c_id = 2;",
        "COMMIT;",
    ]


def verify_absent(client, table, column, value):
    count = select_scalar_int(client, f"SELECT COUNT(*) FROM {table} WHERE {column} = {value};")
    if count != 0:
        raise AssertionError(f"{table}.{column}={value} should be absent, got {count}")


def verify_committed_order(client, order_id, district_id, line_count):
    orders = select_scalar_int(
        client,
        f"SELECT COUNT(*) FROM orders WHERE o_w_id = 1 AND o_d_id = {district_id} "
        f"AND o_id = {order_id};",
    )
    new_orders = select_scalar_int(
        client,
        f"SELECT COUNT(*) FROM new_orders WHERE no_w_id = 1 AND no_d_id = {district_id} "
        f"AND no_o_id = {order_id};",
    )
    lines = select_scalar_int(
        client,
        f"SELECT COUNT(*) FROM order_line WHERE ol_w_id = 1 AND ol_d_id = {district_id} "
        f"AND ol_o_id = {order_id};",
    )
    if orders != 1 or new_orders != 1 or lines != line_count:
        raise AssertionError(
            f"order {order_id} incomplete: orders={orders}, "
            f"new_orders={new_orders}, order_line={lines}"
        )


def read_baseline(args):
    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    try:
        baseline = {
            "d_next": {
                d: select_scalar_int(
                    client,
                    f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {d};",
                )
                for d in (1, 2, 3)
            },
            "stock_qty": {
                i: select_scalar_int(
                    client,
                    f"SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = {i};",
                )
                for i in (1, 2, 3, 4)
            },
            "stock_ytd": {
                i: scalar_float(
                    client,
                    f"SELECT s_ytd FROM stock WHERE s_w_id = 1 AND s_i_id = {i};",
                )
                for i in (1, 2, 3, 4)
            },
            "stock_order_cnt": {
                i: select_scalar_int(
                    client,
                    f"SELECT s_order_cnt FROM stock WHERE s_w_id = 1 AND s_i_id = {i};",
                )
                for i in (1, 2, 3, 4)
            },
            "w_ytd": scalar_float(client, "SELECT w_ytd FROM warehouse WHERE w_id = 1;"),
            "d_ytd": {
                d: scalar_float(
                    client,
                    f"SELECT d_ytd FROM district WHERE d_w_id = 1 AND d_id = {d};",
                )
                for d in (1, 2, 3)
            },
            "customer": {},
            "new_order_min": {},
        }
        for d in (1, 2, 3):
            rows = query_rows(
                client,
                f"SELECT MIN(no_o_id) FROM new_orders WHERE no_w_id = 1 AND no_d_id = {d};",
            )
            baseline["new_order_min"][d] = int(float(rows[-1][0])) if rows else 11
        for d in (1, 2, 3):
            baseline["customer"][d] = {
                "balance": scalar_float(
                    client,
                    f"SELECT c_balance FROM customer WHERE c_w_id = 1 AND c_d_id = {d} AND c_id = 2;",
                ),
                "ytd_payment": scalar_float(
                    client,
                    f"SELECT c_ytd_payment FROM customer WHERE c_w_id = 1 AND c_d_id = {d} AND c_id = 2;",
                ),
                "payment_cnt": select_scalar_int(
                    client,
                    f"SELECT c_payment_cnt FROM customer WHERE c_w_id = 1 AND c_d_id = {d} AND c_id = 2;",
                ),
                "delivery_cnt": select_scalar_int(
                    client,
                    f"SELECT c_delivery_cnt FROM customer WHERE c_w_id = 1 AND c_d_id = {d} AND c_id = 2;",
                ),
            }
        return baseline
    finally:
        client.close()


def render_failure(title, failures):
    rendered = [title]
    for label, responses in failures:
        rendered.append(f"transaction={label}")
        rendered.extend(f"  {sql} => {resp!r}" for sql, resp in responses)
    return "\n".join(rendered)


def run_probe(args):
    baseline = read_baseline(args)

    new_orders = [
        {
            "label": "new-order-d1-a",
            "order_id": args.order_base,
            "district_id": 1,
            "customer_id": 2,
            "items": [(1, 2), (2, 1), (3, 2)],
        },
        {
            "label": "new-order-d2-a",
            "order_id": args.order_base + 100,
            "district_id": 2,
            "customer_id": 2,
            "items": [(1, 1), (2, 2), (4, 1)],
        },
        {
            "label": "new-order-d3-a",
            "order_id": args.order_base + 200,
            "district_id": 3,
            "customer_id": 2,
            "items": [(2, 3), (3, 1)],
        },
    ]
    payments = [
        {"label": "payment-d1", "history_id": 1, "district_id": 1, "customer_id": 2, "amount": 7.0},
        {"label": "payment-d2", "history_id": 2, "district_id": 2, "customer_id": 2, "amount": 11.0},
        {"label": "payment-d3", "history_id": 3, "district_id": 3, "customer_id": 2, "amount": 13.0},
    ]
    deliveries = [
        {
            "label": "delivery-d1",
            "district_id": 1,
            "order_id": baseline["new_order_min"][1],
            "carrier_id": 5,
        },
        {
            "label": "delivery-d2",
            "district_id": 2,
            "order_id": baseline["new_order_min"][2],
            "carrier_id": 6,
        },
        {
            "label": "delivery-d3",
            "district_id": 3,
            "order_id": baseline["new_order_min"][3],
            "carrier_id": 7,
        },
    ]

    tasks = []
    for workload in new_orders:
        tasks.append((
            workload["label"],
            new_order_txn(
                workload["order_id"],
                workload["district_id"],
                workload["customer_id"],
                workload["items"],
            ),
        ))
    for workload in payments:
        tasks.append((
            workload["label"],
            payment_txn(
                workload["history_id"],
                workload["district_id"],
                workload["customer_id"],
                workload["amount"],
            ),
        ))
    for workload in deliveries:
        tasks.append((
            workload["label"],
            delivery_txn(
                workload["district_id"],
                workload["order_id"],
                workload["carrier_id"],
            ),
        ))

    failures = []
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {
            executor.submit(
                run_txn,
                statements,
                args.host,
                args.port,
                args.timeout,
                args.snapshot,
            ): label
            for label, statements in tasks
        }
        for future in as_completed(futures):
            label = futures[future]
            ok, responses = future.result()
            if not ok:
                failures.append((label, responses))

    if failures and not args.allow_snapshot_aborts:
        raise AssertionError(render_failure("mixed transaction unexpectedly aborted", failures))

    committed_labels = {label for label, _ in tasks}
    committed_labels -= {label for label, _responses in failures}

    abort_order_id = args.order_base + 9000
    ok, responses = run_txn(
        aborting_new_order_txn(abort_order_id, 1),
        args.host,
        args.port,
        args.timeout,
        args.snapshot,
    )
    if ok:
        raise AssertionError(
            "intentional abort transaction committed\n" +
            "\n".join(f"{sql} => {resp!r}" for sql, resp in responses)
        )

    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    try:
        expected_d_next = dict(baseline["d_next"])
        expected_stock_qty = dict(baseline["stock_qty"])
        expected_stock_ytd = dict(baseline["stock_ytd"])
        expected_stock_order_cnt = dict(baseline["stock_order_cnt"])
        expected_w_ytd = baseline["w_ytd"]
        expected_d_ytd = dict(baseline["d_ytd"])
        expected_customer = {
            d: dict(values) for d, values in baseline["customer"].items()
        }
        for workload in new_orders:
            if workload["label"] not in committed_labels:
                continue
            expected_d_next[workload["district_id"]] += 1
            for item_id, qty in workload["items"]:
                expected_stock_qty[item_id] -= qty
                expected_stock_ytd[item_id] += float(qty)
                expected_stock_order_cnt[item_id] += 1
            verify_committed_order(
                client,
                workload["order_id"],
                workload["district_id"],
                len(workload["items"]),
            )
        for workload in payments:
            if workload["label"] not in committed_labels:
                continue
            district_id = workload["district_id"]
            amount = workload["amount"]
            expected_w_ytd += amount
            expected_d_ytd[district_id] += amount
            expected_customer[district_id]["balance"] -= amount
            expected_customer[district_id]["ytd_payment"] += amount
            expected_customer[district_id]["payment_cnt"] += 1
        for workload in deliveries:
            if workload["label"] not in committed_labels:
                continue
            district_id = workload["district_id"]
            expected_customer[district_id]["delivery_cnt"] += 1

        for district_id, expected in expected_d_next.items():
            actual = select_scalar_int(
                client,
                f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district_id};",
            )
            if actual != expected:
                raise AssertionError(
                    f"district {district_id} d_next_o_id mismatch: expected {expected}, got {actual}"
                )
        for item_id, expected in expected_stock_qty.items():
            actual = select_scalar_int(
                client,
                f"SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
            if actual != expected:
                raise AssertionError(
                    f"stock {item_id} s_quantity mismatch: expected {expected}, got {actual}"
                )
            actual_ytd = scalar_float(
                client,
                f"SELECT s_ytd FROM stock WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
            if abs(actual_ytd - expected_stock_ytd[item_id]) > 0.01:
                raise AssertionError(
                    f"stock {item_id} s_ytd mismatch: expected {expected_stock_ytd[item_id]}, got {actual_ytd}"
                )
            actual_cnt = select_scalar_int(
                client,
                f"SELECT s_order_cnt FROM stock WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
            if actual_cnt != expected_stock_order_cnt[item_id]:
                raise AssertionError(
                    f"stock {item_id} s_order_cnt mismatch: expected {expected_stock_order_cnt[item_id]}, got {actual_cnt}"
                )

        actual_w_ytd = scalar_float(client, "SELECT w_ytd FROM warehouse WHERE w_id = 1;")
        if abs(actual_w_ytd - expected_w_ytd) > 0.01:
            raise AssertionError(f"warehouse w_ytd mismatch: expected {expected_w_ytd}, got {actual_w_ytd}")

        for district_id, expected in expected_d_ytd.items():
            actual = scalar_float(
                client,
                f"SELECT d_ytd FROM district WHERE d_w_id = 1 AND d_id = {district_id};",
            )
            if abs(actual - expected) > 0.01:
                raise AssertionError(
                    f"district {district_id} d_ytd mismatch: expected {expected}, got {actual}"
                )
            for col, expected_value in expected_customer[district_id].items():
                actual_stmt = (
                    f"SELECT c_{col} FROM customer "
                    f"WHERE c_w_id = 1 AND c_d_id = {district_id} AND c_id = 2;"
                )
                if col in ("payment_cnt", "delivery_cnt"):
                    actual = select_scalar_int(client, actual_stmt)
                    if actual != expected_value:
                        raise AssertionError(
                            f"customer d={district_id} {col} mismatch: expected {expected_value}, got {actual}"
                        )
                else:
                    actual = scalar_float(client, actual_stmt)
                    if abs(actual - expected_value) > 0.01:
                        raise AssertionError(
                            f"customer d={district_id} {col} mismatch: expected {expected_value}, got {actual}"
                        )

        verify_absent(client, "orders", "o_id", abort_order_id)
        verify_absent(client, "new_orders", "no_o_id", abort_order_id)
        verify_absent(client, "order_line", "ol_o_id", abort_order_id)
    finally:
        client.close()

    print(
        "performance mixed probe passed: "
        f"{len(committed_labels)} committed mixed transactions, "
        f"{len(failures)} expected snapshot abort(s)"
    )


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run a mixed TPC-C-shaped consistency probe."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--startup-timeout", type=float, default=10.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="performance_mixed_probe_db")
    parser.add_argument("--db-dir", type=Path)
    parser.add_argument(
        "--server-log",
        type=Path,
        default=REPO_ROOT / "build" / "performance_mixed_probe_server.log",
    )
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    parser.add_argument("--skip-setup", action="store_true")
    parser.add_argument("--snapshot", action="store_true")
    parser.add_argument("--allow-snapshot-aborts", action="store_true")
    parser.add_argument("--workers", type=int, default=9)
    parser.add_argument("--order-base", type=int, default=800000)
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
            setup_tpcc(args)
        run_probe(args)
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
