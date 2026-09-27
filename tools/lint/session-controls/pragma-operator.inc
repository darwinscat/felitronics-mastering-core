// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a pragma spelled as an operator, which a #pragma rule reading directives would not see.
namespace felitronics::session
{
double controlPragma (double a, double b, double c) noexcept;
double controlPragma (double a, double b, double c) noexcept
{
    _Pragma ("clang fp contract(fast)")   // VIOLATION
    return a * b + c;
}
}
