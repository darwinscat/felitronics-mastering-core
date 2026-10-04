### analysis_offline · tools — PEAQ Basic (ITU-R BS.1387): `analysis::Peaq` and `fcore_peaq`

- **`analysis::Peaq`** (`modules/analysis_offline/include/felitronics/analysis/Peaq.h`, target `felitronics::peaq`) —
  the Basic version of ITU-R BS.1387-2, written from the text of the Recommendation, with P. Kabal's examination for
  the unclear places: the FFT ear model (2048/1024, Hann, 92 dB SPL for a full-scale sine), the 109 bands of Table 6,
  internal noise, level-dependent spreading, forward masking, level and pattern adaptation, modulation, loudness, the
  eleven MOVs, the data boundary and the other frame selections of 5.2.4, stereo per channel with the binaural MFPD
  and ADB, and the 11-3-1 network. Out come the MOVs, the Distortion Index and the ODG.
- **Where the text decides, the text wins**: |INT(e)| steps (eq. 78), RelDistFrames at >= 1.5 dB, the bandwidth of a
  frame whose test is digitally silent (-inf levels compared as IEEE does), the loudness threshold as two marks per
  channel, the delayed averaging from the start of the measurement. **Where it leaves a choice, GstPEAQ decided** (run
  as a black box; its source neither copied nor read): the tail frame completed with zeros, EHS from line 1 at lags
  0..255 with the mean removed before the window (window-first gives EHS ~100x outside Table 13's range). On the 158
  pairs GstPEAQ grades (48 drum loops at graded damage, 99 masters of this core at nine loudness targets, 11 synthetic
  pairs) the ODG agrees to 0.0015 on average and 0.032 at worst; it differs where the text and GstPEAQ part: frames in
  which both programmes are silent, and a pair whose loud passages never overlap (GstPEAQ: nan).
- **Verdicts.** `Graded`; `Transparent` (ODG 0, the network's answer kept in `modelOdg`) when every channel's Total
  NMR is under -90 dB and its waveform error under -40 dB — on a master that left the programme alone GstPEAQ and this
  model both read EHS 52-67 and grade -2.1; `Undefined` (checked first) when a MOV averaged over no frame, which is
  then NaN; `NoSignal`, `NonFinite`, and `OutOfRange` for a sample past +18 dBFS, where the spreading stops being
  defined.
- **Determinism.** core::det, the core FFT (one transform per programme, so a test identical to its reference reads
  an error of exactly zero), fixed sums. `felitronics::peaq` carries -ffp-contract=off -fno-fast-math (MSVC
  /fp:precise) as INTERFACE options and the header refuses a unit built without them. prepare() allocates exactly
  `storageFor()`, process()/finish() nothing, any slicing gives the same bits.
- **Conformance (7.4) is not proven**: the 16 ITU test items are not openly available.
- **`fcore_peaq <ref.wav> <test.wav>`** (or `--f32le <rate> <channels> <ref> <test>`) prints the verdict, ODG, the
  network's ODG and DI, and the eleven MOVs. Other rates go to 48 kHz through `core::DeliveryResampler`, the same plan
  for both (its kernel uses the system libm, so only 48 kHz input is bit-identical across rows). Programmes of
  different lengths, and programmes past 2 GiB of working memory, are refused. `tools/wasm/build.sh` builds the same
  source as `fcpeaq.node.js`; CI diffs the two at 48 kHz. About 1.0 s per minute of stereo natively, 1.2 s in wasm.
