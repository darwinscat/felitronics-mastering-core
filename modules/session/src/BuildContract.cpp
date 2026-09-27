// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE BUILD CONTRACT, ANSWERED UNDER THE LIBRARY'S FLAGS (src/BuildContract.h). The probes are src/FpProbes.h's, which
// have internal linkage: the copies called here are compiled in this translation unit, with the flags the target gives
// it, so their answers are the library's.

#include "BuildGuards.h"

#include "BuildContract.h"
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

} // namespace felitronics::session::detail
