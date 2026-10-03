### analysis_offline · tools — PEAQ Basic (ITU-R BS.1387): `analysis::Peaq` and `fcore_peaq`

- **`analysis::Peaq`** (`modules/analysis_offline/include/felitronics/analysis/Peaq.h`) — the Basic version of
  ITU-R BS.1387-2, written from the text of the Recommendation, with P. Kabal's examination for the unclear places:
  the FFT ear model (2048/1024, Hann, 92 dB SPL for a full-scale sine), the 109 bands of Table 6, internal noise,
  level-dependent spreading, forward masking, level and pattern adaptation, modulation, loudness, the eleven MOVs,
  the data boundary and the other frame selections of 5.2.4, stereo per channel with the binaural MFPD and ADB,
  and the 11-3-1 network. Out come the MOVs, the Distortion Index and the ODG.
- **Readings checked against GstPEAQ** (run as a black box; its source neither copied nor read), each written where
  it is made: the tail frame completed with zeros, the delayed averaging counted from the start of the programme,
  no bandwidth for a frame of digital silence, EHS from line 1 at lags 0..255. On 147 pairs (48 drum loops at
  graded damage, 99 masters of this core at nine loudness targets) the ODG agrees to 0.0003 on average and 0.0044
  at worst; NMR, the modulation and loudness MOVs, MFPD and RelDistFrames to 1e-6.
- **Verdicts.** `Transparent` when the whole-programme Total NMR is under -90 dB: the reported grade is 0 and the
  network's own answer stays in `modelOdg` (on a master that left the programme alone, GstPEAQ and this model both
  read EHS 52-67 and grade -2.1). `Undefined` when a MOV averaged over no frame (a reference that never reaches
  8.1 kHz, a programme under 0.5 s): the MOV is NaN, not a made-up 0. `NoSignal` and `NonFinite` as named.
- **Analyzer contract.** prepare() allocates everything (`storageFor()`, exactly); process()/finish() allocate
  nothing; any slicing gives the same bits; core::det, the core FFT and fixed sums, so native and wasm agree bit
  for bit.
- **`fcore_peaq <ref.wav> <test.wav>`** (or `--f32le <rate> <channels> <ref> <test>`) prints the verdict, ODG, the
  network's ODG and DI, and the eleven MOVs. Other rates go to 48 kHz through `core::DeliveryResampler`, the same
  plan for both. `tools/wasm/build.sh` builds the same source as `fcpeaq.node.js`; CI diffs the two outputs.
  About 0.9 s per minute of stereo natively, 1.0 s in wasm (node).
