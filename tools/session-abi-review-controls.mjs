// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Freeze review controls for the non-runtime promises; an optional root supports baseline audits.
import assert from 'node:assert/strict';
import {existsSync, readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
const root = resolve(process.argv[2] ?? fileURLToPath(new URL('../', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
// A changelog.d fragment until a release folds it, then its section in CHANGELOG.md: the fold keeps the fragment's
// heading line and body, and deletes the file.
const released = (fragment, heading) => {
    if (existsSync(resolve(root, 'changelog.d', fragment))) return read(`changelog.d/${fragment}`);
    const lines = read('CHANGELOG.md').split(/\r?\n/), at = lines.indexOf(heading);
    assert(at >= 0 && lines.indexOf(heading, at + 1) < 0, `changelog.d/${fragment} or one CHANGELOG.md section "${heading}"`);
    const end = lines.findIndex((line, i) => i > at && /^#{2,3} /.test(line));
    return lines.slice(at, end < 0 ? lines.length : end).join('\n');
};
const selected = process.argv[3];
const checks = {
    scope() {
        assert(!/enum felitronics::session::(?:Command|Event|Column|Status|CodecStatus|EventKind|text::(?:Lang|Plural))::/.test(read('tools/session-abi-v1.txt')),
            'the permanent manifest contains only C/wire-observable numeric values');
        const tests = read('tools/tests/SessionAbiV1Tests.cpp');
        for (const name of ['Command', 'Event', 'Column', 'Status', 'CodecStatus'])
            assert(tests.includes(`unsigned (${name}::`), `${name} internal checks remain outside the floor`);
    },
    docs() {
        const state = released('session-2-state.md',
            "### session · tools — the session's states and commands: one table of who may do what, when, and a project in two layers");
        const project = released('session-5-project.md', '### session — canonical TOML projects and recovery by replay');
        assert(!/only in the manual mode|travel and step exactly|mode off takes back/.test(state), 'no obsolete manual/grid/clear behavior');
        assert.match(state, /regardless of panel visibility/);
        assert.match(state, /mode off hides the panel and preserves every edit/);
        assert.match(state, /slider steps guide the UI/);
        assert(!/same core verifies|different core preserves/.test(project), 'no obsolete same-core exception');
        assert.match(project, /Every saved machine layer is preserved/);
        assert.match(project, /including one carrying the same core stamp/);
    },
    targets() {
        const source = read('tools/tests/SessionAbiV1Tests.cpp');
        const begin = source.indexOf('void directCapabilities()'), end = source.indexOf('void guards()', begin);
        const body = source.slice(begin, end);
        assert.match(body, /"allStreaming", "cd", "lp"/);
        assert.match(body, /ok \(changed.rejection == Rejection::None/);
        assert(!/UnknownTarget/.test(body), 'no silent skip of an unknown target');
    }
};
for (const [name, check] of Object.entries(checks)) if (!selected || selected === name) {
    check(); console.log(`freeze review control: ${name} GREEN`);
}
assert(!selected || selected in checks, 'known control');
