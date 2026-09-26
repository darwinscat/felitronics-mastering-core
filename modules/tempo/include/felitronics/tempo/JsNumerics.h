// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/core/DetMath.h>

#include <cmath>
#include <limits>

//==============================================================================
// felitronics::tempo::js — the JavaScript arithmetic the tempo port reproduces, each piece with the reason it
// had to be reproduced rather than approximated. The spec is executable (the site's dsp/tempo.js), and every
// function here answers the question "what number does the page compute at this step?".
//
// WHAT IS EXACT AND WHAT IS NOT.
//   * round / min / max / hypot are EXACT: the same bits as V8 (node 26, V8 14.6), because each is defined by a
//     sequence of IEEE-754 operations that are correctly rounded on every row — floor, compare, divide, multiply,
//     add, sqrt. No libm is involved, so these are also the same on every row this repository builds on.
//   * exp is NOT V8's. Math.exp is an fdlibm polynomial; this is 2^(x·log2 e) on the core's deterministic
//     floor (core::det), which is the same function on every row but not the same function as V8's. They agree
//     to a few ulps. It feeds only the octave preference and the window scoring — ranks and a confidence
//     rounded to 0.01 — so an ulp moves a reported number only if a score lands on a rounding tie.
//
// THIS FILE DOES NOT SPELL std::complex, on purpose: the det-math lint governs `std::sqrt` in any file that
// does (a complex sqrt is transcendental), and the scalar sqrt below is IEEE-exact and has to stay ungoverned.
namespace felitronics::tempo::js
{

// Math.round for a finite value: the nearest integer, a tie toward +infinity. Not std::round (ties away from
// zero) and not floor(x + 0.5), which rounds 0.49999999999999994 up. NaN stays NaN and ±Inf stays itself, as
// in JavaScript. (The one place it is not Math.round: x in [-0.5, -0) gives +0 where JavaScript gives -0. No
// value in this port is negative, so the sign of that zero is never observed.)
inline double round (double x) noexcept
{
    const double f = std::floor (x);
    return (x - f >= 0.5) ? f + 1.0 : f;
}

// Math.min / Math.max of two numbers: NaN if either is NaN, and -0 < +0. std::min / std::max answer by the
// ARGUMENT ORDER when a NaN is involved, which is a different function.
inline double min (double a, double b) noexcept
{
    if (std::isnan (a) || std::isnan (b)) return std::numeric_limits<double>::quiet_NaN();
    if (a < b) return a;
    if (b < a) return b;
    return std::signbit (a) ? a : b;               // equal: only the zeros can differ, and -0 is the smaller
}

inline double max (double a, double b) noexcept
{
    if (std::isnan (a) || std::isnan (b)) return std::numeric_limits<double>::quiet_NaN();
    if (a > b) return a;
    if (b > a) return b;
    return std::signbit (a) ? b : a;               // equal: +0 is the larger
}

// Math.hypot(x, y) AS V8 COMPUTES IT (src/builtins/math.tq, MathHypot): the absolute values normalised by the
// largest, squared and summed with Kahan compensation, then sqrt(sum) * max. It is not std::hypot, which is
// the system libm's and not one function: measured against node 26 on 2 000 225 pairs spread over eighty
// decades plus the IEEE edge cases, THIS reproduces V8 on every one and the system hypot differs on 491 004
// (Apple's libm). The order of the special cases is V8's and is observable: an infinity wins over a NaN
// (hypot(NaN, Inf) is +Inf), and a NaN wins over everything else.
//
// The square is PINNED (core::det::mul) because `n * n - comp` is the contractible shape: fused into one FMA it
// rounds once where JavaScript rounds twice, and the page never fuses.
inline double hypot (double x, double y) noexcept
{
    const double v[2] = { x, y };
    double absV[2] = { 0.0, 0.0 };
    bool anyNaN = false;
    double mx = 0.0;
    for (int i = 0; i < 2; ++i)
    {
        if (std::isnan (v[i])) { anyNaN = true; continue; }
        absV[i] = std::fabs (v[i]);
        if (absV[i] > mx) mx = absV[i];
    }
    if (std::isinf (mx)) return mx;
    if (anyNaN) return std::numeric_limits<double>::quiet_NaN();
    if (! (mx > 0.0)) return 0.0;                  // both zero (spelled without == for -Wfloat-equal)
    double sum = 0.0, comp = 0.0;
    for (int i = 0; i < 2; ++i)
    {
        const double n = absV[i] / mx;
        const double summand = core::det::mul (n, n) - comp;
        const double prelim = sum + summand;
        comp = (prelim - sum) - summand;
        sum = prelim;
    }
    return std::sqrt (sum) * mx;
}

// e^x on the deterministic floor: 2^(x · log2 e), with the product formed EXACTLY before it reaches exp2 —
// the Dekker construction core::det::pow10 uses for 10^x, with log2(e) split into a 26-bit head and its tail
// (generated from 80 decimal digits of 1/ln 2; head + tail is log2(e) to 7.7e-27). A plain exp2(x * log2e)
// would lose the low bits of the product, up to ~7 ulp of the result at the x ≈ -10 this port reaches.
// NaN stays NaN; below -1100 the answer is 0 and above +1100 it is +Inf (exp2 decides the exact edges; the
// guard only keeps the Veltkamp split away from an overflow). The port evaluates it at -z^2/2 only: on
// [-10.5, 0] at the default range (|z| <= log2(3)/0.35), and never below -410 at the widest range it admits
// ([1, 1000] BPM). Deep in the subnormals det::exp2 rounds to 0 a little early (2^-1074.8 reads 0).
inline double exp (double x) noexcept
{
    constexpr double kLog2EHi = 1.4426950216293335;           // 0x3ff7154760000000, 26 significant bits
    constexpr double kLog2ELo = 1.9259629911266175e-08;       // 0x3e54ae0bf85ddf44
    constexpr double kLn2     = 6.93147180559945286227e-01;
    constexpr double kSplit   = 134217729.0;                  // 2^27 + 1, Veltkamp
    if (std::isnan (x)) return x;
    if (x < -1100.0) return 0.0;
    if (x > 1100.0) return std::numeric_limits<double>::infinity();
    const double t  = core::det::mul (kSplit, x);
    const double xh = t - (t - x);
    const double xl = x - xh;
    const double hi = core::det::mul (xh, kLog2EHi);                                   // exact: 26 x 26 bits
    const double lo = core::det::mulAdd (xh, kLog2ELo,
                          core::det::mulAdd (xl, kLog2EHi, core::det::mul (xl, kLog2ELo)));
    const double s  = hi + lo;
    const double e  = (hi - s) + lo;                                                   // what the sum dropped
    const double corr = core::det::mulAdd (core::det::mul (e, kLn2),
                                           core::det::mulAdd (e, core::det::mul (0.5, kLn2), 1.0), 1.0);
    return core::det::mul (core::det::exp2 (s), corr);
}

} // namespace felitronics::tempo::js
