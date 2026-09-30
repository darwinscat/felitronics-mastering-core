// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import assert from 'node:assert/strict';
import {readFileSync, readdirSync, mkdtempSync, cpSync, rmSync, appendFileSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve, basename} from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {fixtures, fixtureRoot} from './fixtures.mjs';
import {parse} from './grammar.mjs';
import {runWasm, arrayEncodings} from './wasm.mjs';
import {lfBytes, recordings} from './recordings.mjs';
import {childTimeout} from './child-timeout.mjs';
import {createHash} from 'node:crypto';

const root = fileURLToPath(new URL('scenarios/', import.meta.url));
const scenarioFiles = entries => entries.filter(f => f.endsWith('.session') && !f.startsWith('._')).sort();
export function records(bytes) {
    return bytes.toString('utf8').trimEnd().split('\n').map(line => {
        const [session, kind, raw, rows = ''] = line.split('\t');
        return {session, kind, raw, rows};
    });
}
// These are actual allocation budgets for the host ABI: 64-bit native and
// 32-bit Wasm own differently sized C++ objects. Source and allocator bytes
// have the same meaning and remain byte-compared. Per-tier budget tests check
// the masked numbers against allocations rather than unlike pointer widths.
function platformBudgets(bytes) {
    return Buffer.from(bytes.toString('utf8')
        .replace(/"measurementStorage":\{[^{}]*\}/g, object => object.replace(/"([^"]+)":([0-9]+(?:\.[0-9]+)?)/g,
            (field, key) => key === 'sourceBytes' || key === 'allocatorBytes' ? field : `"${key}":0`))
        .replace(/"needlesBytes":[0-9]+(?:\.[0-9]+)?/g, '"needlesBytes":0')
        .replace(/"needlesLargestBlockBytes":[0-9]+(?:\.[0-9]+)?/g, '"needlesLargestBlockBytes":0')
        .replace(/"needBytes":[0-9]+(?:\.[0-9]+)?/g, '"needBytes":0'));
}
function eventRows(raw, hex) {
    const rows = [];
    function visit(value) {
        if (!value || typeof value !== 'object') return;
        if ('byteOffset' in value && 'length' in value && 'stride' in value) {
            rows.push(hex.slice(value.byteOffset * 2, (value.byteOffset + value.length * value.stride * 8) * 2));
        } else for (const child of Object.values(value)) visit(child);
    }
    if (raw) visit(JSON.parse(raw));
    return rows;
}
export function compare(name, native, wasm) {
    const leftBytes = platformBudgets(native), rightBytes = platformBudgets(wasm);
    if (leftBytes.equals(rightBytes)) return;
    const left = records(leftBytes), right = records(rightBytes); let eventIndex = 0;
    for (let i = 0; i < Math.max(left.length, right.length); ++i) {
        const a = left[i], b = right[i];
        if (JSON.stringify(a) !== JSON.stringify(b)) {
            if (a?.kind === 'events' && b?.kind === 'events') {
                const ae = arrayEncodings(a.raw), be = arrayEncodings(b.raw);
                for (let n = 0; n < Math.max(ae.length, be.length); ++n) {
                    if (ae[n] === be[n] && a.session === b.session
                        && JSON.stringify(eventRows(ae[n], a.rows)) === JSON.stringify(eventRows(be[n], b.rows))) continue;
                    const ak = ae[n] ? JSON.parse(ae[n]).kind : '<missing>';
                    const bk = be[n] ? JSON.parse(be[n]).kind : '<missing>';
                    throw new Error(`${name}: first differing event index ${eventIndex + n}, kind ${ak}/${bk}\nnative: ${ae[n] ?? '<missing>'}\nwasm:   ${be[n] ?? '<missing>'}\nnative rows: ${a.rows}\nwasm rows:   ${b.rows}`);
                }
            }
            throw new Error(`${name}: first differing ${a?.kind ?? b?.kind} record ${i}\nnative: ${JSON.stringify(a)}\nwasm:   ${JSON.stringify(b)}`);
        }
        if (a?.kind === 'events') eventIndex += arrayEncodings(a.raw).length;
    }
    throw new Error(`${name}: byte mismatch (including whitespace or final newline)`);
}
function validate(name, bytes) {
    const trace = records(bytes), expected = JSON.parse(readFileSync(join(root, `${name}.expect.json`), 'utf8'));
    const select = ({kind, session, index = 0}) => trace.filter(r => r.kind === kind && (!session || r.session === session)).at(index);
    for (const check of expected.checks) {
        const record = select(check); assert.ok(record, `${name}: missing ${JSON.stringify(check)}`);
        let value = JSON.parse(record.raw);
        for (const part of (check.path ?? '').split('.').filter(Boolean)) value = value[part];
        assert.deepEqual(value, check.value, `${name}: ${JSON.stringify(check)}`);
    }
    for (const [a, b] of expected.equal ?? []) assert.deepEqual(select(a), select(b), `${name}: refusal must leave snapshot unchanged`);
    for (const {a, b, path} of expected.samePath ?? []) {
        const value = spec => path.split('.').reduce((v, key) => v[key], JSON.parse(select(spec).raw));
        assert.deepEqual(value(a), value(b), `${name}: ${path} must be retained`);
    }
    if (expected.projects) {
        const texts = trace.filter(r => r.kind === 'project'); assert.equal(texts.length, expected.projects);
        for (const text of texts) assert.equal(text.raw, texts[0].raw, `${name}: project restore preserves canonical bytes`);
    }
    const events = trace.filter(r => r.kind === 'events').flatMap(r => JSON.parse(r.raw));
    assert.ok(events.length >= expected.minEvents, `${name}: expected real work, got ${events.length} events`);
    for (const kind of expected.eventKinds) assert.ok(events.some(e => e.kind === kind), `${name}: missing ${kind}`);
    const answers = trace.filter(r => r.kind === 'answer').map(r => JSON.parse(r.raw));
    assert.equal(answers.filter(a => a.kind === 'rejected').length, expected.rejections ?? 0, `${name}: unexpected command refusal`);
    assert.ok(trace.some(r => r.kind === 'snapshot'), `${name}: no final snapshot`);
}
// ONE BUDGET PER CHILD PROCESS, read by child-timeout.mjs as wav-contract.mjs reads its own. 30 s is ample for every scenario on an optimised build (the
// longest takes about a second) and a hang still ends quickly there. A sanitized build runs the same scenarios an order
// of magnitude slower, so tools/CMakeLists.txt raises it for that tier through FC_SESSION_CONTRACT_TIMEOUT_MS. Each
// child's output fits the buffer with room: the largest scenario writes under 7 MB.
const timeout = childTimeout(30000), maxBuffer = 64 * 1024 * 1024;
function nativeRun(cli, ...args) {
    const p = spawnSync(cli, args, {maxBuffer, timeout});
    if (p.status !== 0) throw new Error(`native ${args.join(' ')}: exit ${p.status}: ${p.error ?? ''}${p.stderr}`);
    return p.stdout;
}
function grammarTest(cli) {
    assert.deepEqual(scenarioFiles(['._measurement.session', 'measurement.session', 'readme.txt']), ['measurement.session']);
    const corpus = JSON.parse(readFileSync(new URL('grammar-cases.json', import.meta.url), 'utf8'));
    for (const item of corpus) {
        let parsed; try { parsed = parse(item.script); } catch { parsed = null; }
        const p = spawnSync(cli, ['parse', '-'], {input:item.script, encoding:'utf8', timeout, maxBuffer});
        assert.equal(p.status, item.valid ? 0 : 2, `native grammar: ${JSON.stringify(item.script)}: ${p.stderr}`);
        assert.equal(parsed !== null, item.valid, `wasm grammar: ${JSON.stringify(item.script)}`);
        const encoding = parsed?.map(i => [i.line, i.op, ...i.args].join('\t') + '\n').join('') ?? '';
        assert.equal(p.stdout, encoding, 'both parsers have identical instructions or no output on refusal');
    }
    console.log(`contract grammar: ${corpus.length} cases, both parsers`);
}
async function main() {
    const args = process.argv.slice(2), cli = resolve(args.shift() ?? '');
    const nativeOnly = args.includes('--native-only'), controls = args.includes('--controls');
    const reorder = args.includes('--reorder'), rewrite = args.includes('--rebuild-recordings');
    const modulePath = nativeOnly ? null : resolve(args.find(a => !a.startsWith('--')) ?? '');
    const start = performance.now(); fixtures(); grammarTest(cli);
    const names = scenarioFiles(readdirSync(root));
    const siteTraces = new Map();
    for (const file of names) {
        const name = basename(file, '.session'), path = join(root, file);
        const native = nativeRun(cli, 'run', path); validate(name, native);
        if (!nativeOnly) { const wasm = await runWasm(modulePath, path, {reorder}); compare(name, native, wasm); validate(name, wasm); siteTraces.set(name, wasm); }
        console.log(`contract ${name}: ${nativeOnly ? 'native assertions' : 'byte-identical native/wasm'} (${native.length} bytes)`);
    }
    if (!nativeOnly && !reorder) {
        const versions = {
            components:nativeRun(cli, 'version').toString('utf8').replace(/\r\n/g, '\n').trim().split('\n'),
            configVersion:nativeRun(cli, 'config', 'version').toString('utf8').trim(),
            codecSchemaSha256:createHash('sha256').update(lfBytes(fileURLToPath(new URL('../session-codec-schema.json', import.meta.url)))).digest('hex')
        };
        console.log(`contract recordings: ${recordings(siteTraces, versions, rewrite)} verified`);
    }
    if (controls) {
        assert.ok(!nativeOnly, 'controls require wasm');
        const control = spawnSync(process.execPath, [fileURLToPath(import.meta.url), cli, modulePath, '--reorder'],
            {encoding:'utf8', timeout, maxBuffer});
        assert.equal(control.status, 1, `reorder must exit red: ${control.error ?? control.stderr}`);
        assert.match(control.stderr, /first differing event index \d+, kind/);
        console.log(`CONTROL RED (exit ${control.status}): ${control.stderr.trim()}`);
        const file = join(root, 'measurement.session'), native = nativeRun(cli, 'run', file);
        const field = await runWasm(modulePath, file, {corruptField:true});
        assert.throws(() => compare('field control', native, field), /first differing snapshot/);
        const rows = await runWasm(modulePath, file, {corruptRows:true});
        assert.throws(() => compare('rows control', native, rows), /first differing snapshot/);
        const temp = mkdtempSync(join(tmpdir(), 'session-contract-'));
        try {
            cpSync(fixtureRoot, temp, {recursive:true}); appendFileSync(join(temp, 'inputs.json'), '\n');
            assert.throws(() => fixtures(temp), /stale fixture inputs; rebuild:/);
            cpSync(fixtureRoot, temp, {recursive:true}); appendFileSync(join(temp, 'mono.pcm'), '0\n');
            assert.throws(() => fixtures(temp), /stale fixture mono.pcm; rebuild:/);
            // A Windows checkout without the .gitattributes rule: the schema and a scenario arrive CRLF.
            for (const input of [new URL('../session-codec-schema.json', import.meta.url), new URL('scenarios/measurement.session', import.meta.url)]) {
                const lf = readFileSync(input), crlf = join(temp, 'crlf-copy');
                writeFileSync(crlf, lf.toString('latin1').replaceAll('\n', '\r\n'), 'latin1');
                assert.throws(() => lfBytes(crlf), /has CR line endings/);
                writeFileSync(crlf, lf); assert.deepEqual(lfBytes(crlf), lf);
            }
        } finally { rmSync(temp, {recursive:true, force:true}); }
        console.log('contract controls: event order, scalar field, binary rows, changed inputs, damaged PCM and CRLF copies of hashed text refused');
    }
    console.log(`contract: ${names.length} scenarios passed in ${((performance.now() - start) / 1000).toFixed(3)} s`);
}
try { await main(); } catch (error) { console.error(`contract FAIL: ${error.message}`); process.exitCode = 1; }
