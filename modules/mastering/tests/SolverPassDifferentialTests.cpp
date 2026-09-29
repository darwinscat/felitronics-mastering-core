// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/mastering/LoudnessSolver.h>
#include <felitronics_test.h>
#include <alloc_counter.h>

#include <climits>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace felitronics::mastering
{
struct SolverPassDifferential
{
    static bool sameDouble (double a, double b) noexcept
    { return std::memcmp (&a, &b, sizeof (a)) == 0; }

    static bool sameStats (const GainReductionStats& a, const GainReductionStats& b) noexcept
    {
        return sameDouble (a.meanDb, b.meanDb) && sameDouble (a.p95Db, b.p95Db)
            && sameDouble (a.maxDb, b.maxDb) && sameDouble (a.activeFraction, b.activeFraction)
            && a.frames == b.frames && a.nonFinite == b.nonFinite && a.aboveRange == b.aboveRange
            && a.valid == b.valid && sameDouble (a.quantileDb, b.quantileDb)
            && sameDouble (a.quantileQ, b.quantileQ);
    }

    static bool sameTrace (const GainReductionTrace& a, const GainReductionTrace& b) noexcept
    {
        if (a.buckets != b.buckets || a.valid != b.valid || a.samples != b.samples
            || a.nonFinite != b.nonFinite || a.bucket.size() != b.bucket.size()) return false;
        for (std::size_t i = 0; i < a.bucket.size(); ++i)
        {
            const auto& x = a.bucket[i]; const auto& y = b.bucket[i];
            if (! sameDouble (x.maxDb, y.maxDb) || ! sameDouble (x.meanDb, y.meanDb)
                || x.samples != y.samples || x.nonFinite != y.nonFinite) return false;
        }
        return true;
    }

    static bool sameMeasurement (const MasterMeasurement& a, const MasterMeasurement& b) noexcept
    {
#define SAME(field) sameDouble (a.field, b.field)
        const bool doubles = SAME (integratedLufs) && SAME (truePeakDbTp) && SAME (samplePeakDb)
            && SAME (loudnessRangeLu) && SAME (plrDb) && SAME (limiterMaxReconstructedPeakDb)
            && SAME (peakClipReductionMaxDb) && SAME (peakClipReductionP95Db)
            && SAME (peakClipOccupancy) && SAME (airMidEnergy) && SAME (airSideEnergyBefore)
            && SAME (airSideEnergyAfter) && SAME (airWidthBefore) && SAME (airWidthAfter);
#undef SAME
        return doubles && sameStats (a.compressor, b.compressor) && sameStats (a.limiter, b.limiter)
            && a.peakClipRuns == b.peakClipRuns && a.peakClipRunSamplesTotal == b.peakClipRunSamplesTotal
            && a.peakClipLongestRunSamples == b.peakClipLongestRunSamples
            && a.airJudgedSamples == b.airJudgedSamples && a.latencySamples == b.latencySamples
            && a.gatingBlocks == b.gatingBlocks && a.droppedBlocks == b.droppedBlocks
            && a.nonFiniteSubHops == b.nonFiniteSubHops && a.loudnessValid == b.loudnessValid
            && a.lraValid == b.lraValid;
    }

    static bool sameSolutionReadings (const LoudnessSolution& a, const LoudnessSolution& b) noexcept
    {
        if (! sameTrace (a.compressorTrace, b.compressorTrace)
            || ! sameTrace (a.limiterTrace, b.limiterTrace)
            || a.bandGrAbsence != b.bandGrAbsence || a.bandGr.size() != b.bandGr.size()
            || ! sameStats (a.limiterActive.stats, b.limiterActive.stats)
            || a.limiterActive.windows != b.limiterActive.windows
            || a.limiterActive.activeWindows != b.limiterActive.activeWindows
            || ! sameDouble (a.limiterActive.thresholdDb, b.limiterActive.thresholdDb)) return false;
        for (std::size_t i = 0; i < a.bandGr.size(); ++i)
        {
            const auto& x = a.bandGr[i]; const auto& y = b.bandGr[i];
            if (x.band != y.band || x.lane != y.lane || ! sameStats (x.whole, y.whole)
                || ! sameStats (x.active.stats, y.active.stats)
                || x.active.windows != y.active.windows || x.active.activeWindows != y.active.activeWindows
                || ! sameDouble (x.active.thresholdDb, y.active.thresholdDb)
                || ! sameTrace (x.trace, y.trace)) return false;
        }
        for (double q : { 0.05, 0.50, 0.95, 1.0 })
            for (GrStage stage : { GrStage::Compressor, GrStage::Limiter })
            {
                double x = 0.0, y = 0.0;
                const bool ax = a.grQuantile (stage, q, x), by = b.grQuantile (stage, q, y);
                if (ax != by || (ax && ! sameDouble (x, y))) return false;
            }
        return true;
    }

    // The conversion loop that preceded DeliveryConverter::begin/step. The
    // complete solver pass below is the retained whole-call implementation.
    static bool previousConvert (double sourceRate, double deliveryRate, int channels,
                                 int sourceFrames, const float* const* source,
                                 float* const* out, int frames)
    {
        constexpr int block = 257;
        core::DeliveryResampler src;
        core::DeliveryResampler::Params params;
        params.inRate = sourceRate; params.outRate = deliveryRate;
        if (! src.prepare (params, channels, block)) return false;
        const auto& plan = src.currentPlan();
        int perCall = block;
        if (! plan.identity)
            for (int s = 0; s < plan.count; ++s)
                perCall = perCall * plan.stage[s].L / plan.stage[s].M + 1;
        std::vector<float> staging ((std::size_t) channels * (std::size_t) perCall);
        std::vector<float> gated ((std::size_t) channels * block);
        std::vector<float> silence ((std::size_t) channels * block, 0.0f);
        float* sp[core::kMaxChannels] {};
        const float* zp[core::kMaxChannels] {};
        for (int c = 0; c < channels; ++c)
        {
            sp[c] = staging.data() + (std::size_t) c * (std::size_t) perCall;
            zp[c] = silence.data() + (std::size_t) c * block;
        }
        src.reset();
        const long long trim = plan.identity ? 0 : std::llround (src.latencyOutputSamples());
        long long emitted = 0;
        const auto take = [&] (int got)
        {
            for (int k = 0; k < got; ++k, ++emitted)
            {
                const long long n = emitted - trim;
                if (n < 0 || n >= frames) continue;
                for (int c = 0; c < channels; ++c) out[c][n] = sp[c][k];
            }
        };
        for (int off = 0; off < sourceFrames; )
        {
            const int m = std::min (block, sourceFrames - off);
            const float* ip[core::kMaxChannels] {};
            for (int c = 0; c < channels; ++c)
            {
                float* g = gated.data() + (std::size_t) c * block;
                for (int i = 0; i < m; ++i)
                {
                    const float v = source[c][off + i];
                    g[i] = std::clamp (std::isfinite (v) ? v : 0.0f, -1.0e6f, 1.0e6f);
                }
                ip[c] = g;
            }
            int got = 0;
            if (! src.process (ip, channels, m, sp, perCall, got)) return false;
            take (got); off += m;
        }
        for (long long guard = 0; emitted < trim + frames; ++guard)
        {
            if (guard > 64 + (trim + frames) / block) return false;
            int got = 0;
            if (! src.process (zp, channels, block, sp, perCall, got)) return false;
            take (got);
        }
        return true;
    }

    static bool boundedFinishAndTraceReuse (bool& gatesBounded, bool& tracesReused)
    {
        gatesBounded = false;
        tracesReused = true;
        constexpr int rate = 48000, frames = 4 * rate, block = 257;
        MasteringChain chain;
        OfflineRenderer renderer;
        TargetLoudnessSolver solver;
        MasteringChainConfig config;
        if (! chain.prepare (rate, 1, config) || ! renderer.prepare (1, block)
            || ! solver.prepare (rate, 1, block, chain.internalBlock(), chain.tapOversampleFactor())) return false;
        MasteringChainParams params;
        auto& band = params.eqBands[0];
        band.on = true; band.type = eq::FilterType::Bell;
        band.lanes[0].on = true; band.lanes[0].freq = 6700.0;
        band.lanes[0].Q = 1.5; band.dyn.on = true; band.dyn.rangeDb = -6.0;
        band.dyn.thrAuto = true;
        std::vector<float> source (frames), output (frames);
        for (int i = 0; i < frames; ++i)
            source[(std::size_t) i] = (float) (0.2 * std::sin (0.017 * i)
                + (i % 5003 == 0 ? 0.7 : 0.0));
        const float* input[1] { source.data() };
        float* out[1] { output.data() };
        LoudnessRequest request;
        request.targetLufs = -14.0; request.maxTruePeakDbTp = -1.0;
        MasterMeasurement measured;
        LoudnessSolution solution;
        ProgressClock clock (ProgressCallback {});
        using Phase = TargetLoudnessSolver::PassWorkspace::Phase;
        bool gateBounded = false, rangeBounded = false;
        for (int pass = 0; pass < 12; ++pass)
        {
            chain.setParams (params);
            if (! clock.begin (ProgressStage::SearchPass, pass + 1, 12, 2LL * frames, frames)
                || ! solver.beginPass (chain, renderer, params, input, out, 1, frames,
                                       request, measured, solution, clock, true)) return false;
            const long long before = test::alloc::count.load();
            StepResult state = StepResult::More;
            for (int guard = 0; state == StepResult::More && guard < 500000; ++guard)
            {
                auto& workspace = *solver.pass_;
                long long budget = LLONG_MAX;
                if (pass == 0)
                {
                    if (workspace.phase == Phase::Render)
                        budget = std::max (1LL, (long long) frames + chain.latencySamples()
                                              - renderer.processedFrames() - 1);
                    else if (workspace.phase == Phase::Meter)
                        budget = std::max (1LL, (long long) frames - workspace.measured - 1);
                    else if (workspace.phase == Phase::FinishGate || workspace.phase == Phase::FinishRange)
                        budget = 1;
                    else budget = 1;
                }
                const Phase phase = workspace.phase;
                state = solver.stepPass (budget);
                if (pass == 0 && phase == Phase::FinishGate && workspace.lm.gatingBlockCount() > 1)
                    gateBounded = gateBounded || workspace.phase == Phase::FinishGate;
                if (pass == 0 && phase == Phase::FinishRange && workspace.lm.shortTermCount() > 1)
                    rangeBounded = rangeBounded || workspace.phase == Phase::FinishRange;
            }
            if (state != StepResult::Done) return false;
            if (pass >= 2 && test::alloc::count.load() != before) tracesReused = false;
        }
        gatesBounded = gateBounded && rangeBounded;
        return true;
    }

    static bool maximumTraceCursors()
    {
        constexpr int rate = 48000, frames = 96000, block = 257, buckets = 65536;
        MasteringChain oldChain, chain;
        OfflineRenderer oldRenderer, renderer;
        TargetLoudnessSolver oldSolver, solver;
        MasteringChainConfig config;
        if (! oldChain.prepare (rate, 1, config) || ! chain.prepare (rate, 1, config)
            || ! oldRenderer.prepare (1, block) || ! renderer.prepare (1, block)
            || ! oldSolver.prepare (rate, 1, block, oldChain.internalBlock(), oldChain.tapOversampleFactor())
            || ! solver.prepare (rate, 1, block, chain.internalBlock(), chain.tapOversampleFactor())) return false;
        std::vector<float> source (frames), oldOutput (frames), output (frames);
        for (int i = 0; i < frames; ++i)
            source[(std::size_t) i] = float (0.16 * std::sin (0.017 * i)
                + (i % 499 == 0 ? 0.4 : 0.0));
        const float* input[1] { source.data() };
        float* oldOut[1] { oldOutput.data() };
        float* out[1] { output.data() };
        MasteringChainParams params;
        oldChain.setParams (params); chain.setParams (params);
        LoudnessRequest request;
        request.targetLufs = -14.0; request.maxTruePeakDbTp = -1.0;
        request.grTraceBuckets = buckets;
        MasterMeasurement oldMeasure, measured;
        LoudnessSolution oldSolution, solution;
        ProgressClock oldClock (ProgressCallback {}), clock (ProgressCallback {});
        if (! oldClock.begin (ProgressStage::SearchPass, 1, 1, 2LL * frames, frames)
            || ! clock.begin (ProgressStage::SearchPass, 1, 1, 2LL * frames, frames)
            || ! oldSolver.legacyRenderPass (oldChain, oldRenderer, params, input, oldOut, 1,
                                            frames, request, oldMeasure, oldSolution, oldClock)
            || ! solver.beginPass (chain, renderer, params, input, out, 1, frames,
                                  request, measured, solution, clock, true)) return false;
        using Phase = TargetLoudnessSolver::PassWorkspace::Phase;
        unsigned initSteps = 0, finishSteps = 0;
        StepResult state = StepResult::More;
        for (unsigned guard = 0; guard < 20000 && state == StepResult::More; ++guard)
        {
            auto& p = *solver.pass_;
            const auto phase = p.phase;
            const auto initialized = p.compTrace ? p.compTrace->beginWorkUnits()
                + p.limTrace->beginWorkUnits() + p.clipTrace->beginWorkUnits() : 0u;
            const auto finished = p.compTrace ? p.compTrace->finishedBuckets()
                + p.limTrace->finishedBuckets() + p.clipTrace->finishedBuckets() : 0u;
            state = solver.stepPass (1024);
            if (phase == Phase::TraceInit)
            {
                const auto after = p.compTrace->beginWorkUnits()
                    + p.limTrace->beginWorkUnits() + p.clipTrace->beginWorkUnits();
                if (after < initialized || after - initialized > 1024) return false;
                ++initSteps;
            }
            if (phase == Phase::FinishStats)
            {
                const auto after = p.compTrace->finishedBuckets()
                    + p.limTrace->finishedBuckets() + p.clipTrace->finishedBuckets();
                if (after < finished || after - finished > 1024) return false;
                ++finishSteps;
            }
        }
        return state == StepResult::Done && initSteps > 1 && finishSteps > 1
            && std::memcmp (oldOutput.data(), output.data(), output.size() * sizeof (float)) == 0
            && sameMeasurement (oldMeasure, measured)
            && sameSolutionReadings (oldSolution, solution);
    }

    static bool run (bool sliced)
    {
        constexpr int rate = 48000, frames = 4 * rate, channels = 2;
        constexpr int block = 257;
        MasteringChainConfig config;
        MasteringChain oldChain, newChain;
        OfflineRenderer oldRenderer, newRenderer;
        TargetLoudnessSolver oldSolver, newSolver;
        if (! oldChain.prepare (rate, channels, config) || ! newChain.prepare (rate, channels, config)
            || ! oldRenderer.prepare (channels, block) || ! newRenderer.prepare (channels, block)
            || ! oldSolver.prepare (rate, channels, block, oldChain.internalBlock(), oldChain.tapOversampleFactor())
            || ! newSolver.prepare (rate, channels, block, newChain.internalBlock(), newChain.tapOversampleFactor())) return false;
        std::vector<float> source ((std::size_t) channels * frames);
        std::vector<float> oldOutput (source.size()), newOutput (source.size());
        const float* input[channels] { source.data(), source.data() + frames };
        float* oldPlanes[channels] { oldOutput.data(), oldOutput.data() + frames };
        float* newPlanes[channels] { newOutput.data(), newOutput.data() + frames };
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
            {
                const double t = (double) i / rate;
                float sample = (float) (0.13 * std::sin (6.283185307179586 * (97.0 + c) * t)
                    + 0.06 * std::sin (6.283185307179586 * 3107.0 * t));
                if (i % 5003 == 0) sample += c == 0 ? 0.71f : -0.63f;
                source[(std::size_t) c * frames + (std::size_t) i] = sample;
            }
        LoudnessRequest request; request.targetLufs = -12.0; request.maxTruePeakDbTp = -1.0;
        for (int sequence = 0; sequence < 4; ++sequence)
        {
            MasteringChainParams params;
            params.preLimiterGainDb = (double) sequence * 2.75 - 2.0;
            params.limiter.ceilingDbTp = -1.0 - (double) (sequence % 2) * 1.5;
            params.compressor.thresholdDb = -21.0; params.compressor.ratio = 2.0;
            oldChain.setParams (params); newChain.setParams (params);
            MasterMeasurement oldMeasure, newMeasure;
            LoudnessSolution oldSolution, newSolution;
            ProgressClock oldClock (ProgressCallback {}), newClock (ProgressCallback {});
            if (! oldClock.begin (ProgressStage::SearchPass, sequence + 1, 4, 2LL * frames, frames)
                || ! newClock.begin (ProgressStage::SearchPass, sequence + 1, 4, 2LL * frames, frames)) return false;
            const bool oldDone = oldSolver.legacyRenderPass (oldChain, oldRenderer, params, input, oldPlanes,
                channels, frames, request, oldMeasure, oldSolution, oldClock);
            if (! newSolver.beginPass (newChain, newRenderer, params, input, newPlanes,
                                      channels, frames, request, newMeasure, newSolution, newClock)) return false;
            StepResult state = StepResult::More;
            for (int guard = 0; guard < 1000000 && state == StepResult::More; ++guard)
                state = newSolver.stepPass (sliced ? 73 : LLONG_MAX);
            if (! oldDone || state != StepResult::Done
                || std::memcmp (oldOutput.data(), newOutput.data(), oldOutput.size() * sizeof (float)) != 0
                || ! sameMeasurement (oldMeasure, newMeasure)
                || ! sameSolutionReadings (oldSolution, newSolution)) return false;
        }
        return true;
    }

    static bool runCase (int sourceRate, int deliveryRate, int sourceFrames,
                         long long cut, bool armed)
    {
        constexpr int channels = 2, block = 257;
        const int frames = (int) DeliveryConverter::deliveredFrames (sourceRate, deliveryRate, sourceFrames);
        if (frames <= 0) return false;
        MasteringChainConfig config;
        MasteringChain oldChain, newChain;
        OfflineRenderer oldRenderer, newRenderer;
        TargetLoudnessSolver oldSolver, newSolver;
        DeliveryConverter converter;
        if (! oldChain.prepare (deliveryRate, channels, config)
            || ! newChain.prepare (deliveryRate, channels, config)
            || ! oldRenderer.prepare (channels, block) || ! newRenderer.prepare (channels, block)
            || ! oldSolver.prepare (deliveryRate, channels, block,
                                    oldChain.internalBlock(), oldChain.tapOversampleFactor())
            || ! newSolver.prepare (deliveryRate, channels, block,
                                    newChain.internalBlock(), newChain.tapOversampleFactor())
            || (sourceRate != deliveryRate
                && ! converter.prepare (sourceRate, deliveryRate, channels, block))) return false;
        std::vector<float> source ((std::size_t) channels * (std::size_t) sourceFrames);
        std::vector<float> converted ((std::size_t) channels * (std::size_t) frames);
        std::vector<float> oldOutput (converted.size()), newOutput (converted.size());
        const float* input[channels] { source.data(), source.data() + sourceFrames };
        float* convertedPlanes[channels] { converted.data(), converted.data() + frames };
        const float* previousInput[channels] { converted.data(), converted.data() + frames };
        float* oldPlanes[channels] { oldOutput.data(), oldOutput.data() + frames };
        float* newPlanes[channels] { newOutput.data(), newOutput.data() + frames };
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < sourceFrames; ++i)
                source[(std::size_t) c * sourceFrames + (std::size_t) i] =
                    (float) (0.15 * std::sin (0.017 * i + c)
                        + 0.04 * std::sin (0.31 * i) + (i % 499 == 0 ? 0.6 : 0.0));
        source[(std::size_t) sourceFrames - 1] = 0.95f;
        source.back() = -0.93f; // the drain and true-peak read the final impulse
        if (! previousConvert (sourceRate, deliveryRate, channels, sourceFrames,
                               input, convertedPlanes, frames)) return false;
        MasteringChainParams params;
        params.preLimiterGainDb = 3.5;
        params.limiter.ceilingDbTp = -1.1;
        if (armed)
        {
            auto& band = params.eqBands[0];
            band.on = true; band.type = eq::FilterType::Bell;
            band.lanes[0].on = true; band.lanes[0].freq = 6700.0;
            band.lanes[0].Q = 1.5; band.dyn.on = true;
            band.dyn.rangeDb = -6.0; band.dyn.thrAuto = true;
        }
        oldChain.setParams (params); newChain.setParams (params);
        LoudnessRequest request;
        request.targetLufs = -14.0; request.maxTruePeakDbTp = -1.0;
        MasterMeasurement oldMeasure, newMeasure;
        LoudnessSolution oldSolution, newSolution;
        ProgressClock oldClock (ProgressCallback {}), newClock (ProgressCallback {});
        if (! oldClock.begin (ProgressStage::SearchPass, 1, 12, 2LL * frames, frames)
            || ! newClock.begin (ProgressStage::SearchPass, 1, 12,
                2LL * frames + sourceFrames, frames)) return false;
        if (! oldSolver.legacyRenderPass (oldChain, oldRenderer, params, previousInput, oldPlanes,
                                          channels, frames, request, oldMeasure, oldSolution, oldClock)) return false;
        if (sourceRate != deliveryRate)
        {
            newSolver.deliveryConverter_ = &converter;
            newSolver.deliverySource_ = input;
            newSolver.deliveryFrames_ = sourceFrames;
        }
        if (! newSolver.beginPass (newChain, newRenderer, params,
                                  sourceRate == deliveryRate ? input : previousInput,
                                  newPlanes, channels, frames, request, newMeasure, newSolution, newClock)) return false;
        StepResult state = StepResult::More;
        constexpr long long cuts[] { 1, 256, 2, 257, 3, 17, 258, 73, 509 };
        for (int guard = 0; state == StepResult::More && guard < 2000000; ++guard)
            state = newSolver.stepPass (cut > 0 ? cut : cuts[guard % 9]);
        if (state != StepResult::Done || std::memcmp (oldOutput.data(), newOutput.data(),
                                                       oldOutput.size() * sizeof (float)) != 0
            || ! sameMeasurement (oldMeasure, newMeasure)
            || ! sameSolutionReadings (oldSolution, newSolution)) return false;
        // The final sample and a late measurement must be live in the comparator.
        newOutput.back() = 0.0f;
        if (std::memcmp (oldOutput.data(), newOutput.data(),
                         oldOutput.size() * sizeof (float)) == 0) return false;
        MasterMeasurement changed = newMeasure;
        changed.airMidEnergy += 1.0;
        return ! sameMeasurement (oldMeasure, changed);
    }

    static bool extendedCoverage()
    {
        for (const auto [sourceRate, deliveryRate] :
             { std::pair<int, int> { 48000, 48000 }, { 44100, 48000 }, { 96000, 44100 } })
        {
            for (int frames : { 1, 17, 257 })
                if (! runCase (sourceRate, deliveryRate, frames, -1, true)) return false;
            for (long long cut : { 1LL, 73LL, 256LL, 257LL, 258LL, LLONG_MAX, -1LL })
                if (! runCase (sourceRate, deliveryRate, 3 * sourceRate + 1, cut, true)) return false;
        }
        return true;
    }
};
} // namespace felitronics::mastering

int main()
{
    felitronics::test::group ("saved solver pass versus the previous complete render and meter");
    felitronics::test::ok (felitronics::mastering::SolverPassDifferential::run (false),
                          "four successive parameter sets preserve exact PCM, loudness, TP, LRA, and limiter trace");
    felitronics::test::ok (felitronics::mastering::SolverPassDifferential::run (true),
                          "the same sequence stays exact when each pass resumes in small work slices");
    bool gatesBounded = false, tracesReused = false;
    const bool finishRan = felitronics::mastering::SolverPassDifferential::boundedFinishAndTraceReuse (
        gatesBounded, tracesReused);
    felitronics::test::ok (finishRan && gatesBounded,
                          "gates and range scan one unit at a time");
    felitronics::test::ok (finishRan && tracesReused,
                          "armed dynamic EQ traces allocate only during warmup across twelve passes");
    felitronics::test::ok (felitronics::mastering::SolverPassDifferential::extendedCoverage(),
                          "previous whole path matches both SRC directions, armed EQ, short tails and adversarial cuts");
    felitronics::test::ok (felitronics::mastering::SolverPassDifferential::maximumTraceCursors(),
                          "maximum trace initialization and finish yield and match the previous whole path bit for bit");
    return felitronics::test::report();
}
