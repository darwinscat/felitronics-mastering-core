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
