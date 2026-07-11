#!/usr/bin/env python3
"""Run a configurable, official-shaped TPC-C load against RMDB.

This is a local engineering benchmark, not a reproduction of the private
evaluator.  It deliberately uses the normal SQL protocol and normal database
paths; it contains no server-side shortcuts or score-oriented special cases.
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

from check_tpcc_consistency import check_consistency  # noqa: E402
from run_performance_mixed_probe import setup_tpcc  # noqa: E402
from run_performance_smoke import (  # noqa: E402
    SqlClient,
    response_failed,
    start_server,
    stop_server,
    table_rows,
)


TXN_NAMES = ("new_order", "payment", "order_status", "delivery", "stock_level")
ISOLATION_SQL = {
    "default": None,
    "snapshot": "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;",
    "serializable": "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;",
}
PERF_DIAG_LINE = re.compile(r"(?:^|\s)([a-z_]+)=(\d+)(?=\s|$)")


class TransactionFailed(RuntimeError):
    pass


@dataclass
class WorkerStats:
    attempts: dict = field(default_factory=lambda: {name: 0 for name in TXN_NAMES})
    commits: dict = field(default_factory=lambda: {name: 0 for name in TXN_NAMES})
    aborts: dict = field(default_factory=lambda: {name: 0 for name in TXN_NAMES})
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
        self.max_latency_seconds = max(self.max_latency_seconds, other.max_latency_seconds)


def execute_ok(client, statement):
    response = client.execute(statement)
    lowered = response.lower()
    if response_failed(response) or "error" in lowered or "exception" in lowered:
        raise TransactionFailed(f"{statement}\n{response}")
    return response


def optional_number(response, cast=int):
    rows = table_rows(response)
    if not rows or not rows[-1]:
        return None
    value = rows[-1][0].strip()
    if not value or value.lower() in {"null", "none"}:
        return None
    try:
        return cast(float(value))
    except ValueError:
        return None


def select_number(client, statement, cast=int, optional=False):
    value = optional_number(execute_ok(client, statement), cast)
    if value is None and not optional:
        raise TransactionFailed(f"query returned no numeric value: {statement}")
    return value


def rollback_quietly(client):
    try:
        client.execute("ROLLBACK;")
    except Exception:
        pass


def choose_dimension(rng, upper_bound, hotspot_percent):
    if hotspot_percent and rng.random() * 100.0 < hotspot_percent:
        return 1
    return rng.randint(1, upper_bound)


def run_new_order(client, rng, args):
    district = choose_dimension(rng, args.districts, args.hot_district_percent)
    customer = rng.randint(1, args.customers)
    line_count = rng.randint(args.min_order_lines, args.max_order_lines)
    if args.workload_mode == "official-shape":
        if line_count > args.items:
            raise TransactionFailed(
                "official-shape requires --items >= generated order-line count"
            )
        item_ids = rng.sample(range(1, args.items + 1), line_count)
        items = [(item_id, rng.randint(1, 10)) for item_id in item_ids]
    else:
        items = [
            (choose_dimension(rng, args.items, args.hot_item_percent), rng.randint(1, 10))
            for _ in range(line_count)
        ]
    execute_ok(client, "BEGIN;")
    execute_ok(
        client,
        f"SELECT c_discount, c_last, c_credit FROM customer "
        f"WHERE c_w_id = 1 AND c_d_id = {district} AND c_id = {customer};",
    )
    execute_ok(client, "SELECT w_tax FROM warehouse WHERE w_id = 1;")
    order_id = select_number(
        client,
        f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district};",
    )
    execute_ok(
        client,
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
        f"WHERE d_w_id = 1 AND d_id = {district};",
    )
    execute_ok(
        client,
        f"INSERT INTO orders VALUES ({order_id}, {district}, 1, {customer}, "
        f"'2026-07-10 12:00:00', 0, {line_count}, 1);",
    )
    execute_ok(client, f"INSERT INTO new_orders VALUES ({order_id}, {district}, 1);")
    for line_no, (item_id, quantity) in enumerate(items, start=1):
        price = select_number(
            client, f"SELECT i_price FROM item WHERE i_id = {item_id};", float
        )
        execute_ok(
            client,
            f"SELECT s_quantity, s_ytd, s_order_cnt, s_remote_cnt FROM stock "
            f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
        )
        if args.workload_mode == "official-shape":
            execute_ok(
                client,
                f"UPDATE stock SET s_quantity = s_quantity - {quantity}, "
                f"s_ytd = s_ytd + {float(quantity):.1f}, "
                f"s_order_cnt = s_order_cnt + 1 "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
        else:
            # Preserve the original conflict-amplifying local workload as an
            # explicit worst-case mode for before/after comparisons.
            execute_ok(
                client,
                f"UPDATE stock SET s_quantity = s_quantity - {quantity} "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
            execute_ok(
                client,
                f"UPDATE stock SET s_ytd = s_ytd + {float(quantity):.1f} "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
            execute_ok(
                client,
                f"UPDATE stock SET s_order_cnt = s_order_cnt + 1 "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};",
            )
        execute_ok(
            client,
            f"INSERT INTO order_line VALUES ({order_id}, {district}, 1, {line_no}, "
            f"{item_id}, 1, '2026-07-10 12:00:00', {quantity}, "
            f"{price * quantity:.3f}, 'local-benchmark');",
        )
    execute_ok(client, "COMMIT;")


def run_payment(client, rng, args):
    district = choose_dimension(rng, args.districts, args.hot_district_percent)
    customer = rng.randint(1, args.customers)
    amount = float(rng.randint(100, 5000)) / 100.0
    execute_ok(client, "BEGIN;")
    execute_ok(client, f"UPDATE warehouse SET w_ytd = w_ytd + {amount:.2f} WHERE w_id = 1;")
    execute_ok(
        client,
        f"UPDATE district SET d_ytd = d_ytd + {amount:.2f} "
        f"WHERE d_w_id = 1 AND d_id = {district};",
    )
    execute_ok(
        client,
        f"UPDATE customer SET c_balance = c_balance - {amount:.2f} "
        f"WHERE c_w_id = 1 AND c_d_id = {district} AND c_id = {customer};",
    )
    execute_ok(
        client,
        f"UPDATE customer SET c_ytd_payment = c_ytd_payment + {amount:.2f} "
        f"WHERE c_w_id = 1 AND c_d_id = {district} AND c_id = {customer};",
    )
    execute_ok(
        client,
        f"UPDATE customer SET c_payment_cnt = c_payment_cnt + 1 "
        f"WHERE c_w_id = 1 AND c_d_id = {district} AND c_id = {customer};",
    )
    execute_ok(
        client,
        f"INSERT INTO history VALUES ({customer}, {district}, 1, {district}, 1, "
        f"'2026-07-10 12:00:00', {amount:.2f}, 'local-payment');",
    )
    execute_ok(client, "COMMIT;")


def run_order_status(client, rng, args):
    district = choose_dimension(rng, args.districts, args.hot_district_percent)
    customer = rng.randint(1, args.customers)
    execute_ok(client, "BEGIN;")
    execute_ok(
        client,
        f"SELECT c_balance, c_first, c_middle, c_last FROM customer "
        f"WHERE c_w_id = 1 AND c_d_id = {district} AND c_id = {customer};",
    )
    order_id = select_number(
        client,
        f"SELECT MAX(o_id) FROM orders WHERE o_w_id = 1 AND o_d_id = {district} "
        f"AND o_c_id = {customer};",
        optional=True,
    )
    if order_id is not None:
        execute_ok(
            client,
            f"SELECT ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d "
            f"FROM order_line WHERE ol_w_id = 1 AND ol_d_id = {district} "
            f"AND ol_o_id = {order_id};",
        )
    execute_ok(client, "COMMIT;")


def run_delivery(client, rng, args):
    district = choose_dimension(rng, args.districts, args.hot_district_percent)
    execute_ok(client, "BEGIN;")
    order_id = select_number(
        client,
        f"SELECT MIN(no_o_id) FROM new_orders WHERE no_w_id = 1 AND no_d_id = {district};",
        optional=True,
    )
    if order_id is not None:
        customer = select_number(
            client,
            f"SELECT o_c_id FROM orders WHERE o_w_id = 1 AND o_d_id = {district} "
            f"AND o_id = {order_id};",
        )
        amount = select_number(
            client,
            f"SELECT SUM(ol_amount) FROM order_line WHERE ol_w_id = 1 "
            f"AND ol_d_id = {district} AND ol_o_id = {order_id};",
            float,
            optional=True,
        ) or 0.0
        execute_ok(
            client,
            f"DELETE FROM new_orders WHERE no_w_id = 1 AND no_d_id = {district} "
            f"AND no_o_id = {order_id};",
        )
        execute_ok(
            client,
            f"UPDATE orders SET o_carrier_id = {rng.randint(1, 10)} WHERE o_w_id = 1 "
            f"AND o_d_id = {district} AND o_id = {order_id};",
        )
        execute_ok(
            client,
            f"UPDATE order_line SET ol_delivery_d = '2026-07-10 12:00:00' "
            f"WHERE ol_w_id = 1 AND ol_d_id = {district} AND ol_o_id = {order_id};",
        )
        execute_ok(
            client,
            f"UPDATE customer SET c_balance = c_balance + {amount:.3f} WHERE c_w_id = 1 "
            f"AND c_d_id = {district} AND c_id = {customer};",
        )
        execute_ok(
            client,
            f"UPDATE customer SET c_delivery_cnt = c_delivery_cnt + 1 WHERE c_w_id = 1 "
            f"AND c_d_id = {district} AND c_id = {customer};",
        )
    execute_ok(client, "COMMIT;")


def run_stock_level(client, rng, args):
    district = choose_dimension(rng, args.districts, args.hot_district_percent)
    threshold = rng.randint(10, 20)
    execute_ok(client, "BEGIN;")
    next_order = select_number(
        client,
        f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district};",
    )
    execute_ok(
        client,
        f"SELECT COUNT(*) FROM order_line, stock WHERE ol_w_id = 1 "
        f"AND ol_d_id = {district} AND ol_o_id >= {max(1, next_order - 20)} "
        f"AND ol_o_id < {next_order} AND s_w_id = 1 AND s_i_id = ol_i_id "
        f"AND s_quantity < {threshold};",
    )
    execute_ok(client, "COMMIT;")


RUNNERS = {
    "new_order": run_new_order,
    "payment": run_payment,
    "order_status": run_order_status,
    "delivery": run_delivery,
    "stock_level": run_stock_level,
}


def choose_transaction(rng):
    slot = rng.randrange(23)
    if slot < 10:
        return "new_order"
    if slot < 20:
        return "payment"
    return TXN_NAMES[slot - 18]


def connect_worker(args):
    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    isolation = ISOLATION_SQL[args.isolation]
    if isolation:
        execute_ok(client, isolation)
    execute_ok(client, "set output_file off")
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
                except (ConnectionError, OSError, TimeoutError, TransactionFailed):
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
                except (ConnectionError, OSError, TimeoutError, TransactionFailed):
                    time.sleep(0.05)
                    client = None
            elapsed = time.monotonic() - started
            stats.latency_seconds += elapsed
            stats.max_latency_seconds = max(stats.max_latency_seconds, elapsed)
    finally:
        if client is not None:
            client.close()
    return stats


def run_phase(args, duration, round_id, label):
    diag_before = read_latest_perf_diag(args.server_log) if args.perf_diag else {}
    barrier = threading.Barrier(args.clients + 1)
    clock = {"deadline": float("inf")}
    with ThreadPoolExecutor(max_workers=args.clients) as pool:
        futures = [
            pool.submit(phase_worker, worker, round_id, args, barrier, clock)
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
    result.update(
        {
            "label": label,
            "configured_seconds": duration,
            "actual_seconds": actual,
            "transactions_per_second": commits / actual if actual else 0.0,
            "new_order_tpmc": combined.commits["new_order"] * 60.0 / actual if actual else 0.0,
            "commit_rate": commits / attempts if attempts else 0.0,
            "average_latency_ms": combined.latency_seconds * 1000.0 / attempts if attempts else 0.0,
            "max_latency_ms": combined.max_latency_seconds * 1000.0,
        }
    )
    if args.perf_diag:
        diag_after = read_latest_perf_diag(args.server_log)
        result["perf_diag_observed_before"] = diag_before
        result["perf_diag_observed_after"] = diag_after
        result["perf_diag_observed_delta"] = counter_delta(diag_before, diag_after)
    print(
        f"{label}: commits={commits}, aborts={sum(combined.aborts.values())}, "
        f"TPS={result['transactions_per_second']:.2f}, "
        f"NewOrder tpmC={result['new_order_tpmc']:.2f}"
    )
    return result


def read_latest_perf_diag(log_path):
    path = Path(log_path)
    if not path.exists():
        return {}
    latest = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "RMDB_PERF_DIAG" not in line:
            continue
        values = {name: int(value) for name, value in PERF_DIAG_LINE.findall(line)}
        if values:
            latest = values
    return latest


def counter_delta(before, after):
    return {
        key: after.get(key, 0) - before.get(key, 0)
        for key in sorted(set(before) | set(after))
    }


def effective_isolation(args):
    if args.isolation == "default":
        return {
            "requested": "default",
            "effective": "read_committed",
            "source": "server connection default; set output_file off only disables output",
        }
    return {
        "requested": args.isolation,
        "effective": args.isolation,
        "source": "explicit SET TRANSACTION ISOLATION LEVEL command",
    }


def consistency_args(args):
    return argparse.Namespace(
        host=args.host,
        port=args.port,
        timeout=args.timeout,
        max_failures=20,
        # The bundled miniature CSV intentionally has pre-existing o_ol_cnt
        # discrepancies.  The benchmark checks that the per-district baseline
        # discrepancy does not change instead of requiring an invalid zero gap.
        check_order_line_counts=False,
    )


def read_order_line_gaps(args):
    client = SqlClient(args.host, args.port, args.timeout)
    try:
        client.connect()
        order_rows = table_rows(
            execute_ok(
                client,
                "SELECT o_w_id, o_d_id, SUM(o_ol_cnt) FROM orders "
                "GROUP BY o_w_id, o_d_id;",
            )
        )
        line_rows = table_rows(
            execute_ok(
                client,
                "SELECT ol_w_id, ol_d_id, COUNT(ol_o_id) FROM order_line "
                "GROUP BY ol_w_id, ol_d_id;",
            )
        )
    finally:
        client.close()

    order_counts = {
        (int(float(row[0])), int(float(row[1]))): int(float(row[2]))
        for row in order_rows
    }
    line_counts = {
        (int(float(row[0])), int(float(row[1]))): int(float(row[2]))
        for row in line_rows
    }
    keys = set(order_counts) | set(line_counts)
    return {
        key: order_counts.get(key, 0) - line_counts.get(key, 0)
        for key in keys
    }


def check_order_line_gaps(args, expected, label):
    actual = read_order_line_gaps(args)
    if actual != expected:
        raise AssertionError(
            f"order-line count gaps changed {label}: "
            f"expected={expected}, actual={actual}"
        )
    print(f"Order-line baseline-gap check {label}: PASS")


def read_durability_markers(args):
    statements = [
        "SELECT COUNT(*) FROM orders;",
        "SELECT COUNT(*) FROM new_orders;",
        "SELECT COUNT(*) FROM order_line;",
        "SELECT COUNT(*) FROM history;",
        "SELECT SUM(d_next_o_id) FROM district;",
        "SELECT SUM(w_ytd) FROM warehouse;",
    ]
    client = SqlClient(args.host, args.port, args.timeout)
    try:
        client.connect()
        return {statement: table_rows(execute_ok(client, statement)) for statement in statements}
    finally:
        client.close()


def crash_and_restart(args, process, log_handle):
    before = read_durability_markers(args)
    process.kill()
    process.wait(timeout=5)
    log_handle.close()
    reset_db = args.reset_db
    args.reset_db = False
    try:
        process, log_handle, _db_dir = start_server(args)
    finally:
        args.reset_db = reset_db
    after = read_durability_markers(args)
    if before != after:
        raise AssertionError(
            "durability markers changed after kill -9 restart:\n"
            + json.dumps({"before": before, "after": after}, ensure_ascii=False, indent=2)
        )
    print("Crash-recovery durability markers: PASS")
    return process, log_handle


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Run a local TPC-C-shaped benchmark with the official 23-transaction mix. "
            "Defaults to 30s warmup + 360s measurement, three rounds."
        )
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--clients", type=int, default=16)
    parser.add_argument("--warmup-seconds", type=float, default=30.0)
    parser.add_argument("--measure-seconds", type=float, default=360.0)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--quick", action="store_true", help="Use 2s warmup + 8s measurement, one round")
    parser.add_argument("--seed", type=int, default=20260710)
    parser.add_argument(
        "--workload-mode",
        choices=("official-shape", "mini-contention"),
        default="official-shape",
        help=(
            "official-shape uses distinct items and one multi-column stock UPDATE; "
            "mini-contention preserves the original repeat-item/three-UPDATE stress shape"
        ),
    )
    parser.add_argument("--isolation", choices=sorted(ISOLATION_SQL), default="default")
    parser.add_argument("--districts", type=int, default=3)
    parser.add_argument("--customers", type=int, default=10)
    parser.add_argument("--items", type=int, default=10)
    parser.add_argument("--hot-district-percent", type=float, default=0.0)
    parser.add_argument("--hot-item-percent", type=float, default=0.0)
    parser.add_argument("--min-order-lines", type=int, default=5)
    parser.add_argument("--max-order-lines", type=int, default=10)
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--setup", action="store_true", help="Create/load/index the bundled mini TPC-C database")
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="official_like_benchmark_db")
    parser.add_argument("--db-dir", type=Path)
    parser.add_argument("--server-log", type=Path, default=REPO_ROOT / "build" / "official_like_benchmark.log")
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    parser.add_argument("--skip-consistency", action="store_true")
    parser.add_argument("--crash-check", action="store_true", help="Kill -9 and restart after measurement; requires --start-server")
    parser.add_argument(
        "--perf-diag",
        action="store_true",
        help="Start the server with RMDB_PERF_DIAG and record observed per-phase counter deltas",
    )
    parser.add_argument("--perf-diag-interval", type=int, default=1000)
    parser.add_argument(
        "--pending-wait-us",
        type=int,
        help="Set RMDB_PENDING_WAIT_US for the started server and record it in JSON",
    )
    parser.add_argument("--json-output", type=Path, default=REPO_ROOT / "build" / "official_like_benchmark.json")
    args = parser.parse_args()
    if args.quick:
        args.warmup_seconds = 2.0
        args.measure_seconds = 8.0
        args.rounds = 1
    if args.clients < 1 or args.rounds < 1:
        parser.error("--clients and --rounds must be positive")
    if args.min_order_lines < 1 or args.max_order_lines < args.min_order_lines:
        parser.error("invalid order-line range")
    if args.workload_mode == "official-shape" and args.max_order_lines > args.items:
        parser.error("official-shape requires --max-order-lines <= --items")
    if args.perf_diag_interval < 1:
        parser.error("--perf-diag-interval must be positive")
    if args.pending_wait_us is not None and args.pending_wait_us < 0:
        parser.error("--pending-wait-us cannot be negative")
    if args.pending_wait_us is not None and not args.start_server:
        parser.error("--pending-wait-us requires --start-server")
    if not 0.0 <= args.hot_district_percent <= 100.0:
        parser.error("--hot-district-percent must be between 0 and 100")
    if not 0.0 <= args.hot_item_percent <= 100.0:
        parser.error("--hot-item-percent must be between 0 and 100")
    if args.crash_check and not args.start_server:
        parser.error("--crash-check requires --start-server")
    if args.perf_diag and not args.start_server:
        parser.error("--perf-diag requires --start-server")
    return args


def main():
    args = parse_args()
    process = None
    log_handle = None
    results = []
    baseline_line_gaps = None
    try:
        if args.perf_diag:
            os.environ["RMDB_PERF_DIAG"] = "1"
            os.environ["RMDB_PERF_DIAG_INTERVAL"] = str(args.perf_diag_interval)
        if args.pending_wait_us is not None:
            os.environ["RMDB_PENDING_WAIT_US"] = str(args.pending_wait_us)
        if args.start_server:
            process, log_handle, args.db_dir = start_server(args)
        if args.setup or (args.start_server and args.reset_db):
            setup_tpcc(args)

        control = SqlClient(args.host, args.port, args.timeout)
        try:
            control.connect()
            execute_ok(control, "set output_file off")
        finally:
            control.close()

        if not args.skip_consistency:
            if check_consistency(consistency_args(args)) != 0:
                return 2
            baseline_line_gaps = read_order_line_gaps(args)
            print(f"Initial order-line count gaps: {baseline_line_gaps}")

        for round_id in range(1, args.rounds + 1):
            run_phase(args, args.warmup_seconds, round_id * 2, f"round-{round_id}-warmup")
            results.append(
                run_phase(args, args.measure_seconds, round_id * 2 + 1, f"round-{round_id}-measure")
            )

        tpmcs = [result["new_order_tpmc"] for result in results]
        summary = {
            "configuration": {
                "clients": args.clients,
                "warmup_seconds": args.warmup_seconds,
                "measure_seconds": args.measure_seconds,
                "rounds": args.rounds,
                "isolation": effective_isolation(args),
                "workload_mode": args.workload_mode,
                "data_scale": {
                    "warehouses": 1,
                    "districts": args.districts,
                    "customers_per_district": args.customers,
                    "items": args.items,
                },
                "sql_shape": {
                    "order_lines": [args.min_order_lines, args.max_order_lines],
                    "distinct_items_per_order": args.workload_mode == "official-shape",
                    "stock_update_statements_per_item": 1 if args.workload_mode == "official-shape" else 3,
                },
                "hot_district_percent": args.hot_district_percent,
                "hot_item_percent": args.hot_item_percent,
                "perf_diag": args.perf_diag,
                "perf_diag_interval": args.perf_diag_interval if args.perf_diag else None,
                "pending_wait_us": args.pending_wait_us if args.pending_wait_us is not None else 2000,
                "mix": "10/23 new_order, 10/23 payment, 1/23 each remaining transaction",
            },
            "rounds": results,
            "median_new_order_tpmc": statistics.median(tpmcs),
        }
        print(f"Median NewOrder tpmC: {summary['median_new_order_tpmc']:.2f}")

        if not args.skip_consistency:
            if check_consistency(consistency_args(args)) != 0:
                return 3
            check_order_line_gaps(args, baseline_line_gaps, "after benchmark")
        if args.crash_check:
            process, log_handle = crash_and_restart(args, process, log_handle)
            if not args.skip_consistency:
                if check_consistency(consistency_args(args)) != 0:
                    return 4
                check_order_line_gaps(args, baseline_line_gaps, "after crash recovery")

        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"Result JSON: {args.json_output}")
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
