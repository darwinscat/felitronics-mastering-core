// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — thread-local state: one copy per thread, which is state all the same, and the session has no
// threads to give it meaning.

namespace felitronics::session::control
{
thread_local int perThreadCount = 0;
int bumpPerThread() { return ++perThreadCount; }
}
