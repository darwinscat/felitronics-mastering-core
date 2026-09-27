// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE FLOATING-POINT PROBES — each one a tiny computation whose IEEE-754 answer is known exactly and whose answer under a
// licence the session refuses is known to differ. Two kinds:
//
//   what the COMPILER did (flags):   a fused multiply-add, a division turned into a reciprocal multiply, a reassociated
//                                    sum, a dropped signed zero. Answered by whatever translation unit includes this
//                                    header, under THAT unit's flags — src/BuildContract.cpp includes it to answer for
//                                    the library; a test includes it to show that the licence really changes the answer
//                                    on its row (the control of the control).
//   what the THREAD is set to (runtime state): flush-to-zero, denormals-are-zero, a rounding mode other than
//                                    to-nearest. Answered by the floating-point environment of the calling thread, which
//                                    a host may have changed (an audio thread with FTZ set is the usual one), and which
//                                    no flag of this library can reach. Read with ordinary arithmetic, not <cfenv>: the
//                                    arithmetic is what the session's numbers go through, and it has no symbol to link.
//
// INTERNAL LINKAGE, ON PURPOSE: an anonymous namespace, so every translation unit that includes this has its OWN copy,
// compiled with its OWN flags. An inline function with external linkage would be one copy for the whole program, chosen
// by the linker from whichever unit it met first.
//
// Every operand is `volatile`, so nothing is folded at compile time — where the flags under test would not apply.

#include <cstdint>
#include <cstring>

namespace felitronics::session::probes
{
namespace
{
    // a*b + c in ONE expression. (1 + 2^-27)^2 = 1 + 2^-26 + 2^-54: rounded on its own the product is 1 + 2^-26 (the
    // 2^-54 is below half an ulp), so adding -(1 + 2^-26) gives exactly +0; one fused rounding keeps the 2^-54.
    inline bool fusesMultiplyAdd() noexcept
    {
        volatile double a = 1.0 + 0x1p-27;
        volatile double b = 1.0 + 0x1p-27;
        volatile double c = -(1.0 + 0x1p-26);
        const double r = a * b + c;
        return r > 0.0;
    }

    // x / 3 by division is 5/3 correctly rounded, 0x1.aaaaaaaaaaaabp+0. Under a reciprocal licence the compiler writes
    // x * (1/3), and 5 * 0x1.5555555555555p-2 rounds to 0x1.aaaaaaaaaaaaap+0 — one ulp lower.
    inline bool dividesByReciprocal() noexcept
    {
        volatile double x = 5.0;
        const double q = x / 3.0;
        return q < 0x1.aaaaaaaaaaaabp+0;
    }

    // (a + 1) + 1 at a = 2^53: 2^53 + 1 is a tie, and ties go to the even 2^53, twice. Reassociated to a + 2 it is
    // 2^53 + 2.
    inline bool reassociatesSums() noexcept
    {
        volatile double a = 0x1p53;
        const double r = (a + 1.0) + 1.0;
        return r > 0x1p53;
    }

    // -(x - y) at x == y is -0: the difference is +0, and negating it sets the sign. A compiler that may ignore signed
    // zeros rewrites -(x - y) as y - x, which is +0. The sign is read from the BITS — a floating-point test of it
    // (1/r < 0, signbit) is itself something the licence may fold. (The obvious probe, -0 + 0 folded to -0, is not
    // taken by gcc 14 on aarch64; this one changed under the licence on every row measured: gcc 14 x86-64 and aarch64,
    // Apple clang 14 x86-64 and 21 arm64, emscripten 6.0.9.)
    inline bool dropsSignedZeros() noexcept
    {
        volatile double x = 1.0;
        volatile double y = 1.0;
        const double r = -(x - y);
        std::uint64_t bits = 0;
        std::memcpy (&bits, &r, sizeof bits);
        return (bits >> 63) == 0u;
    }

    // FLUSH-TO-ZERO: the smallest normal, halved, is a subnormal result; a flushing thread returns 0. Asked of double and
    // float, which have one control on x86 (MXCSR) and on arm64 (FPCR.FZ) but are asked of separately so a platform that
    // splits them is caught too.
    inline bool flushesSubnormalResults() noexcept
    {
        volatile double d = 0x1p-1022;
        volatile float f = 0x1p-126f;
        const double rd = d * 0.5;
        const float rf = f * 0.5f;
        return ! (rd > 0.0) || ! (rf > 0.0f);
    }

    // DENORMALS-ARE-ZERO: the smallest subnormal, scaled by 2^60, is a normal result; a thread that reads subnormal
    // INPUTS as zero returns 0. (Flush-to-zero alone does not: the result is normal.)
    inline bool readsSubnormalsAsZero() noexcept
    {
        volatile double d = 0x1p-1074;
        volatile float f = 0x1p-149f;
        const double rd = d * 0x1p60;
        const float rf = f * 0x1p60f;
        return ! (rd > 0.0) || ! (rf > 0.0f);
    }

    // ROUND TO NEAREST: 1 + 0.75 ulp rounds up to 1 + 2^-52 only to nearest and upward; -1 - 0.75 ulp rounds away from
    // zero only to nearest and downward. Both at once is to-nearest and nothing else.
    inline bool roundsToNearest() noexcept
    {
        volatile double one = 1.0;
        volatile double q = 0x1.8p-53;
        volatile float onef = 1.0f;
        volatile float qf = 0x1.8p-24f;
        const double up = one + q;
        const double down = -one - q;
        const float upf = onef + qf;
        const float downf = -onef - qf;
        return up > 1.0 && down < -1.0 && upf > 1.0f && downf < -1.0f;
    }
}
} // namespace felitronics::session::probes
