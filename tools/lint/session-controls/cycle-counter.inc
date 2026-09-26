// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a clock read by a builtin: one instruction, no symbol, a different answer every run.
namespace felitronics::session
{
unsigned long long controlCycles ();
unsigned long long controlCycles () { return __builtin_readcyclecounter (); }   // VIOLATION
}
