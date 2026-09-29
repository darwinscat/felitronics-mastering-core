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
struct LandingTraceBucket
{
    double minDb = 0.0, maxDb = 0.0, meanDb = 0.0;
    std::uint64_t samples = 0, nonFinite = 0;
};
struct LandingTrace
{
    std::uint64_t fromFrame = 0, toFrame = 0, samples = 0, nonFinite = 0;
    std::uint32_t sampleRateHz = 0, columns = 0;
    bool complete = false, valid = false;
    std::span<const LandingTraceBucket> rows;
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
    std::optional<LandingTrace> limiterTrace, peakClipTrace;
};
} // namespace felitronics::session
