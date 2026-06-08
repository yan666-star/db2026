#!/usr/bin/env python3
import argparse
import socket


class SqlClient:
    def __init__(self, host, port, timeout):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = socket.create_connection((host, port), timeout=timeout)

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def execute(self, statement):
        self.sock.sendall(statement.encode("utf-8") + b"\0")
        data = bytearray()
        while True:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise ConnectionError("server closed the connection")
            data.extend(chunk)
            if b"\0" in chunk:
                return bytes(data).split(b"\0", 1)[0].decode(
                    "utf-8", errors="replace"
                )


class Q9Tests:
    def __init__(self, host, port, timeout):
        self.host = host
        self.port = port
        self.timeout = timeout

    def client(self, level=None):
        client = SqlClient(self.host, self.port, self.timeout)
        if level is not None:
            response = client.execute(
                f"SET TRANSACTION ISOLATION LEVEL {level};"
            )
            self.expect_empty(response, "SET TRANSACTION")
        return client

    @staticmethod
    def expect_empty(response, label):
        if response:
            raise AssertionError(f"{label}: expected empty response, got {response!r}")

    @staticmethod
    def expect_abort(response, label):
        if response != "abort\n":
            raise AssertionError(f"{label}: expected 'abort\\n', got {response!r}")

    @staticmethod
    def rows(response):
        result = []
        for line in response.splitlines():
            stripped = line.strip()
            if not (stripped.startswith("|") and stripped.endswith("|")):
                continue
            cells = tuple(cell.strip() for cell in stripped[1:-1].split("|"))
            try:
                result.append(tuple(int(cell) for cell in cells))
            except ValueError:
                pass
        return result

    def expect_rows(self, response, expected, label):
        actual = self.rows(response)
        if sorted(actual) != sorted(expected):
            raise AssertionError(
                f"{label}: expected rows {expected}, got {actual}\n{response}"
            )

    def execute_empty(self, client, statement, label=None):
        response = client.execute(statement)
        self.expect_empty(response, label or statement)

    def setup(self, statements):
        client = self.client()
        try:
            for statement in statements:
                self.execute_empty(client, statement)
        finally:
            client.close()

    def final_rows(self, statement):
        client = self.client()
        try:
            return client.execute(statement)
        finally:
            client.close()

    def si_insert(self):
        self.setup(["CREATE TABLE q9_insert (id int, val int);"])
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(t1, "INSERT INTO q9_insert VALUES (1, 10);")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_insert;"),
                [(1, 10)],
                "SI insert is visible to its writer",
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_insert;"),
                [],
                "SI uncommitted insert is invisible",
            )
            self.execute_empty(t1, "COMMIT;")
            self.expect_rows(
                t2.execute("SELECT * FROM q9_insert;"),
                [],
                "SI old snapshot excludes a newly committed insert",
            )
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_insert;"),
            [(1, 10)],
            "SI committed insert",
        )

    def si_dirty_read(self):
        self.setup(
            [
                "CREATE TABLE q9_dirty (id int, val int);",
                "INSERT INTO q9_dirty VALUES (1, 100);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t1, "UPDATE q9_dirty SET val = 200 WHERE id = 1;"
            )
            self.expect_rows(
                t1.execute("SELECT * FROM q9_dirty WHERE id = 1;"),
                [(1, 200)],
                "SI writer reads its own update",
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_dirty WHERE id = 1;"),
                [(1, 100)],
                "SI does not dirty-read",
            )
            self.execute_empty(t1, "COMMIT;")
            self.expect_rows(
                t2.execute("SELECT * FROM q9_dirty WHERE id = 1;"),
                [(1, 100)],
                "SI repeatable snapshot after concurrent commit",
            )
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_dirty WHERE id = 1;"),
            [(1, 200)],
            "SI final committed update",
        )

    def si_update_conflicts(self):
        self.setup(
            [
                "CREATE TABLE q9_ww (id int, val int);",
                "INSERT INTO q9_ww VALUES (1, 100);",
                "INSERT INTO q9_ww VALUES (2, 100);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(t1, "UPDATE q9_ww SET val = 120 WHERE id = 1;")
            self.expect_abort(
                t2.execute("UPDATE q9_ww SET val = 90 WHERE id = 1;"),
                "SI concurrent write/write conflict",
            )
            self.execute_empty(t1, "COMMIT;")
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()

        t3 = self.client("SNAPSHOT ISOLATION")
        t4 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t3, "BEGIN;")
            self.execute_empty(t4, "BEGIN;")
            self.execute_empty(t3, "UPDATE q9_ww SET val = 130 WHERE id = 2;")
            self.execute_empty(t3, "COMMIT;")
            self.expect_abort(
                t4.execute("UPDATE q9_ww SET val = 80 WHERE id = 2;"),
                "SI stale-snapshot write/write conflict",
            )
            self.execute_empty(t4, "COMMIT;")
        finally:
            t3.close()
            t4.close()

        self.expect_rows(
            self.final_rows("SELECT * FROM q9_ww;"),
            [(1, 120), (2, 130)],
            "SI write/write conflict final state",
        )

    def si_delete_conflict(self):
        self.setup(
            [
                "CREATE TABLE q9_delete (id int, val int);",
                "INSERT INTO q9_delete VALUES (1, 100);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_delete WHERE id = 1;"),
                [(1, 100)],
                "SI delete conflict T1 initial read",
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_delete WHERE id = 1;"),
                [(1, 100)],
                "SI delete conflict T2 initial read",
            )
            self.execute_empty(t1, "DELETE FROM q9_delete WHERE id = 1;")
            self.expect_abort(
                t2.execute("DELETE FROM q9_delete WHERE id = 1;"),
                "SI concurrent delete conflict",
            )
            self.execute_empty(t1, "COMMIT;")
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_delete;"),
            [],
            "SI delete conflict final state",
        )

    def si_write_skew(self):
        self.setup(
            [
                "CREATE TABLE q9_si_duty (doctor_id int, on_call int);",
                "INSERT INTO q9_si_duty VALUES (1, 1);",
                "INSERT INTO q9_si_duty VALUES (2, 1);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_si_duty WHERE doctor_id = 2;"),
                [(2, 1)],
                "SI write skew T1 read",
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_si_duty WHERE doctor_id = 1;"),
                [(1, 1)],
                "SI write skew T2 read",
            )
            self.execute_empty(
                t1,
                "UPDATE q9_si_duty SET on_call = 0 WHERE doctor_id = 1;",
            )
            self.execute_empty(
                t2,
                "UPDATE q9_si_duty SET on_call = 0 WHERE doctor_id = 2;",
            )
            self.execute_empty(t1, "COMMIT;")
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_si_duty;"),
            [(1, 0), (2, 0)],
            "SI permits write skew",
        )

    def ser_repeatable_read(self):
        self.setup(
            [
                "CREATE TABLE q9_ser_rr (id int, val int);",
                "INSERT INTO q9_ser_rr VALUES (1, 100);",
            ]
        )
        t1 = self.client("SERIALIZABLE")
        t2 = self.client("SERIALIZABLE")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_ser_rr WHERE id = 1;"),
                [(1, 100)],
                "SER first snapshot read",
            )
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t2, "UPDATE q9_ser_rr SET val = 200 WHERE id = 1;"
            )
            self.execute_empty(t2, "COMMIT;")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_ser_rr WHERE id = 1;"),
                [(1, 100)],
                "SER repeatable snapshot read",
            )
            self.execute_empty(t1, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_ser_rr;"),
            [(1, 200)],
            "SER repeatable-read final state",
        )

    def ser_empty_predicate(self):
        self.setup(["CREATE TABLE q9_phantom (id int, val int);"])
        t1 = self.client("SERIALIZABLE")
        t2 = self.client("SERIALIZABLE")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_phantom WHERE id = 1;"),
                [],
                "SER empty predicate T1",
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_phantom WHERE id = 2;"),
                [],
                "SER empty predicate T2",
            )
            self.execute_empty(t1, "INSERT INTO q9_phantom VALUES (2, 20);")
            self.expect_abort(
                t2.execute("INSERT INTO q9_phantom VALUES (1, 10);"),
                "SER empty-predicate dangerous structure",
            )
            self.execute_empty(t1, "COMMIT;")
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_phantom;"),
            [(2, 20)],
            "SER empty-predicate final state",
        )

    def ser_write_skew(self):
        self.setup(
            [
                "CREATE TABLE q9_ser_duty (doctor_id int, on_call int);",
                "INSERT INTO q9_ser_duty VALUES (1, 1);",
                "INSERT INTO q9_ser_duty VALUES (2, 1);",
            ]
        )
        t1 = self.client("SERIALIZABLE")
        t2 = self.client("SERIALIZABLE")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.expect_rows(
                t1.execute("SELECT * FROM q9_ser_duty WHERE doctor_id = 2;"),
                [(2, 1)],
                "SER write skew T1 read",
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_ser_duty WHERE doctor_id = 1;"),
                [(1, 1)],
                "SER write skew T2 read",
            )
            self.execute_empty(
                t1,
                "UPDATE q9_ser_duty SET on_call = 0 WHERE doctor_id = 1;",
            )
            self.expect_abort(
                t2.execute(
                    "UPDATE q9_ser_duty SET on_call = 0 WHERE doctor_id = 2;"
                ),
                "SER write-skew dangerous structure",
            )
            self.execute_empty(t1, "COMMIT;")
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows("SELECT * FROM q9_ser_duty;"),
            [(1, 0), (2, 1)],
            "SER write-skew final state",
        )

    def run(self):
        tests = [
            self.si_insert,
            self.si_dirty_read,
            self.si_update_conflicts,
            self.si_delete_conflict,
            self.si_write_skew,
            self.ser_repeatable_read,
            self.ser_empty_predicate,
            self.ser_write_skew,
        ]
        for test in tests:
            test()
            print(f"PASS {test.__name__}")
        print(f"PASS all {len(tests)} q9 runtime tests")


def main():
    parser = argparse.ArgumentParser(
        description="Run RMDB question 9 SI/SSI runtime tests."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()
    Q9Tests(args.host, args.port, args.timeout).run()


if __name__ == "__main__":
    main()
