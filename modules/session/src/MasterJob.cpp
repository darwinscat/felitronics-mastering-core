// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "MasterJob.h"
#include "MeasurementPlan.h"
#include <felitronics/analysis/BandCrestResult.h>
#include "Rules.h"
#include <felitronics/session/Session.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace felitronics::session::detail
{
namespace
{
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
        || int (p.clipper.shape) < 0 || int (p.clipper.shape) > 3
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
    h.f64 (p.limiter.overCeilingDb); h.f64 (p.limiter.kneeDb);
    h.i32 (p.dither.bits); h.i32 (int (p.dither.shaping)); h.u64 (p.dither.seed);
    h.flag (p.dither.autoBlank); h.i32 (p.dither.autoBlankSamples);
    h.flag (p.bypassEq); h.flag (p.bypassMonoBass); h.flag (p.bypassCompressor);
    h.flag (p.bypassClipper); h.flag (p.bypassLimiter); h.flag (p.bypassDither);
    h.f64 (p.compressorMix);
    return h.value;
}

MasterPlan MasterJob::plan (const Session& s, const command::Master& input) noexcept
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
    const auto target = ruleset.row (s.project_.target);
    const double targetLufs = s.project_.targetEdit.lufs.value_or (target.lufs.toDouble());
    const double targetTp = s.project_.targetEdit.tp.value_or (target.tp.toDouble());
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
    if (! validParams (result.ready.params))
    { result.rejection = Rejection::NotFinite; return result; }
    if (input.ready.deliveryBits != 0 && input.ready.deliveryBits != 16
        && input.ready.deliveryBits != 20 && input.ready.deliveryBits != 24
        && input.ready.deliveryBits != 32)
    { result.rejection = Rejection::OutOfDomain; return result; }
    result.deliveryRate = input.ready.deliveryRateHz == 0 ? s.source_.sampleRate : input.ready.deliveryRateHz;
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
    result.traceBuckets = std::min (result.frames, mastering::GainReductionTrace::kDefaultBuckets);
    result.request.targetLufs = targetLufs;
    result.sourceLufs = sourceLufs;
    result.request.maxTruePeakDbTp = targetTp;
    result.request.toleranceLu = tolerance;
    result.request.truePeakAimDb = peakAim;
    result.request.maxPasses = 12;
    result.request.normalizationGainDb = normalization;
    result.request.initialGainDb = targetLufs - reference;
    result.request.productLanding = true;
    result.request.grTraceBuckets = result.traceBuckets;
    result.ready.params.limiter.ceilingDbTp = targetTp - margin;
    if (input.ready.deliveryBits != 0) result.ready.params.dither.bits = input.ready.deliveryBits;
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
    const std::uint64_t crestRows = std::uint64_t (result.crestCapacity) * 15u * sizeof (double);
    const auto impactChainBytes = convert ? mastering::MasteringChain::prepareBytes (
        double (s.source_.sampleRate), int (s.source_.channels), result.ready.topology) : 0u;
    const auto impactScratchBytes = convert ? std::uint64_t (s.source_.channels) * 1024u * sizeof (float) : 0u;
    if (chainBytes == 0 || solverBytes == 0 || (convert && converterBytes == 0) || ! search.ok)
    { result.rejection = Rejection::OutOfDomain; return result; }
    const auto outputBytes = search.outputBytes;
    result.largestBlock = std::max ({ outputBytes, chainBytes, solverBytes, converterBytes,
        renderStorage.bytes(), search.workspaceBytes, std::uint64_t (sizeof (MasterJob)),
        crestRows, crest.ok ? crest.bytes() : 0u, impactChainBytes, impactScratchBytes });
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
        || ! add (total, 12u * sizeof (LandingPass))
        || ! add (total, 2u * std::uint64_t (result.traceBuckets) * sizeof (LandingTraceBucket))
        || ! add (total, crestRows) || ! add (total, crest.ok ? crest.firstBytes() : 0u)
        || ! add (total, impactChainBytes) || ! add (total, impactScratchBytes)
        || ! add (total, 128u * 4096u))
    { result.rejection = Rejection::TooLong; return result; }
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
    sourceLufs = plan.sourceLufs;
    targetLufs = plan.request.targetLufs;
    targetTp = plan.request.maxTruePeakDbTp;
    channels = int (s.source_.channels);
    frames = plan.frames;
    deliveryRate = plan.deliveryRate;
    sourceRate = s.source_.sampleRate;
    sourceFrames = s.source_.frames;
    constexpr int block = 1024;
    if (! chain.prepare (double (deliveryRate), channels, plan.ready.topology)
        || ! renderer.prepare (channels, block)
        || ! solver.prepare (double (deliveryRate), channels, block,
                            plan.ready.topology.internalBlock, plan.ready.topology.oversampleFactor)) return false;
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
        const auto& solved = search.result();
        report.targetLufs = targetLufs;
        report.ceilingDbTp = targetTp;
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
            report.peakSafe = solved.measured.truePeakDbTp <= targetTp
                && solved.measured.droppedBlocks == 0 && solved.measured.nonFiniteSubHops == 0;
            report.deliverable = solved.deliverable && report.peakSafe;
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
        if (! report.targetMet && report.missLu && std::fabs (*report.missLu) > 0.0)
        {
            report.firstHint = hint (solved.mainReason);
            report.secondHint = hint (solved.secondReason);
            if (! report.firstHint)
                report.firstHint = MasterHint { LandingReason::LoudnessDemand, std::fabs (*report.missLu), false };
        }
        if (result == StepResult::Failed || ! solved.deliverable)
        {
            report.crest.status = MeasurementStatus::Unavailable;
            report.crest.reason = MeasurementReason::NotImplemented;
            stage = Stage::Done;
            return result;
        }
        stage = Stage::Prepare;
        return StepResult::More;
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
        { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::TooShort; stage = Stage::Done; return StepResult::Done; }
        if (impact)
        {
            if (! chain.prepare (double (sourceRate), channels, ready.topology))
            { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::Unsupported; stage = Stage::Done; return StepResult::Done; }
            auto winning = ready.params;
            winning.preLimiterGainDb = search.result().preLimiterGainDb;
            winning.limiter.ceilingDbTp = search.result().ceilingDbTp;
            chain.setParams (winning);
            chain.reset();
            impactDelay = chain.latencySamples();
        }
        crest.setParams (crestParams);
        if (! crest.prepare (double (rate), channels, (long long) c.frames))
        { c.status = MeasurementStatus::Unavailable; c.reason = MeasurementReason::Unsupported; stage = Stage::Done; return StepResult::Done; }
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
            if (! chain.process (planes, channels, n)) { stage = Stage::Failed; return StepResult::Failed; }
            const int skip = int (std::min<long long> (n, std::max (0LL, impactDelay - crestCursor)));
            const int kept = int (std::max (0LL, std::min<long long> (n, total - crestCursor) - skip));
            const float* produced[2] { planes[0] + skip, channels == 2 ? planes[1] + skip : nullptr };
            if (kept != 0 && ! crest.process (produced, channels, kept))
            { stage = Stage::Failed; return StepResult::Failed; }
            crestCursor += n;
            if (crestCursor == total) { report.checkPasses = 1; stage = Stage::Finish; }
        }
        else
        {
            const int n = int (std::min<long long> ({ budget, block, frames - crestCursor }));
            const float* planes[2] { outputPlanes[0] + crestCursor,
                channels == 2 ? outputPlanes[1] + crestCursor : nullptr };
            if (! crest.process (planes, channels, n)) { stage = Stage::Failed; return StepResult::Failed; }
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
            stage = Stage::Done; return StepResult::Done;
        }
        const auto& source = session->measurementResults_[std::size_t (Analyzer::Crest)];
        if (source.status == MeasurementStatus::Pending)
        { c.status = MeasurementStatus::Pending; c.reason = MeasurementReason::Pending; }
        else if (source.status != MeasurementStatus::Ready)
        { c.status = MeasurementStatus::Unavailable; c.reason = source.reason; }
        else
        {
            const auto view = MeasurementCrest::view (source);
            const bool compatible = MasterCrestGrid::compatible (c, view);
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
            stage = Stage::Done;
            return StepResult::Done;
        }
        return StepResult::More;
    }
    return StepResult::Failed;
}
} // namespace felitronics::session::detail
