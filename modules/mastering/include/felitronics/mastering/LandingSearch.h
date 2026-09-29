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

// One landing owns one solver while it is active. The caller owns three disjoint planar buffers:
// source, render output, and a full-length safe copy. Keeping the best PCM costs memory, but lets
// all twelve renders be search passes; final delivery is a resumable copy and independent remeasure.
class LandingSearch final
{
public:
    explicit LandingSearch (TargetLoudnessSolver& solver) noexcept : solver_ (solver) {}
    LandingSearch (const LandingSearch&) = delete;
    LandingSearch& operator= (const LandingSearch&) = delete;
    ~LandingSearch() noexcept { releaseDelivery(); }

    // A conservative fresh-call declaration, including a full safe PCM buffer owned by the caller,
    // both retained result workspaces, armed band distributions, and MSVC Debug container proxies.
    // The chain, renderer, converter and solver's prepare() storage are separate preparations.
    static std::uint64_t storageFor (double sampleRate, int channels, int frames,
                                     int traceBuckets, int armedPairs = kBandGrStride,
                                     double binDb = 0.01) noexcept
    {
        if (armedPairs < 0 || armedPairs > kBandGrStride) return 0;
        const std::uint64_t base = TargetLoudnessSolver::solveCallBytes (
            sampleRate, channels, frames, traceBuckets, binDb);
        if (base == 0) return 0;
        const std::uint64_t safe = (std::uint64_t) frames * (std::uint64_t) channels * sizeof (float);
        const std::uint64_t bandHist = dynamics::offline::QuantileHistogram::storageBytes (
            0.0, kBandGrRangeDb, 0.01);
        const std::uint64_t bandTrace = GainReductionTrace::bytesFor (traceBuckets, frames);
        return 3u * base + safe + (std::uint64_t) armedPairs *
            (4u * bandHist + 3u * bandTrace + 3u * sizeof (BandGrResult)
             + 16u * storage::kVectorProxyBytes + 1024u);
    }

    // `sourceRate` is the grid of `source`; `frames` is the delivered length. `safe` has `frames`
    // writable samples per channel. The converter, when present, was prepared for those two rates.
    [[nodiscard]] bool begin (MasteringChain& chain, OfflineRenderer& renderer,
                              const MasteringChainParams& params, const float* const* source,
                              long long sourceFrames, double sourceRate, float* const* out,
                              float* const* safe, int channels, int frames, LoudnessRequest request,
                              const ProgressCallback& progress = {}, DeliveryConverter* converter = nullptr)
    {
        if (phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Failed) return false;
        MasteringSolveStatus why = MasteringSolveStatus::InvalidRequest;
        const float* outputPlanes[core::kMaxChannels] {};
        if (out != nullptr && channels > 0 && channels <= core::kMaxChannels)
            for (int c = 0; c < channels; ++c) outputPlanes[c] = out[c];
        if (request.maxPasses < 1 || request.maxPasses > 12
            || ! std::isfinite (request.normalizationGainDb) || std::fabs (request.normalizationGainDb) > 60.0
            || ! std::isfinite (params.inputGainDb)
            || std::fabs (params.inputGainDb + request.normalizationGainDb) > 60.0
            || ! solver_.admits (chain, renderer, channels, frames, request, why)
            || sourceFrames <= 0 || ! std::isfinite (sourceRate) || sourceRate <= 0.0
            || sourceFrames > LLONG_MAX - 2LL * frames - chain.latencySamples() - kBandGrStride - 6
            || (converter == nullptr && (sourceFrames != frames || ! core::exactlyEqual (sourceRate, chain.sampleRate())))
            || (converter != nullptr && (! converter->isPrepared()
                || ! core::exactlyEqual (converter->sourceRate(), sourceRate)
                || ! core::exactlyEqual (converter->deliveryRate(), chain.sampleRate())
                || DeliveryConverter::deliveredFrames (sourceRate, chain.sampleRate(), sourceFrames) != frames))
            || ! planesUsable (source, out, channels, sourceFrames, frames)
            || ! planesUsable (source, safe, channels, sourceFrames, frames)
            || ! planesUsable (outputPlanes, safe, channels, frames, frames))
        {
            best_.status = why == MasteringSolveStatus::Solved ? MasteringSolveStatus::InvalidRequest : why;
            phase_ = Phase::Failed;
            return false;
        }
        chain_ = &chain; renderer_ = &renderer; source_ = source; out_ = out; safe_ = safe;
        channels_ = channels; frames_ = frames; sourceFrames_ = sourceFrames; sourceRate_ = sourceRate;
        params_ = params; params_.inputGainDb += request.normalizationGainDb;
        request_ = request; clock_ = ProgressClock (progress);
        working_ = LoudnessSolution {}; best_ = LoudnessSolution {};
        working_.activityThresholdDb = best_.activityThresholdDb = request.activityThresholdDb;
        haveBest_ = havePrevious_ = haveBelow_ = haveAbove_ = false;
        passes_ = 0; copied_ = 0; sourceCursor_ = 0; verifyCursor_ = 0;
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
        ceiling_ = std::clamp (std::fmin (initialCeiling, request.maxTruePeakDbTp - 0.15),
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

    // A zero budget is inert. A positive call performs at most one render, so the caller can cancel
    // after any stage. A unit is a source, render, meter, or copy frame; setup and finish cost one.
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
        if (phase_ == Phase::CaptureBest || phase_ == Phase::CopyBest)
        {
            const int n = (int) std::min<long long> (budget, frames_ - copied_);
            for (int c = 0; c < channels_; ++c)
                std::copy_n ((phase_ == Phase::CaptureBest ? out_[c] : safe_[c]) + copied_, n,
                             (phase_ == Phase::CaptureBest ? safe_[c] : out_[c]) + copied_);
            copied_ += n; work_ += (std::uint64_t) n;
            if (phase_ == Phase::CopyBest && ! clock_.advance (n))
                return fail (MasteringSolveStatus::Cancelled);
            if (copied_ < frames_) return StepResult::More;
            copied_ = 0;
            if (phase_ == Phase::CaptureBest)
            {
                if (stopAfterCapture_) return finishSearch();
                phase_ = Phase::PassBegin;
                return StepResult::More;
            }
            phase_ = Phase::VerifySetup;
            return StepResult::More;
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
            phase_ = Phase::VerifyGate;
            if (! clock_.checkpoint()) return fail (MasteringSolveStatus::Cancelled);
            return StepResult::More;
        }
        if (phase_ == Phase::VerifyGate)
        {
            verifyLufs_ = solver_.pass_->lm.integratedLufs(); ++work_;
            phase_ = Phase::VerifyFinish;
            if (! clock_.checkpoint()) return fail (MasteringSolveStatus::Cancelled);
            return StepResult::More;
        }
        if (phase_ == Phase::VerifyFinish)
        {
            if (! core::exactlyEqual (verifyTp_, best_.measured.truePeakDbTp)
                || ! core::exactlyEqual (verifyLufs_, best_.measured.integratedLufs)
                || verifyTp_ > request_.maxTruePeakDbTp || solver_.pass_->lm.droppedBlocks() != 0
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
    bool active() const noexcept { return phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Failed; }

private:
    enum class Phase { Idle, SourceStats, PassBegin, PassRun, CaptureBest, CopyBest, VerifySetup,
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
        const bool valid = measurement_.loudnessValid && std::isfinite (measurement_.integratedLufs);
        if (! valid && measurement_.samplePeakDb <= -180.0) return fail (MasteringSolveStatus::Unavailable);
        const bool safe = valid && measurement_.truePeakDbTp <= request_.maxTruePeakDbTp;
        const double err = valid ? std::fabs (measurement_.integratedLufs - request_.targetLufs)
                                 : std::numeric_limits<double>::infinity();
        const double level = measurement_.integratedLufs;
        const double previousLevel = best_.measured.integratedLufs;
        const bool bothBelow = haveBest_ && level <= request_.targetLufs && previousLevel <= request_.targetLufs;
        const bool bothAbove = haveBest_ && level >= request_.targetLufs && previousLevel >= request_.targetLufs;
        const bool nearer = ! haveBest_ || (bothBelow && level > previousLevel)
                          || (bothAbove && level < previousLevel)
                          || (! bothBelow && ! bothAbove && err < bestError_);
        const bool sameDistance = haveBest_ && core::exactlyEqual (err, bestError_)
                               && core::exactlyEqual (level, previousLevel);
        bool captured = false;
        if (safe && (nearer || (sameDistance && measurement_.limiter.meanDb < best_.measured.limiter.meanDb)))
        {
            working_.measured = measurement_;
            working_.preLimiterGainDb = gain_; working_.ceilingDbTp = ceiling_;
            std::swap (working_, best_);
            haveBest_ = captured = true; bestError_ = err;
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
        stopAfterCapture_ = success || exhausted;
        if (! stopAfterCapture_) chooseNext (valid);
        if (captured)
        {
            copied_ = 0; phase_ = Phase::CaptureBest;
            return StepResult::More;
        }
        if (stopAfterCapture_) return finishSearch();
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
        if (! haveBest_) return fail (MasteringSolveStatus::Unavailable);
        best_.passes = passes_; best_.logCount = passes_;
        for (int i = 0; i < passes_; ++i) best_.log[i] = records_[(std::size_t) i];
        best_.status = bestError_ <= request_.toleranceLu ? MasteringSolveStatus::Solved : MasteringSolveStatus::PassLimit;
        if (best_.status == MasteringSolveStatus::PassLimit && haveBelow_ && haveAbove_
            && core::exactlyEqual (belowCeiling_, aboveCeiling_)
            && std::fabs (aboveGain_ - belowGain_) <= 0.001)
        {
            best_.status = MasteringSolveStatus::TargetBetweenAchievable;
            best_.achievedBelowLufs = belowLufs_; best_.achievedAboveLufs = aboveLufs_;
            best_.gainBelowDb = belowGain_; best_.gainAboveDb = aboveGain_;
        }
        // An exhausted budget is not proof that the target is outside the reachable range.
        copied_ = 0; phase_ = Phase::CopyBest;
        if (! clock_.begin (ProgressStage::FinalRender, passes_, request_.maxPasses,
                            2LL * frames_ + 2, frames_)) return fail (MasteringSolveStatus::Cancelled);
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
        best_.status = status; best_.deliverable = false;
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
    float* const* safe_ = nullptr;
    int channels_ = 0, frames_ = 0, passes_ = 0, copied_ = 0, verifyCursor_ = 0;
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
    double gain_ = 0.0, ceiling_ = 0.0, previousDrive_ = 0.0, previousShape_ = 0.0;
    double verifyTp_ = 0.0, verifyLufs_ = 0.0;
    double bestError_ = 0.0, belowLufs_ = 0.0, aboveLufs_ = 0.0, belowGain_ = 0.0, aboveGain_ = 0.0;
    double belowCeiling_ = 0.0, aboveCeiling_ = 0.0;
    std::uint64_t work_ = 0;
    bool haveBest_ = false, havePrevious_ = false, haveBelow_ = false, haveAbove_ = false;
    bool stopAfterCapture_ = false;
    Phase phase_ = Phase::Idle;
};

inline LoudnessSolution solveProductLanding (TargetLoudnessSolver& solver, MasteringChain& chain,
    OfflineRenderer& renderer, const MasteringChainParams& params, const float* const* source,
    float* const* out, int channels, int frames, const LoudnessRequest& request,
    const ProgressCallback& progress, DeliveryConverter* converter, long long sourceFrames)
{
    LoudnessSolution refused;
    refused.activityThresholdDb = request.activityThresholdDb;
    const long long sourceLength = converter != nullptr ? sourceFrames : frames;
    const double sourceRate = converter != nullptr ? converter->sourceRate() : chain.sampleRate();
    MasteringSolveStatus why = MasteringSolveStatus::InvalidRequest;
    if (channels < 1 || channels > core::kMaxChannels || frames <= 0
        || request.maxPasses < 1 || request.maxPasses > 12
        || ! solver.admits (chain, renderer, channels, frames, request, why)
        || sourceLength <= 0 || sourceLength > LLONG_MAX - 2LL * frames - chain.latencySamples()
                                            - kBandGrStride - 6
        || ! planesUsable (source, out, channels, sourceLength, frames)
        || (converter != nullptr && (! converter->isPrepared()
            || ! core::exactlyEqual (converter->deliveryRate(), chain.sampleRate())
            || DeliveryConverter::deliveredFrames (sourceRate, chain.sampleRate(), sourceLength) != frames))
        || (std::uint64_t) frames > std::numeric_limits<std::size_t>::max()
                                  / ((std::size_t) channels * sizeof (float)))
    {
        refused.status = why == MasteringSolveStatus::NotPrepared ? why : MasteringSolveStatus::InvalidRequest;
        return refused;
    }
    storage::Buffer<float> safe;
    safe.resizeForOverwrite ((std::size_t) channels * (std::size_t) frames);
    std::array<float*, core::kMaxChannels> planes {};
    for (int c = 0; c < channels; ++c)
        planes[(std::size_t) c] = safe.data() + (std::size_t) c * (std::size_t) frames;
    LandingSearch search (solver);
    if (! search.begin (chain, renderer, params, source, sourceLength, sourceRate,
                        out, planes.data(), channels, frames, request, progress, converter))
        return search.result();
    StepResult state = StepResult::More;
    for (int i = 0; i < 256 && state == StepResult::More; ++i) state = search.step (LLONG_MAX);
    if (state == StepResult::More) search.cancel();
    return search.result();
}

} // namespace felitronics::mastering
