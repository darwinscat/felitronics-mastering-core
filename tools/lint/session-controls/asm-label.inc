// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — a global renamed at the assembler: the object gate refuses the global, and
// this refuses the asm label itself.
namespace felitronics::session
{
int controlCounter asm ("controlCounter") = 0;   // VIOLATION
}
