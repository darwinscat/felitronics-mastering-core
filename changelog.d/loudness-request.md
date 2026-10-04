### session · mastering — loudness is a request, not an order: the level landed on the source's gate, the limiter's budget (owner, 04.10)

- **The level landed on the source's gate** (`engine.toml [landing] onSourceGate = true`). The product landing puts on
  the target the master's mean block energy over the 400 ms blocks the SOURCE's BS.1770 gate admitted (absolute, then
  relative; read on the source's `Loudness` `momentary` series), not gated again. On its own gate, the quiet parts the
  drive lifts joined the master's relative gate and diluted the integrated loudness, so a dynamic song drove its loud
  part far past what that part needed alone (Test Tubes at −9: 24.8 dB of drive, the limiter's P95 15 dB). The file is
  still certified by BS.1770 (`achievedLufs`, `missLu`). `LoudnessRequest` appends `landingOnSourceGate`,
  `sourceMomentaryLufs`, `sourceMomentaryCount`; `LandingSearch::landedLufs()` gives the level landed. The gate and each
  pass's level on it are stepped work (two phases, a block a unit), split-invariant. A source without the series whole
  (a sidecar's facts, rows refused for memory) lands on the master's own gate.
- **The limiter's budget by the target's loudness** (`[landing] limiterBudget = { quietDb = 4, middleDb = 7,
  loudDb = 10, middleLufs = [-10, -8] }`): the P95 of the limiter's gain reduction over the programme's windows — the
  number the report's limiter line prints — may not pass 4 dB for a target below −10 LUFS, 7 dB from −10 to −8, 10 dB
  louder; a person's edited loudness follows the same rule (`detail::limiterBudgetDb`). `LandingSearch` reads the
  request's `limiterGr` as a budget: a render over it is no candidate and is marked in its pass record, the next drive
  is held under the lowest drive that broke it (by the secant of the excess), and the search ends once a candidate
  stands within 0.25 dB under that drive. Such a landing is `TargetUnreachable` with `LimiterGainReduction` bound —
  never `PassLimit` — and delivers the loudest render that kept the budget, or the gentlest ceiling-safe one where none
  did. Nothing in the mix is blamed for it: no hint.
- **The report says it.** `MasterLandingBudget` (600): «Цель −9,0 LUFS, сделано −9,7 LUFS: дальше лимитеру пришлось бы
  срезать больше 7 дБ (P95).» / "Target −9.0 LUFS, reached −9.7 LUFS: going further, the limiter would have to take off
  more than 7 dB (P95)." `MasterLandingGate` (601), in the miss's place where the level landed and the file's BS.1770
  reading part by more than the tolerance: «По громкой части −9,0 LUFS, по стандарту файла −9,8 LUFS.» The solved
  verdict (88) names the level landed. Ids 600–699 are the landing's, continued (1–99 is full).
  `MasterReportText::landing` takes a `LandingMeasure` (the level landed, the budget); `MasterReportText::gate` is new.
- **`FC_SESSION_ABI_VERSION` 9**: the two facts, appended to the manifest; `SURFACE[9]` adds no entry point. No report,
  snapshot or event field is added.
- **Sound moves** for every master of a dynamic mix (its loud part lands where it would alone; the file reads quieter
  than the target) and every master the budget holds short. The `2026-10` defaults' sound version moves, updated in
  place as before (no project of those defaults has been saved). The contract recordings, the WAV contract, the
  scenario's parity line and the event pins are re-recorded; the WAV contract's safe master keeps its bytes
  (`9a601c4c5e044b00`) and its demanding miss is now held by the budget (TargetUnreachable, LimiterGainReduction). The
  declared bytes of a master grow by the landing's state in `LandingSearch` — the request's three new fields, six
  numbers, the twelve passes' excess over the budget: 168 bytes on wasm32 (`safePrice`/`latePrice`), 176 natively.
- With `onSourceGate = false` and a budget no render meets, a master is byte for byte the v0.13.0 one (WAV and landing,
  checked against a build of 81133bf on two corpus songs): the change of sound is the two parts above and nothing else.
