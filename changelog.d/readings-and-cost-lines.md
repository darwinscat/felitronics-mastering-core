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
