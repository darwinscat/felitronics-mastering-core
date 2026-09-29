// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Measurements.h>

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

// The delivered meter and the source-rate crest check answer different questions. Rows are linear
// (peak amplitude, mean-square power) in Low, LowMid, HighMid, High, Full order. A mask value of one
// means that the corresponding source band was suitable for comparison.
struct MasterCrest
{
    MeasurementStatus status = MeasurementStatus::Pending;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint32_t version = 1, sampleRateHz = 0, hopFrames = 0, blockHops = 0;
    double edgeLowHz = 0, edgeMidHz = 0, edgeHighHz = 0;
    std::uint64_t frames = 0, blocks = 0;
    bool complete = false, sourceRateCheck = false;
    std::span<const double> rows, sourceMask;
};
struct MasterHint
{
    LandingReason reason = LandingReason::None;
    double evidence = 0;
    bool percent = false;
};
// A section covers source frames [fromFrame,toFrame). A shift is output short-term loudness
// minus source short-term loudness after removing the integrated loudness change.
struct MasterSection
{
    std::uint64_t fromFrame, toFrame;
    double sourceLufs, masterLufs, shiftLu;
    bool compared;
};
struct MasterCostValue
{
    MeasurementReason reason = MeasurementReason::NotImplemented;
    std::optional<double> value;
    std::uint64_t compared = 0;
};
struct MasterWaveformBucket
{
    std::uint64_t fromFrame, toFrame;
    std::uint32_t channel;
    double minimum, maximum, rms;
    std::uint64_t finite;
};
struct MasterCost
{
    // Positive crest loss means flatter attacks. The last band is full band.
    MasterCostValue crestLowDb {}, crestLowMidDb {}, crestHighMidDb {}, crestHighDb {}, crestFullDb {};
    MasterCostValue shapeP95Lu {}, largestSectionShiftLu {}, pumpingRmsDb {};
    MasterCostValue limiterP50Db {}, limiterP95Db {}, limiterActiveShare {}, activeWindowShare {};
    std::optional<std::uint32_t> worstSectionIndex;
    MeasurementReason k2Reason = MeasurementReason::NotImplemented;
    std::uint32_t sourceRateHz = 0, masterRateHz = 0;
    std::uint64_t sourceFrames = 0, masterFrames = 0;
    std::span<const MasterSection> sections;
    std::span<const MasterWaveformBucket> waveform;
};
struct MasterReport
{
    MeasurementStatus status = MeasurementStatus::Unavailable;
    MeasurementReason reason = MeasurementReason::NotImplemented;
    double targetLufs = 0, ceilingDbTp = 0;
    std::optional<double> achievedLufs, truePeakDbTp, lraLu, plrDb, gainFromSourceDb, missLu;
    MeasurementReason lraReason = MeasurementReason::Pending, plrReason = MeasurementReason::Pending;
    bool peakSafe = false;
    bool deliverable = false;
    bool targetMet = false;
    std::uint32_t checkPasses = 0;
    MasterCrest crest {};
    std::optional<MasterHint> firstHint, secondHint;
    std::optional<MasterCost> cost;
};
struct MasterReportText
{
    [[nodiscard]] static std::optional<text::Fact> miss (const MasterReport& report) noexcept;
    [[nodiscard]] static std::optional<text::Fact> hint (const MasterHint& hint) noexcept;
    [[nodiscard]] static text::Fact crest (const MasterCrest& crest) noexcept;
    [[nodiscard]] static text::Fact shape (const MasterCost& cost) noexcept;
    [[nodiscard]] static text::Fact impact (const MasterCost& cost) noexcept;
    [[nodiscard]] static text::Fact pumping (const MasterCost& cost) noexcept;
    [[nodiscard]] static text::Fact tonal() noexcept;
};
struct MasterCrestGrid
{
    [[nodiscard]] static bool compatible (const MasterCrest& master,
                                          const analysis::BandCrestResult& source) noexcept;
};
} // namespace felitronics::session
