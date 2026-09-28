#!/usr/bin/env python3
"""Report compatibility ledger status counts without hand-maintained totals."""
import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path

LEDGER_STATUSES = (
    "IMPLEMENTED",
    "PARTIAL_IMPLEMENTED",
    "GROUND_TRUTH_REQUIRED",
    "BACKEND_UNSUPPORTED",
    "DEPRECATED",
    "OUT_OF_SCOPE_CURRENT",
)
REPORT_ONLY_STATUSES = ("GROUND_TRUTH_BLOCKED",)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ledger", type=Path, nargs="?", default=Path("compat/cuda-api-ledger.csv"))
    args = parser.parse_args()
    with args.ledger.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    statuses = Counter(row["current_status"] for row in rows)
    unknown = sorted(set(statuses) - set(LEDGER_STATUSES))
    if unknown:
        raise SystemExit("unknown ledger statuses: " + ",".join(unknown))
    families = defaultdict(Counter)
    for row in rows:
        families[row["family"]][row["current_status"]] += 1
    public_rows = [row for row in rows if row["target_surface"] == "PUBLIC_RUNTIME_API"]
    print(f"TOTAL={len(rows)}")
    print(f"PUBLIC_RUNTIME_API_TOTAL={len(public_rows)}")
    for status in LEDGER_STATUSES:
        print(f"{status}={statuses[status]}")
    for status in REPORT_ONLY_STATUSES:
        print(f"{status}=0")
    for status in ("IMPLEMENTED", "PARTIAL_IMPLEMENTED"):
        public_count = sum(row["current_status"] == status for row in public_rows)
        print(f"{status}_PUBLIC_RUNTIME_API={public_count}")
    for family in sorted(families):
        summary = ",".join(f"{key}:{families[family][key]}" for key in sorted(families[family]))
        print(f"FAMILY[{family}]={summary}")


if __name__ == "__main__":
    main()
