// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Linked only into the trap control. Abandon a real C call at its first allocation.
#include <cstddef>
#include <new>
void* operator new (std::size_t) { __builtin_trap(); }
