// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — a class's static data member: one counter shared by every instance, which is exactly the
// place two sessions' histories meet. run.sh plants this file in modules/session/include/ and requires [GLOBALS] on
// the marked line.

#pragma once

namespace felitronics::session
{
struct Journal
{
    static constexpr int kCapacity = 64;   // a constant: allowed
    inline static int entries = 0;         // VIOLATION
    int ownEntries = 0;                    // instance state: allowed
};
}
