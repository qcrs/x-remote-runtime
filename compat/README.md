# CoreX Runtime compatibility ledger

`cuda-api-ledger.csv` is the canonical API census generated from the installed
CoreX Runtime header. Regenerate it with:

```bash
./scripts/census-corex-runtime-api.py \
  --corex /usr/local/corex-4.4.0 \
  --output compat/cuda-api-ledger.csv
```

The generator owns API names and current declarations. All semantic columns
(status, implementation class, backend ground truth, protocol/test needs,
priority, notes, and execution policy) are human-reviewed annotations and are
preserved from the existing ledger. A one-time seed may be supplied with
`--annotations`; routine regeneration must omit that option so later reviews
are not overwritten.

New header declarations default to `GROUND_TRUTH_REQUIRED` and
`BACKEND_CAPABILITY`. This is an explicit unclassified capability state, not a
claim of support and not an inference from the C signature.
