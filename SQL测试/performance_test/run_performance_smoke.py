#!/usr/bin/env python3
import argparse
import shutil
import socket
import subprocess
import sys
import time
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
        print("performance smoke suite passed")
        return 0
    finally:
        stop_server(process, log_handle)


if __name__ == "__main__":
    raise SystemExit(main())
