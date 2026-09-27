// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL, with local_definition_static.cpp — a call to the operating system (getpid) in one object, and a
// LOCAL function of the same name in another. The linker never resolves one object's call with another object's local
// definition: this call reaches the C library. A gate that counted every definition of the set as an answer to every
// call passed it; the gate counts only definitions with global or weak binding.

extern "C" int getpid (void);

namespace felitronics::session::control
{
int processId() noexcept { return getpid(); }
}
