#!/usr/bin/env python3
import sys
from pathlib import Path


def rows(path):
    result = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.startswith("| "):
            continue
        result.append([part.strip() for part in line.strip("|").split("|")])
    return result


actual = rows(sys.argv[1])
expected = [
    ["d_id", "d_w_id", "d_next_o_id"],
    ["1", "1", "6"],
    ["s_i_id", "s_w_id", "s_quantity"],
    ["10", "1", "8"],
    ["o_id"],
    ["4"],
    ["5"],
    ["no_o_id"],
    ["4"],
    ["5"],
    ["ol_o_id", "ol_quantity", "ol_amount"],
    ["4", "7", "286.625000"],
    ["5", "8", "327.600006"],
]

if actual != expected:
    print("statement TPCC recovery mismatch", file=sys.stderr)
    print(f"expected: {expected!r}", file=sys.stderr)
    print(f"actual:   {actual!r}", file=sys.stderr)
    raise SystemExit(1)

print("statement TPCC transaction passed")
