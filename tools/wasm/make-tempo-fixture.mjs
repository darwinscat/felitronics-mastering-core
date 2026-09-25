// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.
//
// Writes the interleaved f32le fixture the `tempo` parity check runs on: a programme with a TEMPO, and one that
// CHANGES — clicks at `bpmA` for the first half and at `bpmB` for the second, over a little noise. make-fixture.mjs
// is not that programme: it has no beat, so a parity run on it would compare a curve of gaps and prove only that
// the two sides agree nothing is there. On this one the whole-track tempo is determined, the curve has points on
// both sides of the change, `varies` is set, the range spans it, and every one of those fields is compared.
//
// DETERMINISM BY CONSTRUCTION, the same rule as the other generators: not one transcendental. A click is a LINEAR
// decay in integer 16-bit codes, the noise is xorshift32, the beat positions are Math.round of IEEE products and
// quotients, and every sample is an integer code over 32768 — exact in float32. Channel c > 0 carries the same
// clicks scaled by (c+1)/(c+2) (one IEEE division, one Math.round) and its own noise, so the detector's mix of
// the channels is a real mix and not a copy.
//
// Usage: node make-tempo-fixture.mjs <out.f32> [sampleRate=48000] [channels=2] [seconds=24] [bpmA=100] [bpmB=150]

import { writeFileSync } from 'node:fs';

const [, , outPath, srArg = '48000', chArg = '2', secArg = '24', aArg = '100', bArg = '150'] = process.argv;
if (!outPath) {
    console.error('usage: node make-tempo-fixture.mjs <out.f32> [sampleRate] [channels] [seconds] [bpmA] [bpmB]');
    process.exit(2);
}
const sr = Number(srArg), ch = Number(chArg), seconds = Number(secArg), bpmA = Number(aArg), bpmB = Number(bArg);
if (!(sr > 0) || !Number.isInteger(ch) || ch < 1 || ch > 16 || !(seconds > 0) || !(bpmA > 0) || !(bpmB > 0)) {
    console.error('bad arguments'); process.exit(2);
}
const frames = Math.round(sr * seconds);
const half = Math.floor(frames / 2);
const burst = Math.max(2, Math.round(sr * 0.005));    // a 5 ms click
const PEAK = 28000;                                    // the click's first code (about -1.4 dBFS)
const NOISE = 300;                                     // noise within +/- 300 codes (about -40 dBFS)

// xorshift32 — integer only, so every sample below is a pure function of the seed.
function rng(seed) {
    let x = seed >>> 0;
    return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x; };
}

// Channel 0's click codes, in integer 16-bit units: clicks at bpmA over [0, half), at bpmB over [half, frames).
const clicks = new Int32Array(frames);
for (const [from, to, bpm] of [[0, half, bpmA], [half, frames, bpmB]]) {
    const period = (60 / bpm) * sr;
    for (let beat = 0; ; ++beat) {
        const start = from + Math.round(beat * period);
        if (start >= to) break;
        for (let i = 0; i < burst && start + i < to; ++i)
            clicks[start + i] = Math.round((PEAK * (burst - i)) / burst) * (i % 2 ? -1 : 1);
    }
}

const out = new Float32Array(frames * ch);
for (let c = 0; c < ch; ++c) {
    const next = rng(0x9E3779B9 + 0x1000 * c);
    for (let i = 0; i < frames; ++i) {
        const click = c === 0 ? clicks[i] : Math.round((clicks[i] * (c + 1)) / (c + 2));
        const noise = (next() % (2 * NOISE + 1)) - NOISE;
        out[i * ch + c] = (click + noise) / 32768;
    }
}
writeFileSync(outPath, Buffer.from(out.buffer));
console.error(`${outPath}: ${frames} frames x ${ch} ch @ ${sr} Hz, ${bpmA} -> ${bpmB} BPM at frame ${half} (${out.byteLength} bytes)`);
