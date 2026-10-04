// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync, writeFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {brotliCompressSync, constants as zlibConstants, gunzipSync} from 'node:zlib';
import {resolve} from 'node:path';
import {types} from '../session-wire-types.mjs';
import {sizeOf} from '../wasm/fc-master-layout.mjs';
import {decodeWavRecording} from './wav-recording.mjs';
import {childTimeout} from './child-timeout.mjs';

const [nativeAbi, nativeJob, wasmModule, wasmJob, nativeCli, option] = process.argv.slice(2);
if (!nativeAbi || !nativeJob || !wasmModule || !wasmJob || !nativeCli || (option && option !== '--rebuild')) {
    console.error('usage: node tools/contract/wav-contract.mjs <native-ABI-test> <native-job-test> <fcsession.node.js> <wasm-job-test.js> <fcore_session> [--rebuild]');
    process.exit(2);
}
const file = new URL('recordings/wav-contract.json', import.meta.url);
const input = new URL('wav-input.json', import.meta.url);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
// Six minutes a child by default: the wasm session check is the longest, about a minute on a desktop. A slower tier
// raises it through FC_SESSION_CONTRACT_TIMEOUT_MS, the same variable and rule as run.mjs.
const timeout = childTimeout(360000);
function command(exe, ...args) {
    const p = spawnSync(exe, args, {encoding:'utf8', timeout, maxBuffer:32 * 1024 * 1024});
    assert.equal(p.status, 0, `${exe} ${args.join(' ')}: ${p.status === null ? `killed by ${p.signal} after the ${timeout} ms child budget` : ''}${p.error ?? p.stderr ?? p.stdout}`);
    return p.stdout.replaceAll('\r\n', '\n');
}
const native = command(resolve(nativeAbi));
const wasm = command(process.execPath, new URL('../wasm/session-check.mjs', import.meta.url).pathname, resolve(wasmModule));
const jobNative = command(resolve(nativeJob));
const jobWasm = command(process.execPath, resolve(wasmJob));
const line = (text, key) => {
    const match = new RegExp(`^${key}=([^\n]+)$`, 'm').exec(text);
    assert.ok(match, `missing ${key}`);
    return match[1];
};
assert.equal(line(native, 'session-master-wav'), line(wasm, 'session-master-wav'), 'native/wasm downloaded WAV digest');
assert.equal(line(native, 'session-master-input'), line(wasm, 'session-master-input'), 'native/wasm synthetic input');
assert.equal(line(native, 'session-master-wav-header'), line(wasm, 'session-master-wav-header'), 'native/wasm WAV header');
assert.equal(line(jobNative, 'wav-outcomes'), line(jobWasm, 'wav-outcomes'), 'native/wasm cancel/refusal/miss outcomes');
assert.equal(line(jobNative, 'wav-outcomes'), 'cancel:false,refusal:false,unavailable:false,miss:true,unsafe:true');
// The delivery format is the target's: [target, dither.bits asked, deliveryRate asked, PCM bits and rate delivered].
const deliveryCases = [['cd', 0, 0, 16, 44100], ['cd', 16, 44100, 16, 44100], ['cdDynamic', 0, 0, 16, 44100],
                       ['allStreaming', 0, 0, 24, 48000], ['spotify', 24, 48000, 24, 48000]];
for (const [target, requested, requestedRate, bits, rate] of deliveryCases)
    assert.equal(line(native, `session-master-${target}-dither${requested}-rate${requestedRate}-wav`),
        line(wasm, `session-master-${target}-dither${requested}-rate${requestedRate}-wav`),
        `${target} ${rate} Hz PCM${bits} native/wasm WAV bytes`);
const contractBytes = line(wasm, 'wav-record');
let contract;
try { contract = JSON.parse(contractBytes); }
catch { throw new Error(`incomplete wav-record response (${contractBytes.length} bytes)`); }
assert.equal(contract.format, 2);
const scenarios = contract.scenarios;
for (const name of ['safe', 'formats', 'formatRefusals', 'warm', 'refusal', 'cancel', 'miss', 'unavailable', 'unsafe', 'lateCrest'])
    assert.ok(scenarios[name], `${name} has actual contract responses`);
const safePrice = scenarios.safe.price;
assert.equal(safePrice.sourcePcmBytes, contract.source.frames * contract.source.channels * 4);
assert.equal(safePrice.deliveredPcmBytes, safePrice.sourcePcmBytes);
assert.ok(safePrice.declared.bytes >= safePrice.deliveredPcmBytes
    && safePrice.declared.largestBlockBytes >= safePrice.deliveredPcmBytes
    && safePrice.declared.liveBytes + safePrice.declared.bytes
        >= safePrice.sourcePcmBytes + safePrice.deliveredPcmBytes);
assert.ok(safePrice.observedHeap.afterGrowthBytes > safePrice.observedHeap.beforeGrowthBytes);
assert.equal(safePrice.browser.playbackBytes, safePrice.deliveredPcmBytes);
assert.equal(safePrice.browser.wavBytes, scenarios.safe.export.bytes);
assert.ok(safePrice.browser.copyChunkBytes > 0 && safePrice.browser.copyChunkBytes <= 65536);
const latePrice = scenarios.lateCrest.price;
assert.equal(latePrice.sourcePcmBytes, scenarios.lateCrest.source.generator.frames * 2 * 4);
assert.ok(latePrice.deliveredPcmBytes > 0
    && latePrice.declared.bytes >= latePrice.deliveredPcmBytes
    && latePrice.declared.largestBlockBytes >= latePrice.deliveredPcmBytes
    && latePrice.declared.liveBytes + latePrice.declared.bytes
        >= latePrice.sourcePcmBytes + latePrice.deliveredPcmBytes);
assert.ok(latePrice.observedHeap.afterGrowthBytes > latePrice.observedHeap.beforeGrowthBytes);
assert.equal(latePrice.browser.wavBytes, scenarios.lateCrest.export.bytes);
const accepts = types(readFileSync(resolve(wasmModule.replace(/fcsession\.node\.js$/, 'snapshot.d.ts')), 'utf8'));
const checkWire = wire => {
    assert.ok(wire && typeof wire.jsonGzipBase64 === 'string'
        && typeof wire.rowsGzipBase64 === 'string');
    const json = gunzipSync(Buffer.from(wire.jsonGzipBase64, 'base64'));
    const rows = wire.rowsGzipBase64
        ? gunzipSync(Buffer.from(wire.rowsGzipBase64, 'base64')) : Buffer.alloc(0);
    assert.equal(json.length, wire.jsonBytes);
    assert.equal(rows.length, wire.rowBytes);
    assert.equal(sha(json), wire.jsonSha256);
    assert.equal(sha(rows), wire.rowsSha256);
    const value = JSON.parse(json);
    assert.ok(accepts(value, Array.isArray(value) ? 'ReadonlyArray<SessionEvent>' : 'SessionSnapshot'));
    const walk = x => {
        if (!x || typeof x !== 'object') return;
        if (Number.isInteger(x.byteOffset) && Number.isInteger(x.length) && Number.isInteger(x.stride))
            assert.ok(x.byteOffset >= 0 && x.byteOffset + x.length * x.stride * 8 <= rows.length,
                'replayable binary row stays inside the recorded payload');
        for (const v of Object.values(x)) walk(v);
    };
    walk(value);
};
const checkScenario = value => {
    if (!value || typeof value !== 'object') return;
    if ('jsonGzipBase64' in value || 'rowsGzipBase64' in value) { checkWire(value); return; }
    for (const child of Object.values(value)) checkScenario(child);
};
for (const scenario of Object.values(scenarios)) checkScenario(scenario);
const checkInputs = inputs => {
    assert.equal(Buffer.from(inputs.configBase64, 'base64').length, sizeOf('fc_master_config'));
    assert.equal(Buffer.from(inputs.paramsBase64, 'base64').length, sizeOf('fc_master_params'));
    assert.equal(sha(Buffer.from(inputs.configBase64, 'base64')), inputs.configSha256);
    assert.equal(sha(Buffer.from(inputs.paramsBase64, 'base64')), inputs.paramsSha256);
    assert.equal(sha(Buffer.from(JSON.stringify(inputs.call))), inputs.callSha256);
};
for (const name of ['safe', 'refusal', 'cancel', 'miss', 'unavailable', 'unsafe', 'lateCrest']) checkInputs(scenarios[name].inputs);
for (const item of [...scenarios.formats, ...scenarios.formatRefusals]) checkInputs(item.inputs);
for (const item of scenarios.warm.cycles) {
    checkInputs(item.inputs);
    assert.equal(item.accepted.kind, 'accepted');
    assert.equal(item.releaseStatus, 0);
    assert.equal(item.forgetAnswer.kind, 'accepted');
    assert.equal(sha(Buffer.from(JSON.stringify(item.forgetCommand))), item.forgetCommandSha256);
}
assert.equal(scenarios.warm.cycles.length, 3);
assert.equal(scenarios.warm.observedHeapBytes[2], scenarios.warm.observedHeapBytes[1]);
assert.deepEqual(scenarios.formats.map(x => [x.target, x.ditherBits, x.deliveryRate, x.bits, x.rate]), deliveryCases);
for (const item of scenarios.formats) {
    assert.equal(item.export.bits, item.bits);
    assert.equal(item.export.rate, item.rate);
    assert.equal(Buffer.from(item.export.headerHex, 'hex').readUInt32LE(24), item.rate);
}
assert.deepEqual(scenarios.formatRefusals.map(x => [x.target, x.ditherBits, x.deliveryRate, x.depth, x.targetRate]),
    [['allStreaming', 16, 0, 24, 48000], ['cd', 24, 0, 16, 44100], ['allStreaming', 32, 0, 24, 48000],
     ['allStreaming', 20, 0, 24, 48000], ['cd', -1, 0, 16, 44100], ['cd', 272, 0, 16, 44100],
     ['cd', 0, 48000, 16, 44100], ['allStreaming', 0, 44100, 24, 48000], ['cd', 0, 44100.5, 16, 44100]]);
for (const item of scenarios.formatRefusals) {
    assert.deepEqual(item.priced, {status:0, rejection:34, bytes:0});
    assert.equal(item.answer.kind, 'rejected');
    assert.equal(item.answer.code, 34);
}
assert.equal(scenarios.refusal.answer.kind, 'rejected');
assert.equal(scenarios.cancel.answer.kind, 'accepted');
assert.equal(scenarios.unavailable.answer.kind, 'rejected');
// Held short by the limiter's budget, proven on its active windows: TargetUnreachable (1), LimiterGainReduction (2), within twelve passes.
assert.equal(scenarios.miss.measurements.status, 1);
assert.equal(scenarios.miss.measurements.binding, 2);
assert.ok(scenarios.miss.measurements.passes <= 12);
assert.ok(Number.isFinite(scenarios.miss.measurements.missLu));
// No render under the ceiling still delivers the file, marked (owner, 01.10).
assert.equal(scenarios.unsafe.measurements.deliverable, true);
assert.equal(scenarios.unsafe.measurements.peaksAboveCeiling, true);
assert.equal(sha(Buffer.from(JSON.stringify(scenarios.cancel.command))), scenarios.cancel.commandSha256);
assert.equal(sha(Buffer.from(JSON.stringify(scenarios.miss.target))), scenarios.miss.targetSha256);
assert.equal(sha(Buffer.from(JSON.stringify(scenarios.unsafe.target))), scenarios.unsafe.targetSha256);
const spec = JSON.parse(readFileSync(input));
const pcm = Buffer.alloc(spec.frames * spec.channels * 4);
for (let channel = 0; channel < spec.channels; ++channel) {
    const rule = channel === 0 ? spec.left : spec.right;
    for (let i = 0; i < spec.frames; ++i)
        pcm.writeFloatLE(Math.fround(((i * rule.stride + rule.offset) % spec.period - rule.subtract)
            / spec.denominator), (channel * spec.frames + i) * 4);
}
let inputFnv = 0xcbf29ce484222325n;
for (const byte of pcm) inputFnv = BigInt.asUintN(64, (inputFnv ^ BigInt(byte)) * 0x100000001b3n);
assert.equal(line(native, 'session-master-input'), inputFnv.toString(16).padStart(16, '0'), 'recorded input is exercised');
assert.equal(contract.source.pcmSha256, sha(pcm));
assert.equal(contract.source.metadataSha256, sha(Buffer.from(contract.source.metadataJson)));
assert.equal(scenarios.unavailable.source.pcmSha256,
    sha(Buffer.from(scenarios.unavailable.source.pcmBase64, 'base64')));
assert.equal(scenarios.unavailable.source.metadataSha256,
    sha(Buffer.from(scenarios.unavailable.source.metadataJson)));
const unsafe = scenarios.unsafe.source;
const unsafePcm = Buffer.alloc(unsafe.generator.frames * unsafe.generator.channels * 4);
for (let channel = 0; channel < unsafe.generator.channels; ++channel)
    for (let i = 0; i < unsafe.generator.frames; ++i)
        unsafePcm.writeFloatLE(i + 1 === unsafe.generator.frames ? unsafe.generator.last : unsafe.generator.value,
            (channel * unsafe.generator.frames + i) * 4);
assert.equal(unsafe.pcmSha256, sha(unsafePcm));
assert.equal(unsafe.metadataSha256, sha(Buffer.from(unsafe.metadataJson)));
const late = scenarios.lateCrest, lateSource = late.source;
const latePcm = Buffer.alloc(lateSource.generator.frames * lateSource.generator.channels * 4);
for (let channel = 0; channel < lateSource.generator.channels; ++channel)
    for (let i = 0; i < lateSource.generator.frames; ++i) {
        let value = Math.fround((((i * (channel ? 19 : 17) + (channel ? 7 : 0)) % 251) - 125) / 4096);
        if (channel === 1 && i + 1 === lateSource.generator.frames)
            value = Math.fround(value + lateSource.generator.lastRightOffset);
        latePcm.writeFloatLE(value, (channel * lateSource.generator.frames + i) * 4);
    }
assert.equal(lateSource.pcmSha256, sha(latePcm));
assert.equal(lateSource.metadataSha256, sha(Buffer.from(lateSource.metadataJson)));
const recordedJson = wire => JSON.parse(gunzipSync(Buffer.from(wire.jsonGzipBase64, 'base64')));
const lateComplete = recordedJson(late.complete), lateReleased = recordedJson(late.release.snapshot);
const lateJoined = recordedJson(late.joined);
const lateId = lateComplete.pendingMaster.master;
const before = lateComplete.masters.find(x => x.id === lateId);
const after = lateJoined.masters.find(x => x.id === lateId);
assert.ok(before && after && before.report.crest.status === 0 && after.report.crest.status === 1);
assert.equal(lateReleased.pendingMaster.master, 0);
assert.equal(lateJoined.pendingMaster.master, 0);
assert.equal(before.recipe.source, lateJoined.source.hash);
assert.equal(after.recipe.readyHash, before.recipe.readyHash);
assert.equal(late.release.heapGrew, true);
assert.equal(before.report.checkPasses, 1);
assert.equal(after.report.checkPasses, 1);
assert.equal(after.report.crest.sourceRateCheck, true);
assert.equal(before.landing.passes, after.landing.passes);
assert.equal(before.report.achievedLufs, after.report.achievedLufs);
assert.equal(before.report.truePeakDbTp, after.report.truePeakDbTp);
assert.ok(recordedJson(late.sourceEvent).some(e => e.kind === 'measurement' && e.payload.analyzer === 9));
assert.ok(recordedJson(late.joinEvent).some(e => e.kind === 'fact' && e.jobId === lateId
    && e.payload.FactId === 18));
assert.equal(late.export.sizeStatus, 0);
assert.equal(late.export.sha256, late.export.postJoinSha256);
assert.equal(late.release.status, 0);
const header = Buffer.from(line(native, 'session-master-wav-header'), 'hex');
assert.equal(header.length, 44);
assert.equal(header.toString('ascii', 0, 4), 'RIFF');
assert.equal(header.readUInt32LE(24), spec.sampleRate);
assert.equal(header.readUInt16LE(34), spec.deliveryBits);
assert.equal(header.readUInt32LE(40), spec.frames * spec.channels * spec.deliveryBits / 8);
const expandedContract = JSON.parse(JSON.stringify(contract));
const expandWire = value => {
    if (!value || typeof value !== 'object') return;
    if (value.jsonGzipBase64) {
        value.json = gunzipSync(Buffer.from(value.jsonGzipBase64, 'base64')).toString('utf8');
        value.rowsBase64 = value.rowsGzipBase64
            ? gunzipSync(Buffer.from(value.rowsGzipBase64, 'base64')).toString('base64') : '';
        delete value.jsonGzipBase64;
        delete value.rowsGzipBase64;
        return;
    }
    for (const child of Object.values(value)) expandWire(child);
};
expandWire(expandedContract);
const archiveBytes = Buffer.from(JSON.stringify(expandedContract));
const archive = brotliCompressSync(archiveBytes, {params:{
    [zlibConstants.BROTLI_PARAM_QUALITY]:8, [zlibConstants.BROTLI_PARAM_LGWIN]:24}});
const recording = {
    format:2,
    rebuild:'node tools/contract/wav-contract.mjs <native-ABI-test> <native-job-test> <fcsession.node.js> <wasm-job-test.js> <fcore_session> --rebuild',
    command:'fc_session_master_wav_size → fc_session_master_wav_copy(offset, <=65536 bytes) → fc_session_master_audio_release; retain bytes by masterId',
    inputSpecSha256:sha(readFileSync(input)), inputPcmSha256:sha(pcm),
    versions:command(resolve(nativeCli), 'version').trim().split('\n')
        .filter(value => /^(felitronics-|fc_session_abi)/.test(value)),
    codecSchemaSha256:sha(readFileSync(new URL('../session-codec-schema.json', import.meta.url))),
    wasmSha256:sha(readFileSync(resolve(wasmModule.replace(/\.js$/, '.wasm')))),
    wavFnv64:line(native, 'session-master-wav'), wavHeaderHex:header.toString('hex'),
    outcomes:line(jobNative, 'wav-outcomes').split(','),
    scenarios:Object.keys(scenarios),
    refusalCodes:{invalidInput:scenarios.refusal.answer.code,
        unavailable:scenarios.unavailable.answer.code, unsafe:scenarios.unsafe.measurements.status},
    missMeasurements:{status:scenarios.miss.measurements.status,
        passes:scenarios.miss.measurements.passes, achievedLufs:scenarios.miss.measurements.achievedLufs,
        missLu:scenarios.miss.measurements.missLu, truePeakDbTp:scenarios.miss.measurements.truePeakDbTp},
    memory:{safePrice:safePrice.declared, latePrice:latePrice.declared,
        sourcePcmBytes:safePrice.sourcePcmBytes,
        deliveredPcmBytes:safePrice.deliveredPcmBytes,
        browser:safePrice.browser, observedHeap:safePrice.observedHeap,
        lateObservedHeap:latePrice.observedHeap},
    contractArchive:{codec:'br+base64', bytes:archiveBytes.length, sha256:sha(archiveBytes),
        base64:archive.toString('base64')}
};
const bytes = Buffer.from(JSON.stringify(recording, null, 2) + '\n');
if (option === '--rebuild') writeFileSync(file, bytes);
else {
    const saved = JSON.parse(readFileSync(file));
    assert.deepEqual({...saved, contractArchive:null}, {...recording, contractArchive:null},
        `stale WAV contract recording; rebuild: ${recording.rebuild}`);
    assert.equal(sha(Buffer.from(JSON.stringify(decodeWavRecording(saved)))), sha(archiveBytes),
        `stale WAV contract responses; rebuild: ${recording.rebuild}`);
}
console.log(`WAV contract: native/wasm identical ${recording.wavFnv64}; repeat, release and outcomes verified`);
