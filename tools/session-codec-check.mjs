// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// No packages or network: read the generated declaration grammar and check REAL encoded fixtures against it.
// This checks Codec.cpp's wire choices against session-codec.cmake's, including every scalar, record and wrapper.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
const [declarations, schemaFile, executable, ...args] = process.argv.slice(2);
const source = readFileSync(declarations, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
const aliases = new Map([...source.matchAll(/export type (\w+) = ([^;]+);/g)].map(m => [m[1], m[2].trim()]));
const records = new Map([...source.matchAll(/export interface (\w+) \{([^}]+)\}/g)].map(m =>
    [m[1], [...m[2].matchAll(/readonly (\w+): ([^;]+);/g)].map(f => [f[1], f[2].trim()])]));
const schema = JSON.parse(readFileSync(schemaFile, 'utf8'));
const visited = new Set();
function accepts(value, type, path = 'Snapshot') {
    type = type.trim();
    visited.add(type);
    if (aliases.has(type)) return accepts(value, aliases.get(type), path);
    if (type.includes(' | ')) return type.split(' | ').some(t => accepts(value, t, path));
    if (type.startsWith('ReadonlyArray<')) return Array.isArray(value) && value.every((v, i) => accepts(v, type.slice(14, -1), `${path}[${i}]`));
    if (records.has(type)) {
        if (value === null || typeof value !== 'object' || Array.isArray(value)) return false;
        const fields = records.get(type);
        return Object.keys(value).length === fields.length && fields.every(([name, t]) =>
            Object.hasOwn(value, name) && accepts(value[name], t, `${path}.${name}`));
    }
    if (type === 'null') return value === null;
    if (['number', 'string', 'boolean'].includes(type)) return typeof value === type;
    if (/^\d+$/.test(type) || /^".*"$/.test(type)) return value === JSON.parse(type);
    throw Error(`Unsupported declaration ${type} at ${path}`);
}
const run = spawnSync(executable, args, { encoding: 'utf8' });
assert.equal(run.status, 0, run.stderr || String(run.error));
const fixtures = run.stdout.trim().split(/\r?\n/).map(line => JSON.parse(line));
assert.equal(fixtures.length, 7);
assert(accepts(fixtures.pop(), 'MeasurementChange'));
for (const fixture of fixtures) assert(accepts(fixture, 'Snapshot'), 'encoded fixture differs from snapshot.d.ts');
for (const [name, record] of Object.entries(schema.records)) {
    assert(visited.has(name), `fixture did not reach record ${name}`);
    assert.deepEqual(records.get(name).map(([key]) => key).sort(), Object.keys(record.fields).sort());
}
for (const name of Object.keys(schema.enums)) assert(visited.has(name), `fixture did not reach enum ${name}`);
// Each mapping gets an incompatible wire type planted into a real encoded record. Extra/missing fields fail too.
for (const [path, wrong] of [
    [['revision'], 1], [['source', 'name'], 1], [['source', 'channels'], '1'], [['source', 'bitDepth'], '1'],
    [['project', 'target'], '1'], [['project', 'devices', 'hpf', 'machine', 'slope'], '12'],
    [['sourceBytes'], '16'], [['mastering'], 1], [['integratedLufs'], 'unknown'], [['state'], 255],
    [['project', 'targetEdit', 'lufs'], false], [['masters'], {}], [['source'], []]
]) {
    const fixture = structuredClone(fixtures[1]);
    let parent = fixture;
    for (const key of path.slice(0, -1)) parent = parent[key];
    parent[path.at(-1)] = wrong;
    assert(!accepts(fixture, 'Snapshot'), `wire mismatch escaped: ${path.join('.')}`);
}
const extra = structuredClone(fixtures[0]); extra.truePeakDb = 0;
assert(!accepts(extra, 'Snapshot'));
const missing = structuredClone(fixtures[0]); delete missing.source;
assert(!accepts(missing, 'Snapshot'));
console.log(`encoded fixtures match declarations: ${fixtures.length} fixtures, ${records.size} records, all wire mappings and refusal controls`);
