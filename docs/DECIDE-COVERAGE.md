<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Decide / Solve / Project: coverage map (slice 3, task 06)

Task 06 has to prove that Decide, Solve and Project work together on scenarios. Slices 2 and 3 already hold much of
that in tests. This map lists every check the task names. Each check points to the test function that holds it, or is
marked as a gap. Test functions are the evidence. Suites without named functions are cited as `main`. Paths are
relative to `modules/session/tests/` unless they start with `tools/` or `modules/`.

Status: **HELD** means a test asserts the check. **PARTIAL** means pieces are held but the check as the task words it
is not. **GAP** means nothing asserts it; the label (A1…, B1…) points to the gap list below. **BEHAVIOUR** marks a gap
that needs new core behaviour, not only a test. **OBSOLETE** means an owner decision has replaced the check.

## Table A: the checks of task 06

Items 1–3 of the task and every bullet of its checks section, one check per row.

### Item 1: the full cycle, import and the file's machine layer

| # | Check | Holder | Status |
|---|---|---|---|
| A1 | load → measure → plan → hand → master → export → import → master, run in two independent Sessions as one scenario | Pieces only: `tools/tests/SessionReplayTests.cpp:replayAfterPoison` (load, edits, master, export, a fresh instance, import; state, both layers and the project text are restored, masters are not replayed); `PlanSoundTests.cpp:vinylAndQuietMastered` (export, open in another session, same recipe fingerprint and warnings); the whole: `ScenarioTests.cpp:theScenario` | HELD |
| A2 | An import keeps the file's machine layer | `PlanTests.cpp:anImportKeepsTheFilesMachine`; `ProjectTests.cpp:roundTrip` | HELD |
| A3 | The file carries no core stamp (v0.6.0): a machine layer is the file's however it arose | `ProjectTests.cpp:filesMachine`, `refusals` (a `core` key is unknown); `TiltLowTests.cpp:theFilesMachineStays` | HELD |
| A4 | New-defaults import | `ProjectTests.cpp:defaultsVersions`: only the current label `2026-10` opens, with the file's machine layer and its differences beside it; any other label (`2026-09`, `2020-01`, a newer or a malformed one) is refused whole with the existing `UnknownDefaults` / `NewerDefaults`, revision and state unchanged; nothing converts | HELD; settled by the owner 30.09, see Q1 |
| A5 | The comparison is shown beside the file's machine layer: the count fact, file and core values per field | `PlanTests.cpp:anImportKeepsTheFilesMachine`; `ProjectTests.cpp:filesMachine`; `TiltLowTests.cpp:theFilesMachineStays`; `HpfMonoTests.cpp:aSentenceStatesWhatSounds` | HELD |
| A6 | The new machine is adopted only on an explicit command: `AdoptMachine` takes the planner's layer, keeps the hand, allocates nothing, and does nothing when repeated | `PlanTests.cpp:anImportKeepsTheFilesMachine`; the master after adoption: `ScenarioTests.cpp:theFilesMachine` | HELD |
| A7 | PCM and WAV bytes: the master made after import equals the first one; after `AdoptMachine` it equals a fresh session's master | `ScenarioTests.cpp:theScenario`, `theFilesMachine` (after `AdoptMachine`, the recipe included) | HELD |
| A8 | Parameters and facts after import | `ProjectTests.cpp:roundTrip` (the whole snapshot JSON is equal; export → import → export is byte-identical); `PlanSoundTests.cpp:vinylAndQuietMastered` (recipe fingerprint, warnings); for the master: `ScenarioTests.cpp:theScenario` (recipe, facts, snapshot JSON with the kept report) | HELD |
| A9 | Version of the sound and of the defaults | `ConfigDecisionsTests.cpp:theSoundIsPinnedToTheDefaults`; `ConfigTests.cpp:theSoundIsWhatCanChangeAMaster`, `theVersionsMoveWithEveryValue`; `ProjectTests.cpp:roundTrip` (the header stamps defaults and core); inside the scenario: `ScenarioTests.cpp:theScenario` | HELD |
| A10 | No separate C facade for Decide | `tools/session-abi-v1.txt` is append-only from its declared base, checked by `tools/session-abi-check.mjs` and `tools/session-abi-append-only.mjs`; the manifest has no Decide entry | HELD |

### Item 2: what Master waits for, and a change of target

| # | Check | Holder | Status |
|---|---|---|---|
| A11 | Panel open: Master is refused whole (`PlanPending`, revision unchanged, the refusal published and rendered as text) until the tempo that the target's glue reads is measured | `PlanTests.cpp:theOpenPanelWaits`; `SourceMeasurementsTests.cpp:masterWaitsForTempo` | HELD |
| A12 | Panel open: Master waits in the same way for the needles (K10) at the ceiling | `PlanTests.cpp:theNeedlesAreWaitedForAtTheirCeiling` | HELD |
| A13 | Master waits only for what this target needs: no tempo when the glue does not compress, and hum does not hold the button | `PlanTests.cpp:aTargetWithoutGlueDoesNotWaitForTempo`, `theOpenPanelWaits`; `SourceMeasurementsTests.cpp:masterTempoDependencyPolicy` | HELD |
| A14 | The button shows the progress of the awaited measurement (fact `PlanWaiting`) | `PlanTests.cpp:theOpenPanelWaits`, `theNeedlesAreWaitedForAtTheirCeiling`, `aWaitingPlanStatesItsWaitAlone` (the plan states the fact itself, `plan.facts`); contract `plan-pending` | HELD |
| A15 | Dimmed panel: read-only while the first measurement runs | `PlanTests.cpp:theOpenPanelWaits` (`plan.readOnly`) | HELD |
| A16 | Dimmed panel: read-only after a change of target while the new target's needles run | `PlanTests.cpp:oneNeedOneMeasurement` (`plan.readOnly` and `PlanStatus::Pending` while club's needles run, neither after) | HELD (B1) |
| A17 | Panel hidden: Master is taken at once and waits by itself | `PlanTests.cpp:aHiddenMasterKeepsWhatWasAsked`; `SourceMeasurementsTests.cpp:masterWaitsForTempo` | HELD |
| A18 | Panel hidden: the waiting master's progress is published | `PlanTests.cpp:aHiddenMasterKeepsWhatWasAsked` (`plan.awaited`, `awaitedBy` and a moving `awaitedFraction` while it waits; after a change of target, its own `masterProgress`) | HELD (B2) |
| A19 | Panel hidden: the waiting master can be cancelled | `PlanTests.cpp:noPlanWaitsForEver`; `SourceMeasurementsTests.cpp:masterWaitsForTempo` | HELD |
| A20 | Panel hidden: the recipe is the project as it was when Master was asked for, so the result does not depend on how early the button was pressed | `PlanTests.cpp:aHiddenMasterKeepsWhatWasAsked`; `PlanSoundTests.cpp:aMasterThatWaited` (same bits); `StateTests.cpp:aMasterKeepsItsRecipe` | HELD |
| A21 | A dependency fails | `SourceMeasurementsTests.cpp:optionalFailure`, `deviceTempoPolicy`; `NeedlesTests.cpp:unusableReadings`; `PlanSoundTests.cpp:theNeedAndTheMeasurement` | HELD |
| A22 | A dependency is cancelled under a waiting master | `PlanTests.cpp:noPlanWaitsForEver` (the needles job is stopped; a stopped source measurement stops the master with `Cancelled`); `SourceMeasurementsTests.cpp:masterTempoDependencyPolicy` | HELD |
| A23 | A new file arrives while a taken master waits | `PlanTests.cpp:noPlanWaitsForEver` (a load; sidecar facts) | HELD |
| A24 | A new target is set while a taken master waits | `PlanTests.cpp:aHiddenMasterKeepsWhatWasAsked`, `noPlanWaitsForEver` | HELD |
| A25 | Change of target: the warning's edit count | `PlanTests.cpp:aChangeOfTargetResetsEdits` (`handFieldCount`) | HELD |
| A26 | Change of target: the warning itself as a core fact | `PlanTests.cpp:aChangeOfTargetResetsEdits` (`SnapshotText::targetChange`: absent at 0 edits, `targetChangeResetsEdits` with N, gone after the reset); `TextTests.cpp:everyMessageRenders` | HELD (B3) |
| A27 | Change of target: once sent, every device edit is reset | `StateTests.cpp:aChangeOfTarget`; `PlanTests.cpp:aChangeOfTargetResetsEdits`, `aTouchedDeviceSounds`; `TiltLowTests.cpp:aPersonsLayer`; `ProjectTests.cpp:roundTrip` (the dither hand) | HELD |
| A28 | Change of target: cancelled, the edits stay | `PlanTests.cpp:aChangeOfTargetResetsEdits` (a change that does not happen keeps every edit, and a project saved before the change keeps them) | HELD; the cancel itself sends no command |
| A29 | Hidden manual mode: edits are kept and still sound with the panel hidden | `StateTests.cpp:theManualModeOff`; `EventTests.cpp:eqSnapshot`; `GlueSaturationTests.cpp:aPersonsKnob`; `TiltLowTests.cpp:aPersonsLayer`; `PlanSoundTests.cpp:theRenderIsThePreviousPaths`; `PlanTests.cpp:thePlansKey` | HELD |
| A30 | "Keep" of the project | The reset-or-keep choice at a change of target is obsolete (see below). Still held: a saved project keeps the hand (`PlanTests.cpp:aChangeOfTargetResetsEdits`, `TiltLowTests.cpp:aPersonsLayer`); a kept master keeps its recipe (`StateTests.cpp:aMasterKeepsItsRecipe`) | OBSOLETE / HELD |
| A31 | Reset and revert | `PlanTests.cpp:thePlansKey`, `aChangeOfTargetResetsEdits`, `aTouchedDeviceSounds` (`RevertEdits`) | HELD |
| A32 | The hand keeps exact numbers | `PlanTests.cpp:aChangeOfTargetResetsEdits` (1.25 and 0.75 dB bit-exact across sessions); `ProjectTests.cpp:domainsAndExactNumbers`, `numbers`; `StateTests.cpp:negativeZero` | HELD |
| A33 | All eight device descriptors are in the snapshot | Each device is read by some test (`PlanTests.cpp:aTouchedDeviceSounds`, `PlanSoundTests.cpp`, `GlueSaturationTests.cpp`); the wire runs through the generated codec (`EventTests.cpp:codec`); `PlanTests.cpp:aTouchedDeviceSounds` carries all eight of one placed snapshot, with on, tick and needs, through encode → decode | HELD (B4) |

### Item 3: cache, slicing, repeated work, stale work

| # | Check | Holder | Status |
|---|---|---|---|
| A34 | The plan's cache is keyed on its real dependencies | `PlanTests.cpp:thePlansKey`; `SourceMeasurementsTests.cpp:cachedSourceAndCommands`; `MeasurementTests.cpp:ownership` | HELD |
| A35 | Pump slicing gives the same results | `ProjectTests.cpp:slicing` (measurement facts and the whole snapshot at budgets 1, 7 and large); `EventTests.cpp:pump` (the event sequence, including a decided master, at 1 and 16); `HpfMonoTests.cpp:throughThePump`; DSP: `modules/mastering/tests/SplitInvarianceTests.cpp`, `modules/analysis_offline/tests/SplitInvarianceTests.cpp` (`tests/split_invariance.h`); the full scenario's master and snapshot at 1, 7 and large: `ScenarioTests.cpp:theSlicing` | HELD |
| A36 | The shared measurements are not repeated | `PlanTests.cpp:oneNeedOneMeasurement`; `HpfMonoTests.cpp:everyTargetFromOneMeasurement` | HELD |
| A37 | The needles are measured again at a new target's ceiling | `PlanTests.cpp:oneNeedOneMeasurement`, `theNeedlesAreWaitedForAtTheirCeiling`, `noPlanWaitsForEver`; `PlanSoundTests.cpp:theNeedlesAreThePlansNeedles` | HELD |
| A38 | Cancelled work publishes no plan and no master | `SourceMeasurementsTests.cpp:masterWaitsForTempo`; `PlanTests.cpp:noPlanWaitsForEver`; `EventTests.cpp:pump`; `PlanSoundTests.cpp:theMemoryOfAMaster` | HELD |
| A39 | Stale work publishes nothing | `EventTests.cpp:pump` ("stale phase two is ignored"), `main` ("stale completion changed no snapshot field"); `MeasurementTests.cpp:ownership`; `MasterJobTests.cpp:main` (`copyMaster` answers `Stale`); `NeedlesTests.cpp:lifecycle` | HELD |

### Checks section of the task

| # | Check | Holder | Status |
|---|---|---|---|
| A40 | Native and wasm: the same scenarios, parameters, statuses, PCM/bytes and allowed event packing | Pieces: `MasterReportTests.cpp:main` (parity cases) with `tools/wasm/master-report-parity.mjs`; `tools/tests/needles-parity.mjs`; `tools/tests/query-parity.mjs`; `TextTests.cpp:theCorpusIsTheSameBytesOnEveryRow`; `tools/wasm/session-check.mjs` (the ABI on the built module). The decided scenario: `ScenarioTests.cpp:theScenario` prints its digests, `tools/wasm/scenario-parity.mjs` checks the wasm run (`tools/wasm/build.sh`) | HELD |
| A41 | Every device's bounds: domain ends, travel ends, values between steps | `StateTests.cpp:theKnobs` (`knobsOf` over every knob device; the dither has a tick only), `theMachinePlacesWhatAPersonCouldSet`; `ProjectTests.cpp:domainsAndExactNumbers` | HELD |
| A42 | 1.25 dB exactly | `TiltLowTests.cpp:theKnobs`; `GlueSaturationTests.cpp:aPersonsKnob`; `PlanTests.cpp:aChangeOfTargetResetsEdits` | HELD |
| A43 | 18 and 36 dB/oct | `HpfMonoTests.cpp:aPersonsKnobs`; `StateTests.cpp:theKnobs` (6 to 96 in steps of 6 accepted; 0, 7, 95 and 102 refused) | HELD |
| A44 | Frequencies outside the slider's travel | `HpfMonoTests.cpp:aPersonsKnobs` (above 50 Hz); `ProjectTests.cpp:domainsAndExactNumbers` (20000.25 Hz, the source's Nyquist); `TiltLowTests.cpp:theKnobs` | HELD |
| A45 | Domain errors | `StateTests.cpp:theKnobs`, `theOrderOfTheChecks`; `ProjectTests.cpp:domainsAndExactNumbers`, `refusals`; `GlueSaturationTests.cpp:aPersonsKnob`; `TiltLowTests.cpp:theKnobs` | HELD |
| A46 | High-pass at 32 Hz on every target; the machine never above its 50 Hz top while the knob travels to 80 | `HpfMonoTests.cpp:everyTargetFromOneMeasurement`, `theCutoffOnTheChainsResponse` (every target, note and rate at or under 50 Hz), `aPersonsKnobs` (70 Hz by hand sounds); `KitTests.cpp` (travel 15…80, red from 50); `ConfigDecisionsTests.cpp:theConfigHoldsTheDecisions` | HELD |
| A47 | High-pass on a short file (under 10 s: the floor, no note detection) | `HpfMonoTests.cpp:theSureLowestNote` | HELD |
| A48 | Mono bass: the loss thresholds of 1 and 3 dB | `HpfMonoTests.cpp:theLossOfTheLowEnd` (to the bit), `whereTheBassSounds`, `throughThePump` | HELD |
| A49 | Every needles class | `PlanSoundTests.cpp:theClasses` | HELD |
| A50 | A manual threshold X on a clipped source | `PlanSoundTests.cpp:thePersonsThreshold` | HELD |
| A51 | Glue set from P95 and tempo | `GlueSaturationTests.cpp:theCurve`, `theTempo`, `theCalibration` | HELD |
| A52 | Glue unavailable without P95 | `GlueSaturationTests.cpp:aPersonsKnob`; `PlanTests.cpp:aTouchedDeviceSounds` | HELD |
| A53 | Saturation cut: the largest and the median | `GlueSaturationTests.cpp:theCut`, `theReport` | HELD |
| A54 | A vinyl hand with its warning | `PlanSoundTests.cpp:vinylOnThePlanner`, `vinylAndQuietMastered` | HELD |
| A55 | No mono-bass veto by width | `HpfMonoTests.cpp:theLossOfTheLowEnd` ("the width warns, it does not decide") | HELD |
| A56 | No mono-bass veto against a hand | `HpfMonoTests.cpp:aPersonsKnobs` | HELD |
| A57 | Edits are not kept across a change of target | as A27 | HELD |
| A58 | Import does not swap the file's machine layer | as A2, A3 | HELD (but see Q1) |
| A59 | The needles may be measured again on a new target | as A37 | HELD |
| A60 | The reset warning is checked | as A26 | HELD (B3) |
| A61 | Cancelling the change of target is checked | as A28 | HELD |
| A62 | Repeating the project and the master does not grow live memory | `PlanSoundTests.cpp:theMemoryOfAMaster` (declared memory: large, cancelled, small); `MasterReportTests.cpp:main` (`memoryLifecycle`). `liveBytes()` over repeated import → master → release → forget, refusals between: `ScenarioTests.cpp:theScenario` | HELD |
| A63 | Budget and memory on refusals keep the slice-2 gate | `StateTests.cpp:memoryIsDeclared`; `MemoryGateTests.cpp:main`; `ProjectTests.cpp:demandsAndOrder`; `PlanSoundTests.cpp:theMemoryOfAMaster`; `MeasurementBudgetTests.cpp:main` | HELD |
| A64 | The report has a coverage map | this file | HELD |
| A65 | The report has public reproducible commands | Gap A4 below: the ctest, the parity script, the input hash and versions the scenario prints | HELD |
| A66 | The report links the private replay without publishing it | Gap A5 below: one line, no path and no data | HELD |

Count: 66 checks.
- 66 HELD. A4 waits on Q1. A30 is held for the part that is not obsolete. A16, A18, A26, A33 and A60 are held by
  B1–B4; A1, A6–A9, A35, A40 and A62 by the scenario (`ScenarioTests.cpp`); A65 and A66 by gap items A4 and A5.
- 0 GAP.

The gap items below (A1–A5, B1–B4) are all held.

## Table B: the 14 MOVE-TO-CORE rows (task item 5)

The rows come from `research/site-ts-tests-by-slice.md` (private). The site's TypeScript was not opened. Each row names
what the core should hold.

| Row | Asks for | Holder | Status |
|---|---|---|---|
| button-style | Glue by target and tempo; the LP chain; the clipper after the structure | `GlueSaturationTests.cpp:theMachine` (glue on cd alone, 2.6 dB), `theTempo`; `PlanTests.cpp:aTargetWithoutGlueDoesNotWaitForTempo`; `PlanSoundTests.cpp:vinylOnThePlanner` (LP: no needles clipper), `theTopologyFollowsTheTicks` | HELD |
| chain-by-target | HPF, mono, shelf, topology and rate, assembled per target | `HpfMonoTests.cpp:everyTargetFromOneMeasurement`; `TiltLowTests.cpp:theMachine`; `PlanSoundTests.cpp:theTopologyFollowsTheTicks`, `theRenderIsThePreviousPaths` (cd at another rate); `StateTests.cpp:aChangeOfTarget`; the delivery rate by target: `MemoryGateTests.cpp:main`, `SessionDirectOracleTests.cpp:main`, `MasterReportTests.cpp:main` | HELD |
| decide | HPF and mono, notes, polarity, silences, determinism | `HpfMonoTests.cpp:theSureLowestNote`, `aRareLowNoteIsNotSkipped`, `theLowestBandWithItsSureness`, `theLossOfTheLowEnd`, `whereTheBassSounds` (a quiet inverted stretch), `quietAndUnmeasured`, `aPieceLongerThanTheReading`, `throughThePump`; across rows, the scenario's plan: `ScenarioTests.cpp:theScenario` with `tools/wasm/scenario-parity.mjs` | HELD |
| device-picking | Glue parameters, tempo and confidence, clamps, override; the threshold from P95 | `GlueSaturationTests.cpp:theCurve`, `theTempo` (confidence 0.5, 120 BPM fallback, 50…500 ms), `aPersonsKnob`, `theCalibration`; `SourceMeasurementsTests.cpp:deviceTempoPolicy` | HELD |
| eq-face | Bands, ranges, step, HPF, dynamics | `PlanTests.cpp:theEqStage`; `TiltLowTests.cpp:theGeometry`, `threeDevicesOneStage` ("no band is dynamic"); `StateTests.cpp:theKnobs`; `HpfMonoTests.cpp:aPersonsKnobs` | HELD; the dynamic band is the de-esser, outside this slice (decision 3.13) |
| findings | Note, duty, infra-low, wide bass, polarity, confidence | `HpfMonoTests.cpp:theSureLowestNote` (2 dB, 10 %, 3 s), `theLowestBandWithItsSureness`, `theCutoffOnTheChainsResponse` (a note under the floor, `hpfBelowFloor`), `theLossOfTheLowEnd` (wide bass at 6 % warns; the polarity advice above 3 dB), `throughThePump`; `PlanSoundTests.cpp:theObservations` (no wide bass and no polarity finding on a mono file) | HELD; infra-low as its own finding is OBSOLETE (3.3) |
| glue | Off, step, the knob's curve; P95 plus calibration | `GlueSaturationTests.cpp:theCurve`, `aPersonsKnob` (a tick starts at 0.5 dB; 0…6), `theCalibration`, `oneSystemOfLevels`; `StateTests.cpp:theKnobs` | HELD |
| k13-evidence | Need, PLR, bass, duration; the K13 bounds are inclusive | `PlanSoundTests.cpp:theClasses` (every boundary, the order of reasons), `theNeedAndTheMeasurement` (a need of 3 dB or less is not measured; "not measured" differs from "none") | HELD |
| observations | Severity, confidence, the order of facts | `PlanSoundTests.cpp:theObservations` (found, not found and not measured are kept apart; strict thresholds; a doubtful wandering hum; the order is file → spectrum → hum), `theObservationsSpeakForThemselves` (the snapshot's `observationFacts` are `ObservationText`'s lines kind by kind, a not-measured kind says why; the loudest low note is a reading); `TextTests.cpp:theObservationsHaveTheirWords`; the readings: `PlanSoundTests.cpp:theReadingsAreFacts` (the snapshot's `readings` are `ReadingText::source`'s, each `Value` with its unit and precision, named by `terms.reading`, following a new source, whole across the wire, an unknown kind refused), `MasterReportTests.cpp:costLinesAndReadings` (the master's readings with the report; each cost line, 93–97, only where its numbers were measured) | HELD; severity as a verdict is OBSOLETE (3.13) |
| rules-corpus | The private Decide replay of 11 finished measurements | held outside this repository (gap A5) | HELD, private |
| stereo-bass | The 3 % and 6 % thresholds, targets, the polarity veto, override, quiet input | `HpfMonoTests.cpp:theLossOfTheLowEnd` (1 and 3 dB to the bit; 6 % width warns; weighed at 150 Hz on vinyl, 120 Hz elsewhere), `aPersonsKnobs` (override), `quietAndUnmeasured`, `everyTargetFromOneMeasurement` | HELD; no current decision names a 3 % threshold (decision 3.5 weighs the loss), so do not carry it |
| structure | The whole chain, needAfter, veto, gain-only on a quiet input | `PlanSoundTests.cpp:theTopologyFollowsTheTicks`, `theRenderIsThePreviousPaths`, `aQuietInput` (strictly under −55 LUFS: gain, ceiling, dither, HPF), `vinylAndQuietMastered`; the needles veto: `theClasses`, `vinylOnThePlanner` | HELD; `needAfter` is not carried, see Q2 |
| verify | The choice of slope, off or mono; its price; the veto at each boundary | `HpfMonoTests.cpp:theCutoffOnTheChainsResponse` (the loss at the note on the chain's own response), `theLossOfTheLowEnd`; `PlanSoundTests.cpp:vinylAndQuietMastered` (HPF off, from 25 Hz or at 6 dB/oct removes the "ready for cutting" line) | HELD |
| w41-vinyl | LP consistency between bench and decision | `PlanSoundTests.cpp:vinylOnThePlanner`, `vinylAndQuietMastered`; `StateTests.cpp:aChangeOfTarget` (the lp layer); `TiltLowTests.cpp:theMachine` (+0.5 dB); `HpfMonoTests.cpp:theLossOfTheLowEnd` (150 Hz) | HELD |
| device-advice | HPF beyond the norm (cutoff below/above the comfort window, slope too gentle/steep), EQ overshoot past warnDb, mono bass outside every zone, the target's note (measured, practice, no normalisation) | `PlanSoundTests.cpp:theAdviceIsAFact` (each why from a hand value, none inside the window or a zone, edges included, none for a device off; the overshoot at the curve's own point, said of tilt or low; the notes by target; the codec round trip) | HELD |
| pure-kit | The shell's one-frame answers without the worker (G6): a fact as text, a typed number for a field, a knob's travel and heat, mono bass's zones, the EQ curve preview and its overshoot against warnDb, the low-end dB curve | `KitTests.cpp:textIsTextWrite`, `parseReadsTheField`, `travelAndHeat`, `zonesAndAdvice` (the comfort and zones advice read the kit's comparison), `eqPreview` (the project path's curve and finding, bit for bit), `lowEndCurve`, `theAbiAnswersAsTheCall`, `theCorpusIsTheWasmModulesBytes` with `tools/wasm/session-check.mjs` (one hash native and wasm) | HELD |

Count: 13 held (decide misses only its cross-row determinism, which is A2). 1 gap: rules-corpus, which is private.
LowEnd and PeakExcursions tests are cited nowhere in this table as proof of a decision.

## Gaps

### A. End-to-end scenario

- **A1. HELD.** `modules/session/tests/ScenarioTests.cpp`, ctest `felitronics_session_scenario_tests`. `theScenario`:
  two independent Sessions on one synthetic source; load → measure → plan → hand (tilt +1.25 dB, low shelf +0.75 dB, the
  target at −13 LUFS) → master → export → import into the second Session → master. Equal: the snapshot JSON (the
  revision, the kept masters' ids and `fromFile` aside, `fromFile` asserted on its own), the recipe, the facts, the
  defaults in the file, the sound version, PCM and WAV bytes. `theFilesMachine`: a file with a machine opinion this
  planner does not hold (a high-pass from 36 Hz) is kept and does not sound as the planner's; `AdoptMachine` then gives the fresh session's master, recipe included. `theSlicing`: budgets 1 and
  7 give the large budget's master and snapshot. `nothingStale`: a master waiting for the tempo, cancelled, and club's
  needles left before they end publish nothing, and the master after them is the scenario's. The new-defaults import
  waits for Q1 (`ProjectTests.cpp:defaultsVersions` as built).
- **A2. HELD.** The scenario prints `scenario-input`, `scenario-versions` and `scenario-parity` (plan, facts, PCM, WAV
  digests). `tools/wasm/scenario-parity.mjs` holds the native lines; `tools/wasm/build.sh` builds the same test for wasm
  and checks its output with it.
- **A3. HELD.** Inside `theScenario`: four more cycles of import → master → release → forget, each with a refused
  import, master and forget, keep `Session::liveBytes()` where the first cycle left it. The cycle forgets its master:
  a kept master is live until `Forget`, by design.
- **A4. HELD.** Public reproducible commands. The fixture is generated in the test (`Mix`: a 10 s, 48 kHz stereo kick
  with a needle on its attack and a pad that swells at 6 s, built with the core's `det::sin`/`det::exp2`).
  - Native: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFELITRONICS_MASTERING_BUILD_TESTS=ON`, then
    `cmake --build build --target felitronics_session_scenario_tests` and
    `ctest --test-dir build -R '^felitronics_session_scenario_tests$' --output-on-failure`.
  - Wasm: `tools/wasm/build.sh` (with `FELITRONICS_CORE_DIR` and `FELITRONICS_TOML_DIR` set), which runs
    `node tools/wasm/scenario-parity.mjs <out>/session-scenario.txt`.
  - What the run prints (release 0.11.0, felitronics-core 0.59.0, defaults `2026-10`): the input
    `source=f58fa8f9570118b5 frames=480000 rate=48000`, the sound version `6fdf2af99bf12945`, the config version
    `64a77734238d01bc`, and `plan=f7b4068cb9e6c42b facts=34186a7c88300845 pcm=0682cfd85ae4b5a8 wav=e30bbbfe627809f9`.
- **A5. HELD, private.** The replay of the 11 finished measurements is private and held outside this repository.

### B. Behaviour

- **B1. HELD.** Dimmed panel after a change of target: `PlanTests.cpp:oneNeedOneMeasurement` asserts `plan.readOnly`
  and `PlanStatus::Pending` while club's needles run after `SetTarget`, and neither after they end. A pin of existing
  behaviour.
- **B2. HELD.** A hidden waiting master publishes what it waits for: `PlanTests.cpp:aHiddenMasterKeepsWhatWasAsked`
  asserts `plan.awaited` (the tempo), `awaitedBy` (the glue) and an `awaitedFraction` that moves while the master waits.
  `plan` is the project's: once the target changes under the waiting master, the project waits for nothing and the
  master's own `masterProgress` carries its wait, which the test asserts too. A pin of existing behaviour.
- **B3. HELD, BEHAVIOUR.** The target-change warning is a core fact. Owner decision "change of target": "Manual device
  edits (N) will be reset". `FactId::TargetChangeResetsEdits` (81, `count`), `messages.targetChangeResetsEdits` (ru,
  then en, plural on the count), and `SnapshotText::targetChange (const SnapshotView&)`: the fact with `handFieldCount`,
  nothing at 0 — the snapshot states its sentences as `ObservationText` and `PlanText` do, from the fields it already
  carries, so the wire does not change. Held by `PlanTests.cpp:aChangeOfTargetResetsEdits` and
  `TextTests.cpp:everyMessageRenders`.
- **B4. HELD.** All eight descriptors in one placed snapshot: `PlanTests.cpp:aTouchedDeviceSounds` encodes and decodes
  it and asserts that `plan.devices` carries hpf, monoBass, glue, saturation, tilt, limiter, dither and low with on, tick
  and needs. A pin of existing behaviour.
- Knob boundaries: no gap (A41–A45 are held).

## Open questions

- **Q1. An older-defaults import replaced the file's machine layer. SETTLED by the owner, 30.09.** A project saved on
  defaults `2026-09` (before the core had its planner) is not opened: an import accepts only the current defaults
  label, any other is refused whole as `UnknownDefaults` and nothing changes. On the current label the import rule
  stands: the machine layer comes from the file, the differences are shown beside it, a new machine opinion only by
  `adoptMachine`. Nothing converts.
  Held by `ProjectTests.cpp:defaultsVersions` (row A4).
- **Q2. `needAfter` in the structure row. SETTLED by the owner, 30.09: not carried.** The needles' classes use the
  input's need (`LimiterFinding.needDb`) and the limiter holds the ceiling; no need is measured after the chain.

## Replaced by owner decisions

- "Keep" of the project at a change of target (task item 2). Decision "change of target": "A change of target
  always resets the device edits (instead of reset / keep)".
- Infra-low, and "empty below the note", as a finding (row findings). Decision 3.3: "The rule 'do not place it if
  below the lowest note is empty' is removed entirely; there is nothing to measure".
- A 3 % width threshold (row stereo-bass). Decision 3.5 decides mono bass by the loss in dB; only the 6 % wide-bass
  warning remains.
- Severity as a verdict (row observations). Decision 3.13: "no verdicts of taste, only facts with
  numbers".
