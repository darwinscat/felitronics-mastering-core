// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — an atomic: a value shared with another thread, which the session does not have. No symbol.
namespace felitronics::session
{
int controlShared ();
int controlShared () { static int n = 0; return __atomic_add_fetch (&n, 1, 0); }   // VIOLATION
}
