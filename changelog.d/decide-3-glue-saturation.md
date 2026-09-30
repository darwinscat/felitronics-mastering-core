### session · mastering — glue and saturation from the normalised input; what each did, measured on its stage

- **One system of levels.** The input reaches the chain brought to the reference loudness by one gain, which the
  landing search adds once (`plan.inputGainDb`); the glue's threshold and the saturation's drive are read in that system,
  so one mix exported louder or quieter gets the same compressor and the same shaper. `writeDynamics` writes both
  stages — every field named — and touches neither gain of the chain.
- **Glue, "up to N dB"**, is the loss on the loud places: the core's own static curve takes exactly N dB at the input's
  short-term P95. One smooth formula over 0…6 dB — no step table, no rounding (`[glue] step`, `[compressor] roundToMs`
  and `roundToDb` are gone). The slider's 0…3 is a hint, the core takes 0…6; the machine sets it on cd alone, 2.6, and
  the config refuses a machine value above the slider's top. The release follows a tempo measured with confidence (0.5
  and up), 120 BPM otherwise, inside 50…500 ms; a clamp and the fallback are facts. **The calibration is the knob's new
  meaning** — 2.6 dB on cd is ratio 1.74 with the threshold 6.1 dB under the P95 — so the config's sound version moves;
  the defaults stay `2026-10`, not yet released.
- **Without a P95** the glue is unavailable to the machine and to a person alike, with its reason
  (`plan.glue.state == Unavailable`, fact `glueUnavailable`): the person's tick and value are kept, no threshold is
  invented, and the master is made without it.
- **Saturation** is the chain's tanh stage, never the machine's; its drive is the knob's at the input's true peak,
  k = 10^(drive/20) − 1, with no trial render.
- **The report** (`MasterCost`, additive; older snapshots decode the four as `NotImplemented`): `glueP95Db` and
  `glueMaxDb` — the compressor's own gain reduction; `saturationCutMaxDb` and `saturationCutUsualDb` — the largest and
  the usual cut of peaks over the loudest 5 % of the stage's quanta (`[saturation] cut.loudShare`), measured on the
  stage against its gain on a quiet sound, never the fall of the chain's true peak. Facts `masterGlue` and
  `masterSaturation` are published with the master's cost; the completion unit's event batch grew by two.
- `mastering::MasteringChain::clipperPeaks (loudShare, out)` and `clipperQuietGain()`: the soft clipper counts the peak
  of its input against the peak of its output every whole quantum — counters only, cleared by `reset()`; the audio is
  the bits it was.
