### session — the lowest note is searched from 30 Hz, and nothing under 30 Hz is named a note

- `[lowEnd] lowestNoteFromHz = 30` (was 25; owner, 07.10: the bass's fifth string as the limit for everything). The search
  starts at B0 30.87 Hz, a five-string bass's lowest note. The spectrum and the drawings still start at 10 Hz.
- Under 30 Hz no fact names a note: the loudest band of the low end there is reported as low-frequency energy at its
  frequency (new fact `LoudestLowEnergy`, 458; the `loudestLowNote` observation's `third` is 1). The decisions do not use
  the name.
- `FC_SESSION_ABI_VERSION` moves to 15. The `2026-10` sound version moves (updated in place).
