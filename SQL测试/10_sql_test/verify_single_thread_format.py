#!/usr/bin/env python3
import sys
from pathlib import Path


EXPECTED = (
    "| id | amount | note |\n"
    "| 1 | 1.250000 | auto |\n"
    "| 2 | 22.125000 | tail   |\n"
    "| 4 | 4.000001 | keep |\n"
)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: verify_single_thread_format.py DATABASE_DIR"
        )

    output = Path(sys.argv[1]) / "output.txt"
    if not output.is_file():
        raise SystemExit(f"missing output file: {output}")

    actual = output.read_text(encoding="utf-8")
    if actual != EXPECTED:
        raise SystemExit(
            "single-thread output mismatch\n"
            f"expected bytes: {EXPECTED.encode()!r}\n"
            f"actual bytes:   {actual.encode()!r}"
        )

    print("single-thread data and exact output format passed")


if __name__ == "__main__":
    main()
