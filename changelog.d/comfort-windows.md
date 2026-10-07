### session — coloured fields for mono bass's crossover, the glue and the saturation's drive

- Four new comfort windows, read by `fc_kit_heat` / `Kit::heat` as the high-pass's is (owner, 07.10): neutral inside,
  shading towards the warning edge, red past it.
  - `[monoBass] comfort`: neutral 100–180 Hz, red at 75 Hz and at 250 Hz.
  - `[glue] comfort`: neutral 0–1.5 dB, red at 6 dB (the domain's end; the knob's travel ends at 3 dB).
  - `[glue] mixComfort`: neutral 30–100 %, red at 0.
  - `[saturation] driveComfort`: neutral 0–1.5 dB, red at 8 dB.
- One shared `config::Comfort` (low, high, warningLow, warningHigh) and one schema reader, `readComfort`: each window
  lies inside its knob's domain, warningLow ≤ low < high ≤ warningHigh. A side that does not exist is an edge at the
  domain's end. The windows are presentation, outside the sound version.
