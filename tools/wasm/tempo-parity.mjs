// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.
//
// The wasm side of the tempo parity check:
//   fcore_measure tempo 48000 2 a.f32 [flags]                        > native.txt
//   node tempo-parity.mjs build/fcprobe.node.js 48000 2 a.f32 [flags] > wasm.txt
//   diff native.txt wasm.txt
// Flags, with the same names, domain and refusals on both roads: --min-bpm --max-bpm --win-sec --hop-sec (one
// finite number each, at most once) and --chunk N (1..2^31-1, at most once). --chunk is the LAW-8a HANDLE: the native
// tool feeds the programme in pieces of N frames, the module always measures in one call, so a row that carries it
// compares a re-sliced run with a whole one. It is validated here and otherwise ignored.
//
// No HEAP view is held across a call into the module: the run allocates, so memory.grow can fire inside it.

import { formatTempo, TEMPO_SCALARS, TEMPO_CAND_STRIDE, TEMPO_POINT_STRIDE } from './tempo-format.mjs';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';

const [, , modPath, srArg, chArg, rawPath] = process.argv;
const refuse = m => { console.error(m); process.exit(2); };
if (!modPath || !srArg || !chArg || !rawPath)
    refuse('usage: node tempo-parity.mjs <module.js> <sampleRate> <channels> <raw.f32le> [flags]');

// THE SAME INPUT DOMAIN AS THE NATIVE TOOL, not a narrower or a wider one.
const NUM = /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/;
const count = (s, name) => { if (!/^[0-9]{1,18}$/.test(s)) refuse(`bad ${name}: ${s}`); return Number(s); };
const rateOf = s => { const v = Number(s); if (!NUM.test(s) || !Number.isFinite(v) || v <= 0) refuse(`bad sampleRate: ${s}`); return v; };
const finiteOf = (s, name) => { const v = Number(s); if (!NUM.test(s) || !Number.isFinite(v)) refuse(`tempo: ${name} needs one finite number`); return v; };
const sr = rateOf(srArg), ch = count(chArg, 'channels');
if (ch < 1 || ch > 16) refuse(`bad channels: ${chArg}`);

// TempoParams' documented defaults, repeated because the module publishes no reader for them. Guarded: the first
// line of the output prints all four as the RUN installed them, so a drift here is a diff on line one.
const P = { '--min-bpm': 60, '--max-bpm': 180, '--win-sec': 6, '--hop-sec': 1.5 };
{
    const seen = new Set();
    const rest = process.argv.slice(6);
    for (let i = 0; i < rest.length; ++i) {
        if (rest[i] === '--precise') continue;
        if (Object.prototype.hasOwnProperty.call(P, rest[i])) {
            if (seen.has(rest[i]) || i + 1 >= rest.length) refuse(`tempo: ${rest[i]} needs one finite number`);
            seen.add(rest[i]);
            P[rest[i]] = finiteOf(rest[i + 1], rest[i]); ++i; continue;
        }
        if (rest[i] === '--chunk') {
            if (seen.has('--chunk') || i + 1 >= rest.length || !/^[0-9]{1,18}$/.test(rest[i + 1]))
                refuse('tempo: --chunk needs one count in 1..2^31-1');
            const k = Number(rest[i + 1]);
            if (k < 1 || k > 0x7FFFFFFF) refuse('tempo: --chunk needs one count in 1..2^31-1');
            seen.add('--chunk'); ++i; continue;
        }
        refuse(`tempo: unknown option ${rest[i]}`);
    }
}

const require = createRequire(import.meta.url);
const M = await require(resolve(modPath))();

const raw = readFileSync(rawPath);
if (raw.byteLength % (4 * ch) !== 0) refuse(`${rawPath} is not a whole number of ${ch}-channel float32 frames`);
const inter = new Float32Array(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength));
const frames = inter.length / ch;
const planar = new Float32Array(frames * ch);
for (let c = 0; c < ch; ++c) for (let i = 0; i < frames; ++i) planar[c * frames + i] = inter[i * ch + c];

const ptr = M._malloc(Math.max(4, planar.length * 4));
if (!ptr) refuse('wasm OOM on the input');
M.HEAPF32.set(planar, ptr >>> 2);
const ok = M._fc_probe_tempo_run_with(ptr, frames, ch, sr, P['--min-bpm'], P['--max-bpm'], P['--win-sec'], P['--hop-sec']) === 1;
M._free(ptr);
// A refused run exits 2, as fcore_measure does, with nothing on stdout.
if (!ok) process.exit(2);

if (M._fc_probe_tempo_scalars_len() !== TEMPO_SCALARS || M._fc_probe_tempo_cand_stride() !== TEMPO_CAND_STRIDE
    || M._fc_probe_tempo_point_stride() !== TEMPO_POINT_STRIDE)
    refuse('the module publishes widths this format does not know — tempo-format.mjs and fc_probe.cpp moved apart');

const pull = (fn, doubles) => {
    const p = M._malloc(Math.max(8, doubles * 8));
    if (!p) refuse('wasm OOM');
    const got = fn(p, doubles);
    const v = new Float64Array(M.HEAPF64.buffer, p, doubles).slice();
    M._free(p);
    return { v, got };
};
const s = pull(M._fc_probe_tempo_scalars, TEMPO_SCALARS).v;
const cand = pull(M._fc_probe_tempo_candidates, s[24] * TEMPO_CAND_STRIDE);
const curve = pull(M._fc_probe_tempo_curve, s[25] * TEMPO_POINT_STRIDE);
if (cand.got !== s[24] || curve.got !== s[25]) refuse('the module returned fewer rows than its scalars announce');

process.stdout.write(formatTempo({ s, cand: cand.v, curve: curve.v }));
