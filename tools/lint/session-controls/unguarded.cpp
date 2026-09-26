// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a translation unit whose first include is not the build guards.
#include <cstdint>   // VIOLATION
namespace felitronics::session
{
std::uint32_t controlUnguarded ();
std::uint32_t controlUnguarded () { return 1u; }
}
