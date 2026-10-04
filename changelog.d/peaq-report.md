### session · mastering — the master's damage, heard (PEAQ in windows against the chain at rest), the loudness range's change, and each walk's own progress; ABI 9

- **The damage is graded after the master, by a job of its own**: the master's job ends and delivers as before (the same
  events, the PCM, `Done`), its report's damage `Pending` and its line saying so; in the same unit the session starts the
  damage's job under a new id (`Session::damageJob()`, `Snapshot.damageJob` and `damageProgress`), announced by the new
  `damage` event (`DamageChange`: the master, the status, the reason) right after `Done`. It runs behind every other work,
  blocks no command, walks its phases `Reference` and `Damage`, and ends with its line and the `damage` event, `Ready` or
  `Unavailable`. `cancel` of its id stops it (`Cancelled`), a new `master` stops it (`MeasurementReason::Superseded`, a
  term of its own), `forget` of its master stops it without a line; `load` drops it with the masters. Its memory is
  admitted with the master (`DamageJob::bytes`) and lives in the room the master's job leaves.
- **`MasterReport.damage`** (`MasterDamage`, `[cost.damage]` in engine.toml): PEAQ Basic of the master against the same
  chain with its dynamics at rest — the master's topology without the dither, the glue in its exact bypass, the chain
  fed `referenceBelowDb` (60 dB) lower so the saturation stays linear and the limiter and its needles never reach the
  ceiling; the master rendered once more without its dither beside it. Same stages, oversamplers, delays and order: a
  chain at rest is Transparent (grade 5), not PEAQ's -2 on a structural difference. Both at 48 kHz from the source,
  through this core's own resampler of deterministic math where the source is not at 48 kHz (core's delivery resampler
  designs its kernel with the platform's libm), matched by one gain on the two integrated loudnesses. Graded in windows
  (10 s every 5 s): the worst window's verdict, BS.1116 grade (edges -0.5, -1.5, -2.5, -3.5 in engine.toml), ODG, DI
  and start, the windows graded, heard and ungraded, the share heard. A source the walks cannot run on still masters;
  the report says why there is no grade.
- **The loudness range's change**: the input's LRA (the programme report) against the master's, in LU and as a share of
  the input's — or the reason one is absent (a programme too short, an input still measured, an input with no range).
- **Facts 600-604** (a new range, the master's report continued): the damage's line (`MasterDamage` — grade, where the
  worst place starts, the share heard; `MasterDamageInaudible`; `MasterDamageUnmeasured` with its reason), the loudness
  range's (`MasterLraChange`, `MasterLraUnmeasured`); the BS.1116 grades are the `damageGrade` terms. ru and en.
- **`Phase.stepFraction`**: the current walk over the file, 0..1, a new count for every walk — every landing pass, the
  check, the crest's and the cost's reads, the measurement's stream, and the damage's two walks, which are phases of
  their own (`PhaseName::Reference`, `PhaseName::Damage`). Absent where a phase walks nothing.
- **Cost**: 60 s of 48 kHz stereo, natively on an M-series Mac — the master is delivered after 1.3 s, as in v0.13.0;
  the damage's job takes 3.6–3.7 s after it (the two chains' loudness 0.9 s, the graded walk 2.8 s, PEAQ's two windows
  in flight about 1.9 s of it).
- **ABI 9**: `FC_SESSION_ABI_VERSION` 9, the manifest appends the records (`MasterDamage`, `DamageChange`), the
  `damage` event, the snapshot's `damageJob` and `damageProgress`, the enums, the facts and terms; no entry point.
- **`tools/wasm/build.sh`**: BUILD-INFO names the felitronics-bands checkout beside the other three.
