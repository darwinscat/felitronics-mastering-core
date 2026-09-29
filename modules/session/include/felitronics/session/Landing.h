// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Config.h>
#include <felitronics/session/LandingResult.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include <cstdint>

namespace felitronics::session
{

enum class LandingPlanStatus : std::uint8_t { Ready, InvalidTarget, UnavailableSource, GainRange };

// Captured once with the master recipe. The source measurement supplies one normalization trim;
// each render changes only the separate pre-limiter search gain and limiter ceiling.
struct LandingPlan
{
    LandingPlanStatus status = LandingPlanStatus::InvalidTarget;
    mastering::LoudnessRequest request {};
    double initialCeilingDbTp = 0.0;
    bool sourceRateImpactPass = false;
};

class LandingOps final
{
public:
    [[nodiscard]] static LandingPlan plan (const config::Engine& engine, bool sourceLoudnessValid,
                                           double sourceLufs, double targetLufs, double targetTruePeakDbTp,
                                           double sourceRate, double deliveryRate) noexcept;

    // Copies exactly the actual pass log into caller-owned rows, then publishes a view of those rows.
    [[nodiscard]] static bool summarize (const mastering::LoudnessSolution& solution,
                                         std::span<LandingPass> rows, LandingSummary& out) noexcept;
};

} // namespace felitronics::session
