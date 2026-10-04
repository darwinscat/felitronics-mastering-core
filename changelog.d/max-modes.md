### session · mastering — the maximum loudness modes: «Максимум · чисто» and «Максимум · плотно» (owner, 04.10)

- **A loudness mode per target**: `manual` (every target until now, unchanged), `maxClean` or `maxDense`. A row of
  `targets.toml` may name it (`loudnessMode`, absent = manual); two targets are appended last, `maxClean` «Максимум ·
  чисто» and `maxDense` «Максимум · плотно» (streaming group, the allStreaming medium). Any target takes a mode by hand:
  `editTarget` `loudnessMode` (null gives the row's back), kept in the project's target layer, said in the snapshot as
  the mode in effect (`Snapshot.loudnessMode`).
- **A max mode asks for the loudest master, not a number**: the landing aims at `[landing.max] ceilingLufs` (−5) with
  the mode's limiter budget (clean 3 dB, dense 7 dB of the active P95), and a budget that holds it is success. Before
  the file is delivered a PEAQ guard grades the render with the damage's machine: its worst window must stay above the
  mode's floor (clean ODG −0.5, dense −1.5), or the drive steps back 1 dB at a time, graded before it is rendered,
  three steps at most; the gentlest graded is delivered where none passes, and says so. The guard's grade is the
  master's damage: no damage job follows a max master. `LoudnessRequest::peakClipMeasured` renders a step back in one
  pass with the peak the landing measured.
- **The report and the text**: `MasterReport.loudnessMode`, `maxStop` (`Budget`, `Guard`, `GuardUnmet`,
  `SearchCeiling`, `Passes`, `TruePeak`) and `guardSteps`; facts 608–613 (ru/en) name the mode, the level and what
  ended it, with no miss and no hint; the `loudnessMode` terms and the field term `FieldTargetLoudnessMode`.
- **`FC_SESSION_ABI_VERSION` 10**: the manifest appends the enums, facts, terms and codec fields; `SURFACE[10]` adds no
  entry point. Existing targets' sound does not move (the WAV contract's PCM is unchanged); the `2026-10` defaults'
  config and sound versions move with the two rows and the `[landing.max]` table, restated in place (no project of those
  defaults has been saved); the event pins, the text corpus and the contract recordings move with them, and a new
  contract scenario (`max-mode`) masters both modes.
