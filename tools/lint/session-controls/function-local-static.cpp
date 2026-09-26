// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — a function-local static: the "remember the last run" shape the probe ABI has, which the
// session must hold as an owned value instead. run.sh plants this file in modules/session/src/ and requires [GLOBALS]
// on the marked line.

namespace felitronics::session
{
double lastPeak (double peak) noexcept
{
    static constexpr double kFloor = -120.0;   // a constant: allowed
    static double remembered = kFloor;         // VIOLATION
    const double previous = remembered;
    remembered = peak;
    return previous;
}
}
