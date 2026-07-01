#!/usr/bin/env python3
import argparse
import socket
from pathlib import Path


class SqlClient:
    def __init__(self, host, port, timeout):
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

    def execute(self, statement):
        if self.sock is None:
            self.connect()
        self.sock.sendall(statement.encode("utf-8") + b"\0")
        data = bytearray()
        while True:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise ConnectionError("server closed the connection")
            data.extend(chunk)
            if b"\0" in chunk:
                break
        return bytes(data).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def parse_rows(output):
    rows = []
    for raw in output.splitlines():
        line = raw.strip()
        if not line.startswith("|") or not line.endswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if not cells or cells[0].lower().startswith("record"):
            continue
        if all(not cell or not cell.lstrip("-").replace(".", "", 1).isdigit() for cell in cells):
            continue
        rows.append(cells)
    return rows


def scalar_int(client, sql):
    rows = parse_rows(client.execute(sql))
    if not rows or not rows[0]:
        return 0
    text = rows[0][0]
    if text == "":
        return 0
    return int(float(text))


def main():
    parser = argparse.ArgumentParser(description="Check TPC-C consistency relations after a performance run.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    client = SqlClient(args.host, args.port, args.timeout)
    mismatches = []
    try:
        districts = parse_rows(client.execute(
            "SELECT d_w_id, d_id, d_next_o_id FROM district ORDER BY d_w_id, d_id;"
        ))
        for row in districts:
            w_id, d_id, d_next = map(lambda x: int(float(x)), row[:3])
            max_o = scalar_int(client, (
                "SELECT MAX(o_id) FROM orders "
                f"WHERE o_w_id={w_id} AND o_d_id={d_id};"
            ))
            max_no = scalar_int(client, (
                "SELECT MAX(no_o_id) FROM new_orders "
                f"WHERE no_w_id={w_id} AND no_d_id={d_id};"
            ))
            min_no = scalar_int(client, (
                "SELECT MIN(no_o_id) FROM new_orders "
                f"WHERE no_w_id={w_id} AND no_d_id={d_id};"
            ))
            count_no = scalar_int(client, (
                "SELECT COUNT(no_o_id) FROM new_orders "
                f"WHERE no_w_id={w_id} AND no_d_id={d_id};"
            ))
            sum_ol_cnt = scalar_int(client, (
                "SELECT SUM(o_ol_cnt) FROM orders "
                f"WHERE o_w_id={w_id} AND o_d_id={d_id};"
            ))
            count_ol = scalar_int(client, (
                "SELECT COUNT(ol_o_id) FROM order_line "
                f"WHERE ol_w_id={w_id} AND ol_d_id={d_id};"
            ))

            if d_next != max_o + 1:
                mismatches.append(
                    f"district/orders w={w_id} d={d_id}: d_next_o_id={d_next}, max_o_id={max_o}"
                )
            if count_no > 0 and max_no - min_no + 1 != count_no:
                mismatches.append(
                    f"new_orders gap w={w_id} d={d_id}: min={min_no}, max={max_no}, count={count_no}"
                )
            if count_no > 0 and max_no != max_o:
                mismatches.append(
                    f"new_orders/orders max w={w_id} d={d_id}: max_no={max_no}, max_o={max_o}"
                )
            if sum_ol_cnt != count_ol:
                mismatches.append(
                    f"orders/order_line w={w_id} d={d_id}: sum_o_ol_cnt={sum_ol_cnt}, count_ol={count_ol}"
                )
    finally:
        client.close()

    if mismatches:
        print("TPC-C consistency mismatches:")
        for item in mismatches:
            print(" - " + item)
        return 1
    print("TPC-C consistency checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
