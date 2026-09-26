// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE CONTRACTION CANARY — a header-inline function with EXTERNAL linkage, which is the whole point of it.
//
// An inline function is compiled into every translation unit that uses it out of line, each copy under that unit's
// flags, and the linker keeps ONE copy for the program (a COMDAT, a weak definition) — in practice the first it meets.
// So when an application links felitronics::session and compiles the same inline function itself, under its own flags,
// the session may run the application's copy. The library's flags then say nothing about that function.
//
// The canary is such a function, called by Session::create() through a pointer (a call through a pointer cannot be
// inlined, so it goes to the copy the linker kept). Its multiply-add is split across two statements: the standard's
// contraction (clang's default, and felitronics-core's policy, -ffp-contract=on) never fuses across statements, gcc's
// default -ffp-contract=fast and fast-math do. So a kept copy compiled at the policy's flags passes, and one compiled
// at gcc's default or with fast-math — a desktop application that does not state felitronics-core's FP policy — makes
// create() refuse with Status::ContractedHelper.
//
// WHAT IT PROVES, AND WHAT IT CANNOT: it proves the kept copy of THIS function. A foreign copy of another inline function
// the session shares with its caller is invisible to it. What holds the rest is structural (docs/SESSION.md): the
// session's arithmetic lives in its own translation units — its public header carries no function body, the lint
// refuses one — and every multiply-add it shares through felitronics-core is pinned (core::det::mulAdd / mul round
// through a volatile, which no copy's flags can fuse). modules/session/tests/ComdatTests.cpp links a copy compiled with
// -ffp-contract=fast ahead of the library and requires create() to refuse.

namespace felitronics::session::detail
{
inline double contractionCanary (double a, double b, double c) noexcept
{
    const double product = a * b;
    return product + c;
}
} // namespace felitronics::session::detail
