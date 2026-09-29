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
console.log('session wire: all six event variants, appended phases, exact identities/bytes and nonempty Float64Array rows agree with generated types; drift controls pass');
