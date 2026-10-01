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
    const inputs = JSON.parse(input);
    const inputHash = hash(Buffer.concat([readFileSync(new URL('fixtures.mjs', import.meta.url)), input]));
    const manifest = {rebuild:'node tools/contract/fixtures.mjs --rebuild', inputHash, files:{}};
    for (const [name, spec] of Object.entries(inputs)) {
        if (spec.sidecarOf) {
            const source = inputs[spec.sidecarOf];
            if (!source || source.project || source.sidecarOf) throw new Error(`invalid sidecar source ${spec.sidecarOf}`);
            let fnv = 0xcbf29ce484222325n;
            const byte = n => { fnv = BigInt.asUintN(64, (fnv ^ BigInt(n)) * 0x100000001b3n); };
            const u32 = n => { for (let i = 0; i < 4; ++i) byte((n >>> (8 * i)) & 255); };
            const u64 = n => { for (let i = 0; i < 8; ++i) byte(Number((BigInt(n) >> BigInt(8 * i)) & 255n)); };
            u32(source.rate); u32(source.channels); u64(source.frames);
            const bits = new DataView(new ArrayBuffer(4));
            for (let i = 0; i < source.channels * source.frames; ++i) {
                const sample = ((i % source.frames + Math.floor(i / source.frames) * 7) % source.period - source.period / 2) * source.scale;
                bits.setFloat32(0, sample / 32768, true); u32(bits.getUint32(0, true));
            }
            const value = {name:spec.sidecarOf, sourceHash:String(fnv), frames:String(source.frames),
                sampleRate:source.rate, channels:source.channels, fileRate:source.rate, bitDepth:16,
                rateKnown:true, integratedLufs:spec.integratedLufs, truePeakDb:spec.truePeakDb};
            const bytes = Buffer.from(JSON.stringify(value) + '\n');
            manifest.files[`${name}.json`] = {inputHash, sha256:hash(bytes), rebuild:manifest.rebuild};
            if (rebuild) writeFileSync(join(root, `${name}.json`), bytes);
            continue;
        }
        if (spec.project) {
            const p = spec.project;
            const bytes = Buffer.from(`defaults = "${p.defaults}"\nmanual = ${p.manual}\n\n[target]\nname = "${p.target}"\n\n[hpf]\nfq.machine = ${p.hpfFrequency}\n\n[low]\non.machine = true\ndb.machine = ${p.lowDb}\n`);
            manifest.files[`${name}.toml`] = {inputHash, sha256:hash(bytes), rebuild:manifest.rebuild};
            if (rebuild) writeFileSync(join(root, `${name}.toml`), bytes);
            continue;
        }
        const {rate, channels, frames, period, scale} = spec;
        // Planar signed integers / 32768: exactly representable in binary32, no host libm.
        const samples = Array.from({length:channels * frames}, (_, i) =>
            spec.silence ? 0 : i === spec.nonFiniteAt ? 'NaN'
                : ((i % frames + Math.floor(i / frames) * 7) % period - period / 2) * scale);
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
