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
const publicHeaders = ['Commands', 'Project', 'Events', 'Snapshot', 'Session', 'Text'];
function generate() {
    const code = ['#include "fc_session_abi.h"', '#include <cstddef>', '#include <cstdio>', '#include <type_traits>'];
    for (const h of publicHeaders) code.push(`#include <felitronics/session/${h}.h>`);
    const body = [];
    const literal = line => body.push(`std::puts(${JSON.stringify(line)});`);
    const number = (key, expr) => body.push(`std::printf(${JSON.stringify(`${key}=%llu\n`)}, static_cast<unsigned long long>(${expr}));`);
    for (const m of header.matchAll(/\b(uint32_t|fc_session_status)\s+(fc_session_\w+)\s*\(([^;]+)\);/g)) {
        const args = m[3].trim() === 'void' ? [] : m[3].split(',').map(a => normalize(a.replace(/\b\w+\s*$/, '')));
        const type = `${m[1]}(*)(${args.join(',')})`;
        code.push(`static_assert(std::is_same_v<decltype(&${m[2]}), ${type}>);`);
        literal(`function ${m[2]} ${m[1]}(${args.join(',')})`);
    }
    for (const m of header.matchAll(/^#define[ \t]+(FC_SESSION_\w+)[ \t]+([^\n]+)/gm)) number(`define ${m[1]}`, m[1]);
    for (const m of header.matchAll(/typedef enum (\w+)\s*\{([^}]+)\}/g)) {
        number(`sizeof ${m[1]}`, `sizeof(${m[1]})`);
        for (const v of m[2].split(',').map(s => s.trim().split(/[ =]/)[0]).filter(Boolean)) number(`enum ${v}`, v);
    }
    for (const m of header.matchAll(/typedef\s+(uint\d+_t)\s+(fc_\w+)\s*;/g)) {
        literal(`typedef ${m[2]} ${m[1]}`);
        code.push(`static_assert(std::is_same_v<${m[2]}, ${m[1]}>);`);
    }
    number('sizeof fc_session', 'sizeof(fc_session)');
    for (const m of header.matchAll(/typedef struct (\w+)\s*\{([^}]+)\}/g)) {
        number(`sizeof ${m[1]}`, `sizeof(${m[1]})`);
        number(`alignof ${m[1]}`, `alignof(${m[1]})`);
        for (const f of m[2].matchAll(/([^;]+?)\s+(\w+)\s*;/g)) {
            literal(`field ${m[1]}.${f[2]} ${normalize(f[1])}`);
            number(`offset ${m[1]}.${f[2]}`, `offsetof(${m[1]},${f[2]})`);
        }
    }
    for (const h of publicHeaders) {
        const text = clean(read(`modules/session/include/felitronics/session/${h}.h`));
        const ns = `felitronics::session::${h === 'Text' ? 'text::' : ''}`;
        for (const m of text.matchAll(/enum class (\w+)\s*:\s*[^{}]+\{([^}]+)\}/g))
            for (const v of m[2].split(',').map(s => s.trim().split(/[ =]/)[0]).filter(Boolean))
                number(`enum ${ns}${m[1]}::${v}`, `${ns}${m[1]}::${v}`);
    }
    const schema = JSON.parse(read('tools/session-codec-schema.json'));
    for (const [name, record] of Object.entries(schema.records))
        for (const [field, type] of Object.entries(record.fields)) literal(`codec ${name}.${field} ${type}`);
    for (const [name, record] of Object.entries(schema.wireRecords))
        for (const [field, type] of Object.entries(record)) literal(`wire ${name}.${field} ${type}`);
    for (const [name, variants] of Object.entries(schema.wireAliases))
        for (const type of variants.split(' | ')) literal(`wire union ${name} ${type}`);
    for (const [name, values] of Object.entries(schema.transportEnums))
        values.forEach((value, i) => code.push(`static_assert(unsigned(felitronics::session::${name}::${value}) == ${i});`));
    for (const field of schema.binaryRows) literal(`binary SessionSnapshot.${field}`);
    code.push('int main() {', ...body, 'return 0; }');
    return code.join('\n') + '\n';
}
const lines = text => new Set(text.split(/\r?\n/).filter(l => l && !l.startsWith('#')));
function check(floor, actual) {
    const missing = [...floor].filter(line => !actual.has(line));
    if (missing.length) throw Error(`frozen session ABI changed or disappeared:\n${missing.join('\n')}`);
}
const args = process.argv.slice(2);
if (args[0] === '--generate') {
    writeFileSync(args[1], generate());
} else if (args[0] === '--self-test') {
    const floor = lines(read('tools/session-abi-v1.txt'));
    check(floor, floor);
    check(floor, new Set([...floor, 'function fc_session_future fc_session_status()']));
    for (const line of floor) {
        const missing = new Set(floor); missing.delete(line);
        assert.throws(() => check(floor, missing), /changed or disappeared/);
        missing.add(`${line} CHANGED`);
        assert.throws(() => check(floor, missing), /changed or disappeared/);
    }
    console.log(`session ABI control: deletion and change go red for all ${floor.size} lines; additions pass`);
} else {
    if (!args.length) throw Error('usage: session-abi-check.mjs [--generate probe.cpp | --self-test | executable [args...]]');
    const result = spawnSync(args[0], args.slice(1), {encoding: 'utf8', maxBuffer: 1024 * 1024});
    if (result.status !== 0) throw Error(result.stderr || String(result.error));
    check(lines(read('tools/session-abi-v1.txt')), lines(result.stdout));
    console.log(`session ABI v1: ${lines(result.stdout).size} compiled surface lines agree with the frozen floor`);
}
