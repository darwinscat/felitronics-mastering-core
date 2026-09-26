// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE fcsession MODULE, CHECKED ON THE ARTIFACT — what felitronics_session_abi_tests cannot see natively, because it
// compiles fc_session.cpp into its own binary: that the entry points REACHED the module a page loads, that nothing
// else did, and that they behave there as the header says, with wasm32's addresses and the one check that only exists
// on this tier (an out-pointer past the end of the heap).
//
//   node tools/wasm/session-check.mjs tools/wasm/build/fcsession.node.js
//
// WHICH MODULE, from the path and then from the artifact (the rule tools/wasm/module-identity.mjs keeps for the
// analysis modules): the file must be named fcsession.*, must answer fc_session_abi_version with the number
// tools/fc_session_abi.h declares, and must answer no other module's version. Every status code and the table's
// capacity are read from the header too, so this file restates no number of the contract.
//
// Exit status: 0 every check passed; 1 a check failed; 3 the module cannot be trusted to be fcsession at all.

import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { basename, resolve } from 'node:path';

const [, , modPath] = process.argv;
if (! modPath) { console.error('usage: node session-check.mjs <fcsession.node.js>'); process.exit(2); }
const distrust = m => { console.error(`session-check: ${m}`); process.exit(3); };

const header = readFileSync(new URL('../fc_session_abi.h', import.meta.url), 'utf8');
const macro = name =>
{
    const m = new RegExp(`^#define ${name} ([0-9]+)u$`, 'm').exec(header);
    if (! m) distrust(`no ${name} in tools/fc_session_abi.h`);
    return Number(m[1]);
};
const VERSION = macro('FC_SESSION_ABI_VERSION');
const MAX_HANDLES = macro('FC_SESSION_MAX_HANDLES');
const STATUS = {};
for (const m of header.matchAll(/^\s*FC_SESSION_(OK|ERR_[A-Z_]+)\s*=\s*([0-9]+)/gm)) STATUS[m[1]] = Number(m[2]);
for (const k of ['OK', 'ERR_POISONED', 'ERR_NULL', 'ERR_ALIGNMENT', 'ERR_SPAN', 'ERR_HANDLE', 'ERR_EXHAUSTED'])
    if (STATUS[k] === undefined) distrust(`tools/fc_session_abi.h declares no FC_SESSION_${k}`);

// THE SURFACE, per version — what the module exports, EXACTLY, besides the heap's _malloc/_free. A bump of
// FC_SESSION_ABI_VERSION appends its line here on purpose (tools/fc_session_abi.h, rule 4).
const SURFACE = {
    1: ['_fc_session_abi_version', '_fc_session_create', '_fc_session_destroy'],
};

if (! basename(modPath).startsWith('fcsession.')) distrust(`${modPath}: this checks fcsession.* and nothing else`);
const require = createRequire(import.meta.url);
const M = await require(resolve(modPath))();
for (const other of ['_fc_probe_abi_version', '_fc_tempo_abi_version', '_fc_master_abi_version'])
    if (typeof M[other] === 'function') distrust(`${modPath} is named fcsession but answers ${other.slice(1)}`);
if (typeof M._fc_session_abi_version !== 'function') distrust(`${modPath} has no fc_session_abi_version`);
const version = M._fc_session_abi_version();
if (version !== VERSION) distrust(`${modPath}: fc_session_abi_version() answers ${version}, the header declares ${VERSION}`);
const surface = SURFACE[version];
if (! surface) distrust(`FC_SESSION_ABI_VERSION is ${version} and this file lists no surface for it — append one on purpose`);

let checks = 0, bad = 0;
const ok = (cond, what) => { ++checks; if (! cond) { ++bad; console.error(`FAIL: ${what}`); } };

const exported = Object.keys(M).filter(k => k.startsWith('_fc_')).sort();
const want = [...surface].sort();
ok(exported.join(' ') === want.join(' '),
   `fcsession exports exactly the v${version} surface — extra: [${exported.filter(k => ! want.includes(k)).join(' ')}],`
   + ` missing: [${want.filter(k => ! exported.includes(k)).join(' ')}]`);
ok(typeof M._malloc === 'function' && typeof M._free === 'function', '_malloc and _free reached the artifact');

// No HEAP view is held across a call: a create may grow the memory, and a grown memory detaches every view taken
// before it. So each read takes M.HEAPU32 afresh.
const SENTINEL = 0xC0FFEE;
const out = M._malloc(8);
const read = () => M.HEAPU32[out >>> 2];
const write = v => { M.HEAPU32[out >>> 2] = v; };

// create and destroy
write(SENTINEL);
ok(M._fc_session_create(out) === STATUS.OK && read() !== 0 && read() !== SENTINEL, 'create writes a handle that is not 0');
const h = read();
ok(M._fc_session_destroy(h) === STATUS.OK, 'the handle destroys');
ok(M._fc_session_destroy(h) === STATUS.ERR_HANDLE, 'and is refused once destroyed');
ok(M._fc_session_destroy(0) === STATUS.ERR_HANDLE, '0 is never a handle');

// the out-pointer's checks, in the header's order, each leaving the memory as it was
write(SENTINEL);
ok(M._fc_session_create(0) === STATUS.ERR_NULL, 'a null out-pointer is refused');
ok(M._fc_session_create(out + 1) === STATUS.ERR_ALIGNMENT && read() === SENTINEL, 'a misaligned out-pointer is refused, and nothing is written');
const heapEnd = M.HEAPU32.buffer.byteLength;
ok(M._fc_session_create(heapEnd) === STATUS.ERR_SPAN, 'an out-pointer at the end of the heap is refused as SPAN — the check only this tier makes');
ok(M._fc_session_create(heapEnd - 2) === STATUS.ERR_ALIGNMENT, '...and one straddling it is refused before its span is looked at');

// the table
const live = [];
let allCreated = true;
for (let i = 0; i < MAX_HANDLES; i++)
{
    const st = M._fc_session_create(out);
    allCreated = allCreated && st === STATUS.OK && read() !== 0;
    live.push(read());
}
ok(allCreated && new Set(live).size === MAX_HANDLES, `${MAX_HANDLES} creates give ${MAX_HANDLES} distinct handles`);
write(SENTINEL);
ok(M._fc_session_create(out) === STATUS.ERR_EXHAUSTED && read() === SENTINEL,
   'the next create is refused as EXHAUSTED and leaves *out as it was');
ok(live.every(x => M._fc_session_destroy(x) === STATUS.OK), 'every live handle destroys');
ok(M._fc_session_create(out) === STATUS.OK && ! live.includes(read()), 'a create after that fits, under a handle never issued before');
ok(M._fc_session_destroy(read()) === STATUS.OK, 'and destroys');
M._free(out);

console.log(`session-check: fcsession v${version} — ${checks} checks, ${bad} failures`);
process.exit(bad ? 1 : 0);
