#!/usr/bin/env python3
import sys
from pathlib import Path


lines = Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
rows = []
for line in lines:
    if not line.startswith("| "):
        continue
    values = [part.strip() for part in line.strip("|").split("|")]
    if values == ["id", "value"]:
        continue
    rows.append(values)

expected_ids = [str(i) for i in range(1, 36) if i not in {7, 8}]
actual_ids = [row[0] for row in rows]
if actual_ids != expected_ids:
    print("multipage RID recovery mismatch", file=sys.stderr)
    print(f"expected ids: {expected_ids!r}", file=sys.stderr)
    print(f"actual ids:   {actual_ids!r}", file=sys.stderr)
    raise SystemExit(1)

values = {row[0]: row[1] for row in rows}
if values.get("5") != "55.500000":
    print(f"committed update lost: id=5 value={values.get('5')!r}", file=sys.stderr)
    raise SystemExit(1)

print("single-thread multipage recovery passed")
