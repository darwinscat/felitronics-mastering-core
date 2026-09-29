// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

#include <felitronics/session/LandingResult.h>
#include <felitronics/analysis/BandCrestResult.h>
#include <felitronics/mastering/LoudnessSolver.h>

namespace felitronics::session::detail
{
struct CostRules
{
    double activityBelowLu = 42, activityFloorLufs = -70, tooQuietLufs = -55;
    double changeLu = 3, minSeconds = 8, minComparedShare = .5, quantile = .95;
    std::uint32_t worstNamedAbove = 2;
    double maxShortfallSeconds = .1;
    double pumpHighPassHz = 1, pumpLowPassHz = 8, pumpSettleSeconds = 2, pumpMinRateHz = 40;
};
struct CostMath
{
    // scratch holds at least master.blocks doubles. Values at the fractional boundary of
    // the largest 5% receive exactly the remaining fraction of a window's weight.
    static MasterCostValue crest (const analysis::BandCrestResult& source,
                                  const MasterCrest& master, unsigned band,
                                  std::span<double> scratch) noexcept;
    // The two series are 0.1-second end-stamped readings in temporal order.
    static MasterCostValue shape (std::span<const double> source, std::span<const double> master,
                                  std::uint32_t sourceRate, std::uint64_t sourceHop,
                                  std::uint32_t masterRate, std::uint64_t masterHop,
                                  double sourceIntegrated, double masterIntegrated,
                                  const CostRules& rules, std::span<MasterSection> sections,
                                  std::span<double> scratch, std::uint64_t& sectionCount) noexcept;
    static MasterCostValue pumping (const LandingTrace& trace, const CostRules& rules) noexcept;
    static MasterCostValue pumping (const mastering::GainReductionTrace& trace,
                                    std::uint32_t sampleRate, const CostRules& rules) noexcept;
};

// The job uses these cursors so a one-unit step never consumes a whole distribution.
// The whole-call functions above remain independent numerical controls for the cursors.
struct CrestScan
{
    enum class Phase : std::uint8_t { Read, Tail, Done };
    Phase phase = Phase::Done;
    MasterCostValue answer;
    std::uint64_t cursor = 0, popped = 0, tailCount = 0;
    double tailWeight = 0, sum = 0;
    unsigned band = 0;
    void start (const analysis::BandCrestResult& source, const MasterCrest& master,
                unsigned selected, std::span<double> scratch) noexcept;
    bool step (const analysis::BandCrestResult& source, const MasterCrest& master,
               std::span<double> scratch, std::uint32_t budget) noexcept;
};

struct ShapeScan
{
    enum class Phase : std::uint8_t { Read, Short, Near, Shift, Compare, Rank, Done };
    Phase phase = Phase::Done, resume = Phase::Done;
    MasterCostValue answer;
    std::uint64_t cursor = 0, sectionCount = 0, scan = 0, shift = 0;
    std::uint64_t comparedSections = 0, sectionRow = 0, sectionUsed = 0, popped = 0;
    double sectionSum = 0, gate = 0, gain = 0, sourceStep = 0;
    std::uint32_t sourceRate = 0;
    std::uint64_t sourceHop = 0;
    CostRules rules;
    void start (std::span<const double> source, std::span<const double> master,
                std::uint32_t sourceRateHz, std::uint64_t sourceHopFrames,
                std::uint32_t masterRateHz, std::uint64_t masterHopFrames,
                double sourceIntegrated, double masterIntegrated, const CostRules& requested,
                std::span<MasterSection> sections, std::span<double> scratch) noexcept;
    bool step (std::span<const double> source, std::span<const double> master,
               std::span<MasterSection> sections, std::span<double> scratch,
               std::uint32_t budget) noexcept;
};

struct CostBiquad
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};
struct PumpScan
{
    enum class Phase : std::uint8_t { Read, Done };
    Phase phase = Phase::Done;
    MasterCostValue answer;
    std::uint64_t cursor = 0;
    double rate = 0, settle = 0, sum = 0;
    CostBiquad hp, lp;
    void start (const mastering::GainReductionTrace& trace, std::uint32_t sampleRate,
                const CostRules& rules) noexcept;
    bool step (const mastering::GainReductionTrace& trace, std::uint32_t budget) noexcept;
};
} // namespace felitronics::session::detail
