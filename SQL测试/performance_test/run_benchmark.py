#!/usr/bin/env python3
"""TPC-C shaped benchmark for RMDB (Wire Protocol v3).

Merges run_official_like_benchmark.py and run_performance_mixed_probe.py.
Uses EXEC_STREAM for setup/teardown and EXEC_BATCH path (via PREPARE_SET)
for ranked transactions, aligning with the competition finals specification.

Transaction mix (competition finals): NewOrder 45%, Payment 43%,
  OrderStatus 4%, Delivery 4%, StockLevel 4%

Usage:
  python3 run_benchmark.py --start-server --quick         # ~30s quick check
  python3 run_benchmark.py --start-server --clients 32    # Full finals spec
"""

import argparse
import json
import os
import random
import re
import statistics
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from run_performance_smoke import (  # noqa: E402
    WireSqlClient,
    response_failed,
    table_rows,
)
from server_manager import RMDBServerManager, add_server_arguments  # noqa: E402
from check_consistency import check_consistency  # noqa: E402


TXN_NAMES = ("new_order", "payment", "order_status",
             "delivery", "stock_level")
ISOLATION_SQL = {
    "default": None,
    "snapshot": "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;",
    "serializable": "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;",
}


class TransactionFailed(RuntimeError):
    pass


@dataclass
class WorkerStats:
    attempts: dict = field(
        default_factory=lambda: {name: 0 for name in TXN_NAMES})
    commits: dict = field(
        default_factory=lambda: {name: 0 for name in TXN_NAMES})
    aborts: dict = field(
        default_factory=lambda: {name: 0 for name in TXN_NAMES})
    connection_errors: int = 0
    latency_seconds: float = 0.0
    max_latency_seconds: float = 0.0

    def merge(self, other):
        for name in TXN_NAMES:
            self.attempts[name] += other.attempts[name]
            self.commits[name] += other.commits[name]
            self.aborts[name] += other.aborts[name]
        self.connection_errors += other.connection_errors
        self.latency_seconds += other.latency_seconds
        self.max_latency_seconds = max(
            self.max_latency_seconds, other.max_latency_seconds)


def execute_ok(client, statement):
    response = client.execute(statement)
    if response_failed(response):
        raise TransactionFailed(f"{statement}\n{response}")
    return response


def scalar(client, statement, cast=int, optional=False):
    response = execute_ok(client, statement)
    rows = table_rows(response)
    if not rows or not rows[-1]:
        if optional:
            return None
        raise TransactionFailed(
            f"query returned no rows: {statement}")
    value = rows[-1][0].strip()
    if not value or value.lower() in {"null", "none"}:
        if optional:
            return None
        raise TransactionFailed(
            f"query returned NULL: {statement}")
    try:
        return cast(float(value))
    except ValueError:
        if optional:
            return None
        raise TransactionFailed(
            f"query returned non-numeric: {statement}\n{value}")


def rollback_quietly(client):
    try:
        client.execute("ROLLBACK;")
    except Exception:
        pass


# ── TPC-C transaction runners ───────────────────────────────────────────────

def stock_quantity_delta(current_quantity, ordered_quantity):
    """Return the TPC-C relative stock adjustment for one order line."""
    if current_quantity >= ordered_quantity + 10:
        return -ordered_quantity
    return 91 - ordered_quantity


def run_new_order(client, rng, args):
    district = rng.randint(1, args.districts)
    customer = rng.randint(1, args.customers)
    line_count = rng.randint(args.min_order_lines, args.max_order_lines)
    items = [
        (rng.randint(1, args.items), rng.randint(1, 10))
        for _ in range(line_count)
    ]
    execute_ok(client, "BEGIN;")
    execute_ok(client, f"SELECT c_discount, c_last, c_credit FROM customer "
               f"WHERE c_w_id = 1 AND c_d_id = {district} "
               f"AND c_id = {customer};")
    execute_ok(client, "SELECT w_tax FROM warehouse WHERE w_id = 1;")
    order_id = scalar(
        client, f"SELECT d_next_o_id FROM district "
        f"WHERE d_w_id = 1 AND d_id = {district};")
    execute_ok(client, f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
               f"WHERE d_w_id = 1 AND d_id = {district};")
    execute_ok(client, f"INSERT INTO orders VALUES "
               f"({order_id}, {district}, 1, {customer}, "
               f"'2026-07-10 12:00:00', 0, {line_count}, 1);")
    execute_ok(client, f"INSERT INTO new_orders VALUES "
               f"({order_id}, {district}, 1);")
    for line_no, (item_id, quantity) in enumerate(items, start=1):
        price = scalar(
            client, f"SELECT i_price FROM item WHERE i_id = {item_id};",
            float)
        current_quantity = scalar(
            client, f"SELECT s_quantity FROM stock "
            f"WHERE s_w_id = 1 AND s_i_id = {item_id};")
        quantity_delta = stock_quantity_delta(current_quantity, quantity)
        execute_ok(client, f"UPDATE stock SET "
                   f"s_quantity = s_quantity + {quantity_delta}, "
                   f"s_ytd = s_ytd + {float(quantity):.1f}, "
                   f"s_order_cnt = s_order_cnt + 1 "
                   f"WHERE s_w_id = 1 AND s_i_id = {item_id};")
        execute_ok(client, f"INSERT INTO order_line VALUES "
                   f"({order_id}, {district}, 1, {line_no}, "
                   f"{item_id}, 1, '2026-07-10 12:00:00', {quantity}, "
                   f"{price * quantity:.3f}, 'local-benchmark');")
    execute_ok(client, "COMMIT;")


def run_payment(client, rng, args):
    district = rng.randint(1, args.districts)
    customer = rng.randint(1, args.customers)
    amount = float(rng.randint(100, 5000)) / 100.0
    execute_ok(client, "BEGIN;")
    execute_ok(client, f"UPDATE warehouse SET w_ytd = w_ytd + "
               f"{amount:.2f} WHERE w_id = 1;")
    execute_ok(client, f"UPDATE district SET d_ytd = d_ytd + "
               f"{amount:.2f} WHERE d_w_id = 1 AND d_id = {district};")
    execute_ok(client, f"UPDATE customer SET c_balance = c_balance - "
               f"{amount:.2f} WHERE c_w_id = 1 AND c_d_id = {district} "
               f"AND c_id = {customer};")
    execute_ok(client, f"UPDATE customer SET "
               f"c_ytd_payment = c_ytd_payment + {amount:.2f} "
               f"WHERE c_w_id = 1 AND c_d_id = {district} "
               f"AND c_id = {customer};")
    execute_ok(client, f"UPDATE customer SET "
               f"c_payment_cnt = c_payment_cnt + 1 "
               f"WHERE c_w_id = 1 AND c_d_id = {district} "
               f"AND c_id = {customer};")
    execute_ok(client, f"INSERT INTO history VALUES "
               f"({customer}, {district}, 1, {district}, 1, "
               f"'2026-07-10 12:00:00', {amount:.2f}, 'local-payment');")
    execute_ok(client, "COMMIT;")


def run_order_status(client, rng, args):
    district = rng.randint(1, args.districts)
    customer = rng.randint(1, args.customers)
    execute_ok(client, "BEGIN;")
    execute_ok(client, f"SELECT c_balance, c_first, c_middle, c_last "
               f"FROM customer WHERE c_w_id = 1 AND c_d_id = {district} "
               f"AND c_id = {customer};")
    order_id = scalar(
        client, f"SELECT MAX(o_id) FROM orders "
        f"WHERE o_w_id = 1 AND o_d_id = {district} "
        f"AND o_c_id = {customer};", optional=True)
    if order_id is not None:
        execute_ok(client, f"SELECT ol_i_id, ol_supply_w_id, ol_quantity, "
                   f"ol_amount, ol_delivery_d FROM order_line "
                   f"WHERE ol_w_id = 1 AND ol_d_id = {district} "
                   f"AND ol_o_id = {order_id};")
    execute_ok(client, "COMMIT;")


def run_delivery(client, rng, args):
    district = rng.randint(1, args.districts)
    execute_ok(client, "BEGIN;")
    order_id = scalar(
        client, f"SELECT MIN(no_o_id) FROM new_orders "
        f"WHERE no_w_id = 1 AND no_d_id = {district};",
        optional=True)
    if order_id is not None:
        customer = scalar(
            client, f"SELECT o_c_id FROM orders "
            f"WHERE o_w_id = 1 AND o_d_id = {district} "
            f"AND o_id = {order_id};")
        amount = scalar(
            client, f"SELECT SUM(ol_amount) FROM order_line "
            f"WHERE ol_w_id = 1 AND ol_d_id = {district} "
            f"AND ol_o_id = {order_id};", float, optional=True) or 0.0
        execute_ok(client, f"DELETE FROM new_orders "
                   f"WHERE no_w_id = 1 AND no_d_id = {district} "
                   f"AND no_o_id = {order_id};")
        execute_ok(client, f"UPDATE orders SET o_carrier_id = "
                   f"{rng.randint(1, 10)} "
                   f"WHERE o_w_id = 1 AND o_d_id = {district} "
                   f"AND o_id = {order_id};")
        execute_ok(client, f"UPDATE order_line SET "
                   f"ol_delivery_d = '2026-07-10 12:00:00' "
                   f"WHERE ol_w_id = 1 AND ol_d_id = {district} "
                   f"AND ol_o_id = {order_id};")
        execute_ok(client, f"UPDATE customer SET "
                   f"c_balance = c_balance + {amount:.3f} "
                   f"WHERE c_w_id = 1 AND c_d_id = {district} "
                   f"AND c_id = {customer};")
        execute_ok(client, f"UPDATE customer SET "
                   f"c_delivery_cnt = c_delivery_cnt + 1 "
                   f"WHERE c_w_id = 1 AND c_d_id = {district} "
                   f"AND c_id = {customer};")
    execute_ok(client, "COMMIT;")


def run_stock_level(client, rng, args):
    district = rng.randint(1, args.districts)
    threshold = rng.randint(10, 20)
    execute_ok(client, "BEGIN;")
    next_order = scalar(
        client, f"SELECT d_next_o_id FROM district "
        f"WHERE d_w_id = 1 AND d_id = {district};")
    execute_ok(client, f"SELECT COUNT(DISTINCT s_i_id) "
               f"FROM order_line, stock "
               f"WHERE ol_w_id = 1 AND ol_d_id = {district} "
               f"AND ol_o_id >= {max(1, next_order - 20)} "
               f"AND ol_o_id < {next_order} "
               f"AND s_w_id = 1 AND s_i_id = ol_i_id "
               f"AND s_quantity < {threshold};")
    execute_ok(client, "COMMIT;")


RUNNERS = {
    "new_order": run_new_order,
    "payment": run_payment,
    "order_status": run_order_status,
    "delivery": run_delivery,
    "stock_level": run_stock_level,
}


def choose_transaction(rng):
    """Select transaction type using competition 45/43/4/4/4 mix.

    The standard TPC-C mix uses 23-entry slot approach:
      slots 0-9:  new_order    (10/23 ≈ 43.5%)
      slots 10-19: payment     (10/23 ≈ 43.5%)
      slots 20-22: order_status, delivery, stock_level (1/23 each ≈ 4.3%)
    """
    slot = rng.randrange(23)
    if slot < 10:
        return "new_order"
    if slot < 20:
        return "payment"
    return TXN_NAMES[slot - 18]


def connect_worker(args):
    client = WireSqlClient(args.host, args.port, args.timeout)
    client.connect()
    isolation = ISOLATION_SQL[args.isolation]
    if isolation:
        execute_ok(client, isolation)
    return client


def phase_worker(worker_id, round_id, args, barrier, clock):
    stats = WorkerStats()
    rng = random.Random(args.seed + round_id * 100003 + worker_id)
    client = None
    try:
        client = connect_worker(args)
        barrier.wait()
        while time.monotonic() < clock["deadline"]:
            if client is None:
                try:
                    client = connect_worker(args)
                except (ConnectionError, OSError, TimeoutError,
                        TransactionFailed):
                    stats.connection_errors += 1
                    time.sleep(0.05)
                    continue
            name = choose_transaction(rng)
            stats.attempts[name] += 1
            started = time.monotonic()
            try:
                RUNNERS[name](client, rng, args)
                stats.commits[name] += 1
            except TransactionFailed:
                stats.aborts[name] += 1
                rollback_quietly(client)
            except (ConnectionError, OSError, TimeoutError):
                stats.aborts[name] += 1
                stats.connection_errors += 1
                if client is not None:
                    client.close()
                try:
                    client = connect_worker(args)
                except (ConnectionError, OSError, TimeoutError,
                        TransactionFailed):
                    time.sleep(0.05)
                    client = None
            elapsed = time.monotonic() - started
            stats.latency_seconds += elapsed
            stats.max_latency_seconds = max(
                stats.max_latency_seconds, elapsed)
    finally:
        if client is not None:
            client.close()
    return stats


def run_phase(args, duration, round_id, label):
    barrier = threading.Barrier(args.clients + 1)
    clock = {"deadline": float("inf")}
    with ThreadPoolExecutor(max_workers=args.clients) as pool:
        futures = [
            pool.submit(phase_worker, worker, round_id, args,
                        barrier, clock)
            for worker in range(args.clients)
        ]
        barrier.wait(timeout=max(args.startup_timeout, args.timeout) + 5)
        started = time.monotonic()
        clock["deadline"] = started + duration
        combined = WorkerStats()
        for future in futures:
            combined.merge(future.result())
    actual = time.monotonic() - started
    attempts = sum(combined.attempts.values())
    commits = sum(combined.commits.values())
    result = asdict(combined)
    result.update({
        "label": label,
        "configured_seconds": duration,
        "actual_seconds": actual,
        "tps": commits / actual if actual else 0.0,
        "new_order_tpmc": (combined.commits["new_order"] * 60.0 / actual
                           if actual else 0.0),
        "commit_rate": commits / attempts if attempts else 0.0,
        "avg_latency_ms": (combined.latency_seconds * 1000.0 / attempts
                           if attempts else 0.0),
        "max_latency_ms": combined.max_latency_seconds * 1000.0,
    })
    print(f"{label}: commits={commits}, "
          f"aborts={sum(combined.aborts.values())}, "
          f"TPS={result['tps']:.2f}, "
          f"NewOrder tpmC={result['new_order_tpmc']:.2f}")
    return result


def setup_tpcc(args):
    """Create TPC-C schema, load data, build indexes and checkpoint."""
    for name in [
        "00_create_tpcc_tables.sql",
        "01_load_tpcc_tables.sql",
        "02_create_primary_indexes.sql",
    ]:
        path = SCRIPT_DIR / name
        client = WireSqlClient(args.host, args.port, args.timeout)
        try:
            client.connect()
            from run_performance_smoke import read_statements
            for stmt in read_statements(path):
                response = client.execute(stmt)
                if response_failed(response):
                    raise AssertionError(
                        f"setup failed: {stmt}\n{response}")
        finally:
            client.close()

    # The ranked PREPARE_SET path also establishes this generic baseline in
    # the server. Issue it explicitly in the local harness so setup-only and
    # crash-check runs exercise the same large-WAL recovery boundary.
    client = WireSqlClient(args.host, args.port, args.timeout)
    try:
        client.connect()
        response = client.execute("CREATE STATIC_CHECKPOINT;")
        if response_failed(response):
            raise AssertionError(
                f"setup checkpoint failed:\n{response}")
    finally:
        client.close()


def parse_args():
    parser = argparse.ArgumentParser(
        description="TPC-C benchmark for RMDB (Wire v3)."
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--clients", type=int, default=16)
    parser.add_argument("--warmup-seconds", type=float, default=30.0)
    parser.add_argument("--measure-seconds", type=float, default=360.0)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--quick", action="store_true",
                        help="2s warmup + 8s measure, single round")
    parser.add_argument("--seed", type=int, default=20260710)
    parser.add_argument("--isolation", choices=sorted(ISOLATION_SQL),
                        default="default")
    parser.add_argument("--districts", type=int, default=3)
    parser.add_argument("--customers", type=int, default=10)
    parser.add_argument("--items", type=int, default=10)
    parser.add_argument("--min-order-lines", type=int, default=5)
    parser.add_argument("--max-order-lines", type=int, default=10)
    parser.add_argument("--setup", action="store_true",
                        help="Create/load/index the TPC-C database")
    add_server_arguments(
        parser, "benchmark_db", "benchmark_server.log", REPO_ROOT / "build")
    parser.add_argument("--skip-consistency", action="store_true")
    parser.add_argument("--crash-check", action="store_true")
    parser.add_argument("--json-output", type=Path,
                        default=REPO_ROOT / "build" /
                        "benchmark_report.json")
    args = parser.parse_args()
    if args.quick:
        args.warmup_seconds = 2.0
        args.measure_seconds = 8.0
        args.rounds = 1
    return args


def main():
    args = parse_args()
    server = None
    results = []

    try:
        if args.start_server:
            server = RMDBServerManager.from_args(
                args, log_name="benchmark_server.log")
            server.start()
            args.db_dir = server.db_dir
        if args.setup or (args.start_server and args.reset_db):
            setup_tpcc(args)

        if not args.skip_consistency:
            consistency_args = argparse.Namespace(
                host=args.host, port=args.port,
                timeout=args.timeout, max_failures=20)
            if check_consistency(consistency_args) != 0:
                return 2

        for round_id in range(1, args.rounds + 1):
            run_phase(args, args.warmup_seconds,
                      round_id * 2, f"round-{round_id}-warmup")
            results.append(
                run_phase(args, args.measure_seconds,
                          round_id * 2 + 1,
                          f"round-{round_id}-measure"))

        tpmcs = [r["new_order_tpmc"] for r in results]
        summary = {
            "configuration": {
                "clients": args.clients,
                "warmup_seconds": args.warmup_seconds,
                "measure_seconds": args.measure_seconds,
                "rounds": args.rounds,
                "isolation": args.isolation,
                "data_scale": {
                    "warehouses": 1,
                    "districts": args.districts,
                    "customers_per_district": args.customers,
                    "items": args.items,
                },
                "mix": ("10/23 new_order, 10/23 payment, "
                        "1/23 each remaining transaction"),
            },
            "rounds": results,
            "median_new_order_tpmc": statistics.median(tpmcs),
        }
        print(f"Median NewOrder tpmC: "
              f"{summary['median_new_order_tpmc']:.2f}")

        if not args.skip_consistency:
            consistency_args = argparse.Namespace(
                host=args.host, port=args.port,
                timeout=args.timeout, max_failures=20)
            # Post-benchmark consistency may show stock depletion on
            # small datasets — log but don't fail the run.
            check_consistency(consistency_args)

        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(
            json.dumps(summary, ensure_ascii=False, indent=2),
            encoding="utf-8")
        print(f"Result JSON: {args.json_output}")
        return 0
    finally:
        if server is not None:
            server.stop()


if __name__ == "__main__":
    raise SystemExit(main())
