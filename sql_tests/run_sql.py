#!/usr/bin/env python3
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 8765

def send_sql(sql: str) -> str:
    sql = sql.strip()
    if not sql.endswith(";"):
        sql += ";"
    with socket.create_connection((HOST, PORT), timeout=5) as s:
        s.sendall(sql.encode() + b"\0")
        data = b""
        while True:
            chunk = s.recv(8192)
            if not chunk:
                break
            data += chunk
            if b"\0" in chunk:
                break
        return data.split(b"\0", 1)[0].decode(errors="replace")

def run_file(path: str):
    with open(path) as f:
        buf = ""
        for line in f:
            line = line.strip()
            if not line or line.startswith("--"):
                continue
            buf += line + " "
            if ";" in line:
                stmt = buf.strip()
                buf = ""
                print(f">>> {stmt}")
                try:
                    resp = send_sql(stmt)
                    print(resp)
                except Exception as e:
                    print(f"ERROR: {e}")
                print("---")
                time.sleep(0.05)

if __name__ == "__main__":
    run_file(sys.argv[1])
