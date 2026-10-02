// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { types } from './session-wire-types.mjs';
const [declarations, executable, ...args] = process.argv.slice(2);
const source = readFileSync(declarations, 'utf8'), accepts = types(source);
const run = spawnSync(executable, args, {encoding: 'utf8'});
assert.equal(run.status, 0, run.stderr || String(run.error));
const [snapshotJson, snapshotHex, eventsJson, eventsHex, version] = run.stdout.trim().split(/\r?\n/);
assert.equal(/FC_SESSION_CONFIG_VERSION: "([0-9a-f]{16})"/.exec(source)?.[1], version, 'generated page config matches the compiled library');
const snapshot = JSON.parse(snapshotJson), events = JSON.parse(eventsJson);
assert(accepts(snapshot, 'SessionSnapshot'));
assert(accepts(events, 'ReadonlyArray<SessionEvent>'));
const frozen = readFileSync(new URL('./session-abi-v1.txt', import.meta.url), 'utf8').split(/\r?\n/);
// THE BASE SNAPSHOT IS OLDER THAN THE FIELDS APPENDED AFTER THE BASE. The manifest's base ends with its section "The
// rest of the compiled surface at the v0.6.0 base"; each field appended since is frozen by a `codec Record.field` line
// after that section, and the base snapshot lacks exactly those. Snapshots are never persisted (law 12): an appended
// field is a plain field, with no decode default, and the base is held to what it carries — each of its fields still in
// the declaration, with its type — while a field of the base stays required.
const baseAt = frozen.findIndex(line => line.startsWith('{"') && !line.startsWith('{"kind"'));
const restAt = frozen.findIndex(line => line.startsWith('# The rest of the compiled surface at the v0.6.0 base'));
assert(restAt > baseAt, 'the manifest declares where its v0.6.0 base ends');
const baseEnd = frozen.findIndex((line, i) => i > restAt && line.startsWith('#'));
const appended = new Set((baseEnd < 0 ? [] : frozen.slice(baseEnd)).flatMap(line => {
    const m = /^codec (\w+)\.(\w+) /.exec(line);
    return !m ? [] : m[1] === 'Snapshot' ? [`Snapshot.${m[2]}`, `SessionSnapshot.${m[2]}`] : [`${m[1]}.${m[2]}`];
}));
const acceptsBase = types(source, appended), base = JSON.parse(frozen[baseAt]);
assert(acceptsBase(base, 'SessionSnapshot'), 'the manifest base snapshot is accepted by the generated declaration');
const withoutBaseField = structuredClone(base); delete withoutBaseField.handFieldCount;
const withoutBaseRecordField = structuredClone(base); delete withoutBaseRecordField.plan.limiter.ceilingDbTp;
assert(!acceptsBase(withoutBaseField, 'SessionSnapshot') && !acceptsBase(withoutBaseRecordField, 'SessionSnapshot'),
    'a field the base carries is still required of it, at the top and in a nested record');
assert(accepts(JSON.parse(frozen.find(line => line.startsWith('['))), 'ReadonlyArray<SessionEvent>'), 'the manifest base events are accepted by the generated declaration');
assert.equal(snapshot.sourceBytes, Number.MAX_SAFE_INTEGER);
assert.equal(snapshot.integratedLufs, '-Infinity');
function rows(row, hex) {
    const bytes = Uint8Array.from(Buffer.from(hex, 'hex'));
    return new Float64Array(bytes.buffer, row.byteOffset, row.length * row.stride);
}
assert.deepEqual([...rows(snapshot.momentary, snapshotHex)], [Number.MAX_SAFE_INTEGER, -Infinity, 4, NaN]);
assert.equal(snapshot.handFieldCount, 2);
assert.deepEqual([...rows(snapshot.eqCurve, snapshotHex)], [20, -3.5, 1000, 0.25]);
assert.deepEqual([...rows(snapshot.runs, snapshotHex)], [7, 2, 0.5]);
assert.deepEqual([...rows(snapshot.machineDifferences, snapshotHex)], [0, 1, 24, 32]);
assert.deepEqual([...rows(snapshot.measurements[0].arrays[0].values, snapshotHex)], [0.25, 0.5, 0.75]);
assert.equal(snapshot.measurements[0].key, '9007199254740993');
assert.equal(events[8].payload.key, snapshot.measurements[0].key);
assert.equal(events[8].kind, 'measurement');
const changedRows = structuredClone(snapshot);
changedRows.measurements[0].arrays[0].values.stride = 2;
assert(!accepts(changedRows, 'SessionSnapshot'));
assert.equal(events[3].payload.args[1].integer, '-9223372036854775808');
assert.deepEqual([...rows(events[4].payload.momentary, eventsHex)], [Number.MAX_SAFE_INTEGER, -Infinity]);
assert.deepEqual([...rows(events[4].payload.shortTerm, eventsHex)], [4, NaN]);
assert.equal(events[6].payload.commandId, '9007199254740993');
assert.equal(events[7].payload.needBytes, Number.MAX_SAFE_INTEGER);
for (const event of events) {
    assert(!accepts({...event, kind:'unknown'}, 'SessionEvent'));
    assert(!accepts({...event, payload:{}}, 'SessionEvent'));
    assert(!accepts({...event, seq:3}, 'SessionEvent'));
}
assert(!accepts({...snapshot, momentary:[]}, 'SessionSnapshot'));
assert(!accepts({...snapshot, sourceBytes:'42'}, 'SessionSnapshot'));
console.log('session wire: all seven event variants, appended phases, exact identities/bytes and nested Float64Array rows agree with generated types; drift controls pass');
