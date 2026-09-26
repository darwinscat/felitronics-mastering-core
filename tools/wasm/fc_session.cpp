// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fc_session — the implementation of the C ABI declared in tools/fc_session_abi.h, and the translation unit of the
// fcsession wasm module (tools/wasm/build.sh). Read the header first: it carries the contract, this file only the
// unpacking of it.
//
// NOTHING HERE DECIDES ANYTHING. Four jobs and no fifth: check what a page hands over, own the handles, refuse every
// call once one has not returned (the poison), and forward to `felitronics::session`. It compiles natively too
// (FC_EXPORT degrades to extern "C"), which is how felitronics_session_abi_tests runs it under ctest, ASan and UBSan,
// and how fcore_session reports the ABI it speaks.
//
// TWO GLOBALS, AND THOSE TWO ARE THE ONLY ONES SESSION HAS ANYWHERE. `modules/session` holds no mutable state outside
// its objects (docs/SESSION.md); a C boundary cannot work without some, because a handle must name something between
// calls and the poison must outlive the call that never returned. So this file keeps the handle table and the poison
// flag, and nothing else — tools/lint/session-laws.txt names both, and the session-laws lint refuses a third.

#include "fc_session_abi.h"

#include <felitronics/session/Session.h>

#include <cstdint>
#include <exception>
#include <memory>

#if defined(__EMSCRIPTEN__)
  #include <emscripten/emscripten.h>
  #include <emscripten/heap.h>                 // emscripten_get_heap_size — NOT declared by emscripten.h
  #define FC_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
  #define FC_EXPORT extern "C"
#endif

using felitronics::session::Session;

static_assert (sizeof (fc_session) == 4, "a handle is 32 bits on every tier");
static_assert (FC_SESSION_MAX_HANDLES >= 1u && FC_SESSION_MAX_HANDLES <= 255u,
               "the slot index is packed into the handle's low 8 bits, with 0 kept for 'no handle'");

namespace
{

//==============================================================================
// THE HANDLE TABLE — fc_master's scheme (tools/wasm/fc_master.cpp, HANDLES), for fc_master's measured reason: under
// emscripten a destroyed object's address came back for the next one 19 times out of 19, so a raw pointer handle is a
// use-after-free that the shipping tier reproduces every time and a desktop machine never does. A handle is a slot and
// that slot's generation: `(generation << 8) | (slot + 1)`, 24 bits of generation, so a destroyed handle is refused
// until the same slot has been destroyed 16.7 million more times — a number no page meets.
struct Slot
{
    std::uint32_t gen = 1;                 // from 1, so a zeroed handle is never valid
    std::unique_ptr<Session> session;      // null: the slot is free
};

Slot g_slots[FC_SESSION_MAX_HANDLES];

// THE POISON FLAG — one variable, three states, because "a call is in progress" and "a call never returned" are the
// same mark read at two moments: an entry point that finds it anything but Idle knows an earlier call did not come
// back (or that it was re-entered, which it cannot tell apart), and latches Poisoned. `volatile` for fc_master's
// reason: the mark's only reader follows an abort, and nothing obliges an optimiser to keep a store whose reader it
// cannot see.
enum : std::uint8_t { kIdle = 0, kInCall = 1, kPoisoned = 2 };
volatile std::uint8_t g_callState = kIdle;

// Clears the in-call mark on a normal return — and only on one. Natively an exception escaping the call unwinds this
// guard too, so it compares the count of exceptions in flight with the count on entry and leaves the mark standing
// when they differ: an escaped exception is a call that did not return. A mark that became Poisoned during the call
// (a re-entry) is not cleared either — the latch is the point.
struct CallGuard
{
    const int unwinding = std::uncaught_exceptions();
    ~CallGuard()
    {
        if (std::uncaught_exceptions() == unwinding && g_callState == kInCall) g_callState = kIdle;
    }
};

// The first statement of every status-returning entry point — ahead of every other check, because an abandoned module
// has no handle and no argument this file can vouch for.
#define FC_SESSION_GUARD                                                                          \
    if (g_callState != kIdle) { g_callState = kPoisoned; return FC_SESSION_ERR_POISONED; }       \
    g_callState = kInCall;                                                                        \
    const CallGuard fcGuard_ {}

fc_session packHandle (std::uint32_t slot, std::uint32_t gen) noexcept
{
    return ((gen & 0x00FFFFFFu) << 8) | (slot + 1u);
}

Slot* lookup (fc_session h) noexcept
{
    if (h == 0u) return nullptr;
    const std::uint32_t slot = (h & 0xFFu) - 1u;          // 0 wraps to a huge index, refused below
    if (slot >= FC_SESSION_MAX_HANDLES) return nullptr;
    Slot& s = g_slots[slot];
    if (s.session == nullptr) return nullptr;
    if ((s.gen & 0x00FFFFFFu) != (h >> 8)) return nullptr;
    return &s;
}

// Is [p, p + bytes) inside the wasm linear memory? An out-pointer from JavaScript is an address a page computed, and
// an aligned one four bytes below the top of the heap passes every other check and then traps the module on the write
// — the barrier this facade exists to be, with a hole exactly where the value looks harmless. Natively there is no
// linear memory to bound it by, and this answers yes.
bool inHeap (const void* p, std::uint64_t bytes) noexcept
{
#if defined(__EMSCRIPTEN__)
    const std::uint64_t base = (std::uint64_t) reinterpret_cast<std::uintptr_t> (p);
    if (base == 0) return false;
    const std::uint64_t end = base + bytes;
    if (end < base) return false;                                      // wrapped
    return end <= (std::uint64_t) emscripten_get_heap_size();
#else
    (void) p; (void) bytes;
    return true;
#endif
}

fc_session_status checkHandleOut (const fc_session* out) noexcept
{
    if (out == nullptr) return FC_SESSION_ERR_NULL;
    if ((reinterpret_cast<std::uintptr_t> (out) & (alignof (fc_session) - 1u)) != 0u) return FC_SESSION_ERR_ALIGNMENT;
    if (! inHeap (out, sizeof (fc_session))) return FC_SESSION_ERR_SPAN;
    return FC_SESSION_OK;
}

} // namespace

//==============================================================================
// ENTRY POINTS

FC_EXPORT std::uint32_t fc_session_abi_version (void) { return FC_SESSION_ABI_VERSION; }

FC_EXPORT fc_session_status fc_session_create (fc_session* out)
{
    FC_SESSION_GUARD;
    if (const fc_session_status st = checkHandleOut (out); st != FC_SESSION_OK) return st;
    std::uint32_t slot = 0;
    while (slot < FC_SESSION_MAX_HANDLES && g_slots[slot].session != nullptr) ++slot;
    if (slot == FC_SESSION_MAX_HANDLES) return FC_SESSION_ERR_EXHAUSTED;
    // Session::create() allocates Session::createBytes() and nothing here allocates besides: the table is static, so a
    // shell's budget for this call is the session's own demand (felitronics_session_abi_tests pins it). A heap that
    // cannot serve it aborts inside, and the mark above stays set — the poison.
    g_slots[slot].session = Session::create();
    *out = packHandle (slot, g_slots[slot].gen);
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_destroy (fc_session session)
{
    FC_SESSION_GUARD;
    Slot* s = lookup (session);
    if (s == nullptr) return FC_SESSION_ERR_HANDLE;
    s->session.reset();
    // The generation moves on, so the handle just destroyed can never address the next session in this slot. It WRAPS
    // at 24 bits rather than retiring the slot (fc_master's reasoning: a retiring 8-bit generation became a lifetime
    // budget a per-file loop met in an afternoon), and skips 0, so no handle is ever a small integer.
    s->gen = (s->gen + 1u) & 0x00FFFFFFu;
    if (s->gen == 0u) s->gen = 1u;
    return FC_SESSION_OK;
}
