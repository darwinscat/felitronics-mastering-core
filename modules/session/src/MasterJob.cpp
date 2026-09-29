// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "MasterJob.h"
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
    if (chainBytes == 0 || solverBytes == 0 || (convert && converterBytes == 0) || ! search.ok)
    { result.rejection = Rejection::OutOfDomain; return result; }
    const auto outputBytes = search.outputBytes;
    result.largestBlock = std::max ({ outputBytes, chainBytes, solverBytes, converterBytes,
        renderStorage.bytes(), search.workspaceBytes, std::uint64_t (sizeof (MasterJob)) });
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
        || ! add (total, 128u * 4096u))
    { result.rejection = Rejection::TooLong; return result; }
    result.bytes = total;
    result.largestBlock += 4096u;
    result.rejection = Rejection::None;
    return result;
}

bool MasterJob::begin (const Session& s, const MasterPlan& plan)
{
    channels = int (s.source_.channels);
    frames = plan.frames;
    deliveryRate = plan.deliveryRate;
    constexpr int block = 1024;
    if (! chain.prepare (double (deliveryRate), channels, plan.ready.topology)
        || ! renderer.prepare (channels, block)
        || ! solver.prepare (double (deliveryRate), channels, block,
                            plan.ready.topology.internalBlock, plan.ready.topology.oversampleFactor)) return false;
    const bool convert = deliveryRate != s.source_.sampleRate;
    if (convert && ! converter.prepare (double (s.source_.sampleRate), double (deliveryRate), channels, block)) return false;
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
} // namespace felitronics::session::detail
