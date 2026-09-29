// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE FROZEN MANIFEST ONLY GROWS. tools/session-abi-check.mjs holds the compiled surface to tools/session-abi-v1.txt,
// and so cannot see a pull request that edits a frozen line in the manifest in lockstep with the surface. This holds
// the manifest to its base branch's: every base line must still be there (as often as it was), in any order, and lines
// may be added anywhere. A removed or edited line — a comment included — is refused. CI runs it on pull requests.
//
//   node tools/session-abi-append-only.mjs <base manifest> <proposed manifest>
//   node tools/session-abi-append-only.mjs --self-test
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const linesOf = text => text.split(/\r?\n/).filter(line => line.trim() !== '');
function removed(base, proposed) {
    const left = new Map();
    for (const line of linesOf(proposed)) left.set(line, (left.get(line) ?? 0) + 1);
    const gone = [];
    for (const line of linesOf(base)) {
        const n = left.get(line) ?? 0;
        if (n === 0) gone.push(line); else left.set(line, n - 1);
    }
    return gone;
}
const args = process.argv.slice(2);
if (args[0] === '--self-test') {
    const base = readFileSync(new URL('session-abi-v1.txt', import.meta.url), 'utf8'), lines = linesOf(base);
    assert.deepEqual(removed(base, base), [], 'the unchanged manifest passes');
    assert.deepEqual(removed(base, `${base}\nfunction fc_session_future fc_session_status(fc_session)\n`), [], 'an appended line passes');
    assert.deepEqual(removed(base, [lines[0], 'wire Future.inserted number', ...lines.slice(1)].join('\n')), [], 'an inserted line passes');
    assert.deepEqual(removed(base, [...lines].reverse().join('\r\n')), [], 'order and line endings are not content');
    let red = 0;
    for (let i = 0; i < lines.length; ++i) {
        const gone = lines.filter((_, j) => j !== i).join('\n');
        const edited = lines.map((line, j) => j === i ? `${line} CHANGED` : line).join('\n');
        assert.deepEqual(removed(base, gone), [lines[i]], `removing line ${i + 1} is refused`);
        assert.deepEqual(removed(base, edited), [lines[i]], `editing line ${i + 1} is refused`);
        red += 2;
    }
    // The lockstep edit the compiled gate cannot see: a frozen value changed in the floor together with the surface.
    const lockstep = base.replace('define FC_SESSION_STEP_UNITS=16', 'define FC_SESSION_STEP_UNITS=17');
    assert.notEqual(lockstep, base, 'control anchor');
    assert.deepEqual(removed(base, lockstep), ['define FC_SESSION_STEP_UNITS=16'], 'a lockstep edit of a frozen value is refused');
    // A duplicated line counts: removing one of two copies is refused.
    assert.deepEqual(removed(`${base}\n${lines[1]}\n`, base), [lines[1]], 'each copy of a line is kept');
    console.log(`session ABI manifest control: additions, insertions and reordering pass; ${red} removals and edits of all ${lines.length} lines are refused`);
} else {
    if (args.length !== 2) {
        console.error('usage: session-abi-append-only.mjs <base manifest> <proposed manifest> | --self-test');
        process.exit(2);
    }
    const gone = removed(readFileSync(args[0], 'utf8'), readFileSync(args[1], 'utf8'));
    if (gone.length) {
        console.error(`the frozen session ABI manifest may only grow — ${gone.length} base line(s) removed or edited:\n${gone.join('\n')}`);
        process.exit(1);
    }
    console.log(`session ABI manifest: all ${linesOf(readFileSync(args[0], 'utf8')).length} base lines kept`);
}
