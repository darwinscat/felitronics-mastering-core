// SPDX-License-Identifier: AGPL-3.0-or-later
// Direct-analyzer assertions run on every tier before their binary64 fingerprint is compared.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const executables = process.argv.slice(2);
assert.equal(executables.length, 3, 'supply native, wasm-audio and checked-wasm executables');
let reference;
for (const executable of executables) {
    const file = resolve(executable);
    const run = spawnSync(file.endsWith('.js') ? process.execPath : file,
        file.endsWith('.js') ? [file] : [], {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024});
    assert.equal(run.status, 0, run.stderr || run.stdout);
    const digest = /^source-results-digest=([0-9a-f]{16})$/m.exec(run.stdout.replace(/\r\n/g, '\n'))?.[1];
    assert.ok(digest, 'missing source fixture');
    reference ??= digest;
    assert.equal(digest, reference, 'source measurement binary64 values differ across tiers');
}
console.log(`Source measurements: three tiers agree (${reference})`);
