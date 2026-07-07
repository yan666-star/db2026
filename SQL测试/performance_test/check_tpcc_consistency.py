#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import SqlClient, response_failed, table_rows  # noqa: E402


def run_select(client, statement):
    response = client.execute(statement)
    if response_failed(response):
        raise AssertionError(f"consistency query failed:\n{statement}\n{response}")
    return table_rows(response), response


def probe_tables(client):
    response = client.execute("SHOW TABLES;")
    if response_failed(response):
        return "SHOW TABLES failed:\n" + response
    return response


def to_int(value):
    return int(float(value))


def key2(row):
    return (to_int(row[0]), to_int(row[1]))


def load_maps(client):
    district_rows, _ = run_select(
        client,
        "SELECT d_w_id, d_id, d_next_o_id FROM district;",
    )
    order_rows, _ = run_select(
        client,
        "SELECT o_w_id, o_d_id, MAX(o_id), COUNT(o_id), SUM(o_ol_cnt) "
        "FROM orders GROUP BY o_w_id, o_d_id;",
    )
    new_order_rows, _ = run_select(
        client,
        "SELECT no_w_id, no_d_id, MIN(no_o_id), MAX(no_o_id), COUNT(no_o_id) "
        "FROM new_orders GROUP BY no_w_id, no_d_id;",
    )
    order_line_rows, _ = run_select(
        client,
        "SELECT ol_w_id, ol_d_id, COUNT(ol_o_id) "
        "FROM order_line GROUP BY ol_w_id, ol_d_id;",
    )
    total_rows, _ = run_select(client, "SELECT COUNT(*) FROM orders;")

    districts = {key2(row): to_int(row[2]) for row in district_rows}
    orders = {
        key2(row): {
            "max_o_id": to_int(row[2]),
            "count": to_int(row[3]),
            "sum_ol_cnt": to_int(row[4]),
        }
        for row in order_rows
    }
    new_orders = {
        key2(row): {
            "min_no_o_id": to_int(row[2]),
            "max_no_o_id": to_int(row[3]),
            "count": to_int(row[4]),
        }
        for row in new_order_rows
    }
    order_lines = {key2(row): to_int(row[2]) for row in order_line_rows}
    total_orders = to_int(total_rows[-1][0]) if total_rows else 0
    return districts, orders, new_orders, order_lines, total_orders


def check_consistency(args):
    client = SqlClient(args.host, args.port, args.timeout)
    failures = []
    try:
        client.connect()
        try:
            districts, orders, new_orders, order_lines, total_orders = load_maps(client)
        except AssertionError as exc:
            print(exc)
            print("\nTable probe:")
            print(probe_tables(client))
            return 2
    finally:
        client.close()

    summed_order_count = 0
    for district_key, next_o_id in districts.items():
        order_info = orders.get(district_key)
        if order_info is None:
            failures.append(f"{district_key}: missing grouped orders row")
            continue

        summed_order_count += order_info["count"]
        expected_next = order_info["max_o_id"] + 1
        if next_o_id != expected_next:
            failures.append(
                f"{district_key}: d_next_o_id={next_o_id}, expected {expected_next} "
                f"from max(o_id)={order_info['max_o_id']}"
            )

        line_count = order_lines.get(district_key, 0)
        if args.check_order_line_counts and order_info["sum_ol_cnt"] != line_count:
            failures.append(
                f"{district_key}: sum(o_ol_cnt)={order_info['sum_ol_cnt']}, "
                f"count(order_line)={line_count}"
            )

        new_info = new_orders.get(district_key)
        if new_info is not None:
            span = new_info["max_no_o_id"] - new_info["min_no_o_id"] + 1
            if new_info["count"] != span:
                failures.append(
                    f"{district_key}: new_orders range has holes: min={new_info['min_no_o_id']}, "
                    f"max={new_info['max_no_o_id']}, count={new_info['count']}, span={span}"
                )
            if new_info["max_no_o_id"] > order_info["max_o_id"]:
                failures.append(
                    f"{district_key}: max(no_o_id)={new_info['max_no_o_id']} "
                    f"> max(o_id)={order_info['max_o_id']}"
                )

    if summed_order_count != total_orders:
        failures.append(
            f"total orders mismatch: grouped sum={summed_order_count}, count(*)={total_orders}"
        )

    if failures:
        print("TPCC consistency check failed:")
        for item in failures[: args.max_failures]:
            print(f"- {item}")
        if len(failures) > args.max_failures:
            print(f"... {len(failures) - args.max_failures} more failure(s)")
        return 1

    print(
        "TPCC consistency check passed: "
        f"{len(districts)} districts, {total_orders} orders"
    )
    return 0


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run local TPC-C post-transaction consistency checks against a running RMDB server."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--max-failures", type=int, default=20)
    parser.add_argument(
        "--check-order-line-counts",
        action="store_true",
        help=(
            "Also check sum(orders.o_ol_cnt) == count(order_line). "
            "The bundled miniature CSV data does not satisfy this at load time, "
            "so this is off by default."
        ),
    )
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(check_consistency(parse_args()))
