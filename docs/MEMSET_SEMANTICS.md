# M3-S2 memset semantics

`cudaMemset` and `cudaMemsetAsync` use byte semantics: the low eight bits of
`value` are sent in a fixed-width network-order payload and executed by
`cuMemsetD8` or `cuMemsetD8Async`. The client and server both validate the live
allocation range. Zero-byte operations succeed without connecting or
dereferencing the pointer.

The async operation is submitted to the resolved CoreX stream, including the
legacy default stream, and advances that stream's ordering frontier. It is not
implemented as a blocking host-side fallback. 2D/3D variants remain census
ground-truth work and are not exported by this slice.
