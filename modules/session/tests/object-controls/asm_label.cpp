// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a mutable global renamed at the assembler (`asm("...")` label), so the
// symbol the object carries is not the C++ name. Not built on MSVC, which has no asm labels.

namespace felitronics::session::control
{
int asmCounterVariable asm ("asmCounter") = 0;
int nextAsm() { return ++asmCounterVariable; }
}
