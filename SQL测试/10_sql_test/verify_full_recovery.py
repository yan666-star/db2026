#!/usr/bin/env python3
import sys
from decimal import Decimal, InvalidOperation
from pathlib import Path


HEADER = ["id", "amount", "tag"]


def parse_sections(path: Path):
    sections = []
    current = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line.startswith("|") or not line.endswith("|"):
            continue
        fields = [field.strip() for field in line[1:-1].split("|")]
        if fields == HEADER:
            current = []
            sections.append(current)
        elif current is not None:
            current.append(fields)
    return sections


def normalize_rows(sections):
    normalized = []
    for section in sections:
        normalized_section = []
        for row in section:
            if len(row) != 3:
                normalized_section.append(row)
                continue
            try:
                normalized_section.append(
                    [str(int(row[0])), Decimal(row[1]), row[2]]
                )
            except (ValueError, InvalidOperation):
                normalized_section.append(row)
        normalized.append(normalized_section)
    return normalized


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(
            "usage: verify_full_recovery.py DATABASE_DIR [ID3_AMOUNT]"
        )

    output_path = Path(sys.argv[1]) / "output.txt"
    id3_amount = sys.argv[2] if len(sys.argv) == 3 else "35"
    if not output_path.is_file():
        raise SystemExit(f"missing output file: {output_path}")

    sections = parse_sections(output_path)
    if len(sections) != 10:
        raise SystemExit(f"expected 10 query sections, found {len(sections)}")

    expected = [
        [
            ["3", id3_amount, "three"],
            ["4", "40", "four"],
            ["7", "70", "seven"],
            ["10", "15.5", "one"],
        ],
        [["10", "15.5", "one"]],
        [],
        [],
        [["3", id3_amount, "three"]],
        [["4", "40", "four"]],
        [],
        [],
        [["7", "70", "seven"]],
        [],
    ]
    if normalize_rows(sections) != normalize_rows(expected):
        raise SystemExit(
            "recovery result mismatch\n"
            f"expected: {expected}\n"
            f"actual:   {sections}"
        )

    print("full recovery verification passed")


if __name__ == "__main__":
    main()
