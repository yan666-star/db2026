#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


REQUIRED_COUNTERS = (
    "abort_insert_records",
    "abort_update_records",
    "abort_delete_records",
    "abort_transactions_with_insert",
    "abort_max_write_set",
    "abort_insert_rollback_us",
    "abort_insert_checkpoint_flush_us",
    "abort_checkpoint_actual",
    "abort_checkpoint_reused",
    "abort_checkpoint_wait_us",
)


def main():
    parser = argparse.ArgumentParser(
        description="Validate generic abort write-set performance diagnostics."
    )
    parser.add_argument("json_path", type=Path)
    args = parser.parse_args()

    payload = json.loads(args.json_path.read_text(encoding="utf-8"))
    if not payload.get("rounds"):
        raise AssertionError("benchmark JSON contains no measured rounds")

    diag = payload["rounds"][-1].get("perf_diag_observed_delta", {})
    missing = [name for name in REQUIRED_COUNTERS if name not in diag]
    if missing:
        raise AssertionError(f"missing abort diagnostic counters: {missing}")

    for name in REQUIRED_COUNTERS:
        if not isinstance(diag[name], int) or diag[name] < 0:
            raise AssertionError(f"{name} must be a non-negative integer")

    if diag["abort_transactions_with_insert"] > diag.get("abort_total", 0):
        raise AssertionError("insert-abort transactions exceed total aborts")
    if diag["abort_insert_records"] and diag["abort_max_write_set"] == 0:
        raise AssertionError("insert rollback records require a non-empty write set")
    checkpoint_requests = diag.get("abort_checkpoint_flush", 0)
    checkpoint_completed = (
        diag["abort_checkpoint_actual"] + diag["abort_checkpoint_reused"]
    )
    checkpoint_sample_gap = checkpoint_requests - checkpoint_completed

    print(
        "abort perf diagnostics passed: "
        f"insert={diag['abort_insert_records']}, "
        f"update={diag['abort_update_records']}, "
        f"delete={diag['abort_delete_records']}, "
        f"insert_txns={diag['abort_transactions_with_insert']}, "
        f"max_write_set={diag['abort_max_write_set']}, "
        f"insert_rollback_us={diag['abort_insert_rollback_us']}, "
        "insert_checkpoint_flush_us="
        f"{diag['abort_insert_checkpoint_flush_us']}, "
        f"checkpoint_actual={diag['abort_checkpoint_actual']}, "
        f"checkpoint_reused={diag['abort_checkpoint_reused']}, "
        f"checkpoint_wait_us={diag['abort_checkpoint_wait_us']}, "
        f"checkpoint_sample_gap={checkpoint_sample_gap}"
    )


if __name__ == "__main__":
    raise SystemExit(main())
