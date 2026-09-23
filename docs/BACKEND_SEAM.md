# Minimal CoreX backend seam

`server/corex_backend.{h,c}` is a statically linked seam around the proven
CoreX Driver primitives used by the current server. It is deliberately not a
plugin or multi-backend framework. The protocol handlers retain object-table,
validation, status mapping, and CoreX metadata policy; the seam owns only the
direct Driver call boundary.

The seam covers device initialization/query, context lifetime and synchronize,
allocation/free and host staging, synchronous/asynchronous copies,
stream/event lifecycle and waits, module/function lookup, and typed kernel
launch. `runtime_server.c` contains no direct `cu*` calls; this invariant is
checked by `scripts/test-backend-seam.sh`.
