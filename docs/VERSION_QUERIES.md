# M3-S4 scalar version queries

`cudaDriverGetVersion` returns the CoreX Driver scalar obtained from
`cuDriverGetVersion` (the validated MR-V100 environment reports 10020).
`cudaRuntimeGetVersion` returns the compatibility layer's declared ABI version
1.1.0 encoded as 11000. Both responses are fixed-width network-order `u32`
DTOs; no native CUDA or CoreX structs are serialized.
