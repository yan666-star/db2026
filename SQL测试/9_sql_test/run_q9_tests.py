#!/usr/bin/env python3
import argparse
import re
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

    @staticmethod
    def table_output(columns, rows):
        separator = "".join("+" + "-" * 18 for _ in columns) + "+\n"

        def record(values):
            return "".join(f"| {str(value):>16} " for value in values) + "|\n"

        return (
            separator
            + record(columns)
            + separator
            + "".join(record(row) for row in rows)
            + separator
            + f"Total record(s): {len(rows)}\n"
        )

    def expect_rows(self, response, expected, label, columns=("id", "val")):
        actual = self.rows(response)
        if actual != expected:
            raise AssertionError(
                f"{label}: expected rows {expected}, got {actual}\n{response}"
            )
        exact = self.table_output(columns, expected)
        if response != exact:
            raise AssertionError(
                f"{label}: table output differs from the required format\n"
                f"expected {exact!r}\n"
                f"actual   {response!r}"
            )

    def execute_empty(self, client, statement, label=None):
        response = client.execute(statement)
        self.expect_empty(response, label or statement)

    def setup(self, statements):
        client = self.client()
        try:
            table_names = []
            for statement in statements:
                match = re.match(
                    r"\s*CREATE\s+TABLE\s+([A-Za-z_][A-Za-z0-9_]*)",
                    statement,
                    re.IGNORECASE,
                )
                if match:
                    table_names.append(match.group(1))
            for table_name in reversed(table_names):
                # The server has no DROP TABLE IF EXISTS syntax. Ignore failure
                # when a table is absent so the test suite remains rerunnable.
                client.execute(f"DROP TABLE {table_name};")
            for statement in statements:
                self.execute_empty(client, statement)
        finally:
            client.close()

    def final_rows(self, statement, level=None):
        client = self.client(level)
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
            self.final_rows("SELECT * FROM q9_insert;", "SNAPSHOT ISOLATION"),
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
            self.final_rows(
                "SELECT * FROM q9_dirty WHERE id = 1;",
                "SNAPSHOT ISOLATION",
            ),
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
            self.final_rows("SELECT * FROM q9_ww;", "SNAPSHOT ISOLATION"),
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
            self.final_rows("SELECT * FROM q9_delete;", "SNAPSHOT ISOLATION"),
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
                ("doctor_id", "on_call"),
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_si_duty WHERE doctor_id = 1;"),
                [(1, 1)],
                "SI write skew T2 read",
                ("doctor_id", "on_call"),
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
            self.final_rows(
                "SELECT * FROM q9_si_duty;", "SNAPSHOT ISOLATION"
            ),
            [(1, 0), (2, 0)],
            "SI permits write skew",
            ("doctor_id", "on_call"),
        )

    def si_deadlock(self):
        self.setup(
            [
                "CREATE TABLE q9_deadlock (id int, val int);",
                "INSERT INTO q9_deadlock VALUES (1, 10);",
                "INSERT INTO q9_deadlock VALUES (2, 20);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t1, "UPDATE q9_deadlock SET val = 11 WHERE id = 1;"
            )
            self.execute_empty(
                t2, "UPDATE q9_deadlock SET val = 22 WHERE id = 2;"
            )
            self.expect_abort(
                t1.execute(
                    "UPDATE q9_deadlock SET val = 12 WHERE id = 2;"
                ),
                "SI deadlock victim",
            )
            self.execute_empty(
                t2, "UPDATE q9_deadlock SET val = 33 WHERE id = 1;"
            )
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_deadlock;", "SNAPSHOT ISOLATION"
            ),
            [(1, 33), (2, 22)],
            "SI deadlock final state",
        )

    def si_non_repeatable_read_lost_update(self):
        self.setup(
            [
                "CREATE TABLE q9_lost_update (id int, val int);",
                "INSERT INTO q9_lost_update VALUES (1, 100);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.expect_rows(
                t1.execute(
                    "SELECT * FROM q9_lost_update WHERE id = 1;"
                ),
                [(1, 100)],
                "SI lost update first read",
            )
            self.execute_empty(
                t2, "UPDATE q9_lost_update SET val = 200 WHERE id = 1;"
            )
            self.execute_empty(t2, "COMMIT;")
            self.expect_rows(
                t1.execute(
                    "SELECT * FROM q9_lost_update WHERE id = 1;"
                ),
                [(1, 100)],
                "SI lost update repeatable read",
            )
            self.expect_abort(
                t1.execute(
                    "UPDATE q9_lost_update SET val = 150 WHERE id = 1;"
                ),
                "SI stale writer abort",
            )
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_lost_update;",
                "SNAPSHOT ISOLATION",
            ),
            [(1, 200)],
            "SI lost update final state",
        )

    def si_deadlock_reverse_multi_row(self):
        self.setup(
            [
                "CREATE TABLE q9_deadlock_reverse (id int, val int);",
                "INSERT INTO q9_deadlock_reverse VALUES (1, 10);",
                "INSERT INTO q9_deadlock_reverse VALUES (2, 20);",
                "INSERT INTO q9_deadlock_reverse VALUES (3, 30);",
                "CREATE INDEX q9_deadlock_reverse (id);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t1,
                "UPDATE q9_deadlock_reverse SET val = 11 WHERE id = 1;",
            )
            self.execute_empty(
                t2,
                "UPDATE q9_deadlock_reverse SET val = 22 WHERE id = 2;",
            )
            self.expect_abort(
                t1.execute(
                    "UPDATE q9_deadlock_reverse SET val = 99 "
                    "WHERE id >= 1;"
                ),
                "SI reverse deadlock multi-row victim",
            )
            self.execute_empty(
                t2,
                "UPDATE q9_deadlock_reverse SET val = 33 WHERE id = 1;",
            )
            self.execute_empty(t1, "COMMIT;")
            self.execute_empty(t2, "COMMIT;")
        finally:
            t1.close()
            t2.close()

        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_deadlock_reverse;",
                "SNAPSHOT ISOLATION",
            ),
            [(1, 33), (2, 22), (3, 30)],
            "SI reverse deadlock rolls back all victim writes",
        )

    def si_lost_update_index_change(self):
        self.setup(
            [
                "CREATE TABLE q9_lost_index (id int, val int);",
                "INSERT INTO q9_lost_index VALUES (1, 100);",
                "CREATE INDEX q9_lost_index (id);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.expect_rows(
                t1.execute(
                    "SELECT * FROM q9_lost_index WHERE id = 1;"
                ),
                [(1, 100)],
                "SI indexed lost update first read",
            )
            self.execute_empty(
                t2,
                "UPDATE q9_lost_index SET id = 2, val = 200 "
                "WHERE id = 1;",
            )
            self.execute_empty(t2, "COMMIT;")
            self.expect_rows(
                t1.execute(
                    "SELECT * FROM q9_lost_index WHERE id = 1;"
                ),
                [(1, 100)],
                "SI old snapshot retains the old index key",
            )
            self.expect_rows(
                t1.execute(
                    "SELECT * FROM q9_lost_index WHERE id = 2;"
                ),
                [],
                "SI old snapshot excludes the new index key",
            )
            self.expect_abort(
                t1.execute(
                    "UPDATE q9_lost_index SET val = 150 WHERE id = 1;"
                ),
                "SI stale indexed writer abort",
            )
            self.execute_empty(t1, "COMMIT;")
        finally:
            t1.close()
            t2.close()

        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_lost_index;",
                "SNAPSHOT ISOLATION",
            ),
            [(2, 200)],
            "SI indexed lost update final state",
        )

    def si_delete_insert_conflict(self):
        self.setup(
            [
                "CREATE TABLE q9_delete_insert (id int, val int);",
                "INSERT INTO q9_delete_insert VALUES (1, 100);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t1, "DELETE FROM q9_delete_insert WHERE id = 1;"
            )
            self.expect_abort(
                t2.execute(
                    "INSERT INTO q9_delete_insert VALUES (1, 200);"
                ),
                "SI delete/insert write conflict",
            )
            self.execute_empty(t1, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_delete_insert;",
                "SNAPSHOT ISOLATION",
            ),
            [],
            "SI delete/insert final state",
        )

    def si_update_edge_cases(self):
        self.setup(
            [
                "CREATE TABLE q9_update_edges (id int, val int);",
                "INSERT INTO q9_update_edges VALUES (1, 10);",
                "INSERT INTO q9_update_edges VALUES (2, 20);",
                "CREATE INDEX q9_update_edges (id);",
            ]
        )

        t1 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(
                t1, "UPDATE q9_update_edges SET val = 11 WHERE id = 1;"
            )
            self.execute_empty(
                t1, "UPDATE q9_update_edges SET val = 12 WHERE id = 1;"
            )
            self.expect_rows(
                t1.execute("SELECT * FROM q9_update_edges WHERE id = 1;"),
                [(1, 12)],
                "SI repeated update reads its final pending version",
            )
            self.execute_empty(t1, "ROLLBACK;")
        finally:
            t1.close()

        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_update_edges;", "SNAPSHOT ISOLATION"
            ),
            [(1, 10), (2, 20)],
            "SI repeated update rollback",
        )

        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t2, "UPDATE q9_update_edges SET id = 3 WHERE id = 1;"
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_update_edges WHERE id = 3;"),
                [(3, 10)],
                "SI indexed update is visible to its writer",
            )
            self.execute_empty(t2, "COMMIT;")
        finally:
            t2.close()

        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_update_edges;", "SNAPSHOT ISOLATION"
            ),
            [(3, 10), (2, 20)],
            "SI indexed update final state",
        )

    def si_multi_row_update_conflict(self):
        self.setup(
            [
                "CREATE TABLE q9_multi_update (id int, val int);",
                "INSERT INTO q9_multi_update VALUES (1, 10);",
                "INSERT INTO q9_multi_update VALUES (2, 20);",
                "INSERT INTO q9_multi_update VALUES (3, 30);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t1, "UPDATE q9_multi_update SET val = 200 WHERE id = 2;"
            )
            self.expect_abort(
                t2.execute("UPDATE q9_multi_update SET val = 999;"),
                "SI multi-row update conflict",
            )
            self.execute_empty(t1, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_multi_update;", "SNAPSHOT ISOLATION"
            ),
            [(1, 10), (2, 200), (3, 30)],
            "SI multi-row conflict rolls back earlier rows",
        )

    def si_insert_then_update(self):
        self.setup(
            [
                "CREATE TABLE q9_insert_update (id int, val int);",
                "CREATE INDEX q9_insert_update (id);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(
                t1, "INSERT INTO q9_insert_update VALUES (1, 10);"
            )
            self.execute_empty(
                t1,
                "UPDATE q9_insert_update SET id = 2, val = 20 "
                "WHERE id = 1;",
            )
            self.expect_rows(
                t1.execute("SELECT * FROM q9_insert_update;"),
                [(2, 20)],
                "SI insert/update is visible to its writer",
            )
            self.execute_empty(t1, "COMMIT;")
        finally:
            t1.close()

        self.expect_rows(
            self.final_rows("SELECT * FROM q9_insert_update;"),
            [(2, 20)],
            "SI insert/update physical final state",
        )
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_insert_update WHERE id = 2;"
            ),
            [(2, 20)],
            "SI insert/update new index key",
        )
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_insert_update WHERE id = 1;"
            ),
            [],
            "SI insert/update removes old index key",
        )
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_insert_update;", "SNAPSHOT ISOLATION"
            ),
            [(2, 20)],
            "SI insert/update snapshot final state",
        )

        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t2, "INSERT INTO q9_insert_update VALUES (3, 30);"
            )
            self.execute_empty(
                t2,
                "UPDATE q9_insert_update SET id = 4, val = 40 "
                "WHERE id = 3;",
            )
            self.execute_empty(t2, "ROLLBACK;")
        finally:
            t2.close()

        self.expect_rows(
            self.final_rows("SELECT * FROM q9_insert_update;"),
            [(2, 20)],
            "SI insert/update rollback physical state",
        )

    def si_unique_update_conflict(self):
        self.setup(
            [
                "CREATE TABLE q9_unique_update (id int, val int);",
                "INSERT INTO q9_unique_update VALUES (1, 10);",
                "INSERT INTO q9_unique_update VALUES (2, 20);",
                "CREATE INDEX q9_unique_update (id);",
            ]
        )
        t1 = self.client("SNAPSHOT ISOLATION")
        t2 = self.client("SNAPSHOT ISOLATION")
        try:
            self.execute_empty(t1, "BEGIN;")
            self.execute_empty(t2, "BEGIN;")
            self.execute_empty(
                t1, "UPDATE q9_unique_update SET id = 3 WHERE id = 1;"
            )
            self.expect_abort(
                t2.execute(
                    "UPDATE q9_unique_update SET id = 3 WHERE id = 2;"
                ),
                "SI concurrent unique-key update conflict",
            )
            self.execute_empty(t1, "COMMIT;")
        finally:
            t1.close()
            t2.close()
        self.expect_rows(
            self.final_rows(
                "SELECT * FROM q9_unique_update;", "SNAPSHOT ISOLATION"
            ),
            [(3, 10), (2, 20)],
            "SI concurrent unique-key update final state",
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
            self.final_rows("SELECT * FROM q9_ser_rr;", "SERIALIZABLE"),
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
            self.final_rows("SELECT * FROM q9_phantom;", "SERIALIZABLE"),
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
                ("doctor_id", "on_call"),
            )
            self.expect_rows(
                t2.execute("SELECT * FROM q9_ser_duty WHERE doctor_id = 1;"),
                [(1, 1)],
                "SER write skew T2 read",
                ("doctor_id", "on_call"),
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
            self.final_rows("SELECT * FROM q9_ser_duty;", "SERIALIZABLE"),
            [(1, 0), (2, 1)],
            "SER write-skew final state",
            ("doctor_id", "on_call"),
        )

    def run(self):
        tests = [
            self.si_insert,
            self.si_dirty_read,
            self.si_update_conflicts,
            self.si_delete_conflict,
            self.si_write_skew,
            self.si_deadlock,
            self.si_non_repeatable_read_lost_update,
            self.si_deadlock_reverse_multi_row,
            self.si_lost_update_index_change,
            self.si_delete_insert_conflict,
            self.si_update_edge_cases,
            self.si_multi_row_update_conflict,
            self.si_insert_then_update,
            self.si_unique_update_conflict,
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
