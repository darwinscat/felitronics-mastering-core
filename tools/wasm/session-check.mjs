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
import { createHash } from 'node:crypto';
import { gzipSync, gunzipSync } from 'node:zlib';
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
        '_fc_session_master_wav_size', '_fc_session_master_wav_copy',
        '_fc_session_master_waveform_chunk_bytes', '_fc_session_master_waveform_chunk_size',
        '_fc_session_master_waveform_chunk_copy'],
};
// Version 2 (v0.4.0) appends a capabilities field, query kinds and snapshot fields, and no entry point.
SURFACE[2] = SURFACE[1];
// Version 3 (v0.5.0) appends the saturation type, the clipper shapes 4-7 and the plan's facts, and no entry point.
SURFACE[3] = SURFACE[1];
// Version 4 (v0.6.0, the ABI manifest's new base) adds the pure kit's entry points, the EQ bands' curve among them.
SURFACE[4] = [...SURFACE[3], '_fc_kit_text', '_fc_kit_parse', '_fc_kit_travel', '_fc_kit_position', '_fc_kit_value_at',
    '_fc_kit_heat', '_fc_kit_mono_zones', '_fc_kit_mono_zones_at', '_fc_kit_eq_curve', '_fc_kit_low_end_curve',
    '_fc_kit_eq_curve_bands'];
// Version 5 (slice 5) appends the rejected answer's fact and the target field's clear, and no entry point.
SURFACE[5] = SURFACE[4];
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
// fc_session_capabilities: 40 bytes, its base at v0.6.0 — leanSummary (at 32) included and 0.
const caps = M._malloc(40);
new DataView(M.HEAPU32.buffer).setFloat64(caps + 8, 256 * 1024 * 1024, true);
M.HEAPU32[caps >>> 2] = 40;
M.HEAPU32[(caps + 32) >>> 2] = 0;
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
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
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
ok(M._fc_session_summary_size(session, querySizes) === STATUS.OK && M.HEAPU32[(querySizes + 8) >>> 2] === 128 * 16,
   'summary omits large measurement rows: its only rows are the placed devices\' EQ curve');
for (const p of [queryInput, querySizes, queryWritten, queryJson, queryRows]) M._free(p);
ok(snapshot.devicesPlaced === true && snapshot.plan.readOnly === false && snapshot.plan.waiting === 0,
   'the real measurement ends with the devices placed by the planner, nothing left to wait for');
ok(cmd(session, {kind:'editDevice', commandId:'2', device:7, fields:{on:true, db:1.25}}).kind === 'accepted', 'the placed devices take an edit');
ok(cmd(session, {kind:'setManual', commandId:'2', on:true}).kind === 'accepted', 'manual command');
ok(M._fc_session_export_project_size(session, resultSize) === STATUS.OK, 'the placed project is exportable');
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
let inputDigest = 0xcbf29ce484222325n;
for (const byte of heapBytes().subarray(masterPcm, masterPcm + masterSamples * 4))
    inputDigest = BigInt.asUintN(64, (inputDigest ^ BigInt(byte)) * 0x100000001b3n);
console.log(`session-master-input=${inputDigest.toString(16).padStart(16, '0')}`);
M.HEAPU32[masterPointers >>> 2] = masterPcm;
M.HEAPU32[(masterPointers >>> 2) + 1] = masterPcm + masterFrames * 4;
const [masterMeta, masterMetaBytes] = input(JSON.stringify({name:'ready.wav', fileRate:48000, bitDepth:24, rateKnown:true}));
const masterPcmSha256 = sha256(heapBytes().slice(masterPcm, masterPcm + masterSamples * 4));
const masterMetaJson = decoder.decode(heapBytes().slice(masterMeta, masterMeta + masterMetaBytes));
ok(M._fc_session_load(masterSession, 1, 0, masterPointers, 2, masterFrames, 48000,
    masterMeta, masterMetaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK,
   'master smoke source loaded');
const masterLoadAnswer = reply();
M._free(masterMeta); M._free(masterPointers); M._free(masterPcm);
const masterWire = (what, handle = masterSession) => {
    M.HEAPU32[resultSize >>> 2] = 12;
    if (M[`_fc_session_${what}_size`](handle, resultSize) !== STATUS.OK) return null;
    const jsonBytes = M.HEAPU32[(resultSize >>> 2) + 1], rowBytes = M.HEAPU32[(resultSize >>> 2) + 2];
    const jsonPtr = M._malloc(jsonBytes), rowsPtr = rowBytes ? M._malloc(rowBytes) : 0;
    const st = M[`_fc_session_${what}_copy`](handle, jsonPtr, jsonBytes,
        rowsPtr, rowBytes);
    const json = st === STATUS.OK ? decoder.decode(heapBytes().slice(jsonPtr, jsonPtr + jsonBytes)) : '';
    const rows = st === STATUS.OK && rowBytes
        ? Buffer.from(heapBytes().slice(rowsPtr, rowsPtr + rowBytes)) : Buffer.alloc(0);
    M._free(jsonPtr); if (rowsPtr) M._free(rowsPtr);
    if (st !== STATUS.OK) return null;
    const value = JSON.parse(json);
    ok(accepts(value, what === 'snapshot' ? 'SessionSnapshot' : 'ReadonlyArray<SessionEvent>'),
       `recorded ${what} agrees with generated codec`);
    return {jsonGzipBase64:gzipSync(Buffer.from(json), {mtime:0}).toString('base64'),
        jsonSha256:sha256(Buffer.from(json)), jsonBytes:Buffer.byteLength(json),
        rowsGzipBase64:rows.length ? gzipSync(rows, {mtime:0}).toString('base64') : '',
        rowsSha256:sha256(rows), rowBytes:rows.length};
};
const wireValue = wire => JSON.parse(gunzipSync(Buffer.from(wire.jsonGzipBase64, 'base64')).toString());
const masterSnapshot = (handle = masterSession) => {
    M.HEAPU32[resultSize >>> 2] = 12;
    if (M._fc_session_snapshot_size(handle, resultSize) !== STATUS.OK) return null;
    const jsonBytes = M.HEAPU32[(resultSize >>> 2) + 1], rowBytes = M.HEAPU32[(resultSize >>> 2) + 2];
    const json = M._malloc(jsonBytes), rows = rowBytes ? M._malloc(rowBytes) : 0;
    const st = M._fc_session_snapshot_copy(handle, json, jsonBytes, rows, rowBytes);
    const value = st === STATUS.OK ? JSON.parse(decoder.decode(heapBytes().slice(json, json + jsonBytes))) : null;
    M._free(json); if (rows) M._free(rows);
    return value;
};
// Did the step just taken complete a master? Its own events say so (a `done` event is a master's), read into one
// reused buffer: a whole snapshot per unit step costs tens of milliseconds and grows with the retained rows.
const doneBytes = 1 << 16, doneJson = M._malloc(doneBytes), doneRows = M._malloc(doneBytes);
const masterCompleted = handle => {
    M.HEAPU32[resultSize >>> 2] = 12;
    if (M._fc_session_events_size(handle, resultSize) !== STATUS.OK) return false;
    const jsonBytes = M.HEAPU32[(resultSize >>> 2) + 1], rowBytes = M.HEAPU32[(resultSize >>> 2) + 2];
    if (jsonBytes > doneBytes || rowBytes > doneBytes
        || M._fc_session_events_copy(handle, doneJson, jsonBytes, doneRows, rowBytes) !== STATUS.OK) return false;
    return /"kind":"done","payload":\{"masterId":[1-9]/.test(decoder.decode(heapBytes().subarray(doneJson, doneJson + jsonBytes)));
};
let readySnapshot = null;
for (let i = 0; i < 20000; ++i) {
    if (i % 32 === 0 && (readySnapshot = masterSnapshot())?.canMaster) break;
    if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK) break;
}
ok(readySnapshot?.canMaster === true, 'mandatory readings make the ready command available');
const readyWire = masterWire('snapshot'), readyEvents = masterWire('events');
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
const masterInputs = (commandId, source, revision) => {
    const config = heapBytes().slice(masterConfig, masterConfig + configBytes);
    const params = heapBytes().slice(masterParams, masterParams + paramsBytes);
    const call = {commandId:String(commandId), source:String(source), revision:String(revision)};
    return {configBase64:Buffer.from(config).toString('base64'), configSha256:sha256(config),
        paramsBase64:Buffer.from(params).toString('base64'), paramsSha256:sha256(params),
        call, callSha256:sha256(encoder.encode(JSON.stringify(call)))};
};
const contractRecord = {format:2, source:{pcmSha256:masterPcmSha256, metadataJson:masterMetaJson,
    metadataSha256:sha256(encoder.encode(masterMetaJson)), frames:masterFrames, channels:2, rate:48000},
    scenarios:{}};
const masterDemand = M._malloc(32);
M.HEAPU32[masterDemand >>> 2] = 32;
ok(M._fc_session_master_bytes(masterSession, lo(sourceId + 1n), hi(sourceId + 1n), lo(revision), hi(revision),
    masterConfig, masterParams, masterDemand) === STATUS.ERR_STALE, 'stale source is rejected before ready preflight');
ok(M._fc_session_master_bytes(masterSession, lo(sourceId), hi(sourceId), lo(revision), hi(revision),
    masterConfig, masterParams, masterDemand) === STATUS.OK && M.HEAPU32[(masterDemand >>> 2) + 1] === 0,
   'ready command has a valid declared demand');
const safePrice = new DataView(M.HEAPU32.buffer);
const safeDemand = {bytes:safePrice.getFloat64(masterDemand + 8, true),
    largestBlockBytes:safePrice.getFloat64(masterDemand + 16, true),
    liveBytes:safePrice.getFloat64(masterDemand + 24, true)};
setMaster(masterParams, 'fc_master_params', 'inputGainDb', 100, 'f64');
const refusalInputs = masterInputs(30, sourceId, revision);
const refusalStatus = M._fc_session_master(masterSession, 30, 0, lo(sourceId), hi(sourceId), lo(revision), hi(revision),
    masterConfig, masterParams, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const refusalAnswer = refusalStatus === STATUS.OK ? reply() : null;
ok(refusalStatus === STATUS.OK && refusalAnswer?.kind === 'rejected',
   'finite but out-of-domain input gain returns a codec refusal before rendering');
contractRecord.scenarios.refusal = {inputs:refusalInputs, status:refusalStatus, answer:refusalAnswer,
    snapshot:masterWire('snapshot'), events:masterWire('events')};
setMaster(masterParams, 'fc_master_params', 'inputGainDb', 0, 'f64');
const safeInputs = masterInputs(2, sourceId, revision);
ok(M._fc_session_master(masterSession, 2, 0, lo(sourceId), hi(sourceId), lo(revision), hi(revision),
    masterConfig, masterParams, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK
    && reply().kind === 'accepted', 'versioned ready input starts a real master');
const safeAnswer = JSON.parse(decoder.decode(heapBytes().slice(answer, answer + M.HEAPU32[resultSize >>> 2])));
let completeSnapshot = null;
for (let i = 0; i < 40000; ++i) {
    if (i % 16 === 0 && (completeSnapshot = masterSnapshot())?.pendingMaster?.master) break;
    if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK) break;
}
ok(completeSnapshot?.pendingMaster?.master > 0, 'real master owns transferable PCM');
const completeWire = masterWire('snapshot'), completeEvents = masterWire('events');
const masterToken = M._malloc(28), tokenValue = completeSnapshot.pendingMaster;
const tokenSource = BigInt(tokenValue.source), tokenRevision = BigInt(tokenValue.revision);
for (const [i, value] of [28, lo(tokenSource), hi(tokenSource), lo(tokenRevision), hi(tokenRevision),
                           tokenValue.job, tokenValue.master].entries()) M.HEAPU32[(masterToken >>> 2) + i] = value;
const audioBytes = M._malloc(8), audioFrames = M._malloc(4), audioChannels = M._malloc(4), audioRate = M._malloc(4);
ok(M._fc_session_master_audio_size(masterSession, masterToken, audioBytes, audioFrames, audioChannels, audioRate) === STATUS.OK
    && M.HEAPU32[audioFrames >>> 2] === masterFrames && M.HEAPU32[audioChannels >>> 2] === 2,
   'master shape is available before transfer');
const beforeGrow = M.HEAPU32.buffer, beforeGrowBytes = beforeGrow.byteLength, growth = [];
for (let i = 0; i < 12 && M.HEAPU32.buffer === beforeGrow; ++i) growth.push(M._malloc(16 * 1024 * 1024));
ok(M.HEAPU32.buffer !== beforeGrow, 'heap grew before the scoped PCM view');
const observedHeap = {beforeGrowthBytes:beforeGrowBytes,
    afterGrowthBytes:M.HEAPU32.buffer.byteLength};
const viewOut = M._malloc(4), sampleOut = M._malloc(4);
ok(M._fc_session_master_audio_view(masterSession, masterToken, viewOut, sampleOut) === STATUS.OK,
   'scoped PCM view returned after growth');
const pcmAddress = M.HEAPU32[viewOut >>> 2], sampleCount = M.HEAPU32[sampleOut >>> 2];
const independent = new Float32Array(M.HEAPU32.buffer, pcmAddress, sampleCount).slice().buffer;
ok(independent !== M.HEAPU32.buffer && independent.byteLength === masterSamples * 4,
   'one copy creates an independent transferable ArrayBuffer');
const wavSize = M._malloc(8), wavBits = M._malloc(4), wavWritten = M._malloc(4);
const wavSized = M._fc_session_master_wav_size(masterSession, masterToken, wavSize, wavBits);
const wavLength = new DataView(M.HEAPU32.buffer).getFloat64(wavSize, true);
ok(wavSized === STATUS.OK && M.HEAPU32[wavBits >>> 2] === 24
    && wavLength === 44 + masterSamples * 3, 'WAV is sized before the session PCM is released');
const wavChunk = M._malloc(997), ownedWav = new Uint8Array(wavLength);
let wavCopied = true;
for (let at = 0; at < ownedWav.length; at += 997) {
    const count = Math.min(997, ownedWav.length - at);
    wavCopied &&= M._fc_session_master_wav_copy(masterSession, masterToken, at, 0,
        wavChunk, count, wavWritten) === STATUS.OK && M.HEAPU32[wavWritten >>> 2] === count;
    ownedWav.set(heapBytes().subarray(wavChunk, wavChunk + count), at);
}
const firstWav = ownedWav.slice(0, 59);
const wavRepeatStatus = M._fc_session_master_wav_copy(masterSession, masterToken, 0, 0,
    wavChunk, firstWav.length, wavWritten);
ok(wavCopied && wavRepeatStatus === STATUS.OK
    && firstWav.every((v, i) => v === heapBytes()[wavChunk + i]),
   'irregular WAV slices and a repeated slice have identical bytes');
const wavView = new DataView(ownedWav.buffer);
ok(new TextDecoder().decode(ownedWav.subarray(0, 4)) === 'RIFF'
    && wavView.getUint32(4, true) + 8 === ownedWav.length
    && wavView.getUint32(24, true) === 48000 && wavView.getUint16(34, true) === 24
    && wavView.getUint32(40, true) === masterSamples * 3,
   'downloaded WAV has the expected RIFF, rate, format and data length');
const listenedPcm = new Float32Array(independent);
let sameWavSamples = true;
for (let frame = 0; frame < masterFrames; ++frame) for (let channel = 0; channel < 2; ++channel) {
    const at = 44 + (frame * 2 + channel) * 3;
    const raw = ownedWav[at] | (ownedWav[at + 1] << 8) | (ownedWav[at + 2] << 16);
    const code = (raw & 0x800000) ? raw - 0x1000000 : raw;
    sameWavSamples &&= listenedPcm[channel * masterFrames + frame] === code / 8388608;
}
ok(sameWavSamples, 'decoded WAV equals the measured and listened PCM sample for sample');
let wavDigest = 0xcbf29ce484222325n;
for (const byte of ownedWav) wavDigest = BigInt.asUintN(64, (wavDigest ^ BigInt(byte)) * 0x100000001b3n);
console.log(`session-master-wav=${wavDigest.toString(16).padStart(16, '0')}`);
console.log(`session-master-wav-header=${Buffer.from(ownedWav.subarray(0, 44)).toString('hex')}`);
const retainedBefore = heapBytes().slice(pcmAddress, pcmAddress + 16);
const retainedSame = () => Buffer.from(heapBytes().subarray(pcmAddress, pcmAddress + 16)).equals(Buffer.from(retainedBefore));
new DataView(M.HEAPU32.buffer).setFloat64(wavSize, -1, true);
M.HEAPU32[wavBits >>> 2] = 99;
ok(M._fc_session_master_wav_size(masterSession, masterToken, pcmAddress, wavBits) === STATUS.ERR_OVERLAP
    && M.HEAPU32[wavBits >>> 2] === 99 && retainedSame(),
   'WAV size refuses a byte-count output inside retained PCM');
ok(M._fc_session_master_wav_size(masterSession, masterToken, wavSize, pcmAddress + 8) === STATUS.ERR_OVERLAP
    && new DataView(M.HEAPU32.buffer).getFloat64(wavSize, true) === -1 && retainedSame(),
   'WAV size refuses a bit-depth output inside retained PCM');
ok(M._fc_session_master_wav_copy(masterSession, masterToken, 0, 0, wavChunk, 64, pcmAddress + 12)
    === STATUS.ERR_OVERLAP && retainedSame(),
   'WAV copy refuses a written-count output inside retained PCM');
// EVERY OTHER OUTPUT IS FENCED AGAINST THE RETAINED PCM: a copy onto itself, the four shape numbers, a view's address
// and length, and a waveform chunk's JSON, rows and sizes.
ok(M._fc_session_master_audio_copy(masterSession, masterToken, pcmAddress, sampleCount) === STATUS.ERR_OVERLAP
    && M._fc_session_master_audio_copy(masterSession, masterToken, pcmAddress + 12, sampleCount - 3) === STATUS.ERR_OVERLAP
    && retainedSame(), 'PCM copy refuses an output inside the retained PCM it copies');
ok(M._fc_session_master_audio_size(masterSession, masterToken, pcmAddress, audioFrames, audioChannels, audioRate) === STATUS.ERR_OVERLAP
    && M._fc_session_master_audio_size(masterSession, masterToken, audioBytes, pcmAddress + 4, audioChannels, audioRate) === STATUS.ERR_OVERLAP
    && M._fc_session_master_audio_size(masterSession, masterToken, audioBytes, audioFrames, pcmAddress + 8, audioRate) === STATUS.ERR_OVERLAP
    && M._fc_session_master_audio_size(masterSession, masterToken, audioBytes, audioFrames, audioChannels, pcmAddress + 12) === STATUS.ERR_OVERLAP
    && retainedSame(), 'PCM shape refuses each of its four outputs inside the retained PCM');
ok(M._fc_session_master_audio_view(masterSession, masterToken, pcmAddress, sampleOut) === STATUS.ERR_OVERLAP
    && M._fc_session_master_audio_view(masterSession, masterToken, viewOut, pcmAddress + 4) === STATUS.ERR_OVERLAP
    && retainedSame(), 'a scoped view refuses an address or length output inside the retained PCM');
{
    const fenceRequestBytes = new TextEncoder().encode(JSON.stringify({kind:9, audioId:sourceId.toString(),
        fromFrame:'100', toFrame:'200', columns:10, requestId:'5', crossoverHz:120, fromHz:20,
        toHz:250, masterId:tokenValue.master}));
    const fenceRequest = M._malloc(fenceRequestBytes.length), fenceSizes = M._malloc(12), fenceWritten = M._malloc(12);
    heapBytes().set(fenceRequestBytes, fenceRequest);
    M.HEAPU32[fenceSizes >>> 2] = 12; M.HEAPU32[fenceWritten >>> 2] = 12;
    const fenceTable = M._malloc(8);
    M.HEAPU32[fenceTable >>> 2] = pcmAddress + 400; M.HEAPU32[(fenceTable >>> 2) + 1] = pcmAddress + masterFrames * 4 + 400;
    ok(M._fc_session_master_waveform_chunk_size(masterSession, fenceRequest, fenceRequestBytes.length,
        2, 100, 48000, fenceSizes) === STATUS.OK, 'a retained-PCM waveform chunk is sized');
    const jsonCapacity = M.HEAPU32[(fenceSizes >>> 2) + 1], rowCapacity = M.HEAPU32[(fenceSizes >>> 2) + 2];
    const fenceJson = M._malloc(jsonCapacity), fenceRows = M._malloc(rowCapacity);
    const chunk = (json, rows, written) => M._fc_session_master_waveform_chunk_copy(masterSession, fenceRequest,
        fenceRequestBytes.length, fenceTable, 2, 100, 48000, json, jsonCapacity, rows, rowCapacity, written);
    ok(chunk(pcmAddress, fenceRows, fenceWritten) === STATUS.ERR_OVERLAP
        && chunk(fenceJson, pcmAddress, fenceWritten) === STATUS.ERR_OVERLAP
        && chunk(fenceJson, fenceRows, pcmAddress + 16) !== STATUS.OK && retainedSame(),
       'a waveform chunk refuses JSON, rows or sizes inside the retained PCM');
    ok(chunk(fenceJson, fenceRows, fenceWritten) === STATUS.OK,
       'the retained PCM stays a valid waveform chunk input');
    for (const ptr of [fenceRequest, fenceSizes, fenceWritten, fenceTable, fenceJson, fenceRows]) M._free(ptr);
}
const releaseStatus = M._fc_session_master_audio_release(masterSession, masterToken);
const repeatReleaseStatus = M._fc_session_master_audio_release(masterSession, masterToken);
ok(releaseStatus === STATUS.OK && repeatReleaseStatus === STATUS.ERR_STALE,
   'release frees the session PCM once');
const staleSizeStatus = M._fc_session_master_wav_size(masterSession, masterToken, wavSize, wavBits);
ok(staleSizeStatus === STATUS.ERR_STALE
    && ownedWav.length === wavLength, 'owned WAV persists after the session PCM is released');
const releasedWire = masterWire('snapshot'), releasedEvents = masterWire('events');
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
ok(audioHash === 0xfaa62a730dd59b81n, 'ready master PCM matches the native reference bit for bit');
contractRecord.scenarios.safe = {inputs:safeInputs, loadAnswer:masterLoadAnswer, ready:readyWire,
    readyEvents, masterAnswer:safeAnswer, complete:completeWire, completeEvents,
    price:{sourcePcmBytes:masterSamples * 4, deliveredPcmBytes:masterSamples * 4,
        declared:safeDemand, observedHeap,
        browser:{playbackBytes:independent.byteLength, wavBytes:ownedWav.byteLength,
            copyChunkBytes:997}},
    export:{sizeStatus:wavSized, bits:24, bytes:wavLength, wavSha256:sha256(ownedWav),
        headerHex:Buffer.from(ownedWav.subarray(0, 44)).toString('hex'),
        firstSliceHex:Buffer.from(firstWav).toString('hex'), repeatStatus:wavRepeatStatus,
        repeatedSliceHex:Buffer.from(heapBytes().slice(wavChunk, wavChunk + firstWav.length)).toString('hex')},
    release:{status:releaseStatus, repeatStatus:repeatReleaseStatus, staleSizeStatus,
        snapshot:releasedWire, events:releasedEvents}};
const formatCases = [];
const ditherBits = masterParams + layoutOf('fc_master_params').fields.get('dither').offset
    + layoutOf('fc_dither').fields.get('bits').offset;
// THE DELIVERY FORMAT IS THE TARGET'S: deliveryRate and dither.bits of 0 take the target's rate (the source's 48 kHz
// when the target keeps it) and depth, and the same values restate them.
const deliveryCases = [['cd', 0, 0, 16, 44100], ['cd', 16, 44100, 16, 44100], ['cdDynamic', 0, 0, 16, 44100],
                       ['allStreaming', 0, 0, 24, 48000], ['spotify', 24, 48000, 24, 48000]];
for (const [caseIndex, [target, requested, requestedRate, bits, rate]] of deliveryCases.entries()) {
    const label = `${target} dither.bits ${requested} deliveryRate ${requestedRate}`;
    const targetCommand = {kind:'setTarget', commandId:String(80 + caseIndex), target};
    ok(cmd(masterSession, targetCommand).kind === 'accepted', `${label}: target selected`);
    new DataView(M.HEAPU32.buffer).setInt32(ditherBits, requested, true);
    setMaster(masterConfig, 'fc_master_config', 'deliveryRate', requestedRate, 'f64');
    const before = masterSnapshot();
    const version = BigInt(before.revision);
    const formatInputs = masterInputs(90 + caseIndex, sourceId, version);
    const started = M._fc_session_master(masterSession, 90 + caseIndex, 0, lo(sourceId), hi(sourceId),
        lo(version), hi(version), masterConfig, masterParams, answer,
        macro('FC_SESSION_ANSWER_BYTES'), resultSize);
    const accepted = started === STATUS.OK ? reply() : null;
    ok(started === STATUS.OK && accepted?.kind === 'accepted', `${label}: facade request accepted`);
    let finished = null;
    for (let i = 0; i < 40000; ++i) {
        if (i % 16 === 0 && (finished = masterSnapshot())?.pendingMaster?.master
            && finished.pendingMaster.master !== tokenValue.master) break;
        if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK) break;
    }
    ok(finished?.pendingMaster?.master > tokenValue.master, `${label}: facade master completes`);
    if (! finished?.pendingMaster?.master) continue;
    const formatWire = masterWire('snapshot'), formatEvents = masterWire('events');
    const t = finished.pendingMaster;
    const ts = BigInt(t.source), tr = BigInt(t.revision);
    for (const [i, value] of [28, lo(ts), hi(ts), lo(tr), hi(tr), t.job, t.master].entries())
        M.HEAPU32[(masterToken >>> 2) + i] = value;
    const shapeStatus = M._fc_session_master_audio_size(masterSession, masterToken, audioBytes, audioFrames,
        audioChannels, audioRate);
    const deliveredFrames = M.HEAPU32[audioFrames >>> 2], deliveredSamples = deliveredFrames * 2;
    const sizeStatus = M._fc_session_master_wav_size(masterSession, masterToken, wavSize, wavBits);
    const length = new DataView(M.HEAPU32.buffer).getFloat64(wavSize, true);
    const headStatus = M._fc_session_master_wav_copy(masterSession, masterToken, 0, 0, wavChunk, 44, wavWritten);
    const head = heapBytes().slice(wavChunk, wavChunk + 44);
    const view = new DataView(head.buffer);
    ok(shapeStatus === STATUS.OK && M.HEAPU32[audioRate >>> 2] === rate
        && (rate === 48000 ? deliveredFrames === masterFrames : deliveredFrames < masterFrames)
        && sizeStatus === STATUS.OK && M.HEAPU32[wavBits >>> 2] === bits
        && length === 44 + deliveredSamples * bits / 8 && headStatus === STATUS.OK
        && M.HEAPU32[wavWritten >>> 2] === 44 && view.getUint16(20, true) === 1 && view.getUint32(24, true) === rate
        && view.getUint16(34, true) === bits && view.getUint32(40, true) === deliveredSamples * bits / 8,
       `${label}: the WAV size and header carry the target's ${rate} Hz PCM${bits}`);
    ok(M._fc_session_master_audio_view(masterSession, masterToken, viewOut, sampleOut) === STATUS.OK,
       `${label}: view is available`);
    const ptr = M.HEAPU32[viewOut >>> 2];
    const sample = new Float32Array(M.HEAPU32.buffer, ptr, deliveredSamples);
    const count = 64;
    const payload = M._malloc(count * bits / 8);
    const copied = M._fc_session_master_wav_copy(masterSession, masterToken, 44, 0,
        payload, count * bits / 8, wavWritten) === STATUS.OK;
    const data = new DataView(M.HEAPU32.buffer, payload, count * bits / 8);
    let matches = copied;
    for (let i = 0; i < count; ++i) {
        const frame = Math.floor(i / 2), channel = i % 2;
        const expected = sample[channel * deliveredFrames + frame];
        let decoded;
        if (bits === 16) decoded = data.getInt16(i * 2, true) / 32768;
        else {
            const raw = data.getUint8(i * 3) | (data.getUint8(i * 3 + 1) << 8)
                | (data.getUint8(i * 3 + 2) << 16);
            decoded = (raw & 0x800000 ? raw - 0x1000000 : raw) / 8388608;
        }
        matches &&= Object.is(expected, decoded);
    }
    ok(matches, `${label}: exported samples equal retained measured PCM`);
    const file = new Uint8Array(length);
    let exported = true;
    for (let at = 0; at < file.length; at += 997) {
        const n = Math.min(997, file.length - at);
        exported &&= M._fc_session_master_wav_copy(masterSession, masterToken, at, 0,
            wavChunk, n, wavWritten) === STATUS.OK && M.HEAPU32[wavWritten >>> 2] === n;
        file.set(heapBytes().subarray(wavChunk, wavChunk + n), at);
    }
    const repeated = M._fc_session_master_wav_copy(masterSession, masterToken, 0, 0,
        wavChunk, 59, wavWritten);
    ok(exported && repeated === STATUS.OK
        && Buffer.from(file.subarray(0, 59)).equals(Buffer.from(heapBytes().subarray(wavChunk, wavChunk + 59))),
       `${label}: repeated WAV slice matches the complete download`);
    const released = M._fc_session_master_audio_release(masterSession, masterToken);
    const stale = M._fc_session_master_wav_size(masterSession, masterToken, wavSize, wavBits);
    ok(released === STATUS.OK && stale === STATUS.ERR_STALE, `${label}: release fences WAV sizing`);
    let formatDigest = 0xcbf29ce484222325n;
    for (const byte of file)
        formatDigest = BigInt.asUintN(64, (formatDigest ^ BigInt(byte)) * 0x100000001b3n);
    console.log(`session-master-${target}-dither${requested}-rate${requestedRate}-wav=${formatDigest.toString(16).padStart(16, '0')}`);
    formatCases.push({target, ditherBits:requested, deliveryRate:requestedRate, bits, rate, targetCommand,
        inputs:formatInputs, answer:accepted,
        complete:formatWire, events:formatEvents,
        export:{sizeStatus, bytes:length, bits, rate, headerHex:Buffer.from(head).toString('hex'),
            wavSha256:sha256(file), wavFnv64:formatDigest.toString(16).padStart(16, '0'), repeatStatus:repeated,
            firstSliceHex:Buffer.from(file.subarray(0, 59)).toString('hex')},
        release:{status:released, staleSizeStatus:stale, snapshot:masterWire('snapshot'),
            events:masterWire('events')}});
    M._free(payload);
}
contractRecord.scenarios.formats = formatCases;
// ...and any other depth or rate is an open refusal before anything is priced: DeliveryFormat (34) in the storage
// record and the answer, and the fact 134 naming the target's depth and rate among the events.
const refusalCases = [['allStreaming', 16, 0, 24, 48000], ['cd', 24, 0, 16, 44100], ['allStreaming', 32, 0, 24, 48000],
                      ['allStreaming', 20, 0, 24, 48000], ['cd', -1, 0, 16, 44100], ['cd', 272, 0, 16, 44100],
                      ['cd', 0, 48000, 16, 44100], ['allStreaming', 0, 44100, 24, 48000], ['cd', 0, 44100.5, 16, 44100]];
const formatRefusals = [];
for (const [caseIndex, [target, requested, requestedRate, depth, targetRate]] of refusalCases.entries()) {
    const label = `${target} dither.bits ${requested} deliveryRate ${requestedRate}`;
    const targetCommand = {kind:'setTarget', commandId:String(100 + caseIndex), target};
    ok(cmd(masterSession, targetCommand).kind === 'accepted', `${label}: target selected`);
    new DataView(M.HEAPU32.buffer).setInt32(ditherBits, requested, true);
    setMaster(masterConfig, 'fc_master_config', 'deliveryRate', requestedRate, 'f64');
    const version = BigInt(masterSnapshot().revision);
    const inputs = masterInputs(110 + caseIndex, sourceId, version);
    M.HEAPU32[masterDemand >>> 2] = 32;
    const pricedStatus = M._fc_session_master_bytes(masterSession, lo(sourceId), hi(sourceId), lo(version), hi(version),
        masterConfig, masterParams, masterDemand);
    const priced = {status:pricedStatus, rejection:M.HEAPU32[(masterDemand >>> 2) + 1],
        bytes:new DataView(M.HEAPU32.buffer).getFloat64(masterDemand + 8, true)};
    const started = M._fc_session_master(masterSession, 110 + caseIndex, 0, lo(sourceId), hi(sourceId),
        lo(version), hi(version), masterConfig, masterParams, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
    const refused = started === STATUS.OK ? reply() : null;
    const events = masterWire('events'), after = masterSnapshot();
    const fact = wireValue(events).find(e => e.kind === 'fact' && e.payload?.FactId === 134);
    ok(pricedStatus === STATUS.OK && priced.rejection === 34 && priced.bytes === 0
        && refused?.kind === 'rejected' && refused?.code === 34
        && fact?.payload?.args?.[0]?.integer === String(depth) && fact?.payload?.args?.[1]?.number === targetRate
        && after?.job === 0 && BigInt(after?.revision ?? '0') === version,
       `${label}: an open refusal before anything is priced, naming the target's ${depth}-bit PCM at ${targetRate} Hz`);
    formatRefusals.push({target, ditherBits:requested, deliveryRate:requestedRate, depth, targetRate, targetCommand,
        inputs, priced, answer:refused, events});
}
setMaster(masterConfig, 'fc_master_config', 'deliveryRate', 0, 'f64');
contractRecord.scenarios.formatRefusals = formatRefusals;
ok(cmd(masterSession, {kind:'setTarget', commandId:'120', target:'allStreaming'}).kind === 'accepted',
   'the default target is selected again for the scenarios that follow');
new DataView(M.HEAPU32.buffer).setInt32(ditherBits, 24, true);
const warmCycles = [], warmHeapBytes = [];
for (let i = 0; i < 3; ++i) {
    const commandId = 40 + i * 2;
    const current = masterSnapshot();
    const currentRevision = BigInt(current.revision);
    const inputs = masterInputs(commandId, sourceId, currentRevision);
    const started = M._fc_session_master(masterSession, commandId, 0, lo(sourceId), hi(sourceId),
        lo(currentRevision), hi(currentRevision), masterConfig, masterParams,
        answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
    const accepted = started === STATUS.OK ? reply() : null;
    let completed = null;
    for (let step = 0; step < 40000; ++step) {
        if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK || masterCompleted(masterSession)) break;
    }
    completed = masterSnapshot();
    const token = completed?.pendingMaster ?? {};
    const source = BigInt(token.source ?? '0'), revision = BigInt(token.revision ?? '0');
    for (const [index, value] of [28, lo(source), hi(source), lo(revision), hi(revision),
                                   token.job ?? 0, token.master ?? 0].entries())
        M.HEAPU32[(masterToken >>> 2) + index] = value;
    const released = M._fc_session_master_audio_release(masterSession, masterToken);
    const forgetCommand = {kind:'forget', commandId:String(commandId + 1), masterId:token.master};
    const forgotten = cmd(masterSession, forgetCommand);
    const after = masterWire('snapshot');
    warmHeapBytes.push(M.HEAPU32.buffer.byteLength);
    warmCycles.push({inputs, accepted, completed:token, releaseStatus:released,
        forgetCommand, forgetCommandSha256:sha256(encoder.encode(JSON.stringify(forgetCommand))),
        forgetAnswer:forgotten, after});
    ok(started === STATUS.OK && accepted?.kind === 'accepted' && token.master > 0
        && released === STATUS.OK && forgotten.kind === 'accepted'
        && wireValue(after).pendingMaster.master === 0,
       `warmed identical master ${i + 1} releases its PCM and retained rows`);
}
ok(warmHeapBytes[2] === warmHeapBytes[1],
   'identical warmed repeats reuse wasm heap without another high-water increase');
contractRecord.scenarios.warm = {cycles:warmCycles, observedHeapBytes:warmHeapBytes};
new DataView(M.HEAPU32.buffer).setInt32(ditherBits, 0, true);
const cancelRevision = BigInt(masterSnapshot().revision);
const cancelInputs = masterInputs(70, sourceId, cancelRevision);
const cancelStatus = M._fc_session_master(masterSession, 70, 0, lo(sourceId), hi(sourceId),
    lo(cancelRevision), hi(cancelRevision), masterConfig, masterParams, answer,
    macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const cancelStart = cancelStatus === STATUS.OK ? reply() : null;
ok(cancelStatus === STATUS.OK && cancelStart?.kind === 'accepted', 'cancel scenario starts a real master');
const cancelCommand = {kind:'cancel', commandId:'71', jobId:cancelStart?.jobId ?? 0};
const cancelAnswer = cmd(masterSession, cancelCommand);
const cancelledSnapshot = masterWire('snapshot'), cancelledEvents = masterWire('events');
ok(cancelAnswer.kind === 'accepted' && wireValue(cancelledSnapshot).pendingMaster.master === 0,
   'cancel scenario has no downloadable PCM');
contractRecord.scenarios.cancel = {inputs:cancelInputs, command:cancelCommand,
    commandSha256:sha256(encoder.encode(JSON.stringify(cancelCommand))),
    start:cancelStart, answer:cancelAnswer,
    snapshot:cancelledSnapshot, events:cancelledEvents};

const missTarget = {kind:'editTarget', commandId:'72', fields:{lufs:-5, tp:-6}};
const missTargetAnswer = cmd(masterSession, missTarget);
const missRevision = BigInt(masterSnapshot().revision);
const missInputs = masterInputs(73, sourceId, missRevision);
const missStatus = M._fc_session_master(masterSession, 73, 0, lo(sourceId), hi(sourceId),
    lo(missRevision), hi(missRevision), masterConfig, masterParams, answer,
    macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const missAnswer = missStatus === STATUS.OK ? reply() : null;
ok(missTargetAnswer.kind === 'accepted' && missStatus === STATUS.OK && missAnswer?.kind === 'accepted',
   'demanding target starts a search with twelve shared passes');
let missSnapshot = null;
for (let i = 0; i < 40000; ++i) {
    if (i % 16 === 0 && (missSnapshot = masterSnapshot())?.pendingMaster?.master) break;
    if (M._fc_session_step(masterSession, 16, resultSize) !== STATUS.OK) break;
}
const missLanding = missSnapshot?.masters?.at(-1)?.landing;
ok(missLanding?.status === 2 && missLanding.deliverable && missLanding.passes === 12
    && Number.isFinite(missLanding.missLu) && missLanding.truePeakDbTp <= -6,
   'safe miss records its actual loudness miss and true-peak measurement');
const missWire = masterWire('snapshot'), missEvents = masterWire('events');
let missExport = null;
if (missSnapshot?.pendingMaster?.master) {
    const t = missSnapshot.pendingMaster, ts = BigInt(t.source), tr = BigInt(t.revision);
    for (const [i, value] of [28, lo(ts), hi(ts), lo(tr), hi(tr), t.job, t.master].entries())
        M.HEAPU32[(masterToken >>> 2) + i] = value;
    const sized = M._fc_session_master_wav_size(masterSession, masterToken, wavSize, wavBits);
    const length = new DataView(M.HEAPU32.buffer).getFloat64(wavSize, true);
    const file = new Uint8Array(length);
    let copied = true;
    for (let at = 0; at < file.length; at += 997) {
        const n = Math.min(997, file.length - at);
        copied &&= M._fc_session_master_wav_copy(masterSession, masterToken, at, 0,
            wavChunk, n, wavWritten) === STATUS.OK && M.HEAPU32[wavWritten >>> 2] === n;
        file.set(heapBytes().subarray(wavChunk, wavChunk + n), at);
    }
    const repeat = M._fc_session_master_wav_copy(masterSession, masterToken, 0, 0, wavChunk, 59, wavWritten);
    const repeated = Buffer.from(heapBytes().slice(wavChunk, wavChunk + 59)).toString('hex');
    const release = M._fc_session_master_audio_release(masterSession, masterToken);
    const stale = M._fc_session_master_wav_size(masterSession, masterToken, wavSize, wavBits);
    ok(sized === STATUS.OK && copied && repeat === STATUS.OK
        && repeated === Buffer.from(file.subarray(0, 59)).toString('hex')
        && release === STATUS.OK && stale === STATUS.ERR_STALE,
       'safe miss exports, repeats and releases its measured WAV');
    missExport = {sizeStatus:sized, bytes:length, bits:M.HEAPU32[wavBits >>> 2],
        wavSha256:sha256(file), headerHex:Buffer.from(file.subarray(0, 44)).toString('hex'),
        firstSliceHex:repeated, repeatStatus:repeat, releaseStatus:release, staleSizeStatus:stale};
}
contractRecord.scenarios.miss = {inputs:missInputs, target:missTarget,
    targetSha256:sha256(encoder.encode(JSON.stringify(missTarget))), targetAnswer:missTargetAnswer,
    answer:missAnswer, snapshot:missWire, events:missEvents, measurements:missLanding,
    export:missExport, afterRelease:masterWire('snapshot')};
const snapshotFor = handle => masterSnapshot(handle);
ok(create(resultSize) === STATUS.OK, 'unavailable scenario session created');
const unavailableSession = M.HEAPU32[resultSize >>> 2];
const shortPcm = M._malloc(32), shortPointers = M._malloc(8);
const shortValues = [0, 0.25, -0.25, 0, 0, 0.25, -0.25, 0];
new Float32Array(M.HEAPU32.buffer, shortPcm, 8).set(shortValues);
M.HEAPU32[shortPointers >>> 2] = shortPcm;
M.HEAPU32[(shortPointers >>> 2) + 1] = shortPcm + 16;
const shortPcmSha256 = sha256(heapBytes().slice(shortPcm, shortPcm + 32));
const shortMetaJson = JSON.stringify({name:'short.wav', fileRate:48000, bitDepth:24, rateKnown:true});
const [shortMeta, shortMetaBytes] = input(shortMetaJson);
const shortLoadStatus = M._fc_session_load(unavailableSession, 1, 0, shortPointers, 2, 4, 48000,
    shortMeta, shortMetaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const shortLoadAnswer = shortLoadStatus === STATUS.OK ? reply() : null;
M._free(shortMeta); M._free(shortPointers); M._free(shortPcm);
// Until the session has no work: a four-frame source never reaches Measured2, so waiting for that state ran all the
// iterations, each with a whole snapshot.
for (let i = 0; i < 20000; ++i)
    if (M._fc_session_step(unavailableSession, 16, resultSize) !== STATUS.OK || M.HEAPU32[resultSize >>> 2] === 1) break;
const unavailableSnapshot = snapshotFor(unavailableSession);
const unavailableSource = BigInt(unavailableSnapshot?.source?.hash ?? '0');
const unavailableRevision = BigInt(unavailableSnapshot?.revision ?? '0');
const unavailableStatus = M._fc_session_master(unavailableSession, 2, 0,
    lo(unavailableSource), hi(unavailableSource), lo(unavailableRevision), hi(unavailableRevision),
    masterConfig, masterParams, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const unavailableAnswer = unavailableStatus === STATUS.OK ? reply() : null;
ok(shortLoadStatus === STATUS.OK && shortLoadAnswer?.kind === 'accepted'
    && unavailableStatus === STATUS.OK && unavailableAnswer?.kind === 'rejected'
    && unavailableSnapshot?.canMaster === false,
   'unavailable short source gives a codec refusal and no WAV');
contractRecord.scenarios.unavailable = {source:{pcmSha256:shortPcmSha256,
    pcmBase64:Buffer.from(new Float32Array(shortValues).buffer).toString('base64'),
    metadataJson:shortMetaJson, metadataSha256:sha256(encoder.encode(shortMetaJson)),
    frames:4, channels:2, rate:48000}, inputs:masterInputs(2, unavailableSource, unavailableRevision), loadAnswer:shortLoadAnswer,
    answer:unavailableAnswer, status:unavailableStatus,
    snapshot:masterWire('snapshot', unavailableSession), events:masterWire('events', unavailableSession)};
ok(M._fc_session_destroy(unavailableSession) === STATUS.OK, 'unavailable scenario session destroyed');
ok(create(resultSize) === STATUS.OK, 'unsafe scenario session created');
const unsafeSession = M.HEAPU32[resultSize >>> 2];
const unsafeFrames = 96000, unsafeSamples = unsafeFrames * 2;
const unsafePcm = M._malloc(unsafeSamples * 4), unsafePointers = M._malloc(8);
const unsafeData = new Float32Array(M.HEAPU32.buffer, unsafePcm, unsafeSamples);
unsafeData.fill(0.003); unsafeData[unsafeFrames - 1] = 1; unsafeData[unsafeSamples - 1] = 1;
M.HEAPU32[unsafePointers >>> 2] = unsafePcm;
M.HEAPU32[(unsafePointers >>> 2) + 1] = unsafePcm + unsafeFrames * 4;
const unsafePcmSha256 = sha256(heapBytes().slice(unsafePcm, unsafePcm + unsafeSamples * 4));
const unsafeMetaJson = JSON.stringify({name:'transient.wav', fileRate:48000, bitDepth:24, rateKnown:true});
const [unsafeMeta, unsafeMetaBytes] = input(unsafeMetaJson);
const unsafeLoadStatus = M._fc_session_load(unsafeSession, 1, 0, unsafePointers, 2, unsafeFrames, 48000,
    unsafeMeta, unsafeMetaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const unsafeLoadAnswer = unsafeLoadStatus === STATUS.OK ? reply() : null;
M._free(unsafeMeta); M._free(unsafePointers); M._free(unsafePcm);
let unsafeReady = null;
for (let i = 0; i < 20000; ++i) {
    if (i % 16 === 0 && (unsafeReady = snapshotFor(unsafeSession))?.canMaster) break;
    if (M._fc_session_step(unsafeSession, 16, resultSize) !== STATUS.OK) break;
}
const unsafeTarget = {kind:'editTarget', commandId:'2', fields:{lufs:-14, tp:-6}};
const unsafeTargetAnswer = cmd(unsafeSession, unsafeTarget);
const unsafeRevision = BigInt(snapshotFor(unsafeSession)?.revision ?? '0');
const unsafeSource = BigInt(unsafeReady?.source?.hash ?? '0');
setMaster(masterConfig, 'fc_master_config', 'limiter', 0);
setMaster(masterParams, 'fc_master_params', 'inputGainDb',
    59 - (-18 - unsafeReady.integratedLufs), 'f64');
const unsafeInputs = masterInputs(3, unsafeSource, unsafeRevision);
const unsafeStartStatus = M._fc_session_master(unsafeSession, 3, 0, lo(unsafeSource), hi(unsafeSource),
    lo(unsafeRevision), hi(unsafeRevision), masterConfig, masterParams,
    answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const unsafeStartAnswer = unsafeStartStatus === STATUS.OK ? reply() : null;
let unsafeComplete = null;
for (let i = 0; i < 40000; ++i) {
    if (i % 16 === 0 && (unsafeComplete = snapshotFor(unsafeSession))?.masters?.length) break;
    if (M._fc_session_step(unsafeSession, 16, resultSize) !== STATUS.OK) break;
}
const unsafeLanding = unsafeComplete?.masters?.at(-1)?.landing;
const ut = unsafeComplete?.pendingMaster ?? {};
const uts = BigInt(ut.source ?? '0'), utr = BigInt(ut.revision ?? '0');
for (const [i, value] of [28, lo(uts), hi(uts), lo(utr), hi(utr), ut.job ?? 0, ut.master ?? 0].entries())
    M.HEAPU32[(masterToken >>> 2) + i] = value;
const unsafeWavStatus = M._fc_session_master_wav_size(unsafeSession, masterToken, wavSize, wavBits);
ok(unsafeLoadAnswer?.kind === 'accepted' && unsafeTargetAnswer?.kind === 'accepted'
    && unsafeStartStatus === STATUS.OK && unsafeStartAnswer?.kind === 'accepted'
    && unsafeLanding && ! unsafeLanding.deliverable && unsafeComplete.pendingMaster.master === 0
    && unsafeWavStatus === STATUS.ERR_STALE,
   'unsafe landing retains measurements but refuses WAV transfer');
contractRecord.scenarios.unsafe = {source:{pcmSha256:unsafePcmSha256,
    generator:{frames:unsafeFrames, channels:2, value:0.003, last:1}, metadataJson:unsafeMetaJson,
    metadataSha256:sha256(encoder.encode(unsafeMetaJson))}, inputs:unsafeInputs,
    loadAnswer:unsafeLoadAnswer, target:unsafeTarget,
    targetSha256:sha256(encoder.encode(JSON.stringify(unsafeTarget))), targetAnswer:unsafeTargetAnswer,
    startAnswer:unsafeStartAnswer, snapshot:masterWire('snapshot', unsafeSession),
    events:masterWire('events', unsafeSession), measurements:unsafeLanding,
    wavSizeStatus:unsafeWavStatus};
ok(M._fc_session_destroy(unsafeSession) === STATUS.OK, 'unsafe scenario session destroyed');
ok(create(resultSize) === STATUS.OK, 'late crest scenario session created');
const lateSession = M.HEAPU32[resultSize >>> 2];
const lateFrames = 48000 * 4, lateSamples = lateFrames * 2;
const latePcm = M._malloc(lateSamples * 4), latePointers = M._malloc(8);
const lateData = new Float32Array(M.HEAPU32.buffer, latePcm, lateSamples);
for (let i = 0; i < lateFrames; ++i) {
    lateData[i] = (i * 17 % 251 - 125) / 4096;
    lateData[lateFrames + i] = ((i * 19 + 7) % 251 - 125) / 4096;
}
lateData[lateSamples - 1] -= .15;
M.HEAPU32[latePointers >>> 2] = latePcm;
M.HEAPU32[(latePointers >>> 2) + 1] = latePcm + lateFrames * 4;
const latePcmSha256 = sha256(heapBytes().slice(latePcm, latePcm + lateSamples * 4));
const lateMetaJson = JSON.stringify({name:'late-crest.wav', fileRate:48000, bitDepth:24, rateKnown:true});
const [lateMeta, lateMetaBytes] = input(lateMetaJson);
const lateLoadStatus = M._fc_session_load(lateSession, 1, 0, latePointers, 2, lateFrames, 48000,
    lateMeta, lateMetaBytes, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const lateLoadAnswer = lateLoadStatus === STATUS.OK ? reply() : null;
M._free(lateMeta); M._free(latePointers); M._free(latePcm);
let lateReady = null;
for (let i = 0; i < 20000; ++i) {
    if (i % 16 === 0 && (lateReady = masterSnapshot(lateSession))?.canMaster) break;
    if (M._fc_session_step(lateSession, 16, resultSize) !== STATUS.OK) break;
}
const lateReadyWire = masterWire('snapshot', lateSession);
const lateSource = BigInt(lateReady?.source?.hash ?? '0');
// The rate change is the target's: cdDynamic delivers 44.1 kHz PCM16 from this 48 kHz source, restated here.
const lateTargetCommand = {kind:'setTarget', commandId:'3', target:'cdDynamic'};
ok(cmd(lateSession, lateTargetCommand).kind === 'accepted', 'late-crest session selects the 44.1 kHz target');
const lateRevision = BigInt(masterSnapshot(lateSession)?.revision ?? '0');
setMaster(masterConfig, 'fc_master_config', 'limiter', 1);
setMaster(masterConfig, 'fc_master_config', 'deliveryRate', 44100, 'f64');
setMaster(masterParams, 'fc_master_params', 'inputGainDb', 0, 'f64');
new DataView(M.HEAPU32.buffer).setInt32(ditherBits, 16, true);
const lateInputs = masterInputs(2, lateSource, lateRevision);
const lateDemandStatus = M._fc_session_master_bytes(lateSession, lo(lateSource), hi(lateSource),
    lo(lateRevision), hi(lateRevision), masterConfig, masterParams, masterDemand);
const latePriceView = new DataView(M.HEAPU32.buffer);
const lateDemand = {bytes:latePriceView.getFloat64(masterDemand + 8, true),
    largestBlockBytes:latePriceView.getFloat64(masterDemand + 16, true),
    liveBytes:latePriceView.getFloat64(masterDemand + 24, true)};
ok(lateDemandStatus === STATUS.OK && M.HEAPU32[(masterDemand >>> 2) + 1] === 0,
   'SRC master declares its source-plus-output peak before work');
const lateStartStatus = M._fc_session_master(lateSession, 2, 0, lo(lateSource), hi(lateSource),
    lo(lateRevision), hi(lateRevision), masterConfig, masterParams,
    answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize);
const lateStartAnswer = lateStartStatus === STATUS.OK ? reply() : null;
for (let i = 0; i < 40000; ++i)
    if (M._fc_session_step(lateSession, 1, resultSize) !== STATUS.OK || masterCompleted(lateSession)) break;
const lateCompleted = masterSnapshot(lateSession);
const lateCompleteWire = masterWire('snapshot', lateSession);
const lateMasterId = lateCompleted?.pendingMaster?.master ?? 0;
const lateBefore = lateCompleted?.masters?.find(row => row.id === lateMasterId)?.report;
const lateLanding = lateCompleted?.masters?.find(row => row.id === lateMasterId)?.landing;
const lateOutputPcmBytes = lateCompleted?.pendingMasterBytes ?? 0;
ok(lateDemand.bytes >= lateOutputPcmBytes && lateDemand.largestBlockBytes >= lateOutputPcmBytes
    && lateDemand.liveBytes + lateDemand.bytes >= lateSamples * 4 + lateOutputPcmBytes,
   'SRC price covers both complete PCM buffers without requiring a third');
const lateToken = lateCompleted?.pendingMaster ?? {};
const lateTokenSource = BigInt(lateToken.source ?? '0'), lateTokenRevision = BigInt(lateToken.revision ?? '0');
for (const [i, value] of [28, lo(lateTokenSource), hi(lateTokenSource), lo(lateTokenRevision),
                          hi(lateTokenRevision), lateToken.job ?? 0, lateToken.master ?? 0].entries())
    M.HEAPU32[(masterToken >>> 2) + i] = value;
const lateWavSizeStatus = M._fc_session_master_wav_size(lateSession, masterToken, wavSize, wavBits);
const lateWavBytes = new DataView(M.HEAPU32.buffer).getFloat64(wavSize, true);
const lateWav = new Uint8Array(lateWavBytes);
let lateWavCopied = lateWavSizeStatus === STATUS.OK;
for (let at = 0; at < lateWav.length; at += 997) {
    const count = Math.min(997, lateWav.length - at);
    lateWavCopied &&= M._fc_session_master_wav_copy(lateSession, masterToken, at, 0,
        wavChunk, count, wavWritten) === STATUS.OK && M.HEAPU32[wavWritten >>> 2] === count;
    lateWav.set(heapBytes().subarray(wavChunk, wavChunk + count), at);
}
const lateWavSha256 = sha256(lateWav);
const lateReleaseStatus = M._fc_session_master_audio_release(lateSession, masterToken);
const lateReleasedWire = masterWire('snapshot', lateSession);
const lateHeapBefore = M.HEAPU32.buffer, lateGrowth = [];
const lateHeapBeforeBytes = lateHeapBefore.byteLength;
for (let i = 0; i < 12 && M.HEAPU32.buffer === lateHeapBefore; ++i)
    lateGrowth.push(M._malloc(16 * 1024 * 1024));
const lateGrew = M.HEAPU32.buffer !== lateHeapBefore;
let lateSourceEvent = null, lateJoinEvent = null;
for (let i = 0; i < 20000 && !lateJoinEvent; ++i) {
    if (M._fc_session_step(lateSession, 1, resultSize) !== STATUS.OK) break;
    const eventWire = masterWire('events', lateSession);
    if (!eventWire) break;
    const events = wireValue(eventWire);
    if (!lateSourceEvent && events.some(e => e.kind === 'measurement' && e.payload.analyzer === 9
        && e.payload.status === 1)) lateSourceEvent = eventWire;
    if (events.some(e => e.kind === 'fact' && e.jobId === lateMasterId
        && e.payload.FactId === 18)) lateJoinEvent = eventWire;
}
const lateJoinedWire = masterWire('snapshot', lateSession);
const lateJoined = wireValue(lateJoinedWire);
const lateAfter = lateJoined.masters?.find(row => row.id === lateMasterId)?.report;
const lateWavAfterSha256 = sha256(lateWav);
ok(lateLoadAnswer?.kind === 'accepted' && lateReady?.canMaster && lateStartAnswer?.kind === 'accepted'
    && lateBefore?.crest?.status === 0 && lateReleaseStatus === STATUS.OK && lateGrew
    && lateSourceEvent && lateJoinEvent && lateAfter?.crest?.status === 1
    && lateBefore.checkPasses === 1 && lateAfter.checkPasses === 1
    && lateAfter.crest.sourceRateCheck && lateLanding?.passes === lateJoined.masters[0].landing.passes
    && lateBefore.achievedLufs === lateAfter.achievedLufs
    && lateBefore.truePeakDbTp === lateAfter.truePeakDbTp && lateWavCopied
    && lateWavSha256 === lateWavAfterSha256,
   'source-rate crest joins its master after WAV release and memory growth without changing delivery');
contractRecord.scenarios.lateCrest = {source:{pcmSha256:latePcmSha256, metadataJson:lateMetaJson,
    metadataSha256:sha256(encoder.encode(lateMetaJson)),
    generator:{frames:lateFrames, channels:2, rate:48000, lastRightOffset:-.15}},
    targetCommand:lateTargetCommand,
    inputs:lateInputs, loadStatus:lateLoadStatus, loadAnswer:lateLoadAnswer, ready:lateReadyWire,
    startStatus:lateStartStatus, startAnswer:lateStartAnswer, complete:lateCompleteWire,
    export:{sizeStatus:lateWavSizeStatus, bytes:lateWavBytes, sha256:lateWavSha256,
        postJoinSha256:lateWavAfterSha256},
    price:{sourcePcmBytes:lateSamples * 4, deliveredPcmBytes:lateOutputPcmBytes,
        declared:lateDemand, observedHeap:{beforeGrowthBytes:lateHeapBeforeBytes,
            afterGrowthBytes:M.HEAPU32.buffer.byteLength},
        browser:{wavBytes:lateWav.byteLength, copyChunkBytes:997}},
    release:{status:lateReleaseStatus, snapshot:lateReleasedWire, heapGrew:lateGrew},
    sourceEvent:lateSourceEvent, joinEvent:lateJoinEvent, joined:lateJoinedWire};
for (const p of lateGrowth) M._free(p);
ok(M._fc_session_destroy(lateSession) === STATUS.OK, 'late crest scenario session destroyed');
console.log(`wav-record=${JSON.stringify(contractRecord)}`);
for (const p of growth) M._free(p);

// A MASTER READ BY QUERIES, AND THE LEAN SUMMARY (additions to version 1; after the recorded scenarios, so none of them
// moves). A shell that appends leanSummary = 1 to its capabilities gets summaries without the masters' heavy rows and
// reads one master whole with a MasterReport query; a version-1 record of 32 bytes — every session above — does not.
{
    const transfer = (what, handle) => {
        const sizes = M._malloc(12); M.HEAPU32[sizes >>> 2] = 12;
        let value = null, jsonBytes = 0, rowBytes = 0, binary = new ArrayBuffer(0);
        if (M[`_fc_session_${what}_size`](handle, sizes) === STATUS.OK) {
            jsonBytes = M.HEAPU32[(sizes >>> 2) + 1]; rowBytes = M.HEAPU32[(sizes >>> 2) + 2];
            const json = M._malloc(jsonBytes), rows = rowBytes ? M._malloc(rowBytes) : 0;
            if (M[`_fc_session_${what}_copy`](handle, json, jsonBytes, rows, rowBytes) === STATUS.OK) {
                value = JSON.parse(decoder.decode(heapBytes().slice(json, json + jsonBytes)));
                if (rowBytes) binary = heapBytes().slice(rows, rows + rowBytes).buffer;
            }
            M._free(json); if (rows) M._free(rows);
        }
        M._free(sizes);
        return {value, jsonBytes, rowBytes, binary};
    };
    const ask = (handle, request) => {
        const encoded = encoder.encode(JSON.stringify(request));
        const text = M._malloc(encoded.length); heapBytes().set(encoded, text);
        const sizes = M._malloc(12), written = M._malloc(12); M.HEAPU32[sizes >>> 2] = 12; M.HEAPU32[written >>> 2] = 12;
        let answer = null;
        if (M._fc_session_query_size(handle, text, encoded.length, sizes) === STATUS.OK) {
            const jsonBytes = M.HEAPU32[(sizes >>> 2) + 1], rowBytes = M.HEAPU32[(sizes >>> 2) + 2];
            const json = M._malloc(jsonBytes), rows = rowBytes ? M._malloc(rowBytes) : 0;
            if (M._fc_session_query_copy(handle, text, encoded.length, json, jsonBytes, rows, rowBytes, written) === STATUS.OK) {
                const wroteJson = M.HEAPU32[(written >>> 2) + 1], wroteRows = M.HEAPU32[(written >>> 2) + 2];
                answer = {response:JSON.parse(decoder.decode(heapBytes().slice(json, json + wroteJson))),
                    rows:new Float64Array(heapBytes().slice(rows, rows + wroteRows).buffer), boundJson:jsonBytes, wroteJson, wroteRows};
            }
            M._free(json); if (rows) M._free(rows);
        }
        M._free(text); M._free(sizes); M._free(written);
        return answer;
    };
    // Row descriptors address each transfer's own buffer: read them out before two records are compared.
    const resolved = (kept, binary) => {
        const copy = structuredClone(kept);
        for (const name of ['rows', 'sourceMask']) {
            const row = copy.report.crest[name];
            copy.report.crest[name] = [...new Float64Array(binary, row.byteOffset, row.length * row.stride)];
        }
        return copy;
    };
    // leanSummary at 32 of the 40-byte record (tools/session-abi-check.mjs measures both from the compiled header).
    const leanCapsBytes = 40, leanAt = 32, leanCaps = M._malloc(leanCapsBytes);
    heapBytes().copyWithin(leanCaps, caps, caps + leanCapsBytes);
    M.HEAPU32[leanCaps >>> 2] = leanCapsBytes;
    M.HEAPU32[(leanCaps + leanAt) >>> 2] = 1;
    ok(M._fc_session_create(leanCaps, configLow, configHigh, resultSize) === STATUS.OK, 'a lean session is created from the capabilities record');
    const leanSession = M.HEAPU32[resultSize >>> 2];
    M.HEAPU32[(leanCaps + leanAt) >>> 2] = 2;
    ok(M._fc_session_create(leanCaps, configLow, configHigh, resultSize) === STATUS.ERR_CAPABILITIES, 'a leanSummary that is neither 0 nor 1 is refused');
    const leanFrames = 96000, leanPcm = M._malloc(leanFrames * 8), leanPointers = M._malloc(8);
    const words = new Float32Array(M.HEAPU32.buffer);
    for (let i = 0; i < leanFrames; ++i) {
        words[(leanPcm >>> 2) + i] = (i * 17 % 251 - 125) / 4096;
        words[(leanPcm >>> 2) + leanFrames + i] = (i * 19 + 7) % 251 / 4096 - 125 / 4096;
    }
    M.HEAPU32[leanPointers >>> 2] = leanPcm; M.HEAPU32[(leanPointers >>> 2) + 1] = leanPcm + leanFrames * 4;
    const [leanMeta, leanMetaBytes] = input(JSON.stringify({name:'ready.wav', fileRate:48000, bitDepth:24, rateKnown:true}));
    ok(M._fc_session_load(leanSession, 1, 0, leanPointers, 2, leanFrames, 48000, leanMeta, leanMetaBytes, answer,
        macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK, 'the lean session loads the master smoke source');
    M._free(leanMeta); M._free(leanPointers); M._free(leanPcm);
    let leanReady = null;
    for (let i = 0; i < 20000; ++i) {
        if (i % 32 === 0 && (leanReady = transfer('summary', leanSession).value)?.canMaster) break;
        if (M._fc_session_step(leanSession, 16, resultSize) !== STATUS.OK) break;
    }
    ok(leanReady?.canMaster === true && leanReady.masterRowsIncluded === false && accepts(leanReady, 'SessionSnapshot'),
       'its summary says masterRowsIncluded false and agrees with the generated types');
    const leanSource = BigInt(leanReady?.source?.hash ?? '0'), leanRevision = BigInt(leanReady?.revision ?? '0');
    // The delivery format is the target's own (0 and 0), whatever the scenario before left in the shared records.
    setMaster(masterConfig, 'fc_master_config', 'deliveryRate', 0, 'f64');
    new DataView(M.HEAPU32.buffer).setInt32(ditherBits, 0, true);
    ok(M._fc_session_master(leanSession, 2, 0, lo(leanSource), hi(leanSource), lo(leanRevision), hi(leanRevision),
        masterConfig, masterParams, answer, macro('FC_SESSION_ANSWER_BYTES'), resultSize) === STATUS.OK && reply().kind === 'accepted',
       'the lean session starts a master');
    for (let i = 0; i < 40000; ++i) {
        if (M._fc_session_step(leanSession, 16, resultSize) !== STATUS.OK) break;
        if (masterCompleted(leanSession)) break;
    }
    const whole = transfer('snapshot', leanSession), light = transfer('summary', leanSession);
    const kept = whole.value?.masters?.at(-1), lightKept = light.value?.masters?.at(-1);
    ok(kept?.landing?.limiterTrace?.rows?.length > 0 && kept.report.cost.waveform.length > 0 && whole.value.masterRowsIncluded === true
        && accepts(whole.value, 'SessionSnapshot'), 'its snapshot carries the master whole');
    ok(lightKept && light.value.masterRowsIncluded === false && accepts(light.value, 'SessionSnapshot')
        && lightKept.landing.limiterTrace == null && lightKept.landing.peakClipTrace == null
        && lightKept.report.crest.rows.length === 0 && lightKept.report.crest.sourceMask.length === 0
        && lightKept.report.cost.waveform.length === 0 && lightKept.landing.log.length === lightKept.landing.passes
        && lightKept.report.cost.sections.length === kept.report.cost.sections.length
        && lightKept.report.achievedLufs === kept.report.achievedLufs && lightKept.landing.status === kept.landing.status
        && light.jsonBytes * 10 < whole.jsonBytes,
       `its summary keeps the master's scalars, pass log and sections, and no heavy row: ${light.jsonBytes} B of JSON against the snapshot's ${whole.jsonBytes}`);
    const base = {audioId:whole.value.source.hash, requestId:'41', masterId:kept.id};
    const report = ask(leanSession, {...base, kind:11, fromFrame:'0', toFrame:'0', columns:0});
    ok(report && accepts(report.response, 'QueryResponse') && report.response.status === 0 && report.wroteJson <= report.boundJson
        && report.response.values.length === 0 && report.response.requestId === undefined && report.response.request.requestId === '41',
       'a MasterReport query is answered within the size given beforehand and agrees with the generated types');
    ok(report && JSON.stringify(resolved(report.response.master, report.rows.buffer)) === JSON.stringify(resolved(kept, whole.binary)),
       'and its record is the snapshot\'s master, field for field and row for row');
    const axes = ask(leanSession, {...base, kind:10, fromFrame:'0', toFrame:String(leanFrames), columns:16});
    ok(axes && accepts(axes.response, 'QueryResponse') && axes.response.status === 0 && axes.response.stride === 13 && axes.response.stored === '64'
        && axes.rows.length === 64 * 13 && axes.rows[2] === 0 && axes.rows[13 + 2] === 1 && axes.rows[26 + 2] === 2 && axes.rows[39 + 2] === 3
        && axes.rows[39 + 7] > 0, 'a MasterAxes query answers four axes a column, the Side carrying signal');
    const curve = ask(leanSession, {...base, kind:4, fromFrame:'0', toFrame:String(leanFrames), columns:64});
    const early = ask(leanSession, {...base, kind:3, fromFrame:'0', toFrame:String(leanFrames), columns:64});
    ok(curve && curve.response.status === 0 && curve.response.stride === 3 && curve.response.stored === '20' && curve.rows[0] === 4800
        && early && early.response.stored === '20' && Number.isFinite(early.rows[3 * 19 + 1]) && early.rows[3 * 19 + 2] === 0,
       'Momentary and ShortTerm with a master id answer the master\'s own curves, a row per 100 ms');
    const density = ask(leanSession, {kind:1, audioId:whole.value.source.hash, fromFrame:'0', toFrame:String(leanFrames), columns:8, requestId:'42'});
    const energy = ask(leanSession, {kind:1, audioId:whole.value.source.hash, fromFrame:'0', toFrame:String(leanFrames), columns:8, requestId:'43', spectrum:1});
    ok(density && energy && accepts(energy.response, 'QueryResponse') && density.response.request.spectrum === 0 && energy.response.request.spectrum === 1
        && density.rows.length === 24 && energy.rows.length === 24 && density.rows.some((v, i) => i % 3 === 1 && v !== energy.rows[i]),
       'LowSpectrum answers density by default and energy when asked');
    const gone = ask(leanSession, {...base, masterId:kept.id + 9, kind:11, fromFrame:'0', toFrame:'0', columns:0});
    ok(gone && gone.response.status === 2 && gone.response.master == null && gone.wroteRows === 0, 'no such master: unavailable, no record');
    console.log(`lean-summary: 1 master — summary ${light.jsonBytes} + ${light.rowBytes} B, snapshot ${whole.jsonBytes} + ${whole.rowBytes} B, report query ${report?.wroteJson} + ${report?.wroteRows} B`);
    ok(M._fc_session_destroy(leanSession) === STATUS.OK, 'lean session destroyed');
    M._free(leanCaps);
}
// THE PURE KIT — the corpus modules/session/tests/KitTests.cpp hashes natively (corpusHash), given to this module through
// the same fc_kit_* entry points in the same order and hashed the same way: one FNV-1a 64 value on both sides is
// native == wasm, byte for byte (the texts, the parsed values, the travels and heats, the zones, the EQ curves to the last
// bit, the low-end curve).
{
    const KIT_PINNED = 0x7607472f9fe4c104n;
    const FACTS = [
        '{"FactId":3,"args":[{"kind":2,"unit":0,"precision":0,"sign":0,"bound":0,"termId":0,"number":0,"integer":"7","userText":""}]}',
        '{"FactId":504,"args":[{"kind":1,"unit":2,"precision":1,"sign":1,"bound":0,"termId":0,"number":2.375,"integer":"0","userText":""},{"kind":1,"unit":7,"precision":0,"sign":0,"bound":0,"termId":0,"number":62.5,"integer":"0","userText":""}]}',
        '{"FactId":5,"args":[{"kind":4,"unit":0,"precision":0,"sign":0,"bound":0,"termId":0,"number":0,"integer":"28","userText":""}]}',
        '{"FactId":112,"args":[{"kind":3,"unit":0,"precision":0,"sign":0,"bound":0,"termId":3,"number":0,"integer":"0","userText":""}]}'];
    const LANGS = ['en', 'ru'];
    const PARSES = [['-14.05', 'en', 3, 0], ['\u221214,05', 'ru', 3, 0], ['14', 'en', 3, 0], ['-0.04', 'en', 4, 0], ['0.25', 'ru', 4, 0],
        ['30.5', 'en', 5, 48000], ['30,4', 'ru', 5, 0], ['24', 'en', 6, 0], ['25', 'en', 6, 0], ['0.325', 'en', 8, 0], ['abc', 'en', 9, 0],
        ['1,5', 'de', 13, 0]];
    const KNOBS = [[3, -20], [3, -14], [3, -9], [4, -1.5], [5, 22], [5, 46], [13, 2.25], [9, 1.2], [16, -4]];
    const ZONE_PROBES = [70, 100, 120, 150, 250];
    const EQ_PARAMS = [[1, 30, 24, 1, 2, 0, 0], [1, 25, 12, 1, -3, 1, 4.5]];
    const EQ_RATES = [48000, 44100];
    const LOW_CENTRES = [20, 30, 40, 50, 60], LOW_ENERGIES = [0.01, 0, 1, 0.0001, 0.5];
    const MASK = (1n << 64n) - 1n;
    let h = 0xcbf29ce484222325n;
    const byte = b => { h = ((h ^ BigInt(b & 255)) * 0x100000001b3n) & MASK; };
    const cell = new Float64Array(1), cellBytes = new Uint8Array(cell.buffer);
    const number = v => { cell[0] = v; for (const b of cellBytes) byte(b); };
    const word = v => { for (let i = 0; i < 4; ++i) byte(v >>> (8 * i)); };
    const S = M._malloc(8192);
    const u8 = () => new Uint8Array(M.HEAPU32.buffer);
    const f64 = (at, n) => new Float64Array(M.HEAPU32.buffer, at, n);
    const u32 = at => M.HEAPU32[at >>> 2];
    const setU32 = (at, v) => { M.HEAPU32[at >>> 2] = v; };
    const put = (text, at) => { const bytes = new TextEncoder().encode(text); u8().set(bytes, at); return bytes.length; };
    const IN = S, LANG = S + 1024, WORD = S + 1040, VALUE = S + 1048, SMALL = S + 1056, TEXT = S + 1280, CURVE = S + 2048,
          PEAK = S + 4096, PARAMS = S + 4160, CENTRES = S + 4224, ENERGIES = S + 4288, POINTS = S + 4352;
    for (const fact of FACTS)
        for (const lang of LANGS) {
            const n = put(fact, IN); put(lang, LANG); setU32(WORD, 0);
            byte(M._fc_kit_text(IN, n, LANG, 2, TEXT, 512, WORD));
            const w = u32(WORD); word(w); for (const b of u8().slice(TEXT, TEXT + w)) byte(b);
        }
    for (const [typed, lang, field, rate] of PARSES) {
        const n = put(typed, IN); put(lang, LANG); f64(VALUE, 1)[0] = -1; setU32(WORD, 9);
        byte(M._fc_kit_parse(IN, n, LANG, 2, field, rate, VALUE, WORD));
        byte(u32(WORD)); number(f64(VALUE, 1)[0]);
    }
    for (const [field, value] of KNOBS) {
        f64(SMALL, 3).fill(0);
        byte(M._fc_kit_position(field, value, SMALL)); number(f64(SMALL, 1)[0]);
        byte(M._fc_kit_value_at(field, 0.37, SMALL)); number(f64(SMALL, 1)[0]);
        byte(M._fc_kit_heat(field, value, SMALL)); for (const v of f64(SMALL, 3)) number(v);
    }
    f64(SMALL, 4).fill(0);
    byte(M._fc_kit_mono_zones(SMALL)); for (const v of f64(SMALL, 4)) number(v);
    for (const hz of ZONE_PROBES) { setU32(WORD, 0); byte(M._fc_kit_mono_zones_at(hz, WORD)); word(u32(WORD)); }
    for (const params of EQ_PARAMS)
        for (const rate of EQ_RATES) {
            f64(PARAMS, 7).set(params); f64(CURVE, 256).fill(0); f64(PEAK, 4).fill(0);
            byte(M._fc_kit_eq_curve(PARAMS, rate, CURVE, PEAK));
            for (const v of f64(CURVE, 256)) number(v);
            for (const v of f64(PEAK, 4)) number(v);
        }
    f64(CENTRES, 5).set(LOW_CENTRES); f64(ENERGIES, 5).set(LOW_ENERGIES); f64(POINTS, 10).fill(0); setU32(WORD, 0);
    byte(M._fc_kit_low_end_curve(CENTRES, ENERGIES, 5, 25, 55, POINTS, 5, WORD));
    { const w = u32(WORD); word(w); for (const v of f64(POINTS, 2 * w)) number(v); }
    ok(h === KIT_PINNED, `the pure kit's corpus hashes to the value KitTests.cpp pins natively (${h.toString(16)})`);
    {
        // The EQ bands' curve: five gains of 0 dB answer fc_kit_eq_curve's bytes, and so do gains ticked off; a gain moves
        // the curve; mud above 0 dB is a contract fault. Thirteen doubles past the corpus's buffers.
        const BANDS = S + 4608;
        let same = true, moved = false;
        for (const params of EQ_PARAMS)
            for (const rate of EQ_RATES) {
                f64(BANDS, 7).set(params);
                same = same && M._fc_kit_eq_curve(BANDS, rate, CURVE, PEAK) === STATUS.OK;
                const plain = [...f64(CURVE, 256)], plainPeak = [...f64(PEAK, 4)];
                for (const bands of [[0, 0, 0, 0, 0, 1], [3, -1.5, 2, -2, 1, 0]]) {
                    f64(BANDS, 13).set([...params, ...bands]);
                    same = same && M._fc_kit_eq_curve_bands(BANDS, rate, CURVE, PEAK) === STATUS.OK
                        && f64(CURVE, 256).every((v, i) => Object.is(v, plain[i])) && f64(PEAK, 4).every((v, i) => Object.is(v, plainPeak[i]));
                }
                f64(BANDS, 13).set([...params, 3, 0, 0, 0, 0, 1]);
                moved = moved || (M._fc_kit_eq_curve_bands(BANDS, rate, CURVE, PEAK) === STATUS.OK
                    && f64(CURVE, 256).some((v, i) => i % 2 === 1 && v - plain[i] > 2.9));
            }
        f64(BANDS, 13).set([1, 30, 24, 1, 2, 0, 0, 0, 0.5, 0, 0, 0, 0]);
        const mudUp = M._fc_kit_eq_curve_bands(BANDS, 48000, CURVE, PEAK);
        f64(BANDS, 13).set([1, 30, 24, 1, 2, 0, 0, 1, 0, 0, 0, 0, 0.5]);
        ok(same && moved && mudUp === STATUS.ERR_CONTRACT && M._fc_kit_eq_curve_bands(BANDS, 48000, CURVE, PEAK) === STATUS.ERR_CONTRACT,
           'fc_kit_eq_curve_bands: at 0 dB or ticked off the bytes of fc_kit_eq_curve, +3 dB of body lifts the curve, mud above 0 dB '
           + 'refused even ticked off, a tick neither 0 nor 1 a contract fault');
    }
    put(FACTS[0], IN); put('ru', LANG);
    ok(M._fc_kit_text(IN, FACTS[0].length, LANG, 2, TEXT, 512, WORD) === STATUS.OK
       && new TextDecoder().decode(u8().slice(TEXT, TEXT + u32(WORD))).includes('7'), 'a fact renders in Russian on the module');
    put('xx', LANG);
    ok(M._fc_kit_text(IN, FACTS[0].length, LANG, 2, TEXT, 512, WORD) === STATUS.ERR_CONTRACT, 'an unknown language is a contract fault');
    ok(M._fc_kit_travel(3, 0) === STATUS.ERR_NULL && M._fc_kit_travel(3, SMALL + 4) === STATUS.ERR_ALIGNMENT
       && M._fc_kit_travel(3, M.HEAPU32.buffer.byteLength - 16) === STATUS.ERR_SPAN, 'the kit guards its outputs as every entry point does');
    M._free(S);
}
for (const p of [masterConfig, masterParams, masterDemand, masterToken, audioBytes, audioFrames, audioChannels, audioRate,
                 viewOut, sampleOut]) M._free(p);
ok(M._fc_session_destroy(masterSession) === STATUS.OK, 'master smoke session destroyed');
ok(M._fc_session_destroy(session) === STATUS.OK, 'smoke session destroyed');
for (const p of [caps, answer, resultSize, demand]) M._free(p);
console.log(`session-check: fcsession v${version} — ${checks} checks, ${bad} failures`);
process.exitCode = bad ? 1 : 0;
