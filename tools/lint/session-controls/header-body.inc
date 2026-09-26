// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a function body in the public header: compiled under the consumer's flags, not the library's.
namespace felitronics::session
{
inline double controlTwice (double x) noexcept { return 2.0 * x; }   // VIOLATION
}
