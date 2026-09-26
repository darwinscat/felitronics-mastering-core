// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a const object whose initialiser is not a constant expression. It is written once, by the
// program's static initialisation, before main — so it lives in writable memory, and it runs code nobody called. A
// constant the compiler can evaluate (constexpr, or a C++20 constexpr std::string) lives in read-only memory and passes.

namespace felitronics::session::control
{
int seedFromElsewhere() noexcept;
const int kStartupValue = seedFromElsewhere();
int startupValue() noexcept { return kStartupValue; }
}
