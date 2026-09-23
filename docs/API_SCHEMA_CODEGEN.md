# Human-reviewed API schema/codegen MVP

`schema/corex_api_schema.json` is the source of truth for three simple
control/query APIs. It explicitly records opcode, capability, wire size,
parameter direction/type/limit, and object reference. The generator emits
`include/generated/corex_api_schema.h` and a C test skeleton. The generated
header supplies bounded payload validation and handler/capability metadata;
the server checks those records at startup and applies the generated payload
validator before dispatch.

The generator has a closed vocabulary for this MVP. Unknown opcodes,
capabilities, types, directions, objects, missing fields, duplicate APIs, and
size mismatches fail generation rather than being guessed. Build regeneration
is deterministic and the schema integration test compares two independent
outputs byte-for-byte.
