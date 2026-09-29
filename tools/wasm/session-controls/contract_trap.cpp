// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// Contract failure injection only; no extra export enters the shipped module.
#include <alloc_counter.h>
#include <emscripten/emscripten.h>
namespace
{
void trap() noexcept { __builtin_trap(); }
}
extern "C" EMSCRIPTEN_KEEPALIVE void contract_arm_trap() { alloc::onNext = &trap; }
