// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE fcsession MODULE, CHECKED ON THE ARTIFACT — what felitronics_session_abi_tests cannot see natively, because it
// compiles fc_session.cpp into its own binary: that the entry points REACHED the module a page loads, that NOTHING ELSE
// did, and that they behave there as the header says, with wasm32's addresses and the one check that only exists on
// this tier (an out-pointer past the end of the heap). And the wrap boundary, walked for real: one slot driven through
// all of its generations — 16.7 million create/destroy cycles, a fraction of a second — must retire, not wrap.
//
//   node tools/wasm/session-check.mjs tools/wasm/build/fcsession.node.js [--config-version <16 hex digits>]
//
// --config-version: what the native `fcore_session config version` answers; the module must carry the same config (CI
// passes it, so the two roads into the session are held to one config).
//
// WHICH MODULE, from the path and then from the artifact (the rule tools/wasm/module-identity.mjs keeps for the
// analysis modules): the file must be named fcsession.*, must answer fc_session_abi_version with the number
// tools/fc_session_abi.h declares, and must answer no other module's version. Every status code and constant is read
// from the header too, so this file restates no number of the contract.
//
// THE EXPORTS ARE COMPARED WHOLE: every property of the loaded Module — not only the names that begin with `_fc_` —
// against the ABI's surface for this version plus the runtime's own, listed below. A callable that reached the module
// without being declared (EMSCRIPTEN_KEEPALIVE exports it whether or not the ABI's extractor saw it) is refused by name;
// tools/wasm/build.sh builds exactly such a module as a control and requires this check to refuse it.
//
// Exit status: 0 every check passed; 1 a check failed; 3 the module cannot be trusted to be fcsession at all.

import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { basename, resolve } from 'node:path';

const [, , modPath, ...options] = process.argv;
const usage = () => { console.error('usage: node session-check.mjs <fcsession.node.js> [--config-version <16 hex digits>]'); process.exit(2); };
if (! modPath) usage();
let nativeConfigVersion = null;
if (options.length)
{
    if (options.length !== 2 || options[0] !== '--config-version' || ! /^[0-9a-f]{16}$/.test(options[1])) usage();
    nativeConfigVersion = options[1];
}
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
const SLOT_GENERATIONS = macro('FC_SESSION_SLOT_GENERATIONS');
const STATUS = {};
for (const m of header.matchAll(/^\s*FC_SESSION_(OK|ERR_[A-Z_]+)\s*=\s*([0-9]+)/gm)) STATUS[m[1]] = Number(m[2]);
for (const k of ['OK', 'ERR_POISONED', 'ERR_NULL', 'ERR_ALIGNMENT', 'ERR_SPAN', 'ERR_HANDLE', 'ERR_EXHAUSTED'])
    if (STATUS[k] === undefined) distrust(`tools/fc_session_abi.h declares no FC_SESSION_${k}`);

// THE SURFACE of the version this module answers — its entry points, as the page sees them. Version 0 is the draft
// (tools/fc_session_abi.h): no promise, so a change of the draft edits this line with the header.
const SURFACE = {
    0: ['_fc_session_abi_version', '_fc_session_create', '_fc_session_destroy', '_fc_session_config_version'],
};
// ...and what the RUNTIME adds, and nothing else may: the heap's allocator for the page's buffers, and the one view of
// the heap the page reads handles through (build.sh's -sEXPORTED_RUNTIME_METHODS).
const RUNTIME = ['_malloc', '_free', 'HEAPU32'];

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

const exported = Object.keys(M).sort();
const want = [...surface, ...RUNTIME].sort();
ok(exported.join(' ') === want.join(' '),
   `fcsession exports exactly the v${version} surface and the runtime's own — extra: [${exported.filter(k => ! want.includes(k)).join(' ')}],`
   + ` missing: [${want.filter(k => ! exported.includes(k)).join(' ')}]`);
ok(surface.every(k => typeof M[k] === 'function') && typeof M._malloc === 'function' && typeof M._free === 'function',
   'every entry point, _malloc and _free are callable');

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

// THE WRAP BOUNDARY, for real, at the shipped width. Every slot is free, so each create takes slot 0 (handle & 0xFF ==
// 1) until slot 0 retires; then the next create is slot 1's. Reproduced before the fix: the create after 16 777 214
// cycles answered the first handle again, and destroying it through a stale copy destroyed the new session.
const seen = [];
let first = 0, last = 0, cycles = 0, allOk = true;
// BOUNDED: a slot that wraps instead of retiring would take slot 0 for ever, and a regression must fail, not hang.
while (cycles < SLOT_GENERATIONS + 2)
{
    if (M._fc_session_create(out) !== STATUS.OK) { allOk = false; break; }
    const h = read();
    if (first === 0) first = h;
    if ((h & 0xFF) !== 1) { seen.push(h); break; }
    last = h;
    if (M._fc_session_destroy(h) !== STATUS.OK) { allOk = false; break; }
    ++cycles;
}
const next = seen[0];
ok(allOk, `every create and destroy on the way succeeded (${cycles} cycles)`);
ok((last >>> 8) === SLOT_GENERATIONS, `slot 0's last handle carries its last generation (${last >>> 8} of ${SLOT_GENERATIONS})`);
ok(next !== undefined && (next & 0xFF) === 2, 'the next create went to slot 1: slot 0 retired, it did not wrap');
ok(M._fc_session_destroy(first) === STATUS.ERR_HANDLE && M._fc_session_destroy(last) === STATUS.ERR_HANDLE
   && M._fc_session_destroy((1 << 8) | 1) === STATUS.ERR_HANDLE,
   "slot 0's handles stay refused — the first seen, the last, and the number a wrap would have issued again");
ok(next !== undefined && M._fc_session_destroy(next) === STATUS.OK, "slot 1's session destroys");
M._free(out);

// the config version: two uint32 halves, low first, the module's own config
const halves = M._malloc(8);
const half = i => M.HEAPU32[(halves >>> 2) + i];
M.HEAPU32[halves >>> 2] = SENTINEL; M.HEAPU32[(halves >>> 2) + 1] = SENTINEL;
ok(M._fc_session_config_version(halves) === STATUS.OK, 'config_version answers');
const configVersion = half(1).toString(16).padStart(8, '0') + half(0).toString(16).padStart(8, '0');
ok(! (half(0) === SENTINEL && half(1) === SENTINEL), 'and writes its two halves');
ok(M._fc_session_config_version(halves) === STATUS.OK
   && configVersion === half(1).toString(16).padStart(8, '0') + half(0).toString(16).padStart(8, '0'), 'the same number twice');
ok(M._fc_session_config_version(0) === STATUS.ERR_NULL, 'a null out-pointer is refused');
ok(M._fc_session_config_version(halves + 2) === STATUS.ERR_ALIGNMENT, 'a misaligned one is refused');
const end = M.HEAPU32.buffer.byteLength;
ok(M._fc_session_config_version(end - 4) === STATUS.ERR_SPAN, 'one whose high half would leave the heap is refused as SPAN');
if (nativeConfigVersion !== null)
    ok(configVersion === nativeConfigVersion, `the module's config version ${configVersion} is the native CLI's ${nativeConfigVersion}`);
M._free(halves);
console.log(`session-check: config version ${configVersion}`);

console.log(`session-check: fcsession v${version}${version === 0 ? ' (the draft)' : ''} — ${checks} checks, ${bad} failures`);
process.exit(bad ? 1 : 0);
