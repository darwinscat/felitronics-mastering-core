// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// A COPY OF THE CONTRACTION CANARY COMPILED WITH -ffp-contract=fast (/fp:contract) — modules/session/CMakeLists.txt gives
// this one source that flag, and puts it FIRST on felitronics_session_comdat_tests' link line, ahead of the library. It
// is what an application compiling a header the library shares, under gcc's default flags, looks like to the linker.
// Taking the canary's address makes this translation unit emit its out-of-line copy; the linker keeps one.

#include "ContractionCanary.h"

namespace felitronics::session::testing
{
using CanaryFn = double (*) (double, double, double) noexcept;
CanaryFn forcedCanaryCopy() noexcept;
CanaryFn forcedCanaryCopy() noexcept { return &detail::contractionCanary; }
}
