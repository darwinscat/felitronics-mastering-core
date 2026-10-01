### session — the device cards' advice and the targets' notes are facts

- **The advice beside a knob.** `plan.facts` states, from the value as it sounds (hand over machine): the high-pass
  cutoff below or above the comfort window `[hpf] comfort` (`hpfBelowComfort` 500, `hpfAboveComfort` 501, with the
  window), its slope gentler or steeper than `slopesNormal` (`hpfSlopeGentle` 502, `hpfSlopeSteep` 503), the EQ curve of
  the shelves past `[eq] curve.warnDb` (`eqOvershoot` 504, "{db} at {hz}", said of tilt or low), mono bass outside every
  `[monoBass.zones]` zone (`monoBassOutsideZones` 505). `kPlanFacts` grows to 18; `plan.hpf.soundingSlope` is new.
- **The target's note.** targets.toml gains `[notes]` (measured, practice, noNormalisation; presentation — `sound` does
  not move, `all` does); the snapshot carries `targetNote` beside `target` (`targetMeasured` 506, `targetPractice` 507,
  `targetNoNormalisation` 508). Fact range 500–599 declared for the plan's advice and the targets' notes.
