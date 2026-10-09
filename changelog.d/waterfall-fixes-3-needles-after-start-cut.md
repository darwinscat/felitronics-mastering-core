### mastering — the limiter's clipper takes its configured cut after the start clipper's last move

- With two clippers, the limiter's own clipper is set to take `[limiter.peakClipper]`'s cut off the loudest peak at its
  input. A move of the start clipper moves that peak, and its threshold kept the peak measured before the move, so the
  regrown peaks were cut by more or less than the configured 1.5 dB. The landing now measures that peak again after the
  start clipper's last move — one render more, never a candidate — and the cut lands on its configured amount.
- Masters with a cut wish while the glue or the saturation sounds move: on the test fixture Maximum · dense, extreme and
  nuke at the page's shares. Masters with no wish do not.
