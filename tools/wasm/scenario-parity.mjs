// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// The end-to-end scenario (modules/session/tests/ScenarioTests.cpp) on wasm against its native run. The scenario masters
// at the source's rate, so no resampler runs and every digest is exact across tiers: the source, the plan (the project
// file without its core stamp, and the ready recipe), the facts in both languages, the PCM bits and the WAV bytes.
// A change of sound or of the defaults moves them on every tier at once; take the new native lines from
// `felitronics_session_scenario_tests` then.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const output = readFileSync(process.argv[2], 'utf8');
assert.match(output, /ALL TESTS PASSED/);

// Native AppleClang Release, arm64.
const native = {
    input: 'source=f58fa8f9570118b5 frames=480000 rate=48000',
    parity: 'plan=638882e8bd039aae facts=7f984ec47da89834 pcm=e9f73602547ff606 wav=39bd949cfa577e40',
};
for (const [name, want] of Object.entries(native)) {
    const lines = [...output.matchAll(new RegExp(`^scenario-${name} (.+)$`, 'gm'))];
    assert.equal(lines.length, 1, `exactly one scenario-${name} line`);
    assert.equal(lines[0][1], want, `scenario-${name}: the wasm run differs from native`);
}
const versions = output.match(/^scenario-versions (.+)$/m);
assert(versions, 'the scenario states its versions');
console.log(`scenario: input, plan, facts, PCM and WAV agree with native (${versions[1]})`);
