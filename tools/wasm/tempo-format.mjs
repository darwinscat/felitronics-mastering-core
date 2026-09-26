// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.
//
// The JavaScript half of `fcore_measure tempo`. It must match that output byte for byte — the diff IS the parity
// test — so every double crosses as a raw IEEE-754 bit pattern and every count as a decimal integer, as the C++
// half prints them. THE ONE EXCEPTION IS NaN, printed `nan` on both sides: in this surface a NaN is the spec's
// `null`, its bits carry nothing, and wasm leaves the sign of a computed NaN to the engine.
//
// THE SCALAR AND ROW ORDERS ARE THE CONTRACT: `fc_probe_tempo_scalars` writes them in the order the indices below
// name (tools/wasm/fc_tempo_entry.h has the same table — one text, compiled into fcprobe and fctempo alike), and a
// mismatch shows up as a diff rather than as a plausible wrong number. A candidate row is 2 doubles, a curve row 5.

import { bitsOf } from './blocks-format.mjs';

const S = {
    sr: 0, ch: 1, samples: 2, odfSr: 3, onsets: 4, minBpm: 5, maxBpm: 6, winSec: 7, hopSec: 8, winFrames: 9, hopFrames: 10,
    determined: 11, bpm: 12, conf: 13, label: 14, altCount: 15, alt0: 16, alt1: 17, period: 18, offset: 19,
    varies: 20, hasRange: 21, lo: 22, hi: 23, candidates: 24, points: 25,
    wBpm: 26, wAltCount: 27, wAlt0: 28, wAlt1: 29, wPeriod: 30,
    anchorBpm: 31, anchorConf: 32, anchorLag: 33, nonFinite: 34,
};
export const TEMPO_SCALARS = 35, TEMPO_CAND_STRIDE = 2, TEMPO_POINT_STRIDE = 5;

const hx = (v) => (Number.isNaN(v) ? 'nan' : bitsOf(v));

export function formatTempo({ s, cand, curve }) {
    const out = [];
    out.push(`# fcore tempo v1 sr=${hx(s[S.sr])} ch=${s[S.ch]} min=${hx(s[S.minBpm])} max=${hx(s[S.maxBpm])}`
           + ` win=${hx(s[S.winSec])} hop=${hx(s[S.hopSec])}`);
    out.push(`samples ${s[S.samples]} onsets ${s[S.onsets]} nonfinite ${s[S.nonFinite]}`);
    out.push(`geometry ${hx(s[S.odfSr])} ${hx(s[S.winFrames])} ${hx(s[S.hopFrames])}`);
    out.push(`head ${s[S.determined]} ${hx(s[S.bpm])} ${hx(s[S.conf])} ${s[S.label]} ${s[S.altCount]}`
           + ` ${hx(s[S.alt0])} ${hx(s[S.alt1])} ${hx(s[S.period])} ${hx(s[S.offset])}`);
    out.push(`whole ${hx(s[S.wBpm])} ${s[S.wAltCount]} ${hx(s[S.wAlt0])} ${hx(s[S.wAlt1])} ${hx(s[S.wPeriod])}`);
    out.push(`anchor ${hx(s[S.anchorBpm])} ${hx(s[S.anchorConf])} ${hx(s[S.anchorLag])}`);
    out.push(`range ${s[S.varies]} ${s[S.hasRange]} ${hx(s[S.lo])} ${hx(s[S.hi])}`);
    out.push(`candidates ${s[S.candidates]}`);
    for (let k = 0; k < s[S.candidates]; ++k)
        out.push(`c ${k} ${hx(cand[k * TEMPO_CAND_STRIDE])} ${hx(cand[k * TEMPO_CAND_STRIDE + 1])}`);
    out.push(`points ${s[S.points]}`);
    for (let k = 0; k < s[S.points]; ++k) {
        const r = k * TEMPO_POINT_STRIDE;
        out.push(`p ${k} ${hx(curve[r])} ${curve[r + 1]} ${hx(curve[r + 2])} ${hx(curve[r + 3])} ${hx(curve[r + 4])}`);
    }
    return out.join('\n') + '\n';
}
