// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "BuildContract.h"
#include "MasterJob.h"
#include "Chain.h"
#include "MeasurementPlan.h"
#include "Cost.h"
#include "Limiter.h"
#include <felitronics/analysis/BandCrestResult.h>
#include "Rules.h"
#include <felitronics/session/Session.h>
#include <felitronics/core/DetMath.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace felitronics::session::detail
{
namespace
{
const MeasurementArray* findArray (const MeasurementResult& result, std::string_view name) noexcept
{
    for (const auto& array : result.arrays) if (array.name == name) return &array;
    return nullptr;
}
double number (toml::embedded::View value) noexcept
{
    if (const auto decimal = value.decimal()) return decimal->toDouble();
    if (const auto integer = value.integer()) return double (*integer);
    return std::numeric_limits<double>::quiet_NaN();
}
bool add (std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value > 9007199254740991ull - total) return false;
    total += value;
    return true;
}
bool validParams (const mastering::MasteringChainParams& p) noexcept
{
    if (int (p.compressor.detector) < 0 || int (p.compressor.detector) > 1
        || int (p.compressor.link) < 0 || int (p.compressor.link) > 1
        || int (p.compressor.mode) < 0 || int (p.compressor.mode) > 2
        || int (p.clipper.shape) < 0 || int (p.clipper.shape) > int (saturation::WaveShaper::Shape::Tape)
        || int (p.dither.shaping) < 0 || int (p.dither.shaping) > 2
        || ! std::isfinite (p.inputGainDb) || ! std::isfinite (p.preLimiterGainDb)
        || ! std::isfinite (p.monoBass.frequencyHz) || ! std::isfinite (p.monoBass.lowWidth)
        || ! std::isfinite (p.stereoAir.frequencyHz) || ! std::isfinite (p.stereoAir.gainDb)
        || ! std::isfinite (p.compressor.rmsWindowMs) || ! std::isfinite (p.compressor.thresholdDb)
        || ! std::isfinite (p.compressor.ratio) || ! std::isfinite (p.compressor.kneeDb)
        || ! std::isfinite (p.compressor.rangeDb) || ! std::isfinite (p.compressor.attackMs)
        || ! std::isfinite (p.compressor.releaseMs) || ! std::isfinite (p.compressor.makeupDb)
        || ! std::isfinite (p.compressor.lookaheadMs) || ! std::isfinite (p.compressorMix)
        || ! std::isfinite (p.clipper.driveDb) || ! std::isfinite (p.clipper.bias)
        || ! std::isfinite (p.clipper.mix) || ! std::isfinite (p.clipper.outputDb)
        || ! std::isfinite (p.clipper.autoComp) || ! std::isfinite (p.clipper.dcBlockHz)
        || ! std::isfinite (p.limiter.ceilingDbTp) || ! std::isfinite (p.limiter.releaseMs)
        || ! std::isfinite (p.limiter.slowReleaseMs) || ! std::isfinite (p.limiter.overCeilingDb)
        || ! std::isfinite (p.limiter.kneeDb)) return false;
    for (const auto& band : p.eqBands)
    {
        if (int (band.type) < 0 || int (band.type) > 8
            || ! std::isfinite (band.dyn.rangeDb) || ! std::isfinite (band.dyn.thrDb)
            || ! std::isfinite (band.dyn.atk) || ! std::isfinite (band.dyn.rel)) return false;
        for (const auto& lane : band.lanes)
            if (! std::isfinite (lane.freq) || ! std::isfinite (lane.Q) || ! std::isfinite (lane.gainDb)) return false;
    }
    return true;
}
CostRules configuredCost() noexcept
{
    const auto root = rules().engine.find ("cost");
    const auto sections = root.find ("sections");
    const auto pump = root.find ("pumping");
    CostRules r;
    r.activityBelowLu = number (root.find ("activityBelowIntegratedLu"));
    r.activityFloorLufs = number (root.find ("activityFloorLufs"));
    r.tooQuietLufs = number (root.find ("tooQuietBelowLufs"));
    r.changeLu = number (sections.find ("changeLu"));
    r.minSeconds = number (sections.find ("minSeconds"));
    r.minComparedShare = number (sections.find ("minComparedShare"));
    r.quantile = number (sections.find ("quantile"));
    r.worstNamedAbove = std::uint32_t (number (sections.find ("worstNamedAbove")));
    r.maxShortfallSeconds = number (sections.find ("masterShorterByAtMost"));
    r.pumpHighPassHz = number (pump.find ("highPassHz"));
    r.pumpLowPassHz = number (pump.find ("lowPassHz"));
    r.pumpSettleSeconds = number (pump.find ("settleSeconds"));
    r.pumpMinRateHz = number (pump.find ("minTraceRateHz"));
    return r;
}
struct Fingerprint
{
    std::uint64_t value = 0xCBF29CE484222325ull;
    void byte (std::uint8_t v) noexcept { value = (value ^ v) * 0x100000001B3ull; }
    void u32 (std::uint32_t v) noexcept { for (unsigned i = 0; i < 4; ++i) byte (std::uint8_t (v >> (8u * i))); }
    void u64 (std::uint64_t v) noexcept { u32 (std::uint32_t (v)); u32 (std::uint32_t (v >> 32)); }
    void i32 (int v) noexcept { u32 (std::uint32_t (v)); }
    void flag (bool v) noexcept { byte (v ? 1u : 0u); }
    void f32 (float v) noexcept { u32 (std::bit_cast<std::uint32_t> (v)); }
    void f64 (double v) noexcept { u64 (std::bit_cast<std::uint64_t> (v)); }
};
} // namespace

std::uint64_t MasterJob::fingerprint (const command::MasterReady& ready) noexcept
{
    Fingerprint h;
    h.u32 (ready.version); h.u32 (ready.deliveryRateHz); h.byte (ready.deliveryBits);
    const auto& t = ready.topology;
    h.i32 (t.internalBlock); h.flag (t.eq); h.flag (t.monoBass); h.flag (t.stereoAir);
    h.flag (t.compressor); h.flag (t.clipper); h.flag (t.limiter); h.flag (t.dither);
    h.f64 (t.compressorLookaheadMs); h.f64 (t.limiterLookaheadMs);
    h.i32 (t.oversampleFactor); h.i32 (t.tapsPerPhase); h.f64 (t.sidechainHpfHz);
    const auto& p = ready.params;
    h.f64 (p.inputGainDb); h.f64 (p.preLimiterGainDb);
    for (const auto& band : p.eqBands)
    {
        h.flag (band.on); h.i32 (int (band.type)); h.flag (band.swept); h.flag (band.bypass);
        h.flag (band.dyn.on); h.f64 (band.dyn.rangeDb); h.f64 (band.dyn.thrDb);
        h.flag (band.dyn.thrAuto); h.f64 (band.dyn.atk); h.f64 (band.dyn.rel);
        for (const auto& lane : band.lanes)
        {
            h.flag (lane.on); h.f64 (lane.freq); h.f64 (lane.Q); h.f64 (lane.gainDb);
            h.i32 (lane.slope); h.flag (lane.bypass);
        }
    }
    h.flag (p.monoBass.enabled); h.f32 (p.monoBass.frequencyHz); h.f32 (p.monoBass.lowWidth);
    h.flag (p.stereoAir.enabled); h.f32 (p.stereoAir.frequencyHz); h.f32 (p.stereoAir.gainDb);
    h.i32 (int (p.compressor.detector)); h.i32 (int (p.compressor.link));
    h.f64 (p.compressor.rmsWindowMs); h.i32 (int (p.compressor.mode));
    h.f64 (p.compressor.thresholdDb); h.f64 (p.compressor.ratio); h.f64 (p.compressor.kneeDb);
    h.f64 (p.compressor.rangeDb); h.f64 (p.compressor.attackMs); h.f64 (p.compressor.releaseMs);
    h.f64 (p.compressor.makeupDb); h.flag (p.compressor.autoMakeup); h.f64 (p.compressor.lookaheadMs);
    h.i32 (int (p.clipper.shape)); h.f32 (p.clipper.driveDb); h.f32 (p.clipper.bias);
    h.f32 (p.clipper.mix); h.f32 (p.clipper.outputDb); h.f32 (p.clipper.autoComp);
    h.f32 (p.clipper.dcBlockHz);
    h.f64 (p.limiter.ceilingDbTp); h.f64 (p.limiter.releaseMs); h.flag (p.limiter.dualRelease);
    h.f64 (p.limiter.slowReleaseMs); h.flag (p.limiter.peakClip);
    h.f64 (p.limiter.overCeilingDb); h.f64 (p.limiter.kneeDb); h.f64 (p.peakClipCutDb); h.f64 (p.peakClipPeakDb);
    h.i32 (p.dither.bits); h.i32 (int (p.dither.shaping)); h.u64 (p.dither.seed);
    h.flag (p.dither.autoBlank); h.i32 (p.dither.autoBlankSamples);
    h.flag (p.bypassEq); h.flag (p.bypassMonoBass); h.flag (p.bypassCompressor);
    h.flag (p.bypassClipper); h.flag (p.bypassLimiter); h.flag (p.bypassDither);
    h.f64 (p.compressorMix);
    return h.value;
}

double limiterBudgetDb (toml::embedded::View engine, double targetLufs) noexcept
{
    const auto budget = engine.find ("landing").find ("limiterBudget");
    const auto middle = budget.find ("middleLufs");
    const double low = number (middle[0]), high = number (middle[1]);
    if (! std::isfinite (targetLufs) || ! std::isfinite (low) || ! std::isfinite (high))
        return std::numeric_limits<double>::quiet_NaN();
    return number (budget.find (targetLufs < low ? "quietDb" : targetLufs <= high ? "middleDb" : "loudDb"));
}

MaxStop maxStopOf (const MaxStopInputs& in) noexcept
{
    using mastering::MasteringSolveStatus; using mastering::MasteringConstraint;
    if (in.peaksAboveCeiling) return MaxStop::TruePeak;
    if (in.overBudget) return MaxStop::OverBudget;
    if (in.floor) return MaxStop::Floor;
    if (in.status == MasteringSolveStatus::Solved) return MaxStop::SearchCeiling;
    if (in.status == MasteringSolveStatus::TargetUnreachable && in.binding == MasteringConstraint::LimiterGainReduction)
        return MaxStop::Budget;
    return MaxStop::Passes;
}

double maxBudgetDb (toml::embedded::View engine, LoudnessMode mode) noexcept
{
    if (mode == LoudnessMode::Manual) return std::numeric_limits<double>::quiet_NaN();
    return number (engine.find ("landing").find ("max").find (mode == LoudnessMode::MaxClean ? "clean" : "dense").find ("budgetDb"));
}

MasterPlan MasterJob::plan (const Session& s, const command::Master& input, const Project& project) noexcept
{
    MasterPlan result;
    if (input.source != 0 && input.source != s.source_.hash) return result;
    if (input.revision != 0 && input.revision != s.revision_) return result;
    if (input.ready.version > 1u) return result;
    if (s.pendingMaster_.master != 0) { result.rejection = Rejection::OutputPending; return result; }
    if (! s.samples_) { result.rejection = Rejection::NoAudio; return result; }
    if (! s.mandatoryReady()) { result.rejection = Rejection::MandatoryUnavailable; return result; }
    double sourceLufs = std::numeric_limits<double>::quiet_NaN();
    for (const auto& value : s.measurementResults_[std::size_t (Analyzer::Loudness)].numbers)
        if (value.name == "integratedLufs" && value.value) sourceLufs = *value.value;
    const auto ruleset = rules();
    const auto target = ruleset.row (project.target);
    // THE DELIVERY FORMAT IS THE TARGET'S (PLAN: rate, bit depth and dither come from the target). Its rate — the
    // source's when the target keeps it (sampleRate 0) — and its depth: 0 names each, the same value restates it,
    // and any other value is refused before anything is priced or allocated.
    const auto deliveryRate = target.sampleRate == 0 ? s.source_.sampleRate : std::uint32_t (target.sampleRate);
    const auto deliveryBits = std::uint8_t (target.bitDepth);   // 16 or 24: the config's build gate
    if ((input.ready.deliveryBits != 0 && input.ready.deliveryBits != deliveryBits)
        || (input.ready.deliveryRateHz != 0 && input.ready.deliveryRateHz != deliveryRate))
    { result.rejection = Rejection::DeliveryFormat; return result; }
    const double targetLufs = project.targetEdit.lufs.value_or (target.lufs.toDouble());
    const double targetTp = project.targetEdit.tp.value_or (target.tp.toDouble());
    const auto engine = ruleset.engine;
    const double reference = number (engine.find ("input").find ("referenceLufs"));
    const double tolerance = number (engine.find ("landing").find ("toleranceLu"));
    const double peakAim = number (engine.find ("landing").find ("truePeakAimDb"));
    const double margin = number (engine.find ("limiter").find ("ceilingMarginDb"));
    const auto passes = engine.find ("landing").find ("passes").integer();
    if (! passes || *passes != 12 || ! std::isfinite (sourceLufs) || sourceLufs <= -120.0
        || ! std::isfinite (targetLufs) || ! std::isfinite (targetTp) || ! std::isfinite (reference)
        || ! std::isfinite (tolerance) || ! std::isfinite (peakAim) || ! std::isfinite (margin))
    { result.rejection = Rejection::MandatoryUnavailable; return result; }
    const double normalization = reference - sourceLufs;
    if (! std::isfinite (normalization) || std::fabs (normalization) > 60.0)
    { result.rejection = Rejection::MandatoryUnavailable; return result; }
    result.ready = input.ready;
    // A master the session decides: the whole plan as the chain gets it, from the project's devices.
    if (input.ready.version == 0)
    {
        const auto inputs = s.planInputs (project);
        writeChain (inputs, project.devices, result.ready);
        result.ready.deliveryRateHz = input.ready.deliveryRateHz;
        result.ready.deliveryBits = input.ready.deliveryBits;
        // What the chain comes to for the target's medium, and the input's quietness: the report's, said of the chain
        // as it is written — the high-pass is the EQ stage's first band, the fold the mono-bass stage.
        const auto& t = result.ready.topology;
        const auto& p = result.ready.params;
        const auto& hpf = p.eqBands[0];
        MasterMedium medium;
        medium.vinyl = target.vinyl;
        medium.crossoverHz = t.monoBass ? double (p.monoBass.frequencyHz) : 0.0;
        medium.cutoffHz = t.eq && hpf.on ? hpf.lanes[0].freq : 0.0;
        medium.lowWidth = t.monoBass ? double (p.monoBass.lowWidth) : 0.0;
        medium.slopeDbPerOct = t.eq && hpf.on ? hpf.lanes[0].slope : 0;
        medium.ceilingDbTp = targetTp;
        medium.overDb = p.limiter.peakClip ? limiterFinding (inputs, project.devices).overDb : 0.0;   // the amount off the peaks
        medium.ruleCrossoverHz = target.monoBass.toDouble();
        medium.ruleLowWidth = ruleset.monoBassWidthDefault.toDouble();
        medium.ruleCutoffHz = target.hpfFloor.toDouble();
        medium.ruleSlopeDbPerOct = target.hpfSlope;
        medium.ruleCeilingDbTp = target.tp.toDouble();
        const bool folded = s.source_.channels == 1
            || (t.monoBass && medium.crossoverHz >= medium.ruleCrossoverHz && medium.lowWidth <= medium.ruleLowWidth);
        const bool cut = t.eq && hpf.on && medium.cutoffHz >= medium.ruleCutoffHz && medium.slopeDbPerOct >= medium.ruleSlopeDbPerOct;
        medium.foldDeparts = target.vinyl && ! folded;
        medium.cutDeparts = target.vinyl && ! cut;
        medium.ceilingDeparts = target.vinyl && ! (targetTp <= medium.ruleCeilingDbTp);
        medium.needlesDeparts = target.vinyl && p.limiter.peakClip;
        medium.ready = target.vinyl && ! medium.foldDeparts && ! medium.cutDeparts && ! medium.ceilingDeparts && ! medium.needlesDeparts;
        medium.quietInput = quietInput (inputs);
        medium.inputLufs = sourceLufs;
        result.medium = medium;
    }
    if (! validParams (result.ready.params))
    { result.rejection = Rejection::NotFinite; return result; }
    result.deliveryRate = deliveryRate;
    result.ready.deliveryRateHz = deliveryRate;
    const double rate = double (result.deliveryRate);
    if (result.deliveryRate < kMinSampleRate || result.deliveryRate > s.capabilities_.maxRateHz
        || s.source_.frames > std::uint64_t (std::numeric_limits<int>::max())
        || ! mastering::MasteringChain::admits (rate, int (s.source_.channels), result.ready.topology))
    { result.rejection = Rejection::OutOfDomain; return result; }
    const bool convert = result.deliveryRate != s.source_.sampleRate;
    const long long delivered = convert ? mastering::DeliveryConverter::deliveredFrames (
        double (s.source_.sampleRate), rate, (long long) s.source_.frames) : (long long) s.source_.frames;
    if (delivered <= 0 || delivered > std::numeric_limits<int>::max())
    { result.rejection = Rejection::TooLong; return result; }
    result.frames = int (delivered);
    const auto costRules = engine.find ("cost");
    const double grWindow = number (costRules.find ("grWindowSeconds"));
    const auto grMin = costRules.find ("grTraceBucketsMin").integer();
    const auto grMax = costRules.find ("grTraceBucketsMax").integer();
    if (! std::isfinite (grWindow) || grWindow <= 0 || ! grMin || ! grMax || *grMin < 1 || *grMax < *grMin)
    { result.rejection = Rejection::OutOfDomain; return result; }
    result.traceBuckets = std::min (result.frames, int (std::clamp (
        std::ceil (double (result.frames) / rate / grWindow), double (*grMin), double (*grMax))));
    result.costHop = 10u * std::max<std::uint64_t> (1u, std::uint64_t (std::floor (rate * .01 + .5)));
    result.costCapacity = std::size_t (std::uint64_t (result.frames) / result.costHop);
    const std::uint64_t sourceHop = 10u * std::max<std::uint64_t> (1u,
        std::uint64_t (std::floor (double (s.source_.sampleRate) * .01 + .5)));
    result.costCapacity = std::max (result.costCapacity,
        std::size_t (s.source_.frames / sourceHop + 1u));
    result.costScratchCapacity = std::max ({ result.costCapacity,
        std::size_t (s.source_.frames / sourceHop + 1u), result.crestCapacity });
    result.waveformCapacity = std::size_t (std::min (result.frames, 2048)) * s.source_.channels;
    result.axesCapacity = std::size_t (std::min (result.frames, 2048));
    result.request.targetLufs = targetLufs;
    result.sourceLufs = sourceLufs;
    result.request.maxTruePeakDbTp = targetTp;
    result.request.toleranceLu = tolerance;
    result.request.truePeakAimDb = peakAim;
    result.request.maxPasses = 12;
    result.request.normalizationGainDb = normalization;
    result.request.initialGainDb = targetLufs - reference;
    result.request.productLanding = true;
    result.request.ceilingMarginDb = margin;
    result.request.grTraceBuckets = result.traceBuckets;
    // LOUDNESS IS A REQUEST (owner, 04.10): the limiter's budget by the target's loudness, and the level landed on the
    // source's gate — read on the source's momentary series. A source without the series whole (a sidecar's facts, rows
    // refused for memory) lands on the master's own gate.
    result.request.limiterGr.limitDb = limiterBudgetDb (engine, targetLufs);
    result.request.limiterGr.statistic = mastering::GrStatistic::P95;
    result.request.limiterActiveInputDb = number (engine.find ("cost").find ("limiterActiveInputDb"));
    const auto onGate = engine.find ("landing").find ("onSourceGate").boolean();
    if (! onGate || ! std::isfinite (result.request.limiterGr.limitDb) || ! std::isfinite (result.request.limiterActiveInputDb))
    { result.rejection = Rejection::MandatoryUnavailable; return result; }
    const auto* momentary = findArray (s.measurementResults_[std::size_t (Analyzer::Loudness)], "momentary");
    if (*onGate && momentary && momentary->complete && momentary->stored != 0)
    {
        result.request.landingOnSourceGate = true;
        result.request.sourceMomentaryLufs = momentary->values.data();
        result.request.sourceMomentaryCount = (long long) momentary->stored;
        result.request.sourceMomentaryHopFrames = (long long) momentary->grid.stepFrames;
    }
    // A MAX MODE ([landing.max]): the landing aims at the modes' search ceiling with the mode's budget in place of the
    // rule by the target's loudness, starting from the target's own number — a landing and a delivery, as a manual
    // master's; its damage is graded as any master's, when the shell asks (owner, 04.10: no guard in the loop).
    result.loudnessMode = loudnessModeOf (ruleset, project);
    if (result.loudnessMode != LoudnessMode::Manual)
    {
        const auto max = engine.find ("landing").find ("max");
        result.floorLufs = number (max.find ("floorLufs"));
        result.request.targetLufs = number (max.find ("ceilingLufs"));
        result.request.limiterGr.limitDb = maxBudgetDb (engine, result.loudnessMode);
        if (! std::isfinite (result.request.targetLufs) || ! std::isfinite (result.request.limiterGr.limitDb)
            || ! std::isfinite (result.floorLufs))
        { result.rejection = Rejection::MandatoryUnavailable; return result; }
    }
    result.ready.params.limiter.ceilingDbTp = targetTp - margin;
    result.ready.deliveryBits = deliveryBits;
    result.ready.params.dither.bits = deliveryBits;
    result.request.pcmBits = deliveryBits;
    if (! std::isfinite (result.ready.params.inputGainDb)
        || std::fabs (result.ready.params.inputGainDb + normalization) > 60.0
        || ! std::isfinite (result.ready.params.preLimiterGainDb))
    { result.rejection = Rejection::NotFinite; return result; }
    constexpr int block = 1024;
    mastering::OfflineRenderer::Storage renderStorage;
    if (! mastering::OfflineRenderer::storageFor (int (s.source_.channels), block, renderStorage)) return result;
    const auto chainBytes = mastering::MasteringChain::prepareBytes (rate, int (s.source_.channels), result.ready.topology);
    const auto solverBytes = mastering::TargetLoudnessSolver::prepareBytes (
        block, result.ready.topology.internalBlock, result.ready.topology.oversampleFactor);
    const auto converterBytes = convert ? mastering::DeliveryConverter::prepareBytes (
        double (s.source_.sampleRate), rate, int (s.source_.channels), block) : 0u;
    const auto search = mastering::LandingSearch::storageForProgramme ((long long) s.source_.frames,
        int (s.source_.channels), rate, result.frames, result.traceBuckets);
    const Pcm shape { nullptr, s.source_.channels, s.source_.frames, s.source_.sampleRate };
    result.crestParams = MeasurementPlan::parametersFor (shape).crest;
    const double crestRate = convert ? double (s.source_.sampleRate) : rate;
    const long long crestFrames = convert ? (long long) s.source_.frames : result.frames;
    const auto crest = analysis::BandCrest::storageFor (crestRate, int (s.source_.channels),
        crestFrames, result.crestParams);
    if (crest.ok && crest.hopCapacity >= std::size_t (result.crestParams.blockHops))
        result.crestCapacity = crest.hopCapacity - std::size_t (result.crestParams.blockHops) + 1u;
    result.costScratchCapacity = std::max (result.costScratchCapacity, result.crestCapacity);
    analysis::StreamingLoudnessMeter::Storage costMeterStorage;
    result.costMeterAdmitted = analysis::StreamingLoudnessMeter::storageFor (
        rate, double (result.frames), costMeterStorage);
    const auto costMeterBytes = result.costMeterAdmitted ? costMeterStorage.bytes() : 0u;
    const std::uint64_t crestRows = std::uint64_t (result.crestCapacity) * 15u * sizeof (double);
    // Two loudness series (short-term and momentary), the sections, the scratch, the waveform's buckets twice: the
    // L/R rows a snapshot carries and the four axes a query answers.
    const std::uint64_t axesRows = std::uint64_t (result.axesCapacity) * sizeof (analysis::WaveformColumn);
    const std::uint64_t costRows = std::uint64_t (result.costCapacity) * (2u * sizeof (double) + sizeof (MasterSection))
        + std::uint64_t (result.costScratchCapacity) * sizeof (double)
        + std::uint64_t (result.waveformCapacity) * sizeof (MasterWaveformBucket) + axesRows;
    // The glue's trace beside the limiter's and the clipper's where the glue compresses (the report's own test).
    result.glueTrace = result.ready.topology.compressor && ! result.ready.params.bypassCompressor;
    // And the saturation's shave where the soft clipper shapes (the report's own test for its cut).
    result.saturationTrace = result.ready.topology.clipper && ! result.ready.params.bypassClipper;
    result.retainedRowBytes = 12u * sizeof (LandingPass)
        + (2u + (result.glueTrace ? 1u : 0u) + (result.saturationTrace ? 1u : 0u)) * std::uint64_t (result.traceBuckets)
            * sizeof (LandingTraceBucket) + crestRows + costRows;
    const auto impactChainBytes = convert ? mastering::MasteringChain::prepareBytes (
        double (s.source_.sampleRate), int (s.source_.channels), result.ready.topology) : 0u;
    const auto impactScratchBytes = convert ? std::uint64_t (s.source_.channels) * 1024u * sizeof (float) : 0u;
    if (chainBytes == 0 || solverBytes == 0 || (convert && converterBytes == 0) || ! search.ok)
    { result.rejection = Rejection::OutOfDomain; return result; }
    const auto outputBytes = search.outputBytes;
    result.largestBlock = std::max ({ outputBytes, chainBytes, solverBytes, converterBytes,
        renderStorage.bytes(), search.workspaceBytes, std::uint64_t (sizeof (MasterJob)),
        crestRows, costMeterBytes, std::uint64_t (result.costCapacity) * sizeof (MasterSection),
        std::uint64_t (result.waveformCapacity) * sizeof (MasterWaveformBucket), axesRows,
        std::uint64_t (result.costScratchCapacity) * sizeof (double),
        crest.ok ? crest.bytes() : 0u, impactChainBytes, impactScratchBytes });
    std::uint64_t total = 0;
    // Include constructors, all retained search work, two compact traces, and an allocator margin
    // for every tier's vector growth and Debug proxies.
    if (! add (total, sizeof (MasterJob)) || ! add (total, outputBytes)
        || ! add (total, chainBytes) || ! add (total, solverBytes)
        || ! add (total, converterBytes) || ! add (total, renderStorage.bytes())
        || ! add (total, search.workspaceBytes)
        || ! add (total, mastering::MasteringChain::constructBytes())
        || ! add (total, mastering::TargetLoudnessSolver::constructBytes())
        || ! add (total, mastering::DeliveryConverter::constructBytes())
        || ! add (total, result.retainedRowBytes)
        || ! add (total, costMeterBytes)
        || ! add (total, crest.ok ? crest.firstBytes() : 0u)
        || ! add (total, impactChainBytes) || ! add (total, impactScratchBytes)
        || ! add (total, 128u * 4096u))
    { result.rejection = Rejection::TooLong; return result; }
    // THE DAMAGE'S WALKS (src/Damage.h): a job of their own after the master is delivered, priced here as that job and
    // admitted with the master — it lives in the room this job leaves; a source they cannot run on still masters, and
    // its report says why the damage has no grade.
    result.damage = Damage::plan (s.source_.sampleRate, s.source_.frames, int (s.source_.channels), result.ready.topology);
    if (result.damage.ok)
    {
        if (! add (total, DamageJob::bytes (result.damage))) { result.rejection = Rejection::TooLong; return result; }
        result.largestBlock = std::max (result.largestBlock, result.damage.largestBlock);
    }
    result.bytes = total;
    result.largestBlock += 4096u;
    result.rejection = Rejection::None;
    return result;
}

bool MasterJob::begin (const Session& s, const MasterPlan& plan)
{
    session = &s;
    crestParams = plan.crestParams;
    ready = plan.ready;
    medium = plan.medium;
    damagePlan = plan.damage;
    mode = plan.loudnessMode;
    floorLufs = plan.floorLufs;
    floorRequest = plan.request;
    floorRequest.targetLufs = plan.floorLufs;
    floorRequest.limiterGr = {};
    floorRequest.landingOnSourceGate = false;
    floorPass = false;
    sourceLufs = plan.sourceLufs;
    targetLufs = plan.request.targetLufs;
    targetTp = plan.request.maxTruePeakDbTp;
    channels = int (s.source_.channels);
    frames = plan.frames;
    deliveryRate = plan.deliveryRate;
    sourceRate = s.source_.sampleRate;
    sourceFrames = s.source_.frames;
    costHop = plan.costHop;
    constexpr int block = 1024;
    if (! chain.prepare (double (deliveryRate), channels, plan.ready.topology)
        || ! renderer.prepare (channels, block)
        || ! solver.prepare (double (deliveryRate), channels, block,
                            plan.ready.topology.internalBlock, plan.ready.topology.oversampleFactor)) return false;
    costMeterReady = plan.costMeterAdmitted
        && costMeter.prepareForSamples (double (deliveryRate), channels, double (frames));
    costAxesReady = costAxes.prepare (deliveryRate, unsigned (channels));
    const bool convert = deliveryRate != s.source_.sampleRate;
    if (convert && ! converter.prepare (double (s.source_.sampleRate), double (deliveryRate), channels, block)) return false;
    if (convert) impactScratch.reset (new float[std::size_t (channels) * block]);
    output.reset (new float[std::size_t (frames) * std::size_t (channels)]);
    for (int c = 0; c < channels; ++c)
    {
        sourcePlanes[c] = s.samples_.get() + std::size_t (c) * std::size_t (s.source_.frames);
        outputPlanes[c] = output.get() + std::size_t (c) * std::size_t (frames);
    }
    chain.setParams (plan.ready.params);
    return search.begin (chain, renderer, plan.ready.params, sourcePlanes,
        (long long) s.source_.frames, double (s.source_.sampleRate), outputPlanes,
        channels, frames, plan.request, {}, convert ? &converter : nullptr);
}

mastering::StepResult MasterJob::step (long long budget) noexcept
{
    using mastering::StepResult;
    if (budget < 0 || stage == Stage::Failed) return StepResult::Failed;
    if (stage == Stage::Done) return StepResult::Done;
    if (budget == 0) return StepResult::More;
    if (stage == Stage::Search)
    {
        const auto result = search.step (budget);
        if (result == StepResult::More) return result;
        // A MAX MODE NEVER DELIVERS A FILE QUIETER THAN [landing.max] floorLufs (owner, 04.10: "always pulled up to −14"),
        // whichever target it sits on: a mix so dense the mode's budget held its file under that floor — by its BS.1770
        // reading, the number a person reads — is landed again on the floor, by that reading, with no budget, and
        // delivered there (MaxStop::Floor).
        if (mode != LoudnessMode::Manual && ! floorPass && result == StepResult::Done && search.result().deliverable)
        {
            const double file = search.result().achievedLufs;
            if (std::isfinite (floorLufs) && std::isfinite (file) && file < floorLufs - floorRequest.toleranceLu)
            {
                floorPass = true;
                if (! beginFloor()) { stage = Stage::Failed; return StepResult::Failed; }
                return StepResult::More;
            }
        }
        return settleLanding (result);
    }
    if (stage == Stage::Prepare)
    {
        auto& c = report.crest;
        const bool impact = sourceRate != deliveryRate;
        const auto rate = impact ? sourceRate : deliveryRate;
        c.sampleRateHz = rate; c.frames = impact ? sourceFrames : std::uint64_t (frames);
        c.blockHops = std::uint32_t (crestParams.blockHops);
        c.edgeLowHz = crestParams.bandEdgeHz[0]; c.edgeMidHz = crestParams.bandEdgeHz[1];
        c.edgeHighHz = crestParams.bandEdgeHz[2]; c.sourceRateCheck = impact;
        if (session->masterRows_[session->masterCount_].crestCapacity == 0)
        { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::TooShort; stage = Stage::CostRead; return StepResult::More; }
        if (impact)
        {
            if (! chain.prepare (double (sourceRate), channels, ready.topology))
            { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::Unsupported; stage = Stage::CostRead; return StepResult::More; }
            chain.setParams (winningParams());
            chain.reset();
            impactDelay = chain.latencySamples();
        }
        crest.setParams (crestParams);
        if (! crest.prepare (double (rate), channels, (long long) c.frames))
        { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::Unsupported; stage = Stage::CostRead; return StepResult::More; }
        c.hopFrames = std::uint32_t (crest.hopSamples());
        stage = Stage::Read;
        return StepResult::More;
    }
    if (stage == Stage::Read)
    {
        constexpr int block = 1024;
        const bool impact = sourceRate != deliveryRate;
        if (impact)
        {
            const long long total = (long long) sourceFrames + impactDelay;
            const int n = int (std::min<long long> ({ budget, block, total - crestCursor }));
            float* planes[2] { impactScratch.get(), channels == 2 ? impactScratch.get() + block : nullptr };
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const auto frame = crestCursor + i;
                    planes[ch][i] = frame < (long long) sourceFrames ? sourcePlanes[ch][frame] : 0.0f;
                }
            if (! chain.process (planes, channels, n))
            { report.crest.status = MeasurementStatus::Unavailable; report.crest.reason = MeasurementReason::NonFinite;
              stage = Stage::CostRead; return StepResult::More; }
            const int skip = int (std::min<long long> (n, std::max (0LL, impactDelay - crestCursor)));
            const int kept = int (std::max (0LL, std::min<long long> (n, total - crestCursor) - skip));
            const float* produced[2] { planes[0] + skip, channels == 2 ? planes[1] + skip : nullptr };
            if (kept != 0 && ! crest.process (produced, channels, kept))
            { report.crest.status = MeasurementStatus::Unavailable; report.crest.reason = MeasurementReason::NonFinite;
              stage = Stage::CostRead; return StepResult::More; }
            crestCursor += n;
            if (crestCursor == total) { report.checkPasses = 1; stage = Stage::Finish; }
        }
        else
        {
            const int n = int (std::min<long long> ({ budget, block, frames - crestCursor }));
            const float* planes[2] { outputPlanes[0] + crestCursor,
                channels == 2 ? outputPlanes[1] + crestCursor : nullptr };
            if (! crest.process (planes, channels, n))
            { report.crest.status = MeasurementStatus::Unavailable; report.crest.reason = MeasurementReason::NonFinite;
              stage = Stage::CostRead; return StepResult::More; }
            crestCursor += n;
            if (crestCursor == frames) stage = Stage::Finish;
        }
        return StepResult::More;
    }
    if (stage == Stage::Finish)
    {
        crest.finish();
        auto& c = report.crest;
        c.blocks = std::uint64_t (crest.blockCount());
        c.complete = crest.droppedHops() == 0 && crest.samplesProcessed() == (long long) c.frames;
        if (! crest.valid() || ! c.complete || c.blocks > session->masterRows_[session->masterCount_].crestCapacity)
        {
            c.status = MeasurementStatus::Unavailable;
            c.reason = crest.invalidReason() == analysis::BandCrestInvalid::NoBlock
                ? MeasurementReason::TooShort : MeasurementReason::NonFinite;
            stage = Stage::CostRead; return StepResult::More;
        }
        const auto& source = session->measurementResults_[std::size_t (Analyzer::Crest)];
        if (source.status == MeasurementStatus::Pending)
        { c.status = MeasurementStatus::Pending; c.reason = MeasurementReason::Pending; }
        else if (source.status != MeasurementStatus::Ready)
        { c.status = MeasurementStatus::Unavailable; c.reason = source.reason; }
        else
        {
            const auto view = MeasurementCrest::view (source);
            const bool compatible = MasterCrestGrid::compatible (c, view, crestParams);
            if (compatible)
            { c.status = MeasurementStatus::Ready; c.reason = MeasurementReason::None; sourceMask = view.mask; }
            else { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::Unsupported; }
        }
        stage = Stage::Copy;
        return StepResult::More;
    }
    if (stage == Stage::Copy)
    {
        const auto limit = std::min<std::size_t> (std::size_t (report.crest.blocks), crestCopied + 128u);
        auto& rows = session->masterRows_[session->masterCount_];
        auto* data = rows.crest.get();
        auto* mask = data + rows.crestCapacity * 10u;
        for (; crestCopied < limit; ++crestCopied)
        {
            for (int band = 0; band < 5; ++band)
            {
                data[crestCopied * 10u + std::size_t (band * 2)] = crest.blockPeakLin ((long long) crestCopied, band);
                data[crestCopied * 10u + std::size_t (band * 2 + 1)] = crest.blockMeanSq ((long long) crestCopied, band);
                if (! sourceMask.empty()) mask[crestCopied * 5u + std::size_t (band)] = sourceMask[crestCopied * 5u + std::size_t (band)];
            }
        }
        if (crestCopied == report.crest.blocks)
        {
            report.crest.rows = { data, crestCopied * 10u };
            if (! sourceMask.empty()) report.crest.sourceMask = { mask, crestCopied * 5u };
            stage = Stage::CostRead;
            return StepResult::More;
        }
        return StepResult::More;
    }
    if (stage == Stage::CostRead)
    {
        constexpr std::uint64_t block = 1024;
        const auto remainingHop = costHop - costCursor % costHop;
        const auto n = std::uint64_t (std::min<long long> (budget,
            (long long) std::min ({ block, remainingHop, std::uint64_t (frames) - costCursor })));
        if (n == 0) { stage = Stage::CostFinish; return StepResult::More; }
        const float* planes[2] { outputPlanes[0] + costCursor,
            channels == 2 ? outputPlanes[1] + costCursor : nullptr };
        if (costMeterReady && ! costMeter.process (planes, channels, int (n))) costMeterReady = false;
        auto& rows = session->masterRows_[session->masterCount_];
        const std::uint64_t bucketCount = rows.waveformCapacity / std::size_t (channels);
        for (std::uint64_t i = 0; i < n; ++i)
        {
            const auto frame = costCursor + i;
            const auto bucket = std::min (bucketCount - 1u, frame * bucketCount / std::uint64_t (frames));
            if (bucket == initializedWaveBuckets)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    auto& w = rows.waveform[std::size_t (bucket * std::uint64_t (channels) + std::uint64_t (ch))];
                    w.fromFrame = (bucket * std::uint64_t (frames) + bucketCount - 1u) / bucketCount;
                    w.toFrame = ((bucket + 1u) * std::uint64_t (frames) + bucketCount - 1u) / bucketCount;
                    w.channel = std::uint32_t (ch);
                    w.minimum = w.maximum = w.rms = 0;
                    w.finite = 0;
                }
                ++initializedWaveBuckets;
            }
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& w = rows.waveform[std::size_t (bucket * std::uint64_t (channels) + std::uint64_t (ch))];
                const double x = planes[ch][std::size_t (i)];
                if (std::isfinite (x))
                {
                    if (w.finite == 0) w.minimum = w.maximum = x;
                    else { w.minimum = std::min (w.minimum, x); w.maximum = std::max (w.maximum, x); }
                    w.rms += x * x; ++w.finite;
                }
            }
            // The same bucket as the source's waveform has a column: closed on its last frame.
            if (costAxesReady && rows.axes)
            {
                costAxes.add (planes[0][std::size_t (i)], channels == 2 ? double (planes[1][std::size_t (i)]) : 0.0);
                if (frame + 1u == rows.waveform[std::size_t (bucket * std::uint64_t (channels))].toFrame && bucket < rows.axesCapacity)
                { rows.axes[std::size_t (bucket)] = costAxes.take(); rows.axesRows = std::size_t (bucket) + 1u; }
            }
        }
        costCursor += n;
        if (costMeterReady && costCursor % costHop == 0 && costStored < rows.costCapacity)
        {
            if (rows.costMomentary) rows.costMomentary[std::size_t (costStored)] = costMeter.momentaryLufs();
            rows.costSeries[std::size_t (costStored++)] = costMeter.shortTermLufs();
            rows.loudnessRows = costStored; rows.loudnessHop = costHop;
        }
        if (costCursor == std::uint64_t (frames)) stage = Stage::CostFinish;
        return StepResult::More;
    }
    if (stage == Stage::CostFinish)
    {
        auto& rows = session->masterRows_[session->masterCount_];
        costResult = {};
        costRules = configuredCost();
        costResult.sourceRateHz = sourceRate; costResult.masterRateHz = deliveryRate;
        costResult.sourceFrames = sourceFrames; costResult.masterFrames = std::uint64_t (frames);
        costResult.waveform = { rows.waveform.get(), rows.waveformCapacity };
        const auto& solution = search.result();
        double quantile = 0;
        // The limiter's reduction over its ACTIVE windows (input above [cost] limiterActiveInputDb): the statistic the
        // landing's budget holds, so silence does not water it down.
        const auto& active = solution.limiterActive.stats;
        if (active.valid && solution.limiterActiveGrWindows.quantile (.5, quantile))
            costResult.limiterP50Db = { MeasurementReason::None, quantile, active.frames };
        else costResult.limiterP50Db.reason = MeasurementReason::Unsupported;
        if (active.valid && solution.limiterActiveGrWindows.quantile (.95, quantile))
            costResult.limiterP95Db = { MeasurementReason::None, quantile, active.frames };
        else costResult.limiterP95Db.reason = MeasurementReason::Unsupported;
        if (solution.limiterActive.stats.valid)
            costResult.limiterActiveShare = { MeasurementReason::None,
                solution.limiterActive.stats.activeFraction, solution.limiterActive.stats.frames };
        else costResult.limiterActiveShare.reason = MeasurementReason::Unsupported;
        // WHAT THE GLUE AND THE SATURATION DID, each on its own stage (owner decisions 3.8, 3.9): the compressor's gain
        // reduction — its P95 over the programme's 4 ms windows and its largest sample — and the soft clipper's cut of
        // peaks against its own gain on a quiet sound. A stage out of the chain has no number.
        const bool compressing = ready.topology.compressor && ! ready.params.bypassCompressor;
        const auto& glue = solution.measured.compressor;
        if (compressing && glue.valid && std::isfinite (glue.maxDb) && solution.grQuantile (mastering::GrStage::Compressor, .95, quantile))
        {
            costResult.glueP95Db = { MeasurementReason::None, quantile, glue.frames };
            costResult.glueMaxDb = { MeasurementReason::None, glue.maxDb, glue.frames };
        }
        else costResult.glueP95Db.reason = costResult.glueMaxDb.reason = compressing ? MeasurementReason::Unsupported : MeasurementReason::NoSignal;
        const bool shaping = ready.topology.clipper && ! ready.params.bypassClipper;
        if (clipCounted && clipPeaks.quietGain > 0.0 && clipPeaks.loudLeastRatio > 0.0 && clipPeaks.loudUsualRatio > 0.0)
        {
            costResult.saturationCutMaxDb = { MeasurementReason::None,
                20.0 * core::det::log10 (clipPeaks.quietGain / clipPeaks.loudLeastRatio), clipPeaks.loudQuanta };
            costResult.saturationCutUsualDb = { MeasurementReason::None,
                20.0 * core::det::log10 (clipPeaks.quietGain / clipPeaks.loudUsualRatio), clipPeaks.loudQuanta };
        }
        else costResult.saturationCutMaxDb.reason = costResult.saturationCutUsualDb.reason = shaping ? MeasurementReason::Unsupported : MeasurementReason::NoSignal;
        costPumpScan.start (solution.limiterTrace, deliveryRate, costRules);
        stage = Stage::CostWave;
        return StepResult::More;
    }
    if (stage == Stage::CostWave)
    {
        auto& rows = session->masterRows_[session->masterCount_];
        const auto limit = std::min<std::uint64_t> (rows.waveformCapacity,
            costFinishCursor + std::uint64_t (std::min<long long> (budget, 1024)));
        while (costFinishCursor < limit)
        {
            auto& wave = rows.waveform[std::size_t (costFinishCursor++)];
            if (wave.finite != 0) wave.rms = std::sqrt (wave.rms / double (wave.finite));
        }
        if (costFinishCursor == rows.waveformCapacity) { costFinishCursor = 0; stage = Stage::CostPump; }
        return StepResult::More;
    }
    if (stage == Stage::CostPump)
    {
        const auto& trace = search.result().limiterTrace;
        if (costPumpScan.step (trace, std::uint32_t (std::min<long long> (budget, 1024))))
        { costResult.pumpingRmsDb = costPumpScan.answer; stage = Stage::CostActive; }
        return StepResult::More;
    }
    if (stage == Stage::CostActive)
    {
        const auto* sourceMomentary = findArray (
            session->measurementResults_[std::size_t (Analyzer::Loudness)], "momentary");
        const auto* momentaryReasons = findArray (
            session->measurementResults_[std::size_t (Analyzer::Loudness)], "momentaryReasons");
        if (! sourceMomentary || ! momentaryReasons
            || momentaryReasons->stored != sourceMomentary->stored)
        { costResult.activeWindowShare.reason = MeasurementReason::Unsupported; stage = Stage::CostShape; return StepResult::More; }
        const auto count = std::size_t (sourceMomentary->stored);
        const auto limit = std::min<std::uint64_t> (count,
            costFinishCursor + std::uint64_t (std::min<long long> (budget, 1024)));
        const double gate = std::max (costRules.activityFloorLufs, sourceLufs - costRules.activityBelowLu);
        while (costFinishCursor < limit)
        {
            const auto i = std::size_t (costFinishCursor++);
            const double level = sourceMomentary->values[i];
            const auto reason = std::bit_cast<std::uint64_t> (momentaryReasons->values[i]);
            if (reason == std::bit_cast<std::uint64_t> (double (MeasurementReason::NoSignal))) ++activeJudged;
            else if (reason == std::bit_cast<std::uint64_t> (double (MeasurementReason::None))
                     && std::isfinite (level))
            { ++activeJudged; if (level >= gate) ++activeCount; }
        }
        if (costFinishCursor == count)
        {
            if (activeJudged)
                costResult.activeWindowShare = { MeasurementReason::None,
                    double (activeCount) / double (activeJudged), activeJudged };
            else costResult.activeWindowShare.reason = MeasurementReason::NoSignal;
            costFinishCursor = 0; stage = Stage::CostShape;
        }
        return StepResult::More;
    }
    if (stage == Stage::CostShape)
    {
        auto& rows = session->masterRows_[session->masterCount_];
        const auto* sourceShort = findArray (
            session->measurementResults_[std::size_t (Analyzer::Loudness)], "shortTerm");
        if (! costMeterReady || ! sourceShort || sourceShort->grid.stepFrames == 0 || ! rows.costSeries)
        { costResult.shapeP95Lu.reason = MeasurementReason::Unsupported; stage = Stage::CostCrest; return StepResult::More; }
        const auto source = sourceShort->values.first (std::size_t (sourceShort->stored));
        const std::span<const double> master { rows.costSeries.get(), std::size_t (costStored) };
        const std::span<MasterSection> sections { rows.sections.get(), rows.costCapacity };
        const std::span<double> scratch { rows.costScratch.get(), rows.costScratchCapacity };
        if (! costShapeStarted)
        {
            costShapeScan.start (source, master, sourceRate, sourceShort->grid.stepFrames,
                deliveryRate, costHop, sourceLufs, *report.achievedLufs, costRules, sections, scratch);
            costShapeStarted = true;
        }
        if (costShapeScan.step (source, master, sections, scratch,
                                std::uint32_t (std::min<long long> (budget, 1024))))
        {
            costResult.shapeP95Lu = costShapeScan.answer;
            costResult.sections = { rows.sections.get(), std::size_t (costShapeScan.sectionCount) };
            stage = costResult.shapeP95Lu.value ? Stage::CostWorst : Stage::CostCrest;
        }
        return StepResult::More;
    }
    if (stage == Stage::CostWorst)
    {
        const auto& rows = session->masterRows_[session->masterCount_];
        const auto limit = std::min<std::uint64_t> (costShapeScan.sectionCount,
            costFinishCursor + std::uint64_t (std::min<long long> (budget, 1024)));
        while (costFinishCursor < limit)
        {
            const auto i = std::size_t (costFinishCursor++);
            if (rows.sections[i].compared)
            {
                ++costComparable;
                if (! rows.sections[costWorst].compared
                    || std::fabs (rows.sections[i].shiftLu) > std::fabs (rows.sections[costWorst].shiftLu))
                    costWorst = i;
            }
        }
        if (costFinishCursor == costShapeScan.sectionCount)
        {
            if (costComparable > 0)
            {
                costResult.largestSectionShiftLu = { MeasurementReason::None,
                    rows.sections[costWorst].shiftLu, costComparable };
                if (costComparable > costRules.worstNamedAbove)
                    costResult.worstSectionIndex = std::uint32_t (costWorst);
            }
            else costResult.largestSectionShiftLu.reason = MeasurementReason::TooShort;
            costFinishCursor = 0; stage = Stage::CostCrest;
        }
        return StepResult::More;
    }
    if (stage == Stage::CostCrest)
    {
        MasterCostValue* bands[] { &costResult.crestLowDb, &costResult.crestLowMidDb,
            &costResult.crestHighMidDb, &costResult.crestHighDb, &costResult.crestFullDb };
        if (report.crest.status != MeasurementStatus::Ready)
        {
            for (auto* band : bands) band->reason = report.crest.reason;
            stage = Stage::CostPublish; return StepResult::More;
        }
        auto& rows = session->masterRows_[session->masterCount_];
        const auto view = MeasurementCrest::view (session->measurementResults_[std::size_t (Analyzer::Crest)]);
        const std::span<double> scratch { rows.costScratch.get(), rows.costScratchCapacity };
        if (! costCrestStarted)
        { costCrestScan.start (view, report.crest, costBand, scratch); costCrestStarted = true; }
        if (costCrestScan.step (view, report.crest, scratch,
                                std::uint32_t (std::min<long long> (budget, 1024))))
        {
            *bands[costBand++] = costCrestScan.answer;
            costCrestStarted = false;
            if (costBand == 5) stage = Stage::CostPublish;
        }
        return StepResult::More;
    }
    if (stage == Stage::CostPublish)
    {
        report.cost = costResult;
        // The loudness range is the master's own; the damage is graded after it, by a job of its own: Pending until then.
        if (settleLra(), ! damagePlan.ok) report.damage.reason = damagePlan.reason;
        else
        {
            report.damage.status = MeasurementStatus::Pending;
            report.damage.reason = MeasurementReason::Pending;
            damageFollows = true;
        }
        stage = Stage::Done;
        return StepResult::Done;
    }
    return StepResult::Failed;
}

// THE LANDING SETTLED — the search's render: the report's numbers, the hints of a manual miss, and what ended a max mode.
mastering::StepResult MasterJob::settleLanding (mastering::StepResult result) noexcept
{
    using mastering::StepResult;
    const auto& solved = search.result();
    clipCounted = result == StepResult::Done && ready.topology.clipper && ! ready.params.bypassClipper
        && chain.clipperPeaks (number (rules().engine.find ("saturation").find ("cut").find ("loudShare")), clipPeaks);
    report.targetLufs = targetLufs;
    report.ceilingDbTp = targetTp;
    report.medium = medium;
    report.targetMet = solved.status == mastering::MasteringSolveStatus::Solved;
    if (solved.measured.loudnessValid && std::isfinite (solved.measured.integratedLufs)
        && std::isfinite (solved.measured.truePeakDbTp))
    {
        report.status = MeasurementStatus::Ready;
        report.reason = MeasurementReason::None;
        report.achievedLufs = solved.measured.integratedLufs;
        report.truePeakDbTp = solved.measured.truePeakDbTp;
        report.missLu = solved.measured.integratedLufs - targetLufs;
        report.gainFromSourceDb = solved.measured.integratedLufs - sourceLufs;
        if (std::isfinite (solved.measured.plrDb))
        { report.plrDb = solved.measured.plrDb; report.plrReason = MeasurementReason::None; }
        else report.plrReason = MeasurementReason::NonFinite;
        if (solved.measured.lraValid && std::isfinite (solved.measured.loudnessRangeLu))
        { report.lraLu = solved.measured.loudnessRangeLu; report.lraReason = MeasurementReason::None; }
        else report.lraReason = solved.measured.nonFiniteSubHops != 0
            ? MeasurementReason::NonFinite : MeasurementReason::TooShort;
        const bool whole = solved.measured.droppedBlocks == 0 && solved.measured.nonFiniteSubHops == 0;
        report.peakSafe = solved.measured.truePeakDbTp <= targetTp && whole;
        // Delivered: under the ceiling — or, where no render was (owner, 01.10), the gentlest one, marked.
        report.peaksAboveCeiling = solved.peaksAboveCeiling && whole && solved.measured.truePeakDbTp > targetTp;
        report.deliverable = solved.deliverable && (report.peakSafe || report.peaksAboveCeiling);
    }
    else report.reason = solved.measured.nonFiniteSubHops != 0 ? MeasurementReason::NonFinite
                       : solved.measured.gatingBlocks == 0 ? MeasurementReason::NoSignal
                       : MeasurementReason::TooShort;
    if (report.status != MeasurementStatus::Ready)
    { report.lraReason = report.reason; report.plrReason = report.reason; }
    const auto hint = [&] (mastering::LandingReason reason) noexcept -> std::optional<MasterHint>
    {
        if (reason == mastering::LandingReason::None) return {};
        MasterHint h; h.reason = LandingReason (reason);
        switch (reason)
        {
            case mastering::LandingReason::ExcessSubBass:
                h.evidence = solved.sourceSubBassShare * 100.0; h.percent = true; break;
            case mastering::LandingReason::SharpPeaks: h.evidence = solved.measured.limiter.maxDb; break;
            case mastering::LandingReason::DarkMix:
                h.evidence = solved.sourcePresenceShare * 100.0; h.percent = true; break;
            case mastering::LandingReason::LoudnessDemand: h.evidence = solved.distanceLu; break;
            case mastering::LandingReason::GainRange: h.evidence = solved.preLimiterGainDb; break;
            case mastering::LandingReason::TruePeak: h.evidence = solved.measured.truePeakDbTp; break;
            case mastering::LandingReason::None: break;
        }
        return std::isfinite (h.evidence) ? std::optional<MasterHint> (h) : std::nullopt;
    };
    // Held short by the limiter's budget: the verdict says so, and nothing in the mix is blamed for it.
    const bool budgetHeld = solved.status == mastering::MasteringSolveStatus::TargetUnreachable
                         && solved.binding == mastering::MasteringConstraint::LimiterGainReduction;
    if (mode == LoudnessMode::Manual && ! report.targetMet && ! budgetHeld && report.missLu && std::fabs (*report.missLu) > 0.0)
    {
        report.firstHint = hint (solved.mainReason);
        report.secondHint = hint (solved.secondReason);
        if (! report.firstHint)
            report.firstHint = MasterHint { LandingReason::LoudnessDemand, std::fabs (*report.missLu), false };
    }
    report.loudnessMode = mode;
    if (mode != LoudnessMode::Manual && solved.deliverable)
        report.maxStop = maxStopOf ({ solved.peaksAboveCeiling, std::isfinite (search.overBudgetDb()), floorPass, solved.status,
                                      solved.binding });
    if (result == StepResult::Failed || ! solved.deliverable)
    {
        report.crest.status = MeasurementStatus::Unavailable;
        report.crest.reason = MeasurementReason::NotImplemented;
        settleLra();
        stage = Stage::Done;
        return result;
    }
    stage = Stage::Prepare;
    return StepResult::More;
}

// THE LANDING AGAIN, ON THE FLOOR: the ready parameters, the request on the target's own loudness with no budget, from
// the drive the first landing ended at; the same chain, buffers and source.
bool MasterJob::beginFloor() noexcept
{
    auto request = floorRequest;
    request.initialGainDb = search.result().preLimiterGainDb;
    chain.setParams (ready.params);
    const bool convert = deliveryRate != sourceRate;
    return search.begin (chain, renderer, ready.params, sourcePlanes, (long long) sourceFrames, double (sourceRate), outputPlanes,
                         channels, frames, request, {}, convert ? &converter : nullptr);
}

// The parameters the delivered render ran with: the request's, with the landing's normalisation, gain and ceiling, and
// the peak its first pass measured for the needles.
mastering::MasteringChainParams MasterJob::winningParams() const noexcept
{
    auto winning = ready.params;
    winning.inputGainDb += search.result().normalizationGainDb;
    winning.preLimiterGainDb = search.result().preLimiterGainDb;
    winning.limiter.ceilingDbTp = search.result().ceilingDbTp;
    winning.peakClipPeakDb = search.peakClipPeakDb();
    return winning;
}

// THE LOUDNESS RANGE, INPUT AGAINST MASTER: the source's programme report beside the report's own; the change in LU and
// as a share of the input's. A number that is absent says why.
void MasterJob::settleLra() noexcept
{
    auto& d = report.damage;
    d.masterLraLu = report.lraLu;
    std::optional<double> input;
    auto inputReason = MeasurementReason::Pending;
    // The programme report has ended before any master is taken (the first measurement ends after it — the table's
    // Measured columns): its reason here is final, never Pending.
    const auto& programme = session->measurementResults_[std::size_t (Analyzer::Programme)];
    detail::debugBound (programme.status != MeasurementStatus::Pending);
    if (programme.status != MeasurementStatus::Ready) inputReason = programme.reason;
    else
    {
        inputReason = MeasurementReason::Unsupported;
        for (const auto& value : programme.numbers)
            if (value.name == "lraLu")
            {
                if (value.value && std::isfinite (*value.value)) { input = value.value; inputReason = MeasurementReason::None; }
                else inputReason = value.reason == MeasurementReason::None ? MeasurementReason::NonFinite : value.reason;
            }
    }
    d.sourceLraLu = input;
    if (! report.lraLu) { d.lraReason = report.lraReason; return; }
    if (! input) { d.lraReason = inputReason; return; }
    d.lraChangeLu = *report.lraLu - *input;
    if (! (*input > 0.0)) { d.lraReason = MeasurementReason::NoSignal; return; }   // no range to lose a share of
    d.lraChangePercent = 100.0 * *d.lraChangeLu / *input;
    d.lraReason = MeasurementReason::None;
}

std::optional<double> MasterJob::stepFraction() const noexcept
{
    if (stage == Stage::Search)
    {
        const double walked = search.walkFraction();
        return walked >= 0.0 ? std::optional<double> (walked) : std::nullopt;
    }
    if (stage == Stage::Read)
    {
        const long long total = sourceRate != deliveryRate ? (long long) sourceFrames + impactDelay : (long long) frames;
        return total > 0 ? std::optional<double> (double (crestCursor) / double (total)) : std::nullopt;
    }
    if (stage == Stage::CostRead) return frames > 0 ? std::optional<double> (double (costCursor) / double (frames)) : std::nullopt;
    return std::nullopt;
}
} // namespace felitronics::session::detail
