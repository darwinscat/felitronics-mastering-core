// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a mutable lambda: a counter carried from call to call inside a closure object.
namespace felitronics::session
{
inline auto controlCounter = [calls = 0] () mutable { return ++calls; };   // VIOLATION
}
