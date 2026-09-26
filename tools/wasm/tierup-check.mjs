// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE FUNCTION BOUNDARIES A FIRST ANALYSIS NEEDS, read from the artifact:
//   node tierup-check.mjs <shipped.wasm> <named twin.wasm> <function name prefix>...
//
// WHY. V8 gives a wasm function optimised code only on its NEXT call — there is no on-stack replacement for wasm — so
// a loop inside a function that one analysis enters once runs that whole analysis on the baseline compiler. The tempo
// detector keeps each loop that carries its time in a function called once per frame, per lag or per window
// (modules/tempo/include/felitronics/tempo/TempoDetector.h, "WHERE THE TIME IS SPENT"), and build.sh keeps emcc's
// optimiser from folding them back. Nothing about the answer changes when either is lost — only the first analysis
// gets twice as slow — so no parity diff can notice. This does, and without timing anything: timing is a flaky gate,
// a function table is not.
//
// HOW. The shipped module carries no names. Its TWIN is the same link line plus --profiling-funcs, which keeps the
// name section, and the names are believed only once the twin is shown to be the shipped module's structure: the
// same number of function bodies, and the bodies' sizes, sorted, pairwise within SLACK bytes. Not byte-identical,
// and that is measured, not allowed for in advance: with names kept the optimiser emits the functions and the types
// in another ORDER, so an index that takes one LEB128 byte below 128 takes two in the other build — fcprobe's 397
// bodies came out at most 1 byte apart, fctempo's 106 identical. An inlining decision that differed would not hide
// in that: folding a function with one caller removes it (the count moves), and any other inlining grows its caller
// by the callee's whole body — a callee small enough to hide in the slack has no loop to lose. Then every prefix given must name a function DEFINED in the twin — an import of that
// name would not be the detector's own loop.
//
// Exit 0: every boundary is there. 1: one is missing, or the twin is not the shipped structure. 2: usage.

import { readFileSync } from 'node:fs';
import { basename } from 'node:path';

const [, , shippedPath, twinPath, ...wanted] = process.argv;
if (!shippedPath || !twinPath || wanted.length === 0) {
    console.error('usage: node tierup-check.mjs <shipped.wasm> <named twin.wasm> <function name prefix>...');
    process.exit(2);
}

// The sections this needs: how many functions are imported (they come first in the index space), every body's size,
// and the function names.
function read (path) {
    const b = readFileSync(path);
    if (b.readUInt32LE(0) !== 0x6d736100 || b.readUInt32LE(4) !== 1) throw new Error(`${path} is not a wasm module`);
    let o = 8;
    const leb = () => { let r = 0, s = 0, x; do { x = b[o++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
    const str = () => { const n = leb(); const s = b.subarray(o, o + n).toString('utf8'); o += n; return s; };
    const limits = () => { const f = b[o++]; leb(); if (f & 1) leb(); };
    let imported = 0, bodies = null;
    const names = new Map();
    while (o < b.length) {
        const id = b[o++], size = leb(), end = o + size;
        if (id === 2) {                                          // imports: count the functions
            for (let k = leb(); k > 0; --k) {
                str(); str();
                const kind = b[o++];
                if (kind === 0) { leb(); ++imported; }
                else if (kind === 1) { o++; limits(); }         // table: reftype, limits
                else if (kind === 2) limits();                  // memory
                else if (kind === 3) o += 2;                    // global: valtype, mutability
                else if (kind === 4) { o++; leb(); }            // tag
                else throw new Error(`${path}: import kind ${kind} is unknown to this reader`);
            }
        } else if (id === 10) {                                  // code: the size of every body
            bodies = [];
            for (let k = leb(); k > 0; --k) { const n = leb(); bodies.push(n); o += n; }
        } else if (id === 0 && str() === 'name') {               // the name section: subsection 1 is the functions
            while (o < end) {
                const sub = b[o++], n = leb(), subEnd = o + n;
                if (sub === 1) for (let k = leb(); k > 0; --k) { const idx = leb(); names.set(idx, str()); }
                o = subEnd;
            }
        }
        o = end;
    }
    if (bodies === null) throw new Error(`${path} has no code section`);
    return { imported, bodies, names };
}

let shipped, twin;
try { shipped = read(shippedPath); twin = read(twinPath); } catch (e) { console.error(`*** ${e.message}`); process.exit(1); }

const SLACK = 4;
const sorted = m => [...m.bodies].sort((a, b) => a - b);
const a = sorted(shipped), t = sorted(twin);
const worst = a.length === t.length ? Math.max(0, ...a.map((v, i) => Math.abs(v - t[i]))) : Infinity;
if (worst > SLACK) {
    console.error(`*** ${basename(twinPath)} is not ${basename(shippedPath)} with names: ${t.length} bodies against ${a.length}`
                  + (a.length === t.length ? `, sizes up to ${worst} bytes apart (${SLACK} allowed)` : '')
                  + ' — its names would describe a different binary');
    process.exit(1);
}
if (twin.names.size === 0) { console.error(`*** ${basename(twinPath)} carries no function names`); process.exit(1); }

let missing = 0;
for (const w of wanted) {
    const hit = [...twin.names].filter(([idx, name]) => idx >= twin.imported && name.startsWith(w));
    if (hit.length === 0) { console.log(`  MISSING  ${w}…  — inlined into its caller, so it runs once per analysis`); ++missing; continue; }
    for (const [idx, name] of hit) console.log(`  kept     ${name}  (${twin.bodies[idx - twin.imported]} bytes)`);
}
if (missing) {
    console.error(`*** ${missing} of ${wanted.length} boundaries are gone from ${basename(shippedPath)}: its first analysis runs `
                  + `those loops unoptimised (TempoDetector.h, "WHERE THE TIME IS SPENT")`);
    process.exit(1);
}
console.log(`  ${wanted.length} of ${wanted.length} kept; the twin is ${basename(shippedPath)}'s ${a.length} functions, sizes at most ${worst} byte(s) apart`);
