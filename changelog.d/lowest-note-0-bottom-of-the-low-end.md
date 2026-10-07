### session — real mixes keep their lowest note; a mix with no bass still says "unsure"

- v0.19.0's background veto took the lowest note from real mixes. In a dense mix the lowest note stands only −6 to
  +0.4 dB over the median band of the note range, so the 6 dB veto said "unsure" (Cold Gaze of Eternity: D♯1 in v0.18.0,
  unsure in v0.19.0). `[lowEnd] occupiedAboveBackgroundDb` is removed (owner, 07.10).
- In its place, the bottom of the low end: the lowest band that was on at all is a note only if the band under it was
  never on, a band under the search start included. Noise, dither, a high tone's leakage and rumble light the bands on
  both sides of the search start alike; a played lowest note has nothing on under it. On real mixes it gives the notes
  v0.18.0 gave, and every no-bass case of the tests stays unsure. It has no number of its own.
- The planner's sure note and the report's `lowestOccupiedSure` use it. A regression guard in the tests holds the
  recorded low end of the two demo songs.
- The `2026-10` sound version moves (updated in place; no saved project exists).
