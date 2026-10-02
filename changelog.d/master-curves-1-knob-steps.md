### session — the owner's knob steps (02.10)

- **Mono bass "below"** (the crossover knob, `[monoBass] frequencyStep`) steps by 5 Hz, was 1; **"cut off the peaks"**
  (`[limiter.peakClipper] manualStepDb`) by 0.1 dB, was 0.5 — what `fc_kit_travel` gives a slider. A step is in the
  sound version (a typed value is placed on the knob's grid), so the `2026-10` defaults' sound version moves, updated in
  place as before (no project of those defaults was ever saved); no machine value and no WAV byte moves.
- The contract recordings are re-recorded from one clean `tools/wasm/build.sh`: the config's version, the measurement
  keys and the recipe's sound version move with the steps, the codec schema's hash with `GlueGr`, and the declared bytes
  of a master by 64 on wasm32 (the master's rows now own the glue's trace). The WAV bytes do not move
  (`9a601c4c5e044b00`).
