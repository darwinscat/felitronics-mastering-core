// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a throw only MSVC compiles (it accepts a bare throw under /EHs-c-), in a branch
// every other row preprocesses away.
namespace felitronics::session
{
void controlFail ();
void controlFail ()
{
#if defined(_MSC_VER) && !defined(__clang__)
    throw 1;   // VIOLATION
#endif
}
}
