// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.

#pragma once

#include <felitronics/core/Config.h>
#include <felitronics/mastering/DeliveryConverter.h>
#include <felitronics/storage/VectorBytes.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/mastering/OfflineRenderer.h>
#include <felitronics/mastering/Progress.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <felitronics/storage/Buffer.h>

namespace felitronics::mastering
{

//==============================================================================
// felitronics::mastering::DeliveredMastering — rendering, search and input range with SRC first.
// A render and each search pass use the caller's delivery buffer for SRC and chain output.
// The range measurement still uses a temporary converted programme for its meter.
//
// WHY THIS IS A CLASS AND NOT THREE LINES IN EACH CALLER. Two paths deliver a programme: the C ABI facade and
// the direct C++ call `fcore_master selftest` compares it with, bit for bit. What sits between the converter
// and the solver — which buffer receives the conversion, that a range too short to measure is refused
// before a sample is converted — is exactly the kind of composition two hand-written copies get wrong in the
// same way. Written here once, both paths call it, and the bit-compare then compares an ABI against a core
// rather than a copy against a copy.
//
// WHAT IT OWNS: the converter, prepared for one (source rate, delivery rate, width, block). WHAT IT DOES NOT:
// the chain, the renderer and the solver stay the caller's, because they already have a lifecycle of their
// own (configure, channel weights, lazy preparation) and a second owner would be a second place to get it
// wrong. Every operation checks that the chain AND the solver it is handed run at the delivery rate: either one
// prepared at the source rate would render or meter a converted programme at the wrong speed, every number
// plausible.
//
// A REFUSAL WRITES NOTHING AND ALLOCATES NOTHING (law 11b). Every verdict an operation can reach is reached before
// the converter writes a sample and before a programme buffer is asked for: the renderer's width and preparation,
// overlapping planes, and — for the search — everything `TargetLoudnessSolver::admits` would refuse before its first
// pass. Converting first and letting the renderer or the solver say no afterwards left the caller's output full of
// unmastered, resampled audio, or spent a whole programme's memory on a request the solver was always going to
// refuse.
//
// MEMORY (law 11d). `render` asks the heap for nothing after preparation. A delivered search converts the
// original source again on every pass and streams SRC blocks into the chain over the caller's output buffer;
// no third complete PCM allocation exists. `solveBytes` is the solver's pass scratch. Range measurement keeps
// one converted programme for its one meter call, declared by `measureRangeBytes`. At equal rates the caller's
// input is read directly.
//==============================================================================
class DeliveredMastering
{
public:
    [[nodiscard]] static long long deliveredFrames (double sourceRate, double deliveryRate, long long inFrames) noexcept
    {
        return DeliveryConverter::deliveredFrames (sourceRate, deliveryRate, inFrames);
    }

    struct RenderStorage
    {
        bool ok = false;
        std::uint64_t sourceBytes = 0, outputBytes = 0, workspaceBytes = 0;
        std::uint64_t maxLiveBytes = 0, largestBlockBytes = 0;
    };

    // Conservative whole-job demand. The source and delivery planes each occupy one
    // contiguous allocation; the workspace is the chain, renderer and converter only.
    // Both sums use uint64_t even when the caller runs on wasm32.
    [[nodiscard]] static RenderStorage storageFor (double sourceRate, double deliveryRate, int numChannels,
                                                   long long inFrames, const MasteringChainConfig& config,
                                                   int block) noexcept
    {
        RenderStorage st;
        const long long d = deliveredFrames (sourceRate, deliveryRate, inFrames);
        if (inFrames < 0 || d < 0 || d > INT_MAX) return st;
        const std::uint64_t prepared = createBytes (sourceRate, deliveryRate, numChannels, config, block);
        if (prepared == 0) return st;
        st.workspaceBytes = prepared + DeliveryConverter::constructBytes() + storage::kVectorProxyBytes;
        st.sourceBytes = (std::uint64_t) inFrames * (std::uint64_t) numChannels * sizeof (float);
        st.outputBytes = (std::uint64_t) d * (std::uint64_t) numChannels * sizeof (float);
        st.maxLiveBytes = st.sourceBytes + st.outputBytes + st.workspaceBytes;
        st.largestBlockBytes = std::max (std::max (st.sourceBytes, st.outputBytes), st.workspaceBytes);
        st.ok = true;
        return st;
    }

    // WHAT A DELIVERING `create` ASKS THE HEAP FOR, AND WHETHER IT CAN BE BUILT AT ALL — one expression for both,
    // as `createBytes` is for a chain at one rate: the chain and the renderer at the DELIVERY rate plus the
    // converter. 0 for exactly the geometries either half refuses, so a facade that decides by this number and
    // publishes it cannot disagree with itself.
    [[nodiscard]] static std::uint64_t createBytes (double sourceRate, double deliveryRate, int numChannels,
                                                    const MasteringChainConfig& config, int rendererBlock) noexcept
    {
        const std::uint64_t chain = mastering::createBytes (deliveryRate, numChannels, config, rendererBlock);
        const std::uint64_t conv  = DeliveryConverter::prepareBytes (sourceRate, deliveryRate, numChannels, rendererBlock);
        return (chain == 0u || conv == 0u) ? 0u : chain + conv;
    }

    // solve(): the search's own peak at the delivered length (one pass — see
    // `TargetLoudnessSolver::solveBytes`). Conversion is streamed into the output on every pass.
    // 0 where the call converts nothing: an empty or
    // unrepresentable programme, which the solver refuses before any pass. `grTraceBuckets` and `binDb` as there.
    [[nodiscard]] static std::uint64_t solveBytes (double sourceRate, double deliveryRate, int numChannels,
                                                   long long inFrames, int grTraceBuckets, double binDb = 0.01) noexcept
    {
        const long long d = deliveredFrames (sourceRate, deliveryRate, inFrames);
        if (d <= 0 || d > INT_MAX || numChannels < 1 || numChannels > core::kMaxChannels) return 0u;
        const std::uint64_t search = TargetLoudnessSolver::solveBytes (deliveryRate, numChannels, (int) d, grTraceBuckets, binDb);
        return search;
    }

    // The result exists even when conversion or the search is refused. Keep its construction
    // inside the core's call budget, alongside the search's working set.
    [[nodiscard]] static std::uint64_t solveCallBytes (double sourceRate, double deliveryRate, int numChannels,
                                                       long long inFrames, int grTraceBuckets, double binDb = 0.01) noexcept
    {
        return solveBytes (sourceRate, deliveryRate, numChannels, inFrames, grTraceBuckets, binDb)
             + TargetLoudnessSolver::solutionReturnBytes();
    }

    // measureInputLoudnessRange(): the converted programme and one meter — and NOTHING for a delivered length too
    // short to have a range, which the call refuses before converting a sample. THE SHORTNESS IS JUDGED ON THE
    // DELIVERED LENGTH, which is what the meter sees: 132,300 frames at 44.1 kHz are 1.38 s of input frames read
    // at 96 kHz but 3.0 s of programme, and a budget computed from the input count said 0 for a call that builds
    // a meter.
    [[nodiscard]] static std::uint64_t measureRangeBytes (double sourceRate, double deliveryRate, int numChannels,
                                                          long long inFrames) noexcept
    {
        const long long d = deliveredFrames (sourceRate, deliveryRate, inFrames);
        if (d <= 0 || d > INT_MAX || numChannels < 1 || numChannels > core::kMaxChannels) return 0u;
        const std::uint64_t meter = TargetLoudnessSolver::measureRangeBytes (deliveryRate, (int) d);
        return meter == 0u ? 0u : programmeBytes (sourceRate, deliveryRate, numChannels, d) + meter;
    }

    [[nodiscard]] static std::uint64_t prepareBytes (double sourceRate, double deliveryRate, int numChannels, int block) noexcept
    {
        return DeliveryConverter::prepareBytes (sourceRate, deliveryRate, numChannels, block);
    }

    [[nodiscard]] bool prepare (double sourceRate, double deliveryRate, int numChannels, int block)
    {
        prepared_ = false;
        renderPhase_ = RenderPhase::Idle;
        if (! conv_.prepare (sourceRate, deliveryRate, numChannels, block)) return false;
        sourceRate_ = sourceRate; deliveryRate_ = deliveryRate; nch_ = numChannels;
        identity_ = conv_.plan().identity;
        prepared_ = true;
        return true;
    }

    bool   isPrepared()   const noexcept { return prepared_; }
    // 0 until prepared, and 0 again after a refused prepare (law 11b) — as `TargetLoudnessSolver::sampleRate()` and
    // `MasteringChain::sampleRate()` answer. A caller that checks these to decide whether to re-prepare must not read
    // the previous build's rates after a refusal (a review of the rate floor found 48000 / 96000 readable after
    // `7999 -> 48000`).
    double sourceRate()   const noexcept { return prepared_ ? sourceRate_ : 0.0; }
    double deliveryRate() const noexcept { return prepared_ ? deliveryRate_ : 0.0; }
    const DeliveryConverter& converter() const noexcept { return conv_; }

    // Non-finite input samples in the programme of THE LAST RENDER, SOLVE OR RANGE MEASUREMENT THAT REACHED THE COUNT —
    // counted by the converter's gate over a conversion that completed, or, where a solve or a range measurement reads
    // the input in place at equal rates (the chain's gate then replaces the samples), by the same test over the input.
    // A number about the CALLER's programme, at the source rate. A call refused before its count leaves the previous
    // one where it was, so a caller that reads it after a refusal reads an earlier programme's — the rule
    // `fc_solution_log` keeps for `written`. A call refused AFTER its count keeps its own: at equal rates a range
    // measurement refuses a poisoned programme having counted it, and the count is then the reason.
    std::uint64_t nonFiniteInputSamples() const noexcept { return nonFinite_; }

    // A render at the parameters the chain already holds. `out` must be exactly `deliveredFrames(inFrames)`
    // frames per channel and `outFrames` that number; the planes must be `planesUsable` (Planes.h) — the conversion
    // writes `out` while it still reads `in`, at another stride, and the render then runs in place over `out`. No
    // allocation.
    [[nodiscard]] bool render (MasteringChain& chain, OfflineRenderer& renderer,
                               const float* const* in, int numChannels, long long inFrames,
                               float* const* out, long long outFrames) noexcept
    {
        return render (chain, renderer, in, numChannels, inFrames, out, outFrames, ProgressCallback {});
    }

    // As above, reporting through `progress`: `ProgressStage::Convert` over the conversion, then
    // `ProgressStage::Render` over the render, each pass/maxPasses 0 and its own 0..1 fraction. A stopped
    // callback ends the call at the point it stopped and returns false; nothing is rolled back.
    [[nodiscard]] bool render (MasteringChain& chain, OfflineRenderer& renderer,
                               const float* const* in, int numChannels, long long inFrames,
                               float* const* out, long long outFrames, const ProgressCallback& progress) noexcept
    {
        if (! beginRender (chain, renderer, in, numChannels, inFrames, out, outFrames, progress)) return false;
        while (renderPhase_ != RenderPhase::Done)
            if (stepRender (chain, renderer, LLONG_MAX) == StepResult::Failed) return false;
        return finishRender();
    }

    // One source plane set and one delivery plane set are borrowed across calls. Conversion
    // finishes before the in-place chain starts, so no delivered programme is allocated.
    [[nodiscard]] bool beginRender (MasteringChain& chain, OfflineRenderer& renderer,
                                    const float* const* in, int numChannels, long long inFrames,
                                    float* const* out, long long outFrames,
                                    const ProgressCallback& progress = ProgressCallback {}) noexcept
    {
        renderPhase_ = RenderPhase::Idle;
        if (! admits (chain, numChannels, inFrames, outFrames)) return false;
        if (renderer.blockSize() < 1 || numChannels > renderer.maxChannels()) return false;
        // THE RULE AT EVERY LENGTH, AN EMPTY PROGRAMME INCLUDED. It used to be skipped at `outFrames == 0`, and the
        // path then ran past it into `ro[c] = out[c]` below: a null table crashed a call whose programme is legal.
        // At two zero lengths the rule judges nothing but null — no span has a byte to overlap with — so an empty
        // programme in real tables is rendered as before, and one without tables is refused.
        if (! planesUsable (in, out, numChannels, inFrames, outFrames)) return false;
        clock_ = ProgressClock (progress);
        if (! conv_.begin (in, numChannels, inFrames, out, outFrames, &clock_)) return false;
        renderOut_ = out; renderFrames_ = (int) outFrames; renderChannels_ = numChannels;
        for (int c = 0; c < numChannels; ++c) renderIn_[c] = out[c];
        renderPhase_ = RenderPhase::Convert;
        return true;
    }

    [[nodiscard]] StepResult stepRender (MasteringChain& chain, OfflineRenderer& renderer, long long budget) noexcept
    {
        if (budget < 0 || renderPhase_ == RenderPhase::Idle) return StepResult::Failed;
        if (renderPhase_ == RenderPhase::Done) return StepResult::Done;
        if (budget == 0) return StepResult::More;
        if (renderPhase_ == RenderPhase::Convert)
        {
            const long long before = conv_.workFrames();
            const StepResult result = conv_.step (budget, &clock_);
            if (result == StepResult::Failed) return result;
            budget -= conv_.workFrames() - before;
            if (result == StepResult::More) return StepResult::More;
            renderPhase_ = RenderPhase::StartRenderer;
        }
        if (renderPhase_ == RenderPhase::StartRenderer)
        {
            if (budget == 0) return StepResult::More;
            if (! conv_.finish (&clock_)) return StepResult::Failed;
            nonFinite_ = conv_.nonFiniteInputSamples();
            if (! clock_.begin (ProgressStage::Render, 0, 0, (long long) renderFrames_ + chain.latencySamples(), renderFrames_))
                return StepResult::Failed;
            MasteringChainTaps none;
            if (! renderer.begin (chain, renderIn_, renderOut_, renderChannels_, renderFrames_, none)) return StepResult::Failed;
            renderPhase_ = RenderPhase::Render;
            --budget;
        }
        if (budget == 0) return StepResult::More;
        if (renderPhase_ == RenderPhase::Render)
        {
            MasteringChainTaps none;
            const long long before = renderer.processedFrames();
            const StepResult result = renderer.step (chain, none, NullTapSink {}, budget, renderFrames_, &clock_);
            if (result == StepResult::Failed) return result;
            budget -= renderer.processedFrames() - before;
            if (result == StepResult::More) return result;
            renderPhase_ = RenderPhase::FinishRenderer;
        }
        if (budget == 0) return StepResult::More;
        if (! renderer.finish() || ! clock_.finish()) return StepResult::Failed;
        renderPhase_ = RenderPhase::Done;
        return StepResult::Done;
    }

    [[nodiscard]] bool finishRender() noexcept
    {
        if (renderPhase_ != RenderPhase::Done) return false;
        renderPhase_ = RenderPhase::Idle;
        return true;
    }

    void cancelRender (OfflineRenderer& renderer) noexcept
    {
        conv_.cancel(); renderer.cancel(); renderPhase_ = RenderPhase::Idle;
    }

    // A complete render job includes preparation. Its first call only validates
    // geometry and borrows the planes; each preparation stage costs one unit.
    // Rendering then spends one unit per source or chain frame.
    [[nodiscard]] bool beginJob (MasteringChain& chain, OfflineRenderer& renderer,
                                 double sourceRate, double deliveryRate, int numChannels,
                                 long long inFrames, const MasteringChainConfig& config,
                                 const MasteringChainParams& params, int block,
                                 const float* const* in, float* const* out, long long outFrames,
                                 const ProgressCallback& progress = ProgressCallback {}) noexcept
    {
        cancelJob();
        const RenderStorage st = storageFor (sourceRate, deliveryRate, numChannels, inFrames, config, block);
        if (! st.ok || outFrames != deliveredFrames (sourceRate, deliveryRate, inFrames)
            || ! planesUsable (in, out, numChannels, inFrames, outFrames)) return false;
        jobChain_ = &chain; jobRenderer_ = &renderer;
        jobSourceRate_ = sourceRate; jobDeliveryRate_ = deliveryRate;
        jobChannels_ = numChannels; jobInFrames_ = inFrames; jobOutFrames_ = outFrames; jobBlock_ = block;
        jobConfig_ = config; jobParams_ = params; jobIn_ = in; jobOut_ = out; jobProgress_ = progress;
        jobState_ = JobState::PrepareChain;
        return true;
    }

    [[nodiscard]] StepResult stepJob (long long budget)
    {
        if (budget < 0 || jobState_ == JobState::Idle) return StepResult::Failed;
        if (jobState_ == JobState::Done) return StepResult::Done;
        if (budget == 0) return StepResult::More;
        while (budget > 0)
        {
            switch (jobState_)
            {
            case JobState::PrepareChain:
                if (! jobChain_->prepare (jobDeliveryRate_, jobChannels_, jobConfig_)) return StepResult::Failed;
                jobState_ = JobState::PrepareRenderer; --budget; break;
            case JobState::PrepareRenderer:
                if (! jobRenderer_->prepare (jobChannels_, jobBlock_)) return StepResult::Failed;
                jobState_ = JobState::PrepareConverter; --budget; break;
            case JobState::PrepareConverter:
                if (! prepare (jobSourceRate_, jobDeliveryRate_, jobChannels_, jobBlock_)) return StepResult::Failed;
                jobState_ = JobState::StartRender; --budget; break;
            case JobState::StartRender:
                jobChain_->setParams (jobParams_);
                if (! beginRender (*jobChain_, *jobRenderer_, jobIn_, jobChannels_, jobInFrames_,
                                   jobOut_, jobOutFrames_, jobProgress_)) return StepResult::Failed;
                jobState_ = JobState::Render; --budget; break;
            case JobState::Render:
            {
                const StepResult result = stepRender (*jobChain_, *jobRenderer_, budget);
                if (result == StepResult::Done) jobState_ = JobState::Done;
                return result;
            }
            case JobState::Done: return StepResult::Done;
            case JobState::Idle: return StepResult::Failed;
            }
        }
        return StepResult::More;
    }

    [[nodiscard]] bool finishJob() noexcept
    {
        if (jobState_ != JobState::Done || ! finishRender()) return false;
        jobState_ = JobState::Idle;
        jobChain_ = nullptr; jobRenderer_ = nullptr;
        return true;
    }

    void cancelJob() noexcept
    {
        if (jobState_ != JobState::Idle && jobRenderer_ != nullptr) cancelRender (*jobRenderer_);
        jobState_ = JobState::Idle;
        jobChain_ = nullptr; jobRenderer_ = nullptr;
    }

    // The loudness search over the delivered programme. Refusals of this class's own are the solver's verdict
    // type and the solver's words for them: a converter that is not ready is `NotPrepared`, a length, width,
    // rate or buffer that does not fit is `InvalidRequest` — the same two a caller already has to read.
    LoudnessSolution solve (TargetLoudnessSolver& solver, MasteringChain& chain, OfflineRenderer& renderer,
                            const MasteringChainParams& params,
                            const float* const* in, int numChannels, long long inFrames,
                            float* const* out, long long outFrames, const LoudnessRequest& req)
    {
        return solve (solver, chain, renderer, params, in, numChannels, inFrames, out, outFrames, req, ProgressCallback {});
    }

    LoudnessSolution solve (TargetLoudnessSolver& solver, MasteringChain& chain, OfflineRenderer& renderer,
                            const MasteringChainParams& params,
                            const float* const* in, int numChannels, long long inFrames,
                            float* const* out, long long outFrames, const LoudnessRequest& req,
                            const ProgressCallback& progress)
    {
        const auto refuse = [&] (MasteringSolveStatus status)
        {
            LoudnessSolution result;
            result.activityThresholdDb = req.activityThresholdDb;
            result.status = status;
            return result;
        };
        MasteringSolveStatus why = MasteringSolveStatus::InvalidRequest;
        if (! prepared_) return refuse (MasteringSolveStatus::NotPrepared);
        if (! admits (chain, numChannels, inFrames, outFrames))
            return refuse (MasteringSolveStatus::InvalidRequest);
        // AN EMPTY PROGRAMME IS THE SOLVER'S TO ANSWER, not this class's: it answers `InvalidRequest` before any
        // pass, and answering for it here would be a second policy for one question.
        if (outFrames == 0)
            return solver.solve (chain, renderer, params, in, out, numChannels, 0, req, progress);
        // THE SOLVER'S OWN VERDICT, ASKED BEFORE A BYTE IS SPENT. Its words, its order, one definition.
        if (! solver.admits (chain, renderer, numChannels, (int) outFrames, req, why)) return refuse (why);
        // The planes, at the CALLER's two lengths. An overlap would make a later
        // search pass read its own previous master.
        if (! planesUsable (in, out, numChannels, inFrames, outFrames))
            return refuse (MasteringSolveStatus::InvalidRequest);

        if (! identity_)
            return solver.solveDelivered (chain, renderer, params, conv_, in, inFrames,
                                          out, numChannels, (int) outFrames, req, progress, &nonFinite_);
        const float* src[core::kMaxChannels] {};
        storage::Buffer<float> programme;
        ProgressClock clock (progress);
        if (! converted (in, numChannels, inFrames, outFrames, programme, src, clock))
            return refuse (clock.stopped() ? MasteringSolveStatus::Cancelled : MasteringSolveStatus::InvalidRequest);
        return solver.solve (chain, renderer, params, src, out, numChannels, (int) outFrames, req, progress);
    }

    // The input's loudness range, measured on the DELIVERED programme — the one the search will meter, so the
    // range constraint compares a programme with itself. False, having converted nothing, where the solver
    // would refuse a range for that length.
    [[nodiscard]] bool measureInputLoudnessRange (const TargetLoudnessSolver& solver, const float* const* in,
                                                  int numChannels, long long inFrames, double& out)
    {
        return measureInputLoudnessRange (solver, in, numChannels, inFrames, out, ProgressCallback {});
    }

    [[nodiscard]] bool measureInputLoudnessRange (const TargetLoudnessSolver& solver, const float* const* in,
                                                  int numChannels, long long inFrames, double& out,
                                                  const ProgressCallback& progress)
    {
        if (! prepared_ || ! solver.isPrepared()) return false;
        if (! (std::fabs (solver.sampleRate() - deliveryRate_) < 1.0e-9)) return false;   // meters at the delivery rate
        if (in == nullptr || numChannels != nch_ || inFrames <= 0) return false;
        for (int c = 0; c < numChannels; ++c) if (in[c] == nullptr) return false;
        const long long d = deliveredFrames (sourceRate_, deliveryRate_, inFrames);
        if (d <= 0 || d > INT_MAX) return false;
        if (TargetLoudnessSolver::measureRangeBytes (deliveryRate_, (int) d) == 0u) return false;
        const float* src[core::kMaxChannels] {};
        storage::Buffer<float> programme;
        ProgressClock clock (progress);
        if (! converted (in, numChannels, inFrames, d, programme, src, clock)) return false;
        return solver.measureInputLoudnessRange (src, numChannels, (int) d, out, progress);
    }

private:
    static std::uint64_t programmeBytes (double sourceRate, double deliveryRate, int numChannels, long long d) noexcept
    {
        core::DeliveryResampler::Params p;
        p.inRate = sourceRate; p.outRate = deliveryRate;
        if (core::DeliveryResampler::plan (p).identity) return 0u;       // the caller's input is read in place
        return (std::uint64_t) sizeof (float) * (std::uint64_t) numChannels * (std::uint64_t) d;
    }

    bool admits (const MasteringChain& chain, int numChannels, long long inFrames, long long outFrames) const noexcept
    {
        if (! prepared_ || numChannels != nch_ || inFrames < 0 || outFrames < 0 || outFrames > INT_MAX) return false;
        if (outFrames != deliveredFrames (sourceRate_, deliveryRate_, inFrames)) return false;
        // THE RATE IS CHECKED, as the solver checks its own: a chain at the source rate would play a converted
        // programme at the wrong speed and every number it reported would still look like a measurement.
        return chain.isPrepared() && chain.numChannels() == numChannels
            && std::fabs (chain.sampleRate() - deliveryRate_) < 1.0e-9;
    }

    // The programme at the delivery rate, in `src`. At equal rates that is the caller's own input, read in place.
    bool converted (const float* const* in, int numChannels, long long inFrames, long long outFrames,
                    storage::Buffer<float>& programme, const float** src, ProgressClock& clock)
    {
        if (identity_)
        {
            // Read in place; the chain's own gate replaces a bad sample one for one, so only the count is taken here.
            std::uint64_t bad = 0;
            for (int c = 0; c < numChannels; ++c)
            {
                src[c] = in[c];
                for (long long i = 0; i < inFrames; ++i) bad += std::isfinite (in[c][i]) ? 0u : 1u;
            }
            nonFinite_ = bad;
            return true;
        }
        programme.assign ((std::size_t) numChannels * (std::size_t) outFrames, 0.0f);
        float* dst[core::kMaxChannels] {};
        for (int c = 0; c < numChannels; ++c)
        {
            dst[c] = programme.data() + (std::size_t) c * (std::size_t) outFrames;
            src[c] = dst[c];
        }
        const bool ok = conv_.convert (in, numChannels, inFrames, dst, outFrames, clock);
        if (ok) nonFinite_ = conv_.nonFiniteInputSamples();         // a conversion that completed — see the getter
        return ok;
    }

    DeliveryConverter conv_;
    double sourceRate_ = 0.0, deliveryRate_ = 0.0;
    int nch_ = 0;
    std::uint64_t nonFinite_ = 0;
    bool identity_ = false, prepared_ = false;
    enum class RenderPhase { Idle, Convert, StartRenderer, Render, FinishRenderer, Done };
    RenderPhase renderPhase_ = RenderPhase::Idle;
    ProgressClock clock_ { ProgressCallback {} };
    const float* renderIn_[core::kMaxChannels] {};
    float* const* renderOut_ = nullptr;
    int renderFrames_ = 0, renderChannels_ = 0;
    enum class JobState { Idle, PrepareChain, PrepareRenderer, PrepareConverter, StartRender, Render, Done };
    JobState jobState_ = JobState::Idle;
    MasteringChain* jobChain_ = nullptr;
    OfflineRenderer* jobRenderer_ = nullptr;
    double jobSourceRate_ = 0.0, jobDeliveryRate_ = 0.0;
    int jobChannels_ = 0, jobBlock_ = 0;
    long long jobInFrames_ = 0, jobOutFrames_ = 0;
    MasteringChainConfig jobConfig_ {};
    MasteringChainParams jobParams_ {};
    const float* const* jobIn_ = nullptr;
    float* const* jobOut_ = nullptr;
    ProgressCallback jobProgress_ {};
};

// A mastering instance owns a solver and a delivery converter even before either is prepared.
// A delivery rate of 0 selects the plain chain. Refused geometry constructs no instance, so the
// verdict stays 0; every admitted create includes all four core objects' construction here.
[[nodiscard]] inline std::uint64_t createInstanceBytes (double sampleRate, double deliveryRate, int numChannels,
                                                        const MasteringChainConfig& config, int rendererBlock) noexcept
{
    // Only zero selects a plain chain; a NaN still reaches the delivery plan's refusal.
    const bool plain = deliveryRate <= 0.0 && deliveryRate >= 0.0;
    const auto prepared = plain
        ? createBytes (sampleRate, numChannels, config, rendererBlock)
        : DeliveredMastering::createBytes (sampleRate, deliveryRate, numChannels, config, rendererBlock);
    return prepared == 0u ? 0u : prepared + TargetLoudnessSolver::constructBytes() + DeliveryConverter::constructBytes();
}

} // namespace felitronics::mastering
