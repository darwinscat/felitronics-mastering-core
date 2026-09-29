// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync, writeFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';

const [nativeAbi, nativeJob, wasmModule, wasmJob, nativeCli, option] = process.argv.slice(2);
if (!nativeAbi || !nativeJob || !wasmModule || !wasmJob || !nativeCli || (option && option !== '--rebuild')) {
    console.error('usage: node tools/contract/wav-contract.mjs <native-ABI-test> <native-job-test> <fcsession.node.js> <wasm-job-test.js> <fcore_session> [--rebuild]');
    process.exit(2);
}
const file = new URL('recordings/wav-contract.json', import.meta.url);
const input = new URL('wav-input.json', import.meta.url);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
function command(exe, ...args) {
    const p = spawnSync(exe, args, {encoding:'utf8', timeout:180000, maxBuffer:8 * 1024 * 1024});
    assert.equal(p.status, 0, `${exe} ${args.join(' ')}: ${p.error ?? p.stderr ?? p.stdout}`);
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
assert.equal(line(jobNative, 'wav-outcomes'), 'cancel:false,refusal:false,unavailable:false,miss:true,unsafe:false');
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
const header = Buffer.from(line(native, 'session-master-wav-header'), 'hex');
assert.equal(header.length, 44);
assert.equal(header.toString('ascii', 0, 4), 'RIFF');
assert.equal(header.readUInt32LE(24), spec.sampleRate);
assert.equal(header.readUInt16LE(34), spec.deliveryBits);
assert.equal(header.readUInt32LE(40), spec.frames * spec.channels * spec.deliveryBits / 8);
const recording = {
    format:1,
    rebuild:'node tools/contract/wav-contract.mjs <native-ABI-test> <native-job-test> <fcsession.node.js> <wasm-job-test.js> <fcore_session> --rebuild',
    command:'fc_session_master_wav_size → fc_session_master_wav_copy(offset, <=65536 bytes) → fc_session_master_audio_release; retain bytes by masterId',
    inputSpecSha256:sha(readFileSync(input)), inputPcmSha256:sha(pcm),
    versions:command(resolve(nativeCli), 'version').trim().split('\n')
        .filter(value => /^(felitronics-|fc_session_abi)/.test(value)),
    codecSchemaSha256:sha(readFileSync(new URL('../session-codec-schema.json', import.meta.url))),
    wasmSha256:sha(readFileSync(resolve(wasmModule.replace(/\.js$/, '.wasm')))),
    wavFnv64:line(native, 'session-master-wav'), wavHeaderHex:header.toString('hex'),
    checked:{master:true, export:true, repeat:true, release:true},
    outcomes:line(jobNative, 'wav-outcomes').split(',')
};
const bytes = Buffer.from(JSON.stringify(recording, null, 2) + '\n');
if (option === '--rebuild') writeFileSync(file, bytes);
else assert.deepEqual(readFileSync(file), bytes, `stale WAV contract recording; rebuild: ${recording.rebuild}`);
console.log(`WAV contract: native/wasm identical ${recording.wavFnv64}; repeat, release and outcomes verified`);
