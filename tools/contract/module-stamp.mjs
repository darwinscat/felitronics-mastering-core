// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// ONE BUILD, FROM THESE SOURCES. The site recordings come from two wasm modules: the production fcsession and, for
// every scenario that places or poisons, the contract-trap copy beside it. tools/wasm/build.sh builds both in one run
// and writes this stamp next to them: each module's sha256 and one digest of the sources it compiled. The contract
// check refuses a module directory whose files are not the ones the stamp names (one module rebuilt by hand, the other
// left from an older build) or whose stamp was made from other sources (edited after the build). Both reached a
// committed recording once: the budget numbers a stale module prints are masked in the native/wasm comparison, so the
// local check passed and only a clean build in CI saw the difference.
//
//   node tools/contract/module-stamp.mjs --sources                 prints the sources digest (build.sh, before it compiles)
//   node tools/contract/module-stamp.mjs <module dir> <digest>     writes <module dir>/contract-modules.sha256 (after)
import {createHash} from 'node:crypto';
import {existsSync, readFileSync, readdirSync, statSync, writeFileSync} from 'node:fs';
import {dirname, join, relative, resolve, sep} from 'node:path';
import {fileURLToPath} from 'node:url';

export const stampName = 'contract-modules.sha256';
export const stampedModules = ['fcsession.node.js', 'fcsession.node.wasm', 'contract-trap/fcsession.node.js', 'contract-trap/fcsession.node.wasm'];
const repoRoot = fileURLToPath(new URL('../../', import.meta.url));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

function walk(dir, out) {
    for (const entry of readdirSync(dir, {withFileTypes:true})) {
        if (entry.name.startsWith('.')) continue;
        const path = join(dir, entry.name);
        if (entry.isDirectory()) { if (entry.name !== 'tests') walk(path, out); } else if (entry.isFile()) out.push(path);
    }
    return out;
}
// What build.sh compiles into the session modules, read with LF line endings so a CRLF checkout digests the same text.
// felitronics-core and felitronics-toml are not in it: BUILD-INFO names their versions.
export function sourceFiles(root = repoRoot) {
    const tools = join(root, 'tools');
    const files = walk(join(root, 'modules'), []);
    walk(join(tools, 'wasm', 'session-controls'), files);
    for (const name of ['build.sh', 'fc_session.cpp']) files.push(join(tools, 'wasm', name));
    for (const name of readdirSync(tools)) if (/\.(h|hpp|cpp)$/.test(name) || name === 'session-codec-schema.json' || name === 'session-codec.cmake') files.push(join(tools, name));
    return files.map(f => relative(root, f).split(sep).join('/')).sort();
}
export function sourcesDigest(root = repoRoot) {
    const hash = createHash('sha256');
    for (const file of sourceFiles(root)) {
        const text = readFileSync(join(root, file)).toString('latin1').replaceAll('\r\n', '\n');
        hash.update(`${file}\n${text.length}\n`).update(text, 'latin1');
    }
    return hash.digest('hex');
}
export function writeStamp(dir, sources = sourcesDigest()) {
    const lines = [`${sources}  sources`, ...stampedModules.map(m => `${sha256(readFileSync(join(dir, m)))}  ${m}`)];
    writeFileSync(join(dir, stampName), lines.join('\n') + '\n');
}
// Throws unless every stamped module beside `modulePath` is the one its build wrote, from the sources checked out now.
export function checkStamp(modulePath, sources = sourcesDigest()) {
    const dir = dirname(resolve(modulePath)), stamp = join(dir, stampName);
    const rebuild = `rebuild every module with tools/wasm/build.sh${dir === resolve(repoRoot, 'tools/wasm/build') ? '' : ` ${dir}`}`;
    if (!existsSync(stamp)) throw new Error(`no ${stampName} beside ${modulePath}: these modules were not built by tools/wasm/build.sh; ${rebuild}`);
    const listed = new Map(readFileSync(stamp, 'utf8').trimEnd().split('\n').map(line => {
        const [hash, name] = line.split('  '); return [name, hash];
    }));
    if (listed.get('sources') !== sources)
        throw new Error(`${stamp} was made from other sources than the ones checked out (edited or switched after the build); ${rebuild}`);
    for (const name of stampedModules) {
        const path = join(dir, name);
        if (!existsSync(path) || !statSync(path).isFile() || sha256(readFileSync(path)) !== listed.get(name))
            throw new Error(`${name} beside ${modulePath} is not the module ${stampName} names (rebuilt alone, or left from another build); ${rebuild}`);
    }
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const [what, digest] = process.argv.slice(2);
    if (what === '--sources') console.log(sourcesDigest());
    else if (what && /^[0-9a-f]{64}$/.test(digest ?? '')) writeStamp(resolve(what), digest);
    else { console.error('usage: module-stamp.mjs --sources | <module dir> <sources digest taken before the build>'); process.exitCode = 2; }
}
