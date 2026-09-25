// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#ifndef FC_PROBE_ABI_H
#define FC_PROBE_ABI_H

// fc_probe — the C ABI over the offline analyzers (tools/wasm/fc_probe.cpp, which compiles natively as well:
// EMSCRIPTEN_KEEPALIVE degrades to extern "C"). This header carries the one thing a caller must be able to ask
// the running module before it calls anything else: WHICH SURFACE IT SPEAKS. The entry points themselves stay
// declared where they are defined; a caller that wants them declares them (the suites under tools/tests do).
//
// ====================================================================================
// THE COMPATIBILITY RULE — APPEND-ONLY, the rule fc_master_abi.h states for its own surface
// ====================================================================================
// 1. WHAT MOVES THE VERSION. One number for the whole probe ABI, and it moves by one when the surface a caller
//    may use grows: an entry point is ADDED, or a published row or block GROWS. The first half is not pedantry —
//    a page's only protection against calling an export the module does not have (a TypeError, not a refusal)
//    is its loader's "module version >= page version" gate, so an entry point that arrived without a bump would
//    pass that gate and still be missing.
//
// 2. WHAT A BUMP MAY DO. One logical addition: new entry points, and/or new fields appended to the END of a
//    scalars block or a row (`fc_probe_<mode>_scalars`, a `_stride`-wide row). Every such block publishes its
//    width (`_scalars_len`, `_<row>_stride`), so a reader written against an older version reads the fields it
//    knows at the offsets it knows and skips the rest by the published width.
//
// 3. WHAT NEVER CHANGES. An existing entry point's name, arguments, return type and meaning; the offset and
//    meaning of an existing field in a block or a row; the sentinels (a refused run answers 0 and leaves its
//    getters silent; a refused price is +0.0; NaN is a measurement's `null`). Nothing is removed and nothing is
//    reordered: a change that cannot be written as an append is a NEW entry point, beside the old one.
//
// 4. WHAT ONE BUMP TOUCHES: this file's FC_PROBE_ABI_VERSION, the entry points in tools/wasm/fc_probe.cpp, the
//    suites that pin them, and the CHANGELOG entry that names the version.
//
// VERSION 1 is the surface as of the release that introduced this header: every fc_probe_* and fc_stream_* entry
// point in tools/wasm/fc_probe.cpp at that commit, the tempo detector's included, and fc_probe_abi_version itself.
#define FC_PROBE_ABI_VERSION 1u

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The version this build speaks — FC_PROBE_ABI_VERSION. Takes nothing, reads no measurement, cannot fail.
uint32_t fc_probe_abi_version (void);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif   // FC_PROBE_ABI_H
