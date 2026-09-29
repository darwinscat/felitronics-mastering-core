// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/mastering/LoudnessSolver.h>
#include <felitronics_test.h>

#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

namespace felitronics::mastering
{
struct SolverPassDifferential
{
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
                || ! core::exactlyEqual (oldMeasure.integratedLufs, newMeasure.integratedLufs)
                || ! core::exactlyEqual (oldMeasure.truePeakDbTp, newMeasure.truePeakDbTp)
                || ! core::exactlyEqual (oldMeasure.loudnessRangeLu, newMeasure.loudnessRangeLu)
                || ! core::exactlyEqual (oldMeasure.limiter.meanDb, newMeasure.limiter.meanDb)
                || ! core::exactlyEqual (oldMeasure.limiter.maxDb, newMeasure.limiter.maxDb)
                || oldSolution.limiterTrace.bucket.size() != newSolution.limiterTrace.bucket.size()) return false;
            for (std::size_t i = 0; i < oldSolution.limiterTrace.bucket.size(); ++i)
            {
                const auto& a = oldSolution.limiterTrace.bucket[i];
                const auto& b = newSolution.limiterTrace.bucket[i];
                if (a.samples != b.samples || a.nonFinite != b.nonFinite
                    || ! core::exactlyEqual (a.meanDb, b.meanDb)
                    || ! core::exactlyEqual (a.maxDb, b.maxDb)) return false;
            }
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
    return felitronics::test::report();
}
