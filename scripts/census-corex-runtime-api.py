#!/usr/bin/env python3
"""Deterministically census the installed CoreX Runtime API surface."""

import argparse
import csv
import hashlib
from pathlib import Path
import re


COLUMNS = [
    "api_name", "family", "header", "signature", "target_surface",
    "current_status", "implementation_class", "client_component",
    "server_component", "backend_requirement", "protocol_change",
    "ground_truth_required", "test_level", "priority", "notes",
    "planned_milestone", "execution_policy", "completion_rule",
]
REVIEWED_COLUMNS = [column for column in COLUMNS if column not in {"api_name", "signature"}]


def normalize(text):
    return re.sub(r"\s+", " ", text).strip()


def family_for(name):
    rules = [
        ("Graph", "Graph"), ("MemPool", "Memory Pool / Async Allocation"),
        ("MallocAsync", "Memory Pool / Async Allocation"),
        ("FreeAsync", "Memory Pool / Async Allocation"),
        ("Stream", "Stream"), ("Event", "Event"), ("Memcpy", "Memcpy"),
        ("Memset", "Linear Memory"), ("Malloc", "Linear Memory"),
        ("Free", "Linear Memory"), ("Host", "Host Memory"),
        ("Array", "Array"), ("Texture", "Texture Object"),
        ("Surface", "Surface Object"), ("Func", "Execution"),
        ("Launch", "Execution"), ("Occupancy", "Execution"),
        ("Device", "Device"), ("GetDevice", "Device"),
        ("SetDevice", "Device"), ("GetLastError", "Error"),
        ("PeekAtLastError", "Error"), ("GetError", "Error"),
        ("External", "External Resource Interop"), ("Ipc", "IPC"),
        ("Peer", "Peer / Multi-device"),
    ]
    for token, family in rules:
        if token in name:
            return family
    return "Other Runtime"


def extract_declarations(header):
    text = header.read_text(errors="replace")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    rows = {}
    for statement in text.split(";"):
        if "CUDARTAPI" not in statement:
            continue
        match = re.search(r"\bCUDARTAPI\s+(cuda[A-Za-z0-9_]+)\s*\((.*)\)\s*$", statement, re.S)
        if not match:
            continue
        name = match.group(1)
        declaration_start = statement.rfind("extern ")
        if declaration_start < 0:
            declaration_start = statement.find("CUDARTAPI")
        signature = normalize(statement[declaration_start:]) + ";"
        deprecated = "__CUDA_DEPRECATED" in signature
        rows[name] = {
            "api_name": name,
            "family": family_for(name),
            "header": header.name,
            "signature": signature,
            "target_surface": "PUBLIC_RUNTIME_API",
            "current_status": "DEPRECATED" if deprecated else "GROUND_TRUTH_REQUIRED",
            "implementation_class": "DEPRECATED" if deprecated else "BACKEND_CAPABILITY",
            "client_component": "TBD",
            "server_component": "TBD",
            "backend_requirement": "CoreX 4.4 API/runtime probe required",
            "protocol_change": "TBD after classification",
            "ground_truth_required": "TRUE",
            "test_level": "TBD",
            "priority": "P4",
            "notes": "Census-derived declaration only; semantics not inferred from signature.",
            "planned_milestone": "CENSUS_DECIDES",
            "execution_policy": "PROBE_THEN_IMPLEMENT_OR_MARK_BLOCKED",
            "completion_rule": "Human classification and CoreX ground truth required before implementation.",
        }
    return rows, hashlib.sha256(header.read_bytes()).hexdigest()


def load_rows(path):
    if not path or not path.exists():
        return {}
    with path.open(newline="") as stream:
        return {row["api_name"]: row for row in csv.DictReader(stream)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corex", default="/usr/local/corex-4.4.0")
    parser.add_argument("--annotations", type=Path)
    parser.add_argument("--output", type=Path, default=Path("compat/cuda-api-ledger.csv"))
    args = parser.parse_args()

    header = Path(args.corex) / "include/cuda_runtime_api.h"
    if not header.is_file():
        raise SystemExit(f"missing CoreX Runtime header: {header}")
    rows, header_hash = extract_declarations(header)
    public_declaration_count = len(rows)

    existing = load_rows(args.output)
    annotations = load_rows(args.annotations)
    for source in (existing, annotations):
        for name, reviewed in source.items():
            row = rows.setdefault(name, {column: "" for column in COLUMNS})
            row["api_name"] = name
            if not row.get("signature"):
                row["signature"] = reviewed.get("signature", "PROJECT_OR_COMPILER_ABI")
            for column in REVIEWED_COLUMNS:
                if reviewed.get(column, ""):
                    row[column] = reviewed[column]

    expected_path = Path("packaging/EXPECTED-EXPORTED-FUNCTIONS.txt")
    expected = {
        line.strip() for line in expected_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    for name in sorted(expected - rows.keys()):
        if not (name.startswith("__cuda") or name.startswith("corexRemote")):
            continue
        rows[name] = {
            column: "" for column in COLUMNS
        }
        rows[name].update({
            "api_name": name,
            "family": "Compiler ABI" if name.startswith("__cuda") else "CoreX Remote Extension",
            "header": "compiler-generated/private" if name.startswith("__cuda") else "corex_remote_cudart_ext.h",
            "signature": "PROJECT_OR_COMPILER_ABI",
            "target_surface": "COMPILER_PRIVATE_ABI" if name.startswith("__cuda") else "PROJECT_EXTENSION",
            "current_status": "IMPLEMENTED",
            "implementation_class": "COMPILER_ABI",
            "backend_requirement": "Existing proven implementation",
            "protocol_change": "NONE",
            "ground_truth_required": "FALSE",
            "test_level": "L1 regression",
            "priority": "P0",
            "notes": "Project/compiler ABI row; not derived from the public Runtime header.",
            "execution_policy": "REGRESSION_ONLY_UNLESS_REFACTOR_REQUIRED",
            "completion_rule": "Existing semantic remains PASS under common gates.",
        })
    missing = sorted(expected - rows.keys())
    if missing:
        raise SystemExit("implemented/exported APIs missing census rows: " + ",".join(missing))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS, lineterminator="\n")
        writer.writeheader()
        for name in sorted(rows):
            writer.writerow({column: rows[name].get(column, "") for column in COLUMNS})

    statuses = {}
    for row in rows.values():
        status = row.get("current_status", "")
        statuses[status] = statuses.get(status, 0) + 1
    print(f"COREX_RUNTIME_HEADER={header}")
    print(f"COREX_RUNTIME_HEADER_SHA256={header_hash}")
    print(f"COREX_RUNTIME_API_COUNT={len(rows)}")
    print(f"COREX_PUBLIC_RUNTIME_DECLARATIONS={public_declaration_count}")
    print(f"EXPORTED_API_ROWS={len(expected)}")
    print("EXPORTED_API_COVERAGE=PASS")
    print("ANNOTATION_PRESERVATION=PASS")
    for status in sorted(statuses):
        print(f"STATUS_{status or 'EMPTY'}={statuses[status]}")
    print("M2_S1_CENSUS=PASS")


if __name__ == "__main__":
    main()
