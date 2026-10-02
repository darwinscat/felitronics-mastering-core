### session — the gentle tilt (02.10)

- **Tilt sounds on first-order shelves** (owner, 02.10): the tilt's band now asks felitronics-core for slope 6, its
  first-order tilt (`matched::lowShelf1` / `highShelf1`, in the felitronics-core release that ships them), and the
  curve the page draws (`eqCurve`, `eqOnlyCurve`, the EQ preview `fc_kit_eq_curve` and its finding) computes the same
  shelves with the deterministic maths. Pivot (1 kHz), knob range and meaning stay: low end −dB, top +dB, ends 2·dB
  apart. At +3 dB: 125 Hz −2.91 · 250 −2.64 · 500 −1.79 · 1k 0 · 2k +1.79 · 4k +2.65 · 8k +2.91 — was −3.00 · −2.98 ·
  −2.64 · 0 · +2.64 · +2.98 · +3.00, the whole 6 dB inside 500 Hz…2 kHz.
- The machine never sets a tilt, so no machine decision moves; a master with a hand tilt sounds different, and the EQ
  curves and findings with a tilt move. No config value changed, so neither config version nor sound version moves.
  The kit corpus pin (`KitTests.cpp`, `tools/wasm/session-check.mjs`) moves to the new curves.
