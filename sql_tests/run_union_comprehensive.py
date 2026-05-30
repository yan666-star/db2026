#!/usr/bin/env python3
"""Run comprehensive union tests and verify expected row counts / key values."""
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 8765
ROOT = Path(__file__).resolve().parent.parent
SQL_FILE = ROOT / "sql_tests" / "test_union_comprehensive.sql"
DB = "union_comprehensive_db"

# Each entry: (description, expected_row_count, optional row substring checks)
EXPECTATIONS = [
    ("1. 四分支去重", 3, ["| 1 | 100 |", "| 2 | 200 |", "| 3 | 300 |"]),
    ("2. INT/FLOAT跨类型去重", 4, ["| 3 | 200 |", "| 4 | 150.5 |", "| 2 | 99 |", "| 1 | 50 |"]),
    ("3. CHAR长度提升", 3, ["| 1 | apple |", "| 2 | pear |", "| 3 | watermelon |"]),
    ("4. 多列排序tie-break", 6, None),
    ("5. 顶层三分支UNION", 3, ["| 3 | 3.5 |", "| 2 | 2.5 |", "| 1 | 1.5 |"]),
    ("6. 无AS别名", 2, ["| 20 | beta |", "| 10 | alpha |"]),
    ("7. 限定表名ORDER BY", 3, ["| 2 | 92.5 |", "| 1 | 88 |", "| 3 | 75 |"]),
    ("8. 嵌套派生表", 3, ["| 3 | 30 |", "| 2 | 20 |", "| 1 | 10 |"]),
    ("9. 类型提升SELECT*", 3, None),
    ("10. 大量重复去重", 2, ["| 1 |", "| 2 |"]),
    ("11. FLOAT格式", 5, ["| 1 | 560 |", "| 5 | 199.99 |", "| 2 | 230.5 |"]),
    ("12. 顶层INT/FLOAT", 3, ["| 1 | 100 |", "| 2 | 200 |", "| 3 | 300.5 |"]),
    ("13. 6.1变体三表", 4, ["| 2 | 230.5 |", "| 6 | 199.99 |", "| 1 | 150 |"]),
    ("14. 默认ASC排序", 3, ["| 1 | aaa |", "| 2 | bbb |", "| 3 | ccc |"]),
    ("15. 重复值去重", 2, ["| 5 |", "| 6 |"]),
]


def send_sql(sql: str) -> str:
    sql = sql.strip()
    if not sql.endswith(";"):
        sql += ";"
    with socket.create_connection((HOST, PORT), timeout=10) as s:
        s.sendall(sql.encode() + b"\0")
        data = b""
        while True:
            chunk = s.recv(8192)
            if not chunk:
                break
            data += chunk
            if b"\0" in chunk:
                break
        return data.split(b"\0", 1)[0].decode(errors="replace")


def parse_output_blocks(text: str):
    """Split output.txt into blocks (one per SELECT)."""
    lines = [ln for ln in text.strip().split("\n") if ln.strip()]
    blocks = []
    current = []
    for ln in lines:
        if ln.startswith("|") and current and not ln.startswith(current[0].split("|")[0] + "|"):
            pass
        if ln.startswith("|") and " |" in ln:
            if current and ln == current[0]:
                # new header -> new block
                if len(current) > 1:
                    blocks.append(current)
                current = [ln]
            elif not current:
                current = [ln]
            else:
                current.append(ln)
        elif ln == "failure":
            blocks.append([ln])
    if current:
        blocks.append(current)
    return blocks


def split_output_by_selects(raw: str):
    """Split on header lines that look like column headers."""
    lines = raw.strip().split("\n")
    blocks = []
    cur = []
    for ln in lines:
        if not ln.strip():
            continue
        if ln.strip() == "failure":
            blocks.append([ln.strip()])
            cur = []
            continue
        if re.match(r"^\| \w", ln) and cur and cur[0].startswith("|") and ln != cur[0]:
            # check if this is a header (next line after header pattern)
            prev_is_header = len(cur) == 1 or (len(cur) >= 1 and cur[0].count("|") == ln.count("|"))
            if len(cur) >= 1 and cur[0].count("|") == ln.count("|") and not any(c.isdigit() for c in ln.split("|")[1].strip()[:3] if ln.split("|")[1].strip()):
                # likely new header with same column count
                if len(cur) > 1:
                    blocks.append(cur)
                cur = [ln]
                continue
        if not cur:
            cur = [ln]
        else:
            # detect new result set: header line after data lines
            if ln.startswith("|") and cur and not ln.startswith("| " + cur[0].split("|")[1].strip()):
                parts_ln = [p.strip() for p in ln.split("|") if p.strip()]
                parts_hdr = [p.strip() for p in cur[0].split("|") if p.strip()]
                if parts_ln == parts_hdr and len(cur) > 1:
                    blocks.append(cur)
                    cur = [ln]
                    continue
            cur.append(ln)
    if cur:
        blocks.append(cur)
    return blocks


def extract_data_rows(block):
    if not block:
        return []
    if block[0].strip() == "failure":
        return block
    if len(block) <= 1:
        return []
    return block[1:]


def main():
    subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stderr=subprocess.DEVNULL)
    time.sleep(1)
    db_path = ROOT / DB
    if db_path.exists():
        import shutil
        shutil.rmtree(db_path)
    proc = subprocess.Popen(
        [str(ROOT / "build" / "bin" / "rmdb"), DB],
        cwd=str(ROOT),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(2)
    if proc.poll() is not None:
        print("FAIL: server did not start")
        sys.exit(1)

    # run SQL file statement by statement
    select_count = 0
    with open(SQL_FILE) as f:
        buf = ""
        for line in f:
            line = line.strip()
            if not line or line.startswith("--"):
                continue
            buf += line + " "
            if ";" in line:
                stmt = buf.strip()
                buf = ""
                if stmt.upper().startswith("SELECT"):
                    try:
                        send_sql(stmt)
                    except Exception as e:
                        print(f"ERROR sending SQL: {e}")
                        proc.kill()
                        sys.exit(1)
                    select_count += 1
                else:
                    try:
                        send_sql(stmt)
                    except Exception as e:
                        print(f"ERROR setup: {e}")
                        proc.kill()
                        sys.exit(1)
                time.sleep(0.02)

    proc.kill()
    out_file = db_path / "output.txt"
    if not out_file.exists():
        print("FAIL: no output.txt")
        sys.exit(1)

    raw = out_file.read_text()
    # simple split: every header starts a block
    blocks = []
    cur = []
    for ln in raw.strip().split("\n"):
        ln = ln.rstrip()
        if not ln:
            continue
        if ln == "failure":
            blocks.append([ln])
            cur = []
            continue
        if re.match(r"^\| [a-zA-Z_]", ln):
            if cur and len(cur) > 1:
                blocks.append(cur)
            cur = [ln]
        else:
            if cur:
                cur.append(ln)
    if cur:
        blocks.append(cur)

    print(f"Ran {select_count} SELECT statements, got {len(blocks)} output blocks\n")
    passed = 0
    failed = 0
    for i, (desc, exp_rows, checks) in enumerate(EXPECTATIONS):
        if i >= len(blocks):
            print(f"FAIL [{desc}]: missing output block")
            failed += 1
            continue
        block = blocks[i]
        if block[0] == "failure":
            print(f"FAIL [{desc}]: unexpected failure")
            failed += 1
            continue
        data = extract_data_rows(block)
        ok = len(data) == exp_rows
        status = "PASS" if ok else "FAIL"
        if not ok:
            failed += 1
            print(f"{status} [{desc}]: expected {exp_rows} rows, got {len(data)}")
            for r in data:
                print(f"    {r}")
        else:
            sub_ok = True
            if checks:
                joined = "\n".join(data)
                for c in checks:
                    if c not in joined:
                        sub_ok = False
                        print(f"FAIL [{desc}]: missing expected row fragment: {c!r}")
                        failed += 1
                        ok = False
                        break
            if ok and sub_ok:
                passed += 1
                print(f"PASS [{desc}]: {exp_rows} rows")
                for r in data[:5]:
                    print(f"    {r}")
                if len(data) > 5:
                    print(f"    ... ({len(data)-5} more)")

    print(f"\n{'='*50}")
    print(f"Total: {passed} passed, {failed} failed, {len(EXPECTATIONS)} cases")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
