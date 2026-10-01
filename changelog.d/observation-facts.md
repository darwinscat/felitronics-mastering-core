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
  `observationFacts` is optional (a snapshot without it decodes as no lines). The decoder refuses an unknown fact id or
  kind. The config's version moves with `engine.toml`: the recordings move with it, with the snapshot JSON and the
  memory counts, and by nothing else.
