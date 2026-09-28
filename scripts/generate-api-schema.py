#!/usr/bin/env python3
"""Generate reviewed scalar and bounded-field CoreX API plumbing.

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
BOUNDED_TYPES = {"bounded_bytes", "bounded_string"}


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
    allowed = {"name", "type", "object_kind", "max_bytes"}
    if set(field) - allowed or not {"name", "type"}.issubset(field):
        fail(f"{api_name}: {section} fields must contain name/type and no unknown keys")
    if not isinstance(field["name"], str) or not field["name"]:
        fail(f"{api_name}: {section} field name must be non-empty")
    if not isinstance(field["type"], str) or field["type"] not in set(TYPES) | BOUNDED_TYPES:
        fail(f"{api_name}: unknown {section} field type {field['type']!r}")
    if "object_kind" in field and field["object_kind"] not in OBJECT_KINDS:
        fail(f"{api_name}: unknown object kind {field['object_kind']!r}")
    if field["type"].endswith("_id") and field.get("object_kind") is None:
        fail(f"{api_name}: object_kind is required for {field['type']}")
    if field["type"] in BOUNDED_TYPES:
        maximum = field.get("max_bytes")
        if not isinstance(maximum, int) or isinstance(maximum, bool) or not 1 <= maximum <= 65535:
            fail(f"{api_name}: {section} {field['name']} max_bytes must be in 1..65535")
    elif "max_bytes" in field:
        fail(f"{api_name}: max_bytes is only valid for bounded fields")


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
    opcode_names = set()
    for api in data["apis"]:
        required = {
            "name", "opcode_name", "opcode", "capability_id", "implementation_class",
            "public_export", "abi", "request", "response", "backend_binding",
            "server_handler",
        }
        if set(api) != required:
            fail(f"{api.get('name', '<unnamed>')}: exact API fields required")
        name = api["name"]
        if not isinstance(name, str) or not name:
            fail("API name must be non-empty")
        if name in names:
            fail(f"duplicate API {name}")
        names.add(name)
        opcode_name = api["opcode_name"]
        if not isinstance(opcode_name, str) or not re.fullmatch(r"OP_[A-Z0-9_]+", opcode_name):
            fail(f"{name}: opcode_name must match OP_[A-Z0-9_]+")
        if opcode_name in opcode_names:
            fail(f"duplicate opcode_name {opcode_name}")
        opcode_names.add(opcode_name)
        if not isinstance(api["opcode"], int) or api["opcode"] <= 0 or api["opcode"] > 0xffffffff:
            fail(f"{name}: opcode must be an explicit positive u32")
        if api["opcode"] in opcodes:
            fail(f"duplicate opcode {api['opcode']}")
        opcodes.add(api["opcode"])
        if api["capability_id"] not in CAPABILITIES:
            fail(f"{name}: unsupported capability {api['capability_id']!r}")
        if api["implementation_class"] not in IMPLEMENTATION_CLASSES:
            fail(f"{name}: unsupported implementation class {api['implementation_class']!r}")
        if not isinstance(api["server_handler"], str) or not re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*", api["server_handler"]
        ):
            fail(f"{name}: server_handler must be a C identifier")
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


def field_wire_bounds(field):
    if field["type"] in BOUNDED_TYPES:
        return 4, 4 + field["max_bytes"]
    size = TYPES[field["type"]][1]
    return size, size


def wire_bounds(fields):
    bounds = [field_wire_bounds(field) for field in fields]
    return (
        sum(minimum for minimum, _ in bounds),
        sum(maximum for _, maximum in bounds),
    )


def wire_type(field):
    return "COREX_WIRE_" + field["type"].upper()


def bounded_max_macro(api, section, field):
    return (
        f"COREX_GENERATED_{c_identifier(api['name']).upper()}_"
        f"{section.upper()}_{c_identifier(field['name']).upper()}_MAX_BYTES"
    )


def render_field_array(lines, prefix, fields):
    lines.append(f"static const CorexGeneratedFieldMetadata {prefix}[] = {{")
    for field in fields:
        minimum, maximum = field_wire_bounds(field)
        max_bytes = field.get("max_bytes", 0)
        object_kind = field.get("object_kind", "none")
        lines.append(
            f"    {{\"{field['name']}\", {wire_type(field)}, {minimum}u, {maximum}u, "
            f"{max_bytes}u, \"{object_kind}\"}},"
        )
    lines.append("};")


def render_codec(lines, api, section):
    fields = api[section]
    stem = c_identifier(api["name"])
    suffix = "Request" if section == "request" else "Response"
    struct_name = f"CorexGenerated{stem}{suffix}"
    minimum, maximum = wire_bounds(fields)
    lines.append("typedef struct {")
    for field in fields:
        name = c_identifier(field["name"])
        if field["type"] == "bounded_bytes":
            lines.append(f"    uint32_t {name}_length;")
            lines.append(f"    uint8_t {name}[{bounded_max_macro(api, section, field)}];")
        elif field["type"] == "bounded_string":
            lines.append(f"    uint32_t {name}_length;")
            lines.append(f"    char {name}[{bounded_max_macro(api, section, field)} + 1u];")
        else:
            lines.append(f"    {TYPES[field['type']][0]} {name};")
    if not fields:
        lines.append("    uint8_t _empty;")
    lines.append(f"}} {struct_name};")
    lines.append(
        f"static inline int corex_generated_encode_{stem}_{section}("
        f"unsigned char *output, size_t capacity, size_t *encoded_length, "
        f"const {struct_name} *value)"
    )
    lines.append("{")
    lines.append("    if (!encoded_length || !value) return -1;")
    if maximum:
        lines.append(f"    if (!output || capacity < {minimum}u) return -1;")
    else:
        lines.append("    (void)output; (void)capacity;")
    lines.append("    size_t position = 0;")
    for field in fields:
        name = c_identifier(field["name"])
        if field["type"] in BOUNDED_TYPES:
            maximum_macro = bounded_max_macro(api, section, field)
            lines.append(f"    if (value->{name}_length > {maximum_macro}) return -1;")
            if field["type"] == "bounded_string":
                lines.append(
                    f"    if (value->{name}[value->{name}_length] != '\\0' || "
                    f"memchr(value->{name}, '\\0', value->{name}_length) != NULL) return -1;"
                )
            lines.append(
                f"    if (position > capacity || capacity - position < 4u + value->{name}_length) return -1;"
            )
            lines.append(f"    corex_protocol_write_u32(output, &position, value->{name}_length);")
            lines.append(
                f"    if (value->{name}_length) memcpy(output + position, value->{name}, value->{name}_length);"
            )
            lines.append(f"    position += value->{name}_length;")
        elif TYPES[field["type"]][1] == 4:
            lines.append(f"    corex_protocol_write_u32(output, &position, (uint32_t)value->{name});")
        else:
            lines.append(f"    corex_protocol_write_u64(output, &position, (uint64_t)value->{name});")
    lines.append("    *encoded_length = position;")
    lines.append(f"    return position <= {maximum}u ? 0 : -1;")
    lines.append("}")
    lines.append(
        f"static inline int corex_generated_decode_{stem}_{section}("
        f"const unsigned char *input, size_t length, {struct_name} *value)"
    )
    lines.append("{")
    if maximum:
        if minimum == maximum:
            lines.append(f"    if (!input || !value || length != {minimum}u) return -1;")
        else:
            lines.append(f"    if (!input || !value || length < {minimum}u || length > {maximum}u) return -1;")
    else:
        lines.append("    if (!value || length != 0u) return -1;")
        lines.append("    (void)input;")
    lines.append("    size_t position = 0;")
    for field in fields:
        name = c_identifier(field["name"])
        if field["type"] in BOUNDED_TYPES:
            maximum_macro = bounded_max_macro(api, section, field)
            lines.append(
                f"    uint32_t {name}_length; if (corex_protocol_take_u32(input, length, &position, &{name}_length) != 0 || {name}_length > {maximum_macro} || {name}_length > length - position) return -1;"
            )
            if field["type"] == "bounded_string":
                lines.append(
                    f"    if (memchr(input + position, '\\0', {name}_length) != NULL) return -1;"
                )
            lines.append(
                f"    if ({name}_length) memcpy(value->{name}, input + position, {name}_length);"
            )
            if field["type"] == "bounded_string":
                lines.append(f"    value->{name}[{name}_length] = '\\0';")
            lines.append(f"    value->{name}_length = {name}_length; position += {name}_length;")
        elif TYPES[field["type"]][1] == 4:
            lines.append(f"    uint32_t {name}_wire; if (corex_protocol_take_u32(input, length, &position, &{name}_wire) != 0) return -1; value->{name} = ({TYPES[field['type']][0]}){name}_wire;")
        else:
            lines.append(f"    uint64_t {name}_wire; if (corex_protocol_take_u64(input, length, &position, &{name}_wire) != 0) return -1; value->{name} = ({TYPES[field['type']][0]}){name}_wire;")
    lines.append("    return position == length ? 0 : -1;")
    lines.append("}")


def render_rpc_call(lines, api):
    stem = c_identifier(api["name"])
    request_name = f"CorexGenerated{stem}Request"
    response_name = f"CorexGenerated{stem}Response"
    request_max = max(1, wire_bounds(api["request"])[1])
    lines += [
        f"static inline int corex_generated_call_{stem}(CorexGeneratedRpcCall rpc_call, "
        f"const {request_name} *request, {response_name} *response)",
        "{",
        f"    unsigned char request_wire[{request_max}u];",
        "    size_t request_length = 0;",
        "    unsigned char *response_wire = NULL;",
        "    uint32_t response_length = 0;",
        "    if (!rpc_call || !request || !response ||",
        f"        corex_generated_encode_{stem}_request(request_wire, sizeof(request_wire), &request_length, request) != 0)",
        "        return -1;",
        f"    if (rpc_call({api['opcode']}u, request_wire, (uint32_t)request_length, "
        "&response_wire, &response_length) != 0) {",
        "        free(response_wire);",
        "        return -1;",
        "    }",
        f"    int result = corex_generated_decode_{stem}_response(response_wire, response_length, response);",
        "    free(response_wire);",
        "    return result;",
        "}",
    ]


def render(data):
    apis = data["apis"]
    lines = [
        "/* Generated by scripts/generate-api-schema.py; DO NOT EDIT. */",
        "#ifndef COREX_GENERATED_API_SCHEMA_H",
        "#define COREX_GENERATED_API_SCHEMA_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "#include \"corex_protocol.h\"",
        "typedef enum {",
    ]
    wire_types = list(TYPES) + sorted(BOUNDED_TYPES)
    for index, field_type in enumerate(wire_types):
        lines.append(f"    COREX_WIRE_{field_type.upper()} = {index + 1},")
    lines += [
        "} CorexGeneratedWireType;",
        "typedef struct { const char *name; uint32_t type; uint32_t min_wire_bytes; uint32_t max_wire_bytes; uint32_t max_payload_bytes; const char *object_kind; } CorexGeneratedFieldMetadata;",
        "typedef struct { const char *name; const char *opcode_name; uint32_t opcode; uint32_t capability_id; const char *implementation_class; uint32_t request_min_bytes; uint32_t request_max_bytes; uint32_t response_min_bytes; uint32_t response_max_bytes; const char *backend_binding; const char *server_handler; const CorexGeneratedFieldMetadata *request; size_t request_count; const CorexGeneratedFieldMetadata *response; size_t response_count; } CorexGeneratedApiMetadata;",
        "typedef int (*CorexGeneratedRpcCall)(uint32_t, const unsigned char *, uint32_t, unsigned char **, uint32_t *);",
    ]
    for api in apis:
        stem = c_identifier(api["name"])
        render_field_array(lines, f"corex_generated_{stem}_request_fields", api["request"])
        render_field_array(lines, f"corex_generated_{stem}_response_fields", api["response"])
        for section in ("request", "response"):
            for field in api[section]:
                if field["type"] in BOUNDED_TYPES:
                    lines.append(
                        f"#define {bounded_max_macro(api, section, field)} {field['max_bytes']}u"
                    )
    lines.append("static const CorexGeneratedApiMetadata corex_generated_apis[] = {")
    for api in apis:
        stem = c_identifier(api["name"])
        lines.append(
            f"    {{\"{api['name']}\", \"{api['opcode_name']}\", {api['opcode']}u, {api['capability_id']}, \"{api['implementation_class']}\", "
            f"{wire_bounds(api['request'])[0]}u, {wire_bounds(api['request'])[1]}u, "
            f"{wire_bounds(api['response'])[0]}u, {wire_bounds(api['response'])[1]}u, "
            f"\"{api['backend_binding'] or ''}\", \"{api['server_handler']}\", "
            f"corex_generated_{stem}_request_fields, {len(api['request'])}u, corex_generated_{stem}_response_fields, {len(api['response'])}u}},"
        )
    lines += [
        "};",
        f"#define COREX_GENERATED_API_COUNT {len(apis)}u",
        "static inline int corex_generated_validate_payload(uint32_t opcode, size_t length)",
        "{",
        "    for (size_t i = 0; i < COREX_GENERATED_API_COUNT; ++i)",
        "        if (corex_generated_apis[i].opcode == opcode)",
        "            return length >= corex_generated_apis[i].request_min_bytes && length <= corex_generated_apis[i].request_max_bytes ? 0 : -1;",
        "    return -1; /* unknown schema entries never guess */",
        "}",
        "static inline int corex_generated_validate_response(uint32_t opcode, size_t length)",
        "{",
        "    for (size_t i = 0; i < COREX_GENERATED_API_COUNT; ++i)",
        "        if (corex_generated_apis[i].opcode == opcode)",
        "            return length >= corex_generated_apis[i].response_min_bytes && length <= corex_generated_apis[i].response_max_bytes ? 0 : -1;",
        "    return -1;",
        "}",
        "/* Schema-owned server registrations are generated from reviewed metadata. */",
    ]
    lines.append("#define COREX_GENERATED_SERVER_HANDLER_REGISTRY(X) " + chr(92))
    for index, api in enumerate(apis):
        continuation = " " + chr(92) if index + 1 < len(apis) else ""
        lines.append(
            f"    X({api['opcode']}u, {api['server_handler']}, {api['capability_id']}){continuation}"
        )
    for api in apis:
        render_codec(lines, api, "request")
        render_codec(lines, api, "response")
        render_rpc_call(lines, api)
    lines += ["#endif", ""]
    return "\n".join(lines)


def render_test(data):
    checks = []
    codec_checks = []
    for api in data["apis"]:
        stem = c_identifier(api["name"])
        request_min, request_max = wire_bounds(api["request"])
        response_min, response_max = wire_bounds(api["response"])
        checks.append(f"    if (corex_generated_validate_payload({api['opcode']}u, {request_min}u) != 0) return 1;")
        checks.append(f"    if (corex_generated_validate_payload({api['opcode']}u, {request_max}u) != 0) return 1;")
        checks.append(f"    if (corex_generated_validate_response({api['opcode']}u, {response_min}u) != 0) return 1;")
        checks.append(f"    if (corex_generated_validate_response({api['opcode']}u, {response_max}u) != 0) return 1;")
        if request_min:
            checks.append(f"    if (corex_generated_validate_payload({api['opcode']}u, {request_min - 1}u) == 0) return 1;")
        checks.append(f"    if (corex_generated_validate_payload({api['opcode']}u, {request_max + 1}u) == 0) return 1;")
        if response_min:
            checks.append(f"    if (corex_generated_validate_response({api['opcode']}u, {response_min - 1}u) == 0) return 1;")
        checks.append(f"    if (corex_generated_validate_response({api['opcode']}u, {response_max + 1}u) == 0) return 1;")
        checks.append(f"    (void)corex_generated_{stem}_request_fields;")
        for section in ("request", "response"):
            struct_suffix = "Request" if section == "request" else "Response"
            encode_name = f"corex_generated_encode_{stem}_{section}"
            decode_name = f"corex_generated_decode_{stem}_{section}"
            for field_index, field in enumerate(api[section]):
                if field["type"] not in BOUNDED_TYPES:
                    continue
                name = c_identifier(field["name"])
                value_type = f"CorexGenerated{stem}{struct_suffix}"
                array_size = max(1, wire_bounds(api[section])[1])
                test_length = min(3, field["max_bytes"])
                test_data = "\"abc\"" if field["type"] == "bounded_string" else "\"abc\""
                maximum_macro = bounded_max_macro(api, section, field)
                codec_checks.append(
                    f"    if ({maximum_macro} != {field['max_bytes']}u) return 1;"
                )
                codec_checks += [
                    f"    {value_type} input_{stem}_{section}_{name} = {{0}}, output_{stem}_{section}_{name} = {{0}};",
                    f"    unsigned char wire_{stem}_{section}_{name}[{array_size}u]; size_t wire_length_{stem}_{section}_{name} = 0;",
                    f"    input_{stem}_{section}_{name}.{name}_length = {test_length}u;",
                    f"    memcpy(input_{stem}_{section}_{name}.{name}, {test_data}, {test_length}u);",
                    f"    if ({encode_name}(wire_{stem}_{section}_{name}, sizeof(wire_{stem}_{section}_{name}), &wire_length_{stem}_{section}_{name}, &input_{stem}_{section}_{name}) != 0 ||",
                    f"        {decode_name}(wire_{stem}_{section}_{name}, wire_length_{stem}_{section}_{name}, &output_{stem}_{section}_{name}) != 0 ||",
                    f"        output_{stem}_{section}_{name}.{name}_length != {test_length}u || memcmp(input_{stem}_{section}_{name}.{name}, output_{stem}_{section}_{name}.{name}, {test_length}u) != 0) return 1;",
                    f"    input_{stem}_{section}_{name}.{name}_length = {field['max_bytes'] + 1}u;",
                    f"    if ({encode_name}(wire_{stem}_{section}_{name}, sizeof(wire_{stem}_{section}_{name}), &wire_length_{stem}_{section}_{name}, &input_{stem}_{section}_{name}) == 0) return 1;",
                ]
                if field["type"] == "bounded_string":
                    wire_offset = sum(
                        field_wire_bounds(previous)[0]
                        for previous in api[section][:field_index]
                    )
                    nul_index = max(0, test_length - 1)
                    codec_checks += [
                        f"    input_{stem}_{section}_{name}.{name}_length = {test_length}u;",
                        f"    memcpy(input_{stem}_{section}_{name}.{name}, {test_data}, {test_length}u);",
                        f"    input_{stem}_{section}_{name}.{name}[{test_length}u] = 'x';",
                        f"    if ({encode_name}(wire_{stem}_{section}_{name}, sizeof(wire_{stem}_{section}_{name}), &wire_length_{stem}_{section}_{name}, &input_{stem}_{section}_{name}) == 0) return 1;",
                        f"    input_{stem}_{section}_{name}.{name}[{test_length}u] = '\\0';",
                        f"    input_{stem}_{section}_{name}.{name}[{nul_index}u] = '\\0';",
                        f"    if ({encode_name}(wire_{stem}_{section}_{name}, sizeof(wire_{stem}_{section}_{name}), &wire_length_{stem}_{section}_{name}, &input_{stem}_{section}_{name}) == 0) return 1;",
                        f"    input_{stem}_{section}_{name}.{name}[{nul_index}u] = {test_data}[{nul_index}];",
                        f"    wire_{stem}_{section}_{name}[{wire_offset + 4 + nul_index}u] = 0;",
                        f"    if ({decode_name}(wire_{stem}_{section}_{name}, wire_length_{stem}_{section}_{name}, &output_{stem}_{section}_{name}) == 0) return 1;",
                        f"    input_{stem}_{section}_{name}.{name}_length = {field['max_bytes']}u;",
                        f"    memset(input_{stem}_{section}_{name}.{name}, 'x', {field['max_bytes']}u);",
                        f"    input_{stem}_{section}_{name}.{name}[{field['max_bytes']}u] = '\\0';",
                        f"    if ({encode_name}(wire_{stem}_{section}_{name}, sizeof(wire_{stem}_{section}_{name}), &wire_length_{stem}_{section}_{name}, &input_{stem}_{section}_{name}) != 0 ||",
                        f"        wire_length_{stem}_{section}_{name} != {4 + field['max_bytes']}u) return 1;",
                    ]
    return "\n".join([
        "/* Generated by scripts/generate-api-schema.py; DO NOT EDIT. */",
        "#include \"corex_api_schema.h\"",
        "#include <stdio.h>",
        "#include <string.h>",
        "int main(void) {",
        f"    if (COREX_GENERATED_API_COUNT != {len(data['apis'])}u) return 1;",
        *checks,
        *codec_checks,
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
