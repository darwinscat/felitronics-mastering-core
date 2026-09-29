// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// Exact decimal helpers for the analyzer hop's required time quantum and the compact numeric project format.
// Slider steps do not constrain edits. These helpers work on exact decimals (a mantissa and decimal places), so
// fractional decimal quanta never depend on a floating-point division happening to produce a whole number.

#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace felitronics::session::detail
{

// m · 10^by, or false when it leaves int64.
inline bool scaleUp (std::int64_t m, int by, std::int64_t& out) noexcept
{
    constexpr std::int64_t limit = std::numeric_limits<std::int64_t>::max() / 10;
    for (; by > 0; --by)
    {
        if (m > limit || m < -limit) return false;
        m *= 10;
    }
    out = m;
    return true;
}

// Is x on the grid that starts at `from` with `step` — is (x − from) a whole number of steps, exactly? All three are
// brought to one scale: no floating point.
inline bool onGrid (const toml::Decimal& x, const toml::Decimal& from, const toml::Decimal& step) noexcept
{
    const int scale = std::max ({ int (x.scale), int (from.scale), int (step.scale) });
    std::int64_t a = 0, f = 0, b = 0;
    if (! scaleUp (x.mantissa, scale - int (x.scale), a) || ! scaleUp (from.mantissa, scale - int (from.scale), f)
        || ! scaleUp (step.mantissa, scale - int (step.scale), b))
        return false;
    constexpr std::int64_t half = std::numeric_limits<std::int64_t>::max() / 2;
    if (a > half || a < -half || f > half || f < -half) return false;
    return b != 0 && (a - f) % b == 0;
}

// The order of two valid decimals, exactly: −1, 0 or 1. They meet at the larger of their scales; the one already there
// is not scaled, and its mantissa is within 2^53 — so if the other leaves int64 on the way up, it is the larger in
// magnitude, and its sign is the answer.
inline int compare (const toml::Decimal& x, const toml::Decimal& y) noexcept
{
    const int scale = std::max (int (x.scale), int (y.scale));
    std::int64_t a = 0, b = 0;
    if (! scaleUp (x.mantissa, scale - int (x.scale), a)) return x.mantissa < 0 ? -1 : 1;
    if (! scaleUp (y.mantissa, scale - int (y.scale), b)) return y.mantissa < 0 ? 1 : -1;
    return a < b ? -1 : (b < a ? 1 : 0);
}

// Exact equality of two doubles, without -Wfloat-equal's objection. −0 equals +0.
inline bool same (double a, double b) noexcept { return ! (a < b) && ! (b < a); }

// A number as the project keeps it: −0 written as +0. The two are one value on every knob, and a recipe is compared by
// its bits.
inline double kept (double x) noexcept { return same (x, 0.0) ? 0.0 : x; }

// THE DECIMAL A DOUBLE IS: the number with the fewest decimal places, one to nine, whose correctly rounded double is x
// — what a person typed or a knob stepped to, given back as the double a shell holds. Nothing when no such number
// exists (x has more places than a knob steps by, or is past 2^53 at one place), and a value with no decimal is on no
// grid. felitronics-toml rounds x · 10^places to the nearest whole mantissa, and its toDouble() divides two exact
// integers once — correctly rounded under IEEE-754 — so "gives x back" is decided exactly.
inline std::optional<toml::Decimal> decimalOf (double x) noexcept
{
    for (std::uint8_t places = 1; places <= 9; ++places)
    {
        const toml::Decimal d = toml::Decimal::fromDouble (x, places);
        if (d.valid() && same (d.toDouble(), x)) return d;
    }
    return std::nullopt;
}

} // namespace felitronics::session::detail
