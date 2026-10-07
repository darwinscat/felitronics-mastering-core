### session — two diode types for the saturation, picked by hand

- A person may now pick two more saturation types (owner, 07.10): "Diode (symmetric)" — the core's cubic soft clipper,
  odd harmonics only — and "Diode (asymmetric)" — the core's asym shape, a second even-harmonic colour beside tube. Their
  words are new terms (`SaturationTypeAsym` 152, `SaturationTypeCubic` 153). The machine never picks either; atan stays
  the config's.
- The asymmetric diode runs at a fixed bias, `[saturation] bias = 0.2`, with its DC blocker at `dcBlockHz = 10`, written
  for it alone; every other type keeps bias 0 and no blocker, so no existing master changes. Measured: at 3.3 dB of drive
  its second harmonic stands at −35 dBc (−0.9 dBFS sine), against tube's −25; it equals tube's curve only at 8 dB of
  drive, the knob's red line.
- The kit draws both curves (`Kit::saturationCurve`, `fc_kit_saturation_curve`).
- The `2026-10` sound version moves (updated in place).
