// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/Session.h>

namespace felitronics::session::testing
{
// Scenario setup waits for a state, independent of the number of analyzer work units.
inline void measure (Session& s, bool firstOnly = false)
{
    while (s.measurementJob() != 0 && (! firstOnly || s.state() == State::Loaded))
        if (s.step (1).refused) break;
}
}
