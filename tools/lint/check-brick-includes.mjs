// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE BRICKS NEVER INCLUDE THE SESSION. modules/mastering, modules/analysis_offline, modules/tempo and modules/storage
// are the bricks felitronics::session is built from; the session may include them, never the other way round — the
// repository is to be split along that line, and a brick that reaches into the session (its headers, its sources, its
// test helpers, its target) would not survive the split. This lint reads every file under the four bricks:
//
//   INCLUDE   a C/C++ #include (any spacing after the `#`, quoted or angled) is refused when it names felitronics/session/,
//             or when a quoted name, resolved from the including file's directory, lands inside modules/session. An
//             #include whose operand is not a quoted or angled name (a macro) cannot be read and is refused as well.
//   CMAKE     a CMakeLists.txt or *.cmake is refused when it names the session's target (felitronics_session…,
//             felitronics::session) or a path into modules/session (modules/session, ../session).
//
// Shared test helpers live in tests/ at the root (tests/DeclaredBudget.h), which both sides may include.
//
// Usage: node tools/lint/check-brick-includes.mjs [--self-test] [--controls]     (from the repository root)
//   (no flag)    scans the tree; exit 1 with file:line for every violation
//   --self-test  the rules against planted sources in memory: each violation found, each allowed spelling passed
//   --controls   the real tree with one violation of each kind planted in a real file of every brick: each is found
//                at its file and line, so the scan is shown to reach every brick and to read its real files
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, dirname, resolve, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

export const BRICKS = ['modules/mastering', 'modules/analysis_offline', 'modules/tempo', 'modules/storage'];
const SESSION = 'modules/session';
const SOURCE = /\.(h|hh|hpp|hxx|inc|ipp|c|cc|cpp|cxx|mm)$/;
const CMAKE = /(^|[\\/])CMakeLists\.txt$|\.cmake$/;

const posix = p => p.split(sep).join('/');
const inside = (root, path) => { const r = posix(relative(root, path)); return r === '' || (!r.startsWith('../') && r !== '..'); };

// Every violation of one file's text: [{ line, rule, what }]. `file` is its path relative to `root`.
export function scanText (root, file, text)
{
    const found = [];
    const lines = text.split(/\r?\n/);
    if (CMAKE.test(file))
    {
        lines.forEach((l, i) =>
        {
            const code = l.replace(/#.*$/, '');
            const m = /felitronics_session\w*|felitronics::session\b|modules\/session\b|\.\.\/session\b/.exec(code);
            if (m) found.push({ line: i + 1, rule: 'CMAKE', what: m[0] });
        });
        return found;
    }
    if (!SOURCE.test(file)) return found;
    lines.forEach((l, i) =>
    {
        const d = /^\s*#\s*include(?:_next)?\b\s*(.*)$/.exec(l);
        if (!d) return;
        const operand = d[1].replace(/\/\/.*$|\/\*.*?\*\//g, '').trim();
        const q = /^"([^"]+)"$/.exec(operand), a = /^<([^>]+)>$/.exec(operand);
        if (!q && !a) { found.push({ line: i + 1, rule: 'INCLUDE', what: `unreadable operand ${operand}` }); return; }
        const name = posix((q ?? a)[1].replace(/\\/g, '/'));
        if (/(^|\/)felitronics\/session\//.test(name)) { found.push({ line: i + 1, rule: 'INCLUDE', what: name }); return; }
        if (q && inside(resolve(root, SESSION), resolve(root, dirname(file), name)))
            found.push({ line: i + 1, rule: 'INCLUDE', what: name });
        else if (/(^|\/)modules\/session\//.test(name)) found.push({ line: i + 1, rule: 'INCLUDE', what: name });
    });
    return found;
}

function walk (root, dir, acc)
{
    for (const e of readdirSync(join(root, dir)).sort())
    {
        const rel = `${dir}/${e}`;
        if (statSync(join(root, rel)).isDirectory()) walk(root, rel, acc);
        else acc.push(rel);
    }
    return acc;
}

// Every brick's files, relative to root. A brick directory that is gone is an error: the lint would pass on nothing.
export function brickFiles (root)
{
    const out = [];
    for (const b of BRICKS)
    {
        if (!existsSync(join(root, b))) throw Error(`check-brick-includes: ${b} is missing — update BRICKS`);
        walk(root, b, out);
    }
    return out;
}

export function scanTree (root, plant = new Map())
{
    const found = [];
    for (const f of brickFiles(root))
    {
        const text = plant.has(f) ? plant.get(f) : readFileSync(join(root, f), 'utf8');
        for (const v of scanText(root, f, text)) found.push({ file: f, ...v });
    }
    return found;
}

function selfTest (root)
{
    const M = 'modules/mastering/src/X.cpp', T = 'modules/mastering/tests/XTests.cpp', C = 'modules/tempo/CMakeLists.txt';
    const cases = [
        [M, '#include <felitronics/session/Session.h>\n', ['INCLUDE']],
        [M, '#  include "felitronics/session/Project.h" // a comment\n', ['INCLUDE']],
        [T, '#include "../../session/tests/DeclaredBudget.h"\n', ['INCLUDE']],
        [T, '#include "../../session/src/Devices.h"\n', ['INCLUDE']],
        [M, '#include "../../../modules/session/include/felitronics/session/Project.h"\n', ['INCLUDE']],
        [M, '#include <modules/session/src/Rules.h>\n', ['INCLUDE']],
        [M, '#include "..\\..\\session\\src\\Rules.h"\n', ['INCLUDE']],
        [M, '#include SESSION_HEADER\n', ['INCLUDE']],
        [M, 'int a;\n\t# include_next <felitronics/session/Text.h>\n', ['INCLUDE']],
        [C, 'target_link_libraries(x PRIVATE felitronics::session)\n', ['CMAKE']],
        [C, 'target_link_libraries(x PRIVATE felitronics_session)\n', ['CMAKE']],
        [C, 'target_include_directories(x PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../session/src)\n', ['CMAKE']],
        [M, '#include <felitronics/mastering/MasteringChain.h>\n#include "../../../tests/DeclaredBudget.h"\n', []],
        [T, '#include "../../../tests/split_invariance.h"\n#include "sessionless.h"\n', []],
        [M, '// #include <felitronics/session/Session.h> is what this file must never say\n', []],
        [C, '# felitronics::session links this brick, never the other way round\ntarget_link_libraries(x PUBLIC felitronics::core)\n', []],
        ['modules/mastering/README.md', '#include <felitronics/session/Session.h>\n', []],
    ];
    let bad = 0;
    for (const [file, text, want] of cases)
    {
        const got = scanText(root, file, text).map(f => f.rule);
        if (got.join(',') !== want.join(','))
        { console.error(`  SELF-TEST FAIL: wanted [${want}], got [${got}] for ${file}: ${JSON.stringify(text)}`); bad++; }
    }
    if (bad) { console.error(`brick-includes lint self-test: ${bad} of ${cases.length} cases wrong`); process.exit(1); }
    console.log(`brick-includes lint self-test: ${cases.length}/${cases.length} cases correct`);
}

// One real source file and one real CMake file of every brick (where it has one), each with a violation planted after
// its last line: the scan of the whole tree must name exactly those files and lines, and nothing else.
function controls (root)
{
    if (scanTree(root).length) { console.error('brick-includes controls: the tree is not clean — run the lint first'); process.exit(1); }
    const files = brickFiles(root), plant = new Map(), want = [];
    for (const b of BRICKS)
    {
        const mine = files.filter(f => f.startsWith(`${b}/`));
        const source = mine.find(f => SOURCE.test(f)), cmake = mine.find(f => CMAKE.test(f));
        for (const [f, line] of [[source, '#include <felitronics/session/Session.h>'], [cmake, 'target_link_libraries(x PRIVATE felitronics::session)']])
        {
            if (!f) continue;
            const text = readFileSync(join(root, f), 'utf8'), at = text.split(/\r?\n/).length + (text.endsWith('\n') ? 0 : 1);
            plant.set(f, `${text}${text.endsWith('\n') ? '' : '\n'}${line}\n`);
            want.push(`${f}:${at}`);
        }
    }
    const got = scanTree(root, plant).map(v => `${v.file}:${v.line}`);
    const missing = want.filter(w => !got.includes(w)), extra = got.filter(g => !want.includes(g));
    if (missing.length || extra.length)
    {
        console.error(`brick-includes controls FAILED — not found: ${missing.join(' ') || 'none'}; unexpected: ${extra.join(' ') || 'none'}`);
        process.exit(1);
    }
    console.log(`brick-includes controls: ${want.length} planted violations over ${BRICKS.length} bricks, each found at its file and line`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1])
{
    const root = process.cwd(), args = process.argv.slice(2);
    if (!existsSync(join(root, SESSION)) || !existsSync(join(root, 'tools/lint')))
    { console.error('check-brick-includes: run from the root of felitronics-mastering-core'); process.exit(2); }
    if (args.some(a => a !== '--self-test' && a !== '--controls'))
    { console.error('usage: node tools/lint/check-brick-includes.mjs [--self-test] [--controls]'); process.exit(2); }
    if (args.includes('--self-test')) selfTest(root);
    if (args.includes('--controls')) controls(root);
    if (args.length === 0)
    {
        const found = scanTree(root);
        for (const v of found) console.error(`${v.file}:${v.line}: ${v.rule} — a brick reaches into the session: ${v.what}`);
        if (found.length) { console.error(`check-brick-includes: ${found.length} violation(s)`); process.exit(1); }
        console.log(`check-brick-includes: ${brickFiles(root).length} files under ${BRICKS.join(', ')} — none includes the session`);
    }
}
