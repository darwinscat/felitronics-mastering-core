// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE BUILD CONTRACT, AS THE LIBRARY ANSWERS IT — private to felitronics::session and its tests, not part of its API.
// Each answer is computed in src/BuildContract.cpp, under the library's own flags, so it is true of the compiled library
// and not of whoever calls it. docs/SESSION.md says what each one holds.

namespace felitronics::session::detail
{
// What the compiler did to the library's own arithmetic (src/FpProbes.h has the four probes). All false, or the
// library was not built with its flags.
struct BuildProbes
{
    bool fusesMultiplyAdd = false;      // -ffp-contract=off did not arrive
    bool dividesByReciprocal = false;   // a reciprocal licence (fast-math, -freciprocal-math, /fp:fast) did
    bool reassociatesSums = false;      // an associative licence did
    bool dropsSignedZeros = false;      // a no-signed-zeros licence did
};
[[nodiscard]] BuildProbes buildProbes() noexcept;

// Does the copy of detail::contractionCanary that the linker KEPT fuse across statements (src/ContractionCanary.h)?
[[nodiscard]] bool keptCanaryContracts() noexcept;
} // namespace felitronics::session::detail
