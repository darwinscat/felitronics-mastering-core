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
