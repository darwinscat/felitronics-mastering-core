// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/mastering/LoudnessSolver.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace felitronics::mastering
{

// One landing owns one solver while it is active. The caller owns source and output
// planar buffers. A saved candidate is restored by a counted render when needed.
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
        std::uint64_t sourceBytes = 0, outputBytes = 0, workspaceBytes = 0;
        std::uint64_t maxLiveBytes = 0, largestBlockBytes = 0;
    };

    // A conservative fresh-call declaration, without another full PCM buffer,
    // both retained result workspaces, armed band distributions, and MSVC Debug container proxies.
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
        st.workspaceBytes = storageFor (deliveryRate, sourceChannels, deliveryFrames,
                                        traceBuckets, armedPairs, binDb);
        if (st.workspaceBytes == 0u) return st;
        st.sourceBytes = (std::uint64_t) sourceFrames * (std::uint64_t) sourceChannels * sizeof (float);
        st.outputBytes = (std::uint64_t) deliveryFrames * (std::uint64_t) sourceChannels * sizeof (float);
        constexpr auto max = std::numeric_limits<std::uint64_t>::max();
        if (st.sourceBytes > max - st.outputBytes
            || st.sourceBytes + st.outputBytes > max - st.workspaceBytes) return Storage {};
        st.maxLiveBytes = st.sourceBytes + st.outputBytes + st.workspaceBytes;
        // The workspace figure is a conservative bound on any one of its
        // components, including MSVC Debug proxy allocations.
        st.largestBlockBytes = std::max ({ st.sourceBytes, st.outputBytes, st.workspaceBytes });
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
            constexpr auto max = std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t pcm = st.sourceBytes > max - st.outputBytes
                ? max : st.sourceBytes + st.outputBytes;
            st.maxLiveBytes = pcm > max - st.workspaceBytes ? max : pcm + st.workspaceBytes;
            st.maxLiveBytes = retainedWorkspaceUpper_ > max - st.maxLiveBytes
                ? max : st.maxLiveBytes + retainedWorkspaceUpper_;
            st.largestBlockBytes = std::max ({ st.largestBlockBytes, st.sourceBytes, st.outputBytes });
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
        retainedWorkspaceUpper_ = storageFor (retainedRateFloor_, retainedChannelsUpper_,
            retainedFramesUpper_, retainedBucketsUpper_, kBandGrStride,
            solver_.compHist_.binWidth());
        retainedLargestBlockUpper_ = std::max (retainedLargestBlockUpper_, demand.largestBlockBytes);
        chain_ = &chain; renderer_ = &renderer; source_ = source; out_ = out;
        channels_ = channels; frames_ = frames; sourceFrames_ = sourceFrames; sourceRate_ = sourceRate;
        params_ = params; params_.inputGainDb += request.normalizationGainDb;
        request_ = request; clock_ = ProgressClock (progress);
        working_.activityThresholdDb = best_.activityThresholdDb = request.activityThresholdDb;
        haveBest_ = havePrevious_ = haveBelow_ = haveAbove_ = haveUnsafe_ = peakProbe_ = false;
        passes_ = 0; sourceCursor_ = 0; verifyCursor_ = 0; bestPass_ = 0; restoring_ = false;
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
            || ! planesUsable (source, out, channels, sourceFrames, frames))
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
            if (sourceCursor_ == sourceFrames_) phase_ = Phase::PassBegin;
            return StepResult::More;
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
    // The peak at the limiter's input, at a gain of 0, the delivered render's clipper was set from: the one the first
    // pass measured where the clipper rides it, else the caller's (MasteringChainParams::peakClipPeakDb).
    double peakClipPeakDb() const noexcept { return params_.peakClipPeakDb; }
    int startedPasses() const noexcept { return passes_; }
    std::uint64_t completedWork() const noexcept { return work_; }
    bool active() const noexcept { return phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Failed; }

private:
    enum class Phase { Idle, SourceStats, PassBegin, PassRun, VerifySetup,
                       VerifyMeter, VerifyDrain, VerifyGate, VerifyFinish, Done, Failed };

    StepResult completedPass()
    {
        SolvePassRecord& rec = records_[(std::size_t) (passes_ - 1)];
        rec.integratedLufs = measurement_.integratedLufs; rec.truePeakDbTp = measurement_.truePeakDbTp;
        rec.plrDb = measurement_.plrDb; rec.limiterMaxGrDb = measurement_.limiter.maxDb;
        rec.loudnessRangeLu = measurement_.loudnessRangeLu;
        rec.violated = measurement_.truePeakDbTp > request_.maxTruePeakDbTp
            ? constraintBit (MasteringConstraint::TruePeakCeiling) : 0u;
        if (! clock_.finish (&rec)) return fail (MasteringSolveStatus::Cancelled);
        if (restoring_)
        {
            if (! core::exactlyEqual (measurement_.integratedLufs, best_.measured.integratedLufs)
                || ! core::exactlyEqual (measurement_.truePeakDbTp, best_.measured.truePeakDbTp)
                || (haveBest_ && measurement_.truePeakDbTp > request_.maxTruePeakDbTp))
                return fail (MasteringSolveStatus::Unavailable);
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
        const double err = valid ? std::fabs (measurement_.integratedLufs - request_.targetLufs)
                                 : std::numeric_limits<double>::infinity();
        // THE GENTLEST RENDER ABOVE THE CEILING, held in `best_` while no render is under it (owner, 01.10: the file is
        // delivered even then, marked): the smallest overshoot of the ceiling, the nearer loudness on an equal one. The
        // first ceiling-safe render replaces it below (`nearer` holds for any render while `haveBest_` is false).
        if (! haveBest_ && valid && ! safe
            && (! haveUnsafe_ || measurement_.truePeakDbTp < best_.measured.truePeakDbTp
                || (core::exactlyEqual (measurement_.truePeakDbTp, best_.measured.truePeakDbTp) && err < bestError_)))
        {
            working_.measured = measurement_;
            working_.preLimiterGainDb = gain_; working_.ceilingDbTp = ceiling_;
            std::swap (working_, best_);
            bestError_ = err; bestPass_ = passes_;
            bestGain_ = gain_; bestCeiling_ = ceiling_;
            haveUnsafe_ = true;
        }
        const double level = measurement_.integratedLufs;
        const double previousLevel = best_.measured.integratedLufs;
        const bool bothBelow = haveBest_ && level <= request_.targetLufs && previousLevel <= request_.targetLufs;
        const bool bothAbove = haveBest_ && level >= request_.targetLufs && previousLevel >= request_.targetLufs;
        const bool nearer = ! haveBest_ || (bothBelow && level > previousLevel)
                          || (bothAbove && level < previousLevel)
                          || (! bothBelow && ! bothAbove && err < bestError_);
        const bool sameDistance = haveBest_ && core::exactlyEqual (err, bestError_)
                               && core::exactlyEqual (level, previousLevel);
        if (safe && (nearer || (sameDistance && measurement_.limiter.meanDb < best_.measured.limiter.meanDb)))
        {
            working_.measured = measurement_;
            working_.preLimiterGainDb = gain_; working_.ceilingDbTp = ceiling_;
            std::swap (working_, best_);
            haveBest_ = true; bestError_ = err; bestPass_ = passes_;
            bestGain_ = gain_; bestCeiling_ = ceiling_;
        }
        if (safe)
        {
            if (measurement_.integratedLufs <= request_.targetLufs)
            {
                if (! haveBelow_ || measurement_.integratedLufs > belowLufs_)
                    { haveBelow_ = true; belowLufs_ = measurement_.integratedLufs;
                      belowGain_ = gain_; belowCeiling_ = ceiling_; }
            }
            else if (! haveAbove_ || measurement_.integratedLufs < aboveLufs_)
                { haveAbove_ = true; aboveLufs_ = measurement_.integratedLufs;
                  aboveGain_ = gain_; aboveCeiling_ = ceiling_; }
        }
        const bool success = safe && err <= request_.toleranceLu;
        const bool exhausted = passes_ >= request_.maxPasses;
        if (success || exhausted) return finishSearch();
        // Reserve the last pass for restoration once a candidate exists — a safe one, or else the gentlest above the
        // ceiling. It is a real render and is logged within the caller's one pass budget.
        // When this pass IS that candidate, `out` already holds it: finish without rendering it twice.
        if ((haveBest_ || haveUnsafe_) && passes_ == request_.maxPasses - 1)
        {
            if (bestPass_ == passes_) return finishSearch();
            gain_ = bestGain_; ceiling_ = bestCeiling_;
            restoring_ = true;
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
        const double shape = measurement_.integratedLufs - ceiling_;
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
        previousDrive_ = currentDrive; previousShape_ = shape; havePrevious_ = true;
        gain_ = std::clamp (nextDrive + nextCeiling, -60.0, 60.0);
        ceiling_ = nextCeiling;
    }

    StepResult finishSearch()
    {
        // Renders that could not be measured at all prove nothing about the target: Unavailable.
        if (! haveBest_ && ! haveUnsafe_) return fail (MasteringSolveStatus::Unavailable);
        best_.passes = passes_; best_.logCount = passes_;
        for (int i = 0; i < passes_; ++i) best_.log[i] = records_[(std::size_t) i];
        if (! haveBest_)
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
        // An exhausted budget is not proof that the target is outside the reachable range.
        if (bestPass_ != passes_)
        {
            if (passes_ >= request_.maxPasses) return fail (MasteringSolveStatus::Unavailable);
            gain_ = bestGain_; ceiling_ = bestCeiling_; restoring_ = true;
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
        if (best_.status == MasteringSolveStatus::Solved) return;
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

    StepResult fail (MasteringSolveStatus status) noexcept
    {
        best_.status = status; best_.deliverable = false; best_.peaksAboveCeiling = false;
        best_.passes = passes_; best_.logCount = passes_;
        for (int i = 0; i < passes_; ++i) best_.log[i] = records_[(std::size_t) i];
        best_.workUnits = work_;
        phase_ = Phase::Failed; releaseDelivery();
        return StepResult::Failed;
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
    LoudnessRequest request_ {};
    ProgressClock clock_ { ProgressCallback {} };
    LoudnessSolution working_ {}, best_ {};
    MasterMeasurement measurement_ {};
    std::array<SolvePassRecord, 12> records_ {};
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
    bool haveUnsafe_ = false;   // `best_` holds the gentlest measured render above the ceiling (while `haveBest_` is not)
    bool restoring_ = false;
    bool peakProbe_ = false;   // the first pass measured the peak the clipper's threshold is worked out from
    Phase phase_ = Phase::Idle;
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
