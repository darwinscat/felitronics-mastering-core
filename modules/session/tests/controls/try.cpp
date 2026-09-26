// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// BUILD CONTROL: a `try` compiled with felitronics::session's compile options must not compile (-fno-exceptions;
// /EHs-c- with /we4530). modules/session/CMakeLists.txt builds this file clean (it must compile) and with
// FELITRONICS_SESSION_PLANT (it must fail with the compiler's exceptions diagnostic).

int felitronicsSessionControlTry (int (*work) ());

int felitronicsSessionControlTry (int (*work) ())
{
#if defined (FELITRONICS_SESSION_PLANT)
    try { return work(); } catch (...) { return 0; }
#else
    return work();
#endif
}
