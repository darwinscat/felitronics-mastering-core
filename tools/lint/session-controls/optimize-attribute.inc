// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a per-function optimisation switch, past every flag of the library.
namespace felitronics::session
{
[[gnu::optimize ("fast-math")]] double controlFast (double x) noexcept;   // VIOLATION
}
