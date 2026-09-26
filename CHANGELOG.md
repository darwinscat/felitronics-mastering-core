<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Changelog

## v0.2.0 — 2026-09-26

### tests — split invariance, one harness for every analyzer and the mastering path

`tests/split_invariance.h` and two suites (`felitronics_analyzer_split_invariance_tests`,
`felitronics_mastering_split_invariance_tests`) ask every streaming class what a caller that cuts its own chunks
needs answered first: is every output the stream decides — scalars, rows, event lists, histograms, traces, tap
streams, statistics — the same bits under one call, blocks of 64, 480 and 4096, seeded ragged cuts of 1..8192 with
zero-length calls, a second prepared `maxBlock`, the width timelines the contract allows (a clock-only gap in the
middle, at the head and at the end, a narrower stretch), and a finish on (and a read before finish of) a prefix?
For all twelve analyzers, the chain in two topologies with and without taps, `OfflineRenderer` at eight blockings
and ten short lengths, and the solver's `LoudnessMeter` + `ReferenceTruePeakMeter` pair, it is: 360 rows, on Apple
clang arm64, gcc 14.2 x86-64, MSVC 19.44 and wasm32. A one-ulp nudge at every call boundary is caught for every
class, and `ReferenceTruePeakMeter::truePeakLinearBlock()`, the last call's peak by definition, is reported as not
invariant. So are a `ProgressClock`'s events, which follow the pieces the work is cut into while the audio does not.
Measured and not fixed: `BandCrest::process (nullptr, 0, n)` is refused, where law 11a lets a clock-only call pass a
null plane array; `StereoBandBursts` and `PeakExcursions` latch their width on an empty call, where law 11d makes
`n == 0` a no-op.

### build — felitronics-core v0.53.0 is the minimum

The chain now calls `MonoBass::setBypass()` and `LaneDynamics::setReleaseOnDisengage()`, which first shipped in
felitronics-core v0.53.0, so the pinned `FELITRONICS_MASTERING_FCORE_TAG` moves from v0.52.0 to v0.53.0 and the
configure-time messages and `tools/wasm/build.sh` name v0.53.0 as the minimum.

### analysis_offline — the two contract edges the split-invariance suite found, fixed

Neither moves a number. `BandCrest::process (nullptr, 0, n)` is accepted now: law 11a lets a clock-only call pass a
null plane array, so a null array is refused only where a plane will be read (a null array at a live width still is).
`StereoBandBursts` and `PeakExcursions` latch their width on the first call that carries AUDIO, below the `n == 0`
exit and the plane check: law 11d makes `n == 0` a no-op, and an empty or a refused call used to latch it, so an
empty width-1 call ahead of the programme turned every stereo call after it into a refused width change. A width
that differs from a latched one is still refused, an empty call included. The split-invariance suite asserts both
now, with their negative halves, where it printed them as notes; every cut row is unchanged and INVARIANT.

### mastering — the gain nodes and the compressor mix glide instead of stepping

`inputGainDb`, `preLimiterGainDb` and `compressorMix` used to land on a quantum boundary as a step, which clicks on
a live preview: on a -12 dBFS 227 Hz sine at K = 128, max|Δ²y| where the change reaches the output was -20.1 dBFS
for inputGainDb 0 -> +3, -22.3 for preLimiterGainDb 0 -> +3 and -34.4 for compressorMix 1 -> 0.5, against -73.1 for
the steady tone. Each now moves by a fixed-length LINEAR ramp, `MasteringChain::kParamRampMs` = 30 ms (read back as
`paramRampSamples()`), per sample on the quantum's own clock, from the quantum the write lands on — -69.1, -69.4 and
-74.7. 30 ms was chosen by measurement: a +3 dB step is clean at every length from 5 ms, and 0 -> +12 dB reads -51.2
/ -58.0 / -57.2 / -60.8 / -60.2 dBFS at 5 / 10 / 20 / 30 / 50 ms against the louder tone's own -61.1, so 30 ms is the
shortest length at which a big jump sits on the tone's floor. The ramp accumulates in double (a float accumulator
drifted by ~len·ulp/2 and, at 352.8 kHz, drove a +59.5 -> -60 dB move through zero before landing — found by the
code-review round and pinned at 352.8 kHz, 768 kHz and 3 MHz). It arrives on the exact resolved value, so the
rest path is the constant multiply (and the mix branches) it always was; each glide sample of the mix is the stated
double blend at a float `m`, so the law-10 argument for the blend holds sample by sample. The FIRST write of a
stream — after `prepare()` or `reset()` — snaps: every offline render (`setParams -> reset -> process`, the renderer
and the solver) is bit-identical to the tree before this change, measured on six topologies including K = 8 and K =
100. The split-invariance suite gains two AUTOMATED chain rows — every glidable parameter and every bypass moved at
fixed stream positions on no grid — which must be the same bits under every cut, taps and statistics included.

### mastering — a dynamic point switched off mid-duck releases; the compressor's makeup and the stages' own glides

From felitronics-core v0.53.0 the stages glide on their own (the Saturator's drive, bias, mix and trim;
MonoBass's corners, its `enabled` and the air's; the Compressor's makeup and auto-makeup; the limiter's ceiling, down
over 2 ms), and each snaps on the
first write after a restart, so this chain's offline renders stay bit-identical (checked by hash on six topologies).
Two things are the chain's own: every `dynamiceq::LaneDynamics` producer is opted into RELEASE ON DISENGAGE, so a
point whose `dyn.on` goes off — or whose range goes to 0 — mid-duck releases its delta through its own ballistics
instead of snapping it (a -9 dB duck on a -12 dBFS tone: -16.1 dBFS max|Δ²y| before, -55.3 after, the rest being the
band's 16-sample control grid), and once released the chain renders bit for bit what a chain with a static point
renders; and an EQ bypass edge hands every band its caller's own parameters back, since a releasing producer holds
its band's dynamic seam open. `bypassCompressor` no longer clicks either: it writes makeup 0, which now glides (-30.5
-> -73.8 dBFS).

### mastering — bypass toggles fade: the clipper and the limiter warm up and cross-fade, mono-bass rides its own fades

A bypass of the clipper or the limiter was a skip with a `reset()` on BOTH edges and a hard swap, so a toggle clicked
and a return punched a hole: on a -12 dBFS 227 Hz sine at K = 128 (max|Δ²y| where the change reaches the output) the
clipper's round trip read -9.2 dBFS with a ~63-sample dropout and its return at drive 9 -7.3, the limiter's round trip
-12.5 with a 111-sample hole of exact zeros, and entering bypass while it held 6 dB -18.2. Each now has a FADER (five
modes, a position counted in samples of the quantum): entering bypass fades the stage to its aligned dry over
`kBypassFadeMs` = 10 ms and then stops calling it (a steady bypass is the skip it always was, bit for bit); leaving it
resets the stage, WARMS it up on the live input while the output stays on the aligned dry — for the stage's whole
finite memory, 2L+1 samples for the clipper and 2·O+A+1 for the limiter (its oversampler round trip around its
lookahead, the delay line and the peak window running in parallel) — and fades it back in; a reversal mid-fade turns
around from the weight it reached, and a bypass during the warm-up goes straight back to dry. Measured the same way:
-67.1 (round trip, no dropout), -63.6 (return at drive 9), -73.1 (limiter round trip, no hole), -73.1 (into bypass
while limiting), -70.7 (out of it), -63.2 / -75.9 (reversals), -65.1 (an Asym clipper's return), -68.7 (a dual-release
limiter holding 12 dB). 10 ms was measured against 5 (worst -60.9, the Asym return) and 20 (worst -64.3). After the
warm-up and the fade a restored chain is bit for bit the chain never bypassed wherever the stages' memory is finite
(a symmetric curve, a limiter not limiting); the Asym DC blocker and the limiter's release state start fresh, as any
reset starts them. The limiter's traces are its own whenever it runs, warming or fading included; they read zero only
while it is not called. Mono-bass's bypass now rides `stereo::MonoBass::setBypass` — the island's own 20 ms fades and
its own retirement — where it was a skip with a reset: -21.5 / -48.7 dBFS before, -74.3 / -75.5 after. Latency does not
move, and a render with bypass flags set before its first sample is the bits it always was (checked by hash).

### C ABI v14 — `fc_master_set_params`: a parameter set written while the stream runs

`fc_master_configure` re-prepares, so it is exact and refused (FC_ERR_STATE) once audio has been seen. The live preview
needs the other half, and this is it: `fc_master_set_params (h, params)` maps the set with the same `toCore` and hands it
to `MasteringChain::setParams()` — no preparation, no reset, no allocation — before, during and between streams. The set
lands on the chain's next internal quantum and GLIDES from there by each stage's own rule (the gain nodes and the mix,
the Saturator, MonoBass, the compressor's makeup, a dynamic point switched off, the limiter's ceiling, the bypass
fades); several calls before that boundary are the last one alone; and the first quantum of a stream snaps, so a set
written before the first frame renders exactly what `fc_master_configure` with that set renders (pinned).
`fc_master_resolved_get` lags ONE QUANTUM behind it — it reads what the chain applied — which the header states. Refused
where `fc_master_process` is refused (a delivering handle; a handle that has solved, until a configure) and for
configure's struct and value reasons, with the stream untouched. One entry point and no struct, so the size table
has no new row (as v7 and v9). The JavaScript half moves with it: `FC_MASTER_ABI_VERSION = 14`, and `FC_STATUS` gains
the three v13 codes (16–18) it had been missing for a version — layout-check.mjs now holds `FC_STATUS` against the
header's `fc_status` (count, order, value, name), so the next code cannot go missing the same way. MasterAbiTests: the
ABI stream with a live write held bit for bit against the C++ chain driven the same way, the resolved lag, the snap
before the first frame, every refusal and that it moves nothing, no allocation, the poison list. The wasm modules and
the parity scripts (master-parity on both generated programmes, the probe NULL) pass against a build of this tree.
The review round (codex astra) found no defect in the entry point and three gaps in its tests, all closed: every
accepted write went through `goodParams()`, the frozen v1 writer, so no field past v1 — `compressorMix`, the one that
glides, dual release, the peak clipper, the air shelf — ever crossed this call (a setter that forced the mix to 1
passed); now a current-version set with each of them moved is held bit for bit against the C++ chain too. Refused
writes after a valid one are shown to leave it pending as it was (a refusal that applied its half-mapped set is
caught), and a malformed call on a delivering or a solved handle answers FC_ERR_STATE — the handle's state first.

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
