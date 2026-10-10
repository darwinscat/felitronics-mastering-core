### docs — the waterfall's known limit, said as it is

- `docs/SESSION.md` said the steering is outside the det-math zone, so its decisions use the platform's libm. Its
  files are outside the zone, but its own arithmetic calls `core::det` already (`LandingSearch.h` and `LoudnessSolver.h`
  call no transcendental of the platform's libm; `MasteringChain.h` calls four, each once a setting, none per sample).
  What runs on the platform's libm is the audio the steering measures: the saturator's `tanh` on every oversampled
  sample, the compressor's `log10` and `pow` on every sample and the limiter's `log10` and `pow`, all
  felitronics-core's. So a steered master's bits are the platform's, and a last-bit difference in a take can move a
  0.1 dB step of the steering.
- It stays so by decision: those stages on det-math were measured at about ×1.7 of a master's time (Apple M5 Pro,
  native, three masters, one run) and not taken. No code moves.
