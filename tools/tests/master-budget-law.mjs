// SPDX-License-Identifier: AGPL-3.0-or-later
// Source controls complement allocation measurements: Release has no iterator proxies, so a
// facade sum of those proxies is numerically invisible there and still breaks the facade law.
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import assert from 'node:assert/strict';

const root = resolve(process.argv[2] || '.');
const read = path => readFileSync(resolve(root, path), 'utf8');
const facade = read('tools/wasm/fc_master.cpp');
const abi = read('tools/fc_master_abi.h');
const renderer = read('modules/mastering/include/felitronics/mastering/OfflineRenderer.h');
const workflow = read('.github/workflows/ci.yml');
let failed = 0;
for (const [name, check] of [
    ['F1: create forwards one complete core budget', () => assert.match(facade,
        /createBytesFor[^]*?\{\s*return mastering::createInstanceBytes[^;]+;\s*\}/)],
    ['F1: solution records carry sizeof alone', () => {
        const records = [...facade.matchAll(/(?:facade|v\.facadeBytes)\s*=\s*(sizeof\s*\(LoudnessSolution\)[^;]*);/g)];
        assert.equal(records.length, 2);
        for (const record of records) assert.match(record[1], /^sizeof\s*\(LoudnessSolution\)$/);
    }],
    ['F1: configure forwards the chain budget', () => assert.match(facade, /call = m\.chain\.configureBytes\(\);/)],
    ['F1: explicit law and refusal detail', () => {
        assert.match(abi.slice(0, 2400), /never summed/);
        assert.match(abi.slice(0, 2400), /Where the core cannot answer, this file refuses/);
    }],
    ['F3: CI runs the complete-selection gate', () => assert.match(workflow.split('windows-debug:')[1].split('core-main:')[0],
        /node tools\/tests\/windows-debug\.mjs build-debug/)],
    ['F4: configure states the snap and Debug lifetime', () => {
        const comment = abi.slice(abi.indexOf('// WHY THIS RE-PREPARES'), abi.indexOf('fc_status fc_master_configure'));
        assert.match(comment, /first-write snap/); assert.match(comment, /temporary oversampler proxies/);
        assert.doesNotMatch(comment, /COSTS NOTHING/);
    }],
    ['F4: aggregate distinguishes transient proxies', () => assert.match(renderer, /transient iterator proxies on Debug builds/)],
    ['F4: expanded budget comments fit the surrounding width', () => {
        const comment = abi.slice(abi.indexOf('typedef struct fc_need\n'), abi.indexOf('} fc_need;'));
        for (const line of comment.split('\n')) assert.ok(line.length <= 120, line);
    }],
]) {
    try { check(); console.log(`PASS ${name}`); }
    catch (error) { ++failed; console.error(`FAIL ${name}: ${error.message.split('\n')[0]}`); }
}
process.exitCode = failed ? 1 : 0;
