// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// WHICH ANALYSIS MODULE A HARNESS WAS HANDED — asked of the PATH and then checked against the ARTIFACT, never read off
// the artifact alone. fcprobe (every analyzer) and fctempo (the tempo detector alone, tools/wasm/fc_tempo.cpp) publish
// the tempo entry points under the same names, so a harness that decided which module it held from what the module
// exports would, handed fcprobe under fctempo's name, run the probe's checks, pass them, and report fctempo green
// (found by the review round: 34 checks, 0 failures, and the exact-surface check never ran). So the name says what is
// EXPECTED — fcprobe.* or fctempo.*, as build.sh names its artifacts — and the module must then answer that module's
// version entry point, not the other's, with the number its header declares.

import { readFileSync } from 'node:fs';
import { basename } from 'node:path';

const MODULES = {
    fcprobe: { fn: '_fc_probe_abi_version', header: '../fc_probe_abi.h', macro: 'FC_PROBE_ABI_VERSION' },
    fctempo: { fn: '_fc_tempo_abi_version', header: '../fc_tempo_abi.h', macro: 'FC_TEMPO_ABI_VERSION' },
};

// Returns { name, version } or throws an Error saying what does not match. The caller decides the exit status.
export function identifyModule(M, modPath) {
    const base = basename(modPath);
    const name = Object.keys(MODULES).find(k => base.startsWith(`${k}.`));
    if (!name) throw new Error(`${modPath}: cannot tell which module this is meant to be — name it fcprobe.* or fctempo.*`);
    const want = MODULES[name];
    for (const [other, o] of Object.entries(MODULES))
        if (other !== name && typeof M[o.fn] === 'function')
            throw new Error(`${modPath} is named ${name} but answers ${o.fn.slice(1)} — it is ${other}`);
    if (typeof M[want.fn] !== 'function')
        throw new Error(`${modPath} is named ${name} but has no ${want.fn.slice(1)}`);
    const m = new RegExp(`^#define ${want.macro} ([0-9]+)u$`, 'm')
        .exec(readFileSync(new URL(want.header, import.meta.url), 'utf8'));
    if (!m) throw new Error(`no ${want.macro} in tools/${want.header.slice(3)}`);
    const version = M[want.fn]();
    if (version !== Number(m[1]))
        throw new Error(`${modPath}: ${want.fn.slice(1)}() answers ${version}, tools/${want.header.slice(3)} declares ${m[1]}`);
    return { name, version };
}
