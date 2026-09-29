// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {parse} from './grammar.mjs';
import {types} from '../session-wire-types.mjs';

export async function runWasm(modulePath, scriptPath, {reorder = false, corruptRows = false, corruptField = false} = {}) {
    const instructions = parse(readFileSync(scriptPath, 'utf8'));
    if (!instructions.length) return Buffer.from('done 0\n');
    const directory = dirname(resolve(modulePath));
    const runtime = await import(pathToFileURL(join(directory, 'snapshot.mjs')));
    const accepts = types(readFileSync(join(directory, 'snapshot.d.ts'), 'utf8'));
    const version = runtime.FC_SESSION_CONFIG_VERSION;
    const low = Number.parseInt(version.slice(8), 16), high = Number.parseInt(version.slice(0, 8), 16);
    const require = createRequire(import.meta.url);
    const production = require(resolve(modulePath));
    const failing = instructions.some(i => i.op === 'poison') ? require(join(directory, 'contract-trap/fcsession.node.js')) : production;
    let M = await failing(), poisoned = false, current = '', swapped = false, corrupted = false, fieldCorrupted = false, previousSource = '0';
    const sessions = new Map(), projects = new Map(), records = [];
    const header = readFileSync(new URL('../fc_session_abi.h', import.meta.url), 'utf8');
    const macro = name => Number(new RegExp(`^#define FC_SESSION_${name} ([0-9]+)u$`, 'm').exec(header)[1]);
    const status = Object.fromEntries([...header.matchAll(/^\s*FC_SESSION_(OK|ERR_[A-Z_]+)\s*=\s*([0-9]+)/gm)].map(m => [m[1], Number(m[2])]));
    const answerBytes = macro('ANSWER_BYTES');
    const record = (kind, bytes, rows = '') => records.push(`${current}\t${kind}\t${bytes}\t${rows}\n`);
    const call = (name, ...args) => runtime.invokeSession(M[`_fc_session_${name}`], ...args);
    const ok = (name, ...args) => assert.equal(call(name, ...args), 0, name);
    const heap = () => new Uint8Array(M.HEAPU32.buffer);
    const read = pointer => M.HEAPU32[pointer >>> 2];
    const write = (pointer, value) => { M.HEAPU32[pointer >>> 2] = value; };
    const double = pointer => new DataView(M.HEAPU32.buffer).getFloat64(pointer, true);
    const putDouble = (pointer, value) => new DataView(M.HEAPU32.buffer).setFloat64(pointer, value, true);
    function capabilities(alloc, ceiling) {
        const p = alloc(macro('CAPABILITIES_V1_BYTES'));
        write(p, macro('CAPABILITIES_V1_BYTES')); putDouble(p + 8, ceiling);
        write(p + 16, 96000); write(p + 20, macro('DEVICES_ALL')); putDouble(p + 24, ceiling);
        return p;
    }
    function copy(what, alloc) {
        const sizes = alloc(macro('SIZES_V1_BYTES')); write(sizes, macro('SIZES_V1_BYTES'));
        ok(`${what}_size`, handle(), sizes); assert.equal(read(sizes), macro('SIZES_V1_BYTES'));
        const jsonBytes = read(sizes + 4), rowBytes = read(sizes + 8);
        const j = alloc(jsonBytes), r = rowBytes ? alloc(rowBytes) : 0;
        ok(`${what}_copy`, handle(), j, jsonBytes, r, rowBytes);
        return {json:Buffer.from(heap().subarray(j, j + jsonBytes)).toString('utf8'), rows:heap().slice(r, r + rowBytes)};
    }
    function demand(alloc, name, ...args) {
        const before = [copy('events', alloc), copy('snapshot', alloc)];
        const out = alloc(macro('STORAGE_V1_BYTES')); write(out, macro('STORAGE_V1_BYTES'));
        ok(`${name}_bytes`, handle(), ...args, out);
        assert.equal(read(out), macro('STORAGE_V1_BYTES'));
        for (const offset of [8, 16, 24]) assert.ok(Number.isSafeInteger(double(out + offset)) && double(out + offset) >= 0, 'exact storage bytes');
        // Replacement loads can free old residents before allocating a new block: the
        // largest request may exceed the additional live bytes needed at peak.
        assert.ok(double(out + 24) > 0, 'live session is priced');
        assert.deepEqual([copy('events', alloc), copy('snapshot', alloc)], before, 'demand preserves events and snapshot');
    }
    // Allocations and views belong to one call. Memory growth invalidates all old heap views.
    async function scoped(body) {
        const owned = [];
        const alloc = size => { const p = M._malloc(Math.max(1, size)); assert.ok(p); owned.push(p); return p; };
        const input = text => { const bytes = Buffer.from(text); const p = alloc(bytes.length); heap().set(bytes, p); return [p, bytes.length]; };
        try { return await body(alloc, input); } finally { if (!poisoned) for (const p of owned) M._free(p); }
    }
    const handle = () => { assert.ok(!poisoned && sessions.has(current), 'no usable current session'); return sessions.get(current); };
    function validateRows(value, binary) {
        if (value && typeof value === 'object') {
            if ('byteOffset' in value && 'length' in value && 'stride' in value) {
                assert.equal(value.byteOffset % 8, 0);
                // Transfer the owned bytes exactly as a worker does, then view the rows as f64.
                const rows = new Float64Array(binary, value.byteOffset, value.length * value.stride);
                assert.equal(rows.byteLength, value.length * value.stride * 8);
            } else for (const item of Object.values(value)) validateRows(item, binary);
        }
    }
    async function transfer(what) {
        await scoped((alloc) => {
            const copied = copy(what, alloc);
            let json = copied.json;
            const ownedRows = copied.rows;
            const value = JSON.parse(json);
            assert.ok(accepts(value, what === 'events' ? 'ReadonlyArray<SessionEvent>' : 'SessionSnapshot'), `${what} generated types`);
            validateRows(value, ownedRows.buffer);
            if (reorder && !swapped && what === 'events' && value.length >= 2) {
                const encodings = arrayEncodings(json);
                [encodings[0], encodings[1]] = [encodings[1], encodings[0]];
                json = `[${encodings.join(',')}]`; swapped = true;
            }
            if (corruptRows && !corrupted && what === 'snapshot' && ownedRows.length) {
                ownedRows[0] ^= 1; corrupted = true;
            }
            if (corruptField && !fieldCorrupted && what === 'snapshot') {
                json = json.replace(/"revision":"([0-9]+)"/, (_, value) => `"revision":"${BigInt(value) + 1n}"`);
                assert.ok(accepts(JSON.parse(json), 'SessionSnapshot'), 'control changed a valid field');
                fieldCorrupted = true;
            }
            const rows = Buffer.from(ownedRows).toString('hex');
            record(what, json, rows);
        });
    }
    async function answer(body) {
        await scoped((alloc, input) => {
            const out = alloc(answerBytes), written = alloc(4);
            body(alloc, input, out, written);
            const json = Buffer.from(heap().subarray(out, out + read(written))).toString('utf8');
            assert.ok(accepts(JSON.parse(json), 'CommandAnswer'), 'answer generated types'); record('answer', json);
        });
        await transfer('events');
    }
    try {
        assert.equal(M._fc_session_abi_version(), runtime.FC_SESSION_ABI_VERSION);
        for (const {op, args:a, line} of instructions) {
            try {
                if (op === 'new') {
                    if (!poisoned) for (const h of sessions.values()) ok('destroy', h);
                    M = await production(); sessions.clear(); current = ''; poisoned = false; continue;
                }
                assert.ok(!poisoned, 'poisoned instance: use new instance');
                if (op === 'create') {
                    current = a[0]; assert.ok(!sessions.has(current), 'duplicate session');
                    await scoped(alloc => {
                        const caps = capabilities(alloc, Number(a[1])), out = alloc(8);
                        ok('create_bytes', caps, out);
                        assert.ok(Number.isSafeInteger(double(out)) && double(out) > 0, 'pre-create demand');
                        write(out, 777);
                        const result = call('create', caps, low, high, out);
                        assert.ok(result === status.OK || result === status.ERR_MEMORY, `create: ${result}`); record('create', String(result));
                        if (!result) sessions.set(current, read(out)); else assert.equal(read(out), 777);
                    }); continue;
                }
                if (op === 'use') { current = a[0]; handle(); continue; }
                if (op === 'snapshot' || op === 'summary') { await transfer(op); continue; }
                if (op === 'poison') {
                    handle();
                    await scoped(alloc => {
                        const caps = capabilities(alloc, 67108864), out = alloc(macro('SIZES_V1_BYTES'));
                        write(out, 777); M._contract_arm_trap();
                        assert.equal(call('create', caps, low, high, out), status.ERR_TRAP, 'allocation really trapped');
                        poisoned = true; assert.equal(read(out), 777);
                        write(out, macro('SIZES_V1_BYTES')); write(out + 4, 123); write(out + 8, 456);
                        assert.equal(call('snapshot_size', sessions.get(current), out), status.ERR_POISONED);
                        assert.equal(read(out), macro('SIZES_V1_BYTES')); assert.equal(read(out + 4), 123); assert.equal(read(out + 8), 456);
                        assert.equal(call('create', 0, 0, 0, 0), status.ERR_POISONED);
                        record('poison', JSON.stringify({trap:status.ERR_TRAP, status:status.ERR_POISONED, untouched:true}));
                    }); continue;
                }
                if (op === 'capacity') {
                    await scoped(alloc => {
                        const p = alloc(macro('CAPACITY_V1_BYTES')); write(p, macro('CAPACITY_V1_BYTES'));
                        putDouble(p + 8, Number(a[0])); putDouble(p + 16, Number(a[1]));
                        ok('set_capacity', handle(), p); record('capacity', '0');
                    }); continue;
                }
                if (op === 'query') {
                    await scoped((alloc, input) => {
                        const source = JSON.parse(copy('snapshot', alloc).json).source.hash;
                        const request = a[0].replace('$source', source).replace('$previous', previousSource);
                        const bytes = input(request); demand(alloc, 'query', ...bytes);
                        const sizes = alloc(macro('SIZES_V1_BYTES')); write(sizes, macro('SIZES_V1_BYTES'));
                        ok('query_size', handle(), ...bytes, sizes);
                        const maxJson = read(sizes + 4), maxRows = read(sizes + 8);
                        const json = alloc(maxJson), rows = maxRows ? alloc(maxRows) : 0;
                        const actual = alloc(macro('SIZES_V1_BYTES')); write(actual, macro('SIZES_V1_BYTES'));
                        ok('query_copy', handle(), ...bytes, json, maxJson, rows, maxRows, actual);
                        const jsonBytes = read(actual + 4), rowBytes = read(actual + 8);
                        assert.ok(jsonBytes <= maxJson && rowBytes <= maxRows && rowBytes % 8 === 0);
                        const raw = Buffer.from(heap().subarray(json, json + jsonBytes)).toString('utf8');
                        const binary = heap().slice(rows, rows + rowBytes);
                        const value = JSON.parse(raw);
                        assert.ok(accepts(value, 'QueryResponse'), 'query generated types');
                        validateRows(value, binary.buffer);
                        record('query', raw, Buffer.from(binary).toString('hex'));
                    }); continue;
                }
                if (op === 'step' || op === 'drive') {
                    let remaining = Number(a[0]);
                    do {
                        const count = op === 'drive' ? Math.min(remaining, 16) : remaining;
                        let state;
                        await scoped(alloc => { const out = alloc(4); ok('step', handle(), count, out); state = read(out); record('step', String(state)); });
                        await transfer('events');
                        remaining -= count;
                        if (op === 'step' || state === 1) break;
                    } while (remaining > 0);
                    continue;
                }
                if (op === 'export') {
                    await scoped(alloc => {
                        const size = alloc(4); ok('export_project_size', handle(), size);
                        const n = read(size), p = alloc(n); ok('export_project_copy', handle(), p, n, size);
                        assert.equal(read(size), n);
                        const text = Buffer.from(heap().subarray(p, p + n)); projects.set(a[0], text);
                        record('project', text.toString('hex'));
                    }); continue;
                }
                await answer((alloc, input, out, written) => {
                    if (op === 'command' || op === 'cancel') {
                        const json = op === 'command' ? a[0] : `{"kind":"cancel","commandId":"${a[0]}","jobId":${a[1]}}`;
                        const bytes = input(json); demand(alloc, 'command', ...bytes);
                        ok('command', handle(), ...bytes, out, answerBytes, written);
                    } else if (op === 'import' || op === 'import-file') {
                        assert.ok(op === 'import-file' || projects.has(a[1]), 'unknown project');
                        const project = op === 'import-file' ? readFileSync(join(dirname(scriptPath), '../fixtures', `${a[1]}.toml`)) : projects.get(a[1]);
                        const bytes = input(project); demand(alloc, 'import_project', ...bytes);
                        ok('import_project', handle(), Number(a[0]), 0, ...bytes, out, answerBytes, written);
                    } else if (op === 'load-measured') {
                        const facts = readFileSync(join(dirname(scriptPath), '../fixtures', `${a[1]}.json`), 'utf8');
                        assert.ok(accepts(JSON.parse(facts), 'MeasuredSource'), 'measured source generated types');
                        const bytes = input(facts); demand(alloc, 'load_measured', ...bytes);
                        ok('load_measured', handle(), Number(a[0]), 0, ...bytes, out, answerBytes, written);
                    } else if (op === 'load' || op === 'attach-audio') {
                        if (op === 'load') previousSource = JSON.parse(copy('snapshot', alloc).json).source.hash;
                        const words = readFileSync(join(dirname(scriptPath), '../fixtures', `${a[1]}.pcm`), 'utf8').trim().split(/\s+/).map(Number);
                        const [rate, channels, frames, ...samples] = words;
                        assert.ok(channels >= 1 && channels <= 2 && frames > 0 && frames <= 1000000);
                        assert.equal(samples.length, channels * frames);
                        assert.ok(samples.every(n => Number.isNaN(n) || Number.isInteger(n) && n >= -32768 && n <= 32767));
                        const pcm = alloc(samples.length * 4), pointers = alloc(channels * 4);
                        new Float32Array(M.HEAPU32.buffer, pcm, samples.length).set(samples.map(n => n / 32768));
                        for (let c = 0; c < channels; ++c) write(pointers + c * 4, pcm + c * frames * 4);
                        const meta = JSON.stringify({name:a[1], fileRate:rate, bitDepth:16, rateKnown:true});
                        if (op === 'load') {
                            const bytes = input(meta); demand(alloc, 'load', channels, frames, rate, ...bytes);
                            ok('load', handle(), Number(a[0]), 0, pointers, channels, frames, rate, ...bytes, out, answerBytes, written);
                        } else {
                            demand(alloc, 'attach_audio', channels, frames, rate);
                            ok('attach_audio', handle(), Number(a[0]), 0, pointers, channels, frames, rate, out, answerBytes, written);
                        }
                    } else throw new Error(`unknown operation ${op}`);
                });
            } catch (error) { throw new Error(`line ${line}: ${error.message}`, {cause:error}); }
        }
        assert.ok(!poisoned, 'scenario ended in a poisoned instance');
        for (const name of [...sessions.keys()].sort()) { current = name; await transfer('snapshot'); }
        if (reorder) assert.ok(swapped, 'reorder control found no batch with two events');
        if (corruptRows) assert.ok(corrupted, 'row control found no nonempty snapshot rows');
        if (corruptField) assert.ok(fieldCorrupted, 'field control found no snapshot');
        return Buffer.from(records.join(''));
    } finally { if (!poisoned) for (const h of sessions.values()) ok('destroy', h); }
}
// Keep original encodings for diagnostics and the reorder control. JSON.parse/stringify
// would hide a spelling difference; this scanner only locates top-level array elements.
export function arrayEncodings(json) {
    const result = []; let depth = 0, quoted = false, escaped = false, start = 1;
    for (let i = 1; i < json.length - 1; ++i) {
        const c = json[i];
        if (quoted) { if (escaped) escaped = false; else if (c === '\\') escaped = true; else if (c === '"') quoted = false; }
        else if (c === '"') quoted = true;
        else if (c === '[' || c === '{') ++depth;
        else if (c === ']' || c === '}') --depth;
        else if (c === ',' && depth === 0) { result.push(json.slice(start, i)); start = i + 1; }
    }
    if (json.slice(start, -1).trim()) result.push(json.slice(start, -1));
    return result;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try { process.stdout.write(await runWasm(process.argv[2], resolve(process.argv[3]))); }
    catch (error) { console.error(`contract wasm: ${error.message}`); process.exitCode = 2; }
}
