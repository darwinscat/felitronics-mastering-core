<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Changelog

## v0.17.0 — 2026-10-05

### session — the glue is a parallel compressor: its mix, 40 % by default, a knob 0…100 % (owner, 05.10)

- **The glue's mix** — the compressed share of its output, the dry signal under it — is a field of the glue device:
  `[glue] mix` 0.4 wherever the glue is on (the machine's cd glue and a person's tick alike), a person's knob over
  `[glue] mixDomain` 0…1; the page's slider runs 0…1 by 0.2 (`mixRange`, `mixStep`: 0, 20, … 100 % — a finer step is
  placebo), the core takes any share in the domain as written. It is snapshotted and edited as the glue's other knob
  (`GlueFieldsValue/Touched.mix`, `editDevice`, `revertEdits`), kept by the project (`mix.hand` in its `[glue]`), reset
  by a change of target, said by the plan as it sounds (`GlueFinding.mix`), named by its term (`FieldGlueMix`:
  «Смешивание» / "Mix"), and parsed, snapped and travelled by the kit (`Kit::parse`, `travel`: 0…1 by 0.2).
- **At 100 % the glue is the downward compressor it was**, to the bit: `felitronics_session_glue_saturation_tests`
  pins v0.16.0's cd master of its test mix and gets it back with the mix set to 1. `[compressor] mix`, the 1 the glue
  was written at, is no longer read; it stays in `engine.toml` and `config::Compressor` until its removal is decided.
- **What moves**: every master whose glue engages — cd's (the machine glues there) and any master where a person
  ticks the glue — now compresses 40 % in parallel, so the glue takes less off the loud places; no other master
  moves: a glue out of the chain leaves the stage at 1, as before (a blend of the input with itself at another share
  would round it). The config's sound version moves (the config golden and the event pins are restated in place).
- **ABI 12** (`FC_SESSION_ABI_VERSION`): the glue's mix in the codec and the wire, the field term; no entry point.

### mastering · session — a max master's limiter budget search aims at its crossing: it settles in about half the passes (live, 05.10)

- **`LandingSearch::budgetClamp`** no longer steps back three times the excess with no render inside the budget, nor holds
  its secant within the middle three fifths: it aims at where the limiter's active-window statistic crosses the budget —
  from the lowest drive over it, back by its excess over the statistic's slope (about a dB per dB of drive above the
  knee, measured between the two lowest drives over it, else 0.75), and half the proof's resolution more; with a safe
  render inside the budget below, the chord of their excess (the statistic is convex in drive, so the chord crosses at
  or under the true crossing), and, once within the proof's resolution of that render, a test just under the resolution
  above it, so a render over the budget there proves it at once. A far, steep over end counts half each time a render
  lands inside the budget against it (the Illinois rule). The budget's proof (`kBudgetResolutionDb`) is unchanged.
- **Passes, 11 home mixes and 6 Cambridge-MT mixes** (median, before → after): max clean 10 → 5, max dense 8 → 6; every
  stop is still the budget's (or the −14 floor's). The site's live case (max clean, 11 passes, 44.5 s) is the shape this
  fixes: a 20 dB step back and the climb.
- **The max modes alone** (`LoudnessRequest::budgetAimsAtCrossing`, set by the session for a max master): a manual
  landing keeps the previous search to the bit (`previousBudgetClamp`) — its passes, its file, the WAV contract's PCM
  (9a601c4c5e044b00), the end-to-end scenario's digests and the report tests' damage pin are all unchanged.

## v0.16.0 — 2026-10-05

### session — the damage grade is the shell's to ask, a queue of its own; the max modes by ear, without the guard (owner, 04.10)

- **`gradeDamage { masterId }`** (new command): grades a master kept as a job of its own — its `damage` events Pending,
  then the result, its progress, a row of the snapshot's new `damageJobs` (job, master, `Waiting` or `Running`,
  progress). **No grade starts by itself** after a master any more: the shell asks, and decides when and which. Refused
  for a master not kept (`UnknownMaster`), a grade already asked (`DamageQueued`, fact 137) or a settled damage
  (`DamageSettled`, fact 136: graded, or no plan for the source), or past the queue's room of 32 (`DamageQueueFull`, fact
  138); a grade a cancel stopped, or a turn refused for room, may be asked again. A grade is a measurement, not an edit: a project's journal does not hold it.
- **One at a time, in the order asked; a new master never ends a grade** (owner: "it is a process of its own"). A master
  job has the pump first; a new master parks the running grade — its walks freed before the master allocates, so the
  master's price is as before — and the grade waits first, to start again from its beginning when its turn comes back.
  `Superseded` is no longer said. A waiting grade holds an entry alone (`DamageJobEntry`); what its walks need to start
  is its master's own (`MasterRows`: the walks' plan and the parameters it was delivered with). At its turn its walks'
  room is checked against the capacity, and refused there, said: `Unavailable`, `Memory`, its line.
- **Only its master's cause ends a grade, at once**: `cancel` of its job id (Cancelled: the cancel's fact, its line, the
  `damage` event; a running grade's walks freed — the cancel's `releasedBytes`) and `forget` of its master (new
  `MeasurementReason::MasterForgotten`, "the master was deleted", no line; the forget's `releasedBytes` are a running
  grade's walks), each with one last `damage` event and nothing of it after; the queue moves on. A new source ends every
  grade too (`MasterForgotten`): each grade's last word in the load's own batch, in queue order, stamped with the source
  it belonged to and ahead of anything of the new one — the queue's fixed room (32) keeps them within the batch.
- **The max modes by ear** (owner: both went too far while PEAQ heard nothing): the PEAQ guard is out of the master's
  loop — a max master is a landing and a delivery, its damage graded as any master's when the shell asks — and out of the
  config (`floorOdg`, `guardStepDb`, `guardSteps`); `MaxStop` `Guard`, `GuardUnmet`, `Unguarded` and facts 609, 610, 614
  stay in the ABI, no longer said, and a max master's stop comes from its landing alone. The budgets are 0.5 dB (clean)
  and 1.75 dB (dense) of the active P95 — the budget-curve ladder's medians: −13.05 / −11.22 LUFS on the home mixes,
  −12.04 / −10.53 on the Cambridge corpus; the max rows' manual starts are −13 and −11.
- **The max floor at −14 LUFS** (`[landing.max] floorLufs`; owner: "always pulled up to −14"), whichever target the mode
  sits on: a first landing whose file (BS.1770) stands under it by more than the landing's tolerance is landed again
  there, with no budget. Delivered on the floor, new `MaxStop::Floor`: its verdict the mode and the level alone
  (`MasterMaxFloor`, 616), its numbers in a line for the log only (`MasterMaxFloorDetail`, 617: where the first landing
  stopped, the floor, the P95 the limiter took beside the budget — no claim of what held the first landing). A floor out
  of reach says what held it (`TruePeak`, `Passes`), never `Floor`.
- **The gate's level, worded as what it is** (`MasterLandingGate`, 601, both languages): "−9.0 LUFS without the quiet
  parts, −9.8 LUFS by the file's standard reading." — it was "by the loud part", but the number is the master's mean over
  the blocks the source's gate admits, not its loudest place. No argument moves.
- **`FC_SESSION_ABI_VERSION` 11**: the manifest appends the command's wire record, the rejections and facts, the reason
  and its term, `DamageJobState`, `DamageJobEntry`, `Snapshot.damageJobs`, `MaxStop::Floor` and facts 616, 617;
  `SURFACE[11]` adds no entry point. Manual sound does not move (the WAV contract's PCM is unchanged); a max master's
  does, and the `2026-10` defaults' config and sound versions move with it, restated in place; the event pins, the text
  corpus and the contract recordings move with the grades no longer started by themselves (the end-to-end scenario, which
  asks its grade as a page does, keeps its facts digest bit for bit); a new contract scenario (`damage-grades`) masters
  twice and grades both, and the contract scripts gain `release master`.

## v0.15.0 — 2026-10-04

### session · mastering — the maximum loudness modes: Maximum · clean and Maximum · dense (owner, 04.10)

- **A loudness mode per target**: `manual` (every target until now, unchanged), `maxClean` or `maxDense`. A row of
  `targets.toml` may name it (`loudnessMode`, absent = manual); two targets are appended last, `maxClean` (Maximum ·
  clean) and `maxDense` (Maximum · dense) (streaming group, the allStreaming medium). Any target takes a mode by hand:
  `editTarget` `loudnessMode` (null gives the row's back), kept in the project's target layer, said in the snapshot as
  the mode in effect (`Snapshot.loudnessMode`).
- **A max mode asks for the loudest master, not a number**: the landing aims at `[landing.max] ceilingLufs` (−5) with
  the mode's limiter budget (clean 3 dB, dense 7 dB of the active P95), and a budget that holds it is success. Before
  the file is delivered a PEAQ guard grades the render with the damage's machine: its worst window must stay above the
  mode's floor (clean ODG −0.5, dense −1.5), or the drive steps back 1 dB at a time, graded before it is rendered,
  three steps at most; the gentlest graded is delivered where none passes, and says so. A render the guard could not
  grade (no damage plan for the source's rate, a walk refused or unavailable) is said unchecked, never passed; nothing
  heard is a pass. The guard's grade is the master's damage: no damage job follows a max master.
  `LoudnessRequest::peakClipMeasured` renders a step back in one pass with the peak the landing measured, and that pass
  proves the mode's budget itself: one that breaks it — or a landing no render of which kept it — is delivered as over
  the budget, never as held by it.
- **The report and the text**: `MasterReport.loudnessMode`, `maxStop` (`Budget`, `Guard`, `GuardUnmet`,
  `SearchCeiling`, `Passes`, `TruePeak`, `Unguarded`, `OverBudget`) and `guardSteps`; facts 608–615 (ru/en) name the
  mode, the level and what ended it, with no miss and no hint; the `loudnessMode` terms and the field term
  `FieldTargetLoudnessMode`.
- **Measured** on 11 home mixes and 6 Cambridge-MT mixes (medians): clean −10.12 / −9.64 LUFS at a worst ODG of −0.45 /
  −0.40, dense −8.81 / −8.03 LUFS at −1.29 / −1.00; the guard stepped back on 5 of the 11 home mixes in each mode, never
  on a Cambridge mix, and no master ran out of steps. Native and wasm give the same file, stop and grade.
- **Fixed — the reduction over the budget always prints above it** (`MasterLandingOverBudget` 602, since v0.14.0, and
  the new 615): it was printed to the nearest tenth, so 3.01 over a 3 dB budget read "3.0"; it is rounded up to a tenth
  and kept at least a tenth above the budget's own tenths (3.01 over 3 prints 3.1, 7.26 over 7.25 prints 7.3).
- **Fixed — a landing pass's loudness is the same bits on every row** (mastering): the solver's pass meter (every pass,
  the verify, the product landing's search and report) reads the integrated loudness on `core::DetMath`, not the
  system libm's log10, whose result on gcc + glibc stood an ulp apart from Apple's and emscripten's on some energies
  (two passes of the `max-mode` scenario's pass log on CI's gcc row). The pass meter now gives the same bits on every
  row; a stored loudness reading may move by an ulp on any row (the WAV contract's manual master reads −8.999992927225106
  LUFS where it read −8.999992927225104, its PLR with it), and the WAV contract's PCM is unchanged (9a601c4c5e044b00).
- **`FC_SESSION_ABI_VERSION` 10**: the manifest appends the enums, facts, terms and codec fields; `SURFACE[10]` adds no
  entry point. Existing targets' sound does not move (the WAV contract's PCM is unchanged); the `2026-10` defaults'
  config and sound versions move with the two rows and the `[landing.max]` table, restated in place (no project of those
  defaults has been saved); the event pins, the text corpus and the contract recordings move with them, and a new
  contract scenario (`max-mode`) masters both modes.

## v0.14.1 — 2026-10-04

### session — the loud target's limiter budget 7.5 dB, a budget in dB with a fraction (owner, 04.10)

- **`[landing] limiterBudget` takes a fraction**: `quietDb`, `middleDb` and `loudDb` are numbers of dB from 1 to 60 on a
  step of a quarter dB — the landing's own resolution of its proof (`NotOnStep` refuses 7.3), checked on the decimals as
  written (`Config::Landing` holds them as `double`; the order quiet ≤ middle ≤ loud still holds). The verdict that names
  the budget (`MasterLandingBudget` 600, `MasterLandingOverBudget` 602) prints it whole — up to two decimals, the
  trailing zeros dropped, decided on its hundredths as an integer: «7 дБ», «7,5 дБ», «7,25 дБ».
- **The loud step is 7.5 dB, was 10**: `limiterBudget = { quietDb = 4, middleDb = 7, loudDb = 7.5, middleLufs = [-10, -8] }`.
  1058 renders of 23 songs without a budget put the limiter's cost and the PEAQ damage breaking together near 4, 6.5 and
  8 dB of the active P95. Asked for −6 LUFS, the median song at 7.5 dB reaches −7.83 with an ODG of −1.46, 3 of 23
  "annoying"; at 10 dB it reached −7.36, −2.30 and 11 of 23. Targets at −8 LUFS and quieter do not move.
- **Sound moves** for a master louder than −8 LUFS whose limiter would take more than 7.5 dB: it is held there. The
  `2026-10` defaults' sound version moves, restated in place (no project of those defaults has been saved); the event
  pins, the damage pin of the demanding master and the contract recordings move with it.

## v0.14.0 — 2026-10-04

### session · mastering — loudness is a request, not an order: the level landed on the source's gate, the limiter's budget (owner, 04.10)

- **The level landed on the source's gate** (`engine.toml [landing] onSourceGate = true`). The product landing puts on
  the target the louder of the file's BS.1770 reading and the master's mean block energy over the 400 ms blocks the
  SOURCE's BS.1770 gate admitted (absolute, then relative), not gated again. On its own gate, the quiet parts the drive
  lifts joined the master's relative gate and diluted the integrated loudness, so a dynamic song drove its loud part far
  past what that part needed alone (Test Tubes at −9: 24.8 dB of drive). The louder of the two keeps a chain that empties
  the gated blocks (a steep high-pass under a sub-bass source) from landing a file above the target. The gate is read on
  the source's `Loudness` `momentary` series, and the master's blocks take its readings by time through both grids (a
  hop is ten sub-hops of lround (0.01 fs) frames: 99.77 ms at 11025 Hz, 100 ms at 44.1 kHz), a block past the series'
  last reading taking the last. Rates that are not whole hertz land on the master's own gate. `LoudnessRequest` appends
  `landingOnSourceGate`, `sourceMomentaryLufs`, `sourceMomentaryCount`, `sourceMomentaryHopFrames`;
  `LandingSearch::landedLufs()` gives the level landed. The gate and each pass's level on it are stepped work,
  split-invariant. A source without the series whole (a sidecar's facts, rows refused for memory) lands on the master's
  own gate.
- **The limiter's budget by the target's loudness** (`[landing] limiterBudget = { quietDb = 4, middleDb = 7,
  loudDb = 10, middleLufs = [-10, -8] }`): the P95 of the limiter's gain reduction over its ACTIVE windows (input above
  `[cost] limiterActiveInputDb`, now passed to the request) may not pass 4 dB for a target below −10 LUFS, 7 dB from −10
  to −8, 10 dB louder; a person's edited loudness follows the same rule (`detail::limiterBudgetDb`). `LandingSearch`
  reads the request's `limiterGr` as a budget on `limiterActive`: a render over it is no candidate and is marked in its
  pass record and in the session's pass log (`LandingPass::overBudget`, appended plain), and the next drive is held
  under the lowest drive that broke it (by the secant of the excess). The landing is `TargetUnreachable` with
  `LimiterGainReduction` bound only on proof — the render delivered stands within 0.25 dB of drive under the lowest
  drive marked over the budget while the target is still above; otherwise the status is the search's own (a pass limit
  stays `PassLimit`, with its hints). Nothing in the mix is blamed for a budget hold: no hint.
- **The report's limiter line reads the active windows too** (`MasterCost::limiterP50Db`, `limiterP95Db`, from
  `limiterActiveGrWindows`), as its words always said: silence no longer waters it down (2 s of music at −10 read a P95
  of 7.0 dB; with 58 s of digital silence before them, 0).
- **The report says it.** `MasterLandingBudget` (600): «Цель −9,0 LUFS, сделано −9,7 LUFS: дальше лимитеру пришлось бы
  срезать больше 7 дБ (P95).» `MasterLandingOverBudget` (602), where no render kept the budget and the gentlest
  ceiling-safe one is delivered: «Цель …, сделано …: ни один вариант не уложился в бюджет лимитера 7 дБ (P95) — выдан
  самый мягкий из опробованных, лимитер в нём срезает 8,4 дБ.» `MasterLandingGate` (601), in the miss's place where the
  level on the source's gate and the file's BS.1770 reading part by more than the tolerance, either way round: «По громкой части −9,0 LUFS, по стандарту файла −9,8 LUFS.»
  The solved verdict (88) names the level landed. Ids 600–699 are the landing's, continued (1–99 is full).
  `MasterReportText::landing` takes a `LandingMeasure` (the level landed, the budget, what the limiter takes over it);
  `MasterReportText::gate` is new.
- **`FC_SESSION_ABI_VERSION` 9**: the three facts, appended to the manifest; `SURFACE[9]` adds no entry point. No
  report, snapshot or event field is added.
- **Sound moves** for every master of a dynamic mix (its loud part lands where it would alone; the file reads quieter
  than the target) and every master the budget holds short. The `2026-10` defaults' sound version moves, updated in
  place as before (no project of those defaults has been saved). The contract recordings, the WAV contract and the
  report's limiter numbers in them are re-recorded; the WAV contract's safe master keeps its bytes, and its demanding
  miss (−5 LUFS under −6 dBTP) stays `PassLimit` with its twelve passes and hints: its passes over the budget stand far
  above the render delivered, which proves nothing. The event pins hold every job's events but the master's at their v0.13.0 values. The declared
  bytes of a master grow by the landing's state in `LandingSearch` (the request's four new fields, a handful of numbers,
  the twelve passes' excess over the budget): 208 bytes on wasm32 (`safePrice`, `latePrice`), 216 natively.
- With `onSourceGate = false` and a budget no render meets, a master is byte for byte the v0.13.0 one (WAV and
  landing, checked against a build of 81133bf on two corpus songs).

### session · mastering — the master's damage, heard (PEAQ in windows against the chain at rest), the loudness range's change, and each walk's own progress; ABI 9

- **The damage is graded after the master, by a job of its own**: the master's job ends and delivers as before (the same
  events, the PCM, `Done`), its report's damage `Pending` and its line saying so; in the same unit the session starts the
  damage's job under a new id (`Session::damageJob()`, `Snapshot.damageJob` and `damageProgress`), announced by the new
  `damage` event (`DamageChange`: the master, the status, the reason) right after `Done`. It runs behind every other work,
  blocks no command, walks its phases `Reference` and `Damage`, and ends with its line and the `damage` event, `Ready` or
  `Unavailable`. `cancel` of its id stops it (`Cancelled`; the cancel's fact first, the damage event last), a new
  `master` stops it (`MeasurementReason::Superseded`, a term of its own; the master is priced with the damage's bytes
  freed), `forget` of its master stops it without a line; `load` drops it with the masters; with no job id left it is not
  graded (`MeasurementReason::NoJobId`). Its memory is admitted with the master (`DamageJob::bytes`) and lives in the room
  the master's job leaves.
- **The reference is the whole `referenceBelowDb` lower** wherever the master's input gain stands: where the input gain's
  range (60 dB) stops short — any master with an input gain under 0 dB — a scale of the reference's input takes the rest.
- **The damage's line names what was graded**: the windows graded of all (603, 604) and the share heard of the graded
  ones, never the whole track.
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
- **Facts 603-607** (the master's report continued, after the landing's 600-602): the damage's line (`MasterDamage` —
  grade, where the worst place starts, the share heard; `MasterDamageInaudible`; `MasterDamageUnmeasured` with its
  reason), the loudness range's (`MasterLraChange`, `MasterLraUnmeasured`); the BS.1116 grades are the `damageGrade`
  terms. ru and en.
- **`Phase.stepFraction`**: the current walk over the file, 0..1, a new count for every walk — every landing pass, the
  check, the crest's and the cost's reads, the measurement's stream, and the damage's two walks, which are phases of
  their own (`PhaseName::Reference`, `PhaseName::Damage`). Absent where a phase walks nothing.
- **Cost**: 60 s of 48 kHz stereo, natively on an M-series Mac — the master is delivered after 1.3 s, as in v0.13.0;
  the damage's job takes 3.6–3.7 s after it (the two chains' loudness 0.9 s, the graded walk 2.8 s, PEAQ's two windows
  in flight about 1.9 s of it).
- **ABI 9**: `FC_SESSION_ABI_VERSION` 9, the manifest appends the records (`MasterDamage`, `DamageChange`), the
  `damage` event, the snapshot's `damageJob` and `damageProgress`, the enums, the facts and terms, and
  `fc_session_storage.releasedBytes` (the record grows from its 32-byte base to 40: the bytes a `master` frees first, so
  a shell's `liveBytes - releasedBytes + bytes` is the command's own check). A shell sends size 40 only to a module whose
  abi is 9 or more — an older one refuses it as too large; at size 32 nothing is written past it. No entry point.
- **`tools/wasm/build.sh`**: BUILD-INFO names the felitronics-bands checkout beside the other three.

### analysis_offline · tools — PEAQ Basic (ITU-R BS.1387): `analysis::Peaq` and `fcore_peaq`

- **`analysis::Peaq`** (`modules/analysis_offline/include/felitronics/analysis/Peaq.h`, target `felitronics::peaq`) —
  the Basic version of ITU-R BS.1387-2, written from the text of the Recommendation, with P. Kabal's examination for
  the unclear places: the FFT ear model (2048/1024, Hann, 92 dB SPL for a full-scale sine), the 109 bands of Table 6,
  internal noise, level-dependent spreading, forward masking, level and pattern adaptation, modulation, loudness, the
  eleven MOVs, the data boundary and the other frame selections of 5.2.4, stereo per channel with the binaural MFPD
  and ADB, and the 11-3-1 network. Out come the MOVs, the Distortion Index and the ODG.
- **Where the text decides, the text wins**: |INT(e)| steps (eq. 78), RelDistFrames at >= 1.5 dB, the bandwidth of a
  frame whose test is digitally silent (-inf levels compared as IEEE does), the loudness threshold as two marks per
  channel, the delayed averaging from the start of the measurement. **Where it leaves a choice, GstPEAQ decided** (run
  as a black box; its source neither copied nor read): the tail frame completed with zeros, EHS from line 1 at lags
  0..255 with the mean removed before the window (window-first gives EHS ~100x outside Table 13's range). On the 158
  pairs GstPEAQ grades (48 drum loops at graded damage, 99 masters of this core at nine loudness targets, 11 synthetic
  pairs) the ODG agrees to 0.0015 on average and 0.032 at worst; it differs where the text and GstPEAQ part: frames in
  which both programmes are silent, and a pair whose loud passages never overlap (GstPEAQ: nan).
- **Verdicts.** `Graded`; `Transparent` (ODG 0, the network's answer kept in `modelOdg`) when every channel's Total
  NMR is under -90 dB and its waveform error under -40 dB — on a master that left the programme alone GstPEAQ and this
  model both read EHS 52-67 and grade -2.1; `Undefined` (checked first) when a MOV averaged over no frame, which is
  then NaN; `NoSignal`, `NonFinite`, and `OutOfRange` for a sample past +18 dBFS, where the spreading stops being
  defined.
- **Determinism.** core::det, the core FFT (one transform per programme, so a test identical to its reference reads
  an error of exactly zero), fixed sums. `felitronics::peaq` carries -ffp-contract=off -fno-fast-math (MSVC
  /fp:precise) as INTERFACE options and the header refuses a unit built without them. prepare() allocates exactly
  `storageFor()`, process()/finish() nothing, any slicing gives the same bits.
- **Conformance (7.4) is not proven**: the 16 ITU test items are not openly available.
- **`fcore_peaq <ref.wav> <test.wav>`** (or `--f32le <rate> <channels> <ref> <test>`) prints the verdict, ODG, the
  network's ODG and DI, and the eleven MOVs. Other rates go to 48 kHz through `core::DeliveryResampler`, the same plan
  for both (its kernel uses the system libm, so only 48 kHz input is bit-identical across rows). Programmes of
  different lengths, and programmes past 2 GiB of working memory, are refused. `tools/wasm/build.sh` builds the same
  source as `fcpeaq.node.js`; CI diffs the two at 48 kHz. About 1.0 s per minute of stereo natively, 1.2 s in wasm.

## v0.13.0 — 2026-10-03

### session — the seven EQ knobs' geometry and names come from felitronics-bands

- **felitronics-bands v0.1.0 is the one source** of the named EQ bands (`tilt`, `low`, `body`, `mud`, `forward`,
  `brightness`, `air`). The root `CMakeLists.txt` fetches it by its pinned tag (`FELITRONICS_MASTERING_BANDS_TAG`), or
  takes a sibling `../felitronics-bands` / `FELITRONICS_MASTERING_BANDS_DIR`; a product that declares
  `felitronics_bands` first is the one used. `tools/wasm/build.sh` reads `FELITRONICS_BANDS_DIR` (CI passes the
  checkout its configure resolved).
- **Geometry.** `bands.toml` is embedded beside the config as its third document: tilt's pivot, low's corner and Q, and
  each band's type, centre or corner and Q are read from it by the config schema (`Document::Bands`) and built into the
  chain from it. `engine.toml` keeps only the knobs (`band`, `domain`, `normal`, `hard`, `step`); a `freqHz`, `q` or
  `type` written back there is an unknown key, and a band one document has and the other lacks is refused. The numbers
  are the ones engine.toml held: no master moves (the WAV contract and every PCM hash hold).
- **Versions.** `bands.toml` is part of both config versions (all of it is sound), so the config's versions moved with
  no number changed: the 2026-10 defaults' sound version is restated in place. `Config::bind` and
  `Config::versionsOf` take the bands document as a third argument; the two-argument forms use the one compiled in.
- **Names.** The catalog no longer writes the seven knobs' names (`terms.field` tiltDb, lowDb, bandsBody…bandsAir;
  `terms.device` tilt, low): each such term prints felitronics-bands' `text/<lang>.toml` `name`. The text gate refuses a
  copy in the catalog (`FromBands`), a language of the core whose bands text lacks a name, and a `languages.toml` that is
  not the catalog's languages (`LanguagesDiffer`). "EQ bands", the device of the five, stays in the catalog.

### targets — three broadcast standards

- `atsc` (ATSC A/85: −24 LKFS, −2 dBTP), `arib` (ARIB TR-B32: −24 LKFS, −1 dBTP) and `op59` (Free TV OP-59: −24 LKFS,
  −2 dBTP) join `ebu` in `targets.toml`, with its physics (mono bass 120 Hz, high-pass 32 Hz / 24 dB, 24 bit, the
  source's rate). A new row moves the config's versions; no existing target's master moves.

## v0.12.0 — 2026-10-03

### session · mastering — what the saturation took off the peaks, over time (03.10)

- **`QueryKind::SaturationShave`** (appended, 13): per master, on `LimiterGr`'s very buckets
  (`[firstFrame,lastFrame,minDb,maxDb,meanDb,samples,nonFinite]`), the dB the saturation — the chain's soft clipper —
  took off the peaks, ≥ 0. Per internal quantum (256 frames): the stage's input peak, aligned on its own latency, times
  its clean gain (dry share, the shape's slope at zero under the drive compensation, the output trim), against its
  output peak, floored at 0 — only what the curvature took, the pair the report's `saturationCutMaxDb` reads, so the
  largest bucket is that number. Base-rate peaks: the oversampled copies live inside the Saturator. The soft clipper
  sits after the glue and before the landing gain, the limiter and its peak clipper. A master whose soft clipper does
  not shape answers `Unavailable` with reason `NoSignal`, as `GlueGr` does.
- `MasteringChainTaps` appends `clipperShaveDb` (per baseband frame, under `frameCapacity`); `LoudnessSolution` appends
  `saturationTrace`, built on every render like the other three, and the solver's memory formulas count its trace and
  its tap. The session keeps the trace with the master's rows only where the soft clipper shapes (a bucket row in the
  master's declared memory). No output sample moves.
- **`FC_SESSION_ABI_VERSION` 8**: a shell that reads the new query kind knows it by this version.

### session — the tilt has one name everywhere (owner, 03.10)

- The tilt's knob as a refused field (`[terms.field] tiltDb`) is «Наклон» / «Tilt», the device's own word, where it was
  «Баланс тона» / «Tone balance». No other text said the old name. The text corpus pin does not move (its field
  argument is the high-pass slope); a refusal that names the tilt's field renders the new word.

## v0.11.0 — 2026-10-02

### session · mastering — the peak clipper takes at most its amount, at any landing gain; its manual travel to 6 dB (02.10)

- **The clipper keeps its promise** (owner, 02.10): a manual or machine cut of N dB set the threshold at need − N above
  the ceiling, a forecast from the input's true peak at the landing's first gain; the landing then added gain for what
  the limiter takes off the loudness and lowered its pass ceiling, and every dB of that went into the clipper — a manual
  2 dB at −9 LUFS / −1 dBTP took 5.7 dB off the peaks. `MasteringChainParams` appends `peakClipCutDb` and
  `peakClipPeakDb` (NaN by default: the threshold stays as given): the chain works the threshold out for the gain and
  ceiling it renders at, max(0, peak + gain − ceiling − cut), the clipper off where that passes the limiter's range.
  `LandingSearch` replaces the session's forecast peak with the one its first pass measured at the limiter's input
  (before the clip, on its oversampler); that pass steers the search and is never delivered or restored, so a clipper
  landing its first pass would have finished pays one render more (the three test masters: 3 passes, as before). The
  same 2 dB now takes 1.999999 dB. `fc_master` does not map the fields: its renders are bit for bit as before.
- **The manual cut runs 0…6 dB** (owner, 02.10), the whole of `manualDomain`, by 0.1 dB: `manualMaxDb = 6`;
  `fc_kit_travel` answers it.
- Sound moves for every master whose peak clipper cuts. The config's sound version moves (2026-10 updated in place: a
  person's knob travel, no machine value), and with it the event pins and the scenario's parity line.

## v0.10.0 — 2026-10-02

### session — the EQ curve's line wherever it is red; a tempo heard is named (02.10)

- **Beyond the norm, on the curve drawn** (owner, 02.10): `EqFinding` judges the curve `eqOnlyCurve` draws — tilt, low
  and the five EQ bands, the high-pass out — at the very point drawn, no longer the shelves alone; `device` is the one
  that gives most of it there, now Tilt, Low or Bands. Fact 504 (`EqOvershoot`) is raised by a hand on any EQ knob, a
  band's included, and said of that device — so tilt +1.5 with body −2.8 and brightness +3, or body −2.8 alone, now
  has its line, said of the bands. The kit's preview follows: `fc_kit_eq_curve` / `fc_kit_eq_curve_bands` give the
  peak of the same curve, its device bit `FC_SESSION_DEVICE_EQ_BANDS` where the bands give most.
- **A tempo heard with low confidence is named** (owner, 02.10): where the detector gives a tempo under
  `[compressor.tempo] trustedConfidence`, the release still follows `bpmWhenUnsure` (the threshold is unchanged), and
  the glue's line is the new fact 99 `GlueTempoUnsure` — «Темп {measured} измерен неуверенно — восстановление клея
  остаётся рассчитанным на {bpm}.» / “The tempo {measured} was measured with low confidence — the glue release stays
  set for {bpm}.” `GlueFinding.tempoUnsureBpm` (appended) carries the number. `tempoChoice.reason` is `None` for such
  a result — the measurement is whole, the rule declined it; `NoSignal` only where a ready result gave no tempo.
- No sound moves. The text corpus pin moves (one message more).

### session — the gentle tilt (02.10)

- **Tilt sounds on first-order shelves** (owner, 02.10): the tilt's band now asks felitronics-core for slope 6, its
  first-order tilt (`matched::lowShelf1` / `highShelf1`, in the felitronics-core release that ships them), and the
  curve the page draws (`eqCurve`, `eqOnlyCurve`, the EQ preview `fc_kit_eq_curve` and its finding) computes the same
  shelves with the deterministic maths. Pivot (1 kHz), knob range and meaning stay: low end −dB, top +dB, ends 2·dB
  apart. At +3 dB: 125 Hz −2.91 · 250 −2.64 · 500 −1.79 · 1k 0 · 2k +1.79 · 4k +2.65 · 8k +2.91 — was −3.00 · −2.98 ·
  −2.64 · 0 · +2.64 · +2.98 · +3.00, the whole 6 dB inside 500 Hz…2 kHz.
- The machine never sets a tilt, so no machine decision moves; a master with a hand tilt sounds different, and the EQ
  curves and findings with a tilt move. No config value changed, so neither config version nor sound version moves.
  The kit corpus pin (`KitTests.cpp`, `tools/wasm/session-check.mjs`) moves to the new curves.
- **On felitronics-core v0.59.0** (its first-order shelves). **`FC_SESSION_ABI_VERSION` 7.**

## v0.9.0 — 2026-10-02

### session — a master's loudness curves in the source's frames; the glue's gain reduction over time

- **`Momentary` and `ShortTerm` with a `masterId` are asked and named in the source's frames**: the source's own
  request with a `masterId` added answers the master's curve at the source's rows, so on A/B the master's curve lies
  under the original's. The range is refused past the source's end and `sampleRate` is the source's rate. At the
  source's rate nothing changes (the delivered audio is the source's length, the chain's latency cut off, delivered
  frame n is source frame n). A converted master (a target with its own `sampleRate`) used to answer in delivered
  frames; a row ending at delivered frame e is now named `round(e·source/delivery)` — the source's own row on rates
  that are whole multiples of 100 Hz. The readings are unchanged: the job's meter over the delivered audio, kept with
  the master and in its declared memory.
- **`QueryKind::GlueGr`** (appended, 12): the glue's — the compressor's — gain reduction over time from the delivered
  render, rows as `LimiterGr`'s (`[firstFrame,lastFrame,minDb,maxDb,meanDb,samples,nonFinite]`) on its very buckets.
  The trace is the one the landing search already took from the compressor's tap on every render; the session now
  keeps it with the master (a third bucket row in the master's declared memory, only where the glue compressed), so
  no output sample moves. A master whose glue did not compress answers `Unavailable` with reason `NoSignal`.
- **`FC_SESSION_ABI_VERSION` 6**: a shell that reads the new query kind and the master's curves in the source's frames knows them by this version.

### session — the owner's knob steps (02.10)

- **Mono bass "below"** (the crossover knob, `[monoBass] frequencyStep`) steps by 5 Hz, was 1; **"cut off the peaks"**
  (`[limiter.peakClipper] manualStepDb`) by 0.1 dB, was 0.5 — what `fc_kit_travel` gives a slider. A step is in the
  sound version (a typed value is placed on the knob's grid), so the `2026-10` defaults' sound version moves, updated in
  place as before (no project of those defaults was ever saved); no machine value and no WAV byte moves.
- The contract recordings are re-recorded from one clean `tools/wasm/build.sh`: the config's version, the measurement
  keys and the recipe's sound version move with the steps, the codec schema's hash with `GlueGr`, and the declared bytes
  of a master by 64 on wasm32 (the master's rows now own the glue's trace). The WAV bytes do not move
  (`9a601c4c5e044b00`).

### session — the EQ bands' knobs are coloured as tilt's and low's (owner, 02.10)

- `fc_kit_heat` answers `window = true` for the five band gains (fields 134–138, body, mud, forward, brightness, air):
  each `[bands.*]` entry has a `normal`, read as tilt's and low's are, out to its travel. `[eq]` holds no band norm, so
  the window is tilt's and low's ±1.5 dB; the mud band, a cut alone (travel −3…0), is normal down to −1.5 dB. A window
  is a hint: it is left out of the sound version (like `tilt.normal`) and no master moves; the config's `all` version
  and so the recorded `weightsVersion` and config version move.

## v0.8.0 — 2026-10-02

### session — Master as soon as the loudness and the true peak are known; edits before the machine has measured

- **`Measured1` comes with the loudness and the true peak** (owner, 02.10), before any of the source's own runs: the
  devices are placed then and a master is taken. The infra-low reading,
  forensics and stereo no longer stand before it.
- **The high-pass and mono bass read the low end.** The high-pass's cutoff needs the 120 Hz run, mono bass's tick the run
  at the target's crossover (`DevicePlan::needs`). With the panel open a master waits for them (`PlanPending`); with it
  hidden it waits in its own job, its recipe's machine fields placed again as each run ends — it gives the same WAV bytes
  as a master asked after everything ended. Mono bass's tick may change the chain's geometry when its run ends; the
  wait's end checks the demand again.
- **A field not measured yet** holds a placeholder (the floor, mono bass off), its bit in `DevicePlan::pending`
  (appended), the device's `heldBack` `Pending` (appended), and a waiting plan states the card's line
  `deviceUnmeasured` (fact 510): "not measured yet", with no number. The machine fills the field when its run ends.
- **Edits from placement on**: `editDevice` and `revertEdits` are taken in the table's Unplaced columns (a plan still
  waiting), and `plan.readOnly` now means the devices are not placed. A touched field is the person's; the machine never
  moves it. An import and `adoptMachine` still wait for the plan.
- **No file carries a placeholder**: while any device field is not measured yet, a project export and an import are
  refused with `PlanPending` — an import also while the file's own target has such a field on this source. The
  refusal's words no longer name the master: they say the measurements are still running.
- **Order**: the two low-end runs lead, then the findings; a needed tempo goes ahead of a finding, never ahead of a
  low-end run a device reads.
- **Progress** in the `Analyzers` phase moves by `[progress.analysis.weights]` (the infra-low run weighs `lowEnd120`):
  the stereo pass is a sliver, crest a long stretch.

## v0.7.1 — 2026-10-02

### session — a fact without its arguments is refused, never printed as its template

- **`fc_kit_text` / `Kit::text` refuse an incomplete fact**: one short of an argument its message needs, with one too
  many, or with one of another kind (or a unit, precision or term the renderer does not know) is
  `FC_SESSION_ERR_CONTRACT` (`CodecStatus::Invalid`), nothing written, `*written` untouched — as a malformed fact is.
  Before, it came back `OK` with the raw template (`{field}: …`). `Text::size`, `write` and `text` render such a fact
  as nothing (0 bytes, an empty string), and the new `Text::complete` tells it from a rendering; a fact whose plural or
  select argument does not match no longer renders its id. No new C entry point; `FC_SESSION_ABI_VERSION` stays 5.
- **A field's refusal whose field no table names says the command's refusal, whole**: `Text::rejected` gives
  `RejectedContract` (131, no argument) where it gave 112/113/114 without the `{field}` they need. The source was a
  master refused on its own numbers — a ready chain's non-finite parameters (`NotFinite`, field 255: the WAV contract's
  refusal scenario), a delivery rate or topology the chain does not admit (`OutOfDomain`) — and an import refusal on a
  key that is no field. The answer's `code` and `field` are unchanged; only its `fact` moves (WAV contract recording).
- The wasm session check renders every fact the WAV contract's scenarios carry (answers, events, snapshots) on the kit,
  in ru and en, and fails on one that does not render whole. The text corpus builds every fact complete (a term of its
  own group) and pins its new hash; the old renderer gives the same hash on it, so no complete fact's words moved.

## v0.7.0 — 2026-10-02

### session · mastering — refusals carry their facts, a null clears a target field, advice only for a person's value, the no-safe-render landing names its limit

- **A rejected answer carries its fact.** The wire's rejected answer appends `fact` (a `WireFact`, as events carry it)
  after `code`/`field`/`device`/`line`/`column`, and a contract answer carries `RejectedContract` (131). A field refused on
  its number is said with it: `RejectedOutOfDomainValue` (180: the field, the value, the domain's two bounds — the knob's,
  or 0 and half the source's rate for a Nyquist domain) and `RejectedNotOneOfValue` (181: a slope the knob does not
  take). `Answer` gains `value`, `low`, `high`. ru and en in the catalogue. `FC_SESSION_ABI_VERSION` 5.
- **`editTarget` with a null clears the field** (owner, 01.10): the person's number goes and the target row's sounds
  again, per field (`command::EditTarget::clear`). Before, a null was read as an untouched field and ignored. The project
  writes only the numbers a person holds, so the replay gives the same project. A field both set and cleared is
  `Contract`. The frozen wire fixture 29 now sends an empty edit (same answer, same effect); the null's own fixtures are
  appended after the import.
- **Advice beside a knob only for a person's value** (owner, 01.10): the high-pass's comfort window (500, 501), its slope
  (502, 503), mono bass's zones (505, 509) and the EQ shelves' norm (504) are said only where a person set the value
  each judges; the machine's own value — proposed now, or kept from a file — never raises them.
- **A landing whose measured renders all break the true-peak ceiling** (a caller's chain without the limiter) now ends
  `TargetUnreachable` with `binding = TruePeakCeiling` and says fact 89 with its limit, where it ended `Unavailable` and
  said nothing. It still delivers no file: no render kept the target's ceiling. Every loudness the product landing cannot
  hit (PassLimit, between) already returns its master at the closest level found.

### session · mastering — the plan states the glue out of the chain, the limiter's settings and the dither's shaping; the EQ-only curve; the saturation's curve in the kit; the snapshot sized in one walk

- **A glue out of the chain states its numbers** (unticked, or at 0 dB): `plan.glue` carries the ratio, knee and attack of
  the knob as it stands, the threshold and `p95DetectorDb` where the input has a P95, and the release at the decided
  tempo — or at `[compressor.tempo] bpmWhenUnsure`, since nothing measures the tempo of a glue out of the chain. It stays
  out: `state` is `Out` and no compressor gets them. A glue in the chain still waits for its tempo; an unavailable one
  states none.
- **The plan carries the limiter's own settings and the dither's shaping**: `LimiterFinding.releaseMs`, `dualRelease`,
  `slowReleaseMs`, `lookaheadMs`, `oversampling` (the factor as the limiter takes it) and `DitherFinding.shaping`
  (`DitherShaping`: none, weighted, psychoacoustic). writeLimiter reads the release and the shaping from the finding.
- **`eqOnlyCurve`** in the snapshot: the EQ stage without the high-pass (tilt, low, the EQ bands) on `eqCurve`'s points — a
  binary row like it, empty before placement.
- **`Kit::saturationCurve` / `fc_kit_saturation_curve`**: the saturation's transfer curve — 129 inputs from −1 to +1 and
  the chain's saturator settled on each, for the five types a person may pick, from the stage's own design arithmetic
  (`MasteringChain::clipperDesign`, which `clipperQuietGain` now reads too) on the parameters the session writes the stage
  with (`detail::clipperParams`). `FC_SESSION_KIT_SATURATION_POINTS` 129.
- **`Wire::snapshotBytes` walks the view once**: it no longer prints every row as text first; the bytes written are the
  same. On a 3-minute stereo source with one master: 168.8 ms → 10.2 ms.
- The appended plan and snapshot fields are plain — no decode default. `tools/session-wire-check.mjs` holds the manifest's
  base snapshot to the fields it carries and lets it lack the fields frozen after the base's section, by name.
- `FC_SESSION_ABI_VERSION` stays 5 (this release's batch): `SURFACE[5]` gains `_fc_kit_saturation_curve`.
- **A session is two blocks: the session object and its step's events** (`Session::createBytes` declares both). The
  event batch was most of the object, and slice 5 grew the object to 129320 B — past AddressSanitizer's largest primary
  size class with its 2 KiB redzone, so under the sanitizers every create became an mmap and the ABI suite's walk through
  16.7 million handle generations outran CI's hour. Each block now fits it (`felitronics_session_abi_tests` holds both).
- The snapshot sizing's timing test is a target of its own (`felitronics_session_snapshot_sizing_tests`), built without
  the session's flags: `<chrono>` under `/EHs-c- /we4530` is C4530 on MSVC.

### session · mastering — no render under the ceiling still delivers the file, marked

- **A landing whose measured renders all break the true-peak ceiling delivers its file** (owner, 01.10; a caller's chain
  without the limiter): `LandingSearch` keeps the gentlest render measured — the smallest overshoot of the ceiling, the
  nearer loudness on a tie — restores it on the reserved last pass when the search ended elsewhere, verifies it and
  delivers it. The verdict stays fact 89 with the true-peak ceiling named; the master is marked: `LandingSummary` and
  `MasterReport` gain `peaksAboveCeiling` (appended, plain fields), and `MasterPeaksAboveCeiling` (98, ru/en) says the
  true peak and the ceiling beside the verdict. The miss's line (which says the true peak held) is not said of it.
  Delivered means under the ceiling except in exactly this marked case — the decoder's invariants say so. A ceiling-safe
  landing is unchanged.

### analysis · session — hum is a line heard in the quiet passages too

- **A line present only while the music plays is music, not hum** (owner, 01.10). `HumDetector`'s quiet gate leaves the
  candidate bands out (so a hum cannot censor itself), which let a passage where a loud 50/60 Hz line plays over an
  otherwise quiet programme count as quiet — a 60 Hz musical tone was reported as hum. The detector now pools the
  frames where the whole programme is quiet, the candidate bands included, and a stationary line must show there too
  wherever that pool holds two frames: otherwise `valid = false, LineOnlyWithMusic` (11), which the session answers as
  the hum not found. Only those frames INSIDE the programme speak — strictly between its first and last frame above
  the gate — so a dithered lead-in, a tail after the hum's source stops or room tone at an edge never vetoes a hum loud
  enough to keep every frame it plays in above the gate; a pause inside the song does. A line through the pause, loud or
  faint, is still hum; where no still frame lies inside, nothing contradicts the line and it stands. The span is kept
  in the one walk over the frames: the detector holds two more rows of the stretch's width per channel (the pool and
  the still frames waiting for the next programme frame).

### ci · tests — gcc 14 on arm64

- **`felitronics_session_master_abi_tests` compiles with `-ffp-contract=off`, as the library does.** The unit runs its
  own `LandingSearch`, so it emits the same inline mastering code as the library; under the directory's
  `-ffp-contract=on`, gcc 14 — the first gcc that contracts under `on` (gcc 13 treats it as off) — fused multiply-adds
  into its copies on arm64, the linker kept those for the whole binary, and the library's landing re-measured its
  render differently and refused it: 7 checks red on gcc 14 arm64 only. The gcc 14 CI row now runs on
  `ubuntu-24.04-arm` too (`ubuntu-arm64-gcc-14`; the x86-64 row keeps its name).
- Two faults that row found on the way: a lambda's `Reader r` shadowing the command `r` in `Wire.cpp` (gcc's
  `-Wshadow`, an error), and a glue test asking the rules for a target named `vinyl` — the target is `lp` — whose empty
  `optional` was read as a row; the fixture now says an unknown target as a failed precondition.

### build — felitronics-core v0.58.0 is the pin

- **The pin moves to felitronics-core v0.58.0** (`FELITRONICS_MASTERING_FCORE_TAG`); the floor stays v0.57.0 (CMake,
  `tools/wasm/build.sh`): v0.58.0 adds the `codecgrid` module and `fftpffft`'s `PffftComplexFft`, and nothing here uses
  either. No sound moves: the contract recordings move in their version fields only, every audio row and WAV byte
  as it was.

## v0.6.0 — 2026-10-01

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

### contract — the site recordings come from one clean wasm build

- **Nine site recordings corrected** (`domains`, `load-measure`, `machine-layer`, `measurement-poison`, `memory`,
  `project-roundtrip`, `recovery`, `target-edits`, `two-sessions`): every scenario that places or poisons runs on the
  `contract-trap` module, and the committed recordings had been made with a trap module left from an older build. Its
  wasm32 memory budget was 80 bytes short — `measurementStorage.workspaceBytes` and `largestBlockBytes`, and
  `peakBytes`/`workPeakBytes` where the workspace sets the peak. Nothing else moved; these numbers are masked in the
  native/wasm comparison, so only the byte-for-byte recording check saw it, and only on a clean build.
- **`tools/wasm/build.sh` stamps what it built**: `contract-modules.sha256` beside the modules (production and
  `contract-trap`, for the release and the checked tier) names each module's sha256 and a digest of the sources, taken
  before the compile. `tools/contract/run.mjs` refuses a module pair the stamp does not name and a stamp made from other
  sources, with planted controls under `--controls`. The rebuild hint in the manifest now names
  `tools/wasm/build/fcsession.node.js`, the directory build.sh writes.

### session — the device cards' advice and the targets' notes are facts

- **The advice beside a knob.** `plan.facts` states, from the value as it sounds (hand over machine): the high-pass
  cutoff below or above the comfort window `[hpf] comfort` (`hpfBelowComfort` 500, `hpfAboveComfort` 501, with the
  window), its slope gentler or steeper than `slopesNormal` (`hpfSlopeGentle` 502, `hpfSlopeSteep` 503), the EQ curve of
  the shelves past `[eq] curve.warnDb` (`eqOvershoot` 504, "{db} at {hz}", said of tilt or low), mono bass outside every
  `[monoBass.zones]` zone (`monoBassOutsideZones` 505). `kPlanFacts` grows to 18; `plan.hpf.soundingSlope` is new.
- **The target's note.** targets.toml gains `[notes]` (measured, practice, noNormalisation; presentation — `sound` does
  not move, `all` does); the snapshot carries `targetNote` beside `target` (`targetMeasured` 506, `targetPractice` 507,
  `targetNoNormalisation` 508). Fact range 500–599 declared for the plan's advice and the targets' notes.

### session · tools — the EQ bands by hand, the glue's P95 point

- **The EQ bands** (`Device::Bands`, appended as device 8; `BandsFields`: `body`, `mud`, `forward`, `brightness`,
  `air`, dB): five static bands of the one EQ stage from the new `[bands]` config section — body a 160 Hz bell (Q 0.7),
  mud a 300 Hz bell (Q 0.8, a cut only: −6…0 dB), forward a 3 kHz bell (Q 0.7), brightness and air high shelves at 8 and
  12 kHz (Q 0.6) — in bands 3–7, each knob ±3 dB by 0.1 dB within a ±6 dB domain. A person's only: the machine leaves
  them at 0 dB on every target; a band at 0 dB is no band, so a master without them is the master made before (bit for
  bit, held against the previous `writeEq`). No dynamics and no norm advice yet; the curve's `warnDb` still judges tilt
  and low only.
- **The EQ bands' tick** (`BandsFields::on`, appended after the gains as field 5): the whole device in or out of the
  chain with its gains kept. The machine's layer is always on (also where the shell does not offer the bands); a person's
  untick writes all five slots as no band, so the stage, `eqCurve` and the master are those of a project without the
  bands, bit for bit; ticked on again, the same gains sound. The plan's `bands.on` is true where the device is on and any
  band is not 0. `EditDevice` / `RevertEdits` take `on` (a revert gives the machine's on); the project file writes
  `on.hand` after the gains.
- **Everything a device gets**: `EditDevice` / `RevertEdits` alternatives (a gain outside its domain refused
  `OutOfDomain` on its own field, named by the new terms `FieldBandsBody` … `FieldBandsAir`; the device's term
  `DeviceBands`, ru «Полосы EQ»: «Тело», «Грязь», «Вперёд», «Яркость», «Воздух»), the project file's `[bands]`, the
  codec (`Devices.bands`, `DevicePlans.bands`), the summed `eqCurve` and the kit's curve.
- **Capabilities**: `FC_SESSION_DEVICE_EQ_BANDS` (256); `FC_SESSION_DEVICES_ALL` keeps the eight devices before it, so a
  shell that does not know the bands is not offered them. C++ `kAllDevices` is all nine.
- **Kit**: the five gains are kit fields (`FC_SESSION_KIT_FIELD_BANDS_*`, travel, parse, no heat window);
  `fc_kit_eq_curve_bands` takes `FC_SESSION_KIT_EQ_BANDS_PARAMS` (13) — the seven of `fc_kit_eq_curve`, the five
  gains and the bands' tick (0/1; off draws no band, the gains still checked against their domain). In the v0.6.0 manifest base (`FC_SESSION_ABI_VERSION` 4).
- **The glue's P95 point**: `GlueFinding::p95DetectorDb`, the input's short-term P95 on the detector's scale (P95 +
  `[glue] detectorOverP95Db`) — the level the threshold stands on and where the static curve takes `upToDb`, for the
  transfer curve's dot. Set whenever the threshold is.
- **Config**: the sound version of the `2026-10` defaults moves with the added `[bands]` numbers (none changed; a
  2026-10 project has no band and sounds as before).

### session — the high-pass knob travels to 80 Hz; the machine's top stays 50 Hz

- **Owner, 01.10.** `[hpf] hzMax` is the knob's travel alone and goes to 80 Hz (a voice with a guitar from a microphone
  takes a cut that high); the new `[hpf] machineTopHz = 50` is the machine's top. `HpfCut::Top`, its cutoff and the
  report's "the cutoff stopped at 50 Hz" read the machine's top; no machine cutoff moves. The schema holds the new key
  required, finite and on the travel (above `hzMin`, at most `hzMax`). The comfort window is unchanged, so the knob's
  field is red from 50 to 80 Hz. A hand's cutoff keeps its domain (above 0, under the source's Nyquist).
- **Moved records:** the 2026-10 sound version (updated in place) and the pure kit's corpus pin (`KitTests.cpp`,
  `tools/wasm/session-check.mjs`): the high-pass travel's end and the positions along it.

### session — the master report states its verdict, its crest and honest bounds

- **The landing's verdict is a fact.** `MasterReportText::landing` states one fact per landing status, published with
  the master ahead of the miss: solved says the achieved loudness against the target and the landing's tolerance
  (`MasterLandingSolved`, 88 — never "hit" without numbers); unreachable, pass limit and between say why against the
  tolerance (`MasterLandingUnreachable` 89, `MasterLandingPassLimit` 90, `MasterLandingBetween` 91), the achieved number
  and the gap staying the miss's own line (`MasterLandingMiss`/`Above`, 11/23); a technical failure says so
  (`MasterLandingFailed`, 92). An unavailable or cancelled landing says none. A shell composes no verdict of its own.
- **The crest's line goes out once, from one source.** `MasterReportText::crest` is now published with the report when
  the job settles the crest (joined inside the job, or unavailable); a crest still pending is said by the late join, as
  before, and a settled one is never said twice.
- **The limiter promises no exact cut.** `{cut}` in `limiterShort`, `limiterBetween`, `limiterManual` and
  `masterVinylNeedlesDeparts` is a cap, `Bound::AtMost` ("≤ 1.5 dB"), and the lines say the landing decides how much;
  `limiterLittleNeed` names the need as the one at the target.
- **`DefaultsConverted` (9) is gone.** Nothing converts a project, so its shape, message and id are gone.
- The recordings move by the new facts alone: four contract scenarios gain the verdict and the report-time crest line
  in one event record each (with their hashes in the manifest); the event-test and text-corpus pins and the scenario's
  facts digest move with them. No ABI or version change.

### session — the observations speak for themselves; the loudest bass note is a reading

- **The snapshot carries each observation's line.** `SnapshotView::observationFacts` (a
  `BoundedList<ObservationFact, 17>`: the kind and its fact, held in place, no heap) carries what
  `ObservationText::facts` states of `observations`, in the order of `ObservationKind` — so a shell shows every finding
  without composing a sentence. One source: `ObservationText::facts` calls `ObservationText::fact` for each kind, and
  nothing else composes them. A kind found says its fact as before; a kind **not measured** now says so, and why —
  the new fact `ObservationUnmeasured` (437, "{name}: not measured — {reason}"); a kind measured and not found says
  nothing. Empty before a source; a new measurement states the lines anew.
- **The observations' words in the catalogue**, Russian first, then English: the 17 names (`terms.observation`), the
  handling (`terms.handledBy`: nothing, HPF, mono bass, by hand) and why a kind was not measured
  (`terms.measurementReason`: every `MeasurementReason` but `None`).
- **A reading style.** `ObservationStyle` (and the config's `Kind`) gain `Reading`, appended; `[observations.kinds]`
  says `loudestLowNote = "reading"` (owner decision: the loudest bass note is a number the file shows, it does not tint
  the Low end block). Nothing else in the style table moves.
- **On the wire** a line is `{"fact": WireFact, "kind": n}`; the schema learns the enum `ObservationKind`, and
  `observationFacts` is a field like any other. The decoder refuses an unknown fact id or
  kind. The config's version moves with `engine.toml`: the recordings move with it, with the snapshot JSON and the
  memory counts, and by nothing else.

### session — the owner's observation table: styles by nature and by size, severities, his words, and the hum not measured only where it could not listen

- **Styles** (`[observations.kinds]`, owner decision 01.10): polarity is an error; a dual-mono file, an input already
  limited and a spectral wall are warnings; the lowest occupied band is a reading. Four kinds take their style by size
  too — one function, `sized()`, with the thresholds in their own `[observations]` rows: `dcOffset` (a note under 1 % of
  full scale, a warning from `warningFrom` 0.01, an error from `errorFrom` 0.1), `bitsUnused` (judged by the effective
  depth, the container's bits less the low bits always zero: nothing at `depthBits` 24, a warning from `fromBitsShort`
  1 short, an error from `errorFromBitsShort` 8 short — every 16-bit mix), `infraLow` (a note from 2 %, a warning from
  `warningFrom` 0.05) and `hum` (a note, a warning from `warningFromSeverity` 0.5 when not doubtful). A size raises a
  style, never lowers it, and never raises a doubtful finding. `ObservationInputs` and `PlanInputs` carry the source's
  `bitDepth`.
- **Thresholds and severities**: `alreadyLimited` found under a PLR of 10.5 dB (was 8), its severity rising to full at
  `fullAtPlrDb` 7; `spectralWall` from 0.12 under Nyquist (was 0.2), its confidence full at a 40 dB drop (was 60);
  `wideBass` severity from 6 % to `fullAt` 30 %; `polarity` severity by the low band's correlation, 1 − 2 × its side share,
  from 0 to `fullAtLowCorrelation` −0.5; `hum` severity in dB of the line against the programme, `fromPowerDb` −60 to
  `fullAtPowerDb` −40 (replaces `fullAtPowerAgainstProgramme`). The config schema reads and orders every new key.
- **The hum's statuses**: a programme the detector listened to and found no steady line in is NOT FOUND — never quiet
  (`NoQuietStretch`), a comb without its base (`CombWithoutBase`), one quiet stretch or stretches too short with no base
  line in them. Not measured is left for what could not be measured: `ShorterThanWindow` → TooShort, `AllFramesHoled` →
  NonFinite, `InsufficientResolution` (and an unprepared report) → Unsupported, and a base line heard in too little quiet
  to tell whether it stands still → NoSignal. A line in any channel is the hum; without one, a channel that could not
  listen keeps the programme not measured. Wandering lines are read only from channels that judged.
- **New facts**, appended (439–445): `SourceDcNote`, `SourceTruncatedBits`, `SourceShallowMix`, `SourceLimitedBus`,
  `SourceLossy`, `SourceInfraLowNote`, `SourceInfraLowWarning` — the owner's words, Russian first. `SourceDualMono`
  says his sentence; `SourceUnusedBits`, `SourceLimited`, `SourceWall` and `SourceInfraLow` stay in the table, no longer
  said. The limiter's lines (`LimiterShort`/`Between`/`Manual`) and `MasterVinylNeedlesDeparts` read "the clipper will
  take {cut} off the peaks, the limiter the rest" — `{cut}` stays `Bound::AtMost`.
- **Builds**: the kit's slope check calls `std::floor` (on the object list everywhere) instead of `std::trunc`, which the
  Windows object gate refused; a kit test's `std::string_view` takes a `std::size_t` count (wasm32 `-Wshorten-64-to-32`).
- The config's version moves with `engine.toml`; with the previous observation rows put back the event pins are the
  previous ones, bit for bit. The recordings move with it.

### session — the owner's wording for the master's outcome and the advice beyond the norm; the landing names what held it; the machine keeps its own norm

- **The master's outcome** (owner wording 01.10): `MasterLandingSolved` (88) reads "the master reached X against a target
  of Y (tolerance ±Z)". `MasterLandingUnreachable` (89) names the limit that held it — a select on the new term group
  `landingLimit` (true-peak ceiling, limiter GR limit, PLR floor, LRA loss limit, the chain's gain bound, or "one of the
  constraints" where none was named). `MasterLandingBetween` (91) names the two nearest levels: "the target cannot be hit
  exactly: the nearest levels are A and B, both beyond the tolerance" (args `below`, `above`; the tolerance argument goes).
- **The landing carries what decided it**: `LandingSummary` gains `binding` (`LandingConstraint`, the solver's
  `LoudnessSolution::binding`, set for an unreachable landing only) and `belowLufs`/`aboveLufs` (the solver's bracket, for
  a between landing only). The decoder refuses a limit on another status and levels out of order. `MasterReportText::landing` takes the summary.
- **The advice beyond the norm** (owner wording 01.10): the high-pass slope gentler or steeper than usual (502, 503) and
  mono bass above every destination's zone (505, now `crossover`, `clubTo`, `vinylTo`). A crossover below every zone is its
  own fact, `MonoBassBelowZones` (509: `crossover`, `clubFrom`, `vinylFrom`).
- **The limiter's cap in words**: `LimiterShort`/`Between`/`Manual` and `MasterVinylNeedlesDeparts` say "no more than
  {cut}" themselves, so `{cut}` is `Bound::Exact` — the line never reads "no more than ≤ …".
- **DC offset per channel**: a stereo source's DC line names both channels, signed, like the reading —
  `SourceDcNoteStereo` (446) and `SourceDcStereo` (447), "L {left}, R {right}"; the observation carries them in
  `second`/`third` with `places` the channels read. A mono source keeps `SourceDcNote`/`SourceDc`.
- **Guard**: `theMachineKeepsItsOwnNorm` plans every target on the contract's fixture inputs and the suite's synthetic
  mixes and holds that the machine's own plan raises none of 502–505 and 509.

### session · tools — the pure kit: stateless answers for a shell's UI thread

- **`felitronics::session::Kit`** (`Kit.h`): a published fact as text (`Text::write` behind the codec's fact reader); a
  typed number read for a field (`Text::parse`, a bare number negative on a travel below zero, the knob's grid in decimal
  digits, the command's domain; the slope a whole multiple of 6); a knob's travel, position ↔ value on its grid, and heat
  against the config's window (`[edit]` green, `[hpf] comfort`, `[tilt]`/`[low] normal`); mono bass's zones; the EQ curve
  preview with the shelves' peak against `warnDb` (the EQ stage's own `writeEq` / `eqCurve` / `eqFinding`); a low-end dB
  curve from band energies. Allocation-free, no state.
- **C ABI** `fc_kit_text`, `fc_kit_parse`, `fc_kit_travel`, `fc_kit_position`, `fc_kit_value_at`, `fc_kit_heat`,
  `fc_kit_mono_zones`, `fc_kit_mono_zones_at`, `fc_kit_eq_curve`, `fc_kit_low_end_curve` and the `FC_SESSION_KIT_*`
  constants, appended to the v1 manifest; exported by the fcsession module. `FC_SESSION_ABI_VERSION` is unchanged (the
  release moves it).
- The plan's comfort and zones advice reads the kit's comparison; no fact, snapshot or sound changes.

### session — the readings are facts; the master's cost says its details

- **The readings are facts.** One table per place, keyed by `ReadingKind` (appended enum, 31 kinds): the snapshot's
  `readings` (`BoundedList<ReadingFact, 26>`) — the source's integrated loudness, true peak, LRA, PLR, DC offset per
  channel, lowest occupied band, exact PCM bits, correlation, burst events Mid/Side, hum, tempo and its confidence, the
  clipping's runs, longest run, clipped samples and sample peak, the low end's side share, the stereo windows and the
  crest's active blocks per band — stated by `ReadingText::source` from the measurement and following it; and a
  master's `MasterReport::readings` (`BoundedList<ReadingFact, 9>`) — achieved loudness, true peak, LRA, PLR, target,
  ceiling, gain, the landing's passes and the check passes — stated by `MasterReportText::readings` when the job settles
  the report. Each is `FactId::Value` with the core's unit and precision; the tempo's confidence is the new fact
  `TempoConfidence` (438) with a word of `terms.tempoConfidence`. The quantities' names are `terms.reading`, in the
  order of `ReadingKind` (`ReadingText::name`). A kind not measured has no entry. The need is not a reading.
- **The master's cost line by line.** Published with the report beside shape, impact and pumping, each only where its
  numbers were measured: the largest section shift and where it lies (`MasterCostSection`, 93), the sections compared
  (94), the limiter's median and P95 over the active windows (95), the shares it worked in and that were active (96),
  the impact loss of the four bands (97, also with a late crest join). Russian first, then English.
- **On the wire** a reading is `{"fact": WireFact, "kind": n}`; the schema learns the enum `ReadingKind`, and both
  lists are on the wire like any field. The decoder refuses an unknown kind. `BoundedList` moves to `Measurements.h` (no change of shape).
- The recordings move by the new lists and facts alone, with their hashes and the memory counts. The event pins move by
  the five cost lines alone (without them the old pins hold, checked). No ABI or version change.

### session — pre-release review fixes

- **The hum is not "too short" when the analyzer never ran.** A hum analyzer the session refused before any work (no
  price for the programme, or memory it may not take) carries no number; the hum and the wandering hum are now not
  measured with that refusal's own reason (`Memory`, `Unsupported`) instead of `TooShort`. The observation's codes are
  tied to `analysis::HumReason` by `static_assert`.
- **A between landing always delivers its master.** A `TargetBetweenAchievable` landing whose solver side were not a
  number keeps its verdict without `belowLufs`/`aboveLufs` and returns the file, instead of failing the job as a
  contract breach. No known path makes such a side: `LandingSearch` takes a side only from a finite, ceiling-safe pass
  (`felitronics_mastering_landing_search_tests` drives a between verdict and checks both).
- **The ABI gate's reset is authorised, not self-declared.** `tools/session-abi-append-only.mjs` accepts a new declared
  base only from its own list of authorised resets — exactly v0.6.0 over a manifest that declares none; any other base
  (v0.6.1, a removal under a fresh base) is refused. `tools/session-abi-v1.txt` now holds the full compiled surface of
  the v0.6.0 base (lines only added), and the manifest generator reads `uint32_t a, b;` as two fields, each with its
  own offset.
- **Two compatibility slots leave the C boundary** (owner, 01.10). `fc_session_measurement_storage` loses
  `reservedBytes` and `reserved`: 88 bytes, `workspaceBytes` at 24 and every later field 8 bytes earlier.
  `fc_session_capabilities`' base size is 40 with `leanSummary`: a 32-byte record is `STRUCT_TOO_SMALL` like any record
  below its base, and `FC_SESSION_CAPABILITIES_V1_BYTES` is gone. A build still accepts sizes from a record's base up to
  its own.
- **Docs**: the header and `docs/SESSION.md` state every record's base size; the `[bands]` comment in `engine.toml`
  describes the device's tick.

### session — the machine's saturation type is tape

- **The saturation's default type** (`[saturation] shape`) is `tape`, no longer `tanh` (owner, 01.10). The machine
  never raises the drive, so the type is heard once a person does: a hand drive with no type picked now sounds tape. A
  person still picks tanh, tube, transistor, transformer or tape by hand.
- **Config**: the sound version of the `2026-10` defaults moves with the shape (updated in place, before the first
  release that carries it).

## v0.5.0 — 2026-10-01

### session — the plan states its reasons; a contract with the panel open while the plan waits

- **The plan publishes its reasons.** `PlanView::facts` (a `BoundedList<PlanFact, 14>`: a fact and the device it is said
  of, held in place, no heap) carries what `PlanText` states of the plan's own findings, so a shell shows each device
  card's "why the machine set it so" without composing a line — and without the config numbers some of them carry
  (`shortSeconds`, `littleNeedDb`, `longPlrDb`, the dither's bit limit). A Ready plan states every line `PlanText`
  gives, in the order of `Device` and, within a device, of `PlanText`'s members: the high-pass; mono bass, its polarity
  warning, its coverage; the glue's refusal, fallback tempo and held release; the limiter, the warning beside a manual
  threshold, vinyl's ceiling, needles and top; the dither. Where the plan names what a master waits for (`awaited`,
  `awaitedBy`), the waiting fact `PlanWaiting` (29) — the device, the measurement, `awaitedFraction` as its percent —
  comes last and follows the progress between plans; a plan that is not Ready states that fact alone, or nothing. A new
  plan states its reasons anew. One source: the planner calls `PlanText`, nothing else composes them.
- **On the wire a plan's fact is every other fact's**: `{"device": n, "fact": WireFact}`, the fact written by the one
  encoder the events' facts now share (`FactId` and `args`, each argument whole). The codec and `.d.ts` come from the one
  schema, which learns `T[<=N]` (a bounded list: `ReadonlyArray<T>` on the wire) and `Fact` (`WireFact`). The decoder
  refuses a plan fact with an unknown id, an argument out of its range or a person's text (a snapshot holds none).
- **The session grows by one `PlanView` of facts** (3032 bytes on wasm32): the declared memory, the snapshot JSON and the
  recordings move with it, and by nothing else.
- **A new contract scenario, `plan-pending`** (native and wasm byte-identical): the panel open on cd while the tempo the
  glue reads is still measured — `plan.status` Pending, `awaited` / `awaitedBy` / `awaitedFraction`, the waiting fact
  alone; a master refused whole with 35 `PlanPending` and the snapshot unchanged; the waiting fact following the
  progress; then the plan Ready with its reasons, and the master taken.

### session · mastering — the saturation's type, picked by hand; felitronics-core v0.57.0; the bricks never include the session

- **felitronics-core v0.57.0** is the pin and the floor (CMake, `tools/wasm/build.sh`): its `WaveShaper::Shape` appends
  Tube = 4, Transistor = 5, Transformer = 6 and Tape = 7; Tanh, Atan, Cubic and Asym are bit-identical to v0.56.0.
- **The saturation device has a type** — `SaturationFields::type` (`SaturationType`, the core's shapes in their order and
  values), a person's field next to the knobs: tanh, tube, transistor, transformer or tape. The machine never picks one:
  its layer holds the config's `[saturation] shape` (tanh), so nothing sounds different until a person picks a type, and
  the defaults label stays `2026-10`. Atan, cubic and asym stay the config's (research): an edit or a project file that
  gives one by hand is refused whole, `NotOneOf` at the type's place (field 4), with the field's name «Тип сатурации» /
  "Saturation type". Like every device edit a pick sounds (the device's tick follows a touched field), is counted by the
  warning before a change of target (fact 81) and is taken back by that change. The project file writes
  `type.hand = "tape"`; the snapshot and the commands carry the type as a number (codec and `.d.ts` from the one schema;
  a snapshot or command without it decodes as tanh, untouched).
- **The type's names** are catalogue terms (`terms.saturationType`), Russian first: Мягкая / Soft, Ламповая / Tube,
  Транзисторная / Transistor, Трансформаторная / Transformer, Ленточная / Tape. The report's «сатурация срезала пики до N
  дБ» is measured on the stage as before, whatever the type.
- **fc_master**: `FC_SHAPE_TUBE` 4 … `FC_SHAPE_TAPE` 7; the shape's upper bound moves to 7 there, in `fc_session`'s master
  parameters and in the master job's check; `fcore_master clip.shape` takes the four new names.
- **The bricks never include the session.** `DeclaredBudget.h`, the declared-memory harness the mastering suites shared
  with the session's, moves to `tests/` (namespace `felitronics::declared`), and `tools/lint/check-brick-includes.mjs` —
  with its self-test and controls planted in a real file of every brick, run in CI — refuses any file under
  modules/mastering, analysis_offline, tempo or storage that includes a session header or source or links the
  session's target.

## v0.4.0 — 2026-09-30

### session — the planner places the devices; the plan says what they wait for

- **One planner, eight devices.** Every device (high-pass, mono bass, glue, saturation, tilt, limiter with its needles,
  dither, low) proposes its machine fields and says what it reads; the planner is the one place the machine decides, and
  the session places the devices when the first measurement ends — by the pump and by a sidecar's facts alike, where
  before only a fixture's seam did. The machine layer each target gets is the previous placement's, bit for bit.
- **The plan** (`SnapshotView::plan`): its status (None, Pending, Stopped, Ready, Unavailable), what the project's devices
  read and which of it has not ended, the one waited for first with the device that reads it and its progress, whether
  the panel is read-only, whether the machine layer is an imported file's, and each device's plan — what held it back
  and which fields its target decided. Its key hashes every input; the planner runs again only when it moves.
- **What a master waits for.** The tempo is waited for where a glue compresses (cd's machine glue, or a person's) — no
  longer on cdDynamic, whose glue is off — and the needles at the project's ceiling. With the panel open a master the
  session decides is refused (`Rejection::PlanPending`) until they have ended; with it hidden it is taken and measures
  them first, a needed tempo ahead of the optional findings. Its recipe is the project when it was asked for: a target or
  edit made while it waits no longer leaks into it. A stopped needles job ends the wait.
- **Device edits wait for the plan**: the table's Unplaced columns now also stand for a plan that waits (the panel
  read-only); export needs only the placement.
- **`adoptMachine`**, a new command: after an import the file's machine layer is kept, the planner's decisions for the
  file's target on this source are shown beside it (`machineDifferences`), and `adoptMachine` takes them, a person's
  layer kept.
- **The one EQ stage.** The high-pass, tilt and low each write their own band of the chain's EQ; the curve is drawn from
  the bands written — the previous curve bit for bit, core's response of those bands, and the engine's output on sines.
- Facts `PlanWaiting` and `RejectedPlanPending`, the terms of the needles and of the eight devices, in ru and en.

### session — the high-pass and mono bass decide from the first phase; defaults 2026-10

- **The high-pass stands always** (owner decisions 3.2–3.4): every target, a quiet input included, at max(the cutoff the
  sure lowest note allows, the floor — now 32 Hz on every target), never above 50 Hz. A note is sure at 2 dB over the
  occupancy line, 10 % of the frames, above 20 Hz and 3 s in all; a programme under 10 s is not searched. The cutoff is
  found on the chain's own high-pass response so that the note loses exactly the target's `noteLossDb` (1 dB, club 0.3),
  unrounded. "Nothing below the note" is gone, with `hpfAlways`, `hzDefault`, `nothingBelowNote` and `shortConfidence`.
- **Mono bass by the harm** (owner decision 3.5): the loss the low end takes folded to mono, where the bass sounds, at the
  target's crossover (120 Hz, vinyl 150). Under 1 dB placed; 1 to 3 dB placed with the number; above 3 dB left out, a
  person's switch obeyed and flagged (`plan.monoBass.againstMachine`); a loss it cannot weigh leaves it out with its
  reason. The stereo correlation no longer counts as a device switch.
- `PlanView::hpf` / `::monoBass`, the typed findings (the LowEnd result no longer publishes `hpfFloorRequired`, which nothing read); `HeldBack::Measured` and `::Quiet`; `PlanText` and ten facts (ru,
  en) for their report lines.
- **Defaults `2026-10`**: the numbers above change a master, so they are a new set.
- **The whole file's spectral wall** in the forensics result: `wall.*` without a channel index, beside `wall.*[c]` — the
  analyzer's aggregate, appended to the result's numbers.

### session — tilt and low: two devices of taste, the machine's layer stated

- **Tilt is a person's**: the machine leaves its tick off and its knob at 0 dB on every target (it used to tick it, at
  0 dB — the same sound).
- **A person's edit always sounds**, on every device: the tick is the person's own when they set one; otherwise on when
  any of the device's fields carries their value (`[tilt] db.hand = 3` sounds with no tick written; an explicit
  `on.hand = false` stays silent); otherwise the machine's. The snapshot shows the tick as it sounds and where it came
  from: `plan.devices.<device>.on` and `.tick` (Machine, Hand, Touched).
- **Low** is offered on every target; the machine ticks it only as vinyl's +0.5 dB, and no longer on an input below
  −55 LUFS, where it places no device (`HeldBack::Quiet`) and leaves the number on the knob.
- The geometry is stated and held (technical decision 3О10): tilt about 1 kHz, −dB below and +dB above; low a static
  shelf at 80 Hz, Q 0.6. Both knobs take ±6 dB as written, and the band carries the value bit for bit.

### session · mastering — glue and saturation from the normalised input; what each did, measured on its stage

- **One system of levels.** The input reaches the chain brought to the reference loudness by one gain, which the
  landing search adds once (`plan.inputGainDb`); the glue's threshold and the saturation's drive are read in that system,
  so one mix exported louder or quieter gets the same compressor and the same shaper. `writeDynamics` writes both
  stages — every field named — and touches neither gain of the chain.
- **Glue, "up to N dB"**, is the loss on the loud places: the core's own static curve takes exactly N dB at the input's
  short-term P95. One smooth formula over 0…6 dB — no step table, no rounding (`[glue] step`, `[compressor] roundToMs`
  and `roundToDb` are gone). The slider's 0…3 is a hint, the core takes 0…6; the machine sets it on cd alone, 2.6, and
  the config refuses a machine value above the slider's top. The release follows a tempo measured with confidence (0.5
  and up), 120 BPM otherwise, inside 50…500 ms; a clamp and the fallback are facts. **The calibration is the knob's new
  meaning** — 2.6 dB on cd is ratio 1.74 with the threshold 6.1 dB under the loud places — so the config's sound version moves;
  the defaults stay `2026-10`, not yet released.
- **Without a P95** the glue is unavailable to the machine and to a person alike, with its reason
  (`plan.glue.state == Unavailable`, fact `glueUnavailable`): the person's tick and value are kept, no threshold is
  invented, and the master is made without it.
- **Saturation** is the chain's tanh stage, never the machine's; its drive is the knob's at the input's true peak,
  k = 10^(drive/20) − 1, with no trial render.
- **The report** (`MasterCost`, additive; older snapshots decode the four as `NotImplemented`): `glueP95Db` and
  `glueMaxDb` — the compressor's own gain reduction; `saturationCutMaxDb` and `saturationCutUsualDb` — the largest and
  the usual cut of peaks over the loudest 5 % of the stage's quanta (`[saturation] cut.loudShare`), measured on the
  stage against its gain on a quiet sound, never the fall of the chain's true peak. Facts `masterGlue` and
  `masterSaturation` are published with the master's cost; the completion unit's event batch grew by two.
- `mastering::MasteringChain::clipperPeaks (loudShare, out)` and `clipperQuietGain()`: the soft clipper counts the peak
  of its input against the peak of its output every whole quantum — counters only, cleared by `reset()`; the audio is
  the bits it was.

### session — the first review of the plan: no wait for ever, sentences of what sounds, a calibrated glue

- **A dropped waiting master hands the needles back.** A hidden master waiting at its own ceiling and then cancelled —
  or stopped with the source's measurement, or ended by a contract fault — left the needles at its ceiling: the
  project's plan waited for ever and the open panel refused every master. The needles are asked for again at the
  project's ceiling. A master stopped with the measurement now says so under its own job's id (fact `cancelled`).
- **A sentence states what sounds.** `plan.hpf` and `plan.monoBass` are the planner's proposal; each says whether that
  proposal is what sounds (`sounding`: Proposal, Hand, File, Off; `soundingHz`). `PlanText` gives the planner's
  reasons only for its own values; a person's or a file's value is named as that (`hpfByHand`, `hpfKept`, `hpfOff`,
  `monoBassByHand`, `monoBassKept`).
- **The glue's scale is calibrated to music** (`[glue] detectorOverP95Db = 1.5`): the threshold stands 1.5 dB above
  P95 + the travel's offset, so that the knob is the gain reduction really taken on the loud places — the median of 11
  mixes: 1.27 dB at 1.25, 2.61 at 2.6, 3.02 at 3 (it was 1.68, 3.24, 3.70). cd's 2.6 dB now takes 2.6 dB. The sound
  version of defaults `2026-10` moves.
- **Mono bass is not judged on a part of a piece**: where the low-end run holds only the first 10.9 minutes the verdict
  is `Incomplete` — left out, with its own sentence (`monoBassIncomplete`).
- `[stages]` is documented as the defaults layer of the ticks, not the chain's topology.

### session — the high-pass: a lowest band that is not sure means the floor

The lowest band that was on at all decides the high-pass's note, and that band alone (the owner's decision as written):
on in under 10 % of the frames, under 2 dB over the duty line or under 3 s in all, it is not a note and the cutoff is the
target's floor. A band under the 10 % line used to be skipped and the next band taken — a rare 808 under a 55 Hz bass
put the cutoff at 41 Hz and took 24 dB off it; it is 32 Hz now. The floor's sentence says the lowest band was not
certain. The sound version of defaults `2026-10` is unchanged (no number moved); the machine's cutoff on such mixes is
lower.

### session — a load ends a waiting master; mono bass is weighed over the part the reading holds

- **A `load` (or `loadMeasured`) under a hidden waiting master** cleared the master but left its wait standing: the
  needles were then asked for at the ceiling of an empty recipe and the new source's plan waited for ever. The wait ends
  with every other master state.
- **Mono bass on a piece longer than the low-end reading holds** (10.9 minutes) is weighed over the part it holds and
  placed by that loss — it stands unless a measured loss rules against it. The finding carries `coveredSeconds` of
  `pieceSeconds`, and `PlanText::monoBassCoverage` says so (`monoBassPartWeighed`). The verdict `Incomplete` is gone.
- **The polarity warning stands beside a by-hand sentence** (`PlanText::monoBassPolarity`): mono bass switched on at a
  person's crossover against an opposite-polarity verdict is named as the person's and still warned about.

### session · mastering — the limiter with its needles, the dither and the whole plan sounding; the observations

- **A master the session decides is rendered.** `master` without a ready chain (version 0) takes its chain from the
  project's devices (`writeChain`: `writeEq`, `writeDynamics`, `writeLimiter`, mono bass) — the topology from the
  devices as they sound, never `[stages]`; the fixed geometry stated in `[chain]`, `[compressor] lookaheadMs,
  sidechainHpfHz` and `[limiter] lookaheadMs`. Its demand is declared by the command whole; a master that waits for its
  measurements fixes its chain when the wait ends (a heap too small then: a memory error under its own id). It is,
  sample for sample, the previous path's master of the same chain handed in ready.
- **The needles' classes** (owner decisions 3.6, 3.7): short needles lose up to 3 dB off their peaks, the ones between
  up to 1.5 dB, long, bassy, already-limited and clipped material not cut (`plan.limiter`, `NeedlesClass`, `NeedlesWhy`);
  a clipped source is ten confirmed clips a minute (`[limiter.peakClipper] clippedPerMinute`). A person's manual
  threshold — or a threshold turned alone — sounds over every refusal, with the machine's reason beside it. "The same
  ceiling" is decided by the bits in `requestNeedles` as in the plan.
- **The dither** by the delivery's format alone: 16 bits, weighted TPDF from the stated seed, blanked after 4096 zero
  samples (`[dither] seed, autoBlank, autoBlankSamples`); a person's off rounds without noise; a tick above 16 bits is
  kept without effect (`plan.dither`).
- **Vinyl** (`[targets] vinyl`, lp; owner decision 3.12): the machine never above the medium's ceiling and never cutting
  needles; a person's hand is obeyed and warned; the master's report says "ready for cutting", what the file shows and
  what it cannot (`MasterReport::medium`), and the plan carries the constant note about the top above 16 kHz
  (`[observations] vinylTop`).
- **A quiet input** (owner decision 3.13), strictly under −55 LUFS: the gain, the ceiling, the format's dither and the
  high-pass at its floor, nothing else of the machine's; the report says so.
- **The observations** (`snapshot().observations`, additive): DC, unused bits, silence at the edges, clips by place, a
  quiet or short input, an input already limited, a spectral wall, the low notes, infra-low, wide bass, polarity,
  sibilance (shown whatever the de-esser), hum — found, not found and not measured apart, with confidence, severity, the
  config's style, `handledBy` and the hypotheses marked. They change nothing.
- The wide-bass warning no longer tells a person to switch mono bass off: "mono bass will gather it if it is on".
- The defaults stay `2026-10` (not yet released); their sound version moves (the chain's geometry, the dither's noise,
  the clipped-source bound, lp marked as vinyl). The event batch grew by four (a master's medium and input lines).

### session — the lowest occupied band is published with its sureness

- The low end's reading of its lowest occupied band (`lowestOccupiedMidi`, `lowestOccupiedHz`, `lowestOccupiedDuty`,
  `lowestOccupiedMarginDb`) is published wherever a band was on at all, no longer withheld when it stands under the
  2 dB margin; two numbers are added beside it — `lowestOccupiedSure` (it stands the margin and is resolved) and
  `lowestOccupiedResolved`. A shell shows an unsure band as such ("35 Hz, unsure"); the observation says it too
  (`sourceLowestBandUnsure`, doubtful). The planner's rule is unchanged: an unsure lowest band gives the high-pass floor.

### session — the warning before a change of target is the core's fact

- **Manual device edits (N) will be reset.** A change of target always resets a person's device edits (owner decision,
  28.09); a shell warns before it sends `SetTarget`, and the sentence is the core's: `SnapshotText::targetChange` gives
  `targetChangeResetsEdits` with the snapshot's `handFieldCount`, and nothing when no field carries a person's value.
  The shell shows it in its confirmation and sends `SetTarget` on confirm; a cancel sends nothing. No new command, no
  new refusal code, no wire change. The catalogue gains the message in ru and en (plural on the count), and the pinned
  rendering corpus moves with it.

### session — adopting the machine stamps this core

- **`AdoptMachine` stamps the project with this release.** After an import from another core, adopting the planner's
  machine layer left `project.core` at the file's release, though this release placed the layer: the master's recipe
  and the exported file named the old core. The command now stamps `core` before it places, as every other placement
  does, so the adopted project's master is the fresh session's master, recipe included. Held by
  `ScenarioTests.cpp:theFilesMachine`.

### session — the scenario end to end, in independent sessions

- **One scenario, one master** (`felitronics_session_scenario_tests`): load, measure, plan, a hand with a 1.25 dB edit,
  master, export, import into another session, master — the same snapshot JSON, recipe, facts, defaults and sound
  versions, PCM and WAV bytes. On the same file: a same-core and a foreign-core import keep the file's machine layer and
  sound alike; `AdoptMachine` gives the fresh session's master; step budgets of 1, 7 and a large one give the same bytes;
  a cancelled waiting master and a stale needles job publish nothing; import → master → release → forget, repeated with
  refusals between, keeps `liveBytes` flat after the first cycle.
- **Native and wasm, one scenario.** The test prints its input hash, its versions and the digests of the plan, the facts,
  the PCM and the WAV; `tools/wasm/scenario-parity.mjs` checks the wasm run against the native lines.

### session — a clipped source does not wait for its needles, the vinyl report names what departs, a half-measured finding is not measured

- **A clipped source is ruled out before its needles.** `needlesAnswer` tests the confirmed clips per minute before it
  asks for the needles, so a source clipped ≥ 10 times a minute answers `Clipped` while the needles job still runs, and
  the limiter's plan reads no needles for `Clipped` or `LittleNeed` (decision 3.1: wait only for what is needed).
- **The vinyl report says what departs, with numbers.** `MasterMedium` keeps `ready` and gains one flag per rule of the
  medium (`foldDeparts`, `cutDeparts`, `ceilingDeparts`, `needlesDeparts`) with the chain's numbers and the rule's;
  `MasterReportText::vinylDepartures` gives one line per departed rule, published after `masterVinylDeparts`. New facts,
  appended: `masterVinylNoFold` (82), `masterVinylFoldDeparts` (83), `masterVinylNoHighPass` (84),
  `masterVinylHighPassDeparts` (85), `masterVinylCeilingDeparts` (86), `masterVinylNeedlesDeparts` (87), ru and en.
  The codec's `MasterMedium` grows by the new fields (generated); the pinned rendering corpus moves.
- **Not measured is not "not found" and not 0.** Polarity with only one of its two readings, edge silence with one edge
  invalid, a DC offset read on one channel of two, and "already limited" with a PLR that is not dense and the clips not
  counted are now `NotMeasured` with the missing input's reason; the edge line no longer prints an unmeasured edge as
  "0.0 s". The unused low bits already required every channel.
- **The vinyl needles warning keys on the medium.** `needlesAgainstMedium` reads `vinyl`, not `noClipper`.
- **Docs.** `SESSION.md`: cancelling the needles job of a waiting master renders it without the clipper
  (`limiterUnmeasured`); cancelling the source's measurement it waits for ends it.

### session — the needles' clipper cuts an amount off the peaks

- **The two class numbers are amounts, not thresholds** (owner, 30.09): the peak clipper takes at most 3 dB (short
  needles) or 1.5 dB (between) off the peaks and the limiter does the rest. Its threshold stands max(0, need − cut)
  above the ceiling, the need the input's (`plan.limiter.needDb`). Before, 3 and 1.5 dB were thresholds above the
  ceiling, so on a need of 7.9 dB the cautious class clipped 6.4 dB and the short one 4.9. A person's `manual X` is
  X dB off the peaks the same way. Where the need is not known, or need − cut lies beyond the limiter's 12 dB working
  range, the clipper stays off rather than cut more than its amount.
- `[limiter.peakClipper] shortOverDb`, `betweenOverDb` → `shortCutDb`, `betweenCutDb` (`config::PeakClipper` likewise);
  `plan.limiter.overDb`, `proposedOverDb` and the vinyl report's `overDb` carry the amount. The limiter's lines and the
  vinyl departure say "up to X off the peaks" (`{cut}`), the knob reads "Cut off the peaks".
- The defaults stay `2026-10` (not yet released); their sound version moves.
- The core carries only the current defaults table: the empty `previous` slot is gone.

### session — a master read by queries, a lean summary, and the landing's facts published

Additions to `fc_session`, version 2 (`FC_SESSION_ABI_VERSION`); a version-1 shell sees the bytes it saw.

- **The landing's miss and its hints are facts the core publishes** with the master's completion, keyed to the master
  (`jobId`), ahead of the cost's lines: `masterLandingMiss` / `masterLandingAbove` and the `masterHint*` facts. A shell
  composes none of them from the report's fields. The completion unit's event batch grew by three.
- **A lean summary.** `Capabilities::leanSummary` (C: `leanSummary`, appended to `fc_session_capabilities` — a 32-byte
  version-1 record leaves it off): summaries keep every master's scalars, pass log and cost sections and leave out its
  traces, crest rows and mask and waveform buckets, saying `masterRowsIncluded=false`. Measured on 12-second masters:
  1.28 MB of JSON with one master and 8.6 MB with seven by default; 62 KB and 88 KB lean. The full snapshot is
  unchanged. The facade now measures a capabilities record by the size it states (an output placed right behind a
  32-byte record is no overlap).
- **`QueryKind::MasterReport`**: one kept master whole — the record a full snapshot carries, rows included — through
  the existing `query_bytes` / `query_size` / `query_copy`; its memory is declared (`QueryView::master`).
- **`Momentary` and `ShortTerm` with a `masterId`** answer the master's own loudness curves (they were refused): the
  job's meter over the delivered audio, a row per 100 ms — bit for bit the delivered audio measured as a source. The
  job keeps the momentary series beside the short-term one.
- **`QueryKind::MasterAxes`**: the master's retained waveform buckets in the source Waveform's shape — Mid and Side,
  envelope and three band energies, rows of 13 — also on a caller-supplied chunk. `MasterWaveform` is unchanged.
  `analysis::WaveformStream` is the waveform index's per-sample arithmetic for a stream cut where the caller says.
- **`MeasurementQuery::spectrum`** (`Density` by default, `Energy`): `LowSpectrum` can answer the band's whole energy
  as well as its energy per hertz; which is which is stated in `Queries.h`.
- `Recipe` and `Kept` moved from `Session.h` to `LandingResult.h` (still reached through `Session.h`).

### session — an import accepts only the current defaults label

## v0.3.0 — 2026-09-30

### session — bounded WAV delivery

Completed safe masters expose an additive, token-guarded WAV size and bounded slice copy. The shell can
assemble one owned RIFF image before releasing session PCM and repeat downloads without rerendering or
adding dither. The pure planar writer supports PCM16 and PCM24, with the installed core's PCM
grid and odd-chunk padding. Native and wasm checks decode the written bytes and verify the delivered
reference true peak; cancelled, unavailable and unsafe jobs expose no file, while a safe twelve-pass miss does.
Session measures the selected PCM on its delivery grid, and the decoded WAV equals the listening PCM
sample for sample; direct solver calls keep legacy float output by default.

### session — measured master report and comparable crest rows

The ready master now retains delivered LUFS, reference true peak, suitable PLR/LRA, gain from the source, signed target miss, ceiling safety and measured mix hints. A missing LRA carries a reason. Linear band-crest rows use the source's explicit rate, hop and corners and preserve its activity mask when comparable. Rate conversion adds one source-rate check pass with the winning settings, using bounded scratch while the delivered PCM stays intact. Snapshot and the generated codec own the compact rows; older v1 snapshots decode without a report.

### session — ready master job and owned audio transfer

An additive `fc_session_master` entry takes frozen, versioned mastering topology and parameters with a source and revision fence. The session prices the job before allocating, drives the twelve-pass landing search in work units, and keeps the selected delivery PCM with its recipe and compact measurement rows. A target edit leaves the running recipe intact. Cancellation removes only unfinished work; a new source invalidates old transfers. `fc_session_master_audio_*` reports shape, copies, provides a scoped wasm view for one independent `ArrayBuffer` copy, and releases session PCM explicitly. The frozen v1 JSON Master command retains its original behavior for existing callers; new callers use the ready entry. Appended snapshot and codec fields carry conservative decode defaults.

### mastering · session — aligned limiter and K13 traces

The limiter's existing oversampled K13 clipper now exposes its reduction as a time tap without changing
audio or aggregate readings. Delivered mastering stores limiter and clipper min/max/mean rows on one
frame grid, including the final drain. Session snapshots own both series, the generated codec decodes
their absence in older snapshots, and `fc_session_query_*` accepts bounded master trace queries.
The pinned felitronics-core v0.56.0 release supplies the companion tap and the WAV grid and pad implementation.

### mastering · session — saved loudness landing search

The product landing now has one budget of at most twelve measured renders. It stops at a measured hit, otherwise
keeps the closest output whose delivered reference true peak holds the target ceiling. Its resumable source survey,
render, statistics, gates, final copy and independent remeasurement expose cancellation without publishing an
unverified file. Results carry the achieved level, miss, typed reason hints, deterministic work and full pass log.
Session planning keeps −18 LUFS source normalization separate from search gain and records a source-rate impact pass
when delivery changes rate. The optional landing result extends the generated session codec and older v1 snapshots
continue to decode. Differential tests compare the saved pass executor with the previous whole pass bit for bit.

### mastering — resumable delivery render and bounded PCM

Delivery conversion and offline rendering now retain their source, latency, tap, and drain cursors across bounded steps.
The whole APIs use those same steps. A delivery job prepares its chain, renderer, and converter in separate units; a
zero budget does no work, and cancellation followed by a new job resets every state. Delivered searches reconvert the
original source on each pass and feed SRC blocks into the chain using the caller's output buffer, removing the extra
complete converted programme. `storageFor` declares the source, output, and conservative workspace live set in 64-bit
arithmetic. Split, replay, allocation, and prior-path bit comparisons cover the new render path.

### analysis · tempo · mastering — allocation budgets in MSVC Debug

C ABI memory queries now cover MSVC iterator-debugging allocations as well as audio storage. Private prepared buffers use exact owned arrays; containers required by core APIs retain their iterator debugging and publish their construction costs. Mastering configuration keeps the original re-preparation and publishes its Debug temporaries, and a failed solution-record allocation reaches the ABI's poison handler in Debug too.

Windows CI now runs the four allocation-accounting ABI suites, container-construction controls, and five session suites in Debug; the job refuses a selection missing any of the ten suites. Release keeps the complete test run, including the session handle-generation walk.

### tools — `fcore_master` names the three v13 statuses

The CLI printed `?` for `FC_ERR_BAND_NOT_DYNAMIC`, `FC_ERR_BAND_INERT` and `FC_ERR_LANE_OFF` (16–18, ABI v13): its
status-name switch stopped at 15, and only a `-Wswitch` warning said so. It now prints `BAND_NOT_DYNAMIC`,
`BAND_INERT` and `LANE_OFF`, and the build is free of that warning. The names are one list, which the switch reads
and `fcore_master layout` prints, and `layout-check.mjs` holds it against the header's `fc_status` in both
directions, so the next code missed fails ctest instead of warning on the rows whose compiler warns at all. The
domains suite's own status table had the same gap; it is a switch with no `default` now.

### session · analysis — live loudness and resumable programme reports

Live measurements now stream fixed-grid loudness points and source clipping evidence through session events. Programme reports retain native values and absence reasons, with resumable finalization and reference true-peak drainage. Cancelling a measurement retains audio and results; continueMeasurement resumes unfinished work. The v1 transport adds metadata and fields without changing existing layouts or row columns.

### session — preparation budgets and event progress

Preparation admission and retained storage include allocator overhead, including MSVC Debug padding after a capacity reduction. Events carry their emitting job's phase and work through completion and cancellation. Loudness and report preparation initialize duration-sized stores as observations arrive, preserving the deterministic arithmetic and allowing cancellation between bounded pump units.

### session — source-sized measurement admission

Measurement admission counts the instruments the pump runs, retained rows, one snapshot copy and the web transport buffers. Unscheduled analyzer maxima and optional plain-JSON exports no longer inflate every load. Source replacement counts the old and new PCM lifetimes separately, and preparation checks the largest individual allocation. Allocation checks cover four seconds, one minute and ten minutes, including MSVC Debug.

Needles are re-measured for the current target over retained source PCM, in cancellable pump units with a separate declared memory demand. Owned aggregate results carry source and ceiling identity, complete histograms and explicit native run-list truncation. Source-wide measurement demand no longer reserves an excursion index. The additive C query and generated snapshot fields expose job demand, progress and results.

Needles started by a target change after phase two can be cancelled in Measured2. The all-target measurement plan now budgets and owns a separate 150 Hz low-end reading alongside 120 Hz and infra-low. Windows Debug includes the needles allocation suite, and CI compares retained needles fixtures across native, Wasm and checked Wasm, including codec bytes. The MSVC object gate admits UCRT's exact float classifier alongside its existing double classifier.

### session · owned measurement results and memory demand

Analyzer results now have owned numbers, arrays, grids, completeness and missing-value reasons. Snapshot copies
survive workspace release and source replacement. Cancellation retains PCM, completed results and saved progress;
identical loads reuse them. Measurement keys include actual parameters and versions independently of the target.

Load preflight includes native analyzer preparation, result copies, serialization, previous source storage and
allocator allowance. Waveform and stereo columns expose native storage declarations; vector construction and
oversampler padding are included in the other affected declarations. The excursion-index budget remains explicitly
unknown. Live analyzer execution is not connected to the existing deterministic pump.

The v1 ABI gains `fc_session_measurement_bytes` and its size-prefixed output record. Snapshot fields, measurement
events and catalog facts are additive; codec and TypeScript declarations still share one generator. Allocation
coverage includes MSVC Debug through the existing budget job.

### Measured master cost and waveform

- Report source-masked five-band CVaR95 impact loss, gain-removed short-term shape, source-derived sections, limiter GR and pumping as measured numbers with reasons.
- Keep compact delivered waveform rows and answer zoom from retained buckets or an explicitly supplied PCM chunk after transfer.
- Keep K2 tonal change explicitly unmeasured, and publish new cost facts in Russian and English.
- Compute final cost distributions through bounded session steps and retain their cursors across calls.

### session · mastering — the delivery format is the target's

A master's WAV takes the frozen target's format: its rate (the source's when the target's `sampleRate` is 0) and its
bit depth, PCM16 or PCM24. A ready `deliveryRateHz` or `deliveryBits` (the C facade's `fc_master_config.deliveryRate`
and `fc_master_params.dither.bits`) of 0 takes it, the same value restates it, and any other value is refused before
any allocation with the appended `Rejection::DeliveryFormat` and a fact naming the target's depth and rate. cd and
cdDynamic now deliver 44.1 kHz and take the source-rate crest pass. The float32 and 20-bit delivery paths are gone. The WAV of a 16-bit target is now 16-bit even though the completed job's working recipe has
been handed to the kept master. A late crest join waits for a resumed source measurement instead of settling it as
cancelled, and a crest joined inside the job is not published again. Every facade output that could land in the
retained master PCM is fenced. The landing search's first ceiling uses the caller's `LoudnessRequest::ceilingMarginDb`
(required for a product landing; Session passes engine.toml's), and a best pass at the end of the budget is delivered
without a second render. felitronics-core v0.56.0 is consumed as released, without a build-time patch, and is held to its version floor and the
presence of the K13 tap and the WAV writer, no longer to whole-file hashes of either header.

Late source crest results now join retained master rows by source, recipe, master id and complete analysis grid after PCM release. The WAV contract recording includes a replayable late join after wasm heap growth.

The complete Solve memory gate now checks load, measurement, mastering, snapshots, bounded WAV export, transfer, release, cancellation, replacement and repeated jobs on native and wasm. The replayable WAV contract includes the safe master's declared price and observed wasm heap growth, with browser-owned playback and file buffers listed separately.

### session · tools — `felitronics::session`: the mastering session, compiled, with its laws held by the build

A new module, `felitronics::session` (`<felitronics/session/Session.h>`): the object a shell talks to. It is an empty
`Session` — `create()` (which returns the session or the reason there is none), destruction by its owner, `version()`,
`coreVersion()`, `createBytes()` (the demand of `create()`) and `checkFloatingPointEnvironment()`. It keeps no state. The
laws it is held to, which of felitronics-core's apply and which do not, and what holds each one: `docs/SESSION.md`.

**The repository's first compiled target.** A STATIC library whose sources are compiled with PRIVATE flags in one
`SHELL:` group — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti` (`modules/session/build-flags.txt`, which
`tools/wasm/build.sh` reads too); MSVC 2022+ `/fp:precise /EHs-c- /EHa- /GR- /we4530 /we4541` — and CMake's option
de-duplication cannot drop a member of the group. Other compilers, clang-cl, icx-cl and icx are refused at configure
time. Every translation unit — the library's and its C boundary's — includes `src/BuildGuards.h` first, which refuses
exceptions, RTTI, fast-math, `FLT_EVAL_METHOD` other than 0 and, on MSVC, `/fp:contract`. The compile line is read back
from `compile_commands.json` — this build's, and a consumer's that states `-ffp-contract=off` first: the library's group
must be the last word on floating point, exceptions and RTTI (a per-source `-ffp-model=fast` or `-fno-honor-nans` after
it is red), with no forced include, pass-through, plugin or flag file in any spelling, separated or joined, entries
matched by file and read with or without CMake's `output` field (skipped on MSVC, whose guards hold it from inside each
unit). The library's sources are also compiled with `-ffp-contract=fast` and with fast-math licences ahead of its own
options and must answer IEEE-754 anyway — the targets that carry those positive controls are built optimised, so a build
with no configuration is green for the right reason; build controls compile `try`, `throw`, `typeid`, `dynamic_cast` and
appended `-ffast-math` / `-fexceptions` / `-frtti` and require the build to fail on each. Consumers link it like any
other module: `target_link_libraries(app PRIVATE felitronics::session)`.

**What the flags do not reach, stated rather than checked**: header-inline code the library shares with the program
(`std::` templates, core's header functions), of which the linker keeps one copy. A program that links
`felitronics::session` compiles EVERY translation unit with the session's FP flags — no contraction, no fast-math —
and does no partial linking; the wasm modules are built whole by this repository and are not affected.

**`create()` refuses before it allocates** a thread that flushes subnormals to zero, reads them as zero or rounds other
than to nearest (`Status::FloatingPointEnvironment`, read with ordinary arithmetic). NaN sign and payload and the FP
exception masks are outside the check.

**The laws, each held by a check with a control** — against honest mistakes and reasonable spelling variants, not a
hostile author (`docs/SESSION.md` states the threat model). An object-file gate
(`modules/session/tests/object-gates.cmake`, on every native row and the wasm tier, with `readelf`, `objdump`, `dumpbin`
or `llvm-readobj`) reads every object of the library and of its C boundary: no symbol in writable memory, judged by what
the object says of each section (ELF and COFF write flags; the read-only places by name on Mach-O and wasm, which carry
no such flag) — the boundary keeps exactly its handle table and poison flag, and an allowance naming a symbol that is
gone is rot — and nothing called that is not on `tools/lint/session-objects.txt` or defined with global binding
elsewhere in the set, so `printf`, `fopen`, `time`, `getenv`, `strtod`, `isalpha`, `rand` are refused whatever header
declared them and however they are spelled or reached, and a local `getpid` in one object answers no other object's
call. Sixteen controls compile each shape — a global in a section of its own naming among them, and the shipped boundary
with a third global and with a renamed one — and require the refusal to name the planted symbol whole; one — a constexpr
table of pointers, relocated constant data — requires the gate to accept it. A source lint
(`tools/lint/check-session-laws.mjs`) holds what leaves no symbol, over the module and the C boundary, after translation
phase 2: an include allowlist (felitronics headers by name), no macros and no directive but `#include` and `#pragma
once` outside the guards and the boundary's stated allowance, no pragma, an attribute allowlist (no vendor attribute, no
section placement), no alternative tokens, no `mutable`, no exception or RTTI token in any `#if` branch, no atomics,
cycle counters or inline assembly, no `std::unordered_*`, `hash<`, unstable sort or `using namespace`, no function body,
non-constexpr variable or namespace-scope function in the public header, the guards first in every unit — scanned from
the targets' sources (`modules/session/sources.txt` and the boundary's unit, cross-checked against
`compile_commands.json`) and the `#include` closure, failing closed on files it cannot classify;
`tools/lint/session-controls/run.sh` plants 43 violations and requires each to fail on its file and line. Every file of
the module and the boundary is in the det-math zone and every translation unit — the library's, the boundary's, the
CLI's — is an entry point (four new det-math controls). Memory is declared before the work: a declared-budget harness on
core's allocation counter holds `create()` to `createBytes()`.

**`fc_session`, a draft — ABI version 0, no promise** (`tools/fc_session_abi.h`, `tools/wasm/fc_session.cpp`):
`fc_session_abi_version`, `fc_session_create`, `fc_session_destroy`; handles with 24-bit generations — a slot retires at
its last generation instead of wrapping, so a stale handle can never name a new session — at most
`FC_SESSION_MAX_HANDLES` (8 — a fixed capacity, not configurable) live sessions and `FC_SESSION_SLOT_GENERATIONS`
creates per slot, the session's refusal as `FC_SESSION_ERR_FP_ENVIRONMENT`, and the poison. The boundary is compiled
with the library's own options and held to its source laws. `tools/wasm/build.sh` builds a fifth module, `fcsession` (`createFcSession`; ES-module web glue and
node glue, byte-identical wasm, `--wrap=pthread_create`, the sources `sources.txt` lists and none it does not): 2.9 KB of
wasm, 1.4 KB brotli. `tools/wasm/session-check.mjs` compares every export of the module against the ABI and the runtime's
own, runs the surface, and walks one slot through all of its generations; a control copy with one undeclared export
must be refused.

**`fcore_session`**, the native CLI over the session: `fcore_session version` prints both releases and the ABI version;
`fcore_session run <script|->` accepts a script with no command in it and prints `done 0`, and refuses a script with a
command in it, or a session that refuses to be created, with exit status 2 and nothing on stdout.

### session · tools — the mastering config: two TOML documents compiled into `felitronics::session`, read by schema

**Every number of the mastering session now lives in the session**, in two TOML documents of the module, each number
with what it means and where it came from beside it, and the owner's decisions marked as such. `modules/session/config/
targets.toml` is the table of targets — 25 of them, each with its loudness, true-peak ceiling, mono-bass crossover (120 Hz;
vinyl 150), high-pass floor (24 Hz; vinyl 32) and slope (24 dB/oct; vinyl 12), how much the high-pass may take at the
lowest note (1 dB; club 0.3), and its delivery rate and bit depth; vinyl's +0.5 dB low shelf and its ceiling without a
peak clipper; the high-pass always placed on vinyl and on club (there it guards the subwoofers from infrasonic bursts);
the one extra pass at the source's rate of cd and cdDynamic; AES
TD1008's −14 LUFS album loudness, marked desktop-only; the default target, the main list and the travels of the hand
edit. `engine.toml` holds every other number: the input brought to −18 LUFS (a warning below −40, gain and ceiling only
below −55), the landing in series of 12, 24 and 32 passes, the limiter's 0.15 dB ceiling margin and 50 ms release with
its second release stated off (the sound depends on no default of the core's), the peak clipper's classes (its manual
threshold starts at the "between" class), the low-end measurement's geometry, the high-pass knob topping out at 50 Hz
with one comfort window (24–42 Hz, warning towards 20 and 50) and "no DC" named as the dcOffset finding's threshold,
mono bass with its width knob and ONE wide-bass warning at 6 % of side, the compressor's threshold counted from the
short-term P95 and its glue on cd, saturation, tilt, the low shelf, dither at 16 bits only, the de-esser (manual, off, not
offered), the observations' thresholds, what a master's cost is measured with — as measured, without a verdict — the
progress weights and the blind test's protocol.

**Compiled in, never read.** felitronics-toml v0.3.0 (MIT, listed in `THIRD_PARTY_NOTICES.md`) is resolved like
felitronics-core — a sibling checkout for local work, the pinned tag otherwise — and compiles both documents into the
library as constexpr data (`felitronics_toml_embed`); nothing reads a file at run time. A product that consumes this
repository makes felitronics-toml available before it, as it does core. A document the parser refuses stops the build at
its line and column.

**Read by schema — form and physics** (`<felitronics/session/Config.h>`): `Config::load()` binds the documents to typed
structs — every key with its type and its domain (a share within 0…1, a ramp whose ends cannot divide by zero, a series
that does not shrink, a value on its knob's grid counted from the travel's start and checked exactly on the written
decimals, a range another key states such as a target's loudness on the edit travel), checks across keys (a name that is
no target, a name given twice, an EQ band two devices share, a ramp law outside its domain, the limiter switched off, a
default written out), and every key nobody read reported as unknown. The blocks the config feeds an analyzer — the low
end, the crest, the sibilance-band bursts — are handed to that analyzer's own `storageFor()` at the source rates the
product accepts and refused whole where it refuses: one source of truth for its domain. A problem is data: document,
fault, key path, line and column; `Config::bind()` runs the same schema over texts.

**Every build runs the schema.** `felitronics_session_config_check`, a host tool compiled from the library's own schema,
reads the documents before `felitronics::session` is built — a consumer's build and a build without tests included, under
the emulator where the build cross-compiles, or `FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE` — so a typo is a red build at
`<file>:<line>:<column>`, and a failed gate runs again; a newer or another supplied checker, or a change of the schema's
sources, runs it again too. `tools/wasm/build.sh` runs it before linking `fcsession` and records
felitronics-toml in `BUILD-INFO`. Six controls plant mistakes in a copy and require the gate to go red at the spot; the
config suite plants over sixty more, in-process.

**The owner's decisions are pinned apart** (`felitronics_session_config_decisions_tests`): every target row field by field
(delivery rates included) and the engine's decided numbers (the landing's series, the high-pass knob, the glue knob and
its default of none, the mono-bass block, …), so changing one is a deliberate test edit; its controls plant departures the
schema admits and require them named.

**The config's versions** (`Config::versions()`): 64-bit FNV-1a hashes of both documents' normalised data — numbers as
the bits of their double (−0 as +0), tables in key order, order kept in arrays — so spelling, key order, inline-or-not,
comments and spacing move nothing; the target rows' written order counts in `all`. `all` covers every key; `sound`, what a
recipe will record, is what can change a master — when unsure a key stays in — leaving out what is only shown, what
prints a finding without switching a device (every observation threshold but polarity), what is measured after the
master, development, and the de-esser's block while no shell offers it. Both are computed from the
embedded data without allocating. The suite changes every value of both documents one at a time and requires `all` to
move each time to a value of its own and `sound` to move exactly for the values that can change a master; the sound
version is pinned to the name of the defaults, so a sound number changed without new defaults is red.

**`fcore_session config targets|engine|version|sound-version`** prints a document of the embedded config through
felitronics-toml's canonical writer, or a version; ctest holds the output byte for byte to the source documents.

**`fc_session_config_version`** joins the draft `fc_session` (still version 0, no promise): the config's `all` version in
two uint32 halves, the out-pointer checked before anything is written, nothing allocated. `fcsession` now carries the
config — 43.8 KB of wasm, 13.4 KB brotli, from 2.9 / 1.4 — and `tools/wasm/session-check.mjs --config-version` requires
its version to be the native CLI's. `tools/wasm/build.sh` embeds and gates the config with a felitronics-toml checkout:
`FELITRONICS_TOML_DIR`, or the sibling `../felitronics-toml`.

### build — felitronics-core v0.55.0 is the pin

The session's translation units include felitronics-toml's headers, and every one of them is a det-math entry point,
so the lint must resolve those headers to audit them: felitronics-core v0.55.0's `check-det-math.mjs --satellite` takes
`--include-root <dir>`, and CI passes it felitronics-toml's include directory from the build's cache
(`FELITRONICS_MASTERING_TOML_SOURCE_DIR`) on every satellite run. The pinned `FELITRONICS_MASTERING_FCORE_TAG` moves
from v0.53.0 to v0.55.0, and the configure-time messages and `tools/wasm/build.sh` name v0.55.0 as the minimum.

### session · tools — the session's states and commands: one table of who may do what, when, and a project in two layers

**`felitronics::session` has states and commands** (`<felitronics/session/Commands.h>`). A session is Empty, Loaded (a
source, its first measurement running, the devices not placed), Measured1 (the devices placed, a master can be made) or
Measured2, and a master being made is an overlay on the measured two. A shell asks by typed requests — `load`,
`setTarget(name)`, `editTarget`, `editDevice`, `revertEdits`, `setManual`, `master`, `cancel(job)`,
`forget(master)` — each with the shell's own id, and `Session::apply()` answers each whole: accepted with the revision it
made, or rejected with a `Rejection` code (and the field, by its place in its struct), having changed nothing — the
revision included. No text anywhere: the codes are stable values a shell's catalogue writes from.

**Who may do what, when, is one table in code** (`Table` in `Commands.h`): a row per command, a cell per column (Empty,
Loaded, Measured1, Measured2, and a master being made on either measured state) — taken, or the rejection it gets there.
Every command consults it right after the floating-point entry check; the endings of the work (the first measurement, the second, a master) are the session's
own transitions with a table of their own, driven by the work through an internal seam, not by commands.
`fcore_session table` prints both tables from the code as Markdown, and ctest holds `docs/SESSION.md`'s copy to that
output byte for byte. The checks after the table run in one declared order — the thread's floating-point environment,
the table, what the command names, the fields, a load's audio — and the first that fails is the answer.

**The project** (`<felitronics/session/Project.h>`): the target (a row of `[targets]`) with a person's edits of its
loudness and ceiling, the manual mode, and the devices of the first release — the high-pass, mono bass, the glue,
saturation, tilt, the limiter's needles, the dither and the low shelf. Each device's fields are written once, as a
template over a field's form, and used as the machine's layer (complete), a person's layer (only what was touched) and a
revert's mask: no string names a field. The machine's layer — in this release the config's defaults for the target and
the source — is placed when the first measurement ends, and again on a change of target after that; until then the
devices are unplaced (the layer at its types' zeros), and a load unplaces them again. A person's device edits are taken only after placement and
regardless of panel visibility, each value finite and within its knob's domain; slider steps guide the UI and do not
restrict command values. A change of target replaces the target's numbers silently and always resets every device edit;
the machine decides again for the new target. The shell warns using the snapshot's existing `handFieldCount`; switching the manual
mode off hides the panel and preserves every edit; the low shelf is offered on every target, while dither and mono
bass are offered where they apply. `load` checks everything first, then disarms — what ran on the old source stops,
its masters go, the manual mode is switched off and a person's device edits with it, the old samples are freed before the new are asked
for — and writes the new source with its hash. `master` captures the recipe (the project, the source's hash, the config's sound version); the master is kept
under its job's id when it is done.

**Memory is declared before every command** (law 11d): `Session::check()` runs exactly the checks `apply()` runs first
and says what the command will ask the heap for — a load its samples and its name, a master room for one more kept,
everything else nothing — and the state suite holds every command to it, exactly, through the allocation counter.
Reading the config costs nothing: the commands read the embedded documents in place, as the decimals written, and the
suite holds every number of that reading to the schema's binding of the same documents. The build gate validates the
embedded config; a broken required lookup is a contract trap. There is no second config validation at session creation.

**`felitronics_session_state_tests`**: every cell of both tables, every rejection code produced with the whole session
compared before and after, the order of the checks, every knob's domain, fractional values and non-finite values, placement, a change of
target resetting every device edit, the manual mode switched off, a master's recipe, a load's disarm, the source hash pinned,
and memory declared for every command.

**`fcsession`** exports `fc_session` v1: commands, capacity and storage queries, project import/export, stepping,
and snapshot/event transfers into caller-owned buffers.

**The glue is its knob, "up to N dB"** (an owner decision): the project's glue field (`upToDb`), a person's edits of it
and every glue number of `engine.toml` are on the knob — 0…3 dB with a UI step of 0.1 — and the schema holds `default` (0),
`whenTicked` (0.5 dB, where the travel's 0.3 gave 0.51 dB) and `byTarget` (cd 2.6 dB, what the travel's 0.7 read as)
within its domain. The travel 0…1 and its laws stay the compressor's internal mapping. The sound version of the
2026-09 defaults moves with these numbers.

### session · tools — the text: facts, a catalog of whole messages and one formatting table, compiled into `felitronics::session`

**The session states facts, not strings** (`<felitronics/session/Text.h>`). A fact is a `FactId` and typed arguments — a
number with its unit, precision, sign and bound; a count; a term the catalog names; a note as a MIDI number; a text of
the user's, never translated — and carries no ready string. `Text::text(fact, lang)` renders it: a pure function over
data compiled into the library, with `size()` and `write()` beside it that render into a caller's buffer without the
heap, and `textBytes()`, the demand of `text()`. The first facts exercise every kind of argument: a reading alone, the
landing's pass and its convergence (plural on the passes), the blind test's repeat consistency (plural on the second
number), the loudest bass note, the wide-bass warning of phase 1 in the owner's words, and a file above the platform's
highest rate (select on the platform). Russian first, then English.

**The first real facts: a command's rejection.** Every code of the state machine's `Rejection` is a fact, 100 + its code,
with a sentence in Russian and English that says what was refused and why; the four a field refuses name the field (the
target's loudness or ceiling, a device's knob, a load's audio). `Text::rejected(answer, request)` builds the fact from a
refused answer. Fact ids are stable and fall in ranges: 1–99 readings and the landing, 100–199 rejections, 200–299 and
300–399 reserved for the phases and the errors. An unmapped new code is a red build; the suite holds the table code by
code, the field terms against the state machine's walk of the fields, and the answers of a real session.

**Two TOML documents, compiled in** (felitronics_toml_embed, as the config): `modules/session/text/catalog.toml` — whole
messages with named placeholders, `plural` variants by CLDR category and `select` variants by term, and the languages it
declares, `ru` and `en` — and `modules/session/text/format.toml`, the one table of how each of the twelve site languages
writes a number: decimal sign, grouping separator and CLDR's minimum grouping (es, it, pl group from five digits), the
Unicode minus, the bounds `≥` and `≤`, `—` for a value that is not a number, each unit's pattern after a no-break space,
so a number and its unit never wrap apart (Turkish `%45`, French narrow no-break spaces, Russian and Ukrainian unit
signs in Cyrillic as the site writes them), and the names of the notes in the site's three systems (letters; German,
where B natural is H; solfège).

**Every build checks the catalog.** `felitronics_session_text_check`, a host tool compiled from the library's own
`src/TextSchema.cpp`, runs over both documents before the library is built (through node on the wasm tier, and in
`tools/wasm/build.sh`): every declared language has every message and term; placeholders name the fact's arguments
(`src/TextFacts.h`), every argument is placed and every language places the same set; a plural message has exactly its
language's categories, a select message exactly its group's terms, and every variant places every argument (Russian
"one" is also 21); the table covers all twelve languages and every unit, its signs hold no digit and are no separator,
the fixed ones are exactly the law's (U+2212, "—", "≥"/"≤" with a no-break space), and no unit pattern holds a breaking
space; and no key is one nothing reads. A problem is a red build at `<file>:<line>:<column>`. There is no fallback to
English anywhere: a message the catalog does not have in a language renders as its id. Eight controls plant mistakes in
a copy and require the gate red at the spot; the suite plants forty-six more in-process.

**Numbers by rules of its own** — no libm, no printf, no locale, no floating-point arithmetic: a double is read as its
shortest round-trip decimal (`std::to_chars`) and that decimal rounded to the grid, halves away from zero, so the number
a person wrote rounds as they would round it (1.005 → 1.01, −14.05 → −14.1, 0.125 → 0.13, 2.5 → 3), checked against an
independent oracle over 24 000 decimals; the sign is the printed number's — a value that prints as zero takes none under
every Sign ("0.0", never "−0.0") — and is read from its bits, so no rendering depends on the thread's floating-point
environment (held under flush-to-zero, denormals-are-zero and every rounding mode); CLDR 48's plural categories for all
twelve languages, selected on the number as printed ("1.0" is not "one" in English) and pinned against ICU 78's answers;
`Text::parse` reads a typed number with `std::from_chars` and one correctly rounded division, never `strtod`, refuses a
grouping separator rather than guess (a German "12.345" is not twelve), and asks for the default floating-point
environment first. `felitronics_session_text_tests` pins the twelve rows, every message in both languages, the memory
demand through the allocation counter, and one FNV-1a hash of a corpus of renderings that every native row and the wasm
tier must reproduce byte for byte.

The session-laws lint names the two documents as data, admits `Text.h` and, in `src/Text.cpp` alone, the two headers the
build generates from them; the new units are det-math entry points; `tests/HeaderHygiene.cpp` compiles `Text.h`.

### session — work-unit pump, event deltas and owned snapshots

The session runs deterministic stub measurement and master jobs through a bounded work-unit pump. Phase events use
config weights; facts appear on the step that establishes them. Cancellation preserves the session, and job/source
identity checks ignore late completions. Measurement jobs have ids alongside master jobs.

Snapshots own both project layers, source metadata, recipes, progress and reading rows. The named-field JSON codec
preserves finite doubles and signed zero, explicitly represents infinities and NaN gaps, and generates TypeScript
declarations from its field description. Every operation declares its memory demand before work. Scenario tests pin
event sequences, commands between steps, cancellation, stale completions, round trips and exact allocation demands.

Event facts share the bilingual text catalog and own any user text. Phase-one cancellation returns Empty and drops the source. UTF-8 names are checked before loading. Generated record arity and enum controls, plus actual encoded fixtures checked against TypeScript, hold the codec to its declared surface.

### session — canonical TOML projects and recovery by replay

`exportProject()` writes the target by name, manual mode and both device layers as canonical TOML. Only machine
values that differ from defaults and touched human fields are written; equal human edits retain their ownership.
`importProject()` accepts dotted keys and inline tables through felitronics-toml, validates the whole project with
positioned refusals, and declares a size-based allocation bound before parsing.

Unknown defaults are refused. Every saved machine layer is preserved, including one carrying the same core stamp;
the session compares it with today's placement and publishes the ordered differences and their count. Owned snapshots
and their generated JSON/TypeScript codec expose each difference. The original core stamp remains with an imported layer, keeping subsequent exports replayable.

The suites cover canonical round trips, every import refusal without state changes, allocation budgets including
MSVC Debug, deterministic measurement slicing, permanent facade poison and recovery in a fresh session from the
source, measurement and last project text.

### session · C ABI — frozen v1 shell contract

`fc_session` v1 exposes capability/config creation and its demand, named JSON commands, planar PCM load, work-unit
steps, event/snapshot size and copy calls, and project import/export. The session enforces heap ceilings, maximum
rates and offered devices for C++ callers too. Generated types carry the config version and tagged event union;
numeric rows travel in caller-owned f64 buffers. Convert, Lra and Final append stable phase values with catalog text.

Compiled probes freeze signatures, constants, enums and layouts on native and wasm32. The manifest gate permits
additions and has change/deletion controls. Native/wasm tests cover refusal order, allocation bounds, typed transfer,
project round trips and permanent poison; the wasm artifact smoke exercises the public surface and real trap recovery
status. Windows Debug includes both session ABI suites.

The 28 September pre-freeze target decision makes `setTarget` always reset device edits. Its C++ request and generated
JSON/TypeScript command carry only the command id and target. Answers and events are unchanged; the shell warns from
the snapshot's existing hand-edit count. The v1 manifest baseline is regenerated for this not-yet-frozen surface.

- Add one text scenario grammar for `fcore_session` and the Node fcsession consumer, with ten native/wasm contract scenarios and exact codec/row-byte comparison.
- Verify synthetic fixture input/output hashes, scenario behavior, poison recovery, and intentional mismatch controls; run the comparison separately on every PR across the native CI rows.
- Exercise final v1 size prefixes, demand queries and capacity updates, knob domains, hidden manual edits, and saved machine layers including `low`.

Panel visibility leaves device edits active and persistent; snapshots expose their touched-field count. Project import
keeps every saved machine layer, including same-core changes, and reports today's differences with owned rows and
Russian/English count facts. Low is a separate device on every target; `lowDb` names the machine's medium correction.
Snapshots publish the summed high-pass, tilt and low EQ curve through the codec and generated declarations. The
pre-freeze rejection list removes ManualOff and MachineMismatch and renumbers the corresponding catalog facts.

### session · tools — measurement review fixes, and a manifest that only grows

A reload of the same PCM under another bit depth is another forensics result: its key mixes the measurement key with
the depth, and the reload publishes one `Measurement` event for it; the other results keep their key. A needles job
that outlives a measurement cancelled at the first phase's end (the `Stopped` column) is cancellable there, and a
cancel's fact names the job it stopped. A live preparation refused for memory is that instrument's outcome —
`Unavailable` for `Memory`, one error, the job goes on — so a capacity shrunk for good no longer repeats the refusal
on every unit and the job ends; a refused loudness meter leaves its rows missing (the result ready but incomplete),
while the report's integrated loudness and true peak still make the source measured. Needles are `Pending` only while a job runs: with unusable readings (silence) the
result is `Unavailable` with the loudness result's reason. The command table names `master`'s `NoAudio` for a sidecar
source among its name checks.

`FC_SESSION_ABI_VERSION` is a floor, like fc_master's: the manifest's version line is checked as "at least", and after
the first release each batch of additions that lands together in one release moves the number up by one and adds one
row to the header's history; the generated declaration and runtime constant read it from the header. Every `fc_session_*` declaration is frozen whatever it
returns; the wire's `SessionStatus` union is generated from `fc_session_status` instead of a copy in the codec schema;
the generated `SessionCapabilities` carries `largestFreeBlockBytes`, and a wire record that mirrors a C struct must
carry all of its fields. On pull requests CI refuses a manifest that removes or edits a base line.

<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
### session · build — project import uses the TOML storage contract

Pin felitronics-toml v0.3.0, including the embedding tool. Project import declares the library's allocation-free
parse/read allowance for its text and required paths, plus its own inline storage's zero heap demand. Checked sums
refuse an unrepresentable demand. The session's parser-size estimate and separate 16 KiB text cap are gone;
felitronics-toml enforces its own document limit.

The session budget law continues to measure the whole import, with realistic tightness and adversarial project
measurements on every tier. Windows MSVC Debug also runs the event, project and replay suites, and its expected-name
gate requires all thirteen suites.

## v0.2.2 — 2026-09-26

### tempo · tools · wasm — the first tempo analysis is as fast as every later one

A page no longer needs a warm-up run before the first tempo. In the browser the first analysis on a freshly loaded
`fctempo` took more than twice as long as the next: **1.11 s against 0.48 s** on a 6:15 stereo mix at 48 kHz (node
26; 1.11 s against 0.49 s in headless Chromium 151), 1.54 s against 0.67 s on an 8:39 one. Now the first costs what
the others do — **0.47 s, 0.49 s in Chromium, 0.66 s** — and `fcprobe`'s tempo moves the same way (0.62 → 0.48 s).
Every later analysis is unchanged or a little faster; the native tool is unchanged. The answers do not change by a
bit.

Why the first was slow: V8 gives a wasm function optimised code only on its next call — there is no on-stack
replacement for wasm — and emcc's post-link optimiser (binaryen) had folded the detector's whole analysis into the
one exported call, so the first analysis ran entirely on the baseline compiler. `TempoDetector` now keeps each loop
that carries time in a function called many times per analysis — `mixIn()` and `onsetFrame()` once per onset
frame (`process()` walks a call in stretches that end on the sample completing a frame), `lagSum()` once per
autocorrelation lag, `analyzeWindow()` once per window — each `noinline`. That alone is not enough: binaryen inlines
every function with a single caller whatever LLVM decided, `noinline` included, so `tools/wasm/build.sh` links
`fcprobe` and `fctempo` with `-sBINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=0`, which turns that one
rule off. On `fcprobe` that also takes a whole programme-report run in node, loading included, from 1.52 s to
1.04 s, for the same reason; no analyzer got slower, every output of every analyzer is byte-identical, and the modules move by +332 bytes (`fctempo`) and −506
bytes (`fcprobe`) under brotli.

Byte-identical to v0.2.1, native and wasm, both modules: 24 tempo rows — CI's fixtures, `--chunk` splits and option
rows, and three real mixes (1:46, 6:15, 8:39) whole, in 1000-frame calls and at a wide range — each answered by the
new native tool and by both modules before and after, against v0.2.1's native answer: 120 comparisons, none
different. The boundaries change no arithmetic: the same operations on the
same operands in the same order.

Guarded without timing anything: `build.sh` links a named twin of each module (`--profiling-funcs`, into
`build/tierup/`), `tools/wasm/tierup-check.mjs` proves the twin has the shipped module's functions (the same count,
sizes within a few bytes) and then fails the build if any of the four is missing from either module; a control
links `fctempo` without the binaryen option and demands the check refuse it. New in the native suite: after any
`process()` call, exactly the frames its samples completed have been transformed, whether the call ends one sample
before a frame, on it or after it.

## v0.2.1 — 2026-09-26

### tools · wasm — fctempo: the tempo detector as a module of its own

A page that measures a tempo and nothing else no longer has to download every analyzer of `fcprobe` for it.
`tools/wasm/build.sh` builds a third module, `fctempo` (`tools/wasm/fc_tempo.cpp`): `fctempo.web.mjs` + `.wasm` (ES
module, `-sENVIRONMENT=web,worker`, factory `createFcTempo`) and `fctempo.node.js` + `.wasm`, on the probe's flags —
no threads, emmalloc, `-fno-exceptions -fno-rtti -ffp-contract=off -fno-fast-math -msimd128` — with the same gates:
the web and node `.wasm` byte-identical, the web one audited for threads. **37 441 bytes of wasm, 14 604 brotli (`-q
11`)**, against the probe's 275 056 / 76 651; the glue is 8.7 KB against 23.3 KB.

Its entry points ARE the probe's tempo entry points: the same ten `fc_probe_tempo_*` names, arguments, rows and
refusals, because both modules compile one text, `tools/wasm/fc_tempo_entry.h` — a page moves from one module to the
other without changing a call. What it does not share is the version: `fc_tempo_abi_version()` answers
`FC_TEMPO_ABI_VERSION` (**1**, `tools/fc_tempo_abi.h`), which is fc_probe ABI 1's tempo surface. Not
`fc_probe_abi_version`, because a version is a promise about a whole surface — `fc_probe_abi_version() == 1` says
the report, the hum detector and the streaming meter are there, and a page gated on it would meet a missing export
as a TypeError. Append-only, like fc_probe's; a change a caller can see in the shared tempo text moves both versions
in one commit. A page switching over loads `fctempo.web.mjs` and gates on `_fc_tempo_abi_version() >= 1`; every
tempo call stays as it is.

`fcprobe` does not change: the tempo entry points and the argument guards (`tools/wasm/fc_abi_guards.h`) moved out
of `fc_probe.cpp` verbatim, and every fcprobe and fcmaster artifact is byte-identical to the build before the move
(the checked `fcprobe.debug.wasm` once its DWARF is stripped). `build.sh` now reads a module's entry points from its
`#include` closure, not from the `.cpp` alone, so a name declared in a shared header is on every export list that
compiles it, and the one-per-line, count and return-type gates run over the whole closure. The closure is the
compiler's (`-MM`, under the module's own front-end flags, so a header included only under `-msimd128` is in it),
read as Make writes it (a path with a space survives), and a file of it that cannot be read stops the build.

Proven the way the probe is: CI diffs `fcore_measure tempo` against `fcprobe`, its checked build and `fctempo`,
every row byte for byte, and every refusal row now demands exit status 2 on both roads, where it took any failure —
a module that did not load used to count as refusing. The harnesses take the module's identity from its file name
and hold the artifact to it (`tools/wasm/module-identity.mjs`), so fcprobe handed over as fctempo is refused, not
measured. `felitronics_fctempo_abi_tests` runs the tempo ABI suite against `fc_tempo.cpp` natively (ASan, UBSan) and
on the wasm tier's checked build; `storage-probe.mjs` holds `fctempo` to its exact export set and compares the two
modules' tempo prices over 3600 quotes; its tables leave the process only once written, into a pipe as into a file.
On three real mixes (4:10–8:39, 48 kHz stereo) the two modules answer the same bits in the same time.

## v0.2.0 — 2026-09-26

### tests — split invariance, one harness for every analyzer and the mastering path

`tests/split_invariance.h` and two suites (`felitronics_analyzer_split_invariance_tests`,
`felitronics_mastering_split_invariance_tests`) ask every streaming class what a caller that cuts its own chunks
needs answered first: is every output the stream decides — scalars, rows, event lists, histograms, traces, tap
streams, statistics — the same bits under one call, blocks of 64, 480 and 4096, seeded ragged cuts of 1..8192 with
zero-length calls, a second prepared `maxBlock`, the width timelines the contract allows (a clock-only gap in the
middle, at the head and at the end, a narrower stretch), and a finish on (and a read before finish of) a prefix?
For all twelve analyzers, the chain in two topologies with and without taps, `OfflineRenderer` at eight blockings
and ten short lengths, and the solver's `LoudnessMeter` + `ReferenceTruePeakMeter` pair, it is: 360 rows, on Apple
clang arm64, gcc 14.2 x86-64, MSVC 19.44 and wasm32. A one-ulp nudge at every call boundary is caught for every
class, and `ReferenceTruePeakMeter::truePeakLinearBlock()`, the last call's peak by definition, is reported as not
invariant. So are a `ProgressClock`'s events, which follow the pieces the work is cut into while the audio does not.
Measured and not fixed: `BandCrest::process (nullptr, 0, n)` is refused, where law 11a lets a clock-only call pass a
null plane array; `StereoBandBursts` and `PeakExcursions` latch their width on an empty call, where law 11d makes
`n == 0` a no-op.

### build — felitronics-core v0.53.0 is the minimum

The chain now calls `MonoBass::setBypass()` and `LaneDynamics::setReleaseOnDisengage()`, which first shipped in
felitronics-core v0.53.0, so the pinned `FELITRONICS_MASTERING_FCORE_TAG` moves from v0.52.0 to v0.53.0 and the
configure-time messages and `tools/wasm/build.sh` name v0.53.0 as the minimum.

### analysis_offline — the two contract edges the split-invariance suite found, fixed

Neither moves a number. `BandCrest::process (nullptr, 0, n)` is accepted now: law 11a lets a clock-only call pass a
null plane array, so a null array is refused only where a plane will be read (a null array at a live width still is).
`StereoBandBursts` and `PeakExcursions` latch their width on the first call that carries AUDIO, below the `n == 0`
exit and the plane check: law 11d makes `n == 0` a no-op, and an empty or a refused call used to latch it, so an
empty width-1 call ahead of the programme turned every stereo call after it into a refused width change. A width
that differs from a latched one is still refused, an empty call included. The split-invariance suite asserts both
now, with their negative halves, where it printed them as notes; every cut row is unchanged and INVARIANT.

### mastering — the gain nodes and the compressor mix glide instead of stepping

`inputGainDb`, `preLimiterGainDb` and `compressorMix` used to land on a quantum boundary as a step, which clicks on
a live preview: on a -12 dBFS 227 Hz sine at K = 128, max|Δ²y| where the change reaches the output was -20.1 dBFS
for inputGainDb 0 -> +3, -22.3 for preLimiterGainDb 0 -> +3 and -34.4 for compressorMix 1 -> 0.5, against -73.1 for
the steady tone. Each now moves by a fixed-length LINEAR ramp, `MasteringChain::kParamRampMs` = 30 ms (read back as
`paramRampSamples()`), per sample on the quantum's own clock, from the quantum the write lands on — -69.1, -69.4 and
-74.7. 30 ms was chosen by measurement: a +3 dB step is clean at every length from 5 ms, and 0 -> +12 dB reads -51.2
/ -58.0 / -57.2 / -60.8 / -60.2 dBFS at 5 / 10 / 20 / 30 / 50 ms against the louder tone's own -61.1, so 30 ms is the
shortest length at which a big jump sits on the tone's floor. The ramp accumulates in double (a float accumulator
drifted by ~len·ulp/2 and, at 352.8 kHz, drove a +59.5 -> -60 dB move through zero before landing — found by the
code-review round and pinned at 352.8 kHz, 768 kHz and 3 MHz). It arrives on the exact resolved value, so the
rest path is the constant multiply (and the mix branches) it always was; each glide sample of the mix is the stated
double blend at a float `m`, so the law-10 argument for the blend holds sample by sample. The FIRST write of a
stream — after `prepare()` or `reset()` — snaps: every offline render (`setParams -> reset -> process`, the renderer
and the solver) is bit-identical to the tree before this change, measured on six topologies including K = 8 and K =
100. The split-invariance suite gains two AUTOMATED chain rows — every glidable parameter and every bypass moved at
fixed stream positions on no grid — which must be the same bits under every cut, taps and statistics included.

### mastering — a dynamic point switched off mid-duck releases; the compressor's makeup and the stages' own glides

From felitronics-core v0.53.0 the stages glide on their own (the Saturator's drive, bias, mix and trim;
MonoBass's corners, its `enabled` and the air's; the Compressor's makeup and auto-makeup; the limiter's ceiling, down
over 2 ms), and each snaps on the
first write after a restart, so this chain's offline renders stay bit-identical (checked by hash on six topologies).
Two things are the chain's own: every `dynamiceq::LaneDynamics` producer is opted into RELEASE ON DISENGAGE, so a
point whose `dyn.on` goes off — or whose range goes to 0 — mid-duck releases its delta through its own ballistics
instead of snapping it (a -9 dB duck on a -12 dBFS tone: -16.1 dBFS max|Δ²y| before, -55.3 after, the rest being the
band's 16-sample control grid), and once released the chain renders bit for bit what a chain with a static point
renders; and an EQ bypass edge hands every band its caller's own parameters back, since a releasing producer holds
its band's dynamic seam open. `bypassCompressor` no longer clicks either: it writes makeup 0, which now glides (-30.5
-> -73.8 dBFS).

### mastering — bypass toggles fade: the clipper and the limiter warm up and cross-fade, mono-bass rides its own fades

A bypass of the clipper or the limiter was a skip with a `reset()` on BOTH edges and a hard swap, so a toggle clicked
and a return punched a hole: on a -12 dBFS 227 Hz sine at K = 128 (max|Δ²y| where the change reaches the output) the
clipper's round trip read -9.2 dBFS with a ~63-sample dropout and its return at drive 9 -7.3, the limiter's round trip
-12.5 with a 111-sample hole of exact zeros, and entering bypass while it held 6 dB -18.2. Each now has a FADER (five
modes, a position counted in samples of the quantum): entering bypass fades the stage to its aligned dry over
`kBypassFadeMs` = 10 ms and then stops calling it (a steady bypass is the skip it always was, bit for bit); leaving it
resets the stage, WARMS it up on the live input while the output stays on the aligned dry — for the stage's whole
finite memory, 2L+1 samples for the clipper and 2·O+A+1 for the limiter (its oversampler round trip around its
lookahead, the delay line and the peak window running in parallel) — and fades it back in; a reversal mid-fade turns
around from the weight it reached, and a bypass during the warm-up goes straight back to dry. Measured the same way:
-67.1 (round trip, no dropout), -63.6 (return at drive 9), -73.1 (limiter round trip, no hole), -73.1 (into bypass
while limiting), -70.7 (out of it), -63.2 / -75.9 (reversals), -65.1 (an Asym clipper's return), -68.7 (a dual-release
limiter holding 12 dB). 10 ms was measured against 5 (worst -60.9, the Asym return) and 20 (worst -64.3). After the
warm-up and the fade a restored chain is bit for bit the chain never bypassed wherever the stages' memory is finite
(a symmetric curve, a limiter not limiting); the Asym DC blocker and the limiter's release state start fresh, as any
reset starts them. The limiter's traces are its own whenever it runs, warming or fading included; they read zero only
while it is not called. Mono-bass's bypass now rides `stereo::MonoBass::setBypass` — the island's own 20 ms fades and
its own retirement — where it was a skip with a reset: -21.5 / -48.7 dBFS before, -74.3 / -75.5 after. Latency does not
move, and a render with bypass flags set before its first sample is the bits it always was (checked by hash).

### C ABI v14 — `fc_master_set_params`: a parameter set written while the stream runs

`fc_master_configure` re-prepares, so it is exact and refused (FC_ERR_STATE) once audio has been seen. The live preview
needs the other half, and this is it: `fc_master_set_params (h, params)` maps the set with the same `toCore` and hands it
to `MasteringChain::setParams()` — no preparation, no reset, no allocation — before, during and between streams. The set
lands on the chain's next internal quantum and GLIDES from there by each stage's own rule (the gain nodes and the mix,
the Saturator, MonoBass, the compressor's makeup, a dynamic point switched off, the limiter's ceiling, the bypass
fades); several calls before that boundary are the last one alone; and the first quantum of a stream snaps, so a set
written before the first frame renders exactly what `fc_master_configure` with that set renders (pinned).
`fc_master_resolved_get` lags ONE QUANTUM behind it — it reads what the chain applied — which the header states. Refused
where `fc_master_process` is refused (a delivering handle; a handle that has solved, until a configure) and for
configure's struct and value reasons, with the stream untouched. One entry point and no struct, so the size table
has no new row (as v7 and v9). The JavaScript half moves with it: `FC_MASTER_ABI_VERSION = 14`, and `FC_STATUS` gains
the three v13 codes (16–18) it had been missing for a version — layout-check.mjs now holds `FC_STATUS` against the
header's `fc_status` (count, order, value, name), so the next code cannot go missing the same way. MasterAbiTests: the
ABI stream with a live write held bit for bit against the C++ chain driven the same way, the resolved lag, the snap
before the first frame, every refusal and that it moves nothing, no allocation, the poison list. The wasm modules and
the parity scripts (master-parity on both generated programmes, the probe NULL) pass against a build of this tree.
The review round (codex astra) found no defect in the entry point and three gaps in its tests, all closed: every
accepted write went through `goodParams()`, the frozen v1 writer, so no field past v1 — `compressorMix`, the one that
glides, dual release, the peak clipper, the air shelf — ever crossed this call (a setter that forced the mix to 1
passed); now a current-version set with each of them moved is held bit for bit against the C++ chain too. Refused
writes after a valid one are shown to leave it pending as it was (a refusal that applied its half-mapped set is
caught), and a malformed call on a delivering or a solved handle answers FC_ERR_STATE — the handle's state first.

### tempo — the site's BPM detector, ported (`felitronics::tempo`)

New module `felitronics::tempo` (namespace `felitronics::tempo`, links `felitronics::core` alone):
`TempoDetector` is the site's `dsp/tempo.js` — `tempoCurve` (`headline()`) and `detectTempo` (`wholeTrack()`) from
one analysis — over the mono mix the page's `toMono` makes: the whole-track BPM, its confidence and label, the
in-range half/double alternatives, the beat period and offset, the top five autocorrelation candidates, the tempo
curve (6 s windows every 1.5 s, median of five), its 10–90 % range and `varies`. The spec's options are
`TempoParams` (BPM range, window, hop). Streaming with the bits of one call under any split; the width is exact;
`storageFor()` is the allocation, to the byte. `JsNumerics.h` carries the JavaScript arithmetic the port
reproduces: V8's `Math.hypot` (bit-exact against node on 2 000 225 pairs; the system hypot differed on 491 004),
`Math.round`, `Math.min`/`Math.max`, and an `exp` on the deterministic floor.

Measured against the JavaScript (node 26) on the same float32 samples — 19 synthetic programmes (click trains at six
tempi and two rates, a tempo change, a lone click, silence, noise) and eleven real stereo mixes of 106 to 519
seconds through the page's `toMono`: every reported field identical — bpm, confidence, label, alternatives, beat
period and offset, `varies`, range, and all 2 663 curve points' time, bpm and confidence; the unrounded whole-track
tempo within 2.1e-16 relative, the candidate scores within 3.2e-15, the onset curve within 6.2e-14 of its own peak.
The only numeric departure is the Hann window: `core::det::cos` and V8's `Math.cos` disagree by one ulp on 174 of
its 1024 coefficients (the FFT's ten twiddle seeds agree bit for bit). The wasm module agrees with the native build
on all of it bit for bit (8 689 published numbers over 30 programmes), and fed the stereo planes it reproduces the
page's mix. About 3.7x the speed of the JavaScript natively (a 6-minute mix: 0.4 s against 1.5 s).

`fc_probe_tempo_run` / `_run_with`, `_scalars` (35), `_candidates`, `_curve` (t, hasBpm, bpm, conf, and the
window's bpm before the median), the three widths, and `_storage_bytes` / `_storage_bytes_with`, which take the
programme's length. NaN is the spec's `null`, each beside a field that says whether the value is there; an empty
programme is a measurement (undetermined), not a refusal. Suites: `felitronics_tempo_tests`,
`felitronics_tempo_numerics_tests`, `felitronics_tempo_abi_tests`; both headers are in the det-math zone.

### tempo — native against wasm, gated

`fcore_measure tempo` prints the detector's whole surface — both headlines, the anchor, the range, the five
candidates and every curve point, doubles as bit patterns and the spec's `null` as `nan` — and
`tools/wasm/tempo-parity.mjs` prints the same bytes from the module (`tempo-format.mjs` is the shared half). CI's wasm
job diffs the two on three generated programmes with a beat that changes (`make-tempo-fixture.mjs`: clicks at one
tempo then another over integer-code noise, no transcendental, bytes pinned) at 48, 44.1 and 22.05 kHz and one, two
and six channels, under seven re-slicings (`--chunk`, honoured natively, the module measures in one call), four
parameter sets, the loudness fixture and an empty programme, release and checked modules; it asserts the comparison
saw a determined tempo that varies and a curve, and that both roads refuse the same seventeen command lines. Measured
locally: 64 comparisons, every one identical.

### fc_probe_crest_storage_bytes — no price for a span the run refuses

The crest price takes the programme's length and quoted a positive number for programmes whose planes cannot fit a
32-bit address space (mono at 2^30 frames and up; sixteen channels at 2^26) — exactly the spans
`fc_probe_crest_run` refuses before reading a sample, so a page that asked first was promised a measurement it
could not have. Both crest prices now refuse them, through the same predicate the run and the tempo price use.
Pinned natively in `felitronics_analysis_abi_tests` (both sides of the bound, mono and sixteen wide, the
parameterised price, and the run on the same spans) and on the artifact by `storage-probe.mjs check`.

### fc_probe — an ABI version

`fc_probe_abi_version()` answers `FC_PROBE_ABI_VERSION`, declared in the new `tools/fc_probe_abi.h` beside
`fc_master_abi.h`, with the append-only rule that moves it: one number for the probe's whole surface, bumped by one
when an entry point is added or a published block or row grows at its end; nothing existing is renamed, reordered,
re-typed or removed. It starts at 1 — the surface above, the tempo entry points and itself included. Pinned natively
(`felitronics_abi_tests`, a literal as well as the constant) and on the artifact (`storage-probe.mjs check` reads
the constant out of the header and asks the module).

## v0.1.0 — 2026-09-25

### mastering · analysis_offline · the C ABIs — split out of felitronics-core

The mastering chain (`felitronics::mastering`), the twelve offline programme analyzers
(`felitronics::analysis_offline`), the two C ABIs with their native CLIs and suites, and the wasm build with
every native-vs-wasm comparison, from felitronics-core v0.51.0; target names, namespaces and header spellings
are unchanged (`<felitronics/analysis/...>`, `<felitronics/mastering/...>`). `felitronics::analysis_offline`
now owns its include root. New here: the law-11b case for `OfflineRenderer` and the chain through core's shared
harness (`felitronics::test_support`), the analyzers' half of the math-policy gate, a header-hygiene TU that
includes every public header (`PeakExcursions.h` and `StereoBandBursts.h` were outside core's), and this
repository's det-math zone and manifest (`tools/lint/`), linted by core's `check-det-math.mjs --satellite`.
