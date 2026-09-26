// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — the clock. A session that timed its own steps would give a different sequence of events on a
// faster machine. run.sh plants this file in modules/session/src/ and requires [OS] on the marked line.

#include <chrono>   // VIOLATION

namespace felitronics::session
{
long long elapsedMicros() noexcept
{
    return (long long) std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
