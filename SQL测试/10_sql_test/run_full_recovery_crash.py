#!/usr/bin/env python3
import argparse
import re
import sys
import time
from pathlib import Path

from run_sql import SqlClient, read_statements


SECOND_CLIENT_PATTERN = re.compile(
    r"^\s*--\s*SECOND_CLIENT:\s*(.+?)\s*$", re.IGNORECASE
)


def read_second_client_statement(path: Path):
    statements = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = SECOND_CLIENT_PATTERN.match(line)
        if match:
            statement = match.group(1).strip()
            statements.append(
                statement if statement.endswith(";") else statement + ";"
            )
    if len(statements) != 1:
        raise ValueError(
            f"expected exactly one SECOND_CLIENT directive, found {len(statements)}"
        )
    return statements[0]


def execute_and_print(client: SqlClient, statement: str):
    print(f">>> {statement}")
    response = client.execute(statement)
    if response:
        print(response)
    print("---")


def main():
    parser = argparse.ArgumentParser(
        description="Create durable loser logs and crash RMDB for recovery testing."
    )
    parser.add_argument("sql_file", type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--delay", type=float, default=0.05)
    args = parser.parse_args()

    if not args.sql_file.is_file():
        print(f"SQL file not found: {args.sql_file}", file=sys.stderr)
        return 2

    primary = SqlClient(args.host, args.port, args.timeout)
    flusher = SqlClient(args.host, args.port, args.timeout)
    second_statement = read_second_client_statement(args.sql_file)
    try:
        for statement in read_statements(args.sql_file):
            if statement.lower() == "crash":
                # This autocommit transaction flushes the shared WAL buffer,
                # including the still-active primary transaction's records.
                execute_and_print(flusher, second_statement)
                # A follow-up request is an explicit ordering barrier: the
                # server cannot process it before the previous autocommit
                # statement and its WAL flush have completed.
                execute_and_print(
                    flusher,
                    "SELECT * FROM recovery_full WHERE id = 7;",
                )
                execute_and_print(primary, statement)
                return 0
            execute_and_print(primary, statement)
            time.sleep(args.delay)
    finally:
        primary.close()
        flusher.close()

    raise RuntimeError("test input did not contain CRASH")


if __name__ == "__main__":
    raise SystemExit(main())
