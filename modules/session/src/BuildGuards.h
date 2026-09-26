// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE BUILD GUARDS — the first #include of EVERY translation unit of felitronics::session (the session-laws lint refuses
// a source whose first include is anything else). The target's flags are PRIVATE and the same for all of its sources,
// but a source can still be given flags of its own (a per-source compile option, a consumer's source property), so the
// guards are not asserted once for the library: each translation unit asserts them for itself, and a source compiled
// with exceptions, RTTI or fast-math does not compile.
//
// What the preprocessor can see is refused here. What it cannot — FP contraction on clang and gcc, which neither
// announces — is measured instead (src/BuildContract.cpp, and the compile-line gate over compile_commands.json).

// No exceptions.
#if defined (__cpp_exceptions) || defined (__EXCEPTIONS) || defined (_CPPUNWIND)
    #error "felitronics::session must be compiled without exceptions (docs/SESSION.md) — this translation unit was not"
#endif

// No RTTI.
#if defined (__cpp_rtti) || defined (__GXX_RTTI) || defined (_CPPRTTI)
    #error "felitronics::session must be compiled without RTTI (docs/SESSION.md) — this translation unit was not"
#endif

// No fast-math: it licenses reassociation and reciprocals, and a reassociated sum is another number.
#if defined (__FAST_MATH__) || (defined (__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ != 0)
    #error "felitronics::session must be compiled without fast-math (docs/SESSION.md) — this translation unit was not"
#endif

// Every floating-point operation evaluated in its own type: no x87 extended precision carried between operations.
#if defined (__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ != 0
    #error "felitronics::session needs FLT_EVAL_METHOD 0 — each operation rounded to its own type (docs/SESSION.md)"
#endif

// MSVC: 2022 (19.30) or later, /fp:precise, no /fp:contract, and SSE2 arithmetic on 32-bit x86.
#if defined (_MSC_VER) && ! defined (__clang__)
    #if _MSC_VER < 1930
        #error "felitronics::session needs MSVC 2022 (19.30) or later"
    #endif
    #if ! defined (_M_FP_PRECISE)
        #error "felitronics::session must be compiled with /fp:precise on MSVC (docs/SESSION.md)"
    #endif
    #if defined (_M_FP_CONTRACT)
        #error "felitronics::session must be compiled without /fp:contract (docs/SESSION.md)"
    #endif
    #if defined (_M_IX86_FP) && _M_IX86_FP < 2
        #error "felitronics::session needs SSE2 arithmetic on 32-bit x86 (/arch:SSE2 or higher), not x87"
    #endif
#endif

// THE TWO RELEASES, stated by the build that compiles the library — modules/session/CMakeLists.txt reads them from the
// project() lines of both repositories, tools/wasm/build.sh reads the same two lines. No default: a build that does not
// say which releases it is would compile a library that answers a version nobody built.
#if ! defined (FELITRONICS_SESSION_VERSION_MAJOR) || ! defined (FELITRONICS_SESSION_VERSION_MINOR) \
    || ! defined (FELITRONICS_SESSION_VERSION_PATCH)
    #error "felitronics::session: FELITRONICS_SESSION_VERSION_{MAJOR,MINOR,PATCH} are not defined — build it through modules/session/CMakeLists.txt or tools/wasm/build.sh"
#endif
#if ! defined (FELITRONICS_SESSION_CORE_VERSION_MAJOR) || ! defined (FELITRONICS_SESSION_CORE_VERSION_MINOR) \
    || ! defined (FELITRONICS_SESSION_CORE_VERSION_PATCH)
    #error "felitronics::session: FELITRONICS_SESSION_CORE_VERSION_{MAJOR,MINOR,PATCH} are not defined — build it through modules/session/CMakeLists.txt or tools/wasm/build.sh"
#endif
