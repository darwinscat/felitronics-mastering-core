### mastering — the saturation's trace window counts the start clipper's latency

- The saturation's trace is cropped at the soft clipper's output time, which now counts the start clipper's latency
  too. On a master with the start clipper (a cut wish where the glue or the saturation sounds) the window started and
  ended that many frames early, so the programme's last frames were missing from the trace. Statistics only: no sample of
  any master moves.
