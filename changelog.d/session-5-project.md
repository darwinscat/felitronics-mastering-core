### session — canonical TOML projects and recovery by replay

`exportProject()` writes the target by name, manual mode and both device layers as canonical TOML. Only machine
values that differ from defaults and touched human fields are written; equal human edits retain their ownership.
`importProject()` accepts dotted keys and inline tables through felitronics-toml, validates the whole project with
positioned refusals, and declares a size-based allocation bound before parsing.

Unknown defaults are refused. The same core verifies its machine decisions; a different core preserves the saved
layer and publishes the count of differences. Owned snapshots and their generated JSON/TypeScript codec expose each
difference. The original core stamp remains with an imported layer, keeping subsequent exports replayable.

The suites cover canonical round trips, every import refusal without state changes, allocation budgets including
MSVC Debug, deterministic measurement slicing, permanent facade poison and recovery in a fresh session from the
source, measurement and last project text.
