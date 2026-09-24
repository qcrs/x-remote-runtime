# M3-S3 stream/event extensions

The CoreX probe verified `cuStreamCreateWithPriority`, `cuStreamGetFlags`,
`cuStreamGetPriority`, `cuEventElapsedTime`, and event creation with default,
blocking-sync, and disable-timing flags. The Runtime forwards these operations
through dedicated CRX9 opcodes 31–34 while preserving object-generation and
stale-handle validation.

`cudaStreamNonBlocking` is semantic: its client frontier is not merged with
the legacy default stream. Other stream flags and event interprocess mode are
rejected until separately proven. `cudaEventElapsedTime` requires both events
to have timing enabled; CoreX's error behavior is preserved as an explicit
Runtime invalid-value result for disabled timing.
