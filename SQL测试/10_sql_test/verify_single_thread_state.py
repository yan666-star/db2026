#!/usr/bin/env python3
import sys
from pathlib import Path


EXPECTED = (
    "| id | value | note |\n"
    "| 1 | 12.500000 | one-ok |\n"
    "| 7 | 7.250000 | seven |\n"
    "| 3 | 33.500000 | three |\n"
    "| id | value | note |\n"
    "| 1 | 12.500000 | one-ok |\n"
    "| 3 | 33.500000 | three |\n"
    "| 7 | 7.250000 | seven |\n"
)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_single_thread_state.py DATABASE_DIR")

    output = Path(sys.argv[1]) / "output.txt"
    if not output.is_file():
        raise SystemExit(f"missing output file: {output}")

    actual = output.read_text(encoding="utf-8")
    if actual != EXPECTED:
        raise SystemExit(
            "single-thread state mismatch\n"
            f"expected bytes: {EXPECTED.encode()!r}\n"
            f"actual bytes:   {actual.encode()!r}"
        )

    print("single-thread transaction state and RID order passed")


if __name__ == "__main__":
    main()
