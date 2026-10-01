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
