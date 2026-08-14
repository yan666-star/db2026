#!/usr/bin/env python3
"""Final-round-shaped TPC-C workload driver using Wire v3 prepared batches.

The default invocation is the formal 50-warehouse configuration.  Smaller or
shorter runs must use --exploratory and are deliberately not labelled as a
formal median score.
"""

import argparse
import json
import math
import os
import random
import re
import statistics
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
PERFORMANCE_DIR = REPO_ROOT / "SQL测试" / "performance_test"
sys.path.insert(0, str(PERFORMANCE_DIR))

from server_manager import RMDBServerManager, add_server_arguments  # noqa: E402
from wire_client import (  # noqa: E402
    BATCH_ERROR,
    BATCH_OK,
    BATCH_TRANSACTION_ABORT,
    FLAG_AUTO_ABORT,
    RESULT_KIND_COMMAND,
    RESULT_KIND_QUERY,
    SQL_CHAR,
    SQL_FLOAT32,
    SQL_INT32,
    TAG_BATCH_RESULT,
    BatchOperationResult,
    TypedValue,
    WireBatchResult,
    WireClient,
    WireProtocolError,
    _WireReader,
    decode_cell,
    read_frame,
)


WAREHOUSES = 50
CLIENTS = 32
MIX = (45, 43, 4, 4, 4)
WARMUP_SECONDS = 30
MEASURE_SECONDS = 150
ROUNDS = 3
DISTRICTS_PER_WAREHOUSE = 10
CUSTOMERS_PER_DISTRICT = 3000
ITEMS = 100000
TXN_NAMES = ("new_order", "payment", "order_status", "delivery",
             "stock_level")


@dataclass(frozen=True)
class PreparedOp:
    name: str
    sql: str
    parameter_types: tuple[int, ...]

    @property
    def statement_id(self) -> int:
        return STATEMENT_IDS[self.name]

    @property
    def result_kind(self) -> int:
        return (RESULT_KIND_QUERY if self.sql.lstrip().upper().startswith(
            "SELECT") else RESULT_KIND_COMMAND)


def build_prepared_catalog() -> tuple[PreparedOp, ...]:
    """Return the immutable Wire v3 dictionary used by every worker."""
    i = SQL_INT32
    f = SQL_FLOAT32
    c = SQL_CHAR
    return (
        PreparedOp("begin", "BEGIN;", ()),
        PreparedOp("commit", "COMMIT;", ()),
        PreparedOp("rollback", "ROLLBACK;", ()),
        PreparedOp(
            "new_order_customer",
            "SELECT c_discount, c_last, c_credit FROM customer "
            "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?;", (i, i, i)),
        PreparedOp(
            "new_order_district",
            "SELECT d_next_o_id FROM district WHERE d_w_id = ? AND d_id = ?;",
            (i, i)),
        PreparedOp(
            "new_order_district_update",
            "UPDATE district SET d_next_o_id = d_next_o_id + 1 "
            "WHERE d_w_id = ? AND d_id = ?;", (i, i)),
        PreparedOp(
            "new_order_orders_insert",
            "INSERT INTO orders VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
            (i, i, i, i, c, i, i, i)),
        PreparedOp("new_order_queue_insert",
                   "INSERT INTO new_orders VALUES (?, ?, ?);", (i, i, i)),
        PreparedOp("new_order_item", "SELECT i_price FROM item WHERE i_id = ?;",
                   (i,)),
        PreparedOp(
            "new_order_stock",
            "SELECT s_quantity FROM stock WHERE s_w_id = ? AND s_i_id = ?;",
            (i, i)),
        PreparedOp(
            "new_order_stock_update",
            "UPDATE stock SET s_quantity = s_quantity - ?, "
            "s_ytd = s_ytd + ?, s_order_cnt = s_order_cnt + 1 "
            "WHERE s_w_id = ? AND s_i_id = ?;", (i, f, i, i)),
        PreparedOp(
            "new_order_line_insert",
            "INSERT INTO order_line VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            (i, i, i, i, i, i, c, i, f, c)),
        PreparedOp(
            "payment_warehouse_update",
            "UPDATE warehouse SET w_ytd = w_ytd + ? WHERE w_id = ?;", (f, i)),
        PreparedOp(
            "payment_district_update",
            "UPDATE district SET d_ytd = d_ytd + ? "
            "WHERE d_w_id = ? AND d_id = ?;", (f, i, i)),
        PreparedOp(
            "payment_customer",
            "SELECT c_balance, c_credit FROM customer "
            "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?;", (i, i, i)),
        PreparedOp(
            "payment_customer_update",
            "UPDATE customer SET c_balance = c_balance - ?, "
            "c_ytd_payment = c_ytd_payment + ?, "
            "c_payment_cnt = c_payment_cnt + 1 "
            "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?;",
            (f, f, i, i, i)),
        PreparedOp(
            "payment_history_insert",
            "INSERT INTO history VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
            (i, i, i, i, i, c, f, c)),
        PreparedOp(
            "order_status_customer",
            "SELECT c_balance, c_first, c_middle, c_last FROM customer "
            "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?;", (i, i, i)),
        PreparedOp(
            "order_status_order",
            "SELECT MAX(o_id) FROM orders WHERE o_w_id = ? "
            "AND o_d_id = ? AND o_c_id = ?;", (i, i, i)),
        PreparedOp(
            "order_status_lines",
            "SELECT ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, "
            "ol_delivery_d FROM order_line WHERE ol_w_id = ? "
            "AND ol_d_id = ? AND ol_o_id = ?;", (i, i, i)),
        PreparedOp(
            "delivery_oldest",
            "SELECT MIN(no_o_id) FROM new_orders WHERE no_w_id = ? "
            "AND no_d_id = ?;", (i, i)),
        PreparedOp(
            "delivery_order",
            "SELECT o_c_id FROM orders WHERE o_w_id = ? AND o_d_id = ? "
            "AND o_id = ?;", (i, i, i)),
        PreparedOp(
            "delivery_amount",
            "SELECT SUM(ol_amount) FROM order_line WHERE ol_w_id = ? "
            "AND ol_d_id = ? AND ol_o_id = ?;", (i, i, i)),
        PreparedOp(
            "delivery_queue_delete",
            "DELETE FROM new_orders WHERE no_w_id = ? AND no_d_id = ? "
            "AND no_o_id = ?;", (i, i, i)),
        PreparedOp(
            "delivery_order_update",
            "UPDATE orders SET o_carrier_id = ? WHERE o_w_id = ? "
            "AND o_d_id = ? AND o_id = ?;", (i, i, i, i)),
        PreparedOp(
            "delivery_lines_update",
            "UPDATE order_line SET ol_delivery_d = ? WHERE ol_w_id = ? "
            "AND ol_d_id = ? AND ol_o_id = ?;", (c, i, i, i)),
        PreparedOp(
            "delivery_customer_update",
            "UPDATE customer SET c_balance = c_balance + ?, "
            "c_delivery_cnt = c_delivery_cnt + 1 WHERE c_w_id = ? "
            "AND c_d_id = ? AND c_id = ?;", (f, i, i, i)),
        PreparedOp(
            "stock_level_next_order",
            "SELECT d_next_o_id FROM district WHERE d_w_id = ? AND d_id = ?;",
            (i, i)),
        PreparedOp(
            "stock_level_count",
            "SELECT COUNT(DISTINCT s_i_id) FROM order_line, stock "
            "WHERE ol_w_id = ? AND ol_d_id = ? AND ol_o_id >= ? "
            "AND ol_o_id < ? AND s_w_id = ? AND s_i_id = ol_i_id "
            "AND s_quantity < ?;", (i, i, i, i, i, i)),
    )


PREPARED: tuple[PreparedOp, ...] = build_prepared_catalog()
STATEMENT_IDS = {op.name: index + 1 for index, op in enumerate(PREPARED)}
PREPARED_BY_NAME = {op.name: op for op in PREPARED}


@dataclass(frozen=True)
class WirePreparedOp:
    statement_id: int
    result_kind: int
    parameter_types: tuple[int, ...]
    sql: str


def wire_sql(prepared: PreparedOp) -> str:
    """Translate contract placeholders to this checkout's numbered markers."""
    parts = prepared.sql.split("?")
    if len(parts) - 1 != len(prepared.parameter_types):
        raise ValueError(f"parameter types do not match {prepared.name}")
    return "".join(part if index == 0 else f"${index}{part}"
                   for index, part in enumerate(parts))


class FinalsWireClient(WireClient):
    """WireClient extension that fully consumes typed query rows in batches."""

    def prepare_set(self, statements):
        translated = tuple(
            WirePreparedOp(statement.statement_id, statement.result_kind,
                           statement.parameter_types, wire_sql(statement))
            for statement in statements)
        return super().prepare_set(translated)

    def execute_batch(self, operations, auto_abort=True):
        self._batch_operation_ids = [statement_id
                                     for statement_id, _ in operations]
        return super().execute_batch(operations, auto_abort)

    def _read_batch_response(self):
        tag, _flags, payload = read_frame(self.sock)
        if tag != TAG_BATCH_RESULT:
            raise WireProtocolError(
                f"expected BATCH_RESULT (0x15), got 0x{tag:02x}")
        reader = _WireReader(payload)
        result = WireBatchResult()
        result.executed_operations = reader.u16()
        result.status = reader.u8()
        result.failed_operation = reader.u16()
        result.diagnostic = reader.string(reader.u32())
        for _ in range(reader.u16()):
            operation_index = reader.u16()
            if operation_index >= len(self._batch_operation_ids):
                raise WireProtocolError("BATCH_RESULT query index is invalid")
            schema = self._prepared[self._batch_operation_ids[operation_index]]
            rows = []
            for _ in range(reader.u32()):
                rows.append([decode_cell(reader, column.sql_type)
                             for column in schema.columns])
            result.results.append(BatchOperationResult(operation_index, rows))
        reader.require_consumed()
        return result


def _typed_value(sql_type: int, value) -> TypedValue:
    if value is None:
        return TypedValue.null(sql_type)
    if sql_type == SQL_INT32:
        return TypedValue.int32(int(value))
    if sql_type == SQL_FLOAT32:
        return TypedValue.float32(float(value))
    if sql_type == SQL_CHAR:
        return TypedValue.char(str(value))
    raise ValueError(f"unsupported Wire SQL type: {sql_type}")


def operation(name: str, *values) -> tuple[int, list[TypedValue]]:
    prepared = PREPARED_BY_NAME[name]
    if len(values) != len(prepared.parameter_types):
        raise ValueError(f"{name} expects {len(prepared.parameter_types)} values")
    return (prepared.statement_id,
            [_typed_value(sql_type, value)
             for sql_type, value in zip(prepared.parameter_types, values)])


def _timestamp() -> str:
    return "2026-08-14 12:00:00"


def _customer(rng: random.Random, args) -> int:
    return rng.randint(1, args.customers_per_district)


def _district(rng: random.Random, args) -> int:
    return rng.randint(1, args.districts_per_warehouse)


def _order_id(rng: random.Random) -> int:
    # Client-generated ids avoid a second wire round trip while preserving the
    # prepared operation shape; collisions remain vanishingly unlikely per key.
    return rng.randint(1_000_000, 2_000_000_000)


def build_new_order(rng: random.Random, args, warehouse: int):
    district = _district(rng, args)
    customer = _customer(rng, args)
    order_id = _order_id(rng)
    line_count = rng.randint(5, 15)
    operations = [operation("begin"),
                  operation("new_order_customer", warehouse, district, customer),
                  operation("new_order_district", warehouse, district),
                  operation("new_order_district_update", warehouse, district),
                  operation("new_order_orders_insert", order_id, district, warehouse,
                            customer, _timestamp(), 0, line_count, 1),
                  operation("new_order_queue_insert", order_id, district, warehouse)]
    for line_number in range(1, line_count + 1):
        item = rng.randint(1, args.items)
        supply = warehouse if args.warehouses == 1 or rng.random() < 0.93 else \
            rng.choice([candidate for candidate in range(1, args.warehouses + 1)
                        if candidate != warehouse])
        quantity = rng.randint(1, 10)
        operations.extend((operation("new_order_item", item),
                           operation("new_order_stock", supply, item),
                           operation("new_order_stock_update", quantity, float(quantity),
                                     supply, item),
                           operation("new_order_line_insert", order_id, district,
                                     warehouse, line_number, item, supply, "",
                                     quantity, float(quantity * 10),
                                     f"line-{line_number}")))
    operations.append(operation("commit"))
    return operations


def build_payment(rng: random.Random, args, warehouse: int):
    district = _district(rng, args)
    customer = _customer(rng, args)
    amount = rng.randint(100, 5000) / 100.0
    return [operation("begin"),
            operation("payment_warehouse_update", amount, warehouse),
            operation("payment_district_update", amount, warehouse, district),
            operation("payment_customer", warehouse, district, customer),
            operation("payment_customer_update", amount, amount, warehouse,
                      district, customer),
            operation("payment_history_insert", customer, district, warehouse,
                      district, warehouse, _timestamp(), amount, "finals-payment"),
            operation("commit")]


def build_order_status(rng: random.Random, args, warehouse: int):
    district = _district(rng, args)
    customer = _customer(rng, args)
    return [operation("begin"),
            operation("order_status_customer", warehouse, district, customer),
            operation("order_status_order", warehouse, district, customer),
            operation("order_status_lines", warehouse, district, _order_id(rng)),
            operation("commit")]


def build_delivery(rng: random.Random, args, warehouse: int):
    district = _district(rng, args)
    order_id = _order_id(rng)
    customer = _customer(rng, args)
    amount = rng.randint(100, 5000) / 100.0
    return [operation("begin"),
            operation("delivery_oldest", warehouse, district),
            operation("delivery_order", warehouse, district, order_id),
            operation("delivery_amount", warehouse, district, order_id),
            operation("delivery_queue_delete", warehouse, district, order_id),
            operation("delivery_order_update", rng.randint(1, 10), warehouse,
                      district, order_id),
            operation("delivery_lines_update", _timestamp(), warehouse, district,
                      order_id),
            operation("delivery_customer_update", amount, warehouse, district,
                      customer),
            operation("commit")]


def build_stock_level(rng: random.Random, args, warehouse: int):
    district = _district(rng, args)
    next_order = rng.randint(1, 2_000_000_000)
    return [operation("begin"),
            operation("stock_level_next_order", warehouse, district),
            operation("stock_level_count", warehouse, district,
                      max(1, next_order - 20), next_order, warehouse,
                      rng.randint(10, 20)),
            operation("commit")]


BUILDERS: dict[str, Callable] = {
    "new_order": build_new_order,
    "payment": build_payment,
    "order_status": build_order_status,
    "delivery": build_delivery,
    "stock_level": build_stock_level,
}


def choose_transaction(rng: random.Random) -> str:
    slot = rng.randrange(sum(MIX))
    total = 0
    for name, weight in zip(TXN_NAMES, MIX):
        total += weight
        if slot < total:
            return name
    raise AssertionError("transaction mix did not select a transaction")


def _require_command(result, sql: str) -> None:
    if not result.is_command_ok:
        raise RuntimeError(f"session setup failed: {sql}\n{result.diagnostic}")


def connect_worker(args) -> FinalsWireClient:
    client = FinalsWireClient(args.host, args.port, args.timeout)
    client.connect()
    _require_command(client.execute_stream(
        "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;"),
        "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
    prepared = client.prepare_set(PREPARED)
    if prepared.is_error:
        client.close()
        raise RuntimeError(f"PREPARE_SET failed: {prepared.diagnostic}")
    return client


@dataclass
class WorkerStats:
    attempts: dict[str, int] = field(
        default_factory=lambda: {name: 0 for name in TXN_NAMES})
    commits: dict[str, int] = field(
        default_factory=lambda: {name: 0 for name in TXN_NAMES})
    conflicts: dict[str, int] = field(
        default_factory=lambda: {name: 0 for name in TXN_NAMES})
    expected_business_rollbacks: int = 0
    abandoned: int = 0
    latency_ms: list[float] = field(default_factory=list)
    warehouse_ids: set[int] = field(default_factory=set)

    def merge(self, other) -> None:
        for name in TXN_NAMES:
            self.attempts[name] += other.attempts[name]
            self.commits[name] += other.commits[name]
            self.conflicts[name] += other.conflicts[name]
        self.expected_business_rollbacks += other.expected_business_rollbacks
        self.abandoned += other.abandoned
        self.latency_ms.extend(other.latency_ms)
        self.warehouse_ids.update(other.warehouse_ids)


def execute_transaction(client: FinalsWireClient, name: str,
                        rng: random.Random, args, warehouse: int):
    operations = BUILDERS[name](rng, args, warehouse)
    return client.execute_batch(operations, auto_abort=True)


def phase_worker(worker_id: int, round_id: int, args, barrier, clock):
    stats = WorkerStats()
    rng = random.Random(args.seed + round_id * 100003 + worker_id)
    client = None
    sequence = 0
    try:
        try:
            client = connect_worker(args)
        except (ConnectionError, OSError, TimeoutError, RuntimeError):
            stats.abandoned += 1
        barrier.wait(timeout=args.startup_timeout + args.timeout + 5)
        while time.monotonic() < clock["deadline"]:
            if client is None:
                try:
                    client = connect_worker(args)
                except (ConnectionError, OSError, TimeoutError, RuntimeError):
                    stats.abandoned += 1
                    time.sleep(0.05)
                    continue
            warehouse = (worker_id + sequence * args.clients) % args.warehouses + 1
            sequence += 1
            stats.warehouse_ids.add(warehouse)
            name = choose_transaction(rng)
            stats.attempts[name] += 1
            started = time.monotonic()
            try:
                result = execute_transaction(client, name, rng, args, warehouse)
                if result.status == BATCH_OK:
                    stats.commits[name] += 1
                elif result.status == BATCH_TRANSACTION_ABORT:
                    stats.conflicts[name] += 1
                elif result.status == BATCH_ERROR:
                    stats.abandoned += 1
                else:
                    raise WireProtocolError(f"unknown batch status {result.status}")
            except (ConnectionError, OSError, TimeoutError, WireProtocolError):
                stats.abandoned += 1
                client.close()
                client = None
            finally:
                stats.latency_ms.append((time.monotonic() - started) * 1000.0)
    finally:
        if client is not None:
            client.close()
    return stats


def percentile(values: Iterable[float], percentile_value: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = min(len(ordered) - 1,
                max(0, math.ceil(len(ordered) * percentile_value) - 1))
    return ordered[index]


def sample_process(pid: int | None) -> dict:
    if pid is None or os.name == "nt":
        return {"available": False}
    proc = Path(f"/proc/{pid}")
    try:
        stat = (proc / "stat").read_text(encoding="utf-8").split()
        status = (proc / "status").read_text(encoding="utf-8")
        io_text = (proc / "io").read_text(encoding="utf-8")
    except OSError:
        return {"available": False}
    rss_match = re.search(r"^VmRSS:\s+(\d+)\s+kB$", status, re.MULTILINE)
    io = {key: int(value) for key, value in
          re.findall(r"^(rchar|wchar|read_bytes|write_bytes):\s+(\d+)$",
                     io_text, re.MULTILINE)}
    ticks = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    return {
        "available": True,
        "cpu_seconds": (int(stat[13]) + int(stat[14])) / ticks,
        "rss_bytes": int(rss_match.group(1)) * 1024 if rss_match else 0,
        "io": io,
    }


def metric_delta(before: dict, after: dict, elapsed: float) -> tuple[dict, dict, dict]:
    if not before.get("available") or not after.get("available"):
        return ({"available": False}, {"available": False}, {"available": False})
    cpu_seconds = max(0.0, after["cpu_seconds"] - before["cpu_seconds"])
    before_io = before["io"]
    after_io = after["io"]
    return (
        {"seconds": cpu_seconds,
         "percent_of_one_core": cpu_seconds * 100.0 / elapsed if elapsed else 0.0},
        {"bytes": after["rss_bytes"]},
        {key: max(0, after_io.get(key, 0) - before_io.get(key, 0))
         for key in sorted(set(before_io) | set(after_io))},
    )


def run_phase(args, round_id: int, label: str, seconds: float, server_pid):
    barrier = threading.Barrier(args.clients + 1)
    clock = {"deadline": float("inf")}
    before = sample_process(server_pid)
    with ThreadPoolExecutor(max_workers=args.clients) as pool:
        futures = [pool.submit(phase_worker, worker, round_id, args, barrier, clock)
                   for worker in range(args.clients)]
        barrier.wait(timeout=args.startup_timeout + args.timeout + 5)
        started = time.monotonic()
        clock["deadline"] = started + seconds
        combined = WorkerStats()
        for future in futures:
            combined.merge(future.result())
    elapsed = time.monotonic() - started
    after = sample_process(server_pid)
    cpu, rss, io = metric_delta(before, after, elapsed)
    attempts = sum(combined.attempts.values())
    commits = sum(combined.commits.values())
    latency = {
        "count": len(combined.latency_ms),
        "mean": statistics.fmean(combined.latency_ms) if combined.latency_ms else 0.0,
        "p50": percentile(combined.latency_ms, 0.50),
        "p95": percentile(combined.latency_ms, 0.95),
        "max": max(combined.latency_ms, default=0.0),
    }
    per_transaction = {
        name: {
            "attempted": combined.attempts[name],
            "committed": combined.commits[name],
            "conflicts": combined.conflicts[name],
        }
        for name in TXN_NAMES
    }
    return {
        "round": round_id,
        "label": label,
        "configured_seconds": seconds,
        "actual_seconds": elapsed,
        "new_order_per_min": (combined.commits["new_order"] * 60.0 / elapsed
                              if elapsed else 0.0),
        "completion": {
            "attempted": attempts,
            "committed": commits,
            "rate": commits / attempts if attempts else 0.0,
            "transactions": per_transaction,
            "expected_business_rollbacks": combined.expected_business_rollbacks,
        },
        "abandoned": {
            "count": combined.abandoned,
            "rate": combined.abandoned / attempts if attempts else 0.0,
        },
        "latency_ms": latency,
        "warehouse_coverage": {
            "required_ids": list(range(1, args.warehouses + 1)),
            "observed_ids": sorted(combined.warehouse_ids),
            "missing_ids": sorted(set(range(1, args.warehouses + 1)) -
                                  combined.warehouse_ids),
        },
        "cpu": cpu,
        "rss": rss,
        "io": io,
    }


def read_sql_statements(path: Path) -> list[str]:
    text = re.sub(r"--[^\n]*", "", path.read_text(encoding="utf-8"))
    return [statement.strip() + ";" for statement in text.split(";")
            if statement.strip()]


def load_path_for_server(csv_path: Path, database_dir: Path) -> str:
    """Return a LOAD path from RMDB's database-directory working directory."""
    return os.path.relpath(csv_path.resolve(), Path(database_dir).resolve()).replace(
        os.sep, "/")


def setup_database(args) -> None:
    tables = ("warehouse", "item", "stock", "district", "customer", "history",
              "orders", "new_orders", "order_line")
    missing = [name for name in tables if not (args.data_dir / f"{name}.csv").is_file()]
    if missing:
        raise FileNotFoundError(f"missing generated CSV files: {', '.join(missing)}")
    client = FinalsWireClient(args.host, args.port, args.timeout)
    try:
        client.connect()
        for sql in read_sql_statements(PERFORMANCE_DIR / "00_create_tpcc_tables.sql"):
            _require_command(client.execute_stream(sql), sql)
        for table in tables:
            csv_path = (args.data_dir / f"{table}.csv").resolve()
            path_for_server = load_path_for_server(csv_path, args.db_dir)
            sql = f"LOAD {path_for_server} INTO {table};"
            _require_command(client.execute_stream(sql), sql)
        for sql in read_sql_statements(PERFORMANCE_DIR / "02_create_primary_indexes.sql"):
            _require_command(client.execute_stream(sql), sql)
    finally:
        client.close()


def parse_server_counters(log_path: Path, io: dict) -> dict:
    raw = {}
    if log_path.is_file():
        for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if "RMDB_PERF_DIAG" not in line:
                continue
            raw.update({key: int(value) for key, value in
                        re.findall(r"([a-z_]+)=(\d+)", line)})
    return {
        "raw": raw,
        "conflicts": {key: value for key, value in raw.items()
                      if "conflict" in key},
        "latches": {key: value for key, value in raw.items()
                    if "latch" in key or "wait" in key},
        "fsync": raw.get("wal_fsync", 0),
        "bytes_written": io.get("write_bytes", 0),
    }


def validate_args(args) -> None:
    requested_formal = (args.clients == CLIENTS and args.warehouses == WAREHOUSES
                        and args.warmup == WARMUP_SECONDS
                        and args.measure == MEASURE_SECONDS
                        and args.rounds == ROUNDS)
    if not args.exploratory and not requested_formal:
        raise SystemExit("non-default workload settings require --exploratory")
    if args.exploratory and requested_formal:
        raise SystemExit("--exploratory is for a non-default diagnostic run")
    if min(args.clients, args.warehouses, args.rounds) < 1:
        raise SystemExit("clients, warehouses, and rounds must be positive")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clients", type=int, default=CLIENTS)
    parser.add_argument("--warehouses", type=int, default=WAREHOUSES)
    parser.add_argument("--warmup", type=float, default=WARMUP_SECONDS)
    parser.add_argument("--measure", type=float, default=MEASURE_SECONDS)
    parser.add_argument("--rounds", type=int, default=ROUNDS)
    parser.add_argument("--districts-per-warehouse", type=int,
                        default=DISTRICTS_PER_WAREHOUSE)
    parser.add_argument("--customers-per-district", type=int,
                        default=CUSTOMERS_PER_DISTRICT)
    parser.add_argument("--items", type=int, default=ITEMS)
    parser.add_argument("--seed", type=int, default=20260814)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--exploratory", action="store_true",
                        help="mark a non-default run as diagnostic only")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--data-dir", type=Path,
                        default=REPO_ROOT / "src" / "test" /
                        "performance_test" / "table_data" / "tpcc_full")
    parser.add_argument("--json-output", type=Path,
                        default=REPO_ROOT / "build" / "finals_workload.json")
    add_server_arguments(parser, "finals_workload", "finals_workload_server.log",
                         REPO_ROOT / "build")
    return parser.parse_args()


class RecordingClient:
    def __init__(self):
        self.batches = []
        self.stream_calls = 0

    def execute_batch(self, operations, auto_abort=True):
        self.batches.append((operations, auto_abort))
        return type("Batch", (), {"status": BATCH_OK})()

    def execute_stream(self, _sql):
        self.stream_calls += 1
        raise AssertionError("ranked workload used a stream call")


def run_self_test() -> int:
    for prepared in PREPARED:
        if prepared.sql.count("?") != len(prepared.parameter_types):
            raise AssertionError(f"parameter types do not match {prepared.name}")
    if wire_sql(PREPARED_BY_NAME["new_order_customer"]) != (
            "SELECT c_discount, c_last, c_credit FROM customer "
            "WHERE c_w_id = $1 AND c_d_id = $2 AND c_id = $3;"):
        raise AssertionError("PREPARE_SET must number Wire parameter markers")
    expected_load_path = "../../src/test/performance_test/table_data/warehouse.csv"
    actual_load_path = load_path_for_server(
        REPO_ROOT / "src" / "test" / "performance_test" / "table_data" /
        "warehouse.csv", REPO_ROOT / "build" / "finals_workload")
    if actual_load_path != expected_load_path:
        raise AssertionError("LOAD path must be relative to the database directory")
    args = argparse.Namespace(warehouses=50, districts_per_warehouse=10,
                              customers_per_district=3000, items=100000)
    client = RecordingClient()
    for index, name in enumerate(TXN_NAMES):
        execute_transaction(client, name, random.Random(index), args, index + 1)
    decoded = []
    for operations, auto_abort in client.batches:
        if not auto_abort:
            raise AssertionError("ranked batch omitted AUTO_ABORT")
        names = [PREPARED[statement_id - 1].name for statement_id, _ in operations]
        if names[0] != "begin" or names[-1] != "commit":
            raise AssertionError("transaction batch is missing BEGIN or COMMIT")
        decoded.append(names)
    def new_order_with_line_count(line_count):
        for seed in range(1000):
            batch = build_new_order(random.Random(seed), args, 1)
            actual = sum(statement_id == STATEMENT_IDS["new_order_line_insert"]
                         for statement_id, _ in batch)
            if actual == line_count:
                return batch
        raise AssertionError(f"could not build a {line_count}-line NewOrder")

    five_line = new_order_with_line_count(5)
    fifteen_line = new_order_with_line_count(15)
    if client.stream_calls != 0:
        raise AssertionError("ranked workload used a stream call")
    print("self-test PASS: decoded five transaction shapes, 5- and 15-line "
          "NewOrder batches, and no ranked EXEC_STREAM call")
    return 0


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    validate_args(args)
    args.data_dir = args.data_dir.resolve()
    server = None
    try:
        if args.start_server:
            server = RMDBServerManager.from_args(args, log_name="finals_workload_server.log")
            server.start()
            args.db_dir = server.db_dir
            if args.reset_db:
                setup_database(args)
        server_pid = server.process.pid if server is not None else None
        measured = []
        for round_id in range(1, args.rounds + 1):
            run_phase(args, round_id * 2, f"round-{round_id}-warmup",
                      args.warmup, server_pid)
            measured.append(run_phase(args, round_id * 2 + 1,
                                      f"round-{round_id}-measure",
                                      args.measure, server_pid))
        aggregate_io = {}
        for round_result in measured:
            for key, value in round_result["io"].items():
                aggregate_io[key] = aggregate_io.get(key, 0) + value
        score_values = [round_result["new_order_per_min"]
                        for round_result in measured]
        aggregate = {
            "completion": {"attempted": sum(round_result["completion"]["attempted"]
                                              for round_result in measured),
                           "committed": sum(round_result["completion"]["committed"]
                                            for round_result in measured)},
            "abandoned": sum(round_result["abandoned"]["count"]
                             for round_result in measured),
            "latency_ms": {"p95": max((round_result["latency_ms"]["p95"]
                                        for round_result in measured), default=0.0)},
            "warehouse_coverage": [round_result["warehouse_coverage"]
                                    for round_result in measured],
            "cpu": [round_result["cpu"] for round_result in measured],
            "rss": [round_result["rss"] for round_result in measured],
            "io": aggregate_io,
        }
        if aggregate["completion"]["attempted"]:
            aggregate["completion"]["rate"] = (
                aggregate["completion"]["committed"] /
                aggregate["completion"]["attempted"])
        else:
            aggregate["completion"]["rate"] = 0.0
        server_counters = parse_server_counters(
            server.server_log if server is not None else Path(), aggregate_io)
        report = {
            "formal": not args.exploratory,
            "configuration": {"clients": args.clients, "warehouses": args.warehouses,
                              "warmup_seconds": args.warmup,
                              "measure_seconds": args.measure,
                              "rounds": args.rounds, "mix": MIX,
                              "data_dir": str(args.data_dir)},
            "rounds": measured,
            "median_new_order_per_min": (
                statistics.median(score_values) if not args.exploratory else None),
            "exploratory_new_order_per_min_median": (
                statistics.median(score_values) if args.exploratory else None),
            **aggregate,
            "server_counters": server_counters,
        }
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        if args.exploratory:
            print("Exploratory run complete; no formal median was reported.")
        else:
            print(f"Formal median NewOrder/min: {report['median_new_order_per_min']:.2f}")
        print(f"Result JSON: {args.json_output}")
        return 0
    finally:
        if server is not None:
            server.stop()


if __name__ == "__main__":
    raise SystemExit(main())
