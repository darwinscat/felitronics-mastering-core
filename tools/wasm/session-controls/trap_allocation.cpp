// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Linked only into the trap control. The shared allocator's one-shot hook abandons the first session allocation.
#include <alloc_counter.h>
namespace
{
void trap() noexcept { __builtin_trap(); }
const bool armed = [] { alloc::onNext.store (&trap); return true; }();
}
