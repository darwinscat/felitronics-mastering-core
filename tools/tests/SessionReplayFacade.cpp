// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// The shipped facade, compiled with its own flags. This test-only accessor reaches the C++ session
// to compare owned C++ recovery values. It adds no shipping export or reset for the poison latch.
#include "../wasm/fc_session.cpp"

felitronics::session::Session* sessionForReplayTest (fc_session handle) noexcept
{
    auto* slot = lookup (handle);
    return slot == nullptr ? nullptr : slot->session;
}

// The exhaustive generation walk is a Release check. Debug reaches the same shipped boundary
// directly; no shipping entry point or reset exists, and an occupied/retired slot cannot move.
bool sessionAdvanceGenerationForTest (std::uint32_t slot, std::uint32_t generation) noexcept
{
    if (slot >= FC_SESSION_MAX_HANDLES || g_slots[slot].session != nullptr || g_slots[slot].retired
        || generation <= g_slots[slot].gen || generation > FC_SESSION_SLOT_GENERATIONS) return false;
    g_slots[slot].gen = generation;
    return true;
}
