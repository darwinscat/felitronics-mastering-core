// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a class's static data member: one counter shared by every instance.

namespace felitronics::session::control
{
struct Journal
{
    static constexpr int kCapacity = 64;   // a constant: no storage the code can change
    inline static int journalEntries = 0;
    int ownEntries = 0;                    // instance state: allowed
};
int record() { return ++Journal::journalEntries + Journal::kCapacity; }
}
