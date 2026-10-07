### analysis_offline · session — a rumble under 30 Hz no longer hides the bass

- Each frame's loudest band, which every band's occupancy is measured against, is now sought from 30 Hz up
  (`LowEndParams::noteFromHz`, fed from `[lowEnd] lowestNoteFromHz`; owner, 07.10). Before, a rumble under the search
  start more than about 18 dB louder than the bass kept the bass from counting as "on", and the planner said "unsure". Now
  E1 under a 15 Hz rumble 26 dB louder is found.
- Only that reference moves: the band energies, the peak, the background, the range's share and the frame gate are bit for
  bit as before, and a band under 30 Hz is still measured against the new reference.
- With a rumble alone, its leakage is the loudest band from 30 Hz up, so the report may name B0 as the lowest occupied
  band, unsure; the plan stays at the floor.
