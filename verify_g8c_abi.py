#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit(
        "usage: verify_g8b_abi.py <shared-library> <expected-symbol-file>"
    )

lib = Path(sys.argv[1])
expected_file = Path(sys.argv[2])

expected = {
    line.strip()
    for line in expected_file.read_text().splitlines()
    if line.strip() and not line.startswith("#")
}

p = subprocess.run(
    ["nm", "-D", "--defined-only", str(lib)],
    text=True,
    capture_output=True,
    check=True,
)

actual = set()
for line in p.stdout.splitlines():
    cols = line.split()
    if len(cols) < 3:
        continue
    typ = cols[-2]
    name = cols[-1].split("@", 1)[0]
    if typ.upper() in {"T", "W"}:
        actual.add(name)

missing = sorted(expected - actual)
unexpected = sorted(actual - expected)

for sym in sorted(expected):
    print(f"export_{sym}={'PASS' if sym in actual else 'FAIL'}")

print("missing_exports=" + (",".join(missing) if missing else "NONE"))
print("unexpected_function_exports=" + (
    ",".join(unexpected) if unexpected else "NONE"
))

ok = not missing and not unexpected
print(f"G8C_ABI_SURFACE={'PASS' if ok else 'FAIL'}")
raise SystemExit(0 if ok else 1)
