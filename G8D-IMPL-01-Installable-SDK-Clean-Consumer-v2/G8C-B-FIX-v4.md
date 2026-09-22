# G8C-B v4 scope-audit fix

The v3 implementation correctly uses a bounded field-level copy:

```c
memcpy(prop->name, info.name, name_bytes);
```

The old scope audit incorrectly rejected any source text beginning with
`memcpy(prop`, so it treated the safe `prop->name` copy as if the entire
`cudaDeviceProp` object were being copied.

v4 changes the audit to reject only a whole-struct destination:

```python
re.search(r"\bmemcpy\s*\(\s*prop\s*,", api) is None
```

It also adds a positive audit that requires field-wise population of the
client-side `cudaDeviceProp`.

No runtime source, wire DTO, protocol, server semantics, or library ABI changed:

```text
CRX9 / VERSION 3
OP_GET_DEVICE_INFO = 26
COREX_REMOTE_CUDART_1.1
```
