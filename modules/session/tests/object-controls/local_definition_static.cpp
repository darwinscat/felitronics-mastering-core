// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL, with local_definition_call.cpp — the local `getpid` that must answer no other object's call: C
// linkage, so its symbol is the bare name, and internal linkage, so it is LOCAL. Its address is taken so every optimiser
// keeps it.

extern "C"
{
static int getpid (void) { return 7; }
}

namespace felitronics::session::control
{
int (*localGetpid() noexcept) (void) { return &getpid; }
}
