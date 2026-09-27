// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a mutable global planted through a function-like macro, where no text rule
// looking for declarations would see it.

#define SESSION_COUNTER(t, n, i) static t n = i;

namespace felitronics::session::control
{
SESSION_COUNTER (int, g_macroCounter, 0)
int nextMacro() { return ++g_macroCounter; }
}
