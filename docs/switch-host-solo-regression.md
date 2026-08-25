# Switch Host/Solo regression diagnosis

The hardware trace captured on 2026-08-25 showed Host/Solo reaching normal game startup and then spending the remainder of the trace in mod activation. The Switch crash logger previously persisted several checkpoints per mod file. Each persisted checkpoint opens files and calls `fsdevCommitDevice("sdmc")`, multiplying SD-card synchronization cost across large mod sets.

The follow-up fix:

- keeps fine-grained per-file checkpoint text in RAM for exception reports, but skips durable per-file checkpoint writes by default;
- retains coarse durable startup, mod, host, and session checkpoints;
- restores Solo to the normal socket server lifecycle instead of the experimental socket-free shortcut;
- treats a failed host `network_init()` as a real startup failure and returns through standard network cleanup instead of continuing into the level transition;
- adds source-level regression tests for these invariants.

Define `SWITCH_VERBOSE_MOD_CHECKPOINTS` only for targeted deep-debug builds where the extra SD synchronization cost is acceptable.
