<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Changelog

## Unreleased

### session — bounded WAV delivery

Completed safe masters expose an additive, token-guarded WAV size and bounded slice copy. The shell can
assemble one owned RIFF image before releasing session PCM and repeat downloads without rerendering or
adding dither. The pure planar writer supports PCM16 and PCM24, with the installed core's PCM
grid and odd-chunk padding. Native and wasm checks decode the written bytes and verify the delivered
reference true peak; cancelled, unavailable and unsafe jobs expose no file, while a safe twelve-pass miss does.
Session measures the selected PCM on its delivery grid, and the decoded WAV equals the listening PCM
sample for sample; direct solver calls keep legacy float output by default.

### session — measured master report and comparable crest rows

The ready master now retains delivered LUFS, reference true peak, suitable PLR/LRA, gain from the source, signed target miss, ceiling safety and measured mix hints. A missing LRA carries a reason. Linear band-crest rows use the source's explicit rate, hop and corners and preserve its activity mask when comparable. Rate conversion adds one source-rate check pass with the winning settings, using bounded scratch while the delivered PCM stays intact. Snapshot and the generated codec own the compact rows; older v1 snapshots decode without a report.

### session — ready master job and owned audio transfer

An additive `fc_session_master` entry takes frozen, versioned mastering topology and parameters with a source and revision fence. The session prices the job before allocating, drives the twelve-pass landing search in work units, and keeps the selected delivery PCM with its recipe and compact measurement rows. A target edit leaves the running recipe intact. Cancellation removes only unfinished work; a new source invalidates old transfers. `fc_session_master_audio_*` reports shape, copies, provides a scoped wasm view for one independent `ArrayBuffer` copy, and releases session PCM explicitly. The frozen v1 JSON Master command retains its original behavior for existing callers; new callers use the ready entry. Appended snapshot and codec fields carry conservative decode defaults.

### mastering · session — aligned limiter and K13 traces

The limiter's existing oversampled K13 clipper now exposes its reduction as a time tap without changing
audio or aggregate readings. Delivered mastering stores limiter and clipper min/max/mean rows on one
frame grid, including the final drain. Session snapshots own both series, the generated codec decodes
their absence in older snapshots, and `fc_session_query_*` accepts bounded master trace queries.
The pinned felitronics-core v0.56.0 release supplies the companion tap and the WAV grid and pad implementation.

### mastering · session — saved loudness landing search

The product landing now has one budget of at most twelve measured renders. It stops at a measured hit, otherwise
keeps the closest output whose delivered reference true peak holds the target ceiling. Its resumable source survey,
render, statistics, gates, final copy and independent remeasurement expose cancellation without publishing an
unverified file. Results carry the achieved level, miss, typed reason hints, deterministic work and full pass log.
Session planning keeps −18 LUFS source normalization separate from search gain and records a source-rate impact pass
when delivery changes rate. The optional landing result extends the generated session codec and older v1 snapshots
continue to decode. Differential tests compare the saved pass executor with the previous whole pass bit for bit.

### mastering — resumable delivery render and bounded PCM

Delivery conversion and offline rendering now retain their source, latency, tap, and drain cursors across bounded steps.
The whole APIs use those same steps. A delivery job prepares its chain, renderer, and converter in separate units; a
zero budget does no work, and cancellation followed by a new job resets every state. Delivered searches reconvert the
original source on each pass and feed SRC blocks into the chain using the caller's output buffer, removing the extra
complete converted programme. `storageFor` declares the source, output, and conservative workspace live set in 64-bit
arithmetic. Split, replay, allocation, and prior-path bit comparisons cover the new render path.

## v0.2.2 — 2026-09-26

### tempo · tools · wasm — the first tempo analysis is as fast as every later one

A page no longer needs a warm-up run before the first tempo. In the browser the first analysis on a freshly loaded
`fctempo` took more than twice as long as the next: **1.11 s against 0.48 s** on a 6:15 stereo mix at 48 kHz (node
26; 1.11 s against 0.49 s in headless Chromium 151), 1.54 s against 0.67 s on an 8:39 one. Now the first costs what
the others do — **0.47 s, 0.49 s in Chromium, 0.66 s** — and `fcprobe`'s tempo moves the same way (0.62 → 0.48 s).
Every later analysis is unchanged or a little faster; the native tool is unchanged. The answers do not change by a
bit.

Why the first was slow: V8 gives a wasm function optimised code only on its next call — there is no on-stack
replacement for wasm — and emcc's post-link optimiser (binaryen) had folded the detector's whole analysis into the
one exported call, so the first analysis ran entirely on the baseline compiler. `TempoDetector` now keeps each loop
that carries time in a function called many times per analysis — `mixIn()` and `onsetFrame()` once per onset
frame (`process()` walks a call in stretches that end on the sample completing a frame), `lagSum()` once per
autocorrelation lag, `analyzeWindow()` once per window — each `noinline`. That alone is not enough: binaryen inlines
every function with a single caller whatever LLVM decided, `noinline` included, so `tools/wasm/build.sh` links
`fcprobe` and `fctempo` with `-sBINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=0`, which turns that one
rule off. On `fcprobe` that also takes a whole programme-report run in node, loading included, from 1.52 s to
1.04 s, for the same reason; no analyzer got slower, every output of every analyzer is byte-identical, and the modules move by +332 bytes (`fctempo`) and −506
bytes (`fcprobe`) under brotli.

Byte-identical to v0.2.1, native and wasm, both modules: 24 tempo rows — CI's fixtures, `--chunk` splits and option
rows, and three real mixes (1:46, 6:15, 8:39) whole, in 1000-frame calls and at a wide range — each answered by the
new native tool and by both modules before and after, against v0.2.1's native answer: 120 comparisons, none
different. The boundaries change no arithmetic: the same operations on the
same operands in the same order.

Guarded without timing anything: `build.sh` links a named twin of each module (`--profiling-funcs`, into
`build/tierup/`), `tools/wasm/tierup-check.mjs` proves the twin has the shipped module's functions (the same count,
sizes within a few bytes) and then fails the build if any of the four is missing from either module; a control
links `fctempo` without the binaryen option and demands the check refuse it. New in the native suite: after any
`process()` call, exactly the frames its samples completed have been transformed, whether the call ends one sample
before a frame, on it or after it.

## v0.2.1 — 2026-09-26

### tools · wasm — fctempo: the tempo detector as a module of its own

A page that measures a tempo and nothing else no longer has to download every analyzer of `fcprobe` for it.
`tools/wasm/build.sh` builds a third module, `fctempo` (`tools/wasm/fc_tempo.cpp`): `fctempo.web.mjs` + `.wasm` (ES
module, `-sENVIRONMENT=web,worker`, factory `createFcTempo`) and `fctempo.node.js` + `.wasm`, on the probe's flags —
no threads, emmalloc, `-fno-exceptions -fno-rtti -ffp-contract=off -fno-fast-math -msimd128` — with the same gates:
the web and node `.wasm` byte-identical, the web one audited for threads. **37 441 bytes of wasm, 14 604 brotli (`-q
11`)**, against the probe's 275 056 / 76 651; the glue is 8.7 KB against 23.3 KB.

Its entry points ARE the probe's tempo entry points: the same ten `fc_probe_tempo_*` names, arguments, rows and
refusals, because both modules compile one text, `tools/wasm/fc_tempo_entry.h` — a page moves from one module to the
other without changing a call. What it does not share is the version: `fc_tempo_abi_version()` answers
`FC_TEMPO_ABI_VERSION` (**1**, `tools/fc_tempo_abi.h`), which is fc_probe ABI 1's tempo surface. Not
`fc_probe_abi_version`, because a version is a promise about a whole surface — `fc_probe_abi_version() == 1` says
the report, the hum detector and the streaming meter are there, and a page gated on it would meet a missing export
as a TypeError. Append-only, like fc_probe's; a change a caller can see in the shared tempo text moves both versions
in one commit. A page switching over loads `fctempo.web.mjs` and gates on `_fc_tempo_abi_version() >= 1`; every
tempo call stays as it is.

`fcprobe` does not change: the tempo entry points and the argument guards (`tools/wasm/fc_abi_guards.h`) moved out
of `fc_probe.cpp` verbatim, and every fcprobe and fcmaster artifact is byte-identical to the build before the move
(the checked `fcprobe.debug.wasm` once its DWARF is stripped). `build.sh` now reads a module's entry points from its
`#include` closure, not from the `.cpp` alone, so a name declared in a shared header is on every export list that
compiles it, and the one-per-line, count and return-type gates run over the whole closure. The closure is the
compiler's (`-MM`, under the module's own front-end flags, so a header included only under `-msimd128` is in it),
read as Make writes it (a path with a space survives), and a file of it that cannot be read stops the build.

Proven the way the probe is: CI diffs `fcore_measure tempo` against `fcprobe`, its checked build and `fctempo`,
every row byte for byte, and every refusal row now demands exit status 2 on both roads, where it took any failure —
a module that did not load used to count as refusing. The harnesses take the module's identity from its file name
and hold the artifact to it (`tools/wasm/module-identity.mjs`), so fcprobe handed over as fctempo is refused, not
measured. `felitronics_fctempo_abi_tests` runs the tempo ABI suite against `fc_tempo.cpp` natively (ASan, UBSan) and
on the wasm tier's checked build; `storage-probe.mjs` holds `fctempo` to its exact export set and compares the two
modules' tempo prices over 3600 quotes; its tables leave the process only once written, into a pipe as into a file.
On three real mixes (4:10–8:39, 48 kHz stereo) the two modules answer the same bits in the same time.

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
