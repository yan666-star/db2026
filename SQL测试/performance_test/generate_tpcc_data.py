#!/usr/bin/env python3
"""TPC-C dataset generator for RMDB finals performance testing.

Generates 9 CSV files matching the competition finals specification:
  https://...2026年全国大学生计算机系统能力大赛数据库系统设计赛-决赛赛题.md

Key rules (from spec §三-(三)):
  - order_line.ol_quantity = 5 (fixed)
  - Undelivered order_line.ol_amount: random cents [1, 999999]/100 → binary32
  - Delivered order_line.ol_amount: 0
  - orders.o_ol_cnt must equal COUNT(order_line) per order
  - stock.s_quantity ∈ [10, 100]
  - Initial s_ytd = s_order_cnt = s_remote_cnt = 0
  - FLOAT values use IEEE-754 binary32, round-to-nearest ties-to-even

Scale modes: mini | small | medium | full

Usage:
  python3 generate_tpcc_data.py --scale full
  python3 generate_tpcc_data.py --scale medium --output-dir /path/to/data
"""

import argparse
import csv
import os
import random
import struct
import sys
import time
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_OUTPUT_DIR = (
    REPO_ROOT / "src" / "test" / "performance_test" / "table_data")

# Deterministic base seed (competition year + month)
BASE_SEED = 20260710


# ── Scale presets ──────────────────────────────────────────────────────────

SCALES = {
    "mini":   {"warehouses": 1,  "items": 10,    "cust_per_dist": 10},
    "small":  {"warehouses": 5,  "items": 1000,  "cust_per_dist": 300},
    "medium": {"warehouses": 10, "items": 10000,  "cust_per_dist": 300},
    "full":   {"warehouses": 50, "items": 100000, "cust_per_dist": 3000},
}

DISTRICTS_PER_WAREHOUSE = 10
DELIVERED_CUTOFF_RATIO = 0.7  # first 70% orders delivered, last 30% undelivered


# ── FLOAT32 helpers (competition spec §三-(六) FLOAT rules) ────────────────

def to_binary32(value: float) -> float:
    """Round-trip a float through IEEE-754 binary32 (round-to-nearest,
    ties-to-even). This ensures the written CSV value exactly matches what
    the server will store."""
    return struct.unpack('>f', struct.pack('>f', value))[0]


def fmt_float(value: float) -> str:
    """Format a FLOAT32 value with sufficient precision for exact
    binary32 round-tripping.  Uses repr() which produces the shortest
    decimal that round-trips to the same binary32."""
    return repr(to_binary32(value))


def random_amount(rng: random.Random) -> str:
    """Generate ol_amount per spec: random cents [1, 999999]/100 → binary32."""
    cents = rng.randint(1, 999999)
    return fmt_float(cents / 100.0)


# ── CHAR string generators ─────────────────────────────────────────────────

# Characters used: alphanumeric, mimicking the existing mini dataset style
ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"


def rand_str(rng: random.Random, length: int) -> str:
    """Random alphanumeric string of exact length."""
    return "".join(rng.choice(ALPHA) for _ in range(length))


def rand_digits(rng: random.Random, length: int) -> str:
    """Random digit string of exact length."""
    return "".join(str(rng.randint(0, 9)) for _ in range(length))


def rand_upper(rng: random.Random, length: int) -> str:
    """Random uppercase letters."""
    return "".join(rng.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
                   for _ in range(length))


# ── CSV writer with progress ───────────────────────────────────────────────

class TableWriter:
    """Stream CSV rows to a file with progress reporting."""

    def __init__(self, path: Path, columns: list, total_rows: int,
                 label: str = ""):
        self.path = path
        self.total = total_rows
        self.label = label or path.name
        self._file = path.open("w", encoding="utf-8", newline="")
        self._writer = csv.writer(self._file, lineterminator="\n")
        self._writer.writerow(columns)
        self._count = 0
        self._start = time.monotonic()
        self._last_report = 0

    def row(self, values: list):
        self._writer.writerow(values)
        self._count += 1
        if self._count - self._last_report >= 50000:
            self._report()

    def close(self):
        self._file.close()
        elapsed = time.monotonic() - self._start
        print(f"  {self.label}: {self._count} rows "
              f"({elapsed:.1f}s)")

    def _report(self):
        elapsed = time.monotonic() - self._start
        pct = self._count * 100.0 / self.total if self.total else 0
        rate = self._count / elapsed if elapsed > 0 else 0
        print(f"  {self.label}: {self._count}/{self.total} "
              f"({pct:.0f}%, {rate:.0f} row/s)")
        self._last_report = self._count


# ── Table generators ────────────────────────────────────────────────────────

def generate_warehouse(outdir: Path, scale: dict, rng: random.Random):
    n = scale["warehouses"]
    path = outdir / "warehouse.csv"
    w = TableWriter(path,
                    ["w_id", "w_name", "w_street_1", "w_street_2",
                     "w_city", "w_state", "w_zip", "w_tax", "w_ytd"],
                    n)
    for w_id in range(1, n + 1):
        w.row([str(w_id),
               rand_str(rng, 10),
               rand_str(rng, 20),
               rand_str(rng, 20),
               rand_str(rng, 20),
               rand_upper(rng, 2),
               rand_digits(rng, 9),
               fmt_float(rng.uniform(0.01, 0.25)),
               fmt_float(3000.5)])
    w.close()


def generate_district(outdir: Path, scale: dict, rng: random.Random):
    n_wh = scale["warehouses"]
    n = n_wh * DISTRICTS_PER_WAREHOUSE
    path = outdir / "district.csv"
    w = TableWriter(path,
                    ["d_id", "d_w_id", "d_name", "d_street_1",
                     "d_street_2", "d_city", "d_state", "d_zip",
                     "d_tax", "d_ytd", "d_next_o_id"],
                    n)
    for w_id in range(1, n_wh + 1):
        for d_id in range(1, DISTRICTS_PER_WAREHOUSE + 1):
            w.row([str(d_id),
                   str(w_id),
                   rand_str(rng, 10),
                   rand_str(rng, 20),
                   rand_str(rng, 20),
                   rand_str(rng, 20),
                   rand_upper(rng, 2),
                   rand_digits(rng, 9),
                   fmt_float(rng.uniform(0.01, 0.25)),
                   fmt_float(30000.5),
                   "11"])
    w.close()


def generate_item(outdir: Path, scale: dict, rng: random.Random):
    n = scale["items"]
    path = outdir / "item.csv"
    w = TableWriter(path,
                    ["i_id", "i_im_id", "i_name", "i_price", "i_data"],
                    n)
    for i_id in range(1, n + 1):
        w.row([str(i_id),
               str(rng.randint(1, max(1, n // 10))),
               rand_str(rng, 24),
               fmt_float(rng.uniform(1.0, 500.0)),
               rand_str(rng, 50)])
    w.close()


def generate_customer(outdir: Path, scale: dict, rng: random.Random):
    n_wh = scale["warehouses"]
    cpd = scale["cust_per_dist"]
    total = n_wh * DISTRICTS_PER_WAREHOUSE * cpd
    path = outdir / "customer.csv"
    w = TableWriter(path,
                    ["c_id", "c_d_id", "c_w_id", "c_first", "c_middle",
                     "c_last", "c_street_1", "c_street_2", "c_city",
                     "c_state", "c_zip", "c_phone", "c_since",
                     "c_credit", "c_credit_lim", "c_discount",
                     "c_balance", "c_ytd_payment", "c_payment_cnt",
                     "c_delivery_cnt", "c_data"],
                    total)
    # Pre-generate a pool of last names for Payment surname lookups
    last_names = [rand_str(rng, 16) for _ in range(min(1000, cpd * 3))]

    for w_id in range(1, n_wh + 1):
        for d_id in range(1, DISTRICTS_PER_WAREHOUSE + 1):
            for c_id in range(1, cpd + 1):
                w.row([str(c_id),
                       str(d_id),
                       str(w_id),
                       rand_str(rng, 16),
                       "OE",
                       rng.choice(last_names),
                       rand_str(rng, 20),
                       rand_str(rng, 20),
                       rand_str(rng, 20),
                       rand_upper(rng, 2),
                       rand_digits(rng, 9),
                       rand_digits(rng, 16),
                       "2023-07-22 20:50:31",
                       "GC" if rng.random() < 0.9 else "BC",
                       "50000",
                       fmt_float(rng.uniform(0.0, 10.0)),
                       fmt_float(10.5),
                       fmt_float(10.5),
                       "1",
                       "0",
                       rand_str(rng, 50)])
    w.close()


def generate_history(outdir: Path, scale: dict, rng: random.Random):
    """1 history row per customer, matching the customer's key."""
    n_wh = scale["warehouses"]
    cpd = scale["cust_per_dist"]
    total = n_wh * DISTRICTS_PER_WAREHOUSE * cpd
    path = outdir / "history.csv"
    w = TableWriter(path,
                    ["h_c_id", "h_c_d_id", "h_c_w_id",
                     "h_d_id", "h_w_id", "h_date", "h_amount", "h_data"],
                    total)
    for w_id in range(1, n_wh + 1):
        for d_id in range(1, DISTRICTS_PER_WAREHOUSE + 1):
            for c_id in range(1, cpd + 1):
                w.row([str(c_id),
                       str(d_id),
                       str(w_id),
                       str(d_id),
                       str(w_id),
                       "2023-07-22 20:50:31",
                       fmt_float(10.5),
                       rand_str(rng, 24)])
    w.close()


def generate_orders_and_lines(outdir: Path, scale: dict, rng: random.Random):
    """Generate orders, new_orders, and order_line CSVs together.

    This is the core of the dataset.  We generate orders sequentially
    per district.  For each order we:
      1. Pick o_ol_cnt ∈ [5, 15]
      2. Generate that many order_line rows
      3. Write the order row with the correct o_ol_cnt
      4. Write new_orders row if the order is undelivered

    FK consistency:
      - o_c_id ∈ [1, cust_per_dist] (valid FK to customer)
      - ol_i_id ∈ [1, items] (valid FK to item)
      - ol_supply_w_id: 93% same warehouse, 7% remote
    """
    n_wh = scale["warehouses"]
    cpd = scale["cust_per_dist"]
    n_items = scale["items"]
    districts = n_wh * DISTRICTS_PER_WAREHOUSE
    orders_per_dist = cpd  # 1 order per customer initially
    delivered_cutoff = int(orders_per_dist * DELIVERED_CUTOFF_RATIO)
    total_orders = districts * orders_per_dist

    # Estimate order_line count for progress bar
    est_lines = total_orders * 10  # average of [5,15]
    est_new_orders = districts * (orders_per_dist - delivered_cutoff)

    ord_path = outdir / "orders.csv"
    nw_path = outdir / "new_orders.csv"
    ol_path = outdir / "order_line.csv"

    ord_w = TableWriter(ord_path,
                        ["o_id", "o_d_id", "o_w_id", "o_c_id",
                         "o_entry_d", "o_carrier_id", "o_ol_cnt",
                         "o_all_local"],
                        total_orders, "orders")
    nw_w = TableWriter(nw_path,
                       ["no_o_id", "no_d_id", "no_w_id"],
                       est_new_orders, "new_orders")
    ol_w = TableWriter(ol_path,
                       ["ol_o_id", "ol_d_id", "ol_w_id", "ol_number",
                        "ol_i_id", "ol_supply_w_id", "ol_delivery_d",
                        "ol_quantity", "ol_amount", "ol_dist_info"],
                       est_lines, "order_line")

    # Pre-generate timestamps for variety
    ts_base = "2023-07-22 20:50:31"

    for w_id in range(1, n_wh + 1):
        other_wh = [x for x in range(1, n_wh + 1) if x != w_id]
        for d_id in range(1, DISTRICTS_PER_WAREHOUSE + 1):
            for o_id in range(1, orders_per_dist + 1):
                c_id = rng.randint(1, cpd)
                delivered = o_id <= delivered_cutoff
                carrier = (rng.randint(1, 10) if delivered else 0)
                ol_cnt = rng.randint(5, 15)

                # Write order_line rows
                for line_no in range(1, ol_cnt + 1):
                    supply_wh = (w_id if rng.random() < 0.93
                                 else rng.choice(other_wh))
                    amount = ("0.0" if delivered
                              else random_amount(rng))
                    ol_w.row([str(o_id),
                              str(d_id),
                              str(w_id),
                              str(line_no),
                              str(rng.randint(1, n_items)),
                              str(supply_wh),
                              ts_base if delivered else "",
                              "5",       # ol_quantity fixed per spec
                              amount,
                              rand_str(rng, 24)])

                # Write orders row with correct o_ol_cnt
                ord_w.row([str(o_id),
                           str(d_id),
                           str(w_id),
                           str(c_id),
                           ts_base,
                           str(carrier),
                           str(ol_cnt),
                           "1"])

                # Write new_orders row if undelivered
                if not delivered:
                    nw_w.row([str(o_id), str(d_id), str(w_id)])

    ord_w.close()
    nw_w.close()
    ol_w.close()


def generate_stock(outdir: Path, scale: dict, rng: random.Random):
    n_items = scale["items"]
    n_wh = scale["warehouses"]
    total = n_items * n_wh
    path = outdir / "stock.csv"
    w = TableWriter(path,
                    ["s_i_id", "s_w_id", "s_quantity",
                     "s_dist_01", "s_dist_02", "s_dist_03",
                     "s_dist_04", "s_dist_05", "s_dist_06",
                     "s_dist_07", "s_dist_08", "s_dist_09",
                     "s_dist_10", "s_ytd", "s_order_cnt",
                     "s_remote_cnt", "s_data"],
                    total)
    for w_id in range(1, n_wh + 1):
        for i_id in range(1, n_items + 1):
            w.row([str(i_id),
                   str(w_id),
                   str(rng.randint(10, 100)),
                   rand_str(rng, 24),  # dist_01
                   rand_str(rng, 24),  # dist_02
                   rand_str(rng, 24),  # dist_03
                   rand_str(rng, 24),  # dist_04
                   rand_str(rng, 24),  # dist_05
                   rand_str(rng, 24),  # dist_06
                   rand_str(rng, 24),  # dist_07
                   rand_str(rng, 24),  # dist_08
                   rand_str(rng, 24),  # dist_09
                   rand_str(rng, 24),  # dist_10
                   fmt_float(0.0),      # s_ytd
                   "0",                  # s_order_cnt
                   "0",                  # s_remote_cnt
                   rand_str(rng, 50)])
    w.close()


# ── Main ────────────────────────────────────────────────────────────────────

GENERATORS = [
    ("warehouse", generate_warehouse),
    ("district", generate_district),
    ("item", generate_item),
    ("customer", generate_customer),
    ("history", generate_history),
    ("orders+lines", generate_orders_and_lines),
    ("stock", generate_stock),
]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate TPC-C CSV datasets for RMDB finals testing.")
    parser.add_argument("--scale", choices=sorted(SCALES),
                        default="full",
                        help="Dataset scale (default: full)")
    parser.add_argument("--output-dir", type=Path,
                        default=DEFAULT_OUTPUT_DIR,
                        help="Output directory for CSV files")
    parser.add_argument("--seed", type=int, default=BASE_SEED,
                        help="Random seed for reproducibility")
    return parser.parse_args()


def main():
    args = parse_args()
    scale = SCALES[args.scale]
    outdir = args.output_dir

    # For non-mini scales, use a subdirectory to avoid overwriting
    if args.scale != "mini":
        outdir = outdir / f"tpcc_{args.scale}"
    outdir.mkdir(parents=True, exist_ok=True)

    print(f"Generating {args.scale}-scale TPC-C data to {outdir}")
    print(f"  warehouses: {scale['warehouses']}")
    print(f"  districts:  {scale['warehouses'] * DISTRICTS_PER_WAREHOUSE}")
    print(f"  items:      {scale['items']}")
    print(f"  customers:  {scale['warehouses'] * DISTRICTS_PER_WAREHOUSE * scale['cust_per_dist']:,}")
    print(f"  seed:       {args.seed}")
    print()

    overall_start = time.monotonic()

    for idx, (name, gen_fn) in enumerate(GENERATORS):
        table_rng = random.Random(args.seed + idx * 1000)
        print(f"[{name}]")
        gen_fn(outdir, scale, table_rng)

    elapsed = time.monotonic() - overall_start
    print(f"\nDone. Total time: {elapsed:.1f}s")
    print(f"Output: {outdir}")

    # List generated files
    total_size = 0
    for csv_file in sorted(outdir.glob("*.csv")):
        size = csv_file.stat().st_size
        total_size += size
        print(f"  {csv_file.name}: {size:,} bytes")
    print(f"  Total: {total_size:,} bytes ({total_size / 1e9:.2f} GB)")


if __name__ == "__main__":
    main()
