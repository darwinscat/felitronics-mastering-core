// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// BUILD CONTROL: a `throw` compiled with felitronics::session's compile options must not compile (-fno-exceptions).
// Not built on MSVC, where /EHs-c- lets a throw through (modules/session/CMakeLists.txt says why that is enough).
// Built clean (it must compile) and with FELITRONICS_SESSION_PLANT (it must fail with the exceptions diagnostic).

int felitronicsSessionControlThrow (int x);

int felitronicsSessionControlThrow (int x)
{
#if defined (FELITRONICS_SESSION_PLANT)
    if (x < 0) throw x;
#endif
    return x;
}
