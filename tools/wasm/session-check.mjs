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
import { basename, resolve, dirname, join } from 'node:path';

import { types } from '../session-wire-types.mjs';
import { pathToFileURL } from 'node:url';

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

// The frozen surface, independently listed to catch unintended exports.
const SURFACE = {
    1: ['_fc_session_abi_version', '_fc_session_create', '_fc_session_destroy', '_fc_session_config_version',
        '_fc_session_set_capacity', '_fc_session_command_bytes', '_fc_session_load_bytes', '_fc_session_import_project_bytes',
        '_fc_session_create_bytes', '_fc_session_command', '_fc_session_load', '_fc_session_import_project',
        '_fc_session_export_project_size', '_fc_session_export_project_copy', '_fc_session_step',
        '_fc_session_events_size', '_fc_session_events_copy', '_fc_session_snapshot_size', '_fc_session_snapshot_copy',
        '_fc_session_measurement_bytes', '_fc_session_needles_bytes', '_fc_session_query_bytes', '_fc_session_query_size',
        '_fc_session_query_copy', '_fc_session_summary_size', '_fc_session_summary_copy'],
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
if (bad) process.exit(1);
const declarations = readFileSync(join(dirname(resolve(modPath)), 'snapshot.d.ts'), 'utf8');
const accepts = types(declarations);
const configConstant = /export declare const FC_SESSION_CONFIG_VERSION: "([a-f0-9]{16})";/.exec(declarations)?.[1];
if (!configConstant) distrust('generated .d.ts has no config version constant');
const constants = await import(pathToFileURL(join(dirname(resolve(modPath)), 'snapshot.mjs')));
ok(constants.FC_SESSION_CONFIG_VERSION === configConstant && constants.FC_SESSION_ABI_VERSION === VERSION, 'runtime constants agree with declarations');
const configLow = Number.parseInt(configConstant.slice(8), 16);
const configHigh = Number.parseInt(configConstant.slice(0, 8), 16);
const caps = M._malloc(32);
new DataView(M.HEAPU32.buffer).setFloat64(caps + 8, 256 * 1024 * 1024, true);
M.HEAPU32[caps >>> 2] = 32;
new DataView(M.HEAPU32.buffer).setFloat64(caps + 24, 256 * 1024 * 1024, true);
M.HEAPU32[(caps + 16) >>> 2] = 48000;
M.HEAPU32[(caps + 20) >>> 2] = macro('FC_SESSION_DEVICES_ALL');
const create = out => M._fc_session_create(caps, configLow, configHigh, out);
const SENTINEL = 0xC0FFEE;
const out = M._malloc(8);
const read = () => M.HEAPU32[out >>> 2];
const write = v => { M.HEAPU32[out >>> 2] = v; };

// create and destroy
write(SENTINEL);
ok(create(out) === STATUS.OK && read() !== 0 && read() !== SENTINEL, 'create writes a handle that is not 0');
const h = read();
ok(M._fc_session_destroy(h) === STATUS.OK, 'the handle destroys');
ok(M._fc_session_destroy(h) === STATUS.ERR_HANDLE, 'and is refused once destroyed');
ok(M._fc_session_destroy(0) === STATUS.ERR_HANDLE, '0 is never a handle');

// the out-pointer's checks, in the header's order, each leaving the memory as it was
write(SENTINEL);
ok(create(0) === STATUS.ERR_NULL, 'a null out-pointer is refused');
ok(create(out + 1) === STATUS.ERR_ALIGNMENT && read() === SENTINEL, 'a misaligned out-pointer is refused, and nothing is written');
const heapEnd = M.HEAPU32.buffer.byteLength;
ok(create(heapEnd) === STATUS.ERR_SPAN, 'an out-pointer at the end of the heap is refused as SPAN — the check only this tier makes');
ok(create(heapEnd - 2) === STATUS.ERR_ALIGNMENT, '...and one straddling it is refused before its span is looked at');

// the table
const live = [];
let allCreated = true;
for (let i = 0; i < MAX_HANDLES; i++)
{
    const st = create(out);
    allCreated = allCreated && st === STATUS.OK && read() !== 0;
    live.push(read());
}
ok(allCreated && new Set(live).size === MAX_HANDLES, `${MAX_HANDLES} creates give ${MAX_HANDLES} distinct handles`);
write(SENTINEL);
ok(create(out) === STATUS.ERR_EXHAUSTED && read() === SENTINEL,
   'the next create is refused as EXHAUSTED and leaves *out as it was');
ok(live.every(x => M._fc_session_destroy(x) === STATUS.OK), 'every live handle destroys');
ok(create(out) === STATUS.OK && ! live.includes(read()), 'a create after that fits, under a handle never issued before');
ok(M._fc_session_destroy(read()) === STATUS.OK, 'and destroys');

// THE WRAP BOUNDARY, for real, at the shipped width. Every slot is free, so each create takes slot 0 (handle & 0xFF ==
// 1) until slot 0 retires; then the next create is slot 1's. Reproduced before the fix: the create after 16 777 214
// cycles answered the first handle again, and destroying it through a stale copy destroyed the new session.
const seen = [];
let first = 0, last = 0, cycles = 0, allOk = true;
// BOUNDED: a slot that wraps instead of retiring would take slot 0 for ever, and a regression must fail, not hang.
while (cycles < SLOT_GENERATIONS + 2)
{
    if (create(out) !== STATUS.OK) { allOk = false; break; }
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
ok(configVersion === configConstant, 'compiled config matches the page build constant');
M._free(halves);
console.log(`session-check: config version ${configVersion}`);

// Named commands and transferable buffers through the shipped facade.
const encoder = new TextEncoder(), decoder = new TextDecoder();
const heapBytes = () => new Uint8Array(M.HEAPU32.buffer);
const input = text => { const bytes = encoder.encode(text); const ptr = M._malloc(Math.max(1, bytes.length)); heapBytes().set(bytes, ptr); return [ptr, bytes.length]; };
const answer = M._malloc(macro('FC_SESSION_ANSWER_BYTES'));
const resultSize = M._malloc(12);
const reply = () => {
    const n = M.HEAPU32[resultSize >>> 2];
    const value = JSON.parse(decoder.decode(heapBytes().slice(answer, answer + n)));
    ok(accepts(value, 'CommandAnswer'), 'answer agrees with generated union'); return value;
};
const cmd = (h, object) => {
    ok(accepts(object, 'SessionCommand'), 'command agrees with generated union');
    const [p, n] = input(JSON.stringify(object));
    ok(M._fc_session_command(h, p, n, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK, 'command call');
    M._free(p); return reply();
};
const demand = M._malloc(8);
ok(M._fc_session_create_bytes(caps, demand) === STATUS.OK && new DataView(M.HEAPU32.buffer).getFloat64(demand, true) > 0,
   'create demand published before create');
ok(M._fc_session_create(caps, configLow ^ 1, configHigh, resultSize) === STATUS.ERR_CONFIG_VERSION, 'page config mismatch refused');
ok(create(resultSize) === STATUS.OK, 'smoke session created');
const session = M.HEAPU32[resultSize >>> 2];
const readTransfer = what => {
    M.HEAPU32[resultSize >>> 2] = 12;
    ok(M[`_fc_session_${what}_size`](session, resultSize) === STATUS.OK, `${what} size`);
    const jsonBytes = M.HEAPU32[(resultSize >>> 2) + 1], rowBytes = M.HEAPU32[(resultSize >>> 2) + 2];
    const j = M._malloc(jsonBytes), r = rowBytes ? M._malloc(rowBytes) : 0;
    ok(M[`_fc_session_${what}_copy`](session, j, jsonBytes, r, rowBytes) === STATUS.OK, `${what} copy`);
    const json = JSON.parse(decoder.decode(heapBytes().slice(j, j + jsonBytes)));
    const binary = rowBytes ? heapBytes().slice(r, r + rowBytes).buffer : new ArrayBuffer(0);
    const viewRows = row => {
        const values = new Float64Array(binary, row.byteOffset, row.length * row.stride);
        ok(values.length === row.length * row.stride, 'rows read as Float64Array'); return values;
    };
    if (what === 'snapshot') {
        ok(accepts(json, 'SessionSnapshot'), 'snapshot decoded with generated types');
        if (json.eqCurve.length) {
            const curve = new Float64Array(heapBytes().buffer, r + json.eqCurve.byteOffset, json.eqCurve.length * json.eqCurve.stride);
            ok(curve.length === 256 && curve[0] === 20 && Number.isFinite(curve[1]), 'summed EQ curve transfers Hz/dB points');
        }
        for (const key of ['momentary', 'shortTerm', 'runs', 'machineDifferences']) viewRows(json[key]);
        if (json.machineDifferences.length) {
            const difference = viewRows(json.machineDifferences);
            ok(difference.length >= 4 && difference[0] === 0 && difference[1] === 1 && difference[2] === 33, 'nonempty facade rows decode file value 33 as Float64Array');
        }
    } else {
        ok(accepts(json, 'ReadonlyArray<SessionEvent>'), 'event batch decoded with generated tagged union');
        for (const event of json) if (event.kind === 'reading') for (const key of ['momentary', 'shortTerm', 'runs']) viewRows(event.payload[key]);
    }
    M._free(j); if (r) M._free(r); return json;
};
const pcm = M._malloc(48000 * 4), pointers = M._malloc(4);
for (let i = 0; i < 48000; ++i) new Float32Array(M.HEAPU32.buffer)[(pcm >>> 2) + i] = (i % 32 - 16) / 64;
M.HEAPU32[pointers >>> 2] = pcm;
const [meta, metaBytes] = input(JSON.stringify({name:'synthetic', fileRate:48000, bitDepth:24, rateKnown:true}));
ok(M._fc_session_load(session, 1, 0, pointers, 1, 48000, 48001, meta, metaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK
    && reply().code === 29, 'above maximum rate refused');
ok(M._fc_session_load(session, 1, 0, pointers, 1, 48000, 48000, meta, metaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK
    && reply().kind === 'accepted', 'synthetic PCM loaded');
M._free(meta); M._free(pointers); M._free(pcm);
let steps = 0, done = false, phases = 0;
while (!done && steps < 1500) {
    ok(M._fc_session_step(session, 1, resultSize) === STATUS.OK, 'small work-unit budget');
    done = M.HEAPU32[resultSize >>> 2] === 1; ++steps;
    for (const e of readTransfer('events')) if (e.kind === 'phase') ++phases;
}
ok(done && steps > 10 && phases > 10, 'pump reaches done after bounded preparation, streaming, and finalization');
const snapshot = readTransfer('snapshot');
ok(snapshot.state === 2 && snapshot.sourceBytes === 192000 && typeof snapshot.integratedLufs === 'number', 'owned measured snapshot and explicit NaN');
const queryRequest = {kind:0, audioId:snapshot.source.hash, fromFrame:'0', toFrame:'48000', columns:7, requestId:'9007199254740993'};
const queryEncoded = new TextEncoder().encode(JSON.stringify(queryRequest));
const queryInput = M._malloc(queryEncoded.length);
new Uint8Array(M.HEAPU32.buffer, queryInput, queryEncoded.length).set(queryEncoded);
const querySizes = M._malloc(12), queryWritten = M._malloc(12);
M.HEAPU32[querySizes >>> 2] = 12; M.HEAPU32[queryWritten >>> 2] = 12;
ok(M._fc_session_query_size(session, queryInput, queryEncoded.length, querySizes) === STATUS.OK, 'query capacity before execution');
const queryJsonBytes = M.HEAPU32[(querySizes + 4) >>> 2], queryRowBytes = M.HEAPU32[(querySizes + 8) >>> 2];
const queryJson = M._malloc(queryJsonBytes), queryRows = M._malloc(queryRowBytes);
for (let repeat = 0; repeat < 2; ++repeat) {
    ok(M._fc_session_query_copy(session, queryInput, queryEncoded.length, queryJson, queryJsonBytes, queryRows, queryRowBytes, queryWritten) === STATUS.OK, 'owned waveform query');
    const bytes = M.HEAPU32[(queryWritten + 4) >>> 2];
    const response = JSON.parse(new TextDecoder().decode(new Uint8Array(M.HEAPU32.buffer, queryJson, bytes)));
    ok(accepts(response, 'QueryResponse') && response.request.requestId === queryRequest.requestId && response.audioId === queryRequest.audioId, 'generated query metadata and exact request ID');
    ok(response.status === 0 && response.stored === '28' && response.cacheHit === (repeat === 1), 'overview and repeated demand cache');
    const values = new Float64Array(M.HEAPU32.buffer, queryRows, response.values.length);
    ok(Number.isNaN(values[16]) && values[25] === 3, 'mono right axis is absent with a reason');
}
M.HEAPU32[querySizes >>> 2] = 12;
ok(M._fc_session_summary_size(session, querySizes) === STATUS.OK && M.HEAPU32[(querySizes + 8) >>> 2] === 0, 'summary omits large measurement rows');
for (const p of [queryInput, querySizes, queryWritten, queryJson, queryRows]) M._free(p);
ok(cmd(session, {kind:'editDevice', commandId:'2', device:7, fields:{on:true, db:1.25}}).code === 3, 'device edits wait for placement');
ok(cmd(session, {kind:'setManual', commandId:'2', on:true}).kind === 'accepted', 'manual command');
ok(M._fc_session_export_project_size(session, resultSize) === STATUS.ERR_NOT_PLACED, 'unplaced defaults cannot be exported');
const crest = snapshot.measurements.find(r => r.analyzer === 9);
ok(crest?.arrays.some(a => a.name === 'blocks' && a.stored > 0), 'shipped facade transfers owned source crest rows');
ok(cmd(session, {kind:'setTarget', commandId:'5', target:'lp'}).kind === 'accepted', 'target command needs only a target');
const resetSnapshot = readTransfer('snapshot');
ok(resetSnapshot.handFieldCount === 0 && resetSnapshot.measurementJob === 0, 'target changes preserve completed source measurements');
const past = M.HEAPU32.buffer.byteLength;
for (const what of ['events', 'snapshot']) {
    ok(M[`_fc_session_${what}_size`](session, past - 4) === STATUS.ERR_SPAN, `${what} size output crosses heap`);
    ok(M[`_fc_session_${what}_copy`](session, past - 1, 2, 0, 0) === STATUS.ERR_SPAN, `${what} JSON output crosses heap`);
    ok(M[`_fc_session_${what}_copy`](session, answer, 1, past, 8) === STATUS.ERR_SPAN, `${what} row output crosses heap`);
}
ok(M._fc_session_command(session, past, 1, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.ERR_SPAN, 'command input crosses heap');
ok(M._fc_session_import_project(session, 0, 0, past, 1, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.ERR_SPAN, 'project input crosses heap');
ok(M._fc_session_load(session, 0, 0, past, 1, 1, 48000, 0, 0, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.ERR_SPAN, 'PCM pointer table crosses heap');
ok(M._fc_session_export_project_size(session, past) === STATUS.ERR_SPAN && M._fc_session_step(session, 1, past) === STATUS.ERR_SPAN, 'project and step output spans');
ok(M._fc_session_create_bytes(caps, past - 4) === STATUS.ERR_ALIGNMENT, 'demand output alignment precedes span');
ok(M._fc_session_create_bytes(caps, past) === STATUS.ERR_SPAN, 'demand output leaves heap');
ok(M._fc_session_create_bytes(past - 8, demand) === STATUS.ERR_SPAN, 'capability input crosses heap');
ok(M._fc_session_create(past - 8, configLow, configHigh, resultSize) === STATUS.ERR_SPAN, 'create capability input crosses heap');
const answerCalls = [
    (output, written) => M._fc_session_command(session, 0, 0, output, 8, written),
    (output, written) => M._fc_session_load(session, 0, 0, 0, 0, 0, 0, 0, 0, output, 8, written),
    (output, written) => M._fc_session_import_project(session, 0, 0, 0, 0, output, 8, written),
    (output, written) => M._fc_session_export_project_copy(session, output, 8, written),
];
for (const call of answerCalls) {
    ok(call(past - 4, 0) === STATUS.ERR_SPAN, 'first output span checked before null second output');
    ok(call(answer, past) === STATUS.ERR_SPAN, 'written output leaves heap');
    ok(call(answer, resultSize + 1) === STATUS.ERR_ALIGNMENT, 'written output alignment');
}
const table = M._malloc(4); M.HEAPU32[table >>> 2] = M.HEAPU32.buffer.byteLength - 4;
ok(M._fc_session_load(session, 0, 0, table, 1, 2, 48000, 0, 0, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.ERR_SPAN,
   'a channel sample span crosses heap');
M._free(table);
ok(M._fc_session_destroy(session) === STATUS.OK, 'smoke session destroyed');
for (const p of [caps, answer, resultSize, demand]) M._free(p);
console.log(`session-check: fcsession v${version} — ${checks} checks, ${bad} failures`);
process.exit(bad ? 1 : 0);
