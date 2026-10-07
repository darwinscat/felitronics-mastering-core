### session — `DitherFloor`: the noise floor the delivery leaves, per bin

- New query kind `DitherFloor` (14, owner, 07.10): rows `[hz, dbPerBin, reason]` on a log grid of `columns` points from
  `fromHz` (> 0) to `toHz`, for the current delivery — the plan's dither (bit depth, on, shaping) at the target's rate.
  The quantiser's noise (TPDF ±1 LSB with its rounding, LSB²/4; plain rounding without dither, LSB²/12) is shaped by the
  dither's noise transfer function (Weighted, Psychoacoustic) and given in the forensics `meanPower` convention — the
  power one bin of the source's 16384-point Hann analysis reads — so it lies on the song's spectrum on one axis. At
  48 kHz: 16-bit TPDF −138.47 dB per bin, flat; Weighted −162.0 dB at 1 kHz and −128.9 dB at Nyquist; 24-bit rounding
  −191.4 dB. Above the delivery's Nyquist the reason is `Unsupported`; a delivery of 32 bits (no quantiser) is `NoSignal`.
- The codec schema appends the kind and its row (`QueryDitherFloor`); `FC_SESSION_ABI_VERSION` moves 13 → 14, no entry
  point.
