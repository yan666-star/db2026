#!/usr/bin/env python3
import socket
import time


class Client:
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", 8765), timeout=5.0)
        self.sock.settimeout(5.0)

    def execute(self, sql):
        payload = sql[:-1].strip() if sql.lower() == "crash;" else sql
        self.sock.sendall(payload.encode("utf-8") + b"\0")
        if payload == "crash":
            return
        data = bytearray()
        while b"\0" not in data:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise RuntimeError(f"connection closed during: {sql}")
            data.extend(chunk)

    def close(self):
        self.sock.close()


main = Client()
main.execute("create table warehouse (w_id int, w_name char(10), w_ytd float);")
for key in range(1, 21):
    main.execute(
        f"insert into warehouse values "
        f"({key}, 'wh{key % 10000:04d}', {float(key):.6f});"
    )

worker = Client()
for key in range(1, 21):
    worker.execute("begin;")
    worker.execute(
        f"update warehouse set w_ytd={1000.0 + key:.6f} "
        f"where w_id={key};"
    )
    worker.execute("commit;")
worker.close()

# Let the server-side worker thread observe EOF and run its connection cleanup.
time.sleep(0.2)
main.execute("crash;")
main.close()
