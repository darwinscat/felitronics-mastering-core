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
