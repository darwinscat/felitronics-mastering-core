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
    const keep = new Set(sample);
    const firstSnapshots = new Map(), lastSnapshots = new Map();
    for (const row of rows) if (row.kind === 'snapshot') {
        if (!firstSnapshots.has(row.session)) firstSnapshots.set(row.session, row);
        lastSnapshots.set(row.session, row);
    }
    for (const row of firstSnapshots.values()) keep.add(row);
    for (const row of lastSnapshots.values()) keep.add(row);
    for (let i = 0; i < rows.length; ++i) if (keep.has(rows[i]) && rows[i].kind === 'events') {
        for (let j = i + 1; j < rows.length && rows[j].session === rows[i].session; ++j) {
            if (rows[j].kind === 'snapshot') { keep.add(rows[j]); break; }
            if (rows[j].kind === 'events') break;
        }
    }
    // These are real codec JSON strings and their owned binary bytes, not parsed-and-reserialized objects.
    const unique = rows.filter(r => keep.has(r));
    let cursor = 0;
    for (const record of unique) {
        const index = rows.indexOf(record, cursor);
        assert.ok(index >= cursor, 'site recording rewinds the original trace');
        cursor = index + 1;
    }
    assert.ok(unique.some(r => r.kind === 'snapshot'), 'site recording omits milestone snapshots');
    return unique;
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
