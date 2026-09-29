// SPDX-License-Identifier: AGPL-3.0-or-later
// Compare the retained needles scalars, histogram bins and codec bytes across native and Wasm.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

function fixture(executable) {
    const file = resolve(executable);
    const run = spawnSync(file.endsWith('.js') ? process.execPath : file,
        file.endsWith('.js') ? [file, '--fixture'] : ['--fixture'], {encoding: 'utf8', maxBuffer: 32 * 1024 * 1024});
    if (run.error) throw run.error;
    assert.equal(run.status, 0, run.stderr || run.stdout);
    return extract(run.stdout);
}
function extract(output) {
    const records = [];
    let record;
    for (const line of output.split(/\r?\n/)) {
        if (line.startsWith('fixture ')) {
            assert.equal(record, undefined, 'missing codec record');
            record = [line];
        } else if (record) {
            record.push(line);
            if (!line.startsWith('codec ')) continue;
            const snapshot = JSON.parse(line.slice(6));
            const result = snapshot.measurements.find(r => r.analyzer === 12);
            assert.equal(result?.status, 1, 'needles must be ready');
            assert.ok(result.numbers.length >= 27, 'missing scalars');
            assert.deepEqual(result.arrays.map(a => a.name), ['duration', 'ceiling', 'crest', 'classes', 'above']);
            assert.deepEqual(result.arrays.map(a => a.values.length), [2048, 2240, 90, 15, snapshot.source.channels]);
            assert.ok(result.arrays[0].values.some(v => v > 0), 'duration histogram is empty');
            assert.ok(result.numbers.some(n => n.name === 'runCount' && n.value > 0), 'fixture has no excursions');
            const [, rate, channels, ceiling, key] = record[0].split(' ');
            assert.equal(snapshot.source.sampleRate, Number(rate));
            assert.equal(snapshot.source.channels, Number(channels));
            assert.equal(result.numbers.find(n => n.name === 'thresholdDbTp').value, Number(ceiling));
            assert.equal(result.key, key);
            assert.equal(record.length, 2 + result.numbers.length + result.arrays.reduce((n, a) => n + a.values.length, 0));
            for (let i = 0; i < result.numbers.length; ++i)
                assert.match(record[i + 1], new RegExp(`^${result.numbers[i].name} [0-9a-f]{16}$`));
            for (const bin of record.slice(1 + result.numbers.length, -1)) assert.match(bin, /^[0-9a-f]{16}$/);
            records.push(record.join('\n'));
            record = undefined;
        }
    }
    assert.equal(record, undefined, 'unfinished fixture');
    const expected = [];
    for (const channels of [1, 2]) for (const rate of [8000, 48000, 192000]) for (const ceiling of [-6, -3.01, -12.75, -200])
        expected.push(`${rate} ${channels} ${ceiling}`);
    assert.deepEqual(records.map(r => r.split('\n')[0].split(' ').slice(1, 4).map(Number).join(' ')), expected,
        'all 24 rate/channel/ceiling fixtures must be present');
    return records.join('\n');
}
function same(native, wasm) { assert.ok(wasm === native, 'native/Wasm needles fixture differs'); }
function wiring(root) {
    const read = path => readFileSync(resolve(root, path), 'utf8');
    const workflow = read('.github/workflows/ci.yml').replace(/\\\r?\n/g, ' ');
    const debug = workflow.split('windows-debug:')[1].split('core-main:')[0];
    assert.match(debug, /felitronics_session_needles_tests/, 'Windows Debug must build needles allocation tests');
    assert.match(read('tools/tests/windows-debug.mjs'), /'session_needles'/, 'Windows Debug must select needles allocation tests');
    assert.match(workflow, /node tools\/tests\/needles-parity\.mjs build-native\/modules\/session\/felitronics_session_needles_tests\s+build-wasm-audio\/modules\/session\/felitronics_session_needles_tests\.js\s+build-wasm-checked\/modules\/session\/felitronics_session_needles_tests\.js/,
        'CI must compare native fixtures with both Wasm tiers');
    assert.match(read('modules/session/CMakeLists.txt'), /NAME felitronics_session_needles_parity_controls/, 'fixture controls must run in CTest');
    console.log('Needles tier wiring: Windows Debug build/selection and native/Wasm comparison present');
}
const [mode, ...args] = process.argv.slice(2);
if (mode === '--check-wiring') wiring(args[0] || '.');
else if (mode === '--self-test') {
    const data = fixture(args[0]);
    same(data, data);
    for (const mutation of [
        data.replace(/(thresholdLinear [0-9a-f]{15})([0-9a-f])/, (_, prefix, bit) => prefix + (bit === '0' ? '1' : '0')),
        data.replace(/\n[0-9a-f]{16}\n/, '\nffffffffffffffff\n'),
        data.replace(/"thresholdDbTp"/, '"changedThreshold"'),
        data.slice(data.indexOf('\nfixture ') + 1),
    ]) {
        assert.notEqual(mutation, data, 'mutation must land');
        assert.throws(() => same(data, mutation));
    }
    assert.throws(() => extract(''));
    assert.throws(() => extract(data.replace(/^codec .*$/m, '')));
    console.log('Needles parity controls: scalar, bin, codec, missing fixture, empty and incomplete output rejected');
} else {
    assert.ok(mode && !mode.endsWith('.js') && args.length > 0 && args.every(p => p.endsWith('.js')),
        'usage: needles-parity.mjs <native-executable> <wasm.js> [<checked-wasm.js>]');
    const native = fixture(mode);
    for (const wasm of args) same(native, fixture(wasm));
    console.log(`Needles parity: 24 fixtures, all scalars, histogram bins and codec bytes identical across ${1 + args.length} tiers`);
}
