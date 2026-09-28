#!/usr/bin/env python3
"""Check schema, compatibility ledger, protocol, and packaged API contracts."""
import csv
import json
import re
import sys
from pathlib import Path

VALID_STATUSES = {
    "IMPLEMENTED",
    "PARTIAL_IMPLEMENTED",
    "GROUND_TRUTH_REQUIRED",
    "GROUND_TRUTH_BLOCKED",
    "BACKEND_UNSUPPORTED",
    "DEPRECATED",
    "OUT_OF_SCOPE_CURRENT",
}
SCHEMA_MANAGED_LEDGER_CLASSES = {"GENERATED_RPC", "SCALAR_QUERY", "OBJECT_QUERY"}
SCHEMA_TO_LEDGER_CLASSES = {
    "scalar_query": {"GENERATED_RPC"},
    "object_query": {"OBJECT_RPC"},
    "semantic_rpc": {"SEMANTIC_RPC", "GENERATED_RPC"},
}
OPCODE_RE = re.compile(r"(OP_[A-Z0-9_]+)=(\d+)")


def fail(message):
    print(f"API_CONTRACT_ERROR={message}", file=sys.stderr)
    raise SystemExit(1)


def names_from_lines(path):
    return {
        line.strip()
        for line in path.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def parse_protocol_change(value):
    if value == "NONE" or not value.startswith("OP_"):
        return None
    match = OPCODE_RE.fullmatch(value)
    if not match:
        fail(f"malformed ledger protocol_change {value!r}")
    return match.group(1), int(match.group(2))


def main():
    root = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).resolve().parents[1]
    schema = json.loads((root / "schema/corex_api_schema.json").read_text())
    apis = schema.get("apis", [])
    schema_names = {api["name"] for api in apis}
    schema_opcode_names = {api["opcode_name"] for api in apis}
    schema_opcodes = [api["opcode"] for api in apis]
    if len(schema_names) != len(apis):
        fail("schema API names are not unique")
    if len(schema_opcode_names) != len(apis):
        fail("schema opcode_name values are not unique")
    if len(set(schema_opcodes)) != len(apis):
        fail("schema opcodes are not unique")
    for api in apis:
        if not re.fullmatch(r"OP_[A-Z0-9_]+", api.get("opcode_name", "")):
            fail(f"invalid schema opcode_name for {api.get('name', '<unnamed>')}")

    with (root / "compat/cuda-api-ledger.csv").open(newline="") as stream:
        ledger_rows = list(csv.DictReader(stream))
    ledger_by_name = {row["api_name"]: row for row in ledger_rows}
    if len(ledger_by_name) != len(ledger_rows):
        fail("ledger API names are not unique")
    unknown_statuses = sorted(
        {row["current_status"] for row in ledger_rows} - VALID_STATUSES
    )
    if unknown_statuses:
        fail("unknown ledger statuses: " + ",".join(unknown_statuses))
    missing_ledger = sorted(schema_names - set(ledger_by_name))
    if missing_ledger:
        fail("missing ledger rows: " + ",".join(missing_ledger))
    for row in ledger_rows:
        parsed = parse_protocol_change(row["protocol_change"])
        if parsed and parsed[1] <= 0:
            fail(f"ledger opcode must be positive for {row['api_name']}")
        if row["implementation_class"] in SCHEMA_MANAGED_LEDGER_CLASSES:
            if row["api_name"] not in schema_names:
                fail(
                    "schema-managed ledger API is absent from schema: "
                    + row["api_name"]
                )

    protocol = (root / "include/internal/corex_protocol.h").read_text()
    protocol_pairs = re.findall(
        r"\b(OP_[A-Z0-9_]+)\s*=\s*(\d+)\s*,", protocol
    )
    protocol_values = {name: int(value) for name, value in protocol_pairs}
    if len(protocol_values) != len(protocol_pairs):
        fail("protocol enum symbols are not unique")
    for row in ledger_rows:
        parsed = parse_protocol_change(row["protocol_change"])
        if parsed and protocol_values.get(parsed[0]) != parsed[1]:
            fail(
                f"ledger protocol mismatch for {row['api_name']}: "
                f"{parsed[0]}={parsed[1]}"
            )
    for api in apis:
        row = ledger_by_name[api["name"]]
        expected_change = f"{api['opcode_name']}={api['opcode']}"
        if row["protocol_change"] != expected_change:
            fail(f"ledger opcode mismatch for {api['name']}: expected {expected_change}")
        if protocol_values.get(api["opcode_name"]) != api["opcode"]:
            fail(
                f"protocol opcode mismatch for {api['name']}: "
                f"{api['opcode_name']}"
            )
        allowed_classes = SCHEMA_TO_LEDGER_CLASSES.get(api["implementation_class"])
        if allowed_classes and row["implementation_class"] not in allowed_classes:
            fail(
                f"implementation class mismatch for {api['name']}: "
                f"schema={api['implementation_class']} ledger={row['implementation_class']}"
            )
        if api["public_export"] and row["current_status"] not in {
            "IMPLEMENTED", "PARTIAL_IMPLEMENTED"
        }:
            fail(f"public schema API {api['name']} is not classified as implemented")

    public_names = {api["name"] for api in apis if api["public_export"]}
    expected = names_from_lines(root / "packaging/EXPECTED-EXPORTED-FUNCTIONS.txt")
    abi_surface = (root / "packaging/ABI-SURFACE-v1.1.txt").read_text().splitlines()
    abi_names = {line.strip() for line in abi_surface if line.strip() and not line.startswith("[")}
    version_map = (root / "packaging/corex_remote_cudart.map").read_text()
    missing_expected = sorted(public_names - expected)
    missing_abi = sorted(public_names - abi_names)
    missing_map = sorted(
        name for name in public_names
        if not re.search(rf"^\s*{re.escape(name)};\s*$", version_map, re.MULTILINE)
    )
    if missing_expected:
        fail("missing expected exports: " + ",".join(missing_expected))
    if missing_abi:
        fail("missing ABI surface entries: " + ",".join(missing_abi))
    if missing_map:
        fail("missing version-map exports: " + ",".join(missing_map))
    unexpected_schema_expected = sorted((schema_names - public_names) & expected)
    if unexpected_schema_expected:
        fail(
            "non-public schema APIs listed as expected exports: "
            + ",".join(unexpected_schema_expected)
        )

    print(f"SCHEMA_APIS={len(apis)}")
    print(f"SCHEMA_PUBLIC_EXPORTS={len(public_names)}")
    print("API_CONTRACTS=PASS")


if __name__ == "__main__":
    main()
