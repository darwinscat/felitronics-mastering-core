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
import { layoutOf, sizeOf, FC_MASTER_ABI_VERSION } from './fc-master-layout.mjs';
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
        '_fc_session_query_copy', '_fc_session_summary_size', '_fc_session_summary_copy',
        '_fc_session_load_measured_bytes', '_fc_session_load_measured',
        '_fc_session_attach_audio_bytes', '_fc_session_attach_audio',
        '_fc_session_master_bytes', '_fc_session_master', '_fc_session_master_audio_size',
        '_fc_session_master_audio_copy', '_fc_session_master_audio_release', '_fc_session_master_audio_view',
        '_fc_session_master_waveform_chunk_bytes', '_fc_session_master_waveform_chunk_size',
        '_fc_session_master_waveform_chunk_copy'],
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
ok(snapshot.state === 3 && snapshot.sourceBytes === 192000 && typeof snapshot.integratedLufs === 'number', 'owned phase-two snapshot and explicit NaN');
ok(snapshot.tempoChoice.ready && !snapshot.tempoChoice.measured && snapshot.tempoChoice.bpm === 120
    && snapshot.measurements.find(r => r.analyzer === 11)?.status === 2,
    'short optional tempo gives a terminal cause and a separate device fallback');
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
// A rejected overlapping attachment must leave both outputs and the sidecar unchanged.
let hash = 0xcbf29ce484222325n;
for (const byte of [128, 187, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    hash = ((hash ^ BigInt(byte)) * 0x100000001b3n) & 0xffffffffffffffffn;
const [factsPtr, factsBytes] = input(JSON.stringify({name:'one', sourceHash:hash.toString(), frames:'1',
    sampleRate:48000, channels:1, fileRate:48000, bitDepth:16, rateKnown:true, integratedLufs:-14, truePeakDb:-0.5}));
ok(M._fc_session_load_measured(session, 41, 0, factsPtr, factsBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK
    && reply().kind === 'accepted', 'one-frame sidecar loaded through wasm facade');
M._free(factsPtr);
const sidecarBefore = readTransfer('snapshot');
const onePcm = M._malloc(4), oneTable = M._malloc(4);
new Float32Array(M.HEAPU32.buffer)[onePcm >>> 2] = 0;
M.HEAPU32[oneTable >>> 2] = onePcm;
heapBytes().fill(0x5a, answer, answer + macro('FC_SESSION_ANSWER_BYTES'));
const overlapBefore = heapBytes().slice(answer, answer + macro('FC_SESSION_ANSWER_BYTES'));
ok(M._fc_session_attach_audio(session, 42, 0, oneTable, 1, 1, 48000,
    answer, macro('FC_SESSION_ANSWER_BYTES'), answer + 4) === STATUS.ERR_OVERLAP,
   'attachment refuses overlapping answer and written outputs');
ok(Buffer.from(heapBytes().slice(answer, answer + macro('FC_SESSION_ANSWER_BYTES'))).equals(Buffer.from(overlapBefore)),
   'overlap refusal leaves output bytes unchanged');
ok(JSON.stringify(readTransfer('snapshot')) === JSON.stringify(sidecarBefore),
   'overlap refusal leaves the full sidecar snapshot unchanged');
M._free(oneTable); M._free(onePcm);

// A versioned ready input reaches the real session renderer. Copy once from its scoped heap view
// after a memory growth, then transfer only the independent ArrayBuffer.
ok(create(resultSize) === STATUS.OK, 'master smoke session created');
const masterSession = M.HEAPU32[resultSize >>> 2];
const masterFrames = 96000, masterSamples = masterFrames * 2;
const masterPcm = M._malloc(masterSamples * 4), masterPointers = M._malloc(8);
const pcmWords = new Float32Array(M.HEAPU32.buffer);
for (let i = 0; i < masterFrames; ++i) {
    pcmWords[(masterPcm >>> 2) + i] = (i * 17 % 251 - 125) / 4096;
    pcmWords[(masterPcm >>> 2) + masterFrames + i] = (i * 19 + 7) % 251 / 4096 - 125 / 4096;
}
M.HEAPU32[masterPointers >>> 2] = masterPcm;
M.HEAPU32[(masterPointers >>> 2) + 1] = masterPcm + masterFrames * 4;
const [masterMeta, masterMetaBytes] = input(JSON.stringify({name:'ready.wav', fileRate:48000, bitDepth:24, rateKnown:true}));
ok(M._fc_session_load(masterSession, 1, 0, masterPointers, 2, masterFrames, 48000,
    masterMeta, masterMetaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK,
   'master smoke source loaded');
M._free(masterMeta); M._free(masterPointers); M._free(masterPcm);
const masterSnapshot = () => {
    M.HEAPU32[resultSize >>> 2] = 12;
    if (M._fc_session_snapshot_size(masterSession, resultSize) !== STATUS.OK) return null;
    const jsonBytes = M.HEAPU32[(resultSize >>> 2) + 1], rowBytes = M.HEAPU32[(resultSize >>> 2) + 2];
    const json = M._malloc(jsonBytes), rows = rowBytes ? M._malloc(rowBytes) : 0;
    const st = M._fc_session_snapshot_copy(masterSession, json, jsonBytes, rows, rowBytes);
    const value = st === STATUS.OK ? JSON.parse(decoder.decode(heapBytes().slice(json, json + jsonBytes))) : null;
    M._free(json); if (rows) M._free(rows);
    return value;
};
let readySnapshot = null;
for (let i = 0; i < 20000; ++i) {
    if (i % 32 === 0 && (readySnapshot = masterSnapshot())?.canMaster) break;
    if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK) break;
}
ok(readySnapshot?.canMaster === true, 'mandatory readings make the ready command available');
const sourceId = BigInt(readySnapshot?.source?.hash ?? '0');
const revision = BigInt(readySnapshot?.revision ?? '0');
const lo = n => Number(n & 0xffffffffn), hi = n => Number(n >> 32n);
const configBytes = sizeOf('fc_master_config'), paramsBytes = sizeOf('fc_master_params');
const masterConfig = M._malloc(configBytes), masterParams = M._malloc(paramsBytes);
heapBytes().fill(0, masterConfig, masterConfig + configBytes);
heapBytes().fill(0, masterParams, masterParams + paramsBytes);
const setMaster = (ptr, name, field, value, kind = 'i32') => {
    const at = ptr + layoutOf(name).fields.get(field).offset;
    const dv = new DataView(M.HEAPU32.buffer);
    if (kind === 'f64') dv.setFloat64(at, value, true); else dv.setInt32(at, value, true);
};
new DataView(M.HEAPU32.buffer).setUint32(masterConfig, FC_MASTER_ABI_VERSION, true);
new DataView(M.HEAPU32.buffer).setUint32(masterConfig + 4, configBytes, true);
new DataView(M.HEAPU32.buffer).setUint32(masterParams, FC_MASTER_ABI_VERSION, true);
new DataView(M.HEAPU32.buffer).setUint32(masterParams + 4, paramsBytes, true);
for (const [field, value, kind] of [['sampleRate',48000,'f64'],['channels',2],['internalBlock',256],
    ['oversampleFactor',4],['tapsPerPhase',64],['limiter',1],['limiterLookaheadMs',1,'f64']])
    setMaster(masterConfig, 'fc_master_config', field, value, kind);
setMaster(masterParams, 'fc_master_params', 'compressorMix', 1, 'f64');
const masterDemand = M._malloc(32);
M.HEAPU32[masterDemand >>> 2] = 32;
ok(M._fc_session_master_bytes(masterSession, lo(sourceId + 1n), hi(sourceId + 1n), lo(revision), hi(revision),
    masterConfig, masterParams, masterDemand) === STATUS.ERR_STALE, 'stale source is rejected before ready preflight');
ok(M._fc_session_master_bytes(masterSession, lo(sourceId), hi(sourceId), lo(revision), hi(revision),
    masterConfig, masterParams, masterDemand) === STATUS.OK && M.HEAPU32[(masterDemand >>> 2) + 1] === 0,
   'ready command has a valid declared demand');
ok(M._fc_session_master(masterSession, 2, 0, lo(sourceId), hi(sourceId), lo(revision), hi(revision),
    masterConfig, masterParams, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK
    && reply().kind === 'accepted', 'versioned ready input starts a real master');
let completeSnapshot = null;
for (let i = 0; i < 40000; ++i) {
    if (i % 16 === 0 && (completeSnapshot = masterSnapshot())?.pendingMaster?.master) break;
    if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK) break;
}
ok(completeSnapshot?.pendingMaster?.master > 0, 'real master owns transferable PCM');
const masterToken = M._malloc(28), tokenValue = completeSnapshot.pendingMaster;
const tokenSource = BigInt(tokenValue.source), tokenRevision = BigInt(tokenValue.revision);
for (const [i, value] of [28, lo(tokenSource), hi(tokenSource), lo(tokenRevision), hi(tokenRevision),
                           tokenValue.job, tokenValue.master].entries()) M.HEAPU32[(masterToken >>> 2) + i] = value;
const audioBytes = M._malloc(8), audioFrames = M._malloc(4), audioChannels = M._malloc(4), audioRate = M._malloc(4);
ok(M._fc_session_master_audio_size(masterSession, masterToken, audioBytes, audioFrames, audioChannels, audioRate) === STATUS.OK
    && M.HEAPU32[audioFrames >>> 2] === masterFrames && M.HEAPU32[audioChannels >>> 2] === 2,
   'master shape is available before transfer');
const beforeGrow = M.HEAPU32.buffer, growth = [];
for (let i = 0; i < 12 && M.HEAPU32.buffer === beforeGrow; ++i) growth.push(M._malloc(16 * 1024 * 1024));
ok(M.HEAPU32.buffer !== beforeGrow, 'heap grew before the scoped PCM view');
const viewOut = M._malloc(4), sampleOut = M._malloc(4);
ok(M._fc_session_master_audio_view(masterSession, masterToken, viewOut, sampleOut) === STATUS.OK,
   'scoped PCM view returned after growth');
const pcmAddress = M.HEAPU32[viewOut >>> 2], sampleCount = M.HEAPU32[sampleOut >>> 2];
const independent = new Float32Array(M.HEAPU32.buffer, pcmAddress, sampleCount).slice().buffer;
ok(independent !== M.HEAPU32.buffer && independent.byteLength === masterSamples * 4,
   'one copy creates an independent transferable ArrayBuffer');
ok(M._fc_session_master_audio_release(masterSession, masterToken) === STATUS.OK
    && M._fc_session_master_audio_release(masterSession, masterToken) === STATUS.ERR_STALE,
   'release frees the session PCM once');
const waveRequestBytes = new TextEncoder().encode(JSON.stringify({kind:9, audioId:sourceId.toString(),
    fromFrame:'100', toFrame:'200', columns:10, requestId:'3', crossoverHz:120, fromHz:20,
    toHz:250, masterId:tokenValue.master}));
const waveRequest = M._malloc(waveRequestBytes.length);
heapBytes().set(waveRequestBytes, waveRequest);
const waveSizes = M._malloc(12), waveDemand = M._malloc(32);
M.HEAPU32[waveSizes >>> 2] = 12; M.HEAPU32[waveDemand >>> 2] = 32;
ok(M._fc_session_master_waveform_chunk_bytes(masterSession, waveRequest, waveRequestBytes.length,
    2, 100, 48000, waveDemand) === STATUS.OK
    && M._fc_session_master_waveform_chunk_size(masterSession, waveRequest, waveRequestBytes.length,
        2, 100, 48000, waveSizes) === STATUS.OK, 'external master waveform chunk is priced before PCM reads');
const chunkLeft = M._malloc(400), chunkRight = M._malloc(400), chunkTable = M._malloc(8);
const transferred = new Float32Array(independent);
new Float32Array(M.HEAPU32.buffer, chunkLeft, 100).set(transferred.subarray(100, 200));
new Float32Array(M.HEAPU32.buffer, chunkRight, 100).set(transferred.subarray(masterFrames + 100, masterFrames + 200));
M.HEAPU32[chunkTable >>> 2] = chunkLeft; M.HEAPU32[(chunkTable >>> 2) + 1] = chunkRight;
const waveJsonCapacity = M.HEAPU32[(waveSizes >>> 2) + 1], waveRowCapacity = M.HEAPU32[(waveSizes >>> 2) + 2];
const waveJson = M._malloc(waveJsonCapacity), waveRows = M._malloc(waveRowCapacity), waveWritten = M._malloc(12);
M.HEAPU32[waveWritten >>> 2] = 12;
ok(M._fc_session_master_waveform_chunk_copy(masterSession, waveRequest, waveRequestBytes.length,
    chunkTable, 2, 100, 48000, waveJson, waveJsonCapacity, waveRows, waveRowCapacity, waveWritten) === STATUS.OK
    && new Float64Array(M.HEAPU32.buffer, waveRows, waveRowCapacity / 8)[0] === 100
    && new Float64Array(M.HEAPU32.buffer, waveRows, waveRowCapacity / 8)[1] === 110,
   'wasm deep zoom reads an explicit PCM chunk after master release');
let audioHash = 0xcbf29ce484222325n;
for (const bits of new Uint32Array(independent)) for (let shift = 0; shift < 32; shift += 8)
    audioHash = ((audioHash ^ BigInt((bits >>> shift) & 255)) * 0x100000001b3n) & 0xffffffffffffffffn;
console.log(`session-master-audio=${audioHash.toString(16).padStart(16, '0')}`);
ok(audioHash === 0xb495c72924fd8746n, 'ready master PCM matches the native reference bit for bit');
for (const p of growth) M._free(p);
for (const p of [masterConfig, masterParams, masterDemand, masterToken, audioBytes, audioFrames, audioChannels, audioRate,
                 viewOut, sampleOut]) M._free(p);
ok(M._fc_session_destroy(masterSession) === STATUS.OK, 'master smoke session destroyed');
ok(M._fc_session_destroy(session) === STATUS.OK, 'smoke session destroyed');
for (const p of [caps, answer, resultSize, demand]) M._free(p);
console.log(`session-check: fcsession v${version} — ${checks} checks, ${bad} failures`);
process.exit(bad ? 1 : 0);
