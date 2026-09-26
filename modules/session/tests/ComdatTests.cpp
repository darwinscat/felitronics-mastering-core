// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// INLINE CODE AND THE LINKER — felitronics::session's refusal of a helper whose kept copy was compiled with contraction
// (src/ContractionCanary.h). tests/ComdatForcedCopy.cpp, compiled with -ffp-contract=fast and linked ahead of the
// library, carries a copy of the canary; the linker keeps one copy for the program, the first it meets. Pinned:
//   * on a row that can fuse, the KEPT copy is the forced one and fuses — the premise of the test, checked, not assumed;
//   * create() then refuses with Status::ContractedHelper, gives no session and allocates nothing;
//   * where the row cannot fuse, the kept copy does not, and create() accepts — the check is not reachable there.

#include "FpEnvironmentControl.h"

#include <alloc_counter.h>   // installs the allocation counter: EVERY form of `new`, over-aligned included
#include <felitronics_test.h>
#include <felitronics/session/Session.h>

#include "BuildContract.h"

#include <cstdio>

namespace felitronics::session::testing
{
using CanaryFn = double (*) (double, double, double) noexcept;
CanaryFn forcedCanaryCopy() noexcept;
}

using felitronics::session::Session;
using felitronics::session::Status;
using felitronics::test::ok;

int main()
{
    std::printf ("felitronics session: a contracted copy of a shared inline helper, kept by the linker\n");
    felitronics::test::group ("create() refuses when the kept copy of the canary was compiled with contraction");
    // The pointer the forced translation unit takes is the address of the ONE copy the program kept.
    volatile auto canary = felitronics::session::testing::forcedCanaryCopy();
    volatile double a = 1.0 + 0x1p-27;
    volatile double b = 1.0 + 0x1p-27;
    volatile double c = -(1.0 + 0x1p-26);
    const bool keptFuses = canary (a, b, c) > 0.0;
    if (felitronics::session::testing::kRowCanFuse)
        ok (keptFuses, "PRECONDITION: on this row the kept copy is the forced one, and it fuses across statements");
    else
        std::printf ("    this row has no fused multiply-add: no copy can fuse, the refusal is not reachable here\n");

    const bool libraryPointsThere = felitronics::session::detail::keptCanaryContracts() == keptFuses;
    ok (libraryPointsThere, "the library calls the same kept copy the forced translation unit named");

    const long long before = alloc::count.load();
    auto created = Session::create();
    const long long spent = alloc::count.load() - before;
    if (keptFuses)
    {
        ok (created.status == Status::ContractedHelper && created.session == nullptr,
            "create() refuses with Status::ContractedHelper and gives no session");
        ok (spent == 0, "and asked the heap for nothing");
    }
    else
    {
        ok (created.status == Status::Ok && created.session != nullptr, "the kept copy does not fuse, and create() accepts");
    }
    return felitronics::test::report();
}
