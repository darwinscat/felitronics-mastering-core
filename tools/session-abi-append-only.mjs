// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE FROZEN MANIFEST ONLY GROWS FROM ITS DECLARED BASE. tools/session-abi-check.mjs holds the compiled surface to
// tools/session-abi-v1.txt, and so cannot see a pull request that edits a frozen line in the manifest in lockstep with
// the surface. This holds the manifest to its base branch's: every base line must still be there (as often as it was), in
// any order, and lines may be added anywhere. A removed or edited line — a comment included — is refused.
//
// The manifest declares the release its surface starts from in one line, `base v<major>.<minor>.<patch>` (v0.6.0 is the
// one declared reset, docs/SESSION.md "ABI"). Append-only holds between two manifests that declare the same base. A
// proposed manifest that declares a NEWER base than the base branch's (or the first declaration, over a manifest that has
// none) starts the surface anew, once: from then on both declare it and append-only holds again. A proposed manifest that
// declares an OLDER base, or none over a base that has one, is refused. CI runs it on pull requests.
//
//   node tools/session-abi-append-only.mjs <base manifest> <proposed manifest>
//   node tools/session-abi-append-only.mjs --self-test
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const linesOf = text => text.split(/\r?\n/).filter(line => line.trim() !== '');
// The surface lines, compared as content; the base declaration is compared by verdict() as an ordered version.
const surfaceOf = text => linesOf(text).filter(line => !line.startsWith('base'));
function removed(base, proposed) {
    const left = new Map();
    for (const line of surfaceOf(proposed)) left.set(line, (left.get(line) ?? 0) + 1);
    const gone = [];
    for (const line of surfaceOf(base)) {
        const n = left.get(line) ?? 0;
        if (n === 0) gone.push(line); else left.set(line, n - 1);
    }
    return gone;
}
// The declared base: [major, minor, patch], or null for a manifest from before the first declaration.
function declaredBase(text) {
    const declared = linesOf(text).filter(line => line.startsWith('base'));
    if (declared.length === 0) return null;
    const m = declared.length === 1 && /^base v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.exec(declared[0]);
    if (!m) throw Error(`the manifest declares its base in exactly one line "base v<major>.<minor>.<patch>": ${declared.join(' | ')}`);
    return [Number(m[1]), Number(m[2]), Number(m[3])];
}
const order = (a, b) => a === null ? (b === null ? 0 : -1) : b === null ? 1 : a[0] - b[0] || a[1] - b[1] || a[2] - b[2];
const named = v => v === null ? 'no base' : `base v${v.join('.')}`;
// { ok, reason, gone }: the verdict on a proposed manifest against its base branch's.
function verdict(base, proposed) {
    let from, to;
    try { from = declaredBase(base); to = declaredBase(proposed); } catch (e) { return { ok: false, reason: e.message, gone: [] }; }
    const cmp = order(to, from);
    if (cmp < 0) return { ok: false, reason: `the proposed manifest declares ${named(to)}, older than the base branch's ${named(from)}`, gone: [] };
    if (cmp > 0) return { ok: true, reason: `the proposed manifest starts a new ${named(to)} (the base branch's: ${named(from)})`, gone: [] };
    const gone = removed(base, proposed);
    return gone.length
        ? { ok: false, reason: `the frozen session ABI manifest may only grow — ${gone.length} base line(s) removed or edited`, gone }
        : { ok: true, reason: `all ${linesOf(base).length} base lines kept (${named(from)})`, gone };
}
const args = process.argv.slice(2);
if (args[0] === '--self-test') {
    const base = readFileSync(new URL('session-abi-v1.txt', import.meta.url), 'utf8'), lines = linesOf(base);
    const at = declaredBase(base);
    assert(at !== null, 'the manifest declares its base');
    const declaration = `base v${at.join('.')}`, withBase = (text, v) => text.replace(declaration, v === null ? '' : `base v${v.join('.')}`);
    assert.deepEqual(removed(base, base), [], 'the unchanged manifest passes');
    assert.deepEqual(removed(base, `${base}\nfunction fc_session_future fc_session_status(fc_session)\n`), [], 'an appended line passes');
    assert.deepEqual(removed(base, [lines[0], 'wire Future.inserted number', ...lines.slice(1)].join('\n')), [], 'an inserted line passes');
    assert.deepEqual(removed(base, [...lines].reverse().join('\r\n')), [], 'order and line endings are not content');
    let red = 0;
    for (let i = 0; i < lines.length; ++i) {
        const gone = lines.filter((_, j) => j !== i).join('\n');
        const edited = lines.map((line, j) => j === i ? `${line} CHANGED` : line).join('\n');
        assert.equal(verdict(base, gone).ok, false, `removing line ${i + 1} is refused`);
        assert.equal(verdict(base, edited).ok, false, `editing line ${i + 1} is refused`);
        if (lines[i] !== declaration) {
            assert.deepEqual(verdict(base, gone).gone, [lines[i]], `removing line ${i + 1} names it`);
            assert.deepEqual(verdict(base, edited).gone, [lines[i]], `editing line ${i + 1} names it`);
        }
        red += 2;
    }
    // The lockstep edit the compiled gate cannot see: a frozen value changed in the floor together with the surface.
    const lockstep = base.replace('define FC_SESSION_STEP_UNITS=16', 'define FC_SESSION_STEP_UNITS=17');
    assert.notEqual(lockstep, base, 'control anchor');
    assert.deepEqual(verdict(base, lockstep).gone, ['define FC_SESSION_STEP_UNITS=16'], 'a lockstep edit of a frozen value is refused');
    // A duplicated line counts: removing one of two copies is refused.
    assert.deepEqual(removed(`${base}\n${lines[1]}\n`, base), [lines[1]], 'each copy of a line is kept');
    // THE DECLARED BASE. A removal under the same base is refused (above); under a newer base the surface starts anew.
    const cut = lines.filter(line => line !== 'define FC_SESSION_STEP_UNITS=16').join('\n');
    const newer = [at[0], at[1] + 1, 0], older = at[1] > 0 ? [at[0], at[1] - 1, 0] : [at[0] - 1, 0, 0];
    assert.equal(verdict(base, cut).ok, false, 'a removal under the same base is refused');
    assert.equal(verdict(base, withBase(cut, newer)).ok, true, 'a removal under a newer declared base is accepted');
    assert.equal(verdict(withBase(base, null), cut).ok, true, 'the first declaration over an undeclared manifest is accepted');
    assert.equal(verdict(base, withBase(base, older)).ok, false, 'a proposed manifest declaring an older base is refused');
    assert.equal(verdict(base, withBase(base, null)).ok, false, 'a proposed manifest dropping the declaration is refused');
    assert.equal(verdict(withBase(base, newer), withBase(base, newer)).ok, true, 'the same newer base on both sides passes');
    assert.equal(verdict(withBase(base, newer), withBase(cut, newer)).ok, false, 'once both declare the newer base, append-only holds again');
    assert.equal(verdict(withBase(base, null), withBase(cut, null)).ok, false, 'two undeclared manifests hold append-only');
    assert.throws(() => declaredBase(`${base}\nbase v9.0.0\n`), /exactly one line/, 'two declarations are refused');
    assert.throws(() => declaredBase(withBase(base, null).replace(/^/, 'base 0.6\n')), /exactly one line/, 'a malformed declaration is refused');
    console.log(`session ABI manifest control: additions, insertions and reordering pass; ${red} removals and edits of all ${lines.length} lines are refused; `
        + 'a newer declared base starts anew once, an older or dropped one is refused, the same base holds append-only');
} else {
    if (args.length !== 2) {
        console.error('usage: session-abi-append-only.mjs <base manifest> <proposed manifest> | --self-test');
        process.exit(2);
    }
    const { ok, reason, gone } = verdict(readFileSync(args[0], 'utf8'), readFileSync(args[1], 'utf8'));
    if (!ok) {
        console.error(`${reason}${gone.length ? `:\n${gone.join('\n')}` : ''}`);
        process.exit(1);
    }
    console.log(`session ABI manifest: ${reason}`);
}
