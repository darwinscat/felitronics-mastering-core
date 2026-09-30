### session · mastering — the limiter with its needles, the dither and the whole plan sounding; the observations

- **A master the session decides is rendered.** `master` without a ready chain (version 0) takes its chain from the
  project's devices (`writeChain`: `writeEq`, `writeDynamics`, `writeLimiter`, mono bass) — the topology from the
  devices as they sound, never `[stages]`; the fixed geometry stated in `[chain]`, `[compressor] lookaheadMs,
  sidechainHpfHz` and `[limiter] lookaheadMs`. Its demand is declared by the command whole; a master that waits for its
  measurements fixes its chain when the wait ends (a heap too small then: a memory error under its own id). It is,
  sample for sample, the previous path's master of the same chain handed in ready.
- **The needles' classes** (owner decisions 3.6, 3.7): short needles cut from 3 dB above the ceiling, the ones between
  from 1.5 dB, long, bassy, already-limited and clipped material not cut (`plan.limiter`, `NeedlesClass`, `NeedlesWhy`);
  a clipped source is ten confirmed clips a minute (`[limiter.peakClipper] clippedPerMinute`). A person's manual
  threshold — or a threshold turned alone — sounds over every refusal, with the machine's reason beside it. "The same
  ceiling" is decided by the bits in `requestNeedles` as in the plan.
- **The dither** by the delivery's format alone: 16 bits, weighted TPDF from the stated seed, blanked after 4096 zero
  samples (`[dither] seed, autoBlank, autoBlankSamples`); a person's off rounds without noise; a tick above 16 bits is
  kept without effect (`plan.dither`).
- **Vinyl** (`[targets] vinyl`, lp; owner decision 3.12): the machine never above the medium's ceiling and never cutting
  needles; a person's hand is obeyed and warned; the master's report says "ready for cutting", what the file shows and
  what it cannot (`MasterReport::medium`), and the plan carries the constant note about the top above 16 kHz
  (`[observations] vinylTop`).
- **A quiet input** (owner decision 3.13), strictly under −55 LUFS: the gain, the ceiling, the format's dither and the
  high-pass at its floor, nothing else of the machine's; the report says so.
- **The observations** (`snapshot().observations`, additive): DC, unused bits, silence at the edges, clips by place, a
  quiet or short input, an input already limited, a spectral wall, the low notes, infra-low, wide bass, polarity,
  sibilance (shown whatever the de-esser), hum — found, not found and not measured apart, with confidence, severity, the
  config's style, `handledBy` and the hypotheses marked. They change nothing.
- The wide-bass warning no longer tells a person to switch mono bass off: "mono bass will gather it if it is on".
- The defaults stay `2026-10` (not yet released); their sound version moves (the chain's geometry, the dither's noise,
  the clipped-source bound, lp marked as vinyl). The event batch grew by four (a master's medium and input lines).
