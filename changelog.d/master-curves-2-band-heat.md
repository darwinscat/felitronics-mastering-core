### session — the EQ bands' knobs are coloured as tilt's and low's (owner, 02.10)

- `fc_kit_heat` answers `window = true` for the five band gains (fields 134–138, body, mud, forward, brightness, air):
  each `[bands.*]` entry has a `normal`, read as tilt's and low's are, out to its travel. `[eq]` holds no band norm, so
  the window is tilt's and low's ±1.5 dB; the mud band, a cut alone (travel −3…0), is normal down to −1.5 dB. A window
  is a hint: it is left out of the sound version (like `tilt.normal`) and no master moves; the config's `all` version
  and so the recorded `weightsVersion` and config version move.
