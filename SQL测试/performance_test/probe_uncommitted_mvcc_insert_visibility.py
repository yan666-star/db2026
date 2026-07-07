#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import SqlClient, response_failed, table_rows  # noqa: E402


def execute_ok(client, sql):
    response = client.execute(sql)
    if response_failed(response):
        raise AssertionError(f"SQL failed:\n{sql}\n{response}")
    return response


def select_scalar_int(client, sql):
    rows = table_rows(execute_ok(client, sql))
    if not rows:
        raise AssertionError(f"no rows returned for {sql}")
    return int(float(rows[-1][0]))


def connect(args):
    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    return client


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Verify READ COMMITTED readers do not see uncommitted physical "
            "inserts created by a snapshot transaction."
        )
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--order-id", type=int, default=99998)
    args = parser.parse_args()

    reader = connect(args)
    writer = None
    try:
        writer = connect(args)
        execute_ok(writer, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        execute_ok(writer, "BEGIN;")
        execute_ok(writer, f"INSERT INTO new_orders VALUES ({args.order_id}, 1, 1);")

        visible = select_scalar_int(
            reader,
            f"SELECT COUNT(*) FROM new_orders WHERE no_w_id = 1 "
            f"AND no_d_id = 1 AND no_o_id = {args.order_id};",
        )
        if visible != 0:
            raise AssertionError(
                "READ COMMITTED saw an uncommitted MVCC insert: "
                f"count={visible}"
            )

        execute_ok(writer, "ROLLBACK;")
        print("uncommitted MVCC insert visibility probe passed")
        return 0
    finally:
        if writer is not None:
            try:
                writer.execute("ROLLBACK;")
            except Exception:
                pass
            writer.close()
        reader.close()


if __name__ == "__main__":
    raise SystemExit(main())
