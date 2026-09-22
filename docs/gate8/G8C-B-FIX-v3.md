# G8C-B v3 build fix

The v2 client ABI adapter used:

```c
strncpy(prop->name, info.name, sizeof(prop->name) - 1);
```

With GCC fortify diagnostics and `-Werror`, this triggers:

```text
-Wstringop-truncation
```

even though the decoded DTO already forces `info.name[255] = '\0'`.

v3 keeps strict warnings enabled and replaces `strncpy` with explicit bounded
byte copying:

```c
size_t name_bytes = sizeof(info.name);
if (name_bytes >= sizeof(prop->name))
    name_bytes = sizeof(prop->name) - 1;

memcpy(prop->name, info.name, name_bytes);
prop->name[name_bytes] = '\0';
```

This has deterministic truncation semantics and an explicit terminator.

No Runtime semantic, wire DTO, protocol, or exported ABI changes:

```text
CRX9 / VERSION 3
OP_GET_DEVICE_INFO = 26
COREX_REMOTE_CUDART_1.1
SONAME = libcorex_remote_cudart.so.1
```
