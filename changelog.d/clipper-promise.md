### session · mastering — the peak clipper takes at most its amount, at any landing gain; its manual travel to 6 dB (02.10)

- **The clipper keeps its promise** (owner, 02.10): a manual or machine cut of N dB set the threshold at need − N above
  the ceiling, a forecast from the input's true peak at the landing's first gain; the landing then added gain for what
  the limiter takes off the loudness and lowered its pass ceiling, and every dB of that went into the clipper — a manual
  2 dB at −9 LUFS / −1 dBTP took 5.7 dB off the peaks. `MasteringChainParams` appends `peakClipCutDb` and
  `peakClipPeakDb` (NaN by default: the threshold stays as given): the chain works the threshold out for the gain and
  ceiling it renders at, max(0, peak + gain − ceiling − cut), the clipper off where that passes the limiter's range.
  `LandingSearch` replaces the session's forecast peak with the one its first pass measured at the limiter's input
  (before the clip, on its oversampler); that pass steers the search and is never delivered or restored, so a clipper
  landing its first pass would have finished pays one render more (the three test masters: 3 passes, as before). The
  same 2 dB now takes 1.999999 dB. `fc_master` does not map the fields: its renders are bit for bit as before.
- **The manual cut runs 0…6 dB** (owner, 02.10), the whole of `manualDomain`, by 0.1 dB: `manualMaxDb = 6`;
  `fc_kit_travel` answers it.
- Sound moves for every master whose peak clipper cuts. The config's sound version moves (2026-10 updated in place: a
  person's knob travel, no machine value), and with it the event pins and the scenario's parity line.
