// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fc_session, draft version 0 — the C ABI over felitronics::session (tools/fc_session_abi.h), natively, so its guards run under
// ctest, ASan and UBSan; the wasm tier builds and runs this suite too, and tools/wasm/session-check.mjs holds the
// shipped module to the same surface. Pinned:
//   * the version, and that it answers whatever state the module is in;
//   * create: the out-pointer's checks in the header's order, `*out` untouched by every refusal, nothing allocated by
//     a refused call, and what an accepted one allocates held to the session's own demand (memory declared before
//     the work — the table is static, so the facade adds nothing);
//   * destroy: 0, a fabricated handle, a destroyed one and a stale generation are all refused;
//   * the table: FC_SESSION_MAX_HANDLES live sessions and not one more, and a slot freed is a slot reused under a new
//     handle;
//   * the session's own refusal passed through: a thread that rounds other than to nearest gets
//     FC_SESSION_ERR_FP_ENVIRONMENT, `*out` untouched and the slot still free;
//   * THE WRAP BOUNDARY, at the shipped width: one slot driven through all FC_SESSION_SLOT_GENERATIONS of its
//     generations — 16.7 million create/destroy cycles, the reproduction of a stale handle destroying a new session —
//     retires instead of wrapping, and every handle it issued stays refused;
//   * THE POISON, LAST, because it is for good: an entry point re-entered from inside an allocation the module made
//     answers POISONED, and from then on every status call does, though every call returned.

#include <felitronics_test.h>
#include <alloc_counter.h>   // installs the allocation counter: EVERY form of `new`, over-aligned included

#include "../../modules/session/tests/FpEnvironmentControl.h"

#include "fc_session_abi.h"

#include <felitronics/session/Session.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using felitronics::test::ok;

namespace
{
long long requestsNow() { return felitronics::test::alloc::count.load(); }
long long bytesNow()    { return felitronics::test::alloc::bytes.load(); }

void theVersion()
{
    felitronics::test::group ("the version is the header's, and it is the draft");
    ok (fc_session_abi_version() == FC_SESSION_ABI_VERSION, "fc_session_abi_version() == FC_SESSION_ABI_VERSION");
    // 0 IS THE DRAFT: no append-only promise until v1, which freezes the surface together with a create that takes the
    // shell's capabilities and forwards its demand (tools/fc_session_abi.h). Freezing it edits this line on purpose.
    ok (FC_SESSION_ABI_VERSION == 0u, "and that is 0, the draft — no shell may gate on it as a stable interface");
}

void createChecksItsOutPointer()
{
    felitronics::test::group ("create: the out-pointer is checked first, and a refusal writes nothing and allocates nothing");
    // An out-pointer one byte into a handle-sized slot: misaligned for a uint32_t on every tier.
    alignas (8) unsigned char raw[16] {};
    for (auto& b : raw) b = 0xA5;
    auto* misaligned = reinterpret_cast<fc_session*> (raw + 1);
    // Both calls between two readings of the counter and nothing else: the checks below build strings, which allocate.
    const long long r0 = requestsNow();
    const fc_session_status nullOut = fc_session_create (nullptr);
    const fc_session_status oddOut = fc_session_create (misaligned);
    const long long spent = requestsNow() - r0;
    ok (nullOut == FC_SESSION_ERR_NULL, "a null out-pointer is refused");
    ok (oddOut == FC_SESSION_ERR_ALIGNMENT, "a misaligned out-pointer is refused");
    bool untouched = true;
    for (auto b : raw) untouched = untouched && b == 0xA5;
    ok (untouched, "and nothing was written through it");
    ok (spent == 0, "neither refusal asked the heap for anything");
}

void createAndDestroy()
{
    felitronics::test::group ("create and destroy; what a create allocates is the session's declared demand");
    fc_session h = 0;
    const long long r0 = requestsNow(), b0 = bytesNow();
    const fc_session_status created = fc_session_create (&h);
    const long long requests = requestsNow() - r0, bytes = bytesNow() - b0;
    ok (created == FC_SESSION_OK && h != 0u, "a session, under a handle that is not 0");
    const auto declared = (long long) felitronics::session::Session::createBytes();
    ok (requests >= 1 && bytes > 0, "the create was seen asking the heap (" + std::to_string (bytes) + " bytes)");
    ok (bytes <= declared, "requested " + std::to_string (bytes) + " <= Session::createBytes() " + std::to_string (declared)
                           + " — the facade's table is static and adds nothing to the session's demand");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "destroyed");
    ok (fc_session_destroy (h) == FC_SESSION_ERR_HANDLE, "the destroyed handle is refused");
    ok (fc_session_destroy (0u) == FC_SESSION_ERR_HANDLE, "0 is never a handle");
    ok (fc_session_destroy (0xFFFFFFFFu) == FC_SESSION_ERR_HANDLE, "a slot index past the table is refused");

    fc_session again = 0;
    ok (fc_session_create (&again) == FC_SESSION_OK, "the freed slot is used again");
    ok (again != h, "under a different handle: the generation moved on, so the old handle cannot name the new session");
    ok (fc_session_destroy (h) == FC_SESSION_ERR_HANDLE, "and the old handle is still refused");
    const fc_session forged = (again & 0xFFu) | (((again >> 8) + 1u) << 8);
    ok (fc_session_destroy (forged) == FC_SESSION_ERR_HANDLE, "the live slot under another generation is refused");
    ok (fc_session_destroy (again) == FC_SESSION_OK, "the live handle destroys");
}

void theTableHoldsItsCapacity()
{
    felitronics::test::group ("the table holds FC_SESSION_MAX_HANDLES live sessions and not one more");
    std::vector<fc_session> live;
    bool allCreated = true;
    for (std::uint32_t i = 0; i < FC_SESSION_MAX_HANDLES; ++i)
    {
        fc_session h = 0;
        allCreated = allCreated && fc_session_create (&h) == FC_SESSION_OK && h != 0u;
        live.push_back (h);
    }
    ok (allCreated, std::to_string (FC_SESSION_MAX_HANDLES) + " creates succeed");
    fc_session extra = 12345u;
    const long long r0 = requestsNow();
    const fc_session_status refused = fc_session_create (&extra);
    const long long spent = requestsNow() - r0;
    ok (refused == FC_SESSION_ERR_EXHAUSTED, "the next one is refused as EXHAUSTED");
    ok (extra == 12345u, "and leaves `*out` as it was — a variable holding a live handle is not overwritten");
    ok (spent == 0, "and asks the heap for nothing");
    ok (fc_session_destroy (live[3]) == FC_SESSION_OK, "one destroyed");
    fc_session h = 0;
    ok (fc_session_create (&h) == FC_SESSION_OK && h != live[3], "a create fits again, under a new handle");
    live[3] = h;
    bool allDestroyed = true;
    for (auto x : live) allDestroyed = allDestroyed && fc_session_destroy (x) == FC_SESSION_OK;
    ok (allDestroyed, "every live handle destroys");
}

void theSessionsRefusalPassesThrough()
{
    felitronics::test::group ("the session's own refusal passes through: the floating-point environment");
    namespace fpenv = felitronics::session::testing;
    const auto saved = fpenv::saveFpEnvironment();
    if (! fpenv::setRounding (fpenv::kRoundUpward))
    {
        fpenv::restoreFpEnvironment (saved);
        std::printf ("    this row cannot round upward — not reachable here\n");
        return;
    }
    fc_session h = 4242u;
    const long long r0 = requestsNow();
    const fc_session_status st = fc_session_create (&h);
    const long long spent = requestsNow() - r0;
    fpenv::restoreFpEnvironment (saved);
    if (st == FC_SESSION_OK) fc_session_destroy (h);   // keep the table as the tests after this one expect it
    ok (st == FC_SESSION_ERR_FP_ENVIRONMENT, "a create on a thread rounding upward answers FC_SESSION_ERR_FP_ENVIRONMENT");
    ok (h == 4242u && spent == 0, "and writes nothing and allocates nothing");
    fc_session again = 0;
    ok (fc_session_create (&again) == FC_SESSION_OK && fc_session_destroy (again) == FC_SESSION_OK,
        "restored, a create succeeds: the refusal took no slot");
}

void theGenerationRetiresInsteadOfWrapping()
{
    felitronics::test::group ("a slot retires at its last generation instead of wrapping — at the shipped width");
    // Every slot is free here, so each create takes slot 0 (handle & 0xFF == 1) until it retires.
    fc_session first = 0;
    ok (fc_session_create (&first) == FC_SESSION_OK && (first & 0xFFu) == 1u, "PRECONDITION: slot 0 is the free one");
    ok (fc_session_destroy (first) == FC_SESSION_OK, "PRECONDITION: and destroys");
    const fc_session wrappedTwin = (1u << 8) | 1u;   // generation 1 of slot 0 — what a wrap would issue again
    bool allOk = true;
    fc_session h = 0, last = 0;
    std::uint32_t cycles = 0;
    for (;;)
    {
        if (fc_session_create (&h) != FC_SESSION_OK) { allOk = false; break; }
        if ((h & 0xFFu) != 1u) break;                     // slot 0 has retired: this is slot 1
        last = h;
        if (fc_session_destroy (h) != FC_SESSION_OK) { allOk = false; break; }
        ++cycles;
    }
    ok (allOk, "every create and destroy on the way succeeded (" + std::to_string (cycles) + " cycles)");
    ok ((last >> 8) == FC_SESSION_SLOT_GENERATIONS, "slot 0's last handle carries its last generation, "
                                                   + std::to_string (FC_SESSION_SLOT_GENERATIONS));
    ok ((h & 0xFFu) == 2u, "and the next create went to slot 1: slot 0 retired, it did not wrap");
    ok (fc_session_destroy (wrappedTwin) == FC_SESSION_ERR_HANDLE && fc_session_destroy (first) == FC_SESSION_ERR_HANDLE
            && fc_session_destroy (last) == FC_SESSION_ERR_HANDLE,
        "slot 0's handles stay refused — the first, the last, and the number a wrap would have issued again");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "slot 1's session destroys");
}

// THE POISON. Re-entry from INSIDE an allocation the module made, through the shared counter's one-shot hook — the
// position a native new_handler has. The inner call cannot be told apart from the first call after an abandoned one,
// so it must answer POISONED; the outer call returns normally, since it cannot know; and from then on every status
// call answers POISONED, because the poison is a latch and not a flag that the outer call's return clears.
fc_session g_target = 0;
fc_session_status g_inner = FC_SESSION_OK;
int g_innerCalls = 0;
void reenterFromInsideAnAllocation() noexcept
{
    g_inner = fc_session_destroy (g_target);
    ++g_innerCalls;
}

void thePoisonIsForGood()
{
    felitronics::test::group ("an entry point re-entered from inside an allocation poisons the module, for good");
    fc_session h = 0;
    ok (fc_session_create (&h) == FC_SESSION_OK, "PRECONDITION: a live session");
    g_target = h;
    fc_session h2 = 0;
    felitronics::test::alloc::onNext = &reenterFromInsideAnAllocation;   // the next allocation is inside the create
    const fc_session_status outer = fc_session_create (&h2);
    ok (g_innerCalls == 1, "PRECONDITION: an entry point really was called from inside another");
    ok (g_inner == FC_SESSION_ERR_POISONED, "the inner call answers POISONED");
    ok (outer == FC_SESSION_OK && h2 != 0u, "the outer call returns normally: it cannot know");

    fc_session h3 = 777u;
    ok (fc_session_create (&h3) == FC_SESSION_ERR_POISONED && h3 == 777u,
        "from then on a create is refused, and writes nothing");
    ok (fc_session_create (nullptr) == FC_SESSION_ERR_POISONED, "the poison is checked before the out-pointer");
    ok (fc_session_destroy (h) == FC_SESSION_ERR_POISONED && fc_session_destroy (h2) == FC_SESSION_ERR_POISONED,
        "and so is every destroy, of a handle that was live");
    ok (fc_session_destroy (0u) == FC_SESSION_ERR_POISONED, "ahead of the handle check");
    ok (fc_session_abi_version() == FC_SESSION_ABI_VERSION, "the version still answers: it reads no state");
}
} // namespace

int main()
{
    std::printf ("felitronics fc_session ABI tests\n");
    theVersion();
    createChecksItsOutPointer();
    createAndDestroy();
    theTableHoldsItsCapacity();
    theSessionsRefusalPassesThrough();
    theGenerationRetiresInsteadOfWrapping();
    thePoisonIsForGood();   // LAST: the poison is for good, and nothing after it could run
    return felitronics::test::report();
}
