// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Landing.h>
#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/StreamingLoudnessMeter.h>
#include <felitronics/analysis/WaveformIndex.h>
#include "Cost.h"
#include "Damage.h"
#include <memory>
#include <optional>

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
    std::size_t axesCapacity = 0;
    std::uint64_t costHop = 0;
    analysis::BandCrestParams crestParams {};
    double sourceLufs = 0;
    std::uint64_t bytes = 0, largestBlock = 0;
    std::uint64_t retainedRowBytes = 0;
    bool costMeterAdmitted = false;
    bool glueTrace = false;                    // the glue compresses: its gain reduction is kept, a trace beside the limiter's
    bool saturationTrace = false;              // the soft clipper shapes: its shave of the peaks is kept, the same way
    std::optional<MasterMedium> medium;        // a version-0 master's medium and input, from the chain it will run
    DamagePlan damage {};                      // the damage's walks, priced, or why they cannot run
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
    // THE MASTER'S OWN LOUDNESS CURVES, a reading per hop of the delivered audio as the meter gave it: costSeries is the
    // short-term one (the cost's shape reads it), costMomentary the momentary; loudnessRows of each are written.
    std::unique_ptr<double[]> costMomentary;
    std::uint64_t loudnessRows = 0, loudnessHop = 0;
    // The waveform's buckets again, as the source's waveform has them: four axes, envelope and band energies.
    std::unique_ptr<analysis::WaveformColumn[]> axes;
    std::size_t axesCapacity = 0, axesRows = 0;
    // THE GLUE'S GAIN REDUCTION OVER TIME (QueryKind::GlueGr): the delivered render's compressor trace, on the limiter
    // trace's buckets (traceCapacity of them). Only a master whose glue compresses has the rows; the others have none.
    std::unique_ptr<LandingTraceBucket[]> glueRows;
    std::optional<LandingTrace> glueTrace;
    // WHAT THE SATURATION TOOK OFF THE PEAKS OVER TIME (QueryKind::SaturationShave): the delivered render's soft-clipper
    // shave, on the same buckets. Only a master whose soft clipper shapes has the rows.
    std::unique_ptr<LandingTraceBucket[]> saturationRows;
    std::optional<LandingTrace> saturationTrace;
};

struct MasterJob final
{
    // THE PLAN OF A MASTER of `project` on the session's source — its chain, its landing request and what it asks the
    // heap for, or the rejection it gets; nothing is allocated. A version-1 input carries its chain ready; a version-0
    // one takes it from the project's devices (src/Chain.h), on the measurements as they stand.
    static MasterPlan plan (const Session& session, const command::Master& input, const Project& project) noexcept;
    static std::uint64_t fingerprint (const command::MasterReady& ready) noexcept;
    bool begin (const Session& session, const MasterPlan& plan);
    mastering::StepResult step (long long budget) noexcept;
    const mastering::LoudnessSolution& result() const noexcept { return search.result(); }
    const analysis::BandCrest& crestResult() const noexcept { return crest; }
    std::size_t copiedCrestRows() const noexcept { return crestCopied; }
    const MasterReport& reportResult() const noexcept { return report; }
    std::unique_ptr<float[]> takeOutput() noexcept { return std::move (output); }
    // What the Pump says of the step it is in: the phase (the damage's two walks have their own) and the current walk's
    // share of the file — absent where the stage walks nothing.
    PhaseName phaseName() const noexcept;
    std::optional<double> stepFraction() const noexcept;

    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    mastering::DeliveryConverter converter;
    mastering::LandingSearch search { solver };
    analysis::BandCrest crest;
    analysis::StreamingLoudnessMeter costMeter;
    analysis::WaveformStream costAxes;
    bool costAxesReady = false;
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
        CostWave, CostPump, CostActive, CostShape, CostWorst, CostCrest, CostPublish, DamageBegin, DamageRun, Done, Failed };
    Stage stage = Stage::Search;
    CrestScan costCrestScan;
    ShapeScan costShapeScan;
    PumpScan costPumpScan;
    CostRules costRules;
    MasterCost costResult;
    // The soft clipper's peaks as the search's last render left them: read when the search ends, before the chain is
    // prepared again for the source-rate check.
    mastering::ClipperPeaks clipPeaks {};
    bool clipCounted = false;
    std::uint64_t costFinishCursor = 0, activeJudged = 0, activeCount = 0;
    std::uint32_t costComparable = 0;
    std::size_t costWorst = 0;
    unsigned costBand = 0;
    bool costShapeStarted = false, costCrestStarted = false;
    const Session* session = nullptr;
    analysis::BandCrestParams crestParams {};
    double sourceLufs = 0, targetLufs = 0, targetTp = 0;
    command::MasterReady ready {};
    std::optional<MasterMedium> medium;
    MasterReport report {};
    DamagePlan damagePlan {};
    Damage damage;
    mastering::MasteringChainParams winningParams() const noexcept;
    void settleLra() noexcept;
};
} // namespace detail
} // namespace felitronics::session
