// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Structural controls for the shared allocation formula and generated source provenance, plus the debug emit bound.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
const [root, executable, ...args] = process.argv.slice(2);
const read = path => readFileSync(join(root, path), 'utf8');
const snapshot = read('modules/session/src/Snapshot.cpp');
assert.match(snapshot, /Session::snapshotBytes\(\) const noexcept\s*\{\s*return Snapshot::storageFor \(buildView\(\)\);\s*\}/);
assert.match(snapshot, /Session::snapshot\(\) const noexcept\s*\{\s*return Snapshot::copy \(buildView\(\)\);\s*\}/);
assert.equal((snapshot.match(/SnapshotView Session::buildView/g) || []).length, 1);
const header = read('modules/session/src/CodecSchema.h');
assert(header.startsWith(read('modules/session/src/Snapshot.cpp').split('\n\n')[0] + '\n'));
assert.match(read('tools/session-codec.cmake'), /\[\[maybe_unused\]\] auto&/);
assert.match(read('tools/session-codec-schema.py'), /session-codec-schema\.json/);
assert.match(read('modules/session/include/felitronics/session/Commands.h'), /load, master: NoJobId/);
assert.match(read('modules/session/include/felitronics/session/Session.h'), /rejection publishes an event and advances seq/);
const run = spawnSync(executable, [...args, '--emit-limit'], { encoding: 'utf8' });
if (run.status === 77) console.log('debug emit bound: covered by the unconfigured and Debug rows');
else {
    assert.match(run.stdout, /batch capacity filled/);
    assert(run.signal || (run.status !== null && run.status !== 0), 'emit past capacity must trap');
    console.log('debug emit bound: full batch accepted, next event trapped');
}
console.log('shared snapshot demand and generated source provenance controls passed');
