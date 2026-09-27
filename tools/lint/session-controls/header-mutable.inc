// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a constexpr object with a mutable member: every consumer shares one changing counter, and
// the constexpr rule alone would pass it.
namespace felitronics::session
{
struct ControlShared
{
    mutable int controlCalls = 0;   // VIOLATION
};
inline constexpr ControlShared controlShared {};
}
