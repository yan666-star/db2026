#!/usr/bin/env python3
"""Post-run TPCC consistency audit (district counters, stock, rollback residue).

Mirrors the official post-transaction validation hints:
  - d_next_o_id must track max(orders.o_id) per district
  - stock deltas must match committed order_line quantities
  - aborted New-Order transactions must not leave child rows
"""
import argparse
import re
import socket
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]


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


def table_rows(response: str):
    rows = []
    for line in response.splitlines():
        line = line.strip()
        if line.startswith("|") and line.endswith("|"):
            fields = [field.strip() for field in line[1:-1].split("|")]
            rows.append(fields)
    if len(rows) >= 2:
        return rows[1:]
    return []


def scalar_int(response: str):
    rows = table_rows(response)
    if rows and rows[-1]:
        return int(float(rows[-1][0]))
    values = re.findall(r"-?\d+", response)
    if not values:
        raise ValueError(f"no integer in response: {response!r}")
    return int(values[-1])


def response_failed(response: str):
    lowered = response.lower()
    return "failure" in lowered or "abort" in lowered


def run_new_order_si(district_id, quantities, invalid_item, host, port, timeout):
    """Run a TPCC New-Order under SI using d_next_o_id as the new o_id."""
    client = SqlClient(host, port, timeout)
    order_id = None
    try:
        steps = [
            "set transaction isolation level snapshot isolation;",
            "BEGIN;",
            f"SELECT d_next_o_id FROM district WHERE d_w_id = 1 AND d_id = {district_id};",
        ]
        for stmt in steps:
            resp = client.execute(stmt)
            if response_failed(resp):
                if not stmt.strip().upper().startswith("ROLLBACK"):
                    try:
                        client.execute("ROLLBACK;")
                    except Exception:
                        pass
                return False, order_id
            if stmt.startswith("SELECT d_next_o_id"):
                order_id = scalar_int(resp)

        resp = client.execute(
            f"UPDATE district SET d_next_o_id = d_next_o_id + 1 "
            f"WHERE d_w_id = 1 AND d_id = {district_id};"
        )
        if response_failed(resp):
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            return False, order_id

        resp = client.execute(
            f"INSERT INTO orders VALUES ({order_id}, {district_id}, 1, 2, "
            f"'2026-07-02 12:00:00', 0, {len(quantities)}, 1);"
        )
        if response_failed(resp):
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            return False, order_id

        resp = client.execute(
            f"INSERT INTO new_orders VALUES ({order_id}, {district_id}, 1);"
        )
        if response_failed(resp):
            try:
                client.execute("ROLLBACK;")
            except Exception:
                pass
            return False, order_id

        for line_no, (item_id, qty) in enumerate(quantities, start=1):
            resp = client.execute(f"SELECT i_id FROM item WHERE i_id = {item_id};")
            if response_failed(resp) or not table_rows(resp):
                client.execute("ROLLBACK;")
                return False, order_id
            if invalid_item:
                client.execute("ROLLBACK;")
                return False, order_id

            resp = client.execute(
                f"UPDATE stock SET s_quantity = s_quantity - {qty} "
                f"WHERE s_w_id = 1 AND s_i_id = {item_id};"
            )
            if response_failed(resp):
                try:
                    client.execute("ROLLBACK;")
                except Exception:
                    pass
                return False, order_id

            resp = client.execute(
                f"INSERT INTO order_line VALUES ({order_id}, {district_id}, 1, {line_no}, "
                f"{item_id}, 1, '2026-07-02 12:00:00', {qty}, {float(qty) * 10.0}, 'audit-line');"
            )
            if response_failed(resp):
                try:
                    client.execute("ROLLBACK;")
                except Exception:
                    pass
                return False, order_id

        resp = client.execute("COMMIT;")
        if response_failed(resp):
            return False, order_id
        return True, order_id
    finally:
        client.close()


def run_txn(statements, host, port, timeout):
    """Backward-compatible helper for static statement lists."""
    client = SqlClient(host, port, timeout)
    responses = []
    failed = False
    try:
        for stmt in statements:
            resp = client.execute(stmt)
            responses.append((stmt, resp))
            if response_failed(resp):
                failed = True
                if not stmt.strip().upper().startswith("ROLLBACK"):
                    try:
                        responses.append(("ROLLBACK;", client.execute("ROLLBACK;")))
                    except Exception as exc:
                        responses.append(("ROLLBACK;", str(exc)))
                break
    finally:
        client.close()
    return not failed, responses


def audit_district_counters(client: SqlClient):
    """d_next_o_id should equal max(o_id) + 1 for each district."""
    errors = []
    resp = client.execute("SELECT d_w_id, d_id, d_next_o_id FROM district;")
    for row in table_rows(resp):
        w_id, d_id, next_o_id = int(row[0]), int(row[1]), int(float(row[2]))
        max_resp = client.execute(
            f"SELECT MAX(o_id) FROM orders WHERE o_w_id = {w_id} AND o_d_id = {d_id};"
        )
        max_oid = scalar_int(max_resp)
        expected = max_oid + 1 if max_oid >= 0 else 1
        if next_o_id != expected:
            errors.append(
                f"district ({w_id}, {d_id}) next_o_id mismatch: "
                f"expected {expected}, got {next_o_id} (max_o_id={max_oid})"
            )
    return errors


def audit_new_orders_subset(client: SqlClient):
    errors = []
    resp = client.execute(
        "SELECT no_w_id, no_d_id, no_o_id FROM new_orders;"
    )
    for row in table_rows(resp):
        w_id, d_id, o_id = int(row[0]), int(row[1]), int(row[2])
        cnt = scalar_int(
            client.execute(
                f"SELECT COUNT(*) FROM orders WHERE o_w_id = {w_id} "
                f"AND o_d_id = {d_id} AND o_id = {o_id};"
            )
        )
        if cnt != 1:
            errors.append(f"new_orders ({w_id},{d_id},{o_id}) has no matching orders row")
    return errors


def audit_orphan_orders(client: SqlClient, aborted_orders, committed_order_ids):
    errors = []
    for entry in aborted_orders:
        w_id = entry.get("w_id", 1)
        d_id = entry["district_id"]
        oid = entry["order_id"]
        if (w_id, d_id, oid) in committed_order_ids:
            continue
        oc = scalar_int(
            client.execute(
                f"SELECT COUNT(*) FROM orders WHERE o_w_id = {w_id} "
                f"AND o_d_id = {d_id} AND o_id = {oid};"
            )
        )
        nc = scalar_int(
            client.execute(
                f"SELECT COUNT(*) FROM new_orders WHERE no_w_id = {w_id} "
                f"AND no_d_id = {d_id} AND no_o_id = {oid};"
            )
        )
        lc = scalar_int(
            client.execute(
                f"SELECT COUNT(*) FROM order_line WHERE ol_w_id = {w_id} "
                f"AND ol_d_id = {d_id} AND ol_o_id = {oid};"
            )
        )
        if oc or nc or lc:
            errors.append(
                f"aborted order ({w_id},{d_id},{oid}) left rows: "
                f"orders={oc} new_orders={nc} order_line={lc}"
            )
    return errors


def run_stress(args):
    """High-concurrency SI New-Order with mixed invalid-item rollbacks."""
    districts = [1, 2, 3]
    workloads = []
    for i in range(args.txn_count):
        d = districts[i % len(districts)]
        invalid = (i % 100) < 7  # ~7% invalid-item rollback like TPCC
        if invalid:
            quantities = [(99999, 1)]
        else:
            quantities = [(1 + (i % 3), 1 + (i % 4))]
        workloads.append({
            "district_id": d,
            "quantities": quantities,
            "invalid": invalid,
        })

    committed = []
    committed_order_ids = set()
    aborted_ids = []

    INITIAL_STOCK = {1: 54, 2: 17, 3: 97}
    baseline_client = SqlClient(args.host, args.port, args.timeout)
    baseline_client.connect()
    try:
        baseline_ol = {}
        for item_id in (1, 2, 3):
            baseline_ol[item_id] = scalar_int(
                baseline_client.execute(
                    f"SELECT SUM(ol_quantity) FROM order_line "
                    f"WHERE ol_w_id = 1 AND ol_i_id = {item_id};"
                )
            )
    finally:
        baseline_client.close()

    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        futures = {
            pool.submit(
                run_new_order_si,
                w["district_id"],
                w["quantities"],
                w["invalid"],
                args.host,
                args.port,
                args.timeout,
            ): w
            for w in workloads
        }
        for fut in as_completed(futures):
            w = futures[fut]
            ok, order_id = fut.result()
            if ok:
                committed.append(w)
                committed_order_ids.add((1, w["district_id"], order_id))
            elif order_id is not None:
                aborted_ids.append({"order_id": order_id, "district_id": w["district_id"]})

    aborted_ids = [
        {"order_id": oid, "district_id": did}
        for _, did, oid in {
            (1, entry["district_id"], entry["order_id"]) for entry in aborted_ids
        }
    ]

    client = SqlClient(args.host, args.port, args.timeout)
    client.connect()
    try:
        errors = []
        errors.extend(audit_district_counters(client))
        errors.extend(audit_new_orders_subset(client))
        errors.extend(audit_orphan_orders(client, aborted_ids, committed_order_ids))

        for item_id in (1, 2, 3):
            qty = scalar_int(
                client.execute(
                    f"SELECT SUM(ol_quantity) FROM order_line "
                    f"WHERE ol_w_id = 1 AND ol_i_id = {item_id};"
                )
            )
            sq = scalar_int(
                client.execute(
                    f"SELECT s_quantity FROM stock WHERE s_w_id = 1 AND s_i_id = {item_id};"
                )
            )
            stress_ol = qty - baseline_ol[item_id]
            expected = INITIAL_STOCK[item_id] - stress_ol
            if sq != expected:
                errors.append(
                    f"stock (1,{item_id}) mismatch: expected {expected}, got {sq} "
                    f"(initial={INITIAL_STOCK[item_id]}, stress_ol_quantity={stress_ol})"
                )

        if errors:
            print("AUDIT FAILED:")
            for e in errors:
                print(f"  {e}")
            return 1

        print(
            f"AUDIT PASS: {len(committed)} committed, "
            f"{len(aborted_ids)} aborted, concurrency={args.concurrency}"
        )
        return 0
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(description="TPCC post-run consistency audit")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--concurrency", type=int, default=16)
    parser.add_argument("--txn-count", type=int, default=200)
    parser.add_argument("--audit-only", action="store_true",
                        help="only run SQL audits, no stress workload")
    args = parser.parse_args()

    if args.audit_only:
        client = SqlClient(args.host, args.port, args.timeout)
        client.connect()
        try:
            errors = audit_district_counters(client)
            errors.extend(audit_new_orders_subset(client))
            if errors:
                for e in errors:
                    print(e)
                return 1
            print("audit-only PASS")
            return 0
        finally:
            client.close()

    return run_stress(args)


if __name__ == "__main__":
    sys.exit(main())
