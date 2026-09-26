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
