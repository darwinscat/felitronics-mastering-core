### session · analysis_offline — the low-end drawings: the unresolved bands' values and a table up to 494 Hz

- `LowSpectrum` and `LowSide` give the measured value of a band narrower than a Hann main lobe instead of `TooShort`
  (owner, 06.10 and 07.10): the drawings show what was measured down to 10 Hz. The unresolved flag stays on the bands
  array, and no decision reads these queries.
- The low-end table reaches 493.88 Hz (`[lowEnd.run] highNoteHz` 300 → 500, 68 bands, MIDI 4…71), so the side share
  can be drawn for mono bass up to 500 Hz. New `LowEndParams::noteTopHz` / `[lowEnd.run] noteTopHz = 300`: every
  reading of the note — occupancy, the loudest band, the runner-up, the background, the NoEnergy gate,
  `totalBandEnergy()` and `bandRangeShare()` — sees only the bands up to 300 Hz, bit for bit what a 300 Hz table gives.
  A band above it carries energy, density and side for a drawing, and an occupancy of 0. The default 0 means the whole
  table, as before.
