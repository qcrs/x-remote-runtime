#!/usr/bin/env python3
"""Generate the reviewed, fixed-width portion of the CoreX API schema.

The schema describes wire shape and plumbing only. CUDA semantic policy stays
in the handwritten client and server layers.
"""
import argparse
import json
import re
from pathlib import Path

CAPABILITIES = {
    "CRX_CAP_DEVICE_INFO",
    "CRX_CAP_LINEAR_MEMORY",
    "CRX_CAP_COPY_SYNC",
    "CRX_CAP_STREAM_EVENT",
    "CRX_CAP_COPY_ASYNC",
    "CRX_CAP_MODULE_KERNEL",
}
IMPLEMENTATION_CLASSES = {"client_only", "scalar_query", "object_query", "semantic_rpc"}
TYPES = {
    "u32": ("uint32_t", 4, "u32"),
    "i32": ("int32_t", 4, "i32"),
    "u64": ("uint64_t", 8, "u64"),
    "i64": ("int64_t", 8, "i64"),
    "size_u64": ("uint64_t", 8, "size_u64"),
    "bool_u32": ("uint32_t", 4, "bool_u32"),
    "allocation_id": ("uint64_t", 8, "allocation_id"),
    "stream_id": ("uint64_t", 8, "stream_id"),
    "event_id": ("uint64_t", 8, "event_id"),
    "module_id": ("uint64_t", 8, "module_id"),
    "kernel_id": ("uint64_t", 8, "kernel_id"),
}
OBJECT_KINDS = {"allocation", "stream", "event", "module", "kernel"}


def fail(message):
    raise SystemExit(f"schema error: {message}")


def c_identifier(value):
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not result or result[0].isdigit():
        result = "_" + result
    return result


def validate_field(api_name, field, section):
    if not isinstance(field, dict):
        fail(f"{api_name}: {section} field must be an object")
    allowed = {"name", "type", "object_kind"}
    if set(field) - allowed or not {"name", "type"}.issubset(field):
        fail(f"{api_name}: {section} fields must contain name/type and no unknown keys")
    if not isinstance(field["name"], str) or not field["name"]:
        fail(f"{api_name}: {section} field name must be non-empty")
    if field["type"] not in TYPES:
        fail(f"{api_name}: unknown {section} field type {field['type']!r}")
    if "object_kind" in field and field["object_kind"] not in OBJECT_KINDS:
        fail(f"{api_name}: unknown object kind {field['object_kind']!r}")
    if field["type"].endswith("_id") and field.get("object_kind") is None:
        fail(f"{api_name}: object_kind is required for {field['type']}")


def load(path):
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON: {exc}")
    if data.get("schema_version") != 2 or not isinstance(data.get("apis"), list):
        fail("schema_version=2 and apis[] are required")
    if not isinstance(data.get("abi"), str) or not data["abi"]:
        fail("top-level abi is required")
    names = set()
    opcodes = set()
    for api in data["apis"]:
        required = {
            "name", "opcode", "capability_id", "implementation_class",
            "public_export", "abi", "request", "response", "backend_binding",
        }
        if set(api) != required:
            fail(f"{api.get('name', '<unnamed>')}: exact API fields required")
        name = api["name"]
        if not isinstance(name, str) or not name:
            fail("API name must be non-empty")
        if name in names:
            fail(f"duplicate API {name}")
        names.add(name)
        if not isinstance(api["opcode"], int) or api["opcode"] <= 0 or api["opcode"] > 0xffffffff:
            fail(f"{name}: opcode must be an explicit positive u32")
        if api["opcode"] in opcodes:
            fail(f"duplicate opcode {api['opcode']}")
        opcodes.add(api["opcode"])
        if api["capability_id"] not in CAPABILITIES:
            fail(f"{name}: unsupported capability {api['capability_id']!r}")
        if api["implementation_class"] not in IMPLEMENTATION_CLASSES:
            fail(f"{name}: unsupported implementation class {api['implementation_class']!r}")
        if not isinstance(api["public_export"], bool):
            fail(f"{name}: public_export must be boolean")
        if api["abi"] != data["abi"]:
            fail(f"{name}: ABI must match the schema ABI")
        for section in ("request", "response"):
            if not isinstance(api[section], list):
                fail(f"{name}: {section} must be a list")
            fields = set()
            for field in api[section]:
                validate_field(name, field, section)
                if field["name"] in fields:
                    fail(f"{name}: duplicate {section} field {field['name']}")
                fields.add(field["name"])
    return data


def wire_size(fields):
    return sum(TYPES[field["type"]][1] for field in fields)


def render_field_array(lines, prefix, fields):
    lines.append(f"static const CorexGeneratedFieldMetadata {prefix}[] = {{")
    for field in fields:
        _, size, wire_type = TYPES[field["type"]]
        object_kind = field.get("object_kind", "none")
        lines.append(
            f"    {{\"{field['name']}\", COREX_WIRE_{wire_type.upper()}, {size}u, \"{object_kind}\"}},"
        )
    lines.append("};")


def render_codec(lines, api, section):
    fields = api[section]
    stem = c_identifier(api["name"])
    suffix = "Request" if section == "request" else "Response"
    struct_name = f"CorexGenerated{stem}{suffix}"
    lines.append("typedef struct {")
    for field in fields:
        c_type = TYPES[field["type"]][0]
        lines.append(f"    {c_type} {c_identifier(field['name'])};")
    if not fields:
        lines.append("    uint8_t _empty;")
    lines.append(f"}} {struct_name};")
    lines.append(
        f"static inline int corex_generated_encode_{stem}_{section}("
        f"unsigned char *output, size_t capacity, const {struct_name} *value)"
    )
    lines.append("{")
    lines.append("    (void)output; (void)capacity;")
    if wire_size(fields):
        lines.append(f"    if (!output || !value || capacity < {wire_size(fields)}u) return -1;")
    else:
        lines.append("    if (!value) return -1;")
    lines.append("    size_t position = 0;")
    for field in fields:
        name = c_identifier(field["name"])
        if TYPES[field["type"]][1] == 4:
            lines.append(f"    corex_protocol_write_u32(output, &position, (uint32_t)value->{name});")
        else:
            lines.append(f"    corex_protocol_write_u64(output, &position, (uint64_t)value->{name});")
    lines.append(f"    return position == {wire_size(fields)}u ? 0 : -1;")
    lines.append("}")
    lines.append(
        f"static inline int corex_generated_decode_{stem}_{section}("
        f"const unsigned char *input, size_t length, {struct_name} *value)"
    )
    lines.append("{")
    lines.append("    (void)input;")
    if wire_size(fields):
        lines.append(f"    if (!input || !value || length != {wire_size(fields)}u) return -1;")
    else:
        lines.append("    if (!value || length != 0u) return -1;")
    lines.append("    size_t position = 0;")
    for field in fields:
        name = c_identifier(field["name"])
        if TYPES[field["type"]][1] == 4:
            lines.append(f"    uint32_t {name}_wire; if (corex_protocol_take_u32(input, length, &position, &{name}_wire) != 0) return -1; value->{name} = ({TYPES[field['type']][0]}){name}_wire;")
        else:
            lines.append(f"    uint64_t {name}_wire; if (corex_protocol_take_u64(input, length, &position, &{name}_wire) != 0) return -1; value->{name} = ({TYPES[field['type']][0]}){name}_wire;")
    lines.append("    return position == length ? 0 : -1;")
    lines.append("}")


def render(data):
    apis = data["apis"]
    lines = [
        "/* Generated by scripts/generate-api-schema.py; DO NOT EDIT. */",
        "#ifndef COREX_GENERATED_API_SCHEMA_H",
        "#define COREX_GENERATED_API_SCHEMA_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include \"corex_protocol.h\"",
        "typedef enum {",
    ]
    for index, field_type in enumerate(TYPES):
        lines.append(f"    COREX_WIRE_{TYPES[field_type][2].upper()} = {index + 1},")
    lines += [
        "} CorexGeneratedWireType;",
        "typedef struct { const char *name; uint32_t type; uint32_t wire_bytes; const char *object_kind; } CorexGeneratedFieldMetadata;",
        "typedef struct { const char *name; uint32_t opcode; uint32_t capability_id; const char *implementation_class; uint32_t request_bytes; uint32_t response_bytes; const char *backend_binding; const CorexGeneratedFieldMetadata *request; size_t request_count; const CorexGeneratedFieldMetadata *response; size_t response_count; } CorexGeneratedApiMetadata;",
    ]
    for api in apis:
        stem = c_identifier(api["name"])
        render_field_array(lines, f"corex_generated_{stem}_request_fields", api["request"])
        render_field_array(lines, f"corex_generated_{stem}_response_fields", api["response"])
    lines.append("static const CorexGeneratedApiMetadata corex_generated_apis[] = {")
    for api in apis:
        stem = c_identifier(api["name"])
        lines.append(
            f"    {{\"{api['name']}\", {api['opcode']}u, {api['capability_id']}, \"{api['implementation_class']}\", "
            f"{wire_size(api['request'])}u, {wire_size(api['response'])}u, \"{api['backend_binding'] or ''}\", "
            f"corex_generated_{stem}_request_fields, {len(api['request'])}u, corex_generated_{stem}_response_fields, {len(api['response'])}u}},"
        )
    lines += [
        "};",
        f"#define COREX_GENERATED_API_COUNT {len(apis)}u",
        "static inline int corex_generated_validate_payload(uint32_t opcode, size_t length)",
        "{",
        "    for (size_t i = 0; i < COREX_GENERATED_API_COUNT; ++i)",
        "        if (corex_generated_apis[i].opcode == opcode)",
        "            return length == corex_generated_apis[i].request_bytes ? 0 : -1;",
        "    return -1; /* unknown schema entries never guess */",
        "}",
        "static inline int corex_generated_validate_response(uint32_t opcode, size_t length)",
        "{",
        "    for (size_t i = 0; i < COREX_GENERATED_API_COUNT; ++i)",
        "        if (corex_generated_apis[i].opcode == opcode)",
        "            return length == corex_generated_apis[i].response_bytes ? 0 : -1;",
        "    return -1;",
        "}",
        "/* Registry metadata is in corex_generated_apis[]. */",
    ]
    for api in apis:
        render_codec(lines, api, "request")
        render_codec(lines, api, "response")
    lines += ["#endif", ""]
    return "\n".join(lines)


def render_test(data):
    checks = []
    for api in data["apis"]:
        stem = c_identifier(api["name"])
        checks.append(f"    if (corex_generated_validate_payload({api['opcode']}u, {wire_size(api['request'])}u) != 0) return 1;")
        checks.append(f"    if (corex_generated_validate_response({api['opcode']}u, {wire_size(api['response'])}u) != 0) return 1;")
        checks.append(f"    (void)corex_generated_{stem}_request_fields;")
    return "\n".join([
        "/* Generated by scripts/generate-api-schema.py; DO NOT EDIT. */",
        "#include \"corex_api_schema.h\"",
        "#include <stdio.h>",
        "int main(void) {",
        f"    if (COREX_GENERATED_API_COUNT != {len(data['apis'])}u) return 1;",
        *checks,
        "    if (corex_generated_validate_payload(999u, 0) == 0 ||",
        "        corex_generated_validate_response(999u, 0) == 0 ||",
        "        corex_generated_validate_payload(OP_STREAM_QUERY, 7) == 0) return 1;",
        "    puts(\"M3_S6_GENERATED_SCHEMA_TEST=PASS\");",
        "    return 0;",
        "}",
        "",
    ])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--test-output", type=Path, required=True)
    args = parser.parse_args()
    data = load(args.schema)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.test_output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(data))
    args.test_output.write_text(render_test(data))


if __name__ == "__main__":
    main()
