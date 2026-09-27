// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — the FPU's flush-to-zero set by hand, through core's RAII: every later operation rounds
// differently, and nothing leaves a symbol. Admitting an analyzer's header brings FlushToZero.h; this is what stays out.
namespace felitronics::session
{
int controlFlushed ();
int controlFlushed () { const felitronics::core::ScopedFlushToZero ftz; return 0; }   // VIOLATION
}
