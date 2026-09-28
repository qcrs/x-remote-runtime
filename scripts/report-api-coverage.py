#!/usr/bin/env python3
"""Report compatibility ledger status counts without hand-maintained totals."""
import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ledger", type=Path, nargs="?", default=Path("compat/cuda-api-ledger.csv"))
    args = parser.parse_args()
    with args.ledger.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    statuses = Counter(row["current_status"] for row in rows)
    families = defaultdict(Counter)
    for row in rows:
        families[row["family"]][row["current_status"]] += 1
    print(f"TOTAL={len(rows)}")
    for status in sorted(statuses):
        print(f"{status}={statuses[status]}")
    for family in sorted(families):
        summary = ",".join(f"{key}:{families[family][key]}" for key in sorted(families[family]))
        print(f"FAMILY[{family}]={summary}")


if __name__ == "__main__":
    main()
