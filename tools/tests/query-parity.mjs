// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import {types} from '../session-wire-types.mjs';
const [declarations, ...executables] = process.argv.slice(2);
assert.equal(executables.length, 3, 'supply native, wasm-audio and checked-wasm executables');
const accepts = types(readFileSync(declarations, 'utf8'));
let reference;
for (const executable of executables) {
    const file = resolve(executable);
    const run = spawnSync(file.endsWith('.js') ? process.execPath : file, file.endsWith('.js') ? [file] : [],
        {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024});
    assert.equal(run.status, 0, run.stderr || run.stdout);
    const lines = run.stdout.replace(/\r\n/g, '\n').split('\n');
    const queries = lines.filter(line => line.startsWith('query-fixture ')).map(line => JSON.parse(line.slice(14)));
    const rows = lines.filter(line => line.startsWith('query-rows ')).map(line => line.slice(11));
    assert.equal(queries.length, 7); assert.equal(rows.length, 7);
    for (let i = 0; i < 7; ++i) {
        assert(accepts(queries[i], 'QueryResponse'), 'real query codec agrees with generated declarations');
        assert(!accepts({...queries[i], request: {...queries[i].request, requestId: 1}}, 'QueryResponse'));
        const {byteOffset, length, stride} = queries[i].values;
        assert.equal(byteOffset, 0); assert.equal(stride, 1);
        assert.equal(length * 16, rows[i].length, 'owned binary64 row extent');
    }
    const digest = /^measurement-query-digest=([0-9a-f]{16})$/m.exec(run.stdout)?.[1];
    assert(digest);
    const result = {queries, rows, digest}; reference ??= result;
    assert.deepEqual(result, reference, 'query status, metadata, codec or binary rows differ between tiers');
}
console.log(`Measurement queries: three tiers agree (${reference.digest}); all seven generated response types checked`);
