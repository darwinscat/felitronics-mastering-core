// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a local pragma that turns contraction back on past -ffp-contract=off: reproduced,
// Apple arm64 emits fmadd for this function while the target says off.
namespace felitronics::session
{
#if defined(__clang__)
#pragma clang fp contract(fast)   // VIOLATION
#endif
double controlCompute (double a, double b, double c) noexcept;
double controlCompute (double a, double b, double c) noexcept { return a * b + c; }
}
