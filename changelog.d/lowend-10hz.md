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
