# Evidence policy

Source and build outputs have separate homes. Reproducible build output goes
in the ignored repository directories `build/`, `dist/`, `sdk-install/`, and
`clean-consumer/`. New validation runs go in `evidence/gate8d/runs/` and are
also ignored by Git.

Stable historical evidence is kept under `evidence/gate8d/` with its original
date or validation identity. Preserve a run there only when it is useful as a
reviewable record; do not use evidence directories as source trees.
