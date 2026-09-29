// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
import {readFileSync, writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {resolve, join} from 'node:path';
export const fixtureRoot = fileURLToPath(new URL('fixtures/', import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');
export function fixtures(root = fixtureRoot, rebuild = false) {
    const input = readFileSync(join(root, 'inputs.json'));
    const inputHash = hash(Buffer.concat([readFileSync(new URL('fixtures.mjs', import.meta.url)), input]));
    const manifest = {rebuild:'node tools/contract/fixtures.mjs --rebuild', inputHash, files:{}};
    for (const [name, spec] of Object.entries(JSON.parse(input))) {
        if (spec.project) {
            const p = spec.project;
            const bytes = Buffer.from(`defaults = "${p.defaults}"\ncore = "${p.core}"\nmanual = ${p.manual}\n\n[target]\nname = "${p.target}"\n\n[hpf]\nfq.machine = ${p.hpfFrequency}\n\n[low]\non.machine = true\ndb.machine = ${p.lowDb}\n`);
            manifest.files[`${name}.toml`] = {inputHash, sha256:hash(bytes), rebuild:manifest.rebuild};
            if (rebuild) writeFileSync(join(root, `${name}.toml`), bytes);
            continue;
        }
        const {rate, channels, frames, period, scale} = spec;
        // Planar signed integers / 32768: exactly representable in binary32, no host libm.
        const samples = Array.from({length:channels * frames}, (_, i) =>
            ((i % frames + Math.floor(i / frames) * 7) % period - period / 2) * scale);
        const bytes = Buffer.from(`${rate} ${channels} ${frames}\n${samples.join(' ')}\n`);
        manifest.files[`${name}.pcm`] = {inputHash, sha256:hash(bytes), rebuild:manifest.rebuild};
        if (rebuild) writeFileSync(join(root, `${name}.pcm`), bytes);
    }
    if (rebuild) writeFileSync(join(root, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
    const recorded = JSON.parse(readFileSync(join(root, 'manifest.json')));
    if (JSON.stringify(recorded) !== JSON.stringify(manifest)) throw new Error(`stale fixture inputs; rebuild: ${manifest.rebuild}`);
    for (const [name, info] of Object.entries(recorded.files))
        if (hash(readFileSync(join(root, name))) !== info.sha256) throw new Error(`stale fixture ${name}; rebuild: ${info.rebuild}`);
    return Object.keys(recorded.files).length;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try { console.log(`contract fixtures: ${fixtures(fixtureRoot, process.argv[2] === '--rebuild')} verified`); }
    catch (error) { console.error(error.message); process.exitCode = 1; }
}
