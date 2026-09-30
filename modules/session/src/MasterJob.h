// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Landing.h>
#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/StreamingLoudnessMeter.h>
#include "Cost.h"
#include <memory>

namespace felitronics::session
{
class Session;
namespace detail
{
struct MasterPlan
{
    Rejection rejection = Rejection::Contract;
    command::MasterReady ready {};
    mastering::LoudnessRequest request {};
    std::uint32_t deliveryRate = 0;
    int frames = 0, traceBuckets = 0;
    std::size_t crestCapacity = 0, costCapacity = 0, costScratchCapacity = 0;
    std::size_t waveformCapacity = 0;
    std::uint64_t costHop = 0;
    analysis::BandCrestParams crestParams {};
    double sourceLufs = 0;
    std::uint64_t bytes = 0, largestBlock = 0;
    std::uint64_t retainedRowBytes = 0;
    bool costMeterAdmitted = false;
};

struct MasterRows
{
    MasterId crestMasterId = 0;
    std::uint64_t crestSource = 0, crestSourceKey = 0, crestReadyHash = 0, crestSound = 0;
    std::uint32_t crestDeliveryRate = 0;
    analysis::BandCrestParams crestParams {};
    std::unique_ptr<LandingPass[]> passes;
    std::unique_ptr<LandingTraceBucket[]> traces;
    std::uint32_t traceCapacity = 0;
    std::unique_ptr<double[]> crest;
    std::size_t crestCapacity = 0;
    std::size_t crestMaskCopied = 0;
    unsigned crestCostBand = 0;
    CrestScan crestCostScan;
    bool crestCostStarted = false;
    std::unique_ptr<double[]> costSeries, costScratch;
    std::unique_ptr<MasterSection[]> sections;
    std::unique_ptr<MasterWaveformBucket[]> waveform;
    std::size_t costCapacity = 0, costScratchCapacity = 0, waveformCapacity = 0;
};

struct MasterJob final
{
    static MasterPlan plan (const Session& session, const command::Master& input) noexcept;
    static std::uint64_t fingerprint (const command::MasterReady& ready) noexcept;
    bool begin (const Session& session, const MasterPlan& plan);
    mastering::StepResult step (long long budget) noexcept;
    const mastering::LoudnessSolution& result() const noexcept { return search.result(); }
    const analysis::BandCrest& crestResult() const noexcept { return crest; }
    std::size_t copiedCrestRows() const noexcept { return crestCopied; }
    const MasterReport& reportResult() const noexcept { return report; }
    std::unique_ptr<float[]> takeOutput() noexcept { return std::move (output); }

    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    mastering::DeliveryConverter converter;
    mastering::LandingSearch search { solver };
    analysis::BandCrest crest;
    analysis::StreamingLoudnessMeter costMeter;
    std::unique_ptr<float[]> output;
    std::unique_ptr<float[]> impactScratch;
    const float* sourcePlanes[2] {};
    float* outputPlanes[2] {};
    int frames = 0, channels = 0;
    std::uint32_t deliveryRate = 0, sourceRate = 0;
    std::uint64_t sourceFrames = 0;
    std::size_t crestCopied = 0;
    std::span<const double> sourceMask;
    long long crestCursor = 0, impactDelay = 0;
    std::uint64_t costCursor = 0, costHop = 0, costStored = 0;
    std::uint64_t initializedWaveBuckets = 0;
    bool costMeterReady = false;
    enum class Stage : std::uint8_t { Search, Prepare, Read, Finish, Copy, CostRead, CostFinish,
        CostWave, CostPump, CostActive, CostShape, CostWorst, CostCrest, CostPublish, Done, Failed };
    Stage stage = Stage::Search;
    CrestScan costCrestScan;
    ShapeScan costShapeScan;
    PumpScan costPumpScan;
    CostRules costRules;
    MasterCost costResult;
    std::uint64_t costFinishCursor = 0, activeJudged = 0, activeCount = 0;
    std::uint32_t costComparable = 0;
    std::size_t costWorst = 0;
    unsigned costBand = 0;
    bool costShapeStarted = false, costCrestStarted = false;
    const Session* session = nullptr;
    analysis::BandCrestParams crestParams {};
    double sourceLufs = 0, targetLufs = 0, targetTp = 0;
    command::MasterReady ready {};
    MasterReport report {};
};
} // namespace detail
} // namespace felitronics::session
