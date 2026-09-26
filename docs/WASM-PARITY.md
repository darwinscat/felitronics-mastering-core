<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. -->

# Native vs wasm — the comparisons, and why each one can exist

felitronics-core's [`WASM-AUDIO-TIER.md`](https://github.com/darwinscat/felitronics-core/blob/main/docs/WASM-AUDIO-TIER.md) is the tier itself: every module built
for wasm32 with exceptions and RTTI off and no pthreads, the suite run in node, the artifacts audited. The C
ABIs built on that tier live here — the measurement probe (`tools/wasm/fc_probe.cpp`), the tempo detector alone
(`tools/wasm/fc_tempo.cpp`, the `fctempo` module) and the mastering chain (`tools/wasm/fc_master.cpp`) — and so do
the comparisons that prove the wasm modules compute what the native tools compute. This repository's CI runs the tier for its own suite and then every step below
(`.github/workflows/ci.yml`, job `wasm`). The text is as it stood in core's tier document, where these were
steps 5 and 6 of the tier's job.

Three criteria, and they are not interchangeable:

- **the probe, native vs wasm — BYTE FOR BYTE.** The offline analyzers behind `fc_probe` are on core's
  deterministic floor (`core::det`), so their printed text is identical on both roads; the spike's block-energy path
  still uses the system meter and is identical where glibc and musl agree — every rate CI runs it at, on
  Linux (below). `diff` is the acceptance. **`fctempo` is held to the same criterion**: it compiles the probe's
  tempo entry points from the same text (`tools/wasm/fc_tempo_entry.h`), and the tempo step diffs every row —
  refusals included, each one exit status 2 on both roads — through `fcprobe`, its checked build and `fctempo`
  against one native answer. The price step compares the two modules' tempo prices over 3600 quotes and holds
  `fctempo` to its exact export set. Which module a harness holds is named by its path and checked against the
  artifact (`tools/wasm/module-identity.mjs`): fcprobe handed over as fctempo is refused, and CI plants exactly that.
- **the mastering chain, native vs wasm — WITHIN A STATED TOLERANCE** (`tools/wasm/master-parity.mjs`: 1e-5 in
  sample value, 1e-3 dB on the reported loudness, integers exact). `fcore_master` is the reference for the C++
  API a desktop build links and takes the tree's `-ffp-contract=on`; baseline wasm has no scalar FMA to contract
  with, so the two are the same programme at two roundings.
- **the mastering ABI against a direct C++ call — BIT FOR BIT**, in ONE binary on one machine
  (`fcore_master selftest`, a test in the suite), where both sides round identically.

## The job

(In core this was the `wasm-audio-tier` job; here it is the `wasm` job.)

One job, `wasm-audio-tier`, on **`ubuntu-latest`** — and the runner choice is load-bearing for its last step.
Apple's libm and musl (what Emscripten compiles in) return different doubles from `tan()` at 88.2 and
192 kHz, so an arm64-macOS reference would diverge from wasm for a reason that is not a regression. glibc
agrees with musl at every rate measured. See [`WASM-SPIKE.md`](WASM-SPIKE.md).


5. **`tools/wasm/build.sh`** — CI had never compiled the wasm spike at all, which is why it broke unnoticed
   while it was being written.
6. **native↔wasm NULL test** on a **generated** fixture — no private audio enters a public repo. The
   comparison surface is the pre-gate 400 ms block-energy vector plus the true-peak linear maximum, not the
   scalar LUFS: a gated scalar is discontinuous in its own inputs and cannot carry a bit-exactness claim.
   The **checked** artifact (`SAFE_HEAP` + `ASSERTIONS=2` + stack checks) is diffed as well — building it
   without running it would prove nothing, and the libsoxr precedent is precisely a clean build that died
   at runtime.

The fixture (`tools/wasm/make-fixture.mjs`) calls no transcendental — an integer xorshift PRNG and float32
arithmetic only — so its bytes are identical on every JS engine and node version (on a little-endian host: a
TypedArray uses the platform's byte order, and the native tool reads f32**le**, so the harness already
assumed that). It is 10 s of stereo at 48 kHz in
five deliberate sections: ordinary noise, **digital silence** (the absolute gate), louder noise, an fs/4
pattern sampled 45° off the crests, and a decaying burst that stops on a transient at the very last sample.

Two of those sections were designed against a measurement, not a hunch.

**The fs/4 section.** An obvious "alternating ±A" sits at *exactly* Nyquist, where the reconstruction maximum
**is** A — measured `tp == sp`, so the compared true peak came from the sample-peak floor and the polyphase
FIR was never under test at all. Sampling fs/4 45° off the crests puts every sample at A while the
reconstruction peaks near A·√2, which is what puts the filter's own output into the compared number.

**The ending.** The first version decayed a burst across the last second — and `0.98 × 0.9995^48000` is
`3.7e-11`, about −209 dBFS, so the "abrupt ending" was silence and tested nothing. It is now a full-scale hit
occupying the **last 16 samples**, louder than anything before it, so the file's true peak is decided there.
That is what makes it a test: disabling `finish()` in `fcore::Probe` drops the reported true peak from
**1.3936 to 1.0125** — 2.8 dB under-reported — and changes the compared line. With the drain in place, the
whole ending is exact.

Current result on that fixture: **97 block energies plus true peak plus sample peak, every bit equal**,
native arm64 (Apple clang) vs wasm32 — and identical again from the checked `SAFE_HEAP` build.

---


## Reproducing the NULL test

```sh
source ~/emsdk/emsdk_env.sh
FELITRONICS_CORE_DIR=../felitronics-core ./tools/wasm/build.sh
cmake --preset desktop && cmake --build --preset desktop --target fcore_measure
node tools/wasm/make-fixture.mjs fixture.f32 48000 2 10
./build/tools/fcore_measure blocks 48000 2 fixture.f32                        > native.txt
node tools/wasm/parity.mjs tools/wasm/build/fcprobe.node.js 48000 2 fixture.f32 > wasm.txt
diff native.txt wasm.txt          # empty output IS the acceptance criterion
```

The tempo detector, on both modules that carry it — the same text, the same bits:

```sh
node tools/wasm/make-tempo-fixture.mjs tempo48.f32 48000 2 24
./build/tools/fcore_measure tempo 48000 2 tempo48.f32                              > native.txt
node tools/wasm/tempo-parity.mjs tools/wasm/build/fcprobe.node.js 48000 2 tempo48.f32 > probe.txt
node tools/wasm/tempo-parity.mjs tools/wasm/build/fctempo.node.js 48000 2 tempo48.f32 > tempo.txt
diff native.txt probe.txt && diff native.txt tempo.txt
```

On an Apple host the probe's comparison is expected to differ at 88.2 and 192 kHz for the libm reason above; CI
runs it on Linux.
