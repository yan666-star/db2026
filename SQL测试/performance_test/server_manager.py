#!/usr/bin/env python3
"""Shared lifecycle management for locally launched RMDB servers."""

import argparse
import shutil
import subprocess
import time
from pathlib import Path

from wire_client import WireClient


def add_server_arguments(parser: argparse.ArgumentParser, db_name: str,
                         log_name: str,
                         default_build_dir: Path = Path("build")) -> None:
    """Register the common server lifecycle command-line arguments."""
    parser.add_argument("--start-server", action="store_true",
                        help="Start and stop RMDB around this test")
    parser.add_argument("--build-dir", type=Path,
                        default=default_build_dir)
    parser.add_argument("--db-dir", type=Path,
                        help="Database directory passed to the rmdb process")
    parser.add_argument("--db-name", default=db_name,
                        help=argparse.SUPPRESS)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--server-log", type=Path,
                        help=f"RMDB output log (default: build-dir/{log_name})")
    parser.add_argument("--startup-timeout", type=float, default=10.0)
    parser.add_argument("--reset-db", action="store_true", default=True,
                        help="Remove the build-local test database before start")
    parser.add_argument("--keep-db", action="store_false", dest="reset_db",
                        help="Reuse an existing database directory")


class RMDBServerManager:
    """Own one RMDB process and guarantee bounded startup and shutdown."""

    def __init__(self, *, build_dir: Path, db_dir: Path | None,
                 db_name: str, host: str, port: int,
                 server_log: Path | None, startup_timeout: float,
                 reset_db: bool, log_name: str = "rmdb_server.log"):
        self.build_dir = Path(build_dir).resolve()
        self.db_dir = (Path(db_dir).resolve() if db_dir is not None
                       else (self.build_dir / db_name).resolve())
        self.host = host
        self.port = port
        self.server_log = (Path(server_log).resolve()
                           if server_log is not None
                           else (self.build_dir / log_name).resolve())
        self.startup_timeout = startup_timeout
        self.reset_db = reset_db
        self.process = None
        self.log_handle = None

    @classmethod
    def from_args(cls, args, *, log_name: str):
        return cls(
            build_dir=args.build_dir,
            db_dir=args.db_dir,
            db_name=args.db_name,
            host=args.host,
            port=args.port,
            server_log=args.server_log,
            startup_timeout=args.startup_timeout,
            reset_db=args.reset_db,
            log_name=log_name,
        )

    def _find_rmdb(self) -> Path:
        for name in ("rmdb", "rmdb.exe"):
            candidate = self.build_dir / "bin" / name
            if candidate.is_file():
                return candidate.resolve()
        raise FileNotFoundError(
            f"cannot find RMDB server under {self.build_dir / 'bin'}")

    def _reset_database(self) -> None:
        if not self.reset_db or not self.db_dir.exists():
            return
        if self.db_dir.parent != self.build_dir:
            raise RuntimeError(
                f"refusing to remove database outside build directory: "
                f"{self.db_dir}")
        shutil.rmtree(self.db_dir)

    def _read_server_log(self) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.server_log.exists():
            return "<server log was not created>"
        return self.server_log.read_text(encoding="utf-8", errors="replace")

    def _wait_until_ready(self) -> None:
        deadline = time.monotonic() + self.startup_timeout
        last_error = None
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise RuntimeError(
                    f"server exited with code {self.process.returncode}")
            client = WireClient(self.host, self.port, timeout=2.0)
            try:
                client.connect()
                return
            except Exception as exc:  # readiness polling reports final cause
                last_error = exc
                time.sleep(0.05)
            finally:
                client.close()
        raise TimeoutError(str(last_error or "port did not become ready"))

    def start(self):
        try:
            if self.process is not None:
                raise RuntimeError("RMDB server is already managed")
            if not self.build_dir.is_dir():
                raise FileNotFoundError(
                    f"build directory not found: {self.build_dir}")
            rmdb = self._find_rmdb()
            self._reset_database()
            self.server_log.parent.mkdir(parents=True, exist_ok=True)
            self.log_handle = self.server_log.open(
                "w", encoding="utf-8", errors="replace")
            self.process = subprocess.Popen(
                [str(rmdb), str(self.db_dir)],
                cwd=str(self.build_dir),
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            self._wait_until_ready()
            return self
        except Exception as exc:
            log_text = self._read_server_log()
            self.stop()
            raise RuntimeError(
                f"RMDB server startup failed: {exc}\n"
                f"Server log ({self.server_log}):\n{log_text}") from exc

    def stop(self) -> None:
        process = self.process
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        self.process = None
        if self.log_handle is not None and not self.log_handle.closed:
            self.log_handle.close()

    def kill(self) -> None:
        process = self.process
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        self.process = None
        if self.log_handle is not None and not self.log_handle.closed:
            self.log_handle.close()

    def restart(self, *, reset_db: bool = False):
        self.stop()
        previous_reset = self.reset_db
        self.reset_db = reset_db
        try:
            return self.start()
        finally:
            self.reset_db = previous_reset

    def __enter__(self):
        return self.start()

    def __exit__(self, exc_type, exc, traceback):
        del exc_type, exc, traceback
        self.stop()
