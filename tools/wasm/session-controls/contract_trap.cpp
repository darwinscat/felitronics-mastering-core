// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// Contract failure injection only; no extra export enters the shipped module.
#include <cstddef>
#include <cstdlib>
#include <emscripten/emscripten.h>
namespace { bool armed = false; }
extern "C" EMSCRIPTEN_KEEPALIVE void contract_arm_trap() { armed = true; }
void* operator new (std::size_t size)
{
    if (armed) { armed = false; __builtin_trap(); }
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    __builtin_trap();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
