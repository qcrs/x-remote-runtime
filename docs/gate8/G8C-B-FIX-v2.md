# G8C-B v2 build fix

The original Gate-8C-B server queried compute capability with:

```c
cuDeviceComputeCapability(...)
```

CoreX 4.4 still declares that function but marks it deprecated. Because the
server build intentionally uses `-Werror`, the deprecation warning prevented
the build.

v2 does not relax compiler warnings.

Instead it uses the Driver attribute path:

```c
cuDeviceGetAttribute(
    ...,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
    device);

cuDeviceGetAttribute(
    ...,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
    device);
```

through the same existing `ATTR(...)` validation helper used by the other
device capability fields.

No protocol, DTO, library ABI, or runtime semantic changes are made.

```text
CRX9 / VERSION 3
OP_GET_DEVICE_INFO = 26
COREX_REMOTE_CUDART_1.1
```

remain unchanged.
