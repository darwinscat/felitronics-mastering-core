// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.

#include <felitronics/mastering/DeliveredMastering.h>
#include <felitronics_test.h>
#include <alloc_counter.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

using namespace felitronics;
using namespace felitronics::mastering;
using felitronics::test::group;
using felitronics::test::ok;

namespace
{
struct Audio
{
    std::vector<float> samples;
    const float* in[2] {};
    float* out[2] {};

    Audio (int channels, long long frames) : samples ((std::size_t) channels * (std::size_t) frames, -19.0f)
    {
        for (int c = 0; c < channels; ++c)
        {
            in[c] = samples.data() + (std::size_t) c * (std::size_t) frames;
            out[c] = samples.data() + (std::size_t) c * (std::size_t) frames;
        }
    }
};

MasteringChainConfig config()
{
    MasteringChainConfig c;
    c.eq = false; c.monoBass = false; c.compressor = true;
    c.clipper = false; c.limiter = true; c.dither = true;
    return c;
}

bool same (const Audio& a, const Audio& b)
{
    return a.samples.size() == b.samples.size()
        && std::memcmp (a.samples.data(), b.samples.data(), a.samples.size() * sizeof (float)) == 0;
}

// The conversion loop before begin/step existed: process full source blocks, then
// whole silence blocks until the delay-trimmed delivered length is available.
bool legacyConvert (double a, double b, int channels, long long frames,
                    const float* const* in, float* const* out, long long delivered)
{
    constexpr int block = 257;
    core::DeliveryResampler src;
    core::DeliveryResampler::Params params;
    params.inRate = a; params.outRate = b;
    if (! src.prepare (params, channels, block)) return false;
    const auto& plan = src.currentPlan();
    int perCall = block;
    if (! plan.identity)
        for (int s = 0; s < plan.count; ++s)
            perCall = perCall * plan.stage[s].L / plan.stage[s].M + 1;
    std::vector<float> staging ((std::size_t) channels * (std::size_t) perCall);
    std::vector<float> gated ((std::size_t) channels * block);
    std::vector<float> silence ((std::size_t) channels * block, 0.0f);
    float* sp[2] {};
    const float* zp[2] {};
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
            if (n < 0 || n >= delivered) continue;
            for (int c = 0; c < channels; ++c) out[c][n] = sp[c][k];
        }
    };
    for (long long off = 0; off < frames; )
    {
        const int m = (int) std::min<long long> (block, frames - off);
        const float* ip[2] {};
        for (int c = 0; c < channels; ++c)
        {
            float* g = gated.data() + (std::size_t) c * block;
            for (int i = 0; i < m; ++i)
            {
                const float v = in[c][off + i];
                g[i] = std::clamp (std::isfinite (v) ? v : 0.0f, -1.0e6f, 1.0e6f);
            }
            ip[c] = g;
        }
        int got = 0;
        if (! src.process (ip, channels, m, sp, perCall, got)) return false;
        take (got);
        off += m;
    }
    for (long long guard = 0; emitted < trim + delivered; ++guard)
    {
        if (guard > 64 + (trim + delivered) / block) return false;
        int got = 0;
        if (! src.process (zp, channels, block, sp, perCall, got)) return false;
        take (got);
    }
    return true;
}

bool run (double sourceRate, double deliveryRate, int channels, long long frames, long long budget,
          bool cancelFirst, bool& zeroStayed, bool& allocationFree)
{
    const long long delivered = DeliveredMastering::deliveredFrames (sourceRate, deliveryRate, frames);
    Audio source (channels, frames), reference (channels, delivered), resumed (channels, delivered);
    for (int c = 0; c < channels; ++c)
        for (long long i = 0; i < frames; ++i)
            source.out[c][i] = (float) (0.24 * std::sin (0.019 * (double) i + (double) c) + 0.13 * std::sin (0.003 * (double) i));
    if (frames > 0) source.out[channels - 1][frames - 1] = 0.92f;

    // The prior whole composition: a complete SRC programme, followed by the plain renderer.
    Audio converted (channels, delivered), convertedStep (channels, delivered);
    DeliveryConverter converter;
    MasteringChain chain0, chain1;
    OfflineRenderer renderer0, renderer1;
    DeliveredMastering delivery;
    if (! legacyConvert (sourceRate, deliveryRate, channels, frames, source.in, converted.out, delivered)
        || ! converter.prepare (sourceRate, deliveryRate, channels, 257)
        || ! converter.begin (source.in, channels, frames, convertedStep.out, delivered)) return false;
    StepResult convertedState = StepResult::More;
    for (long long guard = 0; convertedState == StepResult::More && guard < 1000000; ++guard)
        convertedState = converter.step (budget);
    if (convertedState != StepResult::Done || ! converter.finish() || ! same (converted, convertedStep)
        || ! chain0.prepare (deliveryRate, channels, config()) || ! renderer0.prepare (channels, 257)
        || ! renderer0.render (chain0, converted.in, reference.out, channels, (int) delivered)
        || ! chain1.prepare (deliveryRate, channels, config()) || ! renderer1.prepare (channels, 257)
        || ! delivery.prepare (sourceRate, deliveryRate, channels, 257)) return false;

    const long long allocationsBefore = alloc::count.load();
    if (! delivery.beginRender (chain1, renderer1, source.in, channels, frames, resumed.out, delivered)) return false;
    const long long readBefore = delivery.converter().readFrames();
    const StepResult zero = delivery.stepRender (chain1, renderer1, 0);
    zeroStayed = zero == StepResult::More && readBefore == delivery.converter().readFrames()
        && std::all_of (resumed.samples.begin(), resumed.samples.end(), [] (float v) { return v == -19.0f; });
    if (cancelFirst)
    {
        if (frames == 9001)
        {
            const long long target = budget == 7 ? delivered : delivered / 2;
            StepResult interrupted = StepResult::More;
            for (long long guard = 0; renderer1.processedFrames() < target
                                     && interrupted == StepResult::More && guard < 1000000; ++guard)
                interrupted = delivery.stepRender (chain1, renderer1, budget);
            if (interrupted != StepResult::More) return false;
        }
        else if (delivery.stepRender (chain1, renderer1, 1) == StepResult::Failed) return false;
        delivery.cancelRender (renderer1);
        if (! delivery.beginRender (chain1, renderer1, source.in, channels, frames, resumed.out, delivered)) return false;
    }
    StepResult state = StepResult::More;
    for (long long guard = 0; state == StepResult::More && guard < 1000000; ++guard)
        state = delivery.stepRender (chain1, renderer1, budget);
    if (state != StepResult::Done || ! delivery.finishRender()) return false;
    allocationFree = alloc::count.load() == allocationsBefore;
    return same (reference, resumed);
}
}

int main()
{
    group ("resumable delivery: old whole composition versus every cut, cancellation and drain");
    int cases = 0, mismatches = 0, zeroFailures = 0, allocationFailures = 0;
    for (const auto rates : { std::pair<double, double> { 44100.0, 48000.0 },
                              { 48000.0, 44100.0 }, { 44100.0, 96000.0 },
                              { 96000.0, 44100.0 }, { 48000.0, 48000.0 } })
        for (int channels : { 1, 2 })
            for (long long frames : { 1LL, 147LL, 9001LL })
                for (long long budget : { 1LL, 7LL, 509LL, 8192LL })
                {
                    bool zero = false, noAlloc = false;
                    const bool matched = run (rates.first, rates.second, channels, frames, budget,
                                              budget == 7 || (budget == 509 && frames == 9001), zero, noAlloc);
                    ++cases;
                    mismatches += ! matched;
                    zeroFailures += ! zero;
                    allocationFailures += ! noAlloc;
                }
    ok (cases == 120 && mismatches == 0, "all rates, widths, short tails and cuts match the whole composition bit for bit");
    ok (zeroFailures == 0, "zero budget consumes no frames and writes nothing");
    ok (allocationFailures == 0, "begin, steps, cancellation and finish allocate nothing");

    group ("whole job: declared storage, stepped preparation and replay");
    {
        constexpr double a = 44100.0, b = 48000.0;
        constexpr int channels = 2;
        constexpr long long frames = 9001;
        const long long delivered = DeliveredMastering::deliveredFrames (a, b, frames);
        Audio source (channels, frames), oracle (channels, delivered), converted (channels, delivered), got (channels, delivered);
        for (long long i = 0; i < frames; ++i)
        {
            source.out[0][i] = (float) (0.3 * std::sin (0.02 * (double) i));
            source.out[1][i] = (float) (0.2 * std::sin (0.03 * (double) i));
        }
        source.out[1][frames - 1] = 0.9f;
        MasteringChain baselineChain;
        OfflineRenderer baselineRenderer;
        const bool baseline = legacyConvert (a, b, channels, frames, source.in, converted.out, delivered)
            && baselineChain.prepare (b, channels, config()) && baselineRenderer.prepare (channels, 257)
            && baselineRenderer.render (baselineChain, converted.in, oracle.out, channels, (int) delivered);
        const auto storage = DeliveredMastering::storageFor (a, b, channels, frames, config(), 257);
        const long long before = alloc::bytes.load();
        MasteringChain chain;
        OfflineRenderer renderer;
        DeliveredMastering delivery;
        const MasteringChainParams params;
        const bool began = delivery.beginJob (chain, renderer, a, b, channels, frames, config(), params,
                                              257, source.in, got.out, delivered);
        const long long beforeZero = alloc::bytes.load();
        const StepResult zero = delivery.stepJob (0);
        const bool zeroClean = baseline && storage.ok && began && zero == StepResult::More
            && beforeZero == alloc::bytes.load()
            && std::all_of (got.samples.begin(), got.samples.end(), [] (float v) { return v == -19.0f; });
        StepResult state = StepResult::More;
        for (long long guard = 0; state == StepResult::More && guard < 1000000; ++guard)
            state = delivery.stepJob (1);
        const bool completed = state == StepResult::Done && delivery.finishJob();
        const long long requested = alloc::bytes.load() - before;
        ok (zeroClean, "begin and zero budget perform no preparation or PCM work");
        ok (completed && same (oracle, got), "unit steps prepare and render the old composition bit for bit");
        ok (storage.workspaceBytes >= (std::uint64_t) requested
            && storage.maxLiveBytes == storage.sourceBytes + storage.outputBytes + storage.workspaceBytes
            && storage.largestBlockBytes >= std::max (storage.sourceBytes, storage.outputBytes),
            "storageFor declares preparation and the source/output live set before the first step (workspace "
            + std::to_string (storage.workspaceBytes) + ", requested " + std::to_string (requested) + ")");
        std::fill (got.samples.begin(), got.samples.end(), -19.0f);
        const bool replayBegun = delivery.beginJob (chain, renderer, a, b, channels, frames, config(), params,
                                                    257, source.in, got.out, delivered);
        const StepResult preparation = delivery.stepJob (1);
        delivery.cancelJob();
        const bool restarted = delivery.beginJob (chain, renderer, a, b, channels, frames, config(), params,
                                                  257, source.in, got.out, delivered);
        state = StepResult::More;
        for (long long guard = 0; state == StepResult::More && guard < 1000000; ++guard)
            state = delivery.stepJob (509);
        ok (replayBegun && preparation == StepResult::More && restarted && state == StepResult::Done
            && delivery.finishJob() && same (oracle, got),
            "a cancelled preparation and a repeat produce the same output");
    }
    return felitronics::test::report();
}
