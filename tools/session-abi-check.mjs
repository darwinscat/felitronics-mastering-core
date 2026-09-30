// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Generate a probe from the public surface, compile it on the tier being tested, then compare
// its output to the checked-in v1 floor. The floor is a subset: additions pass, changes do not.
import { readFileSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
const root = new URL('../', import.meta.url);
const read = path => readFileSync(new URL(path, root), 'utf8');
const clean = text => text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
const normalize = text => text.replace(/\s+/g, ' ').replace(/\s*([*,()])\s*/g, '$1').trim();
const header = clean(read('tools/fc_session_abi.h'));
const publicHeaders = ['Commands', 'Project', 'Events', 'Snapshot', 'Session', 'Text', 'Measurements', 'Queries'];
// EVERY ENTRY POINT, whatever it returns: each `fc_session_*(` the header declares must be read as a declaration, or the
// generator refuses — an entry point the probe skipped would never be frozen.
function entryPoints(text) {
    const found = [...text.matchAll(/\b(\w+)\s+(fc_session_\w+)\s*\(([^;]+)\);/g)];
    const read = new Set(found.map(m => m[2]));
    const missed = [...new Set([...text.matchAll(/\b(fc_session_\w+)\s*\(/g)].map(m => m[1]))].filter(name => !read.has(name));
    if (missed.length) throw Error(`entry points the manifest generator cannot read: ${missed.join(', ')}`);
    return found.map(m => ({ name: m[2], returns: m[1],
        args: m[3].trim() === 'void' ? [] : m[3].split(',').map(a => normalize(a.replace(/\b\w+\s*$/, ''))) }));
}
// THE STATUS UNION ON THE WIRE is fc_session_status's values, read here and in tools/session-codec.cmake from the
// header — never a second hand-written list. Every value is explicit.
function statusValues(text) {
    const m = /typedef enum fc_session_status\s*\{([^}]+)\}/.exec(text);
    if (!m) throw Error('tools/fc_session_abi.h declares no fc_session_status');
    return m[1].split(',').map(s => s.trim()).filter(Boolean).map(s => {
        const v = /^FC_SESSION_\w+\s*=\s*(\d+)$/.exec(s);
        if (!v) throw Error(`fc_session_status value without an explicit number: ${s}`);
        return v[1];
    });
}
// A WIRE RECORD THAT MIRRORS A C STRUCT (Session<Name> for fc_session_<name>) carries every field of it but the size.
function mirrors(text, schema) {
    const pascal = name => name.split('_').map(w => w[0].toUpperCase() + w.slice(1)).join('');
    for (const m of text.matchAll(/typedef struct fc_session_(\w+)\s*\{([^}]+)\}/g)) {
        const record = schema.wireRecords[`Session${pascal(m[1])}`];
        if (!record) continue;
        // The same names, in any order: a TypeScript interface has no layout (offsets are the C lines' business).
        const fields = [...m[2].matchAll(/([^;]+?)\s+(\w+)\s*;/g)].map(f => f[2]).filter(f => f !== 'size').sort();
        const wire = Object.keys(record).map(f => f.replace(/\?$/, '')).sort();
        if (fields.join() !== wire.join())
            throw Error(`wire record Session${pascal(m[1])} [${wire}] does not mirror fc_session_${m[1]} [${fields}]`);
    }
}
function generate(text = header) {
    const code = ['#include "fc_session_abi.h"', '#include <cstddef>', '#include <cstdio>', '#include <type_traits>', '#include "tests/SessionWireFacts.h"'];
    for (const h of publicHeaders) code.push(`#include <felitronics/session/${h}.h>`);
    const body = [];
    const literal = line => body.push(`std::puts(${JSON.stringify(line)});`);
    const number = (key, expr) => body.push(`std::printf(${JSON.stringify(`${key}=%llu\n`)}, static_cast<unsigned long long>(${expr}));`);
    for (const {name, returns, args} of entryPoints(text)) {
        code.push(`static_assert(std::is_same_v<decltype(&${name}), ${returns}(*)(${args.join(',')})>);`);
        literal(`function ${name} ${returns}(${args.join(',')})`);
    }
    for (const m of text.matchAll(/^#define[ \t]+(FC_SESSION_\w+)[ \t]+([^\n]+)/gm)) number(`define ${m[1]}`, m[1]);
    for (const m of text.matchAll(/typedef enum (\w+)\s*\{([^}]+)\}/g)) {
        number(`sizeof ${m[1]}`, `sizeof(${m[1]})`);
        for (const v of m[2].split(',').map(s => s.trim().split(/[ =]/)[0]).filter(Boolean)) number(`enum ${v}`, v);
    }
    for (const m of text.matchAll(/typedef\s+(uint\d+_t)\s+(fc_\w+)\s*;/g)) {
        literal(`typedef ${m[2]} ${m[1]}`);
        code.push(`static_assert(std::is_same_v<${m[2]}, ${m[1]}>);`);
    }
    number('sizeof fc_session', 'sizeof(fc_session)');
    for (const m of text.matchAll(/typedef struct (\w+)\s*\{([^}]+)\}/g)) {
        number(`sizeof ${m[1]}`, `sizeof(${m[1]})`);
        number(`alignof ${m[1]}`, `alignof(${m[1]})`);
        for (const f of m[2].matchAll(/([^;]+?)\s+(\w+)\s*;/g)) {
            literal(`field ${m[1]}.${f[2]} ${normalize(f[1])}`);
            number(`offset ${m[1]}.${f[2]}`, `offsetof(${m[1]},${f[2]})`);
        }
    }
    const schema = JSON.parse(read('tools/session-codec-schema.json'));
    // Only numeric values exposed by C or encoded on the wire belong to the floor.
    const observable = new Set([...Object.keys(schema.enums), ...Object.keys(schema.transportEnums),
        'Unit', 'Sign', 'Bound', 'FactId', 'Term', 'ArgKind']);
    for (const h of publicHeaders) {
        const text = clean(read(`modules/session/include/felitronics/session/${h}.h`));
        const ns = `felitronics::session::${h === 'Text' ? 'text::' : ''}`;
        for (const m of text.matchAll(/enum class (\w+)\s*:\s*[^{}]+\{([^}]+)\}/g))
            if (observable.has(m[1])) for (const v of m[2].split(',').map(s => s.trim().split(/[ =]/)[0]).filter(Boolean))
                number(`enum ${ns}${m[1]}::${v}`, `${ns}${m[1]}::${v}`);
    }
    for (const [name, record] of Object.entries(schema.records))
        for (const [field, type] of Object.entries(record.fields)) literal(`codec ${name}.${field} ${type}`);
    for (const [name, record] of Object.entries(schema.wireRecords))
        for (const [field, type] of Object.entries(record)) literal(`wire ${name}.${field} ${type}`);
    if ('SessionStatus' in schema.wireAliases) throw Error('SessionStatus is generated from fc_session_status; the schema must not list it');
    for (const [name, variants] of Object.entries({ SessionStatus: statusValues(text).join(' | '), ...schema.wireAliases }))
        for (const type of variants.split(' | ')) literal(`wire union ${name} ${type}`);
    for (const [name, values] of Object.entries(schema.transportEnums))
        values.forEach((value, i) => code.push(`static_assert(unsigned(felitronics::session::${name}::${value}) == ${i});`));
    for (const device of schema.enums.Device) {
        const fields = schema.records[`${device}FieldsValue`].fields;
        body.push(`{ ${device}Fields<Value> fields; detail::DeviceOf<decltype(fields)>::each(detail::rules(), [&](std::uint8_t id, const detail::FieldRule&, auto& value) {`);
        for (const field of Object.keys(fields))
            body.push(`if (static_cast<const void*>(&value) == static_cast<const void*>(&fields.${field})) std::printf("field-id ${device}.${field}=%u\\n", unsigned(id));`);
        body.push('}, fields); }');
    }
    for (const field of schema.binaryRows) literal(`binary SessionSnapshot.${field}`);
    literal('row EqPoint element=f64 byteOrder=little columns=hz,db stride=2 bytes=16');
    literal('row ReadingPoint element=f64 byteOrder=little columns=index,value stride=2 bytes=16');
    literal('row ReadingRun element=f64 byteOrder=little columns=first,count,value stride=3 bytes=24');
    literal('row MachineDifference element=f64 byteOrder=little columns=device,field,fileValue,coreValue stride=4 bytes=32');
    for (const [name, fields] of Object.entries(schema.queryRows)) literal(`row Query${name} element=f64 byteOrder=little columns=${fields.join(',')} stride=${fields.length} bytes=${fields.length * 8}`);
    number('row element sizeof', 'sizeof(double)');
    code.push('static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);');
    code.push('int main() {', ...body, 'return sessionWireFixture(true); }');
    return code.join('\n') + '\n';
}
// Keep documents in the floor readable, but compare paths instead of complete JSON lines.
// Row offsets may move when fields are added: compare the bytes each descriptor addresses.
const rawLines = text => text.split(/\r?\n/).filter(l => l && !l.startsWith('#'));
function lines(text) {
    const result = new Set(), input = rawLines(text);
    const fail = why => { throw Error(`frozen session ABI changed or disappeared: ${why}`); };
    for (let i = 0; i < input.length; ++i) {
        const line = input[i], named = /^json (\S+) (.*)$/.exec(line);
        if (!named && !/^[{[]/.test(line)) { result.add(line); continue; }
        let value;
        try { value = JSON.parse(named ? named[2] : line); } catch { fail('invalid JSON fixture'); }
        const name = named ? named[1] : Array.isArray(value) ? 'events' : 'commandId' in value ? `answer-${value.commandId}` : 'snapshot';
        const hex = /^[0-9a-f]+$/.test(input[i + 1] ?? '') ? input[++i] : '';
        const binary = Buffer.from(hex, 'hex');
        const walk = (v, path) => {
            const key = `json ${name} ${path}`, type = v === null ? 'null' : Array.isArray(v) ? 'array' : typeof v;
            result.add(`${key} type=${type}`);
            if (type !== 'object' && type !== 'array') { result.add(`${key} value=${JSON.stringify(v)}`); return; }
            const row = type === 'object' && ['byteOffset', 'length', 'stride'].every(k => Object.hasOwn(v, k));
            if (row) {
                if (![v.byteOffset, v.length, v.stride].every(Number.isSafeInteger) || v.byteOffset < 0 || v.byteOffset % 8
                    || v.length < 0 || v.stride <= 0 || v.byteOffset + v.length * v.stride * 8 > binary.length)
                    fail(`${key}: invalid row descriptor`);
                for (let j = 0; j < v.length * v.stride; ++j)
                    result.add(`row ${name} ${path}/${j}=${binary.subarray(v.byteOffset + j * 8, v.byteOffset + (j + 1) * 8).toString('hex')}`);
            }
            for (const [k, x] of Object.entries(v)) {
                const child = `${path}/${k.replaceAll('~', '~0').replaceAll('/', '~1')}`;
                if (row && k === 'byteOffset') result.add(`json ${name} ${child} type=number`);
                else walk(x, child);
            }
        };
        walk(value, '');
    }
    return result;
}
// FLOORS, not exact values: a boundary struct's size grows with an appended field, and the ABI version moves up by one
// with each batch of additions that lands together in one release (as fc_master's does), so the frozen line holds when
// the compiled value is at least it.
function floorKey(line, extensible) {
    const size = /^(sizeof (\w+))=(\d+)$/.exec(line);
    if (size && extensible.has(size[2])) return { key: size[1], value: Number(size[3]) };
    const version = /^(define FC_SESSION_ABI_VERSION)=(\d+)$/.exec(line);
    return version ? { key: version[1], value: Number(version[2]) } : null;
}
function check(floor, actual) {
    const extensible = new Set([...header.matchAll(/typedef struct (\w+)/g)].map(m => m[1]));
    const missing = [...floor].filter(line => {
        if (actual.has(line)) return false;
        const frozen = floorKey(line, extensible);
        if (!frozen) return true;
        return ![...actual].some(value => {
            const grown = floorKey(value, extensible);
            return grown && grown.key === frozen.key && grown.value >= frozen.value;
        });
    });
    // Existing offsets/types stay fixed; a new field belongs after the entire frozen prefix.
    for (const value of actual) {
        const added = /^offset (\w+)\.(\w+)=(\d+)$/.exec(value);
        if (!added || [...floor].some(line => line.startsWith(`offset ${added[1]}.${added[2]}=`))) continue;
        const oldSize = [...floor].find(line => line.startsWith(`sizeof ${added[1]}=`));
        if (oldSize && Number(added[3]) < Number(oldSize.split('=')[1])) missing.push(`new field overlaps frozen prefix: ${value}`);
    }
    if (missing.length) throw Error(`frozen session ABI changed or disappeared:\n${missing.join('\n')}`);
}
const args = process.argv.slice(2);
const schemaNow = JSON.parse(read('tools/session-codec-schema.json'));
if (args[0] === '--generate') {
    writeFileSync(args[1], generate());
} else if (args[0] === '--self-test') {
    mirrors(header, schemaNow);
    const floor = lines(read('tools/session-abi-v1.txt'));
    check(floor, floor);
    check(floor, new Set([...floor, 'function fc_session_future fc_session_status()']));
    // The version is a floor: it grows with each release that adds, and only a lower number is a change.
    const version = [...floor].find(line => line.startsWith('define FC_SESSION_ABI_VERSION='));
    const frozenVersion = Number(version.split('=')[1]), without = [...floor].filter(line => line !== version);
    check(floor, new Set([...without, `define FC_SESSION_ABI_VERSION=${frozenVersion + 1}`]));
    assert.throws(() => check(floor, new Set([...without, `define FC_SESSION_ABI_VERSION=${frozenVersion - 1}`])), /changed or disappeared/);
    // An entry point with any return type is frozen; one the generator cannot read stops it.
    const probe = generate(`${header}\ndouble fc_session_future_measure (fc_session session, const double* at);\n`);
    assert(probe.includes('function fc_session_future_measure double(fc_session,const double*)'), 'a double-returning entry point is frozen');
    assert.throws(() => generate(`${header}\nconst char* fc_session_future_name (fc_session session);\n`), /cannot read: fc_session_future_name/);
    // The status union follows the header: an appended status is on the wire at once, a hand list in the schema is refused.
    // Appended after the header's LAST status, whichever that is today.
    const last = Math.max(...statusValues(header).map(Number));
    const lastStatus = new RegExp(`(FC_SESSION_\\w+\\s*=\\s*${last})(\\s*\\})`);
    const appended = generate(header.replace(lastStatus, `$1,\n    FC_SESSION_ERR_FUTURE = ${last + 1}$2`));
    assert(appended.includes(`"wire union SessionStatus ${last + 1}"`) && generate().includes(`"wire union SessionStatus ${last}"`)
        && !generate().includes(`"wire union SessionStatus ${last + 1}"`), 'the wire status union is the header\'s');
    assert.throws(() => statusValues(header.replace(new RegExp(`(FC_SESSION_\\w+)\\s*=\\s*${last}\\b`), '$1')), /explicit number/);
    // A wire record mirroring a C struct carries each of its fields: dropping one is refused.
    const dropped = structuredClone(schemaNow); delete dropped.wireRecords.SessionCapabilities.largestFreeBlockBytes;
    assert.throws(() => mirrors(header, dropped), /does not mirror fc_session_capabilities/);
    console.log('session ABI generator controls: the version floor grows, any entry point is frozen, the status union and mirrored records follow the header');
    for (const line of floor) {
        const missing = new Set(floor); missing.delete(line);
        assert.throws(() => check(floor, missing), /changed or disappeared/);
        missing.add(`${line} CHANGED`);
        assert.throws(() => check(floor, missing), /changed or disappeared/);
    }
    const raw = rawLines(read('tools/session-abi-v1.txt'));
    const at = raw.findIndex(line => line.startsWith('{'));
    const snapshot = JSON.parse(raw[at]);
    const mutated = change => {
        const doc = structuredClone(snapshot), copy = [...raw]; change(doc, copy);
        copy[at] = JSON.stringify(doc); return lines(copy.join('\n'));
    };
    check(floor, mutated(doc => { doc.future = { enabled: true }; doc.project.future = 42; }));
    // Insert an additional binary row before all existing rows and adjust their offsets.
    check(floor, mutated((doc, copy) => {
        const shift = v => { if (!v || typeof v !== 'object') return; if ('byteOffset' in v) v.byteOffset += 16; else Object.values(v).forEach(shift); };
        shift(doc); doc.futureRows = { byteOffset: 0, length: 1, stride: 2 };
        copy[at + 1] = '000000000000f03f0000000000000040' + copy[at + 1];
    }));
    for (const change of [doc => { doc.renamed = doc.handFieldCount; delete doc.handFieldCount; },
        doc => { delete doc.project; }, doc => { doc.handFieldCount = String(doc.handFieldCount); },
        doc => { doc.handFieldCount++; }, doc => { doc.eqCurve.byteOffset += 8; },
        doc => { doc.eqCurve.stride++; }])
        assert.throws(() => check(floor, mutated(change)), /changed or disappeared/);
    assert(![...floor].some(line => /enum felitronics::session::(?:Command|Event|Column|Status|CodecStatus|EventKind|text::(?:Lang|Plural))::/.test(line)),
        'C++-only enum ordinals are not a permanent contract');
    console.log('session ABI JSON controls: nested additions and relocated rows GREEN; rename/removal/type/value/descriptor changes RED');
    console.log(`session ABI control: deletion and change go red for all ${floor.size} lines; additions pass`);
} else {
    if (!args.length) throw Error('usage: session-abi-check.mjs [--generate probe.cpp | --self-test | executable [args...]]');
    mirrors(header, schemaNow);
    const result = spawnSync(args[0], args.slice(1), {encoding: 'utf8', maxBuffer: 1024 * 1024});
    if (result.status !== 0) throw Error(result.stderr || String(result.error));
    check(lines(read('tools/session-abi-v1.txt')), lines(result.stdout));
    console.log(`session ABI v1: ${lines(result.stdout).size} compiled surface lines agree with the frozen floor`);
}
