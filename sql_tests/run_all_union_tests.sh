#!/usr/bin/env python3
"""Run all union test suites and summarize results."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN_ONE = ROOT / "sql_tests" / "run_one_test.sh"
SUITES = [
    ("题面 6.1", "union_test_db", "sql_tests/test_union.sql"),
    ("题面 6.2", "union_test_db62", "sql_tests/test_union_62.sql"),
    ("去重+顶层UNION", "union_extra_db", "sql_tests/test_union_extra.sql"),
    ("变体场景", "union_var_db", "sql_tests/test_union_variants.sql"),
    ("ASC排序", "union_asc_db", "sql_tests/test_union_asc.sql"),
    ("综合15例", "union_comprehensive_db", "sql_tests/test_union_comprehensive.sql"),
    ("错误+边界", "union_errors_db", "sql_tests/test_union_errors.sql"),
    ("扩展12例", "union_extended_db", "sql_tests/test_union_extended.sql"),
    ("普通派生表", "derived_simple_db", "sql_tests/test_derived_simple.sql"),
]


def count_blocks(text: str) -> tuple[int, int, int]:
    lines = [ln.strip() for ln in text.strip().split("\n") if ln.strip()]
    failures = sum(1 for ln in lines if ln == "failure")
    headers = sum(1 for ln in lines if ln.startswith("|") and any(c.isalpha() for c in ln))
    # approximate result sets: count header lines that look like column headers
    blocks = 0
    prev = None
    for ln in lines:
        if ln == "failure":
            blocks += 1
            prev = None
            continue
        if ln.startswith("|"):
            parts = [p.strip() for p in ln.split("|") if p.strip()]
            if parts and all(not p.replace(".", "").replace("-", "").isdigit() for p in parts[:2]):
                if prev and ln == prev:
                    continue
                if prev is None or (prev and ln != prev and not ln[2:3].isdigit()):
                    # crude: new header if all parts are identifiers
                    if all(p[0].isalpha() or p == "k" for p in parts if p):
                        blocks += 1
                prev = ln
    return len(lines), failures, max(blocks, failures + (1 if headers else 0))


def main():
    print("Union 全量测试\n" + "=" * 60)
    ok = 0
    for name, db, sql in SUITES:
        sql_path = ROOT / sql
        if not sql_path.exists():
            print(f"SKIP {name}: {sql} not found")
            continue
        r = subprocess.run([str(RUN_ONE), db, str(sql_path)], capture_output=True, text=True)
        out = r.stdout
        out_file = ROOT / db / "output.txt"
        if out_file.exists():
            raw = out_file.read_text()
            lines, fails, _ = count_blocks(raw)
            selects = raw.count("\n| ")  # rough
            print(f"\n[{name}] db={db}")
            print(f"  输出行数: {lines}, failure: {fails}")
            # show last result block preview
            blocks_raw = raw.strip().split("\n")
            preview = blocks_raw[-min(8, len(blocks_raw)):]
            for ln in preview:
                print(f"  {ln}")
            ok += 1
        else:
            print(f"FAIL [{name}]: no output.txt")

    print("\n" + "=" * 60)
    print(f"完成 {ok}/{len(SUITES)} 套测试")
    # run comprehensive verifier
    print("\n--- 综合用例自动校验 ---")
    r = subprocess.run([sys.executable, str(ROOT / "sql_tests" / "run_union_comprehensive.py")],
                       cwd=str(ROOT))
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
