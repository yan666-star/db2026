#!/usr/bin/env python3
import argparse
import shutil
import socket
import subprocess
import threading
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent.parent
SERVER = ROOT / "build" / "bin" / "rmdb"
PORT = 8765


class Client:
    def __init__(self, timeout=20.0):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
        self.sock.settimeout(timeout)

    def execute(self, statement):
        payload = statement[:-1].strip() if statement.lower() == "crash;" else statement
        self.sock.sendall(payload.encode() + b"\0")
        if payload == "crash":
            return
        data = self.sock.recv(1024 * 1024)
        if not data:
            raise RuntimeError(f"server closed connection while handling: {statement}")

    def close(self):
        self.sock.close()


def wait_ready(proc, timeout=30.0):
    start = time.perf_counter()
    deadline = start + timeout
    while time.perf_counter() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"server exited with code {proc.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", PORT), timeout=0.1):
                return time.perf_counter() - start
        except OSError:
            time.sleep(0.02)
    raise RuntimeError("server did not become ready")


def start_server(db_name, log_path):
    log_file = log_path.open("w")
    proc = subprocess.Popen(
        [str(SERVER), db_name],
        cwd=ROOT,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    proc.log_file = log_file
    wait_ready(proc)
    return proc


def stop_server(proc):
    if proc.poll() is None:
        proc.kill()
    proc.wait(timeout=5)
    proc.log_file.close()


def server_rss_kb(proc):
    try:
        for line in Path(f"/proc/{proc.pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError):
        pass
    return -1


def run_trial(rows, use_checkpoint, workers):
    label = "with_checkpoint" if use_checkpoint else "without_checkpoint"
    db_name = f"large_recovery_{label}_db"
    db_path = ROOT / db_name
    log_path = Path("/tmp") / f"rmdb_{label}.log"
    shutil.rmtree(db_path, ignore_errors=True)

    proc = start_server(db_name, log_path)
    client = Client()
    try:
        normal_start = time.perf_counter()
        client.execute("create table warehouse (w_id int, w_name char(10), w_ytd float);")
        checkpoint_step = max(1, rows // 4)
        for key in range(1, rows + 1):
            client.execute(
                f"insert into warehouse values "
                f"({key}, 'wh{key % 10000:04d}', {float(key):.6f});"
            )
            if use_checkpoint and key % checkpoint_step == 0:
                client.execute("create static_checkpoint;")
        insert_time = time.perf_counter() - normal_start
        insert_rss = server_rss_kb(proc)

        errors = []

        def update_worker(worker_id):
            worker = Client()
            try:
                for key in range(worker_id + 1, rows + 1, workers):
                    worker.execute("begin;")
                    worker.execute(
                        f"update warehouse set w_ytd={1000.0 + key:.6f} "
                        f"where w_id={key};"
                    )
                    worker.execute("commit;")
            except Exception as exc:
                errors.append(exc)
            finally:
                worker.close()

        update_start = time.perf_counter()
        threads = [
            threading.Thread(target=update_worker, args=(worker_id,))
            for worker_id in range(workers)
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        if errors:
            raise errors[0]
        update_time = time.perf_counter() - update_start
        update_rss = server_rss_kb(proc)

        if use_checkpoint:
            client.execute("create static_checkpoint;")
        normal_time = time.perf_counter() - normal_start
        print(
            f"{label}: normal={normal_time:.3f}s "
            f"(insert={insert_time:.3f}s/{insert_rss}KB, "
            f"update={update_time:.3f}s/{update_rss}KB)"
        )
        client.execute("crash;")
        client.close()
        proc.wait(timeout=10)
        proc.log_file.close()
    except Exception:
        client.close()
        stop_server(proc)
        print(log_path.read_text(errors="replace"))
        raise

    restart_log = Path("/tmp") / f"rmdb_{label}_restart.log"
    log_file = restart_log.open("w")
    start = time.perf_counter()
    proc = subprocess.Popen(
        [str(SERVER), db_name],
        cwd=ROOT,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    proc.log_file = log_file
    recovery_time = wait_ready(proc)

    verifier = Client()
    for key in sorted({1, max(1, rows // 2), rows}):
        verifier.execute(f"select * from warehouse where w_id={key};")
    verifier.close()
    stop_server(proc)

    output = (db_path / "output.txt").read_text(errors="replace")
    for key in sorted({1, max(1, rows // 2), rows}):
        expected = f"| {key} | wh{key % 10000:04d} | {1000.0 + key:.6f} |"
        if expected not in output:
            raise RuntimeError(f"missing recovered row: {expected}")

    print(f"{label}: rows={rows}, recovery={recovery_time:.3f}s, passed")
    return recovery_time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rows", type=int, default=5000)
    parser.add_argument("--workers", type=int, default=1)
    args = parser.parse_args()

    subprocess.run(["pkill", "-9", "-x", "rmdb"], check=False)
    t1 = run_trial(args.rows, False, args.workers)
    t2 = run_trial(args.rows, True, args.workers)
    print(f"t1={t1:.3f}s, t2={t2:.3f}s, ratio={t2 / t1 if t1 else 0:.3f}")


if __name__ == "__main__":
    main()
