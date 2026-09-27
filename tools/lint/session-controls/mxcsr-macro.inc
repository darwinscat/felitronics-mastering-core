// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — the x86 rounding mode set through <xmmintrin.h>'s accessor macro, in the unit that includes
// the admitted analyzers (whose core headers bring the intrinsics): a bare ldmxcsr, no symbol, and every later
// operation of the thread rounds down.
namespace felitronics::session::config
{
int controlRounded ();
int controlRounded () { _MM_SET_ROUNDING_MODE (_MM_ROUND_DOWN); return 0; }   // VIOLATION
}
