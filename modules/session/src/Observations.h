// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

// THE OBSERVATIONS (internal to modules/session; owner decision 3.13) — when a measurement becomes a finding. Each kind
// reads the results the analyzers already published, weighs what it found by [observations] of engine.toml and says it
// as a typed fact; nothing here measures anything again, decides a device or allocates.
//
// A finding's weights rise along the config's ramps: from `from` (where we start to speak; 0 where it is not written)
// to 1 at `fullAt`. Found, not found and not measured are three answers: a result that has not ended — or ended
// without the number a kind reads — is NotMeasured with the result's reason, never "not found".

#include "Rules.h"

#include <felitronics/session/Measurements.h>
#include <felitronics/session/Session.h>

#include <cstdint>
#include <span>

namespace felitronics::session::detail
{
struct ObservationInputs
{
    Rules rules {};
    std::span<const MeasurementResult> measurements;    // every analyzer's result, by Analyzer; empty before a source
    std::uint32_t channels = 0, sampleRate = 0;
    std::uint64_t frames = 0;
    std::uint32_t bitDepth = 0;                         // the source's container bits; 0 when it has none (a lossy file)
    // The devices that deal with a finding, as the project stands: the high-pass and mono bass in the chain.
    bool hpfOn = false, monoBassOn = false;
};

// Every kind's observation for the inputs.
void observe (const ObservationInputs& in, Observations& out) noexcept;
[[nodiscard]] SourceReport sourceReport (const ObservationInputs& in) noexcept;

// Is the source clipped — confirmed clips at [limiter.peakClipper] clippedPerMinute a minute or more? The one rule the
// limiter's needles and the observations share.
[[nodiscard]] bool regularlyClipped (const Rules& rules, double clipsPerMinute) noexcept;

// Why a number was not measured, as the term a fact names it by: every MeasurementReason but None.
[[nodiscard]] text::Term reasonTerm (MeasurementReason reason) noexcept;
} // namespace felitronics::session::detail
