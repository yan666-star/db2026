#!/usr/bin/env python3
import argparse
import socket
import time


class Client:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)

    def close(self):
        self.sock.close()

    def execute(self, sql):
        self.sock.sendall(sql.encode("utf-8") + b"\0")
        data = bytearray()
        while b"\0" not in data:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed the connection")
            data.extend(chunk)
        return bytes(data).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def timed_query(client, sql):
    start = time.perf_counter()
    result = client.execute(sql)
    return time.perf_counter() - start, result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--rows", type=int, default=2000)
    parser.add_argument("--matches", type=int, default=100)
    args = parser.parse_args()

    if args.matches <= 0 or args.matches >= args.rows:
        parser.error("--matches must be between 1 and --rows - 1")

    client = Client(args.host, args.port, args.timeout)
    try:
        client.execute("CREATE TABLE join_perf_l (id int);")
        client.execute("CREATE TABLE join_perf_r (id int);")
        for value in range(args.rows):
            client.execute(f"INSERT INTO join_perf_l VALUES ({value});")
        right_start = args.rows - args.matches
        for value in range(right_start, right_start + args.rows):
            client.execute(f"INSERT INTO join_perf_r VALUES ({value});")

        query = (
            "SELECT join_perf_l.id, join_perf_r.id "
            "FROM join_perf_l "
            "JOIN join_perf_r ON join_perf_l.id = join_perf_r.id;"
        )
        nlj_time, nlj_result = timed_query(client, query)
        print(client.execute("EXPLAIN ANALYZE " + query))

        client.execute("CREATE INDEX join_perf_r(id);")
        inlj_time, inlj_result = timed_query(client, query)
        print(client.execute("EXPLAIN ANALYZE " + query))

        if nlj_result != inlj_result:
            raise AssertionError("NLJ and INLJ returned different results")

        ratio = inlj_time / nlj_time
        print(f"NLJ={nlj_time:.6f}s INLJ={inlj_time:.6f}s ratio={ratio:.4f}")
        if ratio >= 0.5:
            raise AssertionError("INLJ performance ratio did not meet the < 0.5 requirement")
    finally:
        client.close()


if __name__ == "__main__":
    main()
