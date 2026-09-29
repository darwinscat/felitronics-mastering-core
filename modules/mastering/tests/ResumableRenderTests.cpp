// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.

#include <felitronics/mastering/DeliveredMastering.h>
#include <felitronics_test.h>
#include "../../session/tests/DeclaredBudget.h"
#include "PreviousOfflineRenderer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>
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
    OfflineRenderer renderer1;
    DeliveredMastering delivery;
    if (! legacyConvert (sourceRate, deliveryRate, channels, frames, source.in, converted.out, delivered)
        || ! converter.prepare (sourceRate, deliveryRate, channels, 257)
        || ! converter.begin (source.in, channels, frames, convertedStep.out, delivered)) return false;
    StepResult convertedState = StepResult::More;
    for (long long guard = 0; convertedState == StepResult::More && guard < 1000000; ++guard)
        convertedState = converter.step (budget);
    if (convertedState != StepResult::Done || ! converter.finish() || ! same (converted, convertedStep)
        || ! chain0.prepare (deliveryRate, channels, config())
        || ! testing::previousRender (chain0, converted.in, reference.out, channels, (int) delivered, 257)
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

bool rendererSequence()
{
    constexpr int frames = 4097, block = 257;
    Audio source (2, frames), old (2, frames), stepped (2, frames);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            source.out[c][i] = (float) (0.37 * std::sin (0.023 * i + c) + 0.11 * std::cos (0.051 * i));
    source.out[1][frames - 1] = 0.99f;
    MasteringChain a, b;
    OfflineRenderer renderer;
    MasteringChainParams beforePrepare, beforeSound, mid, repeat;
    beforePrepare.preLimiterGainDb = -4.0;
    beforeSound.preLimiterGainDb = 2.0;
    mid.preLimiterGainDb = -7.0;
    repeat.preLimiterGainDb = 5.0;
    a.setParams (beforePrepare); b.setParams (beforePrepare);
    if (! a.prepare (48000.0, 2, config()) || ! b.prepare (48000.0, 2, config())
        || ! renderer.prepare (2, block)) return false;
    a.setParams (beforeSound); b.setParams (beforeSound);
    bool changed = false;
    if (! testing::previousRender (a, source.in, old.out, 2, frames, block, [&] (long long off)
        {
            if (! changed && off == block) { a.setParams (mid); changed = true; }
        })) return false;
    MasteringChainTaps taps;
    if (! renderer.begin (b, source.in, stepped.out, 2, frames, taps)) return false;
    if (renderer.step (b, taps, NullTapSink {}, block, frames) != StepResult::More
        || renderer.processedFrames() != block) return false;
    b.setParams (mid);
    StepResult state = StepResult::More;
    const int cuts[] { 1, 3, 17, 2, 509, 7, 257 };
    for (int i = 0; i < 100000 && state == StepResult::More; ++i)
        state = renderer.step (b, taps, NullTapSink {}, cuts[i % 7], frames);
    if (state != StepResult::Done || ! renderer.finish() || ! same (old, stepped)) return false;

    a.setParams (repeat); b.setParams (repeat);
    if (! testing::previousRender (a, source.in, old.out, 2, frames, block)
        || ! renderer.begin (b, source.in, stepped.out, 2, frames, taps)) return false;
    state = StepResult::More;
    for (int i = 0; i < 100000 && state == StepResult::More; ++i)
        state = renderer.step (b, taps, NullTapSink {}, cuts[(i + 3) % 7], frames);
    return state == StepResult::Done && renderer.finish() && same (old, stepped);
}

// Every tap a render hands its sink, as streams: each call appends what it wrote, and records the position the
// renderer gave it next to the position the calls before it add up to.
struct TapStreams
{
    std::vector<long long> positions, expected, framesWritten;
    std::vector<float> compressor, preLimiter, limiterGr, limiterPeak, peakClip, band;
    long long frames = 0;

    void take (const MasteringChainTaps& taps, long long position, int channels)
    {
        positions.push_back (position); expected.push_back (frames); framesWritten.push_back (taps.framesWritten);
        compressor.insert (compressor.end(), taps.compressorGrDb, taps.compressorGrDb + taps.framesWritten);
        for (int c = 0; c < channels; ++c)
            preLimiter.insert (preLimiter.end(), taps.preLimiter[c], taps.preLimiter[c] + taps.framesWritten);
        limiterGr.insert (limiterGr.end(), taps.limiterGrDb, taps.limiterGrDb + taps.osWritten);
        limiterPeak.insert (limiterPeak.end(), taps.limiterPeakLin, taps.limiterPeakLin + taps.osWritten);
        peakClip.insert (peakClip.end(), taps.peakClipReductionDb, taps.peakClipReductionDb + taps.osWritten);
        band.insert (band.end(), taps.bandDeltaDb, taps.bandDeltaDb + (std::size_t) taps.bandQuantaWritten * kBandGrStride);
        frames += taps.framesWritten;
    }
    bool sameStreams (const TapStreams& o) const
    {
        const auto bits = [] (const std::vector<float>& a, const std::vector<float>& b)
        { return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0; };
        return frames == o.frames && bits (compressor, o.compressor) && bits (preLimiter, o.preLimiter)
            && bits (limiterGr, o.limiterGr) && bits (limiterPeak, o.limiterPeak) && bits (peakClip, o.peakClip)
            && bits (band, o.band);
    }
};

struct TapBuffers
{
    static constexpr int kFrameCapacity = 4096, kQuanta = 64;
    std::vector<float> compressor, pre0, pre1, limiterGr, limiterPeak, peakClip, band;
    float* pre[2] {};
    MasteringChainTaps taps;

    explicit TapBuffers (int oversample)
        : compressor (kFrameCapacity), pre0 (kFrameCapacity), pre1 (kFrameCapacity),
          limiterGr ((std::size_t) kFrameCapacity * oversample), limiterPeak ((std::size_t) kFrameCapacity * oversample),
          peakClip ((std::size_t) kFrameCapacity * oversample), band ((std::size_t) kQuanta * kBandGrStride)
    {
        pre[0] = pre0.data(); pre[1] = pre1.data();
        taps.compressorGrDb = compressor.data(); taps.preLimiter = pre; taps.frameCapacity = kFrameCapacity;
        taps.bandDeltaDb = band.data(); taps.bandQuantaCapacity = kQuanta;
        taps.limiterGrDb = limiterGr.data(); taps.limiterPeakLin = limiterPeak.data();
        taps.peakClipReductionDb = peakClip.data(); taps.osCapacity = kFrameCapacity * oversample;
    }
};

// THE STEPPED RENDERER'S TAPS AGAINST THE PREVIOUS WHOLE LOOP'S: positions and every trace — the compressor's
// reduction and the limiter's input per frame, one dynamic-band row per quantum, the limiter's reduction, its
// reconstructed peak and the K13 clipper's reduction per oversampled sample. The stepped calls cut the stream
// elsewhere (at the input's end at least), so each call's position must be what the calls before it wrote, and the
// streams, concatenated, must be the old loop's bits: then every call's taps sit where the old loop put them.
bool tapSequence (std::string& why)
{
    constexpr int frames = 24007, block = 257, channels = 2;
    Audio source (channels, frames), old (channels, frames), same257 (channels, frames), cut (channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            source.out[c][i] = (float) (0.33 * std::sin (0.021 * i + c) * (i % 2400 < 600 ? 1.0 : 0.05)
                                        + 0.17 * std::sin (0.0023 * i) + (i % 997 == 0 ? 0.8 : 0.0));
    MasteringChainConfig tapped = config();
    tapped.eq = true;
    MasteringChainParams params;
    params.eqBands[0].on = true; params.eqBands[0].type = eq::FilterType::Bell;
    params.eqBands[0].lanes[0].on = true; params.eqBands[0].lanes[0].freq = 160.0; params.eqBands[0].lanes[0].Q = 1.0;
    params.eqBands[0].dyn.on = true; params.eqBands[0].dyn.rangeDb = -6.0; params.eqBands[0].dyn.thrAuto = false; params.eqBands[0].dyn.thrDb = -40.0;
    params.compressor.thresholdDb = -24.0; params.compressor.ratio = 3.0;
    params.preLimiterGainDb = 9.0;
    params.limiter.ceilingDbTp = -3.0; params.limiter.peakClip = true; params.limiter.overCeilingDb = 2.0;
    MasteringChain a, b, c;
    OfflineRenderer renderer;
    a.setParams (params); b.setParams (params); c.setParams (params);
    if (! a.prepare (48000.0, channels, tapped) || ! b.prepare (48000.0, channels, tapped)
        || ! c.prepare (48000.0, channels, tapped) || ! renderer.prepare (channels, block))
    { why = "prepare"; return false; }
    a.setParams (params); b.setParams (params); c.setParams (params);
    TapBuffers oldTaps (a.tapOversampleFactor()), sameTaps (b.tapOversampleFactor()), cutTaps (c.tapOversampleFactor());
    TapStreams oldStreams, sameStreams, cutStreams;
    if (! testing::previousRender (a, source.in, old.out, channels, frames, block, oldTaps.taps,
            [&] (const MasteringChainTaps& t, long long at) { oldStreams.take (t, at, channels); }))
    { why = "previous render"; return false; }
    const auto sinkInto = [channels] (TapStreams& into)
    { return [&into, channels] (const MasteringChainTaps& t, long long at) { into.take (t, at, channels); }; };
    if (! renderer.begin (b, source.in, same257.out, channels, frames, sameTaps.taps)) { why = "begin"; return false; }
    StepResult state = StepResult::More;
    for (int i = 0; i < 100000 && state == StepResult::More; ++i)
        state = renderer.step (b, sameTaps.taps, sinkInto (sameStreams), block, frames);
    if (state != StepResult::Done || ! renderer.finish()) { why = "same-block steps"; return false; }
    if (! renderer.begin (c, source.in, cut.out, channels, frames, cutTaps.taps)) { why = "begin cut"; return false; }
    state = StepResult::More;
    const int cuts[] { 1, 3, 17, 2, 509, 7, 257 };
    for (int i = 0; i < 100000 && state == StepResult::More; ++i)
        state = renderer.step (c, cutTaps.taps, sinkInto (cutStreams), cuts[i % 7], frames);
    if (state != StepResult::Done || ! renderer.finish()) { why = "cut steps"; return false; }
    double maxClip = 0.0, maxGr = 0.0, maxBand = 0.0;
    for (float v : oldStreams.peakClip) maxClip = std::max (maxClip, (double) v);
    for (float v : oldStreams.limiterGr) maxGr = std::max (maxGr, (double) std::fabs (v));
    for (float v : oldStreams.band) maxBand = std::max (maxBand, (double) std::fabs (v));
    const bool live = maxClip > 0.0 && maxGr > 0.0 && maxBand > 0.0 && ! oldStreams.compressor.empty();
    const bool oldSelf = oldStreams.positions == oldStreams.expected;
    const bool lined = sameStreams.positions == sameStreams.expected && sameStreams.sameStreams (oldStreams);
    const bool cutSelf = cutStreams.positions == cutStreams.expected && cutStreams.sameStreams (oldStreams);
    why = "live " + std::to_string (live) + " (clip " + std::to_string (maxClip) + ", GR " + std::to_string (maxGr)
        + ", band " + std::to_string (maxBand) + " over " + std::to_string (oldStreams.band.size()) + " values, " + std::to_string (oldStreams.compressor.size()) + " frames), old positions " + std::to_string (oldSelf) + ", same-block "
        + std::to_string (lined) + ", cut " + std::to_string (cutSelf) + ", pcm "
        + std::to_string (same (old, same257) && same (old, cut));
    return live && oldSelf && lined && cutSelf && same (old, same257) && same (old, cut);
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
    ok (rendererSequence(), "previous whole renderer matches adversarial cuts across parameter writes and repeat renders");
    std::string tapWhy;
    ok (tapSequence (tapWhy), "the stepped renderer's tap positions, limiter, K13 and band traces are the previous "
                              "tapped whole loop's (" + tapWhy + ")");

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
        const bool baseline = legacyConvert (a, b, channels, frames, source.in, converted.out, delivered)
            && baselineChain.prepare (b, channels, config())
            && testing::previousRender (baselineChain, converted.in, oracle.out, channels, (int) delivered, 257);
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
    group ("whole job: bounded preparation and retained workspace declaration");
    {
        bool admitted = true;
        for (double a : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            for (double b : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
                admitted &= DeliveredMastering::storageFor (a, b, 2, 1, config(), 1 << 22).ok;
        ok (admitted, "all standard delivery rate pairs admit a large requested job block");
    }
    {
        constexpr int hugeBlock = 1 << 22;
        Audio source (2, 1), got (2, 2);
        MasteringChain chain; OfflineRenderer renderer; DeliveredMastering delivery;
        const MasteringChainParams params;
        const bool began = delivery.beginJob (chain, renderer, 48000.0, 48000.0, 2, 1,
                                              config(), params, hugeBlock, source.in, got.out, 1);
        const long long before = alloc::bytes.load();
        const StepResult twoSteps = began ? delivery.stepJob (2) : StepResult::Failed;
        const long long asked = alloc::bytes.load() - before;
        ok (began && twoSteps == StepResult::More && asked < (1 << 20),
            "unit preparation never initializes a multi-megabyte renderer block");
        delivery.cancelJob();
        const auto large = DeliveredMastering::storageFor (48000.0, 48000.0, 2, 1, config(), 1 << 20);
        const auto small = DeliveredMastering::storageFor (48000.0, 48000.0, 2, 1, config(), 257);
        const auto reused = delivery.storageForJob (48000.0, 48000.0, 2, 1, config(), 257);
        ok (large.ok && small.ok && reused.ok
            && reused.workspaceBytes >= small.workspaceBytes + large.workspaceBytes
            && reused.maxLiveBytes >= reused.sourceBytes + reused.outputBytes + reused.workspaceBytes,
            "a reused job quote includes retained capacity and the replacement peak");
    }
    {
        Audio source (2, 1), got (2, 3);
        MasteringChain chain; OfflineRenderer renderer; DeliveredMastering delivery;
        MasteringChainParams params;
        const bool began = delivery.beginJob (chain, renderer, 44100.0, 96000.0, 2, 1,
                                              config(), params, 1 << 22, source.in, got.out, 3);
        const StepResult first = began ? delivery.stepJob (2) : StepResult::Failed;
        const long long before = alloc::bytes.load();
        const StepResult converter = first == StepResult::More ? delivery.stepJob (1) : StepResult::Failed;
        const long long asked = alloc::bytes.load() - before;
        ok (began && first == StepResult::More && converter == StepResult::More
            && asked <= (long long) DeliveredMastering::kMaxPreparationStageBytes,
            "a rate-changing job bounds converter buffers and coefficient preparation per step ("
            + std::to_string (asked) + " bytes)");
        delivery.cancelJob();
    }
    {
        constexpr long long frames = 9001;
        constexpr int block = 1 << 22;
        const long long delivered = DeliveredMastering::deliveredFrames (44100.0, 96000.0, frames);
        Audio source (2, frames), converted (2, delivered), oracle (2, delivered), got (2, delivered);
        for (int c = 0; c < 2; ++c)
            for (long long i = 0; i < frames; ++i)
                source.out[c][i] = (float) (0.25 * std::sin (0.017 * i + c));
        source.out[1][frames - 1] = 0.99f;
        MasteringChain oldChain, chain; OfflineRenderer renderer; DeliveredMastering delivery;
        MasteringChainParams params;
        const bool old = legacyConvert (44100.0, 96000.0, 2, frames, source.in, converted.out, delivered)
            && oldChain.prepare (96000.0, 2, config())
            && testing::previousRender (oldChain, converted.in, oracle.out, 2, (int) delivered, 257);
        const bool began = delivery.beginJob (chain, renderer, 44100.0, 96000.0, 2, frames,
                                              config(), params, block, source.in, got.out, delivered);
        StepResult state = StepResult::More;
        for (int i = 0; began && state == StepResult::More && i < 100000; ++i)
            state = delivery.stepJob ((i % 3) + 1);
        ok (old && began && state == StepResult::Done && delivery.finishJob() && same (oracle, got),
            "a capped large-block job preserves the previous whole conversion and renderer bits");
    }
    {
        Audio source (2, 1), got (2, 2);
        MasteringChain chain; OfflineRenderer renderer; DeliveredMastering delivery;
        MasteringChainParams params;
        bool covered = true;
        std::string detail;
        std::uint64_t lifetimeAsked = 0;
        std::uint64_t repeatedQuote = 0;
        int jobs = 0;
        for (int block : { 257, 1024, 257 })
        {
            const auto quote = delivery.storageForJob (44100.0, 48000.0, 2, 1, config(), block);
            if (jobs == 2) repeatedQuote = quote.workspaceBytes;
            bool ran = false;
            const auto spent = session::testing::spend ([&]
            {
                if (! delivery.beginJob (chain, renderer, 44100.0, 48000.0, 2, 1, config(), params,
                                         block, source.in, got.out, 2)) return;
                StepResult state = StepResult::More;
                for (int guard = 0; state == StepResult::More && guard < 10000; ++guard)
                    state = delivery.stepJob (100000);
                ran = state == StepResult::Done && delivery.finishJob();
            });
            const bool pass = quote.ok && ran && session::testing::covers (quote.workspaceBytes, spent)
                && quote.workspaceBytes >= lifetimeAsked + (std::uint64_t) spent.bytes;
            covered &= pass;
            lifetimeAsked += (std::uint64_t) spent.bytes;
            if (! pass) detail += " block " + std::to_string (block) + " ran " + std::to_string (ran)
                + " " + session::testing::describe (quote.workspaceBytes, spent);
            ++jobs;
        }
        covered &= delivery.storageForJob (44100.0, 48000.0, 2, 1, config(), 257).workspaceBytes == repeatedQuote;
        const auto beforeCancel = delivery.storageForJob (44100.0, 48000.0, 2, 1, config(), 1024);
        const auto cancelled = session::testing::spend ([&]
        {
            if (delivery.beginJob (chain, renderer, 44100.0, 48000.0, 2, 1, config(), params,
                                   1024, source.in, got.out, 2))
                (void) delivery.stepJob (2);
            delivery.cancelJob();
        });
        const bool cancelCovered = beforeCancel.ok && session::testing::covers (beforeCancel.workspaceBytes, cancelled)
            && beforeCancel.workspaceBytes >= lifetimeAsked + (std::uint64_t) cancelled.bytes;
        covered &= cancelCovered;
        lifetimeAsked += (std::uint64_t) cancelled.bytes;
        if (! cancelCovered) detail += " cancel " + session::testing::describe (beforeCancel.workspaceBytes, cancelled);
        const auto refused = session::testing::spend ([&]
        {
            (void) delivery.beginJob (chain, renderer, 44100.0, 48000.0, 2, 1, config(), params,
                                      0, source.in, got.out, 2);
        });
        const bool refusalCovered = refused.bytes == 0 && ! delivery.storageForJob (44100.0, 48000.0, 2, 1, config(), 0).ok;
        covered &= refusalCovered;
        if (! refusalCovered) detail += " refusal " + std::to_string (refused.bytes);
        ok (covered, "DeclaredBudget covers growth, shrink, cancellation and refusal on one reused job object" + detail);
    }
    return felitronics::test::report();
}
