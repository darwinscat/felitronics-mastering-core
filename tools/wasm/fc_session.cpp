// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fc_session — the implementation of the C ABI declared in tools/fc_session_abi.h, and the translation unit of the
// fcsession wasm module (tools/wasm/build.sh). Read the header first: it carries the contract, this file only the
// unpacking of it.
//
// NOTHING HERE DECIDES ANYTHING. Four jobs and no fifth: check what a page hands over, own the handles, refuse every
// call once one has not returned (the poison), and forward to `felitronics::session` — including its refusals, mapped
// to status codes. It compiles natively too (FC_EXPORT degrades to extern "C"), which is how
// felitronics_session_abi_tests runs it under ctest, ASan and UBSan, and how fcore_session reports the ABI it speaks —
// and everywhere it is compiled with the session library's own options (tools/CMakeLists.txt, tools/wasm/build.sh): no
// exceptions, no RTTI, no contraction, no fast-math.
//
// TWO GLOBALS, AND THOSE TWO ARE THE ONLY ONES SESSION HAS ANYWHERE. `modules/session` holds no mutable state outside
// its objects (docs/SESSION.md); a C boundary cannot work without some, because a handle must name something between
// calls and the poison must outlive the call that never returned. So this file keeps the handle table and the poison
// flag, and nothing else — the object-file gate reads this translation unit's object and refuses any writable symbol
// but those two, named in tools/lint/session-objects.txt. Both are trivially destructible, so the module registers no
// destructor to run at exit either.
//
// AND THE SESSION'S SOURCE LAWS HOLD HERE TOO (tools/lint/check-session-laws.mjs scans this file and the ABI header): the
// build guards first, no macro but FC_EXPORT (which tools/wasm/build.sh's export scanner reads), no platform branch but
// the one below, the include allowlist plus the two emscripten headers. That list is the file's stated allowance.

#include "BuildGuards.h"                       // first: refuses a unit compiled with exceptions, RTTI or fast-math
#include "fc_session_abi.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>

#include <cstdint>
#include <memory>

#if defined(__EMSCRIPTEN__)
  #include <emscripten/emscripten.h>
  #include <emscripten/heap.h>                 // emscripten_get_heap_size — NOT declared by emscripten.h
  #define FC_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
  #define FC_EXPORT extern "C"
#endif

using felitronics::session::Session;
using felitronics::session::Status;

static_assert (sizeof (fc_session) == 4, "a handle is 32 bits on every tier");
static_assert (FC_SESSION_MAX_HANDLES >= 1u && FC_SESSION_MAX_HANDLES <= 255u,
               "the slot index is packed into the handle's low 8 bits, with 0 kept for 'no handle'");

namespace
{

//==============================================================================
// THE HANDLE TABLE — fc_master's scheme (tools/wasm/fc_master.cpp, HANDLES), for fc_master's measured reason: under
// emscripten a destroyed object's address came back for the next one 19 times out of 19, so a raw pointer handle is a
// use-after-free that the shipping tier reproduces every time and a desktop machine never does. A handle is a slot and
// that slot's generation: `(generation << 8) | (slot + 1)`, 24 bits of generation.
//
// AND A SLOT RETIRES BEFORE ITS GENERATION WRAPS. A generation that wrapped would give the slot's first handle back to a
// later session, and the stale handle would then destroy it: reproduced on the wasm module — handle 257 created and
// destroyed, 16 777 214 more create/destroy cycles (0.21 s), the next create answered 257 again, and destroy(257)
// through the stale copy destroyed the new session. So a slot whose generation reaches kMaxGeneration is retired when
// that session is destroyed: it issues no handle again, and every handle it ever issued stays refused. The price is a
// lifetime budget — FC_SESSION_MAX_HANDLES x kMaxGeneration creates per module instance — after which a create
// answers FC_SESSION_ERR_EXHAUSTED for good (fc_session_abi.h says so).
constexpr std::uint32_t kMaxGeneration = FC_SESSION_SLOT_GENERATIONS;
static_assert (kMaxGeneration == 0x00FFFFFFu, "a generation is the 24 bits of the handle above the slot byte");

struct Slot
{
    std::uint32_t gen = 1;           // from 1, so a zeroed handle is never valid
    bool retired = false;            // its generations are spent: it issues no handle again
    Session* session = nullptr;      // null: the slot is free. Owned — deleted by destroy — and a raw pointer so the
                                     // table is trivially destructible (no exit-time destructor for a module to run)
};

Slot g_slots[FC_SESSION_MAX_HANDLES];

// THE POISON FLAG — one variable, three states, because "a call is in progress" and "a call never returned" are the
// same mark read at two moments: an entry point that finds it anything but Idle knows an earlier call did not come
// back (or that it was re-entered, which it cannot tell apart), and latches Poisoned. `volatile` for fc_master's
// reason: the mark's only reader follows an abort, and nothing obliges an optimiser to keep a store whose reader it
// cannot see.
enum : std::uint8_t { kIdle = 0, kInCall = 1, kPoisoned = 2 };
volatile std::uint8_t g_callState = kIdle;

// THE CALL, as an object — the first statement of every status-returning entry point, ahead of every other check,
// because an abandoned module has no handle and no argument this file can vouch for:
//     const CallGuard call;
//     if (call.refused()) return FC_SESSION_ERR_POISONED;
// Entering finds the mark Idle and sets InCall, or finds anything else — a call that never returned, or a re-entry,
// which it cannot tell apart — and latches Poisoned. Leaving clears InCall on a normal return. Nothing else can end a
// call: this translation unit and the library are compiled without exceptions, so there is no unwinding to tell apart
// from a return — an allocation that cannot be served ends the process natively and aborts the module in wasm, where
// the mark stays set and poisons what follows. A mark that became Poisoned during the call is not cleared — the latch
// is the point. (A class, not a macro: the session's sources define no macros, and this file keeps only FC_EXPORT.)
class CallGuard
{
public:
    CallGuard() noexcept : refused_ (g_callState != kIdle)
    {
        g_callState = refused_ ? kPoisoned : kInCall;
    }
    ~CallGuard()
    {
        if (g_callState == kInCall) g_callState = kIdle;
    }
    CallGuard (const CallGuard&) = delete;
    CallGuard& operator= (const CallGuard&) = delete;

    [[nodiscard]] bool refused() const noexcept { return refused_; }

private:
    const bool refused_;
};

fc_session packHandle (std::uint32_t slot, std::uint32_t gen) noexcept
{
    return ((gen & kMaxGeneration) << 8) | (slot + 1u);
}

Slot* lookup (fc_session h) noexcept
{
    if (h == 0u) return nullptr;
    const std::uint32_t slot = (h & 0xFFu) - 1u;          // 0 wraps to a huge index, refused below
    if (slot >= FC_SESSION_MAX_HANDLES) return nullptr;
    Slot& s = g_slots[slot];
    if (s.session == nullptr) return nullptr;
    if ((s.gen & kMaxGeneration) != (h >> 8)) return nullptr;
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

// Two uint32 halves of a 64-bit number: the same checks, over both.
fc_session_status checkHalvesOut (const std::uint32_t* out) noexcept
{
    if (out == nullptr) return FC_SESSION_ERR_NULL;
    if ((reinterpret_cast<std::uintptr_t> (out) & (alignof (std::uint32_t) - 1u)) != 0u) return FC_SESSION_ERR_ALIGNMENT;
    if (! inHeap (out, 2 * sizeof (std::uint32_t))) return FC_SESSION_ERR_SPAN;
    return FC_SESSION_OK;
}

} // namespace

//==============================================================================
// ENTRY POINTS

FC_EXPORT std::uint32_t fc_session_abi_version (void) { return FC_SESSION_ABI_VERSION; }

FC_EXPORT fc_session_status fc_session_create (fc_session* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const fc_session_status st = checkHandleOut (out); st != FC_SESSION_OK) return st;
    std::uint32_t slot = 0;
    while (slot < FC_SESSION_MAX_HANDLES && (g_slots[slot].session != nullptr || g_slots[slot].retired)) ++slot;
    if (slot == FC_SESSION_MAX_HANDLES) return FC_SESSION_ERR_EXHAUSTED;
    // Session::create() makes its own checks first and allocates Session::createBytes() only if they pass; nothing here
    // allocates besides — the table is static — so a shell's budget for this call is the session's own demand
    // (felitronics_session_abi_tests pins it). A heap that cannot serve it aborts inside, and the mark above stays set:
    // the poison. A refusal comes back as the session's reason, with the slot still free and `*out` untouched.
    auto created = Session::create();
    switch (created.status)
    {
        case Status::Ok:                       break;
        case Status::FloatingPointEnvironment: return FC_SESSION_ERR_FP_ENVIRONMENT;
    }
    g_slots[slot].session = created.session.release();
    *out = packHandle (slot, g_slots[slot].gen);
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_destroy (fc_session session)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    Slot* s = lookup (session);
    if (s == nullptr) return FC_SESSION_ERR_HANDLE;
    delete s->session;
    s->session = nullptr;
    // The generation moves on, so the handle just destroyed can never address the next session in this slot — and at
    // the last generation the slot retires instead of wrapping (see HANDLES above). Generation 0 is never issued, so no
    // handle is ever a small integer.
    if (s->gen == kMaxGeneration)
        s->retired = true;
    else
        ++s->gen;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_config_version (std::uint32_t* out)
{
    FC_SESSION_GUARD;
    if (const fc_session_status st = checkHalvesOut (out); st != FC_SESSION_OK) return st;
    const std::uint64_t v = felitronics::session::config::version();   // reads the embedded data; allocates nothing
    out[0] = static_cast<std::uint32_t> (v);
    out[1] = static_cast<std::uint32_t> (v >> 32);
    return FC_SESSION_OK;
}
