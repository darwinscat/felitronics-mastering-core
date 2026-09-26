// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// HOSTILE FLAGS AHEAD OF THE LIBRARY'S — felitronics::session's sources compiled into this binary with a licence the
// session refuses PREPENDED to the target's own compile options, the position a consumer's CMAKE_CXX_FLAGS or directory
// options take (modules/session/CMakeLists.txt, felitronics_session_hostile_*). Built twice:
//   contract   -ffp-contract=fast / /fp:contract
//   fastmath   -freciprocal-math -fassociative-math -fno-signed-zeros -fno-trapping-math / /fp:fast
// THIS translation unit is compiled with the same licence and nothing after it, and asks src/FpProbes.h's probes (its
// own copies — internal linkage) first: on this row, does the licence change the answer at all? Where it does, the
// library's probes, compiled with the licence FOLLOWED by the library's options, must answer IEEE anyway. Where the row
// cannot show the change (no fused multiply-add to contract into), the test says so rather than passing it.

#include "FpEnvironmentControl.h"

#include <felitronics_test.h>
#include <felitronics/session/Session.h>

#include "BuildContract.h"
#include "FpProbes.h"

#include <cstdio>

using felitronics::test::ok;
namespace probes = felitronics::session::probes;

int main()
{
    const auto lib = felitronics::session::detail::buildProbes();
#if defined (FELITRONICS_SESSION_HOSTILE_LICENCE_contract)
    std::printf ("felitronics session: -ffp-contract=fast ahead of the library's flags\n");
    felitronics::test::group ("the licence is live here, and the library's flags override it");
    const bool raw = probes::fusesMultiplyAdd();
    if (felitronics::session::testing::kRowCanFuse)
        ok (raw, "CONTROL: under the licence alone this row fuses a*b+c");
    else
        std::printf ("    this row has no fused multiply-add: the licence changes nothing here, the check below cannot fail\n");
    ok (! lib.fusesMultiplyAdd, "the library, built with the licence and then its own flags, does not fuse");
#elif defined (FELITRONICS_SESSION_HOSTILE_LICENCE_fastmath)
    std::printf ("felitronics session: fast-math licences ahead of the library's flags\n");
    felitronics::test::group ("the licences are live here, and the library's flags override them");
    ok (probes::dividesByReciprocal(), "CONTROL: under the licence alone x / 3 becomes x * (1/3) on this row");
    ok (probes::reassociatesSums(), "CONTROL: under the licence alone (a + 1) + 1 becomes a + 2 on this row");
    ok (probes::dropsSignedZeros(), "CONTROL: under the licence alone -(x - y) at x == y loses its sign on this row");
    ok (! lib.dividesByReciprocal, "the library divides");
    ok (! lib.reassociatesSums, "the library adds in the order written");
    ok (! lib.dropsSignedZeros, "the library keeps -(x - y) = -0");
#else
    #error "build HostileFlagsTests.cpp with one FELITRONICS_SESSION_HOSTILE_LICENCE_* defined"
#endif
    auto c = felitronics::session::Session::create();
    ok (c.status == felitronics::session::Status::Ok && c.session != nullptr,
        "and a session is created: the library's own copies decide, not the licence's");
    return felitronics::test::report();
}
