### session · mastering — loudness is a request, not an order: the level landed on the source's gate, the limiter's budget (owner, 04.10)

- **The level landed on the source's gate** (`engine.toml [landing] onSourceGate = true`). The product landing puts on
  the target the louder of the file's BS.1770 reading and the master's mean block energy over the 400 ms blocks the
  SOURCE's BS.1770 gate admitted (absolute, then relative), not gated again. On its own gate, the quiet parts the drive
  lifts joined the master's relative gate and diluted the integrated loudness, so a dynamic song drove its loud part far
  past what that part needed alone (Test Tubes at −9: 24.8 dB of drive). The louder of the two keeps a chain that empties
  the gated blocks (a steep high-pass under a sub-bass source) from landing a file above the target. The gate is read on
  the source's `Loudness` `momentary` series, and the master's blocks take its readings by time through both grids (a
  hop is ten sub-hops of lround (0.01 fs) frames: 99.77 ms at 11025 Hz, 100 ms at 44.1 kHz). `LoudnessRequest` appends
  `landingOnSourceGate`, `sourceMomentaryLufs`, `sourceMomentaryCount`, `sourceMomentaryHopFrames`;
  `LandingSearch::landedLufs()` gives the level landed. The gate and each pass's level on it are stepped work,
  split-invariant. A source without the series whole (a sidecar's facts, rows refused for memory) lands on the master's
  own gate.
- **The limiter's budget by the target's loudness** (`[landing] limiterBudget = { quietDb = 4, middleDb = 7,
  loudDb = 10, middleLufs = [-10, -8] }`): the P95 of the limiter's gain reduction over its ACTIVE windows (input above
  `[cost] limiterActiveInputDb`, now passed to the request) may not pass 4 dB for a target below −10 LUFS, 7 dB from −10
  to −8, 10 dB louder; a person's edited loudness follows the same rule (`detail::limiterBudgetDb`). `LandingSearch`
  reads the request's `limiterGr` as a budget on `limiterActive`: a render over it is no candidate and is marked in its
  pass record, and the next drive is held under the lowest drive that broke it (by the secant of the excess). The
  landing is `TargetUnreachable` with `LimiterGainReduction` bound only on proof — a candidate within 0.25 dB of drive
  under that limit while the target is still above, or the limit holding the last drive chosen; otherwise the status is
  the search's own. Nothing in the mix is blamed for a budget hold: no hint.
- **The report's limiter line reads the active windows too** (`MasterCost::limiterP50Db`, `limiterP95Db`, from
  `limiterActiveGrWindows`), as its words always said: silence no longer waters it down (2 s of music at −10 read a P95
  of 7.0 dB; with 58 s of digital silence before them, 0).
- **The report says it.** `MasterLandingBudget` (600): «Цель −9,0 LUFS, сделано −9,7 LUFS: дальше лимитеру пришлось бы
  срезать больше 7 дБ (P95).» `MasterLandingOverBudget` (602), where no render kept the budget and the gentlest
  ceiling-safe one is delivered: «Цель …, сделано …: ни один вариант не уложился в бюджет лимитера 7 дБ (P95) — выдан
  самый мягкий, лимитер в нём срезает 8,4 дБ.» `MasterLandingGate` (601), in the miss's place where the level landed and
  the file's BS.1770 reading part by more than the tolerance: «По громкой части −9,0 LUFS, по стандарту файла −9,8 LUFS.»
  The solved verdict (88) names the level landed. Ids 600–699 are the landing's, continued (1–99 is full).
  `MasterReportText::landing` takes a `LandingMeasure` (the level landed, the budget, what the limiter takes over it);
  `MasterReportText::gate` is new.
- **`FC_SESSION_ABI_VERSION` 9**: the three facts, appended to the manifest; `SURFACE[9]` adds no entry point. No
  report, snapshot or event field is added.
- **Sound moves** for every master of a dynamic mix (its loud part lands where it would alone; the file reads quieter
  than the target) and every master the budget holds short. The `2026-10` defaults' sound version moves, updated in
  place as before (no project of those defaults has been saved). The contract recordings, the WAV contract and the
  report's limiter numbers in them are re-recorded; the WAV contract's safe master keeps its bytes, and its demanding
  miss (−5 LUFS under −6 dBTP) is now held by the budget, proven: 9.995 dB of active P95 at the drive kept, 10.1 one
  tenth of a dB above it. The event pins hold every job's events but the master's at their v0.13.0 values. The declared
  bytes of a master grow by the landing's state in `LandingSearch` (the request's four new fields, a handful of numbers,
  the twelve passes' excess over the budget): 200 bytes on wasm32 (`safePrice`, `latePrice`), 208 natively.
- With `onSourceGate = false` and a budget no render meets, a master is byte for byte the v0.13.0 one (WAV and
  landing, checked against a build of 81133bf on two corpus songs).
