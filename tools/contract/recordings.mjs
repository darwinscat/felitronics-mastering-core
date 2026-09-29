// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {arrayEncodings} from './wasm.mjs';
import {fixtureRoot} from './fixtures.mjs';

const root = fileURLToPath(new URL('recordings/', import.meta.url));
const scenarios = fileURLToPath(new URL('scenarios/', import.meta.url));
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const rebuild = 'node tools/contract/run.mjs build/tools/fcore_session build/measure-08-wasm-artifacts/fcsession.node.js --rebuild-recordings';
function select(bytes) {
    const rows = bytes.toString('utf8').trimEnd().split('\n').map(line => {
        const [session, kind, raw, binary = ''] = line.split('\t'); return {session, kind, raw, rows:binary};
    });
    const sample = rows.filter(r => ['create', 'answer', 'summary', 'query', 'capacity', 'poison'].includes(r.kind));
    const phases = new Map();
    const milestones = new Map();
    for (const row of rows.filter(r => r.kind === 'events')) {
        const batch = arrayEncodings(row.raw).map(x => JSON.parse(x));
        for (const e of batch) {
            if (e.kind === 'phase') {
                const key = `${row.session}:${e.jobId}:${e.payload.name}`;
                const pair = phases.get(key) ?? [row, row]; pair[1] = row; phases.set(key, pair);
            } else {
                const key = `${row.session}:${e.kind}:${e.payload?.analyzer ?? ''}:${e.payload?.status ?? ''}`;
                const pair = milestones.get(key) ?? [row, row]; pair[1] = row; milestones.set(key, pair);
            }
        }
    }
    for (const pair of phases.values()) sample.push(...pair);
    for (const pair of milestones.values()) sample.push(...pair);
    // These are real codec JSON strings and their owned binary bytes, not parsed-and-reserialized objects.
    return sample.filter((r, i, a) => a.indexOf(r) === i);
}
export function recordings(traces, versions, rewrite = false) {
    const fixture = JSON.parse(readFileSync(join(fixtureRoot, 'manifest.json'), 'utf8'));
    const manifest = {format:1, rebuild, inputHash:fixture.inputHash, versions, scenarios:{}};
    const expected = new Map();
    for (const [name, trace] of [...traces.entries()].sort(([a], [b]) => a.localeCompare(b))) {
        const scriptSha256 = digest(readFileSync(join(scenarios, `${name}.session`)));
        const data = Buffer.from(JSON.stringify({scenario:name, scriptSha256, records:select(trace)}, null, 2) + '\n');
        manifest.scenarios[name] = {scriptSha256, sha256:digest(data), bytes:data.length};
        expected.set(name, data);
    }
    const manifestBytes = Buffer.from(JSON.stringify(manifest, null, 2) + '\n');
    if (rewrite) {
        mkdirSync(root, {recursive:true});
        for (const [name, data] of expected) writeFileSync(join(root, `${name}.json`), data);
        writeFileSync(join(root, 'manifest.json'), manifestBytes);
    }
    assert.deepEqual(readFileSync(join(root, 'manifest.json')), manifestBytes, `stale site recording manifest; rebuild: ${rebuild}`);
    for (const [name, data] of expected)
        assert.deepEqual(readFileSync(join(root, `${name}.json`)), data, `stale site recording ${name}; rebuild: ${rebuild}`);
    return expected.size;
}
