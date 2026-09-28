# Evidence Policy

Evidence is validation output, not a second source tree. Each new milestone
directory keeps the minimum auditable artifacts needed to reproduce the claim:

```text
evidence/<milestone>/<slice>/<timestamp>/
├── 00-RESULTS.txt
├── COMMANDS.txt
├── ENVIRONMENT.txt
├── probe.stdout.log        (when applicable)
├── probe.stderr.log        (when applicable)
├── integration.stdout.log  (when applicable)
├── integration.stderr.log  (when applicable)
└── MANIFEST.sha256
```

Generated headers, generated test sources, complete build trees, binaries, and
other canonical repository artifacts are not copied into new evidence. Their
repository-relative SHA-256 values belong in `MANIFEST.sha256`, together with
deterministic-generation and clean-tree results. Historical evidence remains
unchanged; this policy applies to M3-S8 and later.
