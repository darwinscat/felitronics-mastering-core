// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {decodeWavRecording} from './wav-recording.mjs';

const saved = JSON.parse(readFileSync(new URL('recordings/wav-contract.json', import.meta.url)));
const contract = decodeWavRecording(saved);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const wire = response => {
    assert.equal(typeof response.json, 'string');
    assert.equal(typeof response.rowsBase64, 'string');
    const json = Buffer.from(response.json), rows = Buffer.from(response.rowsBase64, 'base64');
    assert.equal(json.length, response.jsonBytes);
    assert.equal(rows.length, response.rowBytes);
    assert.equal(sha(json), response.jsonSha256);
    assert.equal(sha(rows), response.rowsSha256);
    const value = JSON.parse(json);
    const visit = entry => {
        if (!entry || typeof entry !== 'object') return;
        if (Number.isInteger(entry.byteOffset) && Number.isInteger(entry.length)
            && Number.isInteger(entry.stride))
            assert.ok(entry.byteOffset >= 0
                && entry.byteOffset + entry.length * entry.stride * 8 <= rows.length);
        for (const child of Object.values(entry)) visit(child);
    };
    visit(value);
    return value;
};
const inputs = value => {
    assert.equal(sha(Buffer.from(value.configBase64, 'base64')), value.configSha256);
    assert.equal(sha(Buffer.from(value.paramsBase64, 'base64')), value.paramsSha256);
    assert.equal(sha(Buffer.from(JSON.stringify(value.call))), value.callSha256);
};
const {scenarios} = contract;
assert.deepEqual(Object.keys(scenarios), saved.scenarios);
assert.equal(contract.source.pcmSha256, saved.inputPcmSha256);
for (const name of ['safe', 'refusal', 'cancel', 'miss', 'unavailable', 'unsafe']) inputs(scenarios[name].inputs);
assert.equal(scenarios.refusal.answer.kind, 'rejected');
assert.ok(wire(scenarios.refusal.snapshot).masters.length === 0);
assert.equal(scenarios.cancel.answer.kind, 'accepted');
assert.equal(sha(Buffer.from(JSON.stringify(scenarios.cancel.command))), scenarios.cancel.commandSha256);
assert.equal(wire(scenarios.cancel.snapshot).pendingMaster.master, 0);
assert.equal(scenarios.unavailable.answer.kind, 'rejected');
assert.equal(wire(scenarios.unavailable.snapshot).pendingMaster.master, 0);
assert.equal(scenarios.unsafe.measurements.deliverable, true);
assert.equal(scenarios.unsafe.measurements.peaksAboveCeiling, true);
assert.ok(wire(scenarios.unsafe.snapshot).pendingMaster.master > 0);
assert.ok(wire(scenarios.safe.readyEvents).length > 0);
assert.ok(wire(scenarios.safe.complete).pendingMaster.master > 0);
assert.equal(scenarios.safe.export.bits, 24);
assert.equal(scenarios.safe.export.firstSliceHex, scenarios.safe.export.repeatedSliceHex);
const price = scenarios.safe.price;
assert.equal(price.sourcePcmBytes, contract.source.frames * contract.source.channels * 4);
assert.equal(price.browser.playbackBytes, price.deliveredPcmBytes);
assert.equal(price.browser.wavBytes, scenarios.safe.export.bytes);
assert.ok(price.declared.bytes >= price.deliveredPcmBytes
    && price.declared.largestBlockBytes >= price.deliveredPcmBytes
    && price.observedHeap.afterGrowthBytes > price.observedHeap.beforeGrowthBytes);
assert.deepEqual(saved.memory.safePrice, price.declared);
const latePrice = scenarios.lateCrest.price;
assert.deepEqual(saved.memory.latePrice, latePrice.declared);
assert.ok(latePrice.sourcePcmBytes + latePrice.deliveredPcmBytes
    <= latePrice.declared.liveBytes + latePrice.declared.bytes);
assert.ok(latePrice.observedHeap.afterGrowthBytes > latePrice.observedHeap.beforeGrowthBytes);
assert.equal(scenarios.safe.release.status, 0);
assert.equal(wire(scenarios.safe.release.snapshot).pendingMaster.master, 0);
assert.deepEqual(scenarios.formats.map(x => [x.target, x.ditherBits, x.deliveryRate, x.bits, x.rate]),
    [['cd', 0, 0, 16, 44100], ['cd', 16, 44100, 16, 44100], ['cdDynamic', 0, 0, 16, 44100],
     ['allStreaming', 0, 0, 24, 48000], ['spotify', 24, 48000, 24, 48000]]);
for (const item of scenarios.formats) {
    assert.equal(item.export.rate, item.rate);
    assert.equal(Buffer.from(item.export.headerHex, 'hex').readUInt32LE(24), item.rate);
    const master = wire(item.complete).masters.at(-1);
    assert.equal(master.report.checkPasses, item.rate === 48000 ? 0 : 1);
}
for (const item of scenarios.formatRefusals) {
    inputs(item.inputs);
    assert.deepEqual(item.priced, {status:0, rejection:34, bytes:0});
    assert.equal(item.answer.kind, 'rejected');
    assert.equal(item.answer.code, 34);
    const fact = wire(item.events).find(e => e.kind === 'fact' && e.payload.FactId === 134);
    assert.equal(fact.payload.args[0].integer, String(item.depth));
    assert.equal(fact.payload.args[1].number, item.targetRate);
}
assert.equal(scenarios.warm.cycles.length, 3);
assert.equal(scenarios.warm.observedHeapBytes[2], scenarios.warm.observedHeapBytes[1]);
for (const cycle of scenarios.warm.cycles) {
    inputs(cycle.inputs);
    assert.equal(cycle.accepted.kind, 'accepted');
    assert.equal(cycle.releaseStatus, 0);
    assert.equal(cycle.forgetAnswer.kind, 'accepted');
    assert.equal(sha(Buffer.from(JSON.stringify(cycle.forgetCommand))), cycle.forgetCommandSha256);
    assert.equal(wire(cycle.after).pendingMaster.master, 0);
}
for (const item of scenarios.formats) {
    inputs(item.inputs);
    assert.ok(wire(item.complete).pendingMaster.master > 0);
    assert.equal(item.export.bits, item.bits);
    assert.equal(item.export.repeatStatus, 0);
    assert.equal(item.release.status, 0);
    assert.equal(wire(item.release.snapshot).pendingMaster.master, 0);
}
const miss = wire(scenarios.miss.snapshot).masters.at(-1).landing;
assert.equal(sha(Buffer.from(JSON.stringify(scenarios.miss.target))), scenarios.miss.targetSha256);
assert.equal(sha(Buffer.from(JSON.stringify(scenarios.unsafe.target))), scenarios.unsafe.targetSha256);
// Held short by the limiter's budget: TargetUnreachable (1) with LimiterGainReduction (2) bound, within the twelve passes.
assert.equal(miss.status, 1);
assert.equal(miss.binding, 2);
assert.ok(miss.passes <= 12);
assert.equal(miss.deliverable, true);
assert.ok(Number.isFinite(miss.missLu) && miss.truePeakDbTp <= -6);
assert.equal(scenarios.miss.export.repeatStatus, 0);
assert.equal(scenarios.miss.export.releaseStatus, 0);
assert.equal(wire(scenarios.miss.afterRelease).pendingMaster.master, 0);
console.log(`WAV recording replays ${Object.keys(scenarios).length} codec scenarios without running an engine`);
