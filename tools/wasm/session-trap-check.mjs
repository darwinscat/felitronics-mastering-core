// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const [modulePath, runtimePath] = process.argv.slice(2);
const {invokeSession, FC_SESSION_CONFIG_VERSION: version, FC_SESSION_ABI_VERSION} = await import(pathToFileURL(resolve(runtimePath)));
const M = await createRequire(import.meta.url)(resolve(modulePath))();
const caps = M._malloc(32), out = M._malloc(8);
new DataView(M.HEAPU32.buffer).setFloat64(caps + 8, 64 * 1024 * 1024, true);
M.HEAPU32[caps >>> 2] = 32;
new DataView(M.HEAPU32.buffer).setFloat64(caps + 24, 256 * 1024 * 1024, true);
M.HEAPU32[(caps + 16) >>> 2] = 48000; M.HEAPU32[(caps + 20) >>> 2] = 255;
M.HEAPU32[out >>> 2] = 777;
assert.equal(invokeSession(M._fc_session_create, caps, Number.parseInt(version.slice(8), 16), Number.parseInt(version.slice(0, 8), 16), out), 14);
assert.equal(M.HEAPU32[out >>> 2], 777, 'abandoned call published no handle');
for (const name of Object.keys(M).filter(n => n.startsWith('_fc_session_') && n !== '_fc_session_abi_version')) {
    assert.equal(invokeSession(M[name], ...Array(13).fill(0)), 1, `${name}: poison precedes every argument check`);
    assert.equal(M.HEAPU32[out >>> 2], 777);
}
assert.equal(M._fc_session_abi_version(), FC_SESSION_ABI_VERSION);
assert.throws(() => invokeSession(() => { throw new TypeError('caller bug'); }), TypeError);
M._free(caps); M._free(out);
console.log('session trap control: actual allocation trap is status 14; every subsequent status call is poisoned (1) and publishes nothing');
