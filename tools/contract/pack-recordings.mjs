// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE SITE RECORDINGS AS RELEASE FILES. The recordings are build output (run.mjs and wav-contract.mjs write them once a
// run matches the manifests in git); a release attaches them to its tag, and the site downloads them by version and
// checks each against its own copy of the manifests. This packs one directory of recordings for that upload, refusing
// any file its manifest does not name byte for byte:
//
//     node tools/contract/pack-recordings.mjs <recordings-dir> <out-dir>
//
// <out-dir> receives recording-<scenario>.json.gz for every scenario of manifest.json, recording-wav-contract.json.gz,
// and the two manifests as recording-manifest.json and recording-wav-contract.manifest.json. A tag from before the
// recordings left git (v0.22.1 and older) holds no wav-contract.manifest.json: there the manifest is derived from the
// recording, which is allowed only while git tracks that recording in <recordings-dir> — the file the tag shipped, which
// the release workflow has just held to a fresh build with the tag's own gate. An untracked recording with no manifest is
// refused: deriving would compare it with itself.

import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {existsSync, mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {gzipSync, constants as zlibConstants} from 'node:zlib';
import {decodeWavRecording} from './wav-recording.mjs';

const [from, to] = process.argv.slice(2).map(path => path && resolve(path));
if (!from || !to) {
    console.error('usage: node tools/contract/pack-recordings.mjs <recordings-dir> <out-dir>');
    process.exit(2);
}
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const gzip = bytes => gzipSync(bytes, {level:zlibConstants.Z_BEST_COMPRESSION});
mkdirSync(to, {recursive:true});

const manifestBytes = readFileSync(join(from, 'manifest.json'));
const manifest = JSON.parse(manifestBytes);
for (const [name, entry] of Object.entries(manifest.scenarios)) {
    const bytes = readFileSync(join(from, `${name}.json`));
    assert.equal(bytes.length, entry.bytes, `${name}.json is ${bytes.length} bytes, the manifest says ${entry.bytes}`);
    assert.equal(sha(bytes), entry.sha256, `${name}.json is not the recording the manifest names; rebuild: ${manifest.rebuild}`);
    writeFileSync(join(to, `recording-${name}.json.gz`), gzip(bytes));
}
writeFileSync(join(to, 'recording-manifest.json'), manifestBytes);

const wavBytes = readFileSync(join(from, 'wav-contract.json'));
const wav = JSON.parse(wavBytes);
const {base64, ...archiveEntry} = wav.contractArchive;
const wavManifestPath = join(from, 'wav-contract.manifest.json');
if (!existsSync(wavManifestPath)) {
    const tracked = spawnSync('git', ['-C', from, 'ls-files', '--error-unmatch', 'wav-contract.json'], {stdio:'ignore'});
    assert.equal(tracked.status, 0, `${wavManifestPath} is missing and git tracks no wav-contract.json there: nothing names the WAV `
        + 'recording, so it cannot be packed');
}
const wavManifest = existsSync(wavManifestPath) ? JSON.parse(readFileSync(wavManifestPath)) : {...wav, contractArchive:archiveEntry};
assert.deepEqual({...wav, contractArchive:archiveEntry}, wavManifest,
    `wav-contract.json is not the recording wav-contract.manifest.json names; rebuild: ${wav.rebuild}`);
assert.equal(sha(Buffer.from(JSON.stringify(decodeWavRecording(wav)))), wavManifest.contractArchive.sha256,
    `wav-contract.json's archive does not decode to the responses its manifest names; rebuild: ${wav.rebuild}`);
writeFileSync(join(to, 'recording-wav-contract.json.gz'), gzip(wavBytes));
writeFileSync(join(to, 'recording-wav-contract.manifest.json'), JSON.stringify(wavManifest, null, 2) + '\n');

console.log(`packed ${Object.keys(manifest.scenarios).length} session recordings and the WAV contract recording of `
    + `${manifest.versions.components[0]} into ${to}`);
