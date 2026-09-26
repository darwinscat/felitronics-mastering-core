// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE TARGET'S FLAGS, ASSERTED FROM INSIDE THE TARGET. modules/session/CMakeLists.txt gives every source of
// felitronics::session the same PRIVATE flags, so one translation unit that refuses to compile without them proves
// them for all of them — this one. Four claims; each is held where it can be:
//
//   no exceptions   a compile error below if the compiler says exceptions are on. On clang and gcc -fno-exceptions
//                   also makes a `throw` or a `try` in any session source a compile error of its own; on MSVC
//                   /EHs-c- makes `try` one (/we4530), but a bare `throw` still compiles there, and it is the
//                   clang and gcc rows of the matrix that turn it red. (modules/session/tests/controls/ plants each
//                   of these and requires the build to fail.)
//   no RTTI         a compile error below if RTTI is on; typeid and dynamic_cast are then errors on every compiler
//                   (-fno-rtti; /GR- with /we4541).
//   no fast-math    a compile error below if the compiler announces it (__FAST_MATH__, finite-math-only, or MSVC
//                   without /fp:precise). fast-math would license reassociation, and a reassociated sum is another
//                   number.
//   no contraction  cannot be seen by the preprocessor on clang or gcc — neither announces -ffp-contract (MSVC's
//                   /fp:contract is announced, and refused below). So it is MEASURED, by fusesMultiplyAdd() below,
//                   which modules/session/tests/SessionTests.cpp calls from a translation unit that is itself
//                   compiled with contraction forced ON: the library must answer "no" anyway.

#include <felitronics/session/Session.h>

#if defined (__cpp_exceptions) || defined (__EXCEPTIONS) || defined (_CPPUNWIND)
    #error "felitronics::session must be compiled without exceptions (docs/SESSION.md) — the target's flags did not reach this file"
#endif

#if defined (__cpp_rtti) || defined (__GXX_RTTI) || defined (_CPPRTTI)
    #error "felitronics::session must be compiled without RTTI (docs/SESSION.md) — the target's flags did not reach this file"
#endif

#if defined (__FAST_MATH__) || (defined (__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ != 0)
    #error "felitronics::session must be compiled without fast-math (docs/SESSION.md)"
#endif

#if defined (_MSC_VER) && ! defined (__clang__) && ! defined (_M_FP_PRECISE)
    #error "felitronics::session must be compiled with /fp:precise on MSVC (docs/SESSION.md)"
#endif

// MSVC's spelling of contraction is /fp:contract, and unlike -ffp-contract it IS announced to the preprocessor.
#if defined (_M_FP_CONTRACT)
    #error "felitronics::session must be compiled without /fp:contract (docs/SESSION.md)"
#endif

namespace felitronics::session
{

bool fusesMultiplyAdd() noexcept
{
    // (1 + 2^-27)^2 = 1 + 2^-26 + 2^-54. Rounded to double that is 1 + 2^-26 — the 2^-54 is below half an ulp — so
    // the product rounded on its own and then added to -(1 + 2^-26) is exactly +0, while a single fused rounding of
    // the whole expression keeps the 2^-54. The same constants tools/tests/ProbeTests.cpp uses for the probe's flag.
    // `volatile` so the compiler cannot fold the expression at compile time, where the flag would not be exercised.
    volatile double a = 1.0 + 0x1p-27;
    volatile double b = 1.0 + 0x1p-27;
    volatile double c = -(1.0 + 0x1p-26);
    const double r = a * b + c;
    return r > 0.0;
}

} // namespace felitronics::session
