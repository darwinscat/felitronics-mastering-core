// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a function-local static: the "remember the last run" shape the probe ABI has.

namespace felitronics::session::control
{
double lastPeak (double peak) noexcept
{
    static double rememberedPeak = -120.0;
    const double previous = rememberedPeak;
    rememberedPeak = peak;
    return previous;
}
}
