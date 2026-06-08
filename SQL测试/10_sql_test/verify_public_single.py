#!/usr/bin/env python3
import sys
from pathlib import Path


actual = [
    line
    for line in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
    if line.startswith("| ")
]
expected = [
    "| w_id | w_name | w_ytd |",
    "| 1 | wh0001 | 1001.000000 |",
    "| w_id | w_name | w_ytd |",
    "| 10 | wh0010 | 1010.000000 |",
    "| w_id | w_name | w_ytd |",
    "| 20 | wh0020 | 1020.000000 |",
]

if actual != expected:
    print("public single-thread recovery mismatch", file=sys.stderr)
    print(f"expected: {expected!r}", file=sys.stderr)
    print(f"actual:   {actual!r}", file=sys.stderr)
    raise SystemExit(1)

print("public single-thread recovery passed exactly")
