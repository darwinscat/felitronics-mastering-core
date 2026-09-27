// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for felitronics::session — its creation, and the laws of docs/SESSION.md that a running program
// can check. The others are held by the build (modules/session/CMakeLists.txt and its controls), by the object-file gates
// and by the session-laws lint; the states and the commands by tests/StateTests.cpp. Pinned here:
//   * MEMORY IS DECLARED BEFORE THE WORK: what create() asks the heap for, through the one allocation counter, against
//     Session::createBytes() — and the harness that compares them, proven able to fail on a sample built to fail it;
//   * create() and destruction: a fresh object per call, and destroyed by its owner;
//   * THE FLOATING-POINT ENVIRONMENT: create() refuses a thread that flushes to zero, reads subnormals as zero or rounds
//     other than to nearest — set here the way a host would — having allocated nothing, and accepts it again once the
//     environment is restored;
//   * the two releases the library reports, against the ones this build was configured with;
//   * THE BUILD PROBES, asked of the library from THIS translation unit, which modules/session/CMakeLists.txt compiles
//     with contraction forced ON (-ffp-contract=fast, /fp:contract) — a caller whose flags the library's arithmetic must
//     not follow. On a row that can fuse, the caller's own copy of the probe MUST fuse, or the control is dead.

#include "DeclaredBudget.h"   // installs the allocation counter: EVERY form of `new`, over-aligned included
#include "FpEnvironmentControl.h"

#include <felitronics_test.h>
#include <felitronics/session/Session.h>

#include "BuildContract.h"
#include "FpProbes.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using felitronics::session::Session;
using felitronics::session::Status;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;
namespace fpenv = felitronics::session::testing;

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
    felitronics::session::Created c;
    const budget::Spent spent = budget::spend ([&] { c = Session::create(); });
    ok (c.status == Status::Ok && c.session != nullptr, "PRECONDITION: create() returned a session");
    // NOT A COMPARISON OF ZERO WITH ZERO. A counter that saw nothing would satisfy `declared >= requested` for any
    // declaration at all; the create must have been seen asking.
    ok (spent.requests >= 1 && spent.bytes > 0,
        "the counter saw the create ask the heap (" + budget::describe (declared, spent) + ")");
    ok (budget::covers (declared, spent), "declared >= requested — " + budget::describe (declared, spent));
    // Exact today, by construction (one expression sizes both, src/Session.cpp): pinned so that a create that starts
    // asking for less than it declares is seen as well, not only one that asks for more.
    ok (spent.bytes == (long long) declared, "and exactly the declaration, while create() is one object");
    ok (spent.requests == 1, "in one request: the session holds no container that asks for a proxy of its own (got "
                             + std::to_string (spent.requests) + ")");

    const budget::Spent freed = budget::spend ([&] { c.session.reset(); });
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
    ok (a.status == Status::Ok && b.status == Status::Ok && a.session != nullptr && b.session != nullptr,
        "both creates returned a session");
    ok (a.session.get() != b.session.get(), "two creates are two objects");
    a.session.reset();
    ok (a.session == nullptr && b.session != nullptr, "destroying one leaves the other");
    auto c = Session::create();
    ok (c.status == Status::Ok && c.session != nullptr, "a create after a destroy returns a session");
}

// One refused create, under an environment `set` put in place: the status, no session, and no allocation.
void refusedUnder (const char* what, bool (*set)() noexcept)
{
    const auto saved = fpenv::saveFpEnvironment();
    if (! set())
    {
        fpenv::restoreFpEnvironment (saved);
        std::printf ("    %s: this row has no such control — not reachable here\n", what);
        return;
    }
    const Status env = Session::checkFloatingPointEnvironment();
    felitronics::session::Created c;
    const budget::Spent spent = budget::spend ([&] { c = Session::create(); });
    fpenv::restoreFpEnvironment (saved);
    ok (env == Status::FloatingPointEnvironment, std::string (what) + ": checkFloatingPointEnvironment() says so");
    ok (c.status == Status::FloatingPointEnvironment && c.session == nullptr,
        std::string (what) + ": create() refuses with Status::FloatingPointEnvironment and gives no session");
    ok (spent.requests == 0, std::string (what) + ": and asked the heap for nothing");
}

void theFloatingPointEnvironmentIsRefused()
{
    felitronics::test::group ("create() refuses a thread whose floating-point environment is not IEEE-754's default");
    ok (Session::checkFloatingPointEnvironment() == Status::Ok, "PRECONDITION: this thread starts at the default");
    refusedUnder ("flush-to-zero", [] () noexcept { return fpenv::setFlushToZero(); });
    refusedUnder ("denormals-are-zero", [] () noexcept { return fpenv::setDenormalsAreZero(); });
    refusedUnder ("rounding upward", [] () noexcept { return fpenv::setRounding (fpenv::kRoundUpward); });
    refusedUnder ("rounding downward", [] () noexcept { return fpenv::setRounding (fpenv::kRoundDownward); });
    refusedUnder ("rounding toward zero", [] () noexcept { return fpenv::setRounding (fpenv::kRoundTowardZero); });
    auto again = Session::create();
    ok (again.status == Status::Ok && again.session != nullptr,
        "and once the environment is restored, create() accepts again — the refusal changed nothing");
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

void theLibraryKeepsItsFlags()
{
    felitronics::test::group ("build contract: the library's own arithmetic follows its flags, not its caller's");
    // THIS translation unit is compiled with contraction forced on, so its OWN copy of the probe (src/FpProbes.h has
    // internal linkage) fuses wherever the row has a fused multiply-add. That is the control, and on such a row it is
    // REQUIRED: a positive control that stopped fusing would leave the library's line below proving nothing.
    const bool callerFuses = felitronics::session::probes::fusesMultiplyAdd();
    if (fpenv::kRowCanFuse)
        ok (callerFuses, "this row can fuse, and the caller's copy did (-ffp-contract=fast / /fp:contract) — the check below is live");
    else
        std::printf ("    this row has no fused multiply-add to contract into (baseline x86-64): the check below cannot fail here\n");
    const auto p = felitronics::session::detail::buildProbes();
    ok (! p.fusesMultiplyAdd, "the library rounds a*b and then +c separately — -ffp-contract=off reached it");
    ok (! p.dividesByReciprocal && ! p.reassociatesSums && ! p.dropsSignedZeros,
        "and divides, adds and keeps signed zeros as IEEE-754 says — no fast-math licence reached it");
}
} // namespace

int main()
{
    std::printf ("felitronics session::Session tests\n");
    theDemandOfCreateCoversWhatItAsksFor();
    theHarnessCanFail();
    createAndDestroy();
    theFloatingPointEnvironmentIsRefused();
    theReleasesAreTheBuilds();
    theLibraryKeepsItsFlags();
    return felitronics::test::report();
}
