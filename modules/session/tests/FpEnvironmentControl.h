// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// SETTING THE FLOATING-POINT ENVIRONMENT THE WAY A HOST WOULD — for the tests that require felitronics::session to refuse
// it. Test code only: the library itself never touches the environment, and does not include this. Each setter returns
// false where the row has no such control (the wasm tier has none: it cannot flush and rounds only to nearest), and the
// test then says the check is not reachable on this row instead of passing it. Every setter confirms its effect with the
// test's own arithmetic before it reports success.
//
//   flush-to-zero        x86: MXCSR.FTZ (bit 15)    arm64: FPCR.FZ (bit 24), which also reads subnormal inputs as zero
//   denormals-are-zero   x86: MXCSR.DAZ (bit 6)     arm64: FPCR.FZ again (there is no separate bit)
//   rounding             <cfenv> fesetround, where the platform offers the mode

#include <cfenv>
#include <cstdint>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
    #include <xmmintrin.h>
    #define FELITRONICS_SESSION_TEST_MXCSR 1
#elif defined (__aarch64__) && ! defined (_MSC_VER)
    #define FELITRONICS_SESSION_TEST_FPCR 1
#endif

namespace felitronics::session::testing
{
#if defined (FELITRONICS_SESSION_TEST_FPCR)
inline std::uint64_t readFpcr() noexcept { std::uint64_t v; __asm__ volatile ("mrs %0, fpcr" : "=r" (v)); return v; }
inline void writeFpcr (std::uint64_t v) noexcept { __asm__ volatile ("msr fpcr, %0" : : "r" (v)); }
#endif

// The environment as it was, to put back.
struct SavedFpEnvironment
{
    std::fenv_t env {};
#if defined (FELITRONICS_SESSION_TEST_MXCSR)
    unsigned int mxcsr = 0;
#elif defined (FELITRONICS_SESSION_TEST_FPCR)
    std::uint64_t fpcr = 0;
#endif
};

inline SavedFpEnvironment saveFpEnvironment() noexcept
{
    SavedFpEnvironment s;
    std::fegetenv (&s.env);
#if defined (FELITRONICS_SESSION_TEST_MXCSR)
    s.mxcsr = _mm_getcsr();
#elif defined (FELITRONICS_SESSION_TEST_FPCR)
    s.fpcr = readFpcr();
#endif
    return s;
}

inline void restoreFpEnvironment (const SavedFpEnvironment& s) noexcept
{
    std::fesetenv (&s.env);
#if defined (FELITRONICS_SESSION_TEST_MXCSR)
    _mm_setcsr (s.mxcsr);
#elif defined (FELITRONICS_SESSION_TEST_FPCR)
    writeFpcr (s.fpcr);
#endif
}

// WHETHER A SETTER TOOK EFFECT, read by the test's own arithmetic — a platform may accept a request and ignore it (the
// wasm tier's fesetround returns 0 for a mode it cannot honour), and a refusal test on an environment that never changed
// would be a test of nothing. The same known-answer computations as the library's probes, compiled here.
inline bool flushesNow() noexcept
{
    volatile double d = 0x1p-1022;
    const double r = d * 0.5;
    return ! (r > 0.0);
}
inline bool readsSubnormalsAsZeroNow() noexcept
{
    volatile double d = 0x1p-1074;
    const double r = d * 0x1p60;
    return ! (r > 0.0);
}
inline bool roundsToNearestNow() noexcept
{
    volatile double one = 1.0;
    volatile double q = 0x1.8p-53;
    const double up = one + q;
    const double down = -one - q;
    return up > 1.0 && down < -1.0;
}

inline bool setFlushToZero() noexcept
{
#if defined (FELITRONICS_SESSION_TEST_MXCSR)
    _mm_setcsr (_mm_getcsr() | 0x8000u);
#elif defined (FELITRONICS_SESSION_TEST_FPCR)
    writeFpcr (readFpcr() | (std::uint64_t (1) << 24));
#endif
    return flushesNow();
}

inline bool setDenormalsAreZero() noexcept
{
#if defined (FELITRONICS_SESSION_TEST_MXCSR)
    _mm_setcsr (_mm_getcsr() | 0x0040u);
#elif defined (FELITRONICS_SESSION_TEST_FPCR)
    writeFpcr (readFpcr() | (std::uint64_t (1) << 24));
#endif
    return readsSubnormalsAsZeroNow();
}

// fesetround with a mode the platform may not define; false where it has no such mode, refuses it, or accepts it and
// goes on rounding to nearest.
inline bool setRounding (int mode) noexcept { return mode >= 0 && std::fesetround (mode) == 0 && ! roundsToNearestNow(); }

#if defined (FE_UPWARD)
constexpr int kRoundUpward = FE_UPWARD;
#else
constexpr int kRoundUpward = -1;
#endif
#if defined (FE_DOWNWARD)
constexpr int kRoundDownward = FE_DOWNWARD;
#else
constexpr int kRoundDownward = -1;
#endif
#if defined (FE_TOWARDZERO)
constexpr int kRoundTowardZero = FE_TOWARDZERO;
#else
constexpr int kRoundTowardZero = -1;
#endif

// The rows where a contraction that is allowed actually happens: every arm64 row, and x86-64 built with FMA — in an
// optimised build (modules/session/CMakeLists.txt compiles the positive controls -O2, and /O2 on MSVC in every
// configuration but Debug, which is unoptimised and cannot be).
#if defined (_MSC_VER) && ! defined (__clang__) && defined (_DEBUG)
constexpr bool kOptimised = false;
#else
constexpr bool kOptimised = true;
#endif
#if defined (__aarch64__) || defined (_M_ARM64) || defined (__FMA__)
constexpr bool kRowCanFuse = kOptimised;
#else
constexpr bool kRowCanFuse = false;
#endif
} // namespace felitronics::session::testing
