// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a mutable global placed in a section of its own naming. A gate that knew writable memory by a
// list of NAMES (.data, .bss, __DATA,__data ...) passed it on every format; the gate reads what the object says of the
// section instead — its write flag, or, where the format has none, whether it is one of the few read-only places. On
// ELF the second global goes further and names its section .rodata.*: the object still marks it writable, and the gate
// believes the flag, not the name. (The session-laws lint refuses the attribute itself; this object bypasses the lint,
// which is the point of the control.) One spelling per format, because each toolchain spells the placement its way.

namespace felitronics::session::control
{
#if defined(_MSC_VER)
#pragma section("fcsstate", read, write)
__declspec(allocate("fcsstate")) int sectionCounter = 1;
#elif defined(__APPLE__)
[[gnu::section("__DATA,__session")]] int sectionCounter = 1;
#else
[[gnu::section(".session_state")]] int sectionCounter = 1;
#endif
#if defined(__ELF__)
[[gnu::section(".rodata.session_state")]] int rodataNamedCounter = 1;
int nextRodataNamed() { return ++rodataNamedCounter; }
#endif
int nextSection() { return ++sectionCounter; }
}
