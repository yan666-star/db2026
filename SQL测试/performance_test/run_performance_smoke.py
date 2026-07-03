#!/usr/bin/env python3
import argparse
import re
import shutil
import socket
import subprocess
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
NO_SEMICOLON_COMMANDS = {"set output_file off"}


def read_statements(path: Path):
    statements = []
    buffer = []

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("--"):
            continue

        lowered = line.lower()
        if lowered in NO_SEMICOLON_COMMANDS:
            if buffer:
                raise ValueError(f"{line!r} cannot appear inside another SQL statement")
            statements.append(line)
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
        if statement.lower() in NO_SEMICOLON_COMMANDS:
            statements.append(statement)
        else:
            raise ValueError(f"missing semicolon at end of statement in {path}: {statement}")

    return statements


class SqlClient:
    def __init__(self, host: str, port: int, timeout: float):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def execute(self, statement: str):
        if self.sock is None:
            self.connect()

        self.sock.sendall(statement.encode("utf-8") + b"\0")
        data = bytearray()
        while True:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed the connection")
            data.extend(chunk)
            if b"\0" in chunk:
                break
        return bytes(data).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def output_size(output_file: Path):
    if output_file is None or not output_file.exists():
        return 0
    return output_file.stat().st_size


def find_rmdb(build_dir: Path):
    candidates = [
        build_dir / "bin" / "rmdb",
        build_dir / "bin" / "rmdb.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        "cannot find rmdb binary; expected build/bin/rmdb or build/bin/rmdb.exe"
    )


def remove_db_dir(db_dir: Path, build_dir: Path):
    if not db_dir.exists():
        return
    if db_dir.resolve().parent != build_dir.resolve():
        raise RuntimeError(f"refusing to remove unexpected database directory: {db_dir}")
    shutil.rmtree(db_dir)


def wait_for_server(host: str, port: int, timeout: float, process, log_file: Path):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            log_text = log_file.read_text(encoding="utf-8", errors="replace")
            raise RuntimeError(f"server exited during startup\n{log_text}")
        try:
            sock = socket.create_connection((host, port), timeout=0.2)
            sock.close()
            return
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    raise TimeoutError(f"server did not become ready: {last_error}")


def start_server(args):
    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        raise FileNotFoundError(f"build directory not found: {build_dir}")

    rmdb = find_rmdb(build_dir)
    db_dir = build_dir / args.db_name
    if args.reset_db:
        remove_db_dir(db_dir, build_dir)

    args.server_log.parent.mkdir(parents=True, exist_ok=True)
    log_handle = args.server_log.open("w", encoding="utf-8", errors="replace")
    process = subprocess.Popen(
        [str(rmdb), args.db_name],
        cwd=str(build_dir),
        stdout=log_handle,
        stderr=subprocess.STDOUT,
    )
    wait_for_server(args.host, args.port, args.startup_timeout, process, args.server_log)
    return process, log_handle, db_dir


def stop_server(process, log_handle):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    if log_handle is not None:
        log_handle.close()


def run_files(args, output_file: Path):
    sql_files = args.sql_files or [SCRIPT_DIR / name for name in DEFAULT_SQL_FILES]
    client = SqlClient(args.host, args.port, args.timeout)
    output_off_size = None

    try:
        for sql_file in sql_files:
            sql_file = sql_file.resolve()
            print(f"=== {sql_file.name} ===")
            for statement in read_statements(sql_file):
                before_size = output_size(output_file)
                print(f">>> {statement}")
                response = client.execute(statement)
                if response:
                    print(response.rstrip())

                lowered = statement.lower()
                expected_failure = (
                    lowered.startswith("insert into orders") and
                    "'2026-07-01 10:00:03'" in lowered
                )
                if "failure" in response.lower() and not expected_failure:
                    raise AssertionError(f"statement failed: {statement}")

                if lowered in NO_SEMICOLON_COMMANDS:
                    after_size = output_size(output_file)
                    if not args.skip_output_file_check and after_size != before_size:
                        raise AssertionError(
                            "output.txt changed while processing set output_file off"
                        )
                    output_off_size = after_size
                    continue

                if output_off_size is not None and not args.skip_output_file_check:
                    after_size = output_size(output_file)
                    if after_size != output_off_size:
                        raise AssertionError(
                            f"output.txt changed after set output_file off: {statement}"
                        )

                if lowered.startswith("select min(name)"):
                    if "apple" not in response or "pear" not in response:
                        raise AssertionError("string MIN/MAX probe did not return apple and pear")

                if lowered.startswith("select h_data") and "2026-07-01 10:00:01" in lowered:
                    if "syntax-commit" not in response:
                        raise AssertionError("COMMIT TRANSACTION did not persist the inserted row")

                if lowered.startswith("select h_data") and "2026-07-01 10:00:02" in lowered:
                    if "syntax-rollback" in response:
                        raise AssertionError("ROLLBACK WORK left an inserted row behind")

                if lowered.startswith("select h_data") and "2026-07-01 10:00:03" in lowered:
                    if "failed-before-conflict" in response:
                        raise AssertionError("failed transaction kept writes before the conflict")

                if lowered.startswith("select h_data") and "2026-07-01 10:00:04" in lowered:
                    if "failed-after-conflict" in response:
                        raise AssertionError("failed transaction executed statements after abort")

                time.sleep(args.delay)
    finally:
        client.close()


def response_failed(response: str):
    lowered = response.lower()
    return "abort" in lowered or "failure" in lowered


def table_rows(response: str):
    rows = []
    for line in response.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        rows.append(cells)
    return rows[1:] if rows else []


def select_scalar_int(client: SqlClient, statement: str):
    response = client.execute(statement)
    if response_failed(response):
        raise AssertionError(f"select failed: {statement}\n{response}")
    rows = table_rows(response)
    if rows and rows[-1]:
        return int(float(rows[-1][0]))
    values = re.findall(r"-?\d+", response)
    if not values:
        raise AssertionError(f"no integer value returned for: {statement}\n{response}")
    return int(values[0])


def execute_explicit_txn(statements, host, port, timeout):
    client = SqlClient(host, port, timeout)
    responses = []
    failed = False
    try:
        for statement in statements:
            response = client.execute(statement)
            responses.append((statement, response))
            if response_failed(response):
                failed = True
                break
        if failed:
            try:
                responses.append(("ROLLBACK;", client.execute("ROLLBACK;")))
            except Exception as exc:  # pragma: no cover - diagnostic only
                responses.append(("ROLLBACK;", f"rollback error: {exc}"))
    finally:
        client.close()
    return not failed, responses


def new_order_statements(order_id, district_id, quantities):
    statements = [
        "set transaction isolation level snapshot isolation;",
        "BEGIN;",
        f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district_id};",
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 WHERE d_w_id = 1 AND d_id = {district_id};",
        f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 2, '2026-07-02 12:00:00', 0, {len(quantities)}, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
    ]
    for line_no, (item_id, qty) in enumerate(quantities, start=1):
        statements.extend([
            f"SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = {item_id};",
            f"UPDATE stock SET s_quantity = s_quantity - {qty} WHERE s_w_id = 1 AND s_i_id = {item_id};",
            f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, {line_no}, {item_id}, 1, '2026-07-02 12:00:00', {qty}, {float(qty) * 10.0}, 'probe-line-{line_no}');",
        ])
    statements.append("COMMIT;")
    return statements


def failure_probe_statements(order_id, district_id, stage):
    statements = [
        "set transaction isolation level snapshot isolation;",
        "BEGIN;",
        f"UPDATE district SET d_next_o_id = d_next_o_id + 1 WHERE d_w_id = 1 AND d_id = {district_id};",
    ]
    if stage == "district":
        statements.append("INSERT INTO orders VALUES (11, 1, 1, 2, '2026-07-02 12:10:00', 0, 1, 1);")
        return statements

    statements.extend([
        f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 2, '2026-07-02 12:10:00', 0, 2, 1);",
        f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);",
    ])
    if stage == "orders":
        statements.append("INSERT INTO new_orders VALUES (11, 1, 1);")
        return statements

    statements.extend([
        "UPDATE stock SET s_quantity = s_quantity - 7 WHERE s_w_id = 1 AND s_i_id = 1;",
    ])
    if stage == "stock":
        statements.append("INSERT INTO order_line VALUES (11, 1, 1, 1, 1, 1, '2026-07-02 12:10:00', 1, 1.0, 'dup-line');")
        return statements

    statements.extend([
        f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, 1, 1, 1, '2026-07-02 12:10:00', 7, 70.0, 'partial-line');",
        f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, 1, 1, 1, '2026-07-02 12:10:00', 7, 70.0, 'dup-partial-line');",
    ])
    return statements


def run_concurrent_consistency_probe(args):
    if args.skip_concurrent_probe:
        return

    verifier = SqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        district_keys = [(1, 1), (1, 2)]
        initial_next = {
            key: select_scalar_int(
                verifier,
                f"SELECT d_next_o_id FROM district WHERE d_w_id = {key[0]} AND d_id = {key[1]};",
            )
            for key in district_keys
        }
        stock_items = [(1, 1), (1, 2)]
        initial_stock = {
            key: select_scalar_int(
                verifier,
                f"SELECT s_quantity FROM stock WHERE s_w_id = {key[0]} AND s_i_id = {key[1]};",
            )
            for key in stock_items
        }
    finally:
        verifier.close()

    workloads = [
        {"order_id": 2101, "district_id": 1, "quantities": [(1, 1), (2, 2)]},
        {"order_id": 2102, "district_id": 1, "quantities": [(1, 2), (2, 1)]},
        {"order_id": 2103, "district_id": 1, "quantities": [(1, 3), (2, 1)]},
        {"order_id": 2201, "district_id": 2, "quantities": [(1, 1), (2, 1)]},
        {"order_id": 2202, "district_id": 2, "quantities": [(1, 2), (2, 2)]},
        {"order_id": 2203, "district_id": 2, "quantities": [(1, 1), (2, 3)]},
    ]
    committed = []
    aborted = []
    with ThreadPoolExecutor(max_workers=6) as executor:
        future_to_workload = {
            executor.submit(
                execute_explicit_txn,
                new_order_statements(w["order_id"], w["district_id"], w["quantities"]),
                args.host,
                args.port,
                args.timeout,
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
            rendered = "\n".join(f"{sql} => {resp!r}" for sql, resp in responses)
            raise AssertionError(f"failure probe unexpectedly committed at {stage}\n{rendered}")

    verifier = SqlClient(args.host, args.port, args.timeout)
    verifier.connect()
    try:
        committed_by_district = {key: 0 for key in district_keys}
        stock_delta = {key: 0 for key in stock_items}
        for workload in committed:
            committed_by_district[(1, workload["district_id"])] += 1
            for item_id, qty in workload["quantities"]:
                stock_delta[(1, item_id)] += qty

        for key, initial in initial_next.items():
            actual = select_scalar_int(
                verifier,
                f"SELECT d_next_o_id FROM district WHERE d_w_id = {key[0]} AND d_id = {key[1]};",
            )
            expected = initial + committed_by_district[key]
            if actual != expected:
                raise AssertionError(f"district {key} next_o_id mismatch: expected {expected}, got {actual}")

        for key, initial in initial_stock.items():
            actual = select_scalar_int(
                verifier,
                f"SELECT s_quantity FROM stock WHERE s_w_id = {key[0]} AND s_i_id = {key[1]};",
            )
            expected = initial - stock_delta[key]
            if actual != expected:
                raise AssertionError(f"stock {key} mismatch: expected {expected}, got {actual}")

        for workload in committed:
            order_id = workload["order_id"]
            district_id = workload["district_id"]
            full_key_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM orders WHERE o_w_id = 1 AND o_d_id = {district_id} AND o_id = {order_id};",
            )
            scan_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM orders WHERE o_id = {order_id};",
            )
            if full_key_count != 1 or scan_count != 1:
                raise AssertionError(f"committed order {order_id} is not visible through both indexed and scan predicates")
            new_order_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM new_orders WHERE no_w_id = 1 AND no_d_id = {district_id} AND no_o_id = {order_id};",
            )
            line_count = select_scalar_int(
                verifier,
                f"SELECT count(*) FROM order_line WHERE ol_w_id = 1 AND ol_d_id = {district_id} AND ol_o_id = {order_id};",
            )
            if new_order_count != 1 or line_count != len(workload["quantities"]):
                raise AssertionError(f"committed order {order_id} has incomplete child rows")

        failed_order_ids = [w["order_id"] for w, _ in aborted] + [order_id for order_id, _, _ in failure_probes]
        for order_id in failed_order_ids:
            orders_count = select_scalar_int(verifier, f"SELECT count(*) FROM orders WHERE o_id = {order_id};")
            new_orders_count = select_scalar_int(verifier, f"SELECT count(*) FROM new_orders WHERE no_o_id = {order_id};")
            line_count = select_scalar_int(verifier, f"SELECT count(*) FROM order_line WHERE ol_o_id = {order_id};")
            if orders_count != 0 or new_orders_count != 0 or line_count != 0:
                raise AssertionError(f"aborted order {order_id} left partial rows")
    finally:
        verifier.close()

    print(f"concurrent consistency probe passed ({len(committed)} committed, {len(aborted)} aborted)")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run a local smoke suite for the performance-test statement."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--delay", type=float, default=0.02)
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--startup-timeout", type=float, default=10.0)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--db-name", default="performance_smoke_db")
    parser.add_argument("--db-dir", type=Path)
    parser.add_argument("--server-log", type=Path, default=REPO_ROOT / "build" / "performance_smoke_server.log")
    parser.add_argument("--reset-db", action="store_true", default=True)
    parser.add_argument("--keep-db", action="store_false", dest="reset_db")
    parser.add_argument("--skip-output-file-check", action="store_true")
    parser.add_argument("--skip-concurrent-probe", action="store_true")
    parser.add_argument("sql_files", nargs="*", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    process = None
    log_handle = None
    db_dir = args.db_dir

    try:
        if args.start_server:
            process, log_handle, db_dir = start_server(args)
        if db_dir is None:
            db_dir = args.build_dir / args.db_name
        output_file = db_dir / "output.txt"
        run_files(args, output_file)
        run_concurrent_consistency_probe(args)
        print("performance smoke suite passed")
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
