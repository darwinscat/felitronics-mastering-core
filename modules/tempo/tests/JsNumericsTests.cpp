// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for tempo::js (JsNumerics.h) — the JavaScript arithmetic the tempo port reproduces.
//   * Math.hypot: V8's own answers, printed by node 26 (V8 14.6) and copied as bit patterns — the IEEE edges, the
//     cases a naive sqrt(x*x + y*y) gets wrong, and six pairs on which the system hypot of the machine this was
//     written on (Apple's libm) gives a DIFFERENT double. Those six are why the port carries V8's algorithm: they
//     are one ulp apart, and a magnitude is summed into every onset frame. (Measured over 2 000 225 pairs: V8's
//     algorithm here matched node on every one, the system hypot missed 491 004.)
//   * Math.round / Math.min / Math.max: the rounding direction, NaN, and the signed zeros, as the spec defines them.
//   * exp: NOT V8's (JsNumerics.h says why), so it is held to what it does claim — the V8 values within 2 ulp, the
//     system exp (a different construction) within 4 ulp over the range the port uses, exact at 0, monotone, and
//     the right answer at the edges.

#include <felitronics_test.h>
#include <felitronics/tempo/JsNumerics.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace js = felitronics::tempo::js;
using felitronics::test::ok;

namespace
{
std::uint64_t bits (double d) { std::uint64_t u; std::memcpy (&u, &d, 8); return u; }
double fromBits (std::uint64_t u) { double d; std::memcpy (&d, &u, 8); return d; }
std::string hex (double d) { char b[24]; std::snprintf (b, sizeof b, "%016llx", (unsigned long long) bits (d)); return b; }
std::string num (double v) { char b[48]; std::snprintf (b, sizeof b, "%.17g", v); return b; }

// Distance in ulps between two finite doubles of the same sign.
std::uint64_t ulps (double a, double b)
{
    const std::uint64_t x = bits (a), y = bits (b);
    return x > y ? x - y : y - x;
}

// A value read through a volatile, so no compiler folds the call under test at compile time with its OWN arithmetic.
double opaque (double v) { volatile double x = v; return x; }

void hypotIsV8s()
{
    felitronics::test::group ("js::hypot is V8's Math.hypot, bit for bit");
    const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    struct W { double x, y; std::uint64_t v8; const char* what; };
    const W w[] = {
        { 3.0, 4.0,          0x4014000000000000ull, "3, 4" },
        { 1e308, 1e308,      0x7fe92c80954c51f5ull, "1e308, 1e308 — no overflow (the naive form is +Inf)" },
        { 5e-324, 5e-324,    0x0000000000000001ull, "the smallest subnormal twice (the naive form is 0)" },
        { 1e-310, 3e-310,    0x00003a365ff2ea11ull, "two subnormals (the naive form is 0)" },
        { 0.1, 0.2,          0x3fcc9f25c5bfeddaull, "0.1, 0.2" },
        { 123456.789, 0.000123, 0x40fe240c9fbe76c9ull, "twelve decades apart" },
        { nan, inf,          0x7ff0000000000000ull, "NaN, Inf — an infinity wins over a NaN" },
        { -0.0, 0.0,         0x0000000000000000ull, "-0, +0 is +0" },
        // Pairs on which Apple's hypot answers one ulp away from V8.
        { fromBits (0x4008000000000000ull), fromBits (0x4008000000000000ull), 0x4010f876ccdf6cdaull, "3, 3" },
        { fromBits (0x01a56e1fc2f8f359ull), fromBits (0x01a56e1fc2f8f359ull), 0x01ae4e8d12762226ull, "a tiny equal pair" },
        { fromBits (0xc0b9d10b5b75132cull), fromBits (0x40c22edac791f89bull), 0x40c64c8cb1111ab6ull, "-6609.04, 9309.71" },
        { fromBits (0xbf4db7cda1ccf392ull), fromBits (0xbe9b50898555f9dfull), 0x3f4db7cdd403028bull, "four decades apart, negative" },
        { fromBits (0xc26934e38258c77dull), fromBits (0xc1c94cbdf79a4d10ull), 0x426934e44d7e3c45ull, "large, both negative" },
        { fromBits (0x3d6bedf692087f8bull), fromBits (0x3d9f6447d1f6a5a3ull), 0x3d9f95d3840262fdull, "small, one ten times the other" },
    };
    for (const W& t : w)
    {
        const double got = js::hypot (opaque (t.x), opaque (t.y));
        ok (bits (got) == t.v8, std::string ("hypot(") + t.what + ") = " + hex (got) + ", V8 " + hex (fromBits (t.v8)));
        const double swapped = js::hypot (opaque (t.y), opaque (t.x));
        ok (bits (swapped) == t.v8, std::string ("... and with the arguments swapped"));
    }
    ok (std::isnan (js::hypot (nan, 1.0)) && std::isnan (js::hypot (1.0, nan)), "a NaN with a finite number is NaN");
    ok (js::hypot (-inf, 1.0) == inf && js::hypot (1.0, -inf) == inf, "an infinity of either sign is +Inf");
}

void roundMinMaxAreTheSpecs()
{
    felitronics::test::group ("js::round / min / max are Math.round / Math.min / Math.max");
    const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    struct R { double x, want; };
    const R r[] = { { 0.49999999999999994, 0.0 }, { 0.5, 1.0 }, { 1.5, 2.0 }, { 2.5, 3.0 }, { -2.5, -2.0 }, { -2.5000000000000004, -3.0 },
                    { 4503599627370497.0, 4503599627370497.0 }, { 1203.0000000000002, 1203.0 }, { 1201.5, 1202.0 }, { inf, inf }, { -inf, -inf } };
    for (const R& t : r)
        ok (bits (js::round (opaque (t.x))) == bits (t.want), "round(" + num (t.x) + ") = " + num (t.want) + " (got " + num (js::round (t.x)) + ")");
    ok (std::isnan (js::round (nan)), "round(NaN) is NaN");
    // The documented deviation, pinned so a reader sees it rather than trips on it: JavaScript's -0 here is +0.
    ok (bits (js::round (-0.25)) == bits (0.0), "round(-0.25) is +0 here where JavaScript says -0 — no value in the port is negative");

    ok (std::isnan (js::min (nan, 1.0)) && std::isnan (js::min (1.0, nan)) && std::isnan (js::max (nan, 1.0)) && std::isnan (js::max (1.0, nan)),
        "a NaN on either side is NaN (std::min / std::max answer by argument order instead)");
    ok (std::signbit (js::min (0.0, -0.0)) && std::signbit (js::min (-0.0, 0.0)), "min(+0, -0) is -0, in both orders");
    ok (! std::signbit (js::max (0.0, -0.0)) && ! std::signbit (js::max (-0.0, 0.0)), "max(+0, -0) is +0, in both orders");
    ok (js::min (1.0, -3.0) == -3.0 && js::max (1.0, -3.0) == 1.0 && js::min (-inf, 5.0) == -inf && js::max (inf, 5.0) == inf,
        "and otherwise the smaller and the larger");
}

void expIsCloseAndDeterministic()
{
    felitronics::test::group ("js::exp — the V8 values within 2 ulp, the system exp within 4, exact at 0");
    ok (bits (js::exp (opaque (0.0))) == bits (1.0), "exp(0) is exactly 1");
    struct V { double x; std::uint64_t v8; };
    const V v[] = { { -10.0, 0x3f07cd79b5647c9aull }, { -1.0, 0x3fd78b56362cef38ull }, { -0.5, 0x3fe368b2fc6f960aull },
                    { 1.0, 0x4005bf0a8b145769ull }, { 0.001, 0x3ff0041919b7ee34ull }, { -4.5, 0x3f86c0504695c417ull },
                    { -7.25, 0x3f47455fe323fafeull }, { -0.125, 0x3fec3d6a24ed8222ull } };
    for (const V& t : v)
    {
        const double got = js::exp (opaque (t.x));
        ok (ulps (got, fromBits (t.v8)) <= 2, "exp(" + num (t.x) + ") = " + hex (got) + ", V8 " + hex (fromBits (t.v8))
                                              + " — " + std::to_string (ulps (got, fromBits (t.v8))) + " ulp");
    }
    // The range the port evaluates: -z^2/2 with |z| <= log2(3)/0.35 < 4.6, i.e. [-10.5, 0]; swept a little wider.
    std::uint64_t worst = 0; double worstAt = 0.0; bool monotone = true; double prev = 0.0;
    for (int i = 0; i <= 130000; ++i)
    {
        const double x = -12.0 + (double) i * 1.0e-4;
        const double got = js::exp (opaque (x));
        const std::uint64_t u = ulps (got, std::exp (x));
        if (u > worst) { worst = u; worstAt = x; }
        if (i > 0 && got < prev) monotone = false;
        prev = got;
    }
    ok (worst <= 4, "130 001 points over [-12, 1]: within " + std::to_string (worst) + " ulp of the system exp (worst at " + num (worstAt) + ")");
    ok (monotone, "and never decreasing");
    ok (bits (js::exp (-1200.0)) == bits (0.0) && js::exp (1200.0) == std::numeric_limits<double>::infinity()
            && std::isnan (js::exp (std::numeric_limits<double>::quiet_NaN())),
        "exp(-1200) is +0, exp(1200) is +Inf, exp(NaN) is NaN");
    // The overflow edge is binary64's; the deep-subnormal underflow edge is det::exp2's, which rounds 2^x to 0 a
    // little before binary64 must (2^-1074.8 reads 0, not 2^-1074) — far outside anything the port evaluates.
    ok (js::exp (-700.0) > 0.0 && std::isfinite (js::exp (709.0)) && std::isinf (js::exp (710.0)),
        "exp(-700) is a positive number, exp(709) is finite, exp(710) is +Inf");
}
} // namespace

int main()
{
    std::printf ("felitronics tempo::js numerics tests\n");
    hypotIsV8s();
    roundMinMaxAreTheSpecs();
    expIsCloseAndDeterministic();
    return felitronics::test::report();
}
