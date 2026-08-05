#!/usr/bin/env python3
"""Unit tests for the shared RMDB server lifecycle manager."""

import argparse
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from server_manager import RMDBServerManager, add_server_arguments


class ServerArgumentTests(unittest.TestCase):
    def test_shared_arguments_use_build_relative_defaults(self):
        parser = argparse.ArgumentParser()
        add_server_arguments(parser, "smoke_db", "smoke.log")

        args = parser.parse_args([])

        self.assertEqual(args.build_dir, Path("build"))
        self.assertIsNone(args.db_dir)
        self.assertEqual(args.db_name, "smoke_db")
        self.assertIsNone(args.server_log)
        self.assertEqual(args.host, "127.0.0.1")
        self.assertEqual(args.port, 8765)
        self.assertEqual(args.startup_timeout, 10.0)


class ServerLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.build_dir = self.root / "build"
        self.binary = self.build_dir / "bin" / "rmdb"
        self.binary.parent.mkdir(parents=True)
        self.binary.touch()

    def tearDown(self):
        self.temp_dir.cleanup()

    def make_manager(self, **overrides):
        options = {
            "build_dir": self.build_dir,
            "db_dir": None,
            "db_name": "test_db",
            "host": "127.0.0.1",
            "port": 8765,
            "server_log": None,
            "startup_timeout": 10.0,
            "reset_db": True,
        }
        options.update(overrides)
        return RMDBServerManager(**options)

    @mock.patch("server_manager.WireClient")
    @mock.patch("server_manager.subprocess.Popen")
    def test_start_passes_resolved_database_path(self, popen, wire_client):
        process = popen.return_value
        process.poll.return_value = None
        manager = self.make_manager()

        manager.start()

        command = popen.call_args.args[0]
        self.assertEqual(command, [str(self.binary.resolve()),
                                   str((self.build_dir / "test_db").resolve())])
        wire_client.return_value.connect.assert_called_once_with()
        wire_client.return_value.close.assert_called_once_with()
        manager.stop()

    @mock.patch("server_manager.WireClient")
    @mock.patch("server_manager.subprocess.Popen")
    def test_stop_terminates_and_closes_log(self, popen, wire_client):
        process = popen.return_value
        process.poll.return_value = None
        manager = self.make_manager()
        manager.start()
        log_handle = manager.log_handle

        manager.stop()

        process.terminate.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=3)
        self.assertTrue(log_handle.closed)
        self.assertIsNone(manager.process)

    @mock.patch("server_manager.time.sleep")
    @mock.patch("server_manager.time.monotonic", side_effect=[0.0, 0.0, 11.0])
    @mock.patch("server_manager.WireClient")
    @mock.patch("server_manager.subprocess.Popen")
    def test_startup_failure_reports_server_log(self, popen, wire_client,
                                                monotonic, sleep):
        del monotonic, sleep
        process = popen.return_value
        process.poll.return_value = None
        wire_client.return_value.connect.side_effect = OSError("not ready")
        manager = self.make_manager()
        manager.server_log.parent.mkdir(parents=True, exist_ok=True)

        with self.assertRaisesRegex(
                RuntimeError, "(?s)RMDB server startup failed.*not ready"):
            manager.start()

        process.terminate.assert_called_once_with()
        self.assertIsNone(manager.process)

    def test_missing_binary_uses_standard_startup_failure(self):
        self.binary.unlink()
        manager = self.make_manager()

        with self.assertRaisesRegex(
                RuntimeError, "RMDB server startup failed.*cannot find"):
            manager.start()


if __name__ == "__main__":
    unittest.main()
