### session · analysis_offline — the low end measured from 10 Hz, the lowest note sought from 25 Hz

- `[lowEnd.run] lowNoteHz` 20 → 10 (owner, 06.10: the rubbish under 20 Hz is of interest): the low-end table is 59
  semitone bands, MIDI 4…62 (10.30…293.66 Hz), was 47 from MIDI 16. At fftOrder 17 the bands under 25.36 Hz at 48 kHz
  (23.30 at 44.1, 50.71 at 96) are narrower than a Hann main lobe and are published as unresolved.
- New `[lowEnd] lowestNoteFromHz = 25` (owner, 06.10: «the lowest note from 25 Hz — not 20, 25»): the planner's sure
  lowest note and the `lowestOccupied*` reading search from the first band whose centre lies at or above it (G#0,
  25.96 Hz); a band under it is skipped, never the note and never in the way of the bands above. Schema range 0…200 Hz.
- `LowEnd::lowestOccupiedBand (dutyMin, fromHz = 0)`: the search may start at a band centre; the default searches the
  whole table as before.
- The sound version of the `2026-10` defaults moves (updated in place, no saved project exists): a mix whose lowest band
  on lay between 20 and 25 Hz now takes its note from the bands above it.
- A mix with no bass says "unsure" again (owner, 07.10). From 25 Hz up, the leakage of a high tone, a dither or a rumble
  could light the first band and pass every test of a sure note, so a mix with no bass named "lowest note 25.96 Hz,
  below the floor"; v0.18.0 said "unsure" only because its first band, 20.60 Hz, was unresolved. New
  `[lowEnd] occupiedAboveBackgroundDb = 6`: the lowest band's density must also stand 6 dB above the note range's
  background (`backgroundDensity`, the median density of the other bands), or it is no note — a veto, never a skip to the
  band above. The planner's sure note and `lowestOccupiedSure` apply it alike. A band lit by leakage, dither or rumble
  reads within 2 dB of the background; a played bass reads 17 dB over it and more. Schema range 0…60 dB; it is a sound
  number, so the `2026-10` sound version moves with it.
