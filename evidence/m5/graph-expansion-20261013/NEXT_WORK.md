# Expansion closeout

The original pending work has been processed. BASE_HEAD remains
0383270696516c003743e4784f3c5795168052f6; baseline support is 40 and new support
is 25. GOAL_AUDIT.md and BASELINE_COMPARISON.txt contain the final comparison.

Three user-object APIs remain GROUND_TRUTH_BLOCKED. A future implementation
needs client destructor notification across native graph/clone/exec releases,
failure handling and a non-gated NoDestructorSync lifecycle. These APIs do not
count as supported. General host-node graphs, broader capture and memory upload
remain bounded by the limitations in GRAPH_CAPTURE_MODEL.md and the ledger.
