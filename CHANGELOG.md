<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Changelog

## Unreleased

### tempo — the site's BPM detector, ported (`felitronics::tempo`)

New module `felitronics::tempo` (namespace `felitronics::tempo`, links `felitronics::core` alone):
`TempoDetector` is the site's `dsp/tempo.js` — `tempoCurve` (`headline()`) and `detectTempo` (`wholeTrack()`) from
one analysis — over the mono mix the page's `toMono` makes: the whole-track BPM, its confidence and label, the
in-range half/double alternatives, the beat period and offset, the top five autocorrelation candidates, the tempo
curve (6 s windows every 1.5 s, median of five), its 10–90 % range and `varies`. The spec's options are
`TempoParams` (BPM range, window, hop). Streaming with the bits of one call under any split; the width is exact;
`storageFor()` is the allocation, to the byte. `JsNumerics.h` carries the JavaScript arithmetic the port
reproduces: V8's `Math.hypot` (bit-exact against node on 2 000 225 pairs; the system hypot differed on 491 004),
`Math.round`, `Math.min`/`Math.max`, and an `exp` on the deterministic floor.

Measured against the JavaScript (node 26) on the same float32 samples — 19 synthetic programmes (click trains at six
tempi and two rates, a tempo change, a lone click, silence, noise) and eleven real stereo mixes of 106 to 519
seconds through the page's `toMono`: every reported field identical — bpm, confidence, label, alternatives, beat
period and offset, `varies`, range, and all 2 663 curve points' time, bpm and confidence; the unrounded whole-track
tempo within 2.1e-16 relative, the candidate scores within 3.2e-15, the onset curve within 6.2e-14 of its own peak.
The only numeric departure is the Hann window: `core::det::cos` and V8's `Math.cos` disagree by one ulp on 174 of
its 1024 coefficients (the FFT's ten twiddle seeds agree bit for bit). The wasm module agrees with the native build
on all of it bit for bit (8 689 published numbers over 30 programmes), and fed the stereo planes it reproduces the
page's mix. About 3.7x the speed of the JavaScript natively (a 6-minute mix: 0.4 s against 1.5 s).

`fc_probe_tempo_run` / `_run_with`, `_scalars` (35), `_candidates`, `_curve` (t, hasBpm, bpm, conf, and the
window's bpm before the median), the three widths, and `_storage_bytes` / `_storage_bytes_with`, which take the
programme's length. NaN is the spec's `null`, each beside a field that says whether the value is there; an empty
programme is a measurement (undetermined), not a refusal. Suites: `felitronics_tempo_tests`,
`felitronics_tempo_numerics_tests`, `felitronics_tempo_abi_tests`; both headers are in the det-math zone.

### tempo — native against wasm, gated

`fcore_measure tempo` prints the detector's whole surface — both headlines, the anchor, the range, the five
candidates and every curve point, doubles as bit patterns and the spec's `null` as `nan` — and
`tools/wasm/tempo-parity.mjs` prints the same bytes from the module (`tempo-format.mjs` is the shared half). CI's wasm
job diffs the two on three generated programmes with a beat that changes (`make-tempo-fixture.mjs`: clicks at one
tempo then another over integer-code noise, no transcendental, bytes pinned) at 48, 44.1 and 22.05 kHz and one, two
and six channels, under seven re-slicings (`--chunk`, honoured natively, the module measures in one call), four
parameter sets, the loudness fixture and an empty programme, release and checked modules; it asserts the comparison
saw a determined tempo that varies and a curve, and that both roads refuse the same seventeen command lines. Measured
locally: 64 comparisons, every one identical.

### fc_probe_crest_storage_bytes — no price for a span the run refuses

The crest price takes the programme's length and quoted a positive number for programmes whose planes cannot fit a
32-bit address space (mono at 2^30 frames and up; sixteen channels at 2^26) — exactly the spans
`fc_probe_crest_run` refuses before reading a sample, so a page that asked first was promised a measurement it
could not have. Both crest prices now refuse them, through the same predicate the run and the tempo price use.
Pinned natively in `felitronics_analysis_abi_tests` (both sides of the bound, mono and sixteen wide, the
parameterised price, and the run on the same spans) and on the artifact by `storage-probe.mjs check`.

### fc_probe — an ABI version

`fc_probe_abi_version()` answers `FC_PROBE_ABI_VERSION`, declared in the new `tools/fc_probe_abi.h` beside
`fc_master_abi.h`, with the append-only rule that moves it: one number for the probe's whole surface, bumped by one
when an entry point is added or a published block or row grows at its end; nothing existing is renamed, reordered,
re-typed or removed. It starts at 1 — the surface above, the tempo entry points and itself included. Pinned natively
(`felitronics_abi_tests`, a literal as well as the constant) and on the artifact (`storage-probe.mjs check` reads
the constant out of the header and asks the module).

## v0.1.0 — 2026-09-25

### mastering · analysis_offline · the C ABIs — split out of felitronics-core

The mastering chain (`felitronics::mastering`), the twelve offline programme analyzers
(`felitronics::analysis_offline`), the two C ABIs with their native CLIs and suites, and the wasm build with
every native-vs-wasm comparison, from felitronics-core v0.51.0; target names, namespaces and header spellings
are unchanged (`<felitronics/analysis/...>`, `<felitronics/mastering/...>`). `felitronics::analysis_offline`
now owns its include root. New here: the law-11b case for `OfflineRenderer` and the chain through core's shared
harness (`felitronics::test_support`), the analyzers' half of the math-policy gate, a header-hygiene TU that
includes every public header (`PeakExcursions.h` and `StereoBandBursts.h` were outside core's), and this
repository's det-math zone and manifest (`tools/lint/`), linted by core's `check-det-math.mjs --satellite`.
