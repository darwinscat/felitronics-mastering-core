// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for felitronics::session — the empty session, and the laws of docs/SESSION.md that a running
// program can check. The others are held by the build (modules/session/CMakeLists.txt, src/BuildContract.cpp and the
// controls in tests/controls/) and by the session-laws lint (tools/lint/check-session-laws.mjs). Pinned here:
//   * MEMORY IS DECLARED BEFORE THE WORK: what create() asks the heap for, through the one allocation counter, against
//     Session::createBytes() — and the harness that compares them, proven able to fail on a sample built to fail it;
//   * create() and destruction: a fresh object per call, never null, and destroyed by its owner;
//   * the two releases the library reports, against the ones this build was configured with;
//   * THE CONTRACTION PROBE, asked of the library from THIS translation unit, which modules/session/CMakeLists.txt
//     compiles with contraction forced ON (-ffp-contract=fast, /fp:contract) — a consumer whose flags the library's
//     arithmetic must not follow.

#include "DeclaredBudget.h"   // installs the allocation counter: EVERY form of `new`, over-aligned included

#include <felitronics_test.h>
#include <felitronics/session/Session.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using felitronics::session::Session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;

namespace
{
std::string str (const felitronics::session::Version& v)
{
    return std::to_string (v.major) + "." + std::to_string (v.minor) + "." + std::to_string (v.patch);
}

void theDemandOfCreateCoversWhatItAsksFor()
{
    felitronics::test::group ("memory is declared before the work: createBytes() covers what create() asks the heap for");
    const std::uint64_t declared = Session::createBytes();
    std::unique_ptr<Session> s;
    const budget::Spent spent = budget::spend ([&] { s = Session::create(); });
    ok (s != nullptr, "PRECONDITION: create() returned a session");
    // NOT A COMPARISON OF ZERO WITH ZERO. A counter that saw nothing would satisfy `declared >= requested` for any
    // declaration at all; the create must have been seen asking.
    ok (spent.requests >= 1 && spent.bytes > 0,
        "the counter saw the create ask the heap (" + budget::describe (declared, spent) + ")");
    ok (budget::covers (declared, spent), "declared >= requested — " + budget::describe (declared, spent));
    // Exact today, by construction (one expression sizes both, src/Session.cpp): pinned so that a create that starts
    // asking for less than it declares is seen as well, not only one that asks for more.
    ok (spent.bytes == (long long) declared, "and exactly the declaration, while create() is one object");

    const budget::Spent freed = budget::spend ([&] { s.reset(); });
    ok (freed.requests == 0, "destroying a session asks the heap for nothing");
}

void theHarnessCanFail()
{
    felitronics::test::group ("control: the budget check goes red on a sample that asks for more than it declares");
    // A check that cannot fail is not a check. This sample declares 16 bytes and then asks for 64, so covers() must say
    // no — through exactly the path the session's own line above goes through.
    std::vector<unsigned char> v;
    const budget::Spent spent = budget::spend ([&] { v.resize (64); });
    ok (spent.requests >= 1 && spent.bytes >= 64, "PRECONDITION: the sample really asked for its 64 bytes");
    ok (! budget::covers (16, spent), "an under-declared demand is caught — " + budget::describe (16, spent));
    ok (budget::covers ((std::uint64_t) spent.bytes, spent), "and a declaration equal to the request passes");
}

void createAndDestroy()
{
    felitronics::test::group ("create() gives a fresh session each time; its owner destroys it");
    auto a = Session::create();
    auto b = Session::create();
    ok (a != nullptr && b != nullptr, "both creates returned a session");
    ok (a.get() != b.get(), "two creates are two objects");
    a.reset();
    ok (a == nullptr && b != nullptr, "destroying one leaves the other");
    auto c = Session::create();
    ok (c != nullptr, "a create after a destroy returns a session");
}

void theReleasesAreTheBuilds()
{
    felitronics::test::group ("the library reports the releases this build was configured with");
    const auto v = Session::version();
    const auto core = Session::coreVersion();
    ok (v.major == FELITRONICS_SESSION_VERSION_MAJOR && v.minor == FELITRONICS_SESSION_VERSION_MINOR
            && v.patch == FELITRONICS_SESSION_VERSION_PATCH,
        "felitronics-mastering-core " + str (v));
    ok (core.major == FELITRONICS_SESSION_CORE_VERSION_MAJOR && core.minor == FELITRONICS_SESSION_CORE_VERSION_MINOR
            && core.patch == FELITRONICS_SESSION_CORE_VERSION_PATCH,
        "felitronics-core " + str (core));
    ok (v.major + v.minor + v.patch > 0 && core.major + core.minor + core.patch > 0,
        "neither is the 0.0.0 of a build that said nothing");
}

void theLibraryDoesNotContract()
{
    felitronics::test::group ("build contract: the library's own code does not fuse a*b+c, whatever its caller's flags");
    // THIS translation unit is compiled with contraction forced on, so the same expression computed HERE may fuse —
    // on a machine that has a fused multiply-add to fuse into. That is the control: it shows the constants separate a
    // fused result from a twice-rounded one, and whether this row can see the property at all.
    volatile double a = 1.0 + 0x1p-27;
    volatile double b = 1.0 + 0x1p-27;
    volatile double c = -(1.0 + 0x1p-26);
    const double here = a * b + c;
    const bool callerFuses = here > 0.0;
    std::printf ("    caller (this TU, contraction forced on): %s\n",
                 callerFuses ? "fused — the check below is live on this row"
                             : "not fused — this ISA has no FMA to contract into, so the check below cannot fail here");
    ok (! felitronics::session::fusesMultiplyAdd(),
        "the library rounds a*b and then +c separately — -ffp-contract=off reached its translation units");
}
} // namespace

int main()
{
    std::printf ("felitronics session::Session tests\n");
    theDemandOfCreateCoversWhatItAsksFor();
    theHarnessCanFail();
    createAndDestroy();
    theReleasesAreTheBuilds();
    theLibraryDoesNotContract();
    return felitronics::test::report();
}
