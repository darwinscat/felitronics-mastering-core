// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE BUILD CONTRACT, ANSWERED UNDER THE LIBRARY'S FLAGS (src/BuildContract.h). The probes are src/FpProbes.h's, which
// have internal linkage: the copies called here are compiled in this translation unit, with the flags the target gives
// it, so their answers are the library's. The canary is src/ContractionCanary.h's, which has external linkage: the copy
// called here, through a pointer, is the one the linker kept for the whole program.

#include "BuildGuards.h"

#include "BuildContract.h"
#include "ContractionCanary.h"
#include "FpProbes.h"

namespace felitronics::session::detail
{

BuildProbes buildProbes() noexcept
{
    BuildProbes p;
    p.fusesMultiplyAdd = probes::fusesMultiplyAdd();
    p.dividesByReciprocal = probes::dividesByReciprocal();
    p.reassociatesSums = probes::reassociatesSums();
    p.dropsSignedZeros = probes::dropsSignedZeros();
    return p;
}

bool keptCanaryContracts() noexcept
{
    // Through a `volatile` pointer: a call the compiler cannot inline or resolve at compile time, so it goes to the copy
    // of the canary that the linker kept — this translation unit's, or another's with the same name.
    double (* volatile canary) (double, double, double) noexcept = &contractionCanary;
    // The constants of probes::fusesMultiplyAdd: exactly +0 rounded twice, 2^-54 fused.
    volatile double a = 1.0 + 0x1p-27;
    volatile double b = 1.0 + 0x1p-27;
    volatile double c = -(1.0 + 0x1p-26);
    return canary (a, b, c) > 0.0;
}

} // namespace felitronics::session::detail
