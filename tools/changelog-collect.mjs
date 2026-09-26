// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Folds `changelog.d/*.md` into CHANGELOG.md. See changelog.d/README.md for WHY the fragments exist;
// this file is only the folding. felitronics-core's tools/changelog-collect.mjs, adapted to this repository.
//
//   node tools/changelog-collect.mjs --preview            what the next release would say, writes nothing
//   node tools/changelog-collect.mjs --release v0.2.0     folds, renames the heading, deletes the fragments,
//                                                         and moves `project(... VERSION)` in CMakeLists.txt
//
// Order is by file name, with every run of digits compared as a number (`x-2` before `x-10`). Core ranks by a
// leading task id instead; this repository keeps task ids out of its public files, so the name alone decides.
// README.md is not a fragment.

import { readdirSync, readFileSync, writeFileSync, unlinkSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root  = join(dirname(fileURLToPath(import.meta.url)), '..');
const dir   = join(root, 'changelog.d');
const book  = join(root, 'CHANGELOG.md');
const cmake = join(root, 'CMakeLists.txt');

const args   = process.argv.slice(2);
const preview = args.includes('--preview');
const version = (args[args.indexOf('--release') + 1] || '').trim();

if (preview === (args.includes('--release')))
{
    console.error('usage: changelog-collect.mjs --preview | --release vX.Y.Z');
    process.exit(2);
}
if (! preview && ! /^v\d+\.\d+\.\d+$/.test(version))
{
    console.error(`--release needs a version like v0.2.0, got "${version}"`);
    process.exit(2);
}

// The release's other line: core's release commits move `project(felitronics_core VERSION x.y.z` by hand beside
// the fold; here the fold moves it, so the tag, the heading and the project VERSION cannot disagree.
const versionLine = /^(project\s*\(\s*felitronics_mastering_core\s+VERSION\s+)(\d+)\.(\d+)\.(\d+)/m;
const cmakeText   = readFileSync(cmake, 'utf8');
const current     = versionLine.exec(cmakeText);
if (! current)
{
    console.error('CMakeLists.txt: no `project(felitronics_mastering_core VERSION x.y.z` line to move');
    process.exit(1);
}

// Natural order, locale-free: digit runs as numbers, everything else by code point.
const chunks = s => s.match(/\d+|\D+/g) ?? [];
const natural = (a, b) => {
    const ca = chunks(a), cb = chunks(b);
    for (let i = 0; i < Math.min(ca.length, cb.length); ++i)
    {
        const x = ca[i], y = cb[i];
        const nx = /^\d/.test(x), ny = /^\d/.test(y);
        if (nx && ny && Number(x) !== Number(y)) return Number(x) - Number(y);
        if (x !== y) return x < y ? -1 : 1;
    }
    return ca.length - cb.length;
};

const files = readdirSync(dir)
    .filter(f => f.endsWith('.md') && f !== 'README.md')
    .sort(natural);

if (files.length === 0) { console.error('changelog.d/ holds no fragments'); process.exit(preview ? 0 : 1); }

const folded = files
    .map(f => readFileSync(join(dir, f), 'utf8').replace(/\s+$/, ''))
    .join('\n\n');

if (preview)
{
    console.log(`# ${files.length} fragment(s), in release order — the next release after ${current.slice(2).join('.')}\n`);
    for (const f of files) console.log(`  ${f}`);
    console.log('\n' + '-'.repeat(78) + '\n');
    console.log(folded);
    process.exit(0);
}

const target = version.slice(1).split('.').map(Number);
const have = current.slice(2).map(Number);
const newer = target[0] - have[0] || target[1] - have[1] || target[2] - have[2];
if (newer <= 0)
{
    console.error(`--release ${version} is not after the project VERSION ${have.join('.')} in CMakeLists.txt`);
    process.exit(2);
}

// THE LOCAL DATE, NOT toISOString()'s UTC. The heading sits beside a tag whose date git writes in local
// time, and the two disagreed in core for every release cut between midnight and 02:00 CEST. Nobody reads a
// changelog in UTC, and a heading a day behind its own tag reads as a mistake in the release, not the clock.
const now  = new Date();
const pad  = v => String (v).padStart (2, '0');
const date = `${now.getFullYear()}-${pad (now.getMonth() + 1)}-${pad (now.getDate())}`;
let text = readFileSync(book, 'utf8');

// Either there is an `## Unreleased` section to close, or the release opens its own above the newest one.
if (/^## Unreleased\s*$/m.test(text))
{
    const start = text.search(/^## Unreleased\s*$/m);
    const rest  = text.slice(start);
    const next  = rest.slice(1).search(/^## /m);           // the heading of the previous release, if any
    const body  = next === -1 ? rest : rest.slice(0, next + 1);
    const tail  = next === -1 ? ''   : rest.slice(next + 1);
    const kept  = body.replace(/^## Unreleased\s*$/m, '').replace(/\s+$/, '');
    text = text.slice(0, start)
         + `## ${version} — ${date}\n`
         + (kept ? kept + '\n\n' : '\n')
         + folded + '\n\n'
         + tail;
}
else
{
    const first = text.search(/^## /m);
    const at    = first === -1 ? text.length : first;
    text = text.slice(0, at) + `## ${version} — ${date}\n\n` + folded + '\n\n' + text.slice(at);
}

writeFileSync(book, text);
writeFileSync(cmake, cmakeText.replace(versionLine, `$1${target.join('.')}`));
for (const f of files) unlinkSync(join(dir, f));
console.log(`CHANGELOG.md: ${version} — ${date}, ${files.length} fragment(s) folded and removed; `
          + `CMakeLists.txt: VERSION ${have.join('.')} -> ${target.join('.')}.`);
