// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/limiter/TruePeakLimiter.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include "PreviousTruePeakLimiter.h"
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>

using namespace felitronics;

int main()
{
    test::group ("K13 tap uses the previous limiter audio path");
    for (bool clip : { false, true })
        for (bool observe : { false, true })
            for (int partition : { 1, 2, 3 })
            {
                limiter::TruePeakLimiter now;
                previous_limiter::TruePeakLimiter old;
                limiter::TruePeakLimiterConfig nc;
                previous_limiter::TruePeakLimiterConfig oc;
                nc.oversampleFactor = oc.oversampleFactor = 4;
                test::ok (now.prepare (48000.0, 73, 1, nc) && old.prepare (48000.0, 73, 1, oc),
                          "both renderers prepare");
                limiter::TruePeakLimiterParams np;
                previous_limiter::TruePeakLimiterParams op;
                np.ceilingDbTp = op.ceilingDbTp = -1.0;
                np.peakClip = op.peakClip = clip;
                np.overCeilingDb = op.overCeilingDb = 0.0;
                now.setParams (np); old.setParams (op);
                std::vector<float> a (2111), b (2111), trace (2111 * 4, -99.0f);
                for (std::size_t i = 0; i < a.size(); ++i)
                    a[i] = b[i] = i == 0 || i == 72 || i == 73 || i == 2110 ? 4.0f
                        : (i % 17 == 0 ? -2.0f : (float) std::sin ((double) i * 0.14) * 0.3f);
                const int cuts1[] { 2111 }, cuts2[] { 1, 72, 73, 74, 257, 1, 1633 }, cuts3[] { 71, 1, 1, 9, 203, 4, 17, 1805 };
                const int* cuts = partition == 1 ? cuts1 : partition == 2 ? cuts2 : cuts3;
                const int count = partition == 1 ? 1 : partition == 2 ? 7 : 8;
                int at = 0;
                for (int i = 0; i < count; ++i)
                {
                    const int n = std::min (cuts[i], 2111 - at);
                    float* ap[] { a.data() + at }; float* bp[] { b.data() + at };
                    limiter::TruePeakLimiterTap tap;
                    if (observe) { tap.peakClipReductionDb = trace.data() + (std::size_t) at * 4u; tap.capacity = n * 4; }
                    test::ok (now.process (ap, 1, n, tap) && old.process (bp, 1, n), "same calls accepted");
                    if (i == 0) { np.ceilingDbTp = op.ceilingDbTp = -2.0; now.setParams (np); old.setParams (op); }
                    at += n;
                }
                test::ok (at == 2111 && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0,
                          "PCM bit identity against independent pre-tap header across calls");
                test::ok (now.clipOsSamples() == old.clipOsSamples()
                          && now.clipRunCount() == old.clipRunCount()
                          && now.clipReductionMaxDb() == old.clipReductionMaxDb(), "K13 aggregates unchanged");
                if (observe)
                {
                    const auto peak = *std::max_element (trace.begin(), trace.end());
                    test::ok (clip ? peak > 0.0f && peak == (float) now.clipReductionMaxDb() : peak == 0.0f,
                              "tap follows the K13 reduction and bypass writes zero");
                }
                now.reset(); old.reset();
                np.ceilingDbTp = op.ceilingDbTp = -3.0;
                now.setParams (np); old.setParams (op);
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                a[2110] = b[2110] = 6.0f;
                for (const auto [start, n] : { std::pair { 0, 37 }, std::pair { 37, 2074 } })
                {
                    float* ap[] { a.data() + start }; float* bp[] { b.data() + start };
                    limiter::TruePeakLimiterTap tap;
                    if (observe) { tap.peakClipReductionDb = trace.data() + (std::size_t) start * 4u; tap.capacity = n * 4; }
                    test::ok (now.process (ap, 1, n, tap) && old.process (bp, 1, n), "repeat calls accepted");
                }
                test::ok (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0,
                          "reset and repeated final impulse stay bit identical to the old path");
            }
    test::group ("trace buckets retain minimum, completion and delivered frame grid");
    mastering::GainReductionTrace t;
    mastering::GainReductionTraceBuilder builder (t, 5, 2);
    builder.add (0, 3.0); builder.add (1, 1.0); builder.add (2, 2.0);
    builder.add (3, 4.0); builder.add (4, 0.0); builder.finish();
    test::ok (t.complete && t.valid && t.programmeFrames == 5 && t.bucket[0].samples == 2
              && t.bucket[0].minDb == 1.0 && t.bucket[0].maxDb == 3.0 && t.bucket[0].meanDb == 2.0
              && t.bucket[1].samples == 3 && t.bucket[1].minDb == 0.0 && t.bucket[1].meanDb == 2.0,
              "floor bucket boundaries and min/max/mean");
    test::group ("delivered limiter and K13 traces retain the final impulse on native and wasm");
    mastering::MasteringChain chain;
    mastering::MasteringChainConfig cfg;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    const bool prepared = chain.prepare (48000.0, 2, cfg) && renderer.prepare (2, 73)
        && solver.prepare (48000.0, 2, 73, chain.internalBlock(), chain.tapOversampleFactor());
    test::ok (prepared, "trace geometry prepares");
    if (! prepared) return test::report();
    std::vector<float> source (1026, 0.0f), delivered (1026, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        source[(std::size_t) c * 513u + 255u] = 4.0f;
        source[(std::size_t) c * 513u + 512u] = 4.0f;
    }
    const float* input[] { source.data(), source.data() + 513 };
    float* output[] { delivered.data(), delivered.data() + 513 };
    mastering::MasteringChainParams params;
    params.bypassCompressor = true; params.bypassDither = true;
    params.limiter.peakClip = true; params.limiter.overCeilingDb = 0.0;
    params.limiter.ceilingDbTp = -20.0;
    mastering::LoudnessRequest request;
    request.targetLufs = -14.0; request.maxTruePeakDbTp = -20.0;
    request.maxPasses = 1; request.grTraceBuckets = 513;
    const auto solution = solver.solve (chain, renderer, params, input, output, 2, 513, request);
    const auto& clip = solution.peakClipTrace;
    const auto& lim = solution.limiterTrace;
    test::ok (clip.complete && clip.valid && lim.complete && lim.valid
              && clip.programmeFrames == lim.programmeFrames && clip.programmeFrames == 513
              && clip.samples == lim.samples && clip.samples == 513u * (std::uint64_t) chain.tapOversampleFactor(),
              "both layers use the same complete delivered-frame grid");
    test::ok (clip.bucket[255].maxDb > 0.0 && clip.bucket[512].maxDb > 0.0,
              "step-boundary and final impulses are in their own K13 buckets");
    double max = 0.0;
    for (const auto& bucket : clip.bucket) max = std::max (max, bucket.maxDb);
    test::ok (max > 0.0 && max <= solution.measured.peakClipReductionMaxDb,
              "K13 row maxima agree with the preserved whole-render aggregate");
    std::uint64_t digest = 0xcbf29ce484222325ull;
    const auto word = [&digest] (std::uint64_t value)
    {
        for (unsigned i = 0; i < 8; ++i) digest = (digest ^ std::uint8_t (value >> (8u * i))) * 0x100000001b3ull;
    };
    for (float sample : delivered) word (std::bit_cast<std::uint32_t> (sample));
    for (const auto& trace : { &lim, &clip })
        for (const auto& bucket : trace->bucket)
        {
            word (std::bit_cast<std::uint64_t> (bucket.minDb));
            word (std::bit_cast<std::uint64_t> (bucket.maxDb));
            word (std::bit_cast<std::uint64_t> (bucket.meanDb));
            word (bucket.samples); word (bucket.nonFinite);
        }
    std::printf ("limiter-trace-digest=%016llx\n", static_cast<unsigned long long> (digest));
    return test::report();
}
