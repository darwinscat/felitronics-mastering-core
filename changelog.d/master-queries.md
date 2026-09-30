### session — a master read by queries, a lean summary, and the landing's facts published

Additions to `fc_session` version 1; a version-1 shell sees the bytes it saw.

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
