# Gate 8D — Installable SDK / Clean Consumer Design Freeze

No new CUDA API or protocol change.

Frozen:

```text
CRX9 / VERSION 3
COREX_REMOTE_CUDART_1.1
libcorex_remote_cudart.so.1
```

A clean consumer must not need Runtime implementation sources or internal
headers. It may use CoreX public compiler/headers and the installed
`libcorex_remote_cudart.so.1`.

Installed SDK contains only the public shared library, public extension header,
pkg-config metadata, server executable, ABI surface file, and runtime manifest.
