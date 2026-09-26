// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#ifndef FC_SESSION_ABI_H
#define FC_SESSION_ABI_H

// fc_session — the C ABI over `felitronics::session`, the mastering session: the surface a shell that cannot link C++
// (a browser worker, through the fcsession wasm module) talks to the brain through. It is the DECLARATION only: the
// implementation is tools/wasm/fc_session.cpp, which compiles natively as well (EMSCRIPTEN_KEEPALIVE degrades to a
// plain extern "C"), so its guards run under ctest, ASan and UBSan like everything else. fc_master and fc_probe stay
// what they are — the chain's and the analyzers' own surfaces, for their parity suites and CLIs.
//
// ====================================================================================
// THE FACADE IS THIN — fc_master's law, kept here
// ====================================================================================
// A desktop application links `felitronics::session` AS C++, past this file entirely, so nothing may be reachable
// only through it. What it adds is exactly what a C boundary needs and the C++ API does not: handles instead of
// pointers, a status per call, argument checks on addresses a page computed, and the poison. No arithmetic and no
// state of the session's own: every answer is the session's, read out of it.
//
// ====================================================================================
// v1 — AND WHAT IT IS FOR
// ====================================================================================
// The version, a session created and destroyed through a handle, and the poison. Nothing is measured, mastered or
// loaded yet; that surface arrives as entry points appended under the rule below, each with its bump.
//
// ====================================================================================
// THE COMPATIBILITY RULE — APPEND-ONLY, as tools/fc_master_abi.h and tools/fc_probe_abi.h state it for theirs
// ====================================================================================
// 1. WHAT MOVES THE VERSION: the surface a caller may use grows — an entry point is ADDED, or a struct that crosses
//    the boundary gains a field (appended at its END; a struct nested by value never grows). One logical addition per
//    bump. A page's only protection against calling an export its module does not have (a TypeError, not a status)
//    is its loader's "module version >= page version" gate, so an entry point that arrived without a bump would pass
//    that gate and still be missing.
// 2. WHAT NEVER CHANGES: an existing entry point's name, arguments, return type and meaning; an existing status code's
//    value and meaning; the handle's encoding (0 is never a handle). Nothing is removed or reordered.
// 3. A NEW STATUS CODE IS NOT A NEW VERSION: codes are only ever appended, so a caller built against an older header
//    meets one it does not know exactly where it already handles "not FC_SESSION_OK".
// 4. WHAT ONE BUMP TOUCHES: this file's FC_SESSION_ABI_VERSION and table, the entry points in
//    tools/wasm/fc_session.cpp, the suite that pins them (felitronics_session_abi_tests), tools/wasm/session-check.mjs
//    (which holds the module to its exact export set), and the release note that names the version.
//
//   fc_session   the surface
//   ----------   ------------------------------------------------------------------------------------------------
//   1            fc_session_abi_version, fc_session_create, fc_session_destroy — plus the heap's _malloc / _free.
#define FC_SESSION_ABI_VERSION 1u

// HOW MANY SESSIONS ONE MODULE INSTANCE HOLDS AT ONCE. A create past it answers FC_SESSION_ERR_EXHAUSTED. Part of the
// contract, because a page decides from it whether a second session needs a second module instance.
#define FC_SESSION_MAX_HANDLES 8u

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A session handle: a slot of the module's table and that slot's generation, packed into 32 bits. 0 is never a handle,
// so a zeroed variable in JavaScript is refused; a destroyed handle stays refused, because the slot's generation has
// moved on. A handle is not an address and means nothing outside the module instance that issued it.
typedef uint32_t fc_session;

// THE STATUS OF EVERY CALL THAT CAN FAIL. The values run in the order the checks do, so one malformed call has one
// answer:   POISON -> out-parameter (null, alignment, in the heap) -> handle -> the table.
typedef enum fc_session_status
{
    FC_SESSION_OK            = 0,
    FC_SESSION_ERR_POISONED  = 1,   // an earlier call into this module never returned — see POISON below
    FC_SESSION_ERR_NULL      = 2,   // a required out-pointer was null
    FC_SESSION_ERR_ALIGNMENT = 3,   // an out-pointer that is not aligned for what it points to
    FC_SESSION_ERR_SPAN      = 4,   // an out-pointer whose object leaves the wasm heap (wasm only — natively there is
                                    // no linear memory to bound it by)
    FC_SESSION_ERR_HANDLE    = 5,   // 0, never issued, already destroyed, or from a previous generation of its slot
    FC_SESSION_ERR_EXHAUSTED = 6    // FC_SESSION_MAX_HANDLES sessions are alive already
} fc_session_status;

// ====================================================================================
// POISON — the one flag, and it is for ever
// ====================================================================================
// Under -fno-exceptions a heap that cannot serve a request does not come back as a status: the module ABORTS inside
// the call (law 11d, felitronics-core docs/DSP-ARCHITECTURE.md). The abort does not stop the module — emscripten lets
// the page call again, with every object wherever the abort left it — and such an instance does not fail, it LIES
// (fc_master measured one answering FC_OK at the gain of a search that never returned). So every entry point that
// returns a status marks a call in progress and clears the mark only on a normal return. Finding it set on entry
// means an earlier call never returned — an abort, a trap, or natively an exception that escaped — and from then on
// every such call answers FC_SESSION_ERR_POISONED, writes nothing and touches nothing. The way back is outside the
// instance: a new module instance, and the work replayed into it. fc_session_abi_version reads no state and stays
// callable.
//
// THE MODULE IS NOT RE-ENTRANT, and the poison is what says so: an entry point called while another is still running
// (from a new_handler, from anything the runtime runs inside an allocation) cannot be told apart from the first call
// after an abandoned one, and is answered FC_SESSION_ERR_POISONED — for good.

// The version this build speaks — FC_SESSION_ABI_VERSION. Takes nothing, reads no state, cannot fail.
uint32_t fc_session_abi_version (void);

// Creates an empty session and writes its handle to `*out`. `*out` is written ONLY on FC_SESSION_OK: a refused call
// leaves it as it was, so a variable that still holds a live handle is not overwritten by a create that failed.
// Checks: poison, `out` (null, 4-byte alignment, in the heap), a free slot.
fc_session_status fc_session_create (fc_session* out);

// Destroys the session `session` names; the handle is refused from then on. Checks: poison, the handle.
fc_session_status fc_session_destroy (fc_session session);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif   // FC_SESSION_ABI_H
