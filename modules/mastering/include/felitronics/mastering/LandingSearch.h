// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/mastering/LoudnessSolver.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <utility>

namespace felitronics::mastering
{

// One landing owns one solver while it is active. The caller owns source and output
// planar buffers. A saved candidate is restored directly when available; allocation failure falls back to the
// counted delivery render used before v0.18.
//
// LOUDNESS IS A REQUEST, NOT AN ORDER (owner, 04.10): better to fall short of the target than to reach it and make the
// master unlistenable. Two parts of the request say so.
//   * THE LEVEL LANDED (LoudnessRequest::landingOnSourceGate): the louder of the master's level over the blocks the
//     SOURCE's gate admitted and its own BS.1770 reading — the quiet parts the drive lifts do not dilute the level and
//     drive the loud part past what it needs, and a file never reads louder than the level landed. The source's readings
//     are matched to the master's blocks by time, through both grids.
//   * THE LIMITER'S BUDGET (LoudnessRequest::limiterGr, read on the limiter's ACTIVE windows): a render whose statistic is
//     above it is no candidate. The lowest drive measured to break it is the limit; the next drive is held under it, by
//     the secant of the excess between the loudest render that kept the budget and that one. The landing is
//     TargetUnreachable with LimiterGainReduction named only on proof: the delivered render's drive stands within
//     `LoudnessRequest::budgetResolutionDb` under the lowest drive measured over the budget while the target is still
//     above; with no render
//     that kept it, the gentlest ceiling-safe one is delivered, said the same (`overBudgetDb`). Otherwise the status is
//     the search's own.
class LandingSearch final
{
public:
    explicit LandingSearch (TargetLoudnessSolver& solver) noexcept : solver_ (solver) {}
    LandingSearch (const LandingSearch&) = delete;
    LandingSearch& operator= (const LandingSearch&) = delete;
    ~LandingSearch() noexcept { releaseDelivery(); }

    struct Storage
    {
        bool ok = false;
        std::uint64_t sourceBytes = 0, outputBytes = 0, candidateBytes = 0, workspaceBytes = 0;
        std::uint64_t maxLiveBytes = 0, largestBlockBytes = 0;
    };

    // A conservative fresh-call declaration of the workspace. The programme declaration below also names the
    // best-pass PCM buffer which lets delivery avoid a counted re-render; failure to obtain that optional buffer is
    // harmless and falls back to the counted render. It includes both retained result workspaces, armed band
    // distributions, and MSVC Debug container proxies.
    // The chain, renderer, converter and solver's prepare() storage are separate preparations.
    static std::uint64_t storageFor (double sampleRate, int channels, int frames,
                                     int traceBuckets, int armedPairs = kBandGrStride,
                                     double binDb = 0.01) noexcept
    {
        return TargetLoudnessSolver::productSearchCallBytes (
            sampleRate, channels, frames, traceBuckets, armedPairs, binDb);
    }

    [[nodiscard]] static Storage storageForProgramme (long long sourceFrames, int sourceChannels,
        double deliveryRate, int deliveryFrames, int traceBuckets,
        int armedPairs = kBandGrStride, double binDb = 0.01) noexcept
    {
        Storage st;
        if (sourceFrames <= 0 || sourceChannels < 1 || sourceChannels > core::kMaxChannels
            || deliveryFrames <= 0
            || (std::uint64_t) sourceFrames > std::numeric_limits<std::uint64_t>::max()
                                              / ((std::uint64_t) sourceChannels * sizeof (float))) return st;
        st.sourceBytes = (std::uint64_t) sourceFrames * (std::uint64_t) sourceChannels * sizeof (float);
        st.outputBytes = (std::uint64_t) deliveryFrames * (std::uint64_t) sourceChannels * sizeof (float);
        st.candidateBytes = st.outputBytes;
        const std::uint64_t callBytes = storageFor (deliveryRate, sourceChannels, deliveryFrames,
                                                    traceBuckets, armedPairs, binDb);
        if (callBytes <= st.candidateBytes) return st;
        st.workspaceBytes = callBytes - st.candidateBytes;
        constexpr auto max = std::numeric_limits<std::uint64_t>::max();
        if (st.sourceBytes > max - st.outputBytes
            || st.sourceBytes + st.outputBytes > max - st.candidateBytes
            || st.sourceBytes + st.outputBytes + st.candidateBytes > max - st.workspaceBytes) return Storage {};
        st.maxLiveBytes = st.sourceBytes + st.outputBytes + st.candidateBytes + st.workspaceBytes;
        // The workspace figure is a conservative bound on any one of its
        // components, including MSVC Debug proxy allocations.
        st.largestBlockBytes = std::max ({ st.sourceBytes, st.outputBytes, st.candidateBytes, st.workspaceBytes });
        st.ok = true;
        return st;
    }

    [[nodiscard]] Storage storageForJob (long long sourceFrames, int sourceChannels,
        double deliveryRate, int deliveryFrames, int traceBuckets,
        int armedPairs = kBandGrStride, double binDb = 0.01) const noexcept
    {
        Storage st = storageForProgramme (sourceFrames, sourceChannels, deliveryRate,
                                          deliveryFrames, traceBuckets, armedPairs,
                                          std::min (binDb, solver_.compHist_.binWidth()));
        if (st.ok)
        {
            st.sourceBytes = std::max (st.sourceBytes, retainedSourceUpper_);
            st.outputBytes = std::max (st.outputBytes, retainedOutputUpper_);
            st.candidateBytes = std::max (st.candidateBytes, retainedOutputUpper_);
            constexpr auto max = std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t pcm = st.sourceBytes > max - st.outputBytes
                || st.sourceBytes + st.outputBytes > max - st.candidateBytes
                ? max : st.sourceBytes + st.outputBytes + st.candidateBytes;
            st.maxLiveBytes = pcm > max - st.workspaceBytes ? max : pcm + st.workspaceBytes;
            st.maxLiveBytes = retainedWorkspaceUpper_ > max - st.maxLiveBytes
                ? max : st.maxLiveBytes + retainedWorkspaceUpper_;
            st.largestBlockBytes = std::max ({ st.largestBlockBytes, st.sourceBytes, st.outputBytes,
                                               st.candidateBytes });
            st.largestBlockBytes = std::max (st.largestBlockBytes, retainedLargestBlockUpper_);
        }
        return st;
    }

    // `sourceRate` is the grid of `source`; `frames` is the delivered length.
    [[nodiscard]] bool begin (MasteringChain& chain, OfflineRenderer& renderer,
                              const MasteringChainParams& params, const float* const* source,
                              long long sourceFrames, double sourceRate, float* const* out,
                              int channels, int frames, LoudnessRequest request,
                              const ProgressCallback& progress = {}, DeliveryConverter* converter = nullptr)
    {
        if (phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Failed) return false;
        MasteringSolveStatus why = MasteringSolveStatus::InvalidRequest;
        if (! preflight (solver_, chain, renderer, params, source, sourceFrames, sourceRate,
                         out, channels, frames, request, converter, why))
        {
            best_ = LoudnessSolution {};
            working_ = LoudnessSolution {};
            best_.status = why;
            phase_ = Phase::Failed;
            releaseDelivery();
            return false;
        }
        best_ = LoudnessSolution {};
        working_ = LoudnessSolution {};
        const Storage demand = storageForProgramme (sourceFrames, channels, chain.sampleRate(), frames,
                                                     request.grTraceBuckets, kBandGrStride,
                                                     solver_.compHist_.binWidth());
        retainedSourceUpper_ = std::max (retainedSourceUpper_, demand.sourceBytes);
        retainedOutputUpper_ = std::max (retainedOutputUpper_, demand.outputBytes);
        retainedRateFloor_ = retainedRateFloor_ <= 0.0 ? chain.sampleRate()
            : std::min (retainedRateFloor_, chain.sampleRate());
        retainedFramesUpper_ = std::max (retainedFramesUpper_, frames);
        retainedChannelsUpper_ = std::max (retainedChannelsUpper_, channels);
        retainedBucketsUpper_ = std::max (retainedBucketsUpper_, request.grTraceBuckets);
        const std::uint64_t retainedCall = storageFor (retainedRateFloor_, retainedChannelsUpper_,
            retainedFramesUpper_, retainedBucketsUpper_, kBandGrStride,
            solver_.compHist_.binWidth());
        const std::uint64_t retainedCandidate = (std::uint64_t) retainedFramesUpper_
            * (std::uint64_t) retainedChannelsUpper_ * sizeof (float);
        retainedWorkspaceUpper_ = retainedCall > retainedCandidate ? retainedCall - retainedCandidate : 0u;
        retainedLargestBlockUpper_ = std::max (retainedLargestBlockUpper_, demand.largestBlockBytes);
        const std::size_t candidateSamples = (std::size_t) channels * (std::size_t) frames;
        if (candidateSamples > savedCapacity_)
        {
            saved_.reset(); savedCapacity_ = 0;
            std::unique_ptr<float[]> fresh (new (std::nothrow) float[candidateSamples]);
            if (fresh) { saved_ = std::move (fresh); savedCapacity_ = candidateSamples; }
        }
        chain_ = &chain; renderer_ = &renderer; source_ = source; out_ = out;
        channels_ = channels; frames_ = frames; sourceFrames_ = sourceFrames; sourceRate_ = sourceRate;
        params_ = params; params_.inputGainDb += request.normalizationGainDb;
        initialParams_ = params_;
        request_ = request; clock_ = ProgressClock (progress);
        working_.activityThresholdDb = best_.activityThresholdDb = request.activityThresholdDb;
        haveBest_ = havePrevious_ = haveBelow_ = haveAbove_ = haveUnsafe_ = peakProbe_ = false;
        bestClipperPeaksValid_ = false;
        passes_ = 0; sourceCursor_ = 0; verifyCursor_ = 0; bestPass_ = 0; restoring_ = false;
        nextReason_ = SolvePassRecord::Reason::AimAtTarget;
        peakProbe_ = request.peakClipMeasured;
        gateOn_ = request.landingOnSourceGate && gateGrid (request, sourceRate, chain.sampleRate());
        budgetOn_ = ! request.limiterGr.off();
        haveOverBudget_ = false;
        wall_ = false; wallSlope_ = std::numeric_limits<double>::quiet_NaN();
        records_ = {};
        scanCursor_ = scanCount_ = 0; scanSum_ = gateThreshold_ = level_ = bestLevel_ = 0.0;
        gateLevel_ = bestGate_ = std::numeric_limits<double>::quiet_NaN();
        budgetExcess_.fill (std::numeric_limits<double>::quiet_NaN());
        excerptDrive_.fill (std::numeric_limits<double>::quiet_NaN());
        excerptExcess_.fill (std::numeric_limits<double>::quiet_NaN());
        excerptCount_ = 0; excerptLow_ = excerptHigh_ = -1; excerptReady_ = false;
        excerptEdgeDrive_ = std::numeric_limits<double>::quiet_NaN();
        chordOverDrive_ = std::numeric_limits<double>::quiet_NaN(); chordStale_ = 0;
        work_ = 0; bestError_ = std::numeric_limits<double>::infinity();
        lowState_.fill (0.0); low2State_.fill (0.0); low8State_.fill (0.0);
        totalEnergy_ = bassEnergy_ = presenceEnergy_ = sourcePeak_ = 0.0;
        const double twoPi = 6.2831853071795864769;
        const auto coefficient = [twoPi, sourceRate] (double hz) noexcept
            { const double a = twoPi * hz / sourceRate; return a / (1.0 + a); };
        lowK_ = coefficient (100.0); low2K_ = coefficient (2000.0); low8K_ = coefficient (8000.0);
        const double start = std::isfinite (request.initialGainDb) ? request.initialGainDb : request.targetLufs + 18.0;
        gain_ = std::clamp (start, -TargetLoudnessSolver::kMaxGainDb, TargetLoudnessSolver::kMaxGainDb);
        const double initialCeiling = std::isfinite (params.limiter.ceilingDbTp)
            ? params.limiter.ceilingDbTp : request.maxTruePeakDbTp;
        ceiling_ = std::clamp (std::fmin (initialCeiling, request.maxTruePeakDbTp - request.ceilingMarginDb),
                              -TargetLoudnessSolver::kMaxGainDb, TargetLoudnessSolver::kMaxGainDb);
        converter_ = converter;
        if (converter_ != nullptr)
        {
            solver_.deliveryConverter_ = converter_; solver_.deliverySource_ = source_;
            solver_.deliveryFrames_ = sourceFrames_;
        }
        excerptReady_ = budgetOn_ && request_.budgetAimsAtCrossing && request_.maxExcerptSearch
            && selectExcerpt();
        phase_ = Phase::SourceStats;
        return true;
    }

    [[nodiscard]] static bool preflight (const TargetLoudnessSolver& solver,
                                  const MasteringChain& chain, const OfflineRenderer& renderer,
                                  const MasteringChainParams& params, const float* const* source,
                                  long long sourceFrames, double sourceRate, float* const* out,
                                  int channels, int frames, const LoudnessRequest& request,
                                  const DeliveryConverter* converter, MasteringSolveStatus& why) noexcept
    {
        why = MasteringSolveStatus::InvalidRequest;
        if (request.maxPasses < 1 || request.maxPasses > 12
            || ! std::isfinite (request.ceilingMarginDb) || request.ceilingMarginDb < 0.0
            || request.ceilingMarginDb > 60.0
            || ! std::isfinite (request.normalizationGainDb) || std::fabs (request.normalizationGainDb) > 60.0
            || ! std::isfinite (params.inputGainDb)
            || std::fabs (params.inputGainDb + request.normalizationGainDb) > 60.0
            || ! solver.admits (chain, renderer, channels, frames, request, why)
            || sourceFrames <= 0 || ! std::isfinite (sourceRate) || sourceRate <= 0.0
            || (channels > 0 && (std::uint64_t) sourceFrames >
                std::numeric_limits<std::size_t>::max() / ((std::size_t) channels * sizeof (float)))
            || sourceFrames > LLONG_MAX - 2LL * frames - chain.latencySamples() - kBandGrStride - 6
            || (converter == nullptr && (sourceFrames != frames || ! core::exactlyEqual (sourceRate, chain.sampleRate())))
            || (converter != nullptr && (! converter->isPrepared()
                || ! core::exactlyEqual (converter->sourceRate(), sourceRate)
                || ! core::exactlyEqual (converter->deliveryRate(), chain.sampleRate())
                || DeliveryConverter::deliveredFrames (sourceRate, chain.sampleRate(), sourceFrames) != frames))
            || ! storageForProgramme (sourceFrames, channels, chain.sampleRate(), frames,
                                      request.grTraceBuckets, kBandGrStride,
                                      solver.compHist_.binWidth()).ok
            || ! planesUsable (source, out, channels, sourceFrames, frames)
            || (request.landingOnSourceGate
                && (request.sourceMomentaryLufs == nullptr || request.sourceMomentaryCount <= 0
                    || request.sourceMomentaryHopFrames <= 0))
            || (! request.limiterGr.off() && ! (request.limiterGr.limitDb >= 0.0 && std::isfinite (request.limiterGr.limitDb)))
            || request.limiterStatisticSkipFrames < 0
            || ! std::isfinite (request.budgetResolutionDb)
            || request.budgetResolutionDb < 0.05 || request.budgetResolutionDb > 1.0
            || (request.maxExcerptSearch && (! std::isfinite (request.maxExcerptSeconds)
                || ! std::isfinite (request.maxExcerptPreRollSeconds)
                || ! std::isfinite (request.maxExcerptPercentile)
                || ! std::isfinite (request.maxExcerptToleranceDb)
                || ! std::isfinite (request.maxExcerptOffsetDb)
                || request.maxExcerptSeconds <= 0.0 || request.maxExcerptPreRollSeconds < 0.0
                || request.maxExcerptPercentile < 0.0 || request.maxExcerptPercentile > 100.0
                || request.maxExcerptToleranceDb <= 0.0 || request.maxExcerptToleranceDb > 6.0
                || std::fabs (request.maxExcerptOffsetDb) > 24.0))
            || (! std::isnan (request.clipperLoudShare)
                && (! std::isfinite (request.clipperLoudShare)
                    || request.clipperLoudShare <= 0.0 || request.clipperLoudShare > 1.0)))
        {
            if (why == MasteringSolveStatus::Solved) why = MasteringSolveStatus::InvalidRequest;
            return false;
        }
        return true;
    }

    // A zero budget is inert. A positive call performs at most one render, so the caller can cancel
    // after any stage. A unit is a source, render, meter or gate item; setup and finish cost one.
    [[nodiscard]] StepResult step (long long budget)
    {
        if (phase_ == Phase::Done) return StepResult::Done;
        if (phase_ == Phase::Failed || phase_ == Phase::Idle || budget < 0) return StepResult::Failed;
        if (budget == 0) return StepResult::More;
        if (phase_ == Phase::SourceStats)
        {
            const long long n = std::min (budget, sourceFrames_ - sourceCursor_);
            for (long long i = 0; i < n; ++i)
                for (int c = 0; c < channels_; ++c)
                {
                    double x = (double) source_[c][sourceCursor_ + i];
                    if (! std::isfinite (x)) x = 0.0;
                    x = std::clamp (x, -1000000.0, 1000000.0);
                    sourcePeak_ = std::fmax (sourcePeak_, std::fabs (x));
                    lowState_[(std::size_t) c] = core::det::mulAdd (lowK_, x - lowState_[(std::size_t) c], lowState_[(std::size_t) c]);
                    low2State_[(std::size_t) c] = core::det::mulAdd (low2K_, x - low2State_[(std::size_t) c], low2State_[(std::size_t) c]);
                    low8State_[(std::size_t) c] = core::det::mulAdd (low8K_, x - low8State_[(std::size_t) c], low8State_[(std::size_t) c]);
                    const double band = low8State_[(std::size_t) c] - low2State_[(std::size_t) c];
                    totalEnergy_ += x * x;
                    bassEnergy_ += lowState_[(std::size_t) c] * lowState_[(std::size_t) c];
                    presenceEnergy_ += band * band;
                }
            sourceCursor_ += n; work_ += (std::uint64_t) n;
            if (sourceCursor_ == sourceFrames_)
                phase_ = gateOn_ ? Phase::SourceGate : excerptReady_ ? Phase::ExcerptBegin : Phase::PassBegin;
            return StepResult::More;
        }
        if (phase_ == Phase::SourceGate)
        {
            // THE SOURCE'S GATE, once per landing: BS.1770's absolute gate, then its relative gate 10 LU under the mean
            // energy of the blocks above the absolute one. A source with no block above the absolute gate has no gate to
            // land on: the landing lands the master's own.
            const double absolute = energyOf (kAbsoluteGateLufs);
            const long long n = std::min (budget, request_.sourceMomentaryCount - scanCursor_);
            for (long long i = 0; i < n; ++i, ++scanCursor_)
            {
                const double v = scanCursor_ >= kFirstBlockReading ? request_.sourceMomentaryLufs[scanCursor_]
                                                                   : std::numeric_limits<double>::quiet_NaN();
                const double e = std::isfinite (v) ? energyOf (v) : 0.0;
                if (e > absolute) { scanSum_ += e; ++scanCount_; }
            }
            work_ += (std::uint64_t) n;
            if (! clock_.checkpoint()) return fail (MasteringSolveStatus::Cancelled);
            if (scanCursor_ < request_.sourceMomentaryCount) return StepResult::More;
            gateOn_ = scanCount_ > 0;
            gateThreshold_ = gateOn_ ? 0.1 * (scanSum_ / (double) scanCount_) : 0.0;
            phase_ = excerptReady_ ? Phase::ExcerptBegin : Phase::PassBegin;
            return StepResult::More;
        }
        if (phase_ == Phase::ExcerptBegin)
        {
            if (excerptCount_ >= kMaxExcerptPasses) { fallBackFromExcerpt(); return StepResult::More; }
            if (! clock_.begin (ProgressStage::SearchPass, 0, 0,
                                (converter_ == nullptr ? 0LL : excerptSourceFrames_) + 2LL * excerptFrames_
                                + chain_->latencySamples() + kBandGrStride + 6, excerptFrames_))
                return fail (MasteringSolveStatus::Cancelled);
            params_.preLimiterGainDb = gain_; params_.limiter.ceilingDbTp = ceiling_;
            chain_->setParams (params_);
            measurement_ = MasterMeasurement {};
            excerptRequest_ = request_;
            excerptRequest_.landingOnSourceGate = false;
            excerptRequest_.limiterStatisticSkipFrames = excerptSkipFrames_;
            if (converter_ != nullptr)
            {
                solver_.deliverySource_ = excerptSource_;
                solver_.deliveryFrames_ = excerptSourceFrames_;
            }
            if (! solver_.beginPass (*chain_, *renderer_, params_, excerptSource_, out_, channels_, excerptFrames_,
                                     excerptRequest_, measurement_, working_, clock_, true))
            { fallBackFromExcerpt(); return StepResult::More; }
            phase_ = Phase::ExcerptRun;
            --budget; ++work_;
            if (budget == 0) return StepResult::More;
        }
        if (phase_ == Phase::ExcerptRun)
        {
            const long long before = solver_.pass_->work;
            const StepResult r = solver_.stepPass (budget);
            work_ += (std::uint64_t) std::max (0LL, solver_.pass_->work - before);
            if (r == StepResult::Failed) { fallBackFromExcerpt(); return StepResult::More; }
            if (r == StepResult::More) return r;
            return completedExcerpt();
        }
        if (phase_ == Phase::PassBegin)
        {
            if (! clock_.begin (ProgressStage::SearchPass, passes_ + 1, request_.maxPasses,
                                (converter_ == nullptr ? 0LL : sourceFrames_) + 2LL * frames_
                                + chain_->latencySamples() + kBandGrStride + 6, frames_))
                return fail (MasteringSolveStatus::Cancelled);
            params_.preLimiterGainDb = gain_; params_.limiter.ceilingDbTp = ceiling_;
            chain_->setParams (params_);
            measurement_ = MasterMeasurement {};
            if (! solver_.beginPass (*chain_, *renderer_, params_, source_, out_, channels_, frames_,
                                     request_, measurement_, working_, clock_, true))
                return fail (MasteringSolveStatus::RenderFailed);
            SolvePassRecord& rec = records_[(std::size_t) passes_];
            rec = SolvePassRecord {};
            rec.gainDb = gain_; rec.ceilingDb = ceiling_;
            rec.reason = restoring_ ? SolvePassRecord::Reason::DeliverWinner
                                    : (! peakProbe_ && params_.limiter.peakClip
                                       ? SolvePassRecord::Reason::PeakProbe : nextReason_);
            rec.violated = constraintBit (MasteringConstraint::TruePeakCeiling); // incomplete passes are never certified
            ++passes_;
            phase_ = Phase::PassRun;
            --budget; ++work_;
            if (budget == 0) return StepResult::More;
        }
        if (phase_ == Phase::PassRun)
        {
            const long long before = solver_.pass_->work;
            const StepResult r = solver_.stepPass (budget);
            work_ += (std::uint64_t) std::max (0LL, solver_.pass_->work - before);
            if (r == StepResult::Failed)
                return fail (clock_.stopped() ? MasteringSolveStatus::Cancelled : MasteringSolveStatus::RenderFailed);
            if (r == StepResult::More) return r;
            if (! gateOn_ || restoring_) { gateLevel_ = std::numeric_limits<double>::quiet_NaN(); return completedPass(); }
            scanCursor_ = scanCount_ = 0; scanSum_ = 0.0;
            phase_ = Phase::PassGate;
            return StepResult::More;
        }
        if (phase_ == Phase::PassGate)
        {
            // THIS PASS'S LEVEL ON THE SOURCE'S GATE: the mean energy of its gating blocks whose time the source's gate
            // admitted, not gated again. No such block with energy — the chain took all of it out — is no level here.
            const auto blocks = solver_.pass_->lm.gatingBlockEnergies();
            const long long total = (long long) blocks.size();
            const long long n = std::min (budget, total - scanCursor_);
            for (long long i = 0; i < n; ++i, ++scanCursor_)
                if (std::isfinite (blocks[(std::size_t) scanCursor_]) && inSourceGate (readingOf (scanCursor_)))
                { scanSum_ += blocks[(std::size_t) scanCursor_]; ++scanCount_; }
            work_ += (std::uint64_t) n;
            if (! clock_.checkpoint()) return fail (MasteringSolveStatus::Cancelled);
            if (scanCursor_ < total) return StepResult::More;
            gateLevel_ = scanCount_ > 0 && scanSum_ > 0.0 ? lufsOf (scanSum_ / (double) scanCount_)
                                                          : std::numeric_limits<double>::quiet_NaN();
            return completedPass();
        }
        if (phase_ == Phase::VerifySetup)
        {
            if (! solver_.pass_->lm.prepareForSamples (solver_.fs_, channels_,
                    TargetLoudnessSolver::meterSamples (frames_, solver_.fs_))
                || ! solver_.pass_->tm.prepare (solver_.fs_, frames_, channels_))
                return fail (MasteringSolveStatus::Unavailable);
            for (int c = 0; c < channels_; ++c)
                solver_.pass_->lm.setChannelWeight (c, solver_.weights_[c]);
            verifyCursor_ = 0; ++work_; phase_ = Phase::VerifyMeter;
            return StepResult::More;
        }
        if (phase_ == Phase::VerifyMeter)
        {
            const int n = (int) std::min<long long> (budget, frames_ - verifyCursor_);
            const float* planes[core::kMaxChannels] {};
            for (int c = 0; c < channels_; ++c) planes[c] = out_[c] + verifyCursor_;
            if (! solver_.pass_->lm.process (planes, channels_, n)
                || ! solver_.pass_->tm.process (planes, channels_, n))
                return fail (MasteringSolveStatus::Unavailable);
            verifyCursor_ += n; work_ += (std::uint64_t) n;
            if (! clock_.advance (n)) return fail (MasteringSolveStatus::Cancelled);
            if (verifyCursor_ < frames_) return StepResult::More;
            phase_ = Phase::VerifyDrain;
            return StepResult::More;
        }
        if (phase_ == Phase::VerifyDrain)
        {
            solver_.pass_->tm.drain(); ++work_;
            verifyTp_ = TargetLoudnessSolver::peakDb (solver_.pass_->tm.truePeakLinear());
            solver_.pass_->lm.beginIntegratedScan();
            phase_ = Phase::VerifyGate;
            if (! clock_.checkpoint()) return fail (MasteringSolveStatus::Cancelled);
            return StepResult::More;
        }
        if (phase_ == Phase::VerifyGate)
        {
            int used = 0;
            const bool done = solver_.pass_->lm.stepIntegratedScan (
                (int) std::min<long long> (budget, 256), verifyLufs_, used);
            work_ += (std::uint64_t) used;
            if (! clock_.checkpoint()) return fail (MasteringSolveStatus::Cancelled);
            if (! done) return StepResult::More;
            phase_ = Phase::VerifyFinish;
            return StepResult::More;
        }
        if (phase_ == Phase::VerifyFinish)
        {
            if (! core::exactlyEqual (verifyTp_, best_.measured.truePeakDbTp)
                || ! core::exactlyEqual (verifyLufs_, best_.measured.integratedLufs)
                || (! best_.peaksAboveCeiling && verifyTp_ > request_.maxTruePeakDbTp)
                || solver_.pass_->lm.droppedBlocks() != 0
                || solver_.pass_->lm.nonFiniteSubHops() != 0)
                return fail (MasteringSolveStatus::Unavailable);
            if (! clock_.finish()) return fail (MasteringSolveStatus::Cancelled);
            best_.deliverable = true;
            best_.achievedLufs = verifyLufs_;
            best_.missLu = verifyLufs_ - request_.targetLufs;
            best_.distanceLu = std::fabs (best_.missLu);
            best_.workUnits = work_;
            best_.sourceSubBassShare = totalEnergy_ > 0.0 ? bassEnergy_ / totalEnergy_ : 0.0;
            best_.sourcePresenceShare = totalEnergy_ > 0.0 ? presenceEnergy_ / totalEnergy_ : 0.0;
            best_.limiterMeanReductionDb = best_.measured.limiter.meanDb;
            best_.normalizationGainDb = request_.normalizationGainDb;
            classify();
            phase_ = Phase::Done;
            releaseDelivery();
            return StepResult::Done;
        }
        return StepResult::More;
    }

    void cancel() noexcept { (void) fail (MasteringSolveStatus::Cancelled); }
    const LoudnessSolution& result() const noexcept { return best_; }
    [[nodiscard]] bool clipperPeaks (ClipperPeaks& out) const noexcept
    {
        if (! bestClipperPeaksValid_) return false;
        out = bestClipperPeaks_;
        return true;
    }
    // The peak at the limiter's input, at a gain of 0, the delivered render's clipper was set from: the one the first
    // pass measured where the clipper rides it, else the caller's (MasteringChainParams::peakClipPeakDb).
    double peakClipPeakDb() const noexcept { return params_.peakClipPeakDb; }
    // A finished landing on the source's gate: the level landed of the delivered render — the louder of its level on that
    // gate and its BS.1770 reading. NaN on any other landing, whose level landed is the result's `achievedLufs`.
    double landedLufs() const noexcept
    {
        return phase_ == Phase::Done && gateOn_ ? bestLevel_ : std::numeric_limits<double>::quiet_NaN();
    }
    // ...and the delivered render's level on that gate alone, louder or quieter than its BS.1770 reading. NaN on any
    // other landing, and where the gate's blocks carried no energy.
    double gateLufs() const noexcept
    {
        return phase_ == Phase::Done && gateOn_ ? bestGate_ : std::numeric_limits<double>::quiet_NaN();
    }
    // A finished landing that no render kept within the limiter's budget: the delivered render's statistic, above the
    // budget. NaN on any other landing.
    double overBudgetDb() const noexcept
    {
        return phase_ == Phase::Done && haveOverBudget_ && ! haveBest_ ? budgetStatistic (best_)
                                                                       : std::numeric_limits<double>::quiet_NaN();
    }
    int startedPasses() const noexcept { return passes_; }
    // THE CURRENT WALK OVER THE FILE, 0..1, a new count for each: the source's statistics, every landing pass (its clock's
    // units), the delivered render's check. Negative outside a walk: idle, done, failed, and the bookkeeping between walks.
    double walkFraction() const noexcept
    {
        if (phase_ == Phase::SourceStats)
            return sourceFrames_ > 0 ? (double) sourceCursor_ / (double) sourceFrames_ : 0.0;
        if (phase_ == Phase::ExcerptRun || phase_ == Phase::PassRun || phase_ == Phase::VerifyMeter) return clock_.walked();
        return -1.0;
    }
    bool renderingExcerpt() const noexcept { return phase_ == Phase::ExcerptBegin || phase_ == Phase::ExcerptRun; }
    std::uint64_t excerptFromFrame() const noexcept
    { return excerptStatisticFrom_ > 0 ? (std::uint64_t) excerptStatisticFrom_ : 0u; }
    std::uint64_t excerptFrames() const noexcept
    { return excerptStatisticFrames_ > 0 ? (std::uint64_t) excerptStatisticFrames_ : 0u; }
    std::uint64_t completedWork() const noexcept { return work_; }
    bool active() const noexcept { return phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Failed; }

private:
    enum class Phase { Idle, SourceStats, SourceGate, ExcerptBegin, ExcerptRun, PassBegin, PassRun, PassGate, VerifySetup,
                       VerifyMeter, VerifyDrain, VerifyGate, VerifyFinish, Done, Failed };

    // The source's momentary reading i is the 400 ms ending at (i + 1) of its hops: the first three readings are no block.
    static constexpr long long kFirstBlockReading = 3;
    static constexpr double kAbsoluteGateLufs = -70.0;
    static constexpr int kMaxExcerptPasses = 12;

    struct ExcerptWindow { double score = 0.0; long long from = 0; };
    static_assert (sizeof (ExcerptWindow) == 16u);   // TargetLoudnessSolver::productSearchCallBytes declares these rows

    static double energyOf (double lufs) noexcept { return core::det::pow10 ((lufs + 0.691) / 10.0); }
    static double lufsOf (double energy) noexcept { return -0.691 + 10.0 * core::det::log10 (energy); }

    // THE TWO GRIDS: the source's readings on its hop at its rate, the master's gating blocks on the meter's hop (ten
    // sub-hops of lround (0.01 fs) frames) at the chain's. Both hops are 100 ms only near enough — 1100 frames at 11025 Hz
    // are 99.77 ms — so block j, which starts at j·hopM / fsM s, takes the reading whose 400 ms starts nearest to it:
    // round (j · hopM·fsS / (hopS·fsM)) + 3, kept as the reduced fraction gateNum_ / gateDen_. Rates that are not whole
    // numbers of hertz have no such fraction: the landing lands the master's own gate.
    bool gateGrid (const LoudnessRequest& r, double sourceRate, double chainRate) noexcept
    {
        if (! (sourceRate >= 1.0 && sourceRate <= 1.0e7 && chainRate >= 1.0 && chainRate <= 1.0e7)
            || ! core::exactlyEqual (std::floor (sourceRate), sourceRate)
            || ! core::exactlyEqual (std::floor (chainRate), chainRate)
            || r.sourceMomentaryHopFrames <= 0 || r.sourceMomentaryHopFrames > 100000000LL) return false;
        const auto fsS = (std::uint64_t) sourceRate, fsM = (std::uint64_t) chainRate;
        const auto hopM = (std::uint64_t) (10 * std::max (1L, std::lround (0.01 * chainRate)));
        const std::uint64_t num = hopM * fsS, den = (std::uint64_t) r.sourceMomentaryHopFrames * fsM;
        const std::uint64_t g = std::gcd (num, den);
        gateNum_ = num / g; gateDen_ = den / g;
        return true;
    }

    // ...the nearest reading the series has: a block past its last reading in time takes the last one.
    long long readingOf (long long block) const noexcept
    {
        const auto j = (std::uint64_t) block;
        const long long reading = (long long) ((2u * j * gateNum_ + gateDen_) / (2u * gateDen_)) + kFirstBlockReading;
        return std::min (reading, request_.sourceMomentaryCount - 1);
    }

    bool inSourceGate (long long reading) const noexcept
    {
        const double v = request_.sourceMomentaryLufs[reading];
        if (! std::isfinite (v)) return false;
        const double e = energyOf (v);
        return e > energyOf (kAbsoluteGateLufs) && e > gateThreshold_;
    }

    bool selectExcerpt() noexcept
    {
        const double wantedFrames = request_.maxExcerptSeconds * sourceRate_;
        const double preRollFrames = request_.maxExcerptPreRollSeconds * sourceRate_;
        if (! std::isfinite (wantedFrames) || ! std::isfinite (preRollFrames)
            || wantedFrames >= (double) LLONG_MAX || preRollFrames >= (double) LLONG_MAX
            || ! core::exactlyEqual (std::floor (sourceRate_), sourceRate_)
            || request_.sourceMomentaryLufs == nullptr || request_.sourceMomentaryCount <= 0
            || request_.sourceMomentaryHopFrames <= 0 || sourceFrames_ <= 0) return false;
        const long long loudFrames = std::max (1LL, (long long) std::floor (wantedFrames + 0.5));
        const long long requestedPre = std::max (0LL, (long long) std::floor (preRollFrames + 0.5));
        const long long step = (long long) sourceRate_;   // chooseSegments: one candidate window per source second
        if (loudFrames > sourceFrames_ || requestedPre > sourceFrames_ - loudFrames || step <= 0) return false;
        const long long candidates = 1 + (sourceFrames_ - loudFrames - requestedPre) / step;
        if (candidates <= 0 || (std::uint64_t) candidates > std::numeric_limits<std::size_t>::max()
            || (std::size_t) candidates > std::numeric_limits<std::size_t>::max() / sizeof (ExcerptWindow)) return false;
        std::unique_ptr<ExcerptWindow[]> windows (new (std::nothrow) ExcerptWindow[(std::size_t) candidates]);
        if (! windows) return false;
        long long count = 0;
        for (long long candidate = 0; candidate < candidates; ++candidate)
        {
            const long long from = requestedPre + candidate * step;
            const long long to = from + loudFrames;
            double sum = 0.0;
            long long valid = 0;
            for (long long i = 0; i < request_.sourceMomentaryCount; ++i)
            {
                if (i >= LLONG_MAX / request_.sourceMomentaryHopFrames - 1) return false;
                const long long frame = (i + 1) * request_.sourceMomentaryHopFrames;
                if (frame < from) continue;
                if (frame >= to) break;
                const double lufs = request_.sourceMomentaryLufs[i];
                if (! std::isfinite (lufs)) continue;
                const double energy = energyOf (lufs);
                if (! std::isfinite (energy) || energy < 0.0 || sum > std::numeric_limits<double>::max() - energy)
                    return false;
                sum += energy; ++valid;
            }
            if (valid > 0)
            {
                const double score = sum / (double) valid;
                if (! std::isfinite (score)) return false;
                windows[(std::size_t) count++] = { score, from };
            }
        }
        if (count <= 0) return false;
        std::sort (windows.get(), windows.get() + count, [] (const ExcerptWindow& a, const ExcerptWindow& b) noexcept
        {
            return a.score < b.score || (core::exactlyEqual (a.score, b.score) && a.from < b.from);
        });
        const long long pick = (long long) std::floor ((request_.maxExcerptPercentile / 100.0) * (double) (count - 1));
        const long long loudFrom = windows[(std::size_t) std::clamp (pick, 0LL, count - 1)].from;
        const long long pre = std::min (loudFrom, requestedPre);
        excerptSourceFrom_ = loudFrom - pre;
        excerptStatisticFrom_ = loudFrom;
        excerptStatisticFrames_ = loudFrames;
        excerptSourceFrames_ = pre + loudFrames;
        const long long deliveredExcerpt = converter_ == nullptr ? excerptSourceFrames_
            : DeliveryConverter::deliveredFrames (sourceRate_, chain_->sampleRate(), excerptSourceFrames_);
        const long long deliveredPre = converter_ == nullptr ? pre
            : DeliveryConverter::deliveredFrames (sourceRate_, chain_->sampleRate(), pre);
        if (deliveredExcerpt <= 0 || deliveredExcerpt > frames_
            || deliveredExcerpt > std::numeric_limits<int>::max()
            || deliveredPre < 0 || deliveredPre > deliveredExcerpt) return false;
        excerptFrames_ = (int) deliveredExcerpt;
        excerptSkipFrames_ = deliveredPre;
        for (int c = 0; c < channels_; ++c) excerptSource_[c] = source_[c] + excerptSourceFrom_;
        return true;
    }

    StepResult completedExcerpt()
    {
        SolvePassRecord rec;
        rec.gainDb = gain_; rec.ceilingDb = ceiling_;
        rec.integratedLufs = measurement_.integratedLufs; rec.truePeakDbTp = measurement_.truePeakDbTp;
        rec.plrDb = measurement_.plrDb; rec.limiterMaxGrDb = measurement_.limiter.maxDb;
        rec.loudnessRangeLu = measurement_.loudnessRangeLu;
        rec.limiterP95Db = working_.limiterActive.stats.valid ? working_.limiterActive.stats.p95Db
            : std::numeric_limits<double>::quiet_NaN();
        rec.reason = SolvePassRecord::Reason::Excerpt;
        rec.excerpt = true; rec.excerptFromFrame = excerptStatisticFrom_; rec.excerptFrames = excerptStatisticFrames_;
        const double excess = rec.limiterP95Db - request_.limiterGr.limitDb;
        if (! std::isfinite (excess))
        {
            if (! clock_.finish (&rec)) return fail (MasteringSolveStatus::Cancelled);
            excerptRecords_[(std::size_t) excerptCount_++] = rec;
            fallBackFromExcerpt(); return StepResult::More;
        }
        if (excess > 0.0) rec.violated |= constraintBit (MasteringConstraint::LimiterGainReduction);
        if (! clock_.finish (&rec)) return fail (MasteringSolveStatus::Cancelled);
        const double drive = gain_ - ceiling_;
        excerptRecords_[(std::size_t) excerptCount_] = rec;
        excerptDrive_[(std::size_t) excerptCount_] = drive;
        excerptExcess_[(std::size_t) excerptCount_] = excess;
        const int here = excerptCount_++;
        if (excess > 0.0)
        {
            if (excerptHigh_ < 0 || drive < excerptDrive_[(std::size_t) excerptHigh_]) excerptHigh_ = here;
        }
        else if (excerptLow_ < 0 || drive > excerptDrive_[(std::size_t) excerptLow_]) excerptLow_ = here;
        if (excerptLow_ >= 0 && excerptHigh_ >= 0)
        {
            const double lo = excerptDrive_[(std::size_t) excerptLow_];
            const double hi = excerptDrive_[(std::size_t) excerptHigh_];
            if (! (lo < hi)) { fallBackFromExcerpt(); return StepResult::More; }
            const double edge = brentCandidate (lo, excerptExcess_[(std::size_t) excerptLow_],
                hi, excerptExcess_[(std::size_t) excerptHigh_], excerptDrive_, excerptExcess_, excerptCount_);
            if (hi - lo <= request_.maxExcerptToleranceDb)
            {
                restoreFullProgramme();
                excerptEdgeDrive_ = edge;
                gain_ = std::clamp (edge + request_.maxExcerptOffsetDb + ceiling_, -60.0, 60.0);
                nextReason_ = SolvePassRecord::Reason::InsideBracket;
                phase_ = Phase::PassBegin;
                return StepResult::More;
            }
            gain_ = std::clamp (edge + ceiling_, -60.0, 60.0);
        }
        else
        {
            const double next = drive + (excerptLow_ >= 0 ? 4.0 : -4.0);
            if (! std::isfinite (next) || next <= -60.0 - ceiling_ || next >= 60.0 - ceiling_)
            { fallBackFromExcerpt(); return StepResult::More; }
            gain_ = next + ceiling_;
        }
        phase_ = Phase::ExcerptBegin;
        return StepResult::More;
    }

    template <std::size_t N>
    static double brentCandidate (double lo, double flo, double hi, double fhi,
                                  const std::array<double, N>& drives,
                                  const std::array<double, N>& values, int count) noexcept
    {
        double candidate = lo - flo * (hi - lo) / (fhi - flo);
        int third = -1;
        for (int i = count - 1; i >= 0; --i)
            if (drives[(std::size_t) i] > lo && drives[(std::size_t) i] < hi
                && ! core::exactlyEqual (values[(std::size_t) i], flo)
                && ! core::exactlyEqual (values[(std::size_t) i], fhi)) { third = i; break; }
        if (third >= 0)
        {
            const double x = drives[(std::size_t) third], fx = values[(std::size_t) third];
            const double q = lo * fx * fhi / ((flo - fx) * (flo - fhi))
                           + x * flo * fhi / ((fx - flo) * (fx - fhi))
                           + hi * flo * fx / ((fhi - flo) * (fhi - fx));
            if (std::isfinite (q)) candidate = q;
        }
        const double margin = std::fmin (1.0e-6, 0.25 * (hi - lo));
        if (! std::isfinite (candidate) || candidate <= lo + margin || candidate >= hi - margin)
            candidate = 0.5 * (lo + hi);
        return std::clamp (candidate, lo + margin, hi - margin);
    }

    void restoreFullProgramme() noexcept
    {
        if (converter_ != nullptr)
        {
            solver_.deliverySource_ = source_;
            solver_.deliveryFrames_ = sourceFrames_;
        }
    }

    void fallBackFromExcerpt() noexcept
    {
        restoreFullProgramme();
        params_ = initialParams_;
        peakProbe_ = request_.peakClipMeasured;
        const double start = std::isfinite (request_.initialGainDb) ? request_.initialGainDb : request_.targetLufs + 18.0;
        gain_ = std::clamp (start, -TargetLoudnessSolver::kMaxGainDb, TargetLoudnessSolver::kMaxGainDb);
        excerptReady_ = false; phase_ = Phase::PassBegin;
    }

    StepResult completedPass()
    {
        SolvePassRecord& rec = records_[(std::size_t) (passes_ - 1)];
        // THE LEVEL LANDED: on the source's gate, never quieter than the file's own reading — an EQ that empties the
        // blocks the source's gate admitted would otherwise land a file far louder than the target.
        level_ = gateOn_ && std::isfinite (gateLevel_) && std::isfinite (measurement_.integratedLufs)
            ? std::fmax (gateLevel_, measurement_.integratedLufs) : measurement_.integratedLufs;
        rec.integratedLufs = measurement_.integratedLufs; rec.truePeakDbTp = measurement_.truePeakDbTp;
        rec.plrDb = measurement_.plrDb; rec.limiterMaxGrDb = measurement_.limiter.maxDb;
        rec.loudnessRangeLu = measurement_.loudnessRangeLu;
        rec.limiterP95Db = working_.limiterActive.stats.valid ? working_.limiterActive.stats.p95Db
            : std::numeric_limits<double>::quiet_NaN();
        rec.violated = measurement_.truePeakDbTp > request_.maxTruePeakDbTp
            ? constraintBit (MasteringConstraint::TruePeakCeiling) : 0u;
        if (! clock_.finish (&rec)) return fail (MasteringSolveStatus::Cancelled);
        if (restoring_)
        {
            if (! core::exactlyEqual (measurement_.integratedLufs, best_.measured.integratedLufs)
                || ! core::exactlyEqual (measurement_.truePeakDbTp, best_.measured.truePeakDbTp)
                || (haveBest_ && measurement_.truePeakDbTp > request_.maxTruePeakDbTp))
                return fail (MasteringSolveStatus::Unavailable);
            // The restored render is the candidate's own: its mark over the budget and its excess are the candidate's.
            const auto restored = (std::size_t) (bestPass_ - 1);
            rec.violated |= records_[restored].violated & constraintBit (MasteringConstraint::LimiterGainReduction);
            budgetExcess_[(std::size_t) (passes_ - 1)] = budgetExcess_[restored];
            restoring_ = false; bestPass_ = passes_;
            return finishSearch();
        }
        const bool valid = measurement_.loudnessValid && std::isfinite (measurement_.integratedLufs);
        if (! valid && measurement_.samplePeakDb <= -180.0) return fail (MasteringSolveStatus::Unavailable);
        // THE PEAK CLIPPER'S PEAK, MEASURED (MasteringChainParams::peakClipCutDb): the first pass renders with the
        // caller's forecast of the peak at the limiter's input; it measures the real one, on the limiter's own
        // oversampler, before the clip. The stages before the gain node do not depend on the gain, the ceiling or the
        // threshold, so that peak less this pass's gain is the peak at a gain of 0 for every later render, and the
        // clipper's threshold worked out from it takes no more than the cut off it. This pass cut by the forecast: it
        // steers the search like any other and is never a candidate, so no render with the forecast is delivered or
        // restored. A clipper landing that its first pass would have finished pays one render more.
        if (! peakProbe_ && params_.limiter.peakClip && std::isfinite (params_.peakClipCutDb)
            && std::isfinite (params_.peakClipPeakDb) && std::isfinite (measurement_.limiterMaxReconstructedPeakDb)
            && measurement_.limiterMaxReconstructedPeakDb > -180.0)
        {
            peakProbe_ = true;
            params_.peakClipPeakDb = measurement_.limiterMaxReconstructedPeakDb - params_.preLimiterGainDb;
            if (passes_ >= request_.maxPasses) return fail (MasteringSolveStatus::Unavailable);
            chooseNext (valid);
            phase_ = Phase::PassBegin;
            return StepResult::More;
        }
        const bool safe = valid && measurement_.truePeakDbTp <= request_.maxTruePeakDbTp;
        const double err = valid ? std::fabs (level_ - request_.targetLufs)
                                 : std::numeric_limits<double>::infinity();
        const bool keeps = ! budgetOn_ || ! valid || keepsBudget (rec);
        // The existing log supplies both readings. No probe is requested to find a slope: at least the configured P95
        // spacing, the nearest such pass, with both renders under the ceiling. A peak-probe render used the forecast
        // clip threshold rather than the calibrated curve and therefore cannot contribute either endpoint.
        if (safe && keeps && level_ < request_.targetLufs - request_.toleranceLu
            && request_.limiterSlopeBelow > 0 && ! request_.budgetAimsAtCrossing)
        {
            double spacing = std::numeric_limits<double>::infinity();
            for (int i = 0; i + 1 < passes_; ++i)
            {
                const auto& prior = records_[(std::size_t) i];
                const double cut = rec.limiterP95Db - prior.limiterP95Db;
                if (prior.reason == SolvePassRecord::Reason::PeakProbe
                    || ! std::isfinite (cut) || std::fabs (cut) < request_.limiterSlopeSpacingDb
                    || std::fabs (cut) >= spacing || prior.truePeakDbTp > request_.maxTruePeakDbTp
                    || ! std::isfinite (prior.integratedLufs)
                    || cut * (gain_ - ceiling_ - driveOf (i)) <= 0) continue;
                spacing = std::fabs (cut);
                wallSlope_ = (rec.integratedLufs - prior.integratedLufs) / cut;
            }
            wall_ = std::isfinite (spacing) && wallSlope_ > 0.0 && wallSlope_ < request_.limiterSlopeBelow;
        }
        // THE GENTLEST CEILING-SAFE RENDER OVER THE BUDGET, held in `best_` while no render keeps both: the least drive.
        if (budgetOn_ && ! haveBest_ && safe && ! keeps
            && (! haveOverBudget_ || gain_ - ceiling_ < bestGain_ - bestCeiling_))
        {
            working_.measured = measurement_;
            working_.preLimiterGainDb = gain_; working_.ceilingDbTp = ceiling_;
            std::swap (working_, best_);
            bestError_ = err; bestPass_ = passes_;
            bestGain_ = gain_; bestCeiling_ = ceiling_; bestLevel_ = level_; bestGate_ = gateLevel_;
            haveOverBudget_ = true;
            saveBestAudio();
        }
        // THE GENTLEST RENDER ABOVE THE CEILING, held in `best_` while no render is under it (owner, 01.10: the file is
        // delivered even then, marked): the smallest overshoot of the ceiling, the nearer loudness on an equal one. The
        // first ceiling-safe render replaces it below (`nearer` holds for any render while `haveBest_` is false).
        if (! haveBest_ && ! haveOverBudget_ && valid && ! safe
            && (! haveUnsafe_ || measurement_.truePeakDbTp < best_.measured.truePeakDbTp
                || (core::exactlyEqual (measurement_.truePeakDbTp, best_.measured.truePeakDbTp) && err < bestError_)))
        {
            working_.measured = measurement_;
            working_.preLimiterGainDb = gain_; working_.ceilingDbTp = ceiling_;
            std::swap (working_, best_);
            bestError_ = err; bestPass_ = passes_;
            bestGain_ = gain_; bestCeiling_ = ceiling_; bestLevel_ = level_; bestGate_ = gateLevel_;
            haveUnsafe_ = true;
            saveBestAudio();
        }
        const double level = level_;
        const double previousLevel = bestLevel_;
        const bool bothBelow = haveBest_ && level <= request_.targetLufs && previousLevel <= request_.targetLufs;
        const bool bothAbove = haveBest_ && level >= request_.targetLufs && previousLevel >= request_.targetLufs;
        const bool nearer = ! haveBest_ || (bothBelow && level > previousLevel)
                          || (bothAbove && level < previousLevel)
                          || (! bothBelow && ! bothAbove && err < bestError_);
        const bool sameDistance = haveBest_ && core::exactlyEqual (err, bestError_)
                               && core::exactlyEqual (level, previousLevel);
        if (safe && keeps && (wall_ || nearer || (sameDistance && measurement_.limiter.meanDb < best_.measured.limiter.meanDb)))
        {
            working_.measured = measurement_;
            working_.preLimiterGainDb = gain_; working_.ceilingDbTp = ceiling_;
            std::swap (working_, best_);
            haveBest_ = true; bestError_ = err; bestPass_ = passes_;
            bestGain_ = gain_; bestCeiling_ = ceiling_; bestLevel_ = level_; bestGate_ = gateLevel_;
            saveBestAudio();
        }
        if (safe && keeps)
        {
            if (level_ <= request_.targetLufs)
            {
                if (! haveBelow_ || level_ > belowLufs_)
                    { haveBelow_ = true; belowLufs_ = level_;
                      belowGain_ = gain_; belowCeiling_ = ceiling_; }
            }
            else if (! haveAbove_ || level_ < aboveLufs_)
                { haveAbove_ = true; aboveLufs_ = level_;
                  aboveGain_ = gain_; aboveCeiling_ = ceiling_; }
        }
        const bool success = safe && keeps && err <= request_.toleranceLu;
        const bool exhausted = passes_ >= request_.maxPasses;
        if (wall_ || success || exhausted) return finishSearch();
        if (budgetOn_ && budgetSettled()) return finishSearch();
        // Reserve the last pass for restoration once a candidate exists — a safe one, or else the gentlest over the
        // budget, or else the gentlest above the ceiling. It is a real render and is logged within the caller's one pass
        // budget. When this pass IS that candidate, `out` already holds it: finish without rendering it twice.
        if ((haveBest_ || haveOverBudget_ || haveUnsafe_) && passes_ == request_.maxPasses - 1)
        {
            if (bestPass_ == passes_) return finishSearch();
            if (saved_) return finishSearch();
            gain_ = bestGain_; ceiling_ = bestCeiling_;
            restoring_ = true; nextReason_ = SolvePassRecord::Reason::DeliverWinner;
            phase_ = Phase::PassBegin;
            return StepResult::More;
        }
        chooseNext (valid);
        phase_ = Phase::PassBegin;
        return StepResult::More;
    }

    void chooseNext (bool valid) noexcept
    {
        if (! valid)
        {
            gain_ = std::clamp (gain_ + std::clamp (-20.0 - measurement_.samplePeakDb, 0.0, 48.0), -60.0, 60.0);
            return;
        }
        const double currentDrive = gain_ - ceiling_;
        const double shape = level_ - ceiling_;
        const double aim = request_.maxTruePeakDbTp - request_.truePeakAimDb;
        const double tpError = std::clamp (aim - measurement_.truePeakDbTp, -24.0, 24.0);
        double nextCeiling = std::clamp (ceiling_ + tpError, -60.0,
            std::clamp (request_.maxTruePeakDbTp, -60.0, 60.0));
        if (measurement_.truePeakDbTp > request_.maxTruePeakDbTp)
            nextCeiling = std::fmin (nextCeiling, std::fmax (-60.0, ceiling_ -
                std::clamp (measurement_.truePeakDbTp - request_.maxTruePeakDbTp, 0.0, 24.0) - 0.05));
        double slope = 1.0;
        if (havePrevious_ && std::fabs (currentDrive - previousDrive_) > 1.0e-8)
        {
            const double s = (shape - previousShape_) / (currentDrive - previousDrive_);
            if (std::isfinite (s) && s > 0.0 && s <= 1.2) slope = std::clamp (s, 0.01, 1.0);
        }
        const double need = std::clamp (request_.targetLufs - nextCeiling - shape, -24.0, 24.0);
        const double step = std::clamp (need / slope, -24.0, 24.0);
        double nextDrive = currentDrive + step;
        if (haveBelow_ && haveAbove_ && belowGain_ + 0.002 < aboveGain_
            && core::exactlyEqual (belowCeiling_, ceiling_) && core::exactlyEqual (aboveCeiling_, ceiling_)
            && core::exactlyEqual (nextCeiling, ceiling_))
            nextDrive = std::clamp (nextDrive, belowGain_ - ceiling_ + 0.001, aboveGain_ - ceiling_ - 0.001);
        if (budgetOn_) nextDrive = budgetClamp (nextDrive);
        else nextReason_ = haveBelow_ && haveAbove_ ? SolvePassRecord::Reason::InsideBracket
                                                    : SolvePassRecord::Reason::AimAtTarget;
        previousDrive_ = currentDrive; previousShape_ = shape; havePrevious_ = true;
        gain_ = std::clamp (nextDrive + nextCeiling, -60.0, 60.0);
        ceiling_ = nextCeiling;
    }

    StepResult finishSearch()
    {
        // Renders that could not be measured at all prove nothing about the target: Unavailable.
        if (! haveBest_ && ! haveOverBudget_ && ! haveUnsafe_) return fail (MasteringSolveStatus::Unavailable);
        publishLog();
        if (! haveBest_ && haveOverBudget_)
        {
            // NO RENDER KEPT THE LIMITER'S BUDGET: the gentlest ceiling-safe one measured, the budget named as broken.
            best_.status = MasteringSolveStatus::TargetUnreachable;
            best_.binding = MasteringConstraint::LimiterGainReduction;
            best_.alsoViolated = constraintBit (MasteringConstraint::LimiterGainReduction);
        }
        else if (! haveBest_)
        {
            // MEASURED RENDERS, NONE UNDER THE CEILING: the target is out of reach and the true-peak ceiling is what
            // holds it — said as such, with its binding. The file is still delivered (owner, 01.10): the gentlest render
            // measured, the smallest overshoot, verified like any other and marked `peaksAboveCeiling`.
            best_.status = MasteringSolveStatus::TargetUnreachable;
            best_.binding = MasteringConstraint::TruePeakCeiling;
            best_.alsoViolated = constraintBit (MasteringConstraint::TruePeakCeiling);
            best_.peaksAboveCeiling = true;
        }
        else best_.status = bestError_ <= request_.toleranceLu ? MasteringSolveStatus::Solved
                                                               : MasteringSolveStatus::PassLimit;
        if (best_.status == MasteringSolveStatus::PassLimit && haveBelow_ && haveAbove_
            && core::exactlyEqual (belowCeiling_, aboveCeiling_)
            && std::fabs (aboveGain_ - belowGain_) <= 0.001)
        {
            best_.status = MasteringSolveStatus::TargetBetweenAchievable;
            best_.achievedBelowLufs = belowLufs_; best_.achievedAboveLufs = aboveLufs_;
            best_.gainBelowDb = belowGain_; best_.gainAboveDb = aboveGain_;
        }
        // THE LIMITER'S BUDGET HELD THE LANDING below the target, proven: said as such, not as a pass limit or a bracket.
        if (budgetOn_ && haveBest_ && best_.status != MasteringSolveStatus::Solved && bestLevel_ < request_.targetLufs
            && budgetSettled())
        {
            best_.status = MasteringSolveStatus::TargetUnreachable;
            best_.binding = MasteringConstraint::LimiterGainReduction;
            best_.alsoViolated = constraintBit (MasteringConstraint::LimiterGainReduction);
        }
        // An exhausted budget is not proof that the target is outside the reachable range.
        if (wall_)
        {
            best_.status = MasteringSolveStatus::TargetUnreachable;
            best_.binding = MasteringConstraint::None;
            best_.limiterWall = true;
            best_.limiterSlope = wallSlope_;
            best_.limiterWallP95Db = records_[(std::size_t) (bestPass_ - 1)].limiterP95Db;
        }
        if (bestPass_ != passes_)
        {
            if (restoreBestAudio())
            {
                phase_ = Phase::VerifySetup;
                if (! clock_.begin (ProgressStage::FinalRender, passes_, request_.maxPasses,
                                    frames_ + 2, frames_)) return fail (MasteringSolveStatus::Cancelled);
                return StepResult::More;
            }
            if (passes_ >= request_.maxPasses) return fail (MasteringSolveStatus::Unavailable);
            gain_ = bestGain_; ceiling_ = bestCeiling_; restoring_ = true;
            nextReason_ = SolvePassRecord::Reason::DeliverWinner;
            phase_ = Phase::PassBegin;
            return StepResult::More;
        }
        phase_ = Phase::VerifySetup;
        if (! clock_.begin (ProgressStage::FinalRender, passes_, request_.maxPasses,
                            frames_ + 2, frames_)) return fail (MasteringSolveStatus::Cancelled);
        return StepResult::More;
    }

    void classify() noexcept
    {
        if (wall_) return;
        if (best_.status == MasteringSolveStatus::Solved) return;
        // The limiter's budget held the landing: that is the reason, and nothing in the mix is blamed for it.
        if (best_.status == MasteringSolveStatus::TargetUnreachable
            && best_.binding == MasteringConstraint::LimiterGainReduction) return;
        const bool bass = best_.sourceSubBassShare >= 0.35 && best_.limiterMeanReductionDb >= 0.5;
        const double sourceRms = totalEnergy_ > 0.0
            ? std::sqrt (totalEnergy_ / ((double) sourceFrames_ * channels_)) : 0.0;
        const bool peaks = sourceRms > 0.0 && sourcePeak_ / sourceRms >= 4.0
                        && best_.measured.limiter.maxDb >= 1.5;
        const bool dark = best_.sourcePresenceShare < 0.03;
        best_.mainReason = bass ? LandingReason::ExcessSubBass : peaks ? LandingReason::SharpPeaks
                           : dark ? LandingReason::DarkMix : LandingReason::LoudnessDemand;
        best_.secondReason = bass && peaks ? LandingReason::SharpPeaks
                             : (bass || peaks) && dark ? LandingReason::DarkMix : LandingReason::None;
    }

    double driveOf (int pass) const noexcept
    {
        return records_[(std::size_t) pass].gainDb - records_[(std::size_t) pass].ceilingDb;
    }

    // The budget's statistic of a render: on the limiter's ACTIVE windows (LoudnessRequest::limiterActiveInputDb), so
    // the silence of a programme does not water it down. NaN where those windows cannot answer it.
    double budgetStatistic (const LoudnessSolution& render) const noexcept
    {
        return render.limiterActive.stats.valid ? grStatisticValue (render.limiterActive.stats, request_.limiterGr)
                                                : std::numeric_limits<double>::quiet_NaN();
    }

    // Reads this (valid) pass — its render is `working_` — against the limiter's budget: its excess is kept, a pass over
    // it is marked in its record. A statistic the windows cannot answer is no reading, and so no breach.
    bool keepsBudget (SolvePassRecord& rec) noexcept
    {
        const double excess = budgetStatistic (working_) - request_.limiterGr.limitDb;
        budgetExcess_[(std::size_t) (passes_ - 1)] = excess;
        if (! (excess > 0.0)) return true;
        rec.violated |= constraintBit (MasteringConstraint::LimiterGainReduction);
        return false;
    }

    // The lowest drive measured over the budget, -1 when none.
    int lowestOverBudget() const noexcept
    {
        int over = -1;
        for (int k = 0; k < passes_; ++k)
            if (budgetExcess_[(std::size_t) k] > 0.0 && (over < 0 || driveOf (k) < driveOf (over))) over = k;
        return over;
    }

    // The next full-file drive uses v0.17.0's slope-back, guarded Illinois chord and proof. An excerpt edge changes only
    // the opening: its edge plus the configured offset is the first full render, replacing the old probe and target aim.
    // If that render keeps the budget, bounded 0.5 dB steps establish an over-budget end; once one exists, v0.17.0 takes
    // over unchanged except that the proof resolution is the request's. Brent belongs to the excerpt only.
    double budgetClamp (double next) noexcept
    {
        if (! request_.budgetAimsAtCrossing) return previousBudgetClamp (next);
        if (std::isfinite (excerptEdgeDrive_) && lowestOverBudget() < 0)
        {
            int kept = -1;
            for (int k = 0; k < passes_; ++k)
            {
                const double e = budgetExcess_[(std::size_t) k];
                if (std::isfinite (e) && ! (e > 0.0)
                    && records_[(std::size_t) k].truePeakDbTp <= request_.maxTruePeakDbTp
                    && (kept < 0 || driveOf (k) > driveOf (kept))) kept = k;
            }
            if (kept >= 0)
            {
                nextReason_ = SolvePassRecord::Reason::ProveEdge;
                return std::fmin (next, driveOf (kept) + 0.5);
            }
        }
        return v017BudgetClamp (next);
    }

    double v017BudgetClamp (double next) noexcept
    {
        const int over = lowestOverBudget();
        if (over < 0) return next;
        const double dOver = driveOf (over), eOver = budgetExcess_[(std::size_t) over];
        // The over drive the chord leaned on last time, and how many times running a render landed inside the budget
        // against it: each such time its excess counts half (the Illinois rule), so a far, steep over end stops holding
        // the chord down.
        if (! core::exactlyEqual (dOver, chordOverDrive_)) { chordOverDrive_ = dOver; chordStale_ = 0; }
        // The kept end: the loudest drive under it whose render kept both the budget and the ceiling (an unsafe render
        // proves nothing the delivered one could stand on).
        int kept = -1;
        for (int k = 0; k < passes_; ++k)
        {
            const double e = budgetExcess_[(std::size_t) k];
            if (std::isfinite (e) && ! (e > 0.0) && records_[(std::size_t) k].truePeakDbTp <= request_.maxTruePeakDbTp
                && driveOf (k) < dOver && (kept < 0 || driveOf (k) > driveOf (kept))) kept = k;
        }
        double limit = 0.0;
        if (kept >= 0)
        {
            const double dKept = driveOf (kept), width = dOver - dKept;
            const double eKept = budgetExcess_[(std::size_t) kept];
            const double eLean = eOver / double (1u << std::min (chordStale_, 8));
            const double chord = dKept + (-eKept) / (eLean - eKept) * width;
            limit = chord - dKept <= request_.budgetResolutionDb
                ? dKept + request_.budgetResolutionDb - 0.01 : chord;
            nextReason_ = chord - dKept <= request_.budgetResolutionDb ? SolvePassRecord::Reason::ProveEdge
                                                                        : SolvePassRecord::Reason::InsideBracket;
            limit = std::clamp (limit, dKept + 0.05 * width, dOver - 0.05 * width);
            ++chordStale_;
        }
        else
        {
            nextReason_ = SolvePassRecord::Reason::StepBackBySlope;
            double slope = 0.75, dNext = std::numeric_limits<double>::quiet_NaN(), eNext = 0.0;
            for (int k = 0; k < passes_; ++k)
            {
                const double e = budgetExcess_[(std::size_t) k], d = driveOf (k);
                if (e > 0.0 && d > dOver + 1.0e-6 && (! std::isfinite (dNext) || d < dNext)) { dNext = d; eNext = e; }
            }
            if (std::isfinite (dNext))
                if (const double s = (eNext - eOver) / (dNext - dOver); std::isfinite (s)) slope = std::clamp (s, 0.5, 2.0);
            limit = std::fmin (dOver - eOver / slope - 0.5 * request_.budgetResolutionDb,
                               dOver - request_.budgetResolutionDb);
        }
        return std::fmin (next, limit);
    }

    // The clamp as it was before v0.17.0, kept for a landing that does not ask to aim at the crossing (a manual
    // target's): between the loudest render that kept the budget below the lowest that broke it and that one, by the
    // secant of their excess (kept within the middle three fifths); with no render under it, a step back of three times
    // the excess, a dB at least.
    double previousBudgetClamp (double next) noexcept
    {
        const int over = lowestOverBudget();
        if (over < 0) return next;
        const double dOver = driveOf (over), eOver = budgetExcess_[(std::size_t) over];
        int kept = -1;
        for (int k = 0; k < passes_; ++k)
            if (std::isfinite (budgetExcess_[(std::size_t) k]) && ! (budgetExcess_[(std::size_t) k] > 0.0)
                && driveOf (k) < dOver && (kept < 0 || driveOf (k) > driveOf (kept))) kept = k;
        double limit = dOver - std::fmax (1.0, 3.0 * eOver);
        if (kept >= 0)
        {
            const double dKept = driveOf (kept), eKept = budgetExcess_[(std::size_t) kept];
            limit = dKept + std::clamp (-eKept / (eOver - eKept), 0.2, 0.8) * (dOver - dKept);
        }
        if (limit < next)
            nextReason_ = kept >= 0 ? SolvePassRecord::Reason::InsideBracket
                                    : SolvePassRecord::Reason::StepBackBySlope;
        return std::fmin (next, limit);
    }

    // THE BUDGET'S PROOF: the candidate — the render to be delivered — stands within the requested resolution under the
    // lowest drive measured over the budget, short of the target, and no render within the budget has reached past the
    // target (one that did proves the target reachable inside it).
    bool budgetSettled() const noexcept
    {
        const int over = lowestOverBudget();
        if (! haveBest_ || haveAbove_ || over < 0 || bestLevel_ >= request_.targetLufs - request_.toleranceLu) return false;
        const double delivered = bestGain_ - bestCeiling_;
        return delivered < driveOf (over) && delivered >= driveOf (over) - request_.budgetResolutionDb;
    }

    StepResult fail (MasteringSolveStatus status) noexcept
    {
        best_.status = status; best_.deliverable = false; best_.peaksAboveCeiling = false;
        publishLog();
        best_.workUnits = work_;
        phase_ = Phase::Failed; releaseDelivery();
        return StepResult::Failed;
    }

    void publishLog() noexcept
    {
        best_.passes = passes_;
        best_.logCount = std::min (TargetLoudnessSolverLimits::kMaxPasses, excerptCount_ + passes_);
        int at = 0;
        for (int i = 0; i < excerptCount_ && at < best_.logCount; ++i) best_.log[at++] = excerptRecords_[(std::size_t) i];
        for (int i = 0; i < passes_ && at < best_.logCount; ++i) best_.log[at++] = records_[(std::size_t) i];
    }

    void saveBestAudio() noexcept
    {
        bestClipperPeaksValid_ = chain_ != nullptr && std::isfinite (request_.clipperLoudShare)
            && chain_->clipperPeaks (request_.clipperLoudShare, bestClipperPeaks_);
        if (! saved_ || savedCapacity_ < (std::size_t) channels_ * (std::size_t) frames_) return;
        for (int c = 0; c < channels_; ++c)
            std::copy_n (out_[c], frames_, saved_.get() + (std::size_t) c * (std::size_t) frames_);
    }

    bool restoreBestAudio() noexcept
    {
        if (! saved_ || savedCapacity_ < (std::size_t) channels_ * (std::size_t) frames_) return false;
        for (int c = 0; c < channels_; ++c)
            std::copy_n (saved_.get() + (std::size_t) c * (std::size_t) frames_, frames_, out_[c]);
        return true;
    }

    void releaseDelivery() noexcept
    {
        if (converter_ != nullptr)
        {
            solver_.deliveryConverter_ = nullptr; solver_.deliverySource_ = nullptr;
            solver_.deliveryFrames_ = 0; converter_ = nullptr;
        }
    }

    TargetLoudnessSolver& solver_;
    MasteringChain* chain_ = nullptr;
    OfflineRenderer* renderer_ = nullptr;
    DeliveryConverter* converter_ = nullptr;
    const float* const* source_ = nullptr;
    float* const* out_ = nullptr;
    int channels_ = 0, frames_ = 0, passes_ = 0, bestPass_ = 0, verifyCursor_ = 0;
    long long sourceFrames_ = 0, sourceCursor_ = 0;
    double sourceRate_ = 0.0;
    MasteringChainParams params_ {};
    MasteringChainParams initialParams_ {};
    LoudnessRequest request_ {};
    LoudnessRequest excerptRequest_ {};
    ProgressClock clock_ { ProgressCallback {} };
    LoudnessSolution working_ {}, best_ {};
    MasterMeasurement measurement_ {};
    std::array<SolvePassRecord, 12> records_ {};
    std::array<SolvePassRecord, kMaxExcerptPasses> excerptRecords_ {};
    std::array<double, kMaxExcerptPasses> excerptDrive_ {}, excerptExcess_ {};
    std::unique_ptr<float[]> saved_;
    std::size_t savedCapacity_ = 0;
    ClipperPeaks bestClipperPeaks_ {};
    std::array<double, core::kMaxChannels> lowState_ {}, low2State_ {}, low8State_ {};
    double lowK_ = 0.0, low2K_ = 0.0, low8K_ = 0.0;
    double totalEnergy_ = 0.0, bassEnergy_ = 0.0, presenceEnergy_ = 0.0, sourcePeak_ = 0.0;
    double gain_ = 0.0, ceiling_ = 0.0, bestGain_ = 0.0, bestCeiling_ = 0.0;
    double previousDrive_ = 0.0, previousShape_ = 0.0;
    double verifyTp_ = 0.0, verifyLufs_ = 0.0;
    double bestError_ = 0.0, belowLufs_ = 0.0, aboveLufs_ = 0.0, belowGain_ = 0.0, aboveGain_ = 0.0;
    double belowCeiling_ = 0.0, aboveCeiling_ = 0.0;
    std::uint64_t work_ = 0;
    std::uint64_t retainedWorkspaceUpper_ = 0, retainedLargestBlockUpper_ = 0;
    std::uint64_t retainedSourceUpper_ = 0, retainedOutputUpper_ = 0;
    double retainedRateFloor_ = 0.0;
    int retainedFramesUpper_ = 0, retainedChannelsUpper_ = 0, retainedBucketsUpper_ = 0;
    bool haveBest_ = false, havePrevious_ = false, haveBelow_ = false, haveAbove_ = false;
    bool bestClipperPeaksValid_ = false;
    bool haveUnsafe_ = false;   // `best_` holds the gentlest measured render above the ceiling (while `haveBest_` is not)
    bool restoring_ = false;
    SolvePassRecord::Reason nextReason_ = SolvePassRecord::Reason::AimAtTarget;
    bool peakProbe_ = false;   // the first pass measured the peak the clipper's threshold is worked out from
    bool gateOn_ = false;      // the level landed is read on the source's gate
    bool budgetOn_ = false;
    bool excerptReady_ = false;
    bool wall_ = false;
    double wallSlope_ = std::numeric_limits<double>::quiet_NaN();
    bool haveOverBudget_ = false;   // `best_` holds the gentlest ceiling-safe render over the budget (while `haveBest_` is not)
    Phase phase_ = Phase::Idle;
    // The level landed (`level_`, the pass's; `bestLevel_`, best_'s) and the source's gate. `scan*` serve the source's
    // gate once, then each pass's level on it.
    double gateThreshold_ = 0.0, level_ = 0.0, bestLevel_ = 0.0, gateLevel_ = 0.0, bestGate_ = 0.0, scanSum_ = 0.0;
    long long scanCursor_ = 0, scanCount_ = 0;
    std::uint64_t gateNum_ = 1, gateDen_ = 1;   // block j of the master takes the source's reading round (j·num/den) + 3
    // The limiter's budget: each pass's excess over it (NaN: no reading), indexed as `records_`.
    std::array<double, 12> budgetExcess_ {};
    double chordOverDrive_ = std::numeric_limits<double>::quiet_NaN();
    int chordStale_ = 0;
    const float* excerptSource_[core::kMaxChannels] {};
    long long excerptSourceFrom_ = 0, excerptStatisticFrom_ = 0;
    long long excerptSourceFrames_ = 0, excerptStatisticFrames_ = 0, excerptSkipFrames_ = 0;
    int excerptFrames_ = 0, excerptCount_ = 0, excerptLow_ = -1, excerptHigh_ = -1;
    double excerptEdgeDrive_ = std::numeric_limits<double>::quiet_NaN();
};

inline LoudnessSolution solveProductLanding (TargetLoudnessSolver& solver, MasteringChain& chain,
    OfflineRenderer& renderer, const MasteringChainParams& params, const float* const* source,
    float* const* out, int channels, int frames, const LoudnessRequest& request,
    const ProgressCallback& progress, DeliveryConverter* converter, long long sourceFrames)
{
    const long long sourceLength = converter != nullptr ? sourceFrames : frames;
    const double sourceRate = converter != nullptr ? converter->sourceRate() : chain.sampleRate();
    MasteringSolveStatus why = MasteringSolveStatus::InvalidRequest;
    if (! LandingSearch::preflight (solver, chain, renderer, params, source, sourceLength, sourceRate,
                                    out, channels, frames, request, converter, why))
    {
        LoudnessSolution refused;
        refused.activityThresholdDb = request.activityThresholdDb;
        refused.status = why;
        return refused;
    }
    LandingSearch search (solver);
    if (! search.begin (chain, renderer, params, source, sourceLength, sourceRate,
                        out, channels, frames, request, progress, converter))
        return search.result();
    StepResult state = StepResult::More;
    while (state == StepResult::More) state = search.step (LLONG_MAX);
    return search.result();
}

} // namespace felitronics::mastering
