// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// The shipped facade, compiled with its own flags. This test-only accessor reaches the C++ session
// while the draft ABI carries no commands. It adds no shipping export or reset for the poison latch.
#include "../wasm/fc_session.cpp"

felitronics::session::Session* sessionForReplayTest (fc_session handle) noexcept
{
    auto* slot = lookup (handle);
    return slot == nullptr ? nullptr : slot->session;
}
