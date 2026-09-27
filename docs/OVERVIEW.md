<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# felitronics-mastering-core — what's inside (quick map)

JUCE-free C++20 over [felitronics-core](https://github.com/darwinscat/felitronics-core), under its laws
(felitronics-core `docs/DSP-ARCHITECTURE.md` §2). Header-only, with one exception: `felitronics::session`, the
mastering session, is a compiled library with flags and laws of its own (below, and [`SESSION.md`](SESSION.md)).
The chain, analyzer and tempo rows moved from core's `CORE-OVERVIEW.md` with their modules.

## The chain

| Module | What | Key types |
|---|---|---|
| `mastering` | **the chain itself**, streaming + block-independent, plus the offline render wrapper. The one module that composes many others on purpose — the composite is the unit under test; the VOICING (preset tables, targets) stays in the product | `MasteringChain` (fixed internal quantum → the same bits at any caller block size), `MasteringChainConfig`/`Params`/`Resolved`, `OfflineRenderer` (`out[n] = y[n+D]`, tail included), `TargetLoudnessSolver` (the search for a target loudness under a true-peak ceiling), `DeliveryConverter` / `DeliveredMastering` (render, solve and range at a delivery rate other than the source's) |

`gain → EQ (each point optionally DYNAMIC) → [M/S mono-bass] → compressor (optional internal sidechain HPF) →
[soft clipper] → gain → true-peak limiter → dither`, as a single streaming object with a declared latency, a
latency-neutral per-stage bypass, and a tail that is not lost. It is deliberately the composite of core's stages
(`eq`, `dynamics`, `dynamiceq`, `saturation`, `stereo`, `limiter`, `dither`) rather than a new one; what it adds
is the thing composition kept getting wrong — see its header for why block independence needed a fixed internal
quantum instead of a promise.

Why a composite belongs in the shared layer at all (felitronics-core `DSP-ARCHITECTURE.md` §4): it is the unit
under test, and it can be wrong on its own — latency arithmetic, block dependence, stale state across a bypass
and a lost tail live in no stage and are reachable only by testing the composition. What stays in the product is
the VOICING (`mastering-config.json`: JAZZ/METAL, LIGHT/HEAVY).

## The offline programme analyzers — `felitronics::analysis_offline`

Namespace `felitronics::analysis`, headers `<felitronics/analysis/...>` — the spelling they had in core — over
core's meters (`LoudnessMeter`, `ReferenceTruePeakMeter`, `KWeightingFilter`) and its eq/stereo primitives
(`eq::Crossover2` is the LR4, `stereo::MidSide` the M/S pair). Deterministic by construction: every value that
crosses a row is on core's `core::det` floor, pinned by the det-math lint (`tools/lint/det-math-zone.txt`) and
by `AnalyzerMathPolicyTests`, so their reports are byte-identical native and in wasm (`WASM-PARITY.md`).

| Header | What |
|---|---|
| `ProgrammeReport` | one offline pass over a delivered programme: where the silence is, how much the channels differ, what sits under 30 Hz, how far the loudness travels — coordinates and numbers, never a verdict |
| `SourceForensics` | what the file actually was, as far as the samples prove it: the spectral wall and the sample grid |
| `HumDetector` | mains hum — a stationary line at 50 or 60 Hz with its comb of multiples — looked for only where the programme is quiet |
| `LowEnd` | the two questions a lacquer asks of the bottom end: how wide the bass is, and the dominant low note |
| `BandBursts` / `StereoBandBursts` | bursts of energy in one band (5–9 kHz by default) above its own surroundings, how far and how regularly; the stereo one on Mid and Side, centred against wide |
| `BandCrest` | the crest per 400 ms block and per band, and the paired loss between a source and its master |
| `PeakExcursions` | how far, how often and for how long a render goes over a ceiling, on the reconstruction — the measurement behind a clipper-or-limiter choice |
| `ClipDetector` | sample-clipped runs by flatness, with positions, peak and DC |
| `WaveformPeaks` | the waveform bars — a port of the site's `audio-peaks.js`, bit-identical, native and wasm; box-averaged max-abs, not a metering peak |
| `StereoColumns` / `StereoSums` | the stereo band: width, uncentred phase correlation, RMS per column + the playhead needle — a port of `stereo-meter.js` |
| `SpectrumFrames` | the shared frame producer the spectral analyzers stand on |

## The tempo detector — `felitronics::tempo`

`<felitronics/tempo/TempoDetector.h>` is a port of the site's BPM tool (`dsp/tempo.js`, `tempoCurve` and its
whole-track half `detectTempo`): spectral-flux onsets from 1024-sample Hann frames every 512 samples, detrended,
autocorrelated over the lags of the BPM range, the peak chosen under a log-normal preference around 120 BPM and
refined parabolically, then 6 s windows every 1.5 s anchored to that tempo for the curve, median-smoothed, with the
headline, its 10–90 % range and `varies` taken from the curve — over the mono mix the page makes, and with the
spec's own edges kept (a lone onset reads a tempo with a NaN confidence). The transform is core's offline double
FFT (the same radix-2 as the page's `fft.js`, unnormalised; only its twiddle seeds are `core::det`'s), the window
and the octave weights are on `core::det`, and `Math.hypot` / `Math.round` / `Math.min` / `Math.max` are
reproduced exactly (`<felitronics/tempo/JsNumerics.h>`). Against the JavaScript on real programmes every
reported number is identical; the onset curve differs by the Hann window's cos alone, at ~1e-15 relative. It
streams (any split gives the bits of one call), publishes its demand (`storageFor`), and crosses the probe ABI
as `fc_probe_tempo_*` — in `fcprobe`, and alone in `fctempo`, a module of its own for a page that measures a tempo
and nothing else (37 KB of wasm, 15 KB brotli, against the probe's 275 / 77). Both compile one text,
`tools/wasm/fc_tempo_entry.h`, so the names, rows and bits are the same in either.

## The mastering session — `felitronics::session`

`<felitronics/session/Session.h>`: the object a shell talks to — the web worker through the `fcsession` wasm module, a
desktop application by linking the library, `fcore_session` from a command script. A `Session` refuses a thread whose
floating-point environment is not IEEE-754's default, and it holds:

- **its states and commands** (`<felitronics/session/Commands.h>`): Empty, Loaded, Measured1, Measured2, and a master
  being made as an overlay on the measured two. A shell asks by typed requests — `load`, `setTarget`, `editTarget`,
  `editDevice`, `revertEdits`, `setManual`, `master`, `cancel`, `forget` — each answered whole: accepted with the
  revision it made, or rejected with a code, having changed nothing. Who may do what, when, is one table in the code,
  consulted first by every command, and the checks after it run in one declared order. Every command states what it
  will ask the heap for before it runs;
- **its project** (`<felitronics/session/Project.h>`): the target and a person's edits of its numbers, the manual mode,
  and every device's parameters as typed fields written once and used in two layers — the machine's, complete, placed
  from the config when the first measurement ends, and a person's, only what was touched, edited only after that and
  checked on each knob's travel and step, exactly. [`SESSION.md`](SESSION.md), "The states and the commands".

What is fixed is the ground it stands on:

- **its config** (`<felitronics/session/Config.h>`): every number the session decides, measures and reports with, in
  two TOML documents — `modules/session/config/targets.toml` (the targets) and `engine.toml` (everything else) —
  compiled into the library at build time by felitronics-toml and read by schema into typed structs: an unknown key, a
  wrong type or a value out of its domain stops every build of the library at its line and column. The owner's decisions
  in it are pinned by a suite of their own; its sound version, a hash of the normalised data that can change a master, is
  what a recipe will record. [`SESSION.md`](SESSION.md) has the details;

- **a compiled STATIC target**, the repository's first, whose sources are compiled with PRIVATE flags in one `SHELL:`
  group — no FP contraction, no fast-math, no exceptions, no RTTI. Every translation unit refuses to compile without
  them, the compile line is read back (this build's and a consumer's), and the library answers IEEE-754 under hostile
  flags placed ahead of its own. The flags settle the library's own objects, not the header-inline code it shares with
  the program, of which the linker keeps one copy: so a program that links it compiles **every** translation unit with
  the same FP flags — no contraction, no fast-math. The wasm modules are built whole here and are not affected;
- **its laws, each held by a check with a control**: an object-file gate over its objects and its C boundary's (no
  writable data, read from what each object says of its sections; nothing called outside a short list — no OS, file,
  console, locale, clock or process state), a source lint for what leaves no symbol (includes, directives and macros,
  pragmas, attributes, exception and RTTI tokens, atomics, implementation-defined orders, function bodies and variables
  in the public header) over the module and the C boundary, the whole module in the deterministic zone, memory declared
  before the work. Which of core's laws apply, which do not, what holds each, and what the checks do not try to catch:
  [`SESSION.md`](SESSION.md).

## The C ABIs — `tools/`

`fc_master` (`tools/fc_master_abi.h`, `tools/wasm/fc_master.cpp`) over the chain, `fc_probe`
(`tools/fc_probe_abi.h`, `tools/wasm/fc_probe.cpp`) over the analyzers, `fc_tempo` (`tools/fc_tempo_abi.h`,
`tools/wasm/fc_tempo.cpp`) over the tempo detector alone — fc_probe's `fc_probe_tempo_*` entry points under the
same names, with a version of its own, because a version is a promise about a whole surface — and `fc_session`
(`tools/fc_session_abi.h`, `tools/wasm/fc_session.cpp`) over the session, the surface a shell that cannot link C++
talks to the library through (a DRAFT, version 0, with no promise: the version, a session created and destroyed
through a handle, the config version, the session's refusals of a create, the poison; its wasm module `fcsession`, which
carries the config, is 44 KB, 13 KB brotli) — each with its ABI version
and, for the first three, the append-only rule that moves it in its header —
with their native CLIs (`fcore_master`, `fcore_measure`, `fcore_session`) and suites. `tools/wasm/build.sh` builds the wasm modules against a felitronics-core checkout
(`FELITRONICS_CORE_DIR`, or the sibling `../felitronics-core`) and records both versions in `BUILD-INFO` beside
them; `fcsession` embeds its config with a felitronics-toml checkout (`FELITRONICS_TOML_DIR`, or the sibling
`../felitronics-toml`). How the two roads are compared, and to which criterion: `WASM-PARITY.md`. Law 11d as it applies to the
chain and its ABI: `LAW11D-MASTERING.md`.
