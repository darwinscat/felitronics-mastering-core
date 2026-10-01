### The session ABI starts a new base at v0.6.0

- **The manifest's new base** (owner, 01.10). `tools/session-abi-v1.txt` declares `base v0.6.0`;
  `tools/session-abi-append-only.mjs` holds append-only between manifests that declare the same base, refuses an older
  or dropped declaration, and accepts a new one only from its own list of authorised resets (v0.6.0 over an
  undeclared manifest, once). `FC_SESSION_ABI_VERSION` is 4: the pure kit, the EQ bands and
  their tick join version 4's surface. No project, snapshot or file of an older version exists, and the one consumer
  vendors the exact core, so the entries for such data leave. Live ids keep their numbers.
- **No decode defaults for snapshots and events.** Every snapshot and event field is required on decode (a missing key
  is `Invalid`); a semantically optional field stays nullable and present. The generator no longer asks a field appended
  to a frozen record for a default. Only a request's own optional fields (the query's `masterId`, `crossoverHz`,
  `fromHz`, `toHz`, `spectrum`; an edit's or a revert's tick and type) keep one; `SessionCapabilities.leanSummary` is
  required, as the 40-byte C record is. The bands' machine layer is the planner's alone.
- **The project file has no core stamp.** `core = …` is neither written nor read (a `core` key is `ProjectUnknownKey`);
  `Project.core`, `Rejection::ProjectCore` (27) and its fact 127 are gone, and an adopt no longer stamps anything.
  `MachineDifferences` (8) is the one fact for the differences between the file's machine layer and today's planner,
  published when there are any; `SameCoreMachineDifferences` (10) and `DefaultsConverted` (9) are gone.
- **The saturation's output knob is gone** (owner, 01.10): the landing sets the gain before the limiter, so any trim was
  undone. `SaturationFields.output` (field 3), its config (`outputDomain`, `outputDb`, `outputRange`, `outputStep`), the
  term `FieldSaturationOutput` (12) and `FC_SESSION_KIT_FIELD_SATURATION_OUTPUT` leave; the shaper's output stays at
  0 dB in the chain. The mix and mono bass's width stay. A `[saturation] output` key is `ProjectUnknownKey`.
- **Config**: the sound version of the `2026-10` defaults moves with the four removed keys (updated in place; the output
  was 0 dB, neutral, so no master moves). The recordings move with the config's version, the facts and the snapshot.
- **The high-pass curve's marks** (owner, 01.10): `bass4` at 41.2 Hz (E1), `bass5` at 30.87 Hz (B0), `sub808` at 28 Hz
  (its label fits on the plot); `kick` stays at 50. Labels only: the config's version moves, the sound version does not.
