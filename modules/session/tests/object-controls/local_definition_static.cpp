// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL, with local_definition_call.cpp — the local `getpid` that must answer no other object's call:
// internal linkage, so it is LOCAL, under the bare C symbol name. Its address is taken so every optimiser keeps it.
// The name is given at the assembler where there is one: clang mangles an internal-linkage function even inside
// extern "C" (Apple clang 14 and 21 emitted _ZL6getpidv, which would make this control red for the wrong reason), gcc
// does not; an asm label is kept as written by both — the bare name, with Mach-O's leading underscore. MSVC has no asm
// labels and does not decorate a C-linkage name.

#if defined (_MSC_VER) && ! defined (__clang__)
extern "C"
{
static int getpid (void) { return 7; }
}
#define CONTROL_LOCAL_PID getpid
#else
  #if defined (__APPLE__)
static int localPid (void) asm ("_getpid");
  #else
static int localPid (void) asm ("getpid");
  #endif
static int localPid (void) { return 7; }
#define CONTROL_LOCAL_PID localPid
#endif

namespace felitronics::session::control
{
int (*localGetpid() noexcept) (void) { return &CONTROL_LOCAL_PID; }
}
