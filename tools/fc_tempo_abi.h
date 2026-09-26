// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#ifndef FC_TEMPO_ABI_H
#define FC_TEMPO_ABI_H

// fc_tempo — the tempo detector ALONE, as a module of its own (fctempo.*, tools/wasm/fc_tempo.cpp): for a page that
// needs a tempo and nothing else, and should not download every analyzer of fc_probe to get one. Its entry points
// ARE fc_probe's tempo entry points — the same names (fc_probe_tempo_*), arguments, rows and refusals, because both
// modules compile one text, tools/wasm/fc_tempo_entry.h — so a page moves between the two without touching a call.
// What it does not share with fc_probe is the VERSION, and this header is why.
//
// ====================================================================================
// WHY A VERSION OF ITS OWN, AND NOT fc_probe_abi_version
// ====================================================================================
// A version number is a statement about a SURFACE: "version N has every entry point and every row of every version
// up to N". fc_probe_abi_version() == 1 says the report, the hum detector, the streaming meter ... are all there.
// This module answering it would make that statement falsely, and a page gated on it would meet a missing export as
// a TypeError — the one failure the version exists to prevent (rule 1 of tools/fc_probe_abi.h). And the two numbers
// move for different reasons: fc_probe's whenever ANY analyzer grows, this one only when the tempo surface does.
// A loader also learns WHICH module it holds from which version entry point exists, so a swapped file is a refusal,
// not a plausible pass.
//
// ====================================================================================
// THE COMPATIBILITY RULE — APPEND-ONLY, as tools/fc_probe_abi.h states it for its own surface
// ====================================================================================
// 1. WHAT MOVES THE VERSION: the surface a caller may use grows — an entry point is ADDED, or a published row or
//    block GROWS. One logical addition per bump.
// 2. WHAT NEVER CHANGES: an existing entry point's name, arguments, return type and meaning; the offset and meaning
//    of an existing field; the sentinels (a refused run answers 0 and leaves its getters silent; a refused price is
//    +0.0; NaN is a measurement's `null`). Nothing is removed or reordered.
// 3. THE TIE TO fc_probe. The tempo entry points are one text in two modules, so a change a caller can see in
//    tools/wasm/fc_tempo_entry.h grows BOTH surfaces and moves BOTH versions, in one commit. A change to fc_probe
//    outside that file moves only FC_PROBE_ABI_VERSION. Every bump appends a line to the table below.
// 4. WHAT ONE BUMP TOUCHES: this file's FC_TEMPO_ABI_VERSION and table, the entry points, the suites that pin them
//    (felitronics_fctempo_abi_tests; tools/wasm/storage-probe.mjs, which holds the exact export set), and the
//    release note that names the version.
//
//   fc_tempo   the same tempo surface as
//   --------   ------------------------------------------------------------------------------------------------
//   1          fc_probe ABI 1 (felitronics-mastering-core v0.2.0): fc_probe_tempo_run, _run_with, _scalars_len,
//              _cand_stride, _point_stride, _scalars, _candidates, _curve, _storage_bytes, _storage_bytes_with —
//              plus fc_tempo_abi_version itself. Nothing else is exported but the heap's _malloc / _free.
#define FC_TEMPO_ABI_VERSION 1u

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The version this build speaks — FC_TEMPO_ABI_VERSION. Takes nothing, reads no measurement, cannot fail.
uint32_t fc_tempo_abi_version (void);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif   // FC_TEMPO_ABI_H
