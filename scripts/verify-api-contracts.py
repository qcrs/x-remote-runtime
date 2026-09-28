#!/usr/bin/env python3
"""Check that reviewed schema APIs are represented in the compatibility ledger and ABI."""
import csv
import json
import re
import sys
from pathlib import Path

PROTOCOL_NAMES = {
    "cudaDeviceSynchronize": "OP_SYNC",
    "cudaStreamQuery": "OP_STREAM_QUERY",
    "cudaEventQuery": "OP_EVENT_QUERY",
    "cudaDeviceGetAttribute": "OP_DEVICE_GET_ATTRIBUTE",
    "cudaGetDeviceFlags": "OP_GET_DEVICE_FLAGS",
    "cudaDeviceGetStreamPriorityRange": "OP_GET_PRIORITY_RANGE",
    "cudaDeviceGetLimit": "OP_GET_LIMIT",
    "cudaDeviceGetCacheConfig": "OP_GET_CACHE_CONFIG",
    "cudaDeviceGetSharedMemConfig": "OP_GET_SHARED_MEM_CONFIG",
    "cudaFuncSetAttribute": "OP_FUNCTION_SET_ATTRIBUTE",
    "cudaFuncSetCacheConfig": "OP_FUNCTION_SET_CACHE_CONFIG",
}


def fail(message):
    print(f"API_CONTRACT_ERROR={message}", file=sys.stderr)
    raise SystemExit(1)


def names_from_lines(path):
    return {
        line.strip()
        for line in path.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def main():
    root = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).resolve().parents[1]
    schema = json.loads((root / "schema/corex_api_schema.json").read_text())
    apis = schema.get("apis", [])
    schema_names = {api["name"] for api in apis}
    if len(schema_names) != len(apis):
        fail("schema API names are not unique")
    opcodes = [api["opcode"] for api in apis]
    if len(set(opcodes)) != len(opcodes):
        fail("schema opcodes are not unique")

    with (root / "compat/cuda-api-ledger.csv").open(newline="") as stream:
        ledger_rows = list(csv.DictReader(stream))
    ledger_by_name = {row["api_name"]: row for row in ledger_rows}
    if len(ledger_by_name) != len(ledger_rows):
        fail("ledger API names are not unique")
    ledger_names = set(ledger_by_name)
    missing_ledger = sorted(schema_names - ledger_names)
    if missing_ledger:
        fail("missing ledger rows: " + ",".join(missing_ledger))
    for api in apis:
        row = ledger_by_name[api["name"]]
        if api["public_export"] and row["current_status"] not in {
            "IMPLEMENTED", "PARTIAL_IMPLEMENTED"
        }:
            fail(f"public schema API {api['name']} is not classified as implemented")
        if row["protocol_change"] not in {
            f"{PROTOCOL_NAMES.get(api['name'], '')}={api['opcode']}"
        }:
            fail(f"ledger opcode mismatch for {api['name']}")

    public_names = {api["name"] for api in apis if api["public_export"]}
    expected = names_from_lines(root / "packaging/EXPECTED-EXPORTED-FUNCTIONS.txt")
    abi_surface = (root / "packaging/ABI-SURFACE-v1.1.txt").read_text().splitlines()
    abi_names = {line.strip() for line in abi_surface if line.strip() and not line.startswith("[")}
    missing_expected = sorted(public_names - expected)
    missing_abi = sorted(public_names - abi_names)
    if missing_expected:
        fail("missing expected exports: " + ",".join(missing_expected))
    if missing_abi:
        fail("missing ABI surface entries: " + ",".join(missing_abi))

    protocol = (root / "include/internal/corex_protocol.h").read_text()
    protocol_values = {
        name: int(value)
        for name, value in re.findall(r"\b(OP_[A-Z0-9_]+)\s*=\s*(\d+)\s*,", protocol)
    }
    for api in apis:
        protocol_name = PROTOCOL_NAMES.get(api["name"])
        if not protocol_name:
            fail(f"missing reviewed protocol opcode mapping for {api['name']}")
        if protocol_values.get(protocol_name) != api["opcode"]:
            fail(f"protocol opcode mismatch for {api['name']}: {protocol_name}")

    version_map = (root / "packaging/corex_remote_cudart.map").read_text()
    missing_map = sorted(
        name for name in public_names
        if not re.search(rf"^\s*{re.escape(name)};\s*$", version_map, re.MULTILINE)
    )
    if missing_map:
        fail("missing version-map exports: " + ",".join(missing_map))

    print(f"SCHEMA_APIS={len(apis)}")
    print(f"SCHEMA_PUBLIC_EXPORTS={len(public_names)}")
    print("API_CONTRACTS=PASS")


if __name__ == "__main__":
    main()
