// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// The end-to-end scenario (modules/session/tests/ScenarioTests.cpp) on wasm against its native run. The scenario masters
// at the source's rate, so no resampler runs and every digest is exact across tiers: the source, the plan (the project
// file and the ready recipe), the facts in both languages, the PCM bits and the WAV bytes.
// A change of sound or of the defaults moves them on every tier at once; take the new native lines from
// `felitronics_session_scenario_tests` then.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const output = readFileSync(process.argv[2], 'utf8');
assert.match(output, /ALL TESTS PASSED/);

// Native AppleClang Release, arm64.
const native = {
    input: 'source=f58fa8f9570118b5 frames=480000 rate=48000',
    // facts moved with a landing held short and the master's damage — its lines, its own job and its line naming the
    // windows graded (v0.14.0); plan did not. The limiter budget's search aimed at its crossing (v0.17.0) is the max
    // modes' alone: this manual master keeps that search and its PCM/WAV to the bit. v0.18's report/fact corrections
    // move only the emitted-facts digest.
    parity: 'plan=8043eff22be0264c facts=be4ff10ec942c73a pcm=d73c61caca166b05 wav=5d07cbcd5294c4f0',
};
for (const [name, want] of Object.entries(native)) {
    const lines = [...output.matchAll(new RegExp(`^scenario-${name} (.+)$`, 'gm'))];
    assert.equal(lines.length, 1, `exactly one scenario-${name} line`);
    assert.equal(lines[0][1], want, `scenario-${name}: the wasm run differs from native`);
}
const versions = output.match(/^scenario-versions (.+)$/m);
assert(versions, 'the scenario states its versions');
console.log(`scenario: input, plan, facts, PCM and WAV agree with native (${versions[1]})`);
