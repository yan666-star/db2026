#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


CHECKPOINT_LOG_TYPE = 6
LOG_HEADER = struct.Struct("=iiIqi")
COUNT_FIELD = struct.Struct("=I")


def main():
    parser = argparse.ArgumentParser(description="Verify an RMDB static checkpoint on disk.")
    parser.add_argument("database_dir", type=Path)
    args = parser.parse_args()

    restart_path = args.database_dir / "db.restart"
    log_path = args.database_dir / "db.log"

    if not restart_path.is_file():
        raise SystemExit(f"missing restart file: {restart_path}")
    if not log_path.is_file():
        raise SystemExit(f"missing log file: {log_path}")

    restart_data = restart_path.read_bytes()
    if len(restart_data) != 8:
        raise SystemExit(f"invalid restart file length: {len(restart_data)}")
    checkpoint_offset = struct.unpack("=q", restart_data)[0]

    log_data = log_path.read_bytes()
    end = checkpoint_offset + LOG_HEADER.size + COUNT_FIELD.size
    if checkpoint_offset < 0 or end > len(log_data):
        raise SystemExit(
            f"checkpoint offset {checkpoint_offset} is outside db.log ({len(log_data)} bytes)"
        )

    log_type, lsn, total_len, txn_id, prev_lsn = LOG_HEADER.unpack_from(
        log_data, checkpoint_offset
    )
    if log_type != CHECKPOINT_LOG_TYPE:
        raise SystemExit(
            f"log record at offset {checkpoint_offset} has type {log_type}, expected 6"
        )
    active_count = COUNT_FIELD.unpack_from(
        log_data, checkpoint_offset + LOG_HEADER.size
    )[0]
    expected_len = LOG_HEADER.size + COUNT_FIELD.size + active_count * 8
    if total_len != expected_len:
        raise SystemExit(
            f"checkpoint record length is {total_len}, expected {expected_len}"
        )
    if checkpoint_offset + total_len > len(log_data):
        raise SystemExit("checkpoint record extends beyond the end of db.log")

    print(
        "checkpoint valid: "
        f"offset={checkpoint_offset}, lsn={lsn}, active_txns={active_count}, "
        f"txn_id={txn_id}, prev_lsn={prev_lsn}"
    )


if __name__ == "__main__":
    main()
