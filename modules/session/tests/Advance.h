// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include "../src/Driver.h"

namespace felitronics::session::testing
{
// Command, placement and project fixtures supply completed work through the internal driver seam.
// Real analyzer readiness and cancellation are exercised by the live/source suites without this helper.
inline void measure (Session& s, bool firstOnly = false)
{
    const auto job = s.measurementJob();
    if (s.state() == State::Loaded) (void) detail::Driver::measured1 (s, job, s.source().hash);
    if (! firstOnly && s.state() == State::Measured1) (void) detail::Driver::measured2 (s, job, s.source().hash);
}
}
