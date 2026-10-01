// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// An external driver: timings never enter the session or its deterministic event stream.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {resolve, dirname, join} from 'node:path';
import {pathToFileURL} from 'node:url';

const modulePath = resolve(process.argv[2] ?? '');
const seconds = Number(process.argv[3] ?? 60);
assert.ok(Number.isInteger(seconds) && seconds >= 4 && seconds <= 600);
const frames = 48000 * seconds, channels = 2, rate = 48000;
const runtime = await import(pathToFileURL(join(dirname(modulePath), 'snapshot.mjs')));
const require = createRequire(import.meta.url);
const start = performance.now();
const M = await require(modulePath)();
const coldMs = performance.now() - start;
const header = readFileSync(new URL('../fc_session_abi.h', import.meta.url), 'utf8');
const macro = name => Number(new RegExp(`^#define FC_SESSION_${name} ([0-9]+)u$`, 'm').exec(header)[1]);
const call = (name, ...args) => runtime.invokeSession(M[`_fc_session_${name}`], ...args);
const ok = (name, ...args) => assert.equal(call(name, ...args), 0, name);
const alloc = n => { const p = M._malloc(Math.max(1, n)); assert.ok(p, `malloc ${n}`); return p; };
const u32 = p => M.HEAPU32[p >>> 2];
const put = (p, v) => { M.HEAPU32[p >>> 2] = v; };
const f64 = p => new DataView(M.HEAPU32.buffer).getFloat64(p, true);
const put64 = (p, v) => new DataView(M.HEAPU32.buffer).setFloat64(p, v, true);
const caps = alloc(40), handle = alloc(4);   // fc_session_capabilities: 40 bytes, leanSummary (at 32) 0
put(caps, 40); put64(caps + 8, 1024 * 1024 * 1024);
put(caps + 16, 96000); put(caps + 20, macro('DEVICES_ALL')); put64(caps + 24, 1024 * 1024 * 1024); put(caps + 32, 0);
const version = runtime.FC_SESSION_CONFIG_VERSION;
ok('create', caps, Number.parseInt(version.slice(8), 16), Number.parseInt(version.slice(0, 8), 16), handle);
const session = u32(handle);
const pcm = alloc(frames * channels * 4), planes = alloc(channels * 4);
for (let c = 0; c < channels; ++c) {
    put(planes + c * 4, pcm + c * frames * 4);
    // Exact binary32 values; no host math library participates in the fixture.
    const view = new Float32Array(M.HEAPU32.buffer, pcm + c * frames * 4, frames);
    for (let i = 0; i < frames; ++i) view[i] = (((i + 7 * c) % 128) - 64) / 256;
}
const metaBytes = Buffer.from('{"name":"timing-synthetic","fileRate":48000,"bitDepth":16,"rateKnown":true}');
const meta = alloc(metaBytes.length); new Uint8Array(M.HEAPU32.buffer).set(metaBytes, meta);
const storage = alloc(macro('STORAGE_V1_BYTES')); put(storage, macro('STORAGE_V1_BYTES'));
ok('load_bytes', session, channels, frames, rate, meta, metaBytes.length, storage);
assert.equal(u32(storage + 4), 0, 'long-file load admitted before work');
const answer = alloc(macro('ANSWER_BYTES')), written = alloc(4);
const loadAt = performance.now();
ok('load', session, 1, 0, planes, channels, frames, rate, meta, metaBytes.length, answer, macro('ANSWER_BYTES'), written);
const loadMs = performance.now() - loadAt;
assert.equal(JSON.parse(Buffer.from(new Uint8Array(M.HEAPU32.buffer, answer, u32(written))).toString()).kind, 'accepted');
const state = alloc(4), sizes = alloc(macro('SIZES_V1_BYTES'));
const groups = {prepare:[], process:[], finish:[]}, versions = new Set(), analyzers = {};
function events() {
    put(sizes, macro('SIZES_V1_BYTES')); ok('events_size', session, sizes);
    const json = alloc(u32(sizes + 4)), rows = u32(sizes + 8) ? alloc(u32(sizes + 8)) : 0;
    ok('events_copy', session, json, u32(sizes + 4), rows, u32(sizes + 8));
    const value = JSON.parse(Buffer.from(new Uint8Array(M.HEAPU32.buffer, json, u32(sizes + 4))).toString());
    M._free(json); if (rows) M._free(rows); return value;
}
let calls = 0, intervalMs = 0, measured1Ms = null;
for (; calls < 500000; ++calls) {
    const then = performance.now(); ok('step', session, 16, state); const elapsed = performance.now() - then;
    intervalMs += elapsed;
    const batch = events();
    if (measured1Ms === null && batch.some(e => e.state === 2)) measured1Ms = performance.now() - loadAt;
    for (const e of batch) if (e.kind === 'phase') versions.add(e.payload.weightsVersion);
    const kind = batch.some(e => e.kind === 'measurement' || e.kind === 'done') ? 'finish'
        : batch.some(e => e.phase === 0) ? 'prepare' : 'process';
    groups[kind].push(elapsed);
    const published = batch.filter(e => e.kind === 'measurement');
    if (published.length) {
        for (const e of published) analyzers[e.payload.analyzer] = Number((intervalMs / published.length).toFixed(3));
        intervalMs = 0;
    }
    if (u32(state) === 1) break;
}
assert.ok(calls < 500000, 'long-file measurement finished');
assert.ok(measured1Ms !== null, 'first measurement phase completed');
const growthBytes = M.HEAPU32.buffer.byteLength;
const target = Buffer.from('{"kind":"editTarget","commandId":"20","fields":{"lufs":-1}}');
const targetInput = alloc(target.length); new Uint8Array(M.HEAPU32.buffer).set(target, targetInput);
ok('command_bytes', session, targetInput, target.length, storage);
ok('command', session, targetInput, target.length, answer, macro('ANSWER_BYTES'), written);
assert.equal(JSON.parse(Buffer.from(new Uint8Array(M.HEAPU32.buffer, answer, u32(written))).toString()).kind, 'accepted');
put(sizes, macro('SIZES_V1_BYTES')); ok('summary_size', session, sizes);
const summaryText = alloc(u32(sizes + 4));
ok('summary_copy', session, summaryText, u32(sizes + 4), 0, 0);
const summary = JSON.parse(Buffer.from(new Uint8Array(M.HEAPU32.buffer, summaryText, u32(sizes + 4))).toString());
assert.ok(summary.needlesNeedDb > 3 && summary.needlesJob > 0, 'positive need schedules excursions');
const needlesAt = performance.now();
let needlesPublished = false;
for (let i = 0; i < 500000; ++i) {
    ok('step', session, 16, state);
    needlesPublished ||= events().some(e => e.kind === 'measurement' && e.payload.analyzer === 12);
    if (u32(state) === 1) break;
}
const needlesMs = performance.now() - needlesAt;
assert.ok(needlesPublished, 'positive need ran the excursions index');
// A second load begins a fresh job. Cancelling it is an ordinary command call.
new Float32Array(M.HEAPU32.buffer, pcm, 1)[0] = -0.125;
ok('load_bytes', session, channels, frames, rate, meta, metaBytes.length, storage);
ok('load', session, 2, 0, planes, channels, frames, rate, meta, metaBytes.length, answer, macro('ANSWER_BYTES'), written);
const job = JSON.parse(Buffer.from(new Uint8Array(M.HEAPU32.buffer, answer, u32(written))).toString()).jobId;
for (let i = 0; i < 100; ++i) ok('step', session, 16, state);
const command = Buffer.from(JSON.stringify({kind:'cancel', commandId:'3', jobId:job}));
const input = alloc(command.length); new Uint8Array(M.HEAPU32.buffer).set(command, input);
ok('command_bytes', session, input, command.length, storage);
const cancelAt = performance.now();
ok('command', session, input, command.length, answer, macro('ANSWER_BYTES'), written);
const cancelMs = performance.now() - cancelAt;
assert.equal(JSON.parse(Buffer.from(new Uint8Array(M.HEAPU32.buffer, answer, u32(written))).toString()).kind, 'accepted');
const summarize = values => {
    values.sort((a, b) => a - b);
    return {calls:values.length, totalMs:Number(values.reduce((a, b) => a + b, 0).toFixed(3)),
        medianMs:Number((values[Math.floor(values.length / 2)] ?? 0).toFixed(3)),
        p95Ms:Number((values[Math.floor(values.length * .95)] ?? 0).toFixed(3)),
        maxMs:Number((values.at(-1) ?? 0).toFixed(3))};
};
console.log(JSON.stringify({fixture:`${seconds} s, 48 kHz stereo sawtooth, Node ${process.version}`,coldWasmMs:Number(coldMs.toFixed(3)),
    loadMs:Number(loadMs.toFixed(3)),measured1Ms:Number(measured1Ms.toFixed(3)),prepare:summarize(groups.prepare),process:summarize(groups.process),finish:summarize(groups.finish),
    analyzerIntervalsMs:analyzers,needlesNeedDb:summary.needlesNeedDb,needlesMs:Number(needlesMs.toFixed(3)),
    cancelMs:Number(cancelMs.toFixed(3)),
    weightsVersion:[...versions],wasmMemoryBytes:growthBytes},null,2));
ok('destroy', session);
