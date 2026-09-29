// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace felitronics::session
{
enum class LandingStatus : std::uint8_t
{
    Solved, TargetUnreachable, PassLimit, TargetBetweenAchievable, Unavailable, Cancelled, TechnicalFailure
};
enum class LandingReason : std::uint8_t
{
    None, ExcessSubBass, SharpPeaks, DarkMix, LoudnessDemand, GainRange, TruePeak
};
struct LandingPass
{
    double gainDb = 0.0, ceilingDbTp = 0.0, achievedLufs = 0.0, truePeakDbTp = 0.0;
    double limiterMaxReductionDb = 0.0;
    bool ceilingSafe = false;
};
struct LandingSummary
{
    LandingStatus status = LandingStatus::Unavailable;
    LandingReason mainReason = LandingReason::None, secondReason = LandingReason::None;
    bool deliverable = false;
    std::optional<double> achievedLufs, missLu, distanceLu, truePeakDbTp;
    std::optional<double> sourceSubBassShare, sourcePresenceShare, limiterMeanReductionDb;
    std::uint32_t passes = 0;
    std::uint64_t workUnits = 0;
    std::span<const LandingPass> log;
};
} // namespace felitronics::session
