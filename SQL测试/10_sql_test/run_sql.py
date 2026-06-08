#!/usr/bin/env python3
import argparse
import socket
import sys
import time
from pathlib import Path


def read_statements(path: Path):
    statements = []
    buffer = []

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("--"):
            continue

        if line.rstrip(";").strip().lower() == "crash":
            if buffer:
                raise ValueError("CRASH must not appear inside another SQL statement")
            statements.append("crash")
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
        if statement:
            statements.append(statement if statement.endswith(";") else statement + ";")

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

        payload = statement.rstrip(";") if statement.lower() == "crash" else statement
        self.sock.sendall(payload.encode("utf-8") + b"\0")

        if statement.lower() == "crash":
            try:
                self.sock.recv(1)
            except (ConnectionResetError, socket.timeout):
                pass
            finally:
                self.close()
            return "<server crashed>"

        data = bytearray()
        while True:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise ConnectionError("server closed the connection")
            data.extend(chunk)
            if b"\0" in chunk:
                break
        return bytes(data).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser(description="Run an RMDB SQL test file over one persistent connection.")
    parser.add_argument("sql_file", type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--delay", type=float, default=0.05)
    args = parser.parse_args()

    if not args.sql_file.is_file():
        print(f"SQL file not found: {args.sql_file}", file=sys.stderr)
        return 2

    client = SqlClient(args.host, args.port, args.timeout)
    try:
        for statement in read_statements(args.sql_file):
            print(f">>> {statement}")
            try:
                response = client.execute(statement)
                if response:
                    print(response)
            except Exception as exc:
                print(f"ERROR: {exc}")
                return 1
            print("---")
            time.sleep(args.delay)
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
