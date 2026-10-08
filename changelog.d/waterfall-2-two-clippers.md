### session · mastering — two clippers: the cut zone at the start of the chain

- With a cut wish where the glue or the saturation sounds, a peak clipper at the start of the chain — after the
  high-pass and mono bass, ahead of the glue, on an oversampler of its own at the chain's factor (the limiter's
  oversampling) — takes the cut zone by a steered threshold, cut from the peak its first pass measures. The limiter's own
  clipper stays as the wish set it, unsteered, for the peaks the glue and the saturation regrow; what it took is the
  waterfall's `regrownDb` (a P95 over what it clipped), a part of the limiter's rest.
- `[limiter.peakClipper] place` (new): `both` (the config's) as above, `start` turns the limiter's clipper off,
  `limiter` keeps the cut in the limiter's clipper alone, as before. Without a sounding glue or saturation the limiter's
  clipper takes the cut whatever `place` says.
- The mastering ABI (`fc_master`) pins the start clipper's switch, cut, peak and bypass in its layout and leaves them
  unmapped, as the limiter clipper's cut and peak are, so its renders and `fcore_master`'s do not move.
