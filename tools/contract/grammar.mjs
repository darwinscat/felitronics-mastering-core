// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import {readFileSync, writeFileSync} from 'node:fs';
const grammar = JSON.parse(readFileSync(new URL('grammar.json', import.meta.url), 'utf8'));
export function parse(script) {
    const result = [];
    for (const [index, line] of script.split('\n').entries()) {
        const text = line.split('#')[0].replace(/^[ \t\r\v\f]+|[ \t\r\v\f]+$/g, '');
        if (!text) continue;
        const rule = Object.entries(grammar).map(([op, pattern]) => [op, new RegExp(`^${pattern}$`).exec(text)]).find(([, m]) => m);
        if (!rule) throw new Error(`line ${index + 1}: unknown command '${text}'`);
        result.push({op:rule[0], args:rule[1].slice(1), line:index + 1});
    }
    return result;
}
if (process.argv[2] === '--generate') {
    writeFileSync(process.argv[3], '// Generated from tools/contract/grammar.json. Do not edit.\n'
        + 'const Rule rules[] = {\n' + Object.entries(grammar).map(([op, pattern]) =>
            `    {"${op}", R"grammar(^${pattern}$)grammar"},`).join('\n') + '\n};\n');
}
