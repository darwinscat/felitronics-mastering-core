### session — every manual knob is stepless

- Step 0 means no step (owner, 06.10 and 07.10: «все ручки недискретные»): `Kit::parse` keeps a typed value as typed
  instead of rounding it to a grid, `Kit::valueAt` maps a slider position linearly onto the travel, and `Kit::travel`
  reports step 0. The schema admits 0 for exactly the steps listed below.
- Set to 0: the high-pass cutoff (`[hpf] hzStep`, was 1 Hz), mono bass's crossover (`[monoBass] frequencyStep`, was 5 Hz),
  tilt, low and the five EQ bands (`step`, was 0.1 dB), the glue's amount and mix (`[glue] knobStepDb` 0.1 dB,
  `mixStep` 0.2), the saturation's drive (`[saturation] driveStep`, 0.5 dB), the peak clipper's manual cut
  (`[limiter.peakClipper] manualStepDb`, 0.1 dB) and the target's loudness and ceiling (`targets.toml [edit] lufs/tp
  step`, 0.1). Two knobs keep their steps, and their schema still refuses 0: mono bass's width (`[monoBass] lowWidthStep`,
  0.05) and the saturation's mix (`[saturation] mixStep`, 0.05). Travels and domains are unchanged; the machine's own
  values do not move.
