# G8C-B v5 acceptance-test fix

This package inherits the final Gate-8C acceptance fix:

```text
intentional invalid-device error
→ verify direct return
→ cudaGetLastError()
→ verify sticky error
→ clear
→ cudaPeekAtLastError() == cudaSuccess
```

The test harness does not hard-code the numeric value of
`cudaErrorInvalidDevice`.
