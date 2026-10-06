// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Landing.h>
#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
#include <felitronics/analysis/StreamingLoudnessMeter.h>
#include <felitronics/analysis/WaveformIndex.h>
#include <felitronics/toml/Embedded.h>
#include "Cost.h"
#include "Damage.h"
#include <memory>
#include <optional>

namespace felitronics::session
{
class Session;
namespace detail
{
// [landing] limiterBudget: the P95 of the limiter's gain reduction a landing at `targetLufs` may take, dB — the
// target's own number or a person's edit of it, by one rule. NaN where the config does not say it.
[[nodiscard]] double limiterBudgetDb (toml::embedded::View engine, double targetLufs) noexcept;
// [landing.max]: a max mode's limiter budget, dB (NaN for the manual mode or where the config does not say it).
[[nodiscard]] double maxBudgetDb (toml::embedded::View engine, LoudnessMode mode) noexcept;
// [progress.master]: the renders a master's bar expects in its loudness mode — expectedPasses by hand,
// expectedPassesMaxClean / expectedPassesMaxDense for the max modes; a waiting master's and a rendering one's alike.
[[nodiscard]] std::uint32_t expectedPasses (toml::embedded::View engine, LoudnessMode mode) noexcept;
// WHAT ENDED A MAX MODE (MasterReport::maxStop), from the landing alone (v0.16.0: no guard in the loop), in this order: a
// render above the ceiling; a render over the mode's limiter budget (no render kept it — the same status and binding as
// a budget that held, told apart by the delivered render's own excess); landed again on [landing.max] floorLufs, which
// the budget held it under (Floor); the search's ceiling reached; the budget that held it; the passes. Guard, GuardUnmet
// and Unguarded are no longer said.
struct MaxStopInputs
{
    bool peaksAboveCeiling = false, overBudget = false, floor = false;
    mastering::MasteringSolveStatus status = mastering::MasteringSolveStatus::NotPrepared;
    mastering::MasteringConstraint binding = mastering::MasteringConstraint::None;
};
[[nodiscard]] MaxStop maxStopOf (const MaxStopInputs& in) noexcept;

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
    DamagePlan damage {};                      // the damage's walks, priced (as the job after it), or why they cannot run
    // A max mode ([landing.max]): the mode, its search ceiling and budget in the request, and the floor no max master
    // lands under (floorLufs).
    LoudnessMode loudnessMode = LoudnessMode::Manual;
    double floorLufs = std::numeric_limits<double>::quiet_NaN();
    DeliveryMode deliveryMode = DeliveryMode::Mastered;
    double deliveryGainDb = 0.0, deliveryCeilingDbTp = 0.0;
    bool deliveryDithered = false;
};

struct MasterRows
{
    // Captured at render time, never reconstructed from the current project.
    command::MasterReady workedReady {};
    // WHAT THE MASTER'S DAMAGE GRADE NEEDS TO START (command::GradeDamage, Session::startDamage), kept with the master from
    // its delivery: the walks' plan and the parameters it was delivered with — no walk buffer. `damageGradable` false: a
    // master whose damage cannot be graded (no plan could be made for its source).
    bool damageGradable = false;
    DamagePlan damagePlan {};
    mastering::MasteringChainParams damageWinning {};
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
    bool readyForLateCrest() const noexcept { return stage == Stage::Ready; }
    bool beginLateCrest() noexcept;
    // What the Pump says of the step it is in: the current walk's share of the file — absent where the stage walks nothing.
    std::optional<double> stepFraction() const noexcept;
    // A delivery's whole bar, 0..1 and below 1 until it ends (as-is or peaks-only; a mastered job's bar is its search's).
    double deliveryFraction() const noexcept;
    mastering::MasteringChainParams winningParams() const noexcept;

    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    mastering::DeliveryConverter converter;
    mastering::MasteringChainTaps deliveryTaps {};
    mastering::LandingSearch search { solver };
    analysis::BandCrest crest;
    analysis::ReferenceTruePeakMeter deliveryPeakMeter;
    analysis::StreamingLoudnessMeter costMeter;
    analysis::WaveformStream costAxes;
    bool costAxesReady = false;
    std::unique_ptr<float[]> output;
    std::unique_ptr<float[]> impactScratch;
    const float* sourcePlanes[2] {};
    float* outputPlanes[2] {};
    const float* deliveryPlanes[2] {};
    int frames = 0, channels = 0;
    std::uint32_t deliveryRate = 0, sourceRate = 0;
    std::uint64_t sourceFrames = 0;
    std::size_t crestCopied = 0;
    std::span<const double> sourceMask;
    long long crestCursor = 0, impactDelay = 0;
    std::uint64_t costCursor = 0, costHop = 0, costStored = 0;
    std::uint64_t initializedWaveBuckets = 0;
    bool costMeterReady = false;
    enum class Stage : std::uint8_t { Search, DeliveryCopy, DeliveryConvert, DeliveryRender, DeliveryMeasure, Prepare, Read, Finish, Copy, CostRead, CostFinish,
        CostWave, CostPump, CostActive, CostShape, CostWorst, CostCrest, CostPublish, Ready, Done, Failed };
    Stage stage = Stage::Search;
    std::size_t rowIndex = 0;
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
    DeliveryMode deliveryMode = DeliveryMode::Mastered;
    double deliveryGainDb = 0.0, deliveryCeilingDbTp = 0.0;
    bool deliveryDithered = false, deliveryCorrected = false;
    long long deliveryCursor = 0, deliveryMeasureCursor = 0;
    // THE DAMAGE CAN BE GRADED (command::GradeDamage): the job ended where its damage can be graded — the report says
    // Pending, and the session keeps damagePlan and the delivered parameters with the master for a grade the shell asks.
    bool damageFollows = false;
    void settleLra() noexcept;
    // The loudness mode the job lands in (a max mode's verdict and stop). A max mode's floor ([landing.max] floorLufs) and
    // the landing again on it, with no budget, where the first one's file stood under it — whatever held it there: the
    // budget, the passes (floorPass); floorFirstLufs, where that first file stood.
    LoudnessMode mode = LoudnessMode::Manual;
    double floorLufs = std::numeric_limits<double>::quiet_NaN(), floorFirstLufs = std::numeric_limits<double>::quiet_NaN();
    bool floorPass = false;
    // ...and the first landing's passes and work, which the master's record, pass number and bar keep across the floor
    // landing: its rows stay ahead of the floor landing's (LandingOps::passRows), the count never starts again.
    std::uint32_t floorFirstPasses = 0;
    std::uint64_t floorFirstWork = 0;
    int startedPasses() const noexcept { return int (floorFirstPasses) + search.startedPasses(); }
    int startedRenders() const noexcept { return int (floorFirstPasses) + search.startedRenders(); }
    double renderProgress() const noexcept { return double (floorFirstPasses) + std::max (0.0, search.renderProgress()); }
    mastering::LoudnessRequest floorRequest {};
    [[nodiscard]] bool beginFloor() noexcept;
    [[nodiscard]] bool beginDeliveryRender() noexcept;
    mastering::StepResult settleLanding (mastering::StepResult result) noexcept;
};
} // namespace detail
} // namespace felitronics::session
