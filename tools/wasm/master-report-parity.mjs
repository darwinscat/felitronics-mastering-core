// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const output = readFileSync(process.argv[2], 'utf8');
assert.match(output, /ALL TESTS PASSED/);
assert.match(output, /^master-report-gain-digest=609a46efee2c2887$/m);

// Native AppleClang Release values. The core's rational SRC deliberately uses libm sine for
// coefficient design, so its PCM is compared numerically across tiers. The same-rate path is exact.
const native = [
    [-13.999999840056, -2.904884513397, 0.000000159944, 32272.600098657422, 18.460705399288],
    [-14.000000213604, -4.344040783315, -0.000000213604, 31578.327480815136, 19.793569797199],
    [-14.000000237683, -7.736064824797, -0.000000237683, 29086.524523220956, 19.949102926956]
];
const tolerance = [0.0001, 0.0001, 0.0001, 0.01, 0.001];
const nativeCost = [
    [0.000001316055, 0.000000000087, 0, 1],
    [0.572601210575, 0.041712411864, 0, 1],
    [0.537238988185, 0.040364549196, 0, 1]
];
const costTolerance = [0.01, 0.001, 0.0001, 0];
const lines = [...output.matchAll(/^master-report-parity (\d+) (.+)$/gm)];
assert.equal(lines.length, native.length);
for (let row = 0; row < lines.length; ++row) {
    assert.equal(Number(lines[row][1]), row);
    const values = lines[row][2].split(' ').map(Number);
    assert.equal(values.length, 5);
    for (let field = 0; field < values.length; ++field) {
        assert(Number.isFinite(values[field]));
        assert(Math.abs(values[field] - native[row][field]) <= tolerance[field],
            `parity case ${row}, field ${field}: ${values[field]} differs from ${native[row][field]}`);
    }
}
const costLines = [...output.matchAll(/^master-cost-parity (\d+) (.+)$/gm)];
assert.equal(costLines.length, nativeCost.length);
for (let row = 0; row < costLines.length; ++row) {
    assert.equal(Number(costLines[row][1]), row);
    const values = costLines[row][2].split(' ').map(Number);
    assert.equal(values.length, 4);
    for (let field = 0; field < values.length; ++field) {
        assert(Number.isFinite(values[field]));
        assert(Math.abs(values[field] - nativeCost[row][field]) <= costTolerance[field],
            `cost parity case ${row}, field ${field}: ${values[field]} differs from ${nativeCost[row][field]}`);
    }
}
console.log('master report and cost: same-rate bits and SRC numeric parity agree with native');
