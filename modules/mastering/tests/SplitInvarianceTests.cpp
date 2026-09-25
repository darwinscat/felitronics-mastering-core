// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Split invariance on the mastering side, through the SAME harness the offline analyzers are measured with
// (tests/split_invariance.h): the same cuts, the same bit comparison, the same control. Three things a caller that
// renders and measures in chunks of its own choosing stands on:
//
//   1. THE CHAIN, streamed. `MasteringChain` runs a fixed internal quantum, so "the same bits at any caller block
//      size" is a theorem here, and MasteringChainTests pins the OUTPUT on an adversarial partition. This adds what
//      that section does not compare: every TAP stream (compressor GR, the pre-limiter planes, the limiter's
//      oversampled GR and peak, the per-quantum band-GR rows) concatenated across calls, zero-length calls, and
//      FINISH ON A PREFIX — a chain fed a prefix in ragged pieces and then drained by `latencySamples()` zeros is,
//      by OfflineRenderer's own formula `out[n] = y[n + D]`, the render of that prefix; read BEFORE the drain it is
//      the stream as emitted, raw, from its first sample. Two topologies: the full voicing (EQ with a parked band and
//      an armed dynamic band, mono-bass, sidechain HPF, clipper, limiter, 24-bit auto-blanking dither) at K = 256,
//      and one at K = 96 with a 2x limiter, the stereo air shelf, the limiter's peak clip and a compressor mix of
//      0.6 — every statistic the chain counts (air, peak clip, non-finite input) is compared with the audio. Each
//      topology also runs WITHOUT taps, the other path through process(), and must emit the same audio and
//      statistics as the tapped run cut differently. The chain's width is EXACT, so a zero-width or a narrower call
//      is refused by contract and there are no width timelines. What it has instead is the AUTOMATION timeline:
//      both topologies again with every glidable parameter and every bypass moved at fixed stream positions (see
//      `automation()`), which is where a glide clocked by the call instead of the sample would show.
//   2. THE RENDERER against that stream: `OfflineRenderer::render` at blocks of 1, 64, 480, 4096, a prime, the whole
//      programme in one block, under a ProgressClock (the path every progress callback takes) and IN PLACE — every
//      one the same bits, taps included, as the chain streamed in one call; the output buffer is pre-filled with a
//      sentinel and every sink call's tap position is checked. Then programmes of 0, 1, K-1, K, K+1, D-1, D, D+1,
//      2K+3 and 2D+5 frames, where the whole output comes out of the drain. The progress EVENTS are not a split row:
//      they follow the pieces the work was cut into by construction (measured and printed), the audio does not.
//   3. THE SOLVER'S MEASUREMENT PATH: `analysis::LoudnessMeter` + `analysis::ReferenceTruePeakMeter`, built exactly
//      as `TargetLoudnessSolver::measure()` builds them, over a RENDERED master — integrated loudness, LRA, the
//      momentary and short-term readings, every pre-gate block energy, the counters, the true and sample peaks —
//      with all four timelines and a finish on a prefix, and with the programme's true peak placed where only the
//      DRAIN of a stopping channel can measure it. Tied to the solver itself: `measureInputLoudnessRange()` reads the
//      adapter's one-call LRA bit for bit, with and without a progress callback.
//
// ONE OUTPUT IS CALL-SCOPED BY DEFINITION and is kept out of the comparison: `ReferenceTruePeakMeter::
// truePeakLinearBlock()` (and its dB twin) is the peak of the LAST CALL. It is measured anyway, as a second control —
// a published number the harness must report as NOT invariant — because a caller that chunks must not read it as a
// programme peak.
//
// ProgrammeReport's LRA — the other LRA in this repository, on its own deterministic meter and its own short-term
// series — is measured in the analyzer suite, with the rest of that report.
//
// MEASURED, v0.1.0 on felitronics-core v0.52.0: every row INVARIANT (114), every control caught, the solver tied to
// the adapter — on Apple clang 21 arm64, gcc 14.2 x86-64, MSVC 19.44 x64 and wasm32 (emsdk 6.0.9). The table at the
// end of the run is the record.

#include <felitronics/analysis/LoudnessMeter.h>
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include <felitronics/mastering/OfflineRenderer.h>
#include <felitronics/mastering/Progress.h>
#include <felitronics_test.h>
#include <split_invariance.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace felitronics;
using namespace felitronics::test::split;
using felitronics::test::group;
using felitronics::test::ok;

namespace
{

//==============================================================================
// THE PROGRAMME: silence, a loud stretch in two levels with kicks and a band the dynamic EQ point reacts to, a
// -160 dBFS tone (the compressor's flush floor), then digital silence long enough for the dither to auto-blank —
// and, for the chain's own rows, a transient on the very last sample, which only a drained tail carries out.
Programme chainFixture (double seconds, std::uint64_t seed, bool lastSampleTransient)
{
    const double fs = 48000.0;
    Gen g (fs, 2, (std::int64_t) (seconds * fs) + 1117, seed);
    const std::int64_t n = g.len();
    const std::int64_t a = 14400, b = n - 60000, c = n - 36000;
    for (int ch = 0; ch < 2; ++ch)
    {
        g.noise (ch, a, b, 0.06);
        g.tone (ch, a, b, 110.0, 0.35, 0.2 * ch);
        g.bursts (ch, a, b, 28800, 14400, 480, 2350.0, 0.3, 0.5 * ch);           // the dynamic point's band
        for (std::int64_t k = a + 1000 + 97 * ch; k < b; k += 12000) g.hit (ch, k, 0.8, 70.0, 60.0);
        for (std::int64_t s = a; s < b; s += 96000) g.scale (ch, s, s + 48000, 0.25);   // two levels: a range
        g.tone (ch, b, c, 997.0, 1.0e-8);                                          // -160 dBFS
    }
    if (lastSampleTransient) g.at (1, n - 1) = -0.9f;   // the last sample: what only a drained tail carries out
    return g.p;
}

// Noise from the first sample on, for the short renders: every prefix ends on a live sample.
Programme shortFixture (std::int64_t n)
{
    Gen g (48000.0, 2, n, 0x5eed1003u);
    for (int ch = 0; ch < 2; ++ch) { g.noise (ch, 0, n, 0.5); g.tone (ch, 0, n, 3000.0, 0.3, ch); }
    return g.p;
}

mastering::MasteringChainConfig chainConfig (int topology)
{
    mastering::MasteringChainConfig c;
    if (topology == 0)
    {
        c.internalBlock = 256;
        c.eq = c.compressor = c.limiter = c.dither = true;
        c.monoBass = c.clipper = true;
        c.sidechainHpfHz = 120.0;
    }
    else
    {
        c.internalBlock = 96;
        c.eq = c.compressor = c.limiter = c.dither = true;
        c.stereoAir = true;                      // the air shelf on its own island, without mono-bass
        c.oversampleFactor = 2;
        c.tapsPerPhase = 32;
    }
    return c;
}

mastering::MasteringChainParams chainParams (int topology)
{
    mastering::MasteringChainParams p;
    p.inputGainDb = 1.5;
    p.preLimiterGainDb = 3.0;
    auto bell = [] (eq::BandParams& b, double hz, double q, double db)
    {
        b.on = true;
        b.type = eq::FilterType::Bell;
        b.lane (eq::Lane::Stereo).on = true;
        b.lane (eq::Lane::Stereo).freq = hz;
        b.lane (eq::Lane::Stereo).Q = q;
        b.lane (eq::Lane::Stereo).gainDb = db;
    };
    bell (p.eqBands[0], 3200.0, 0.9, 2.5);
    bell (p.eqBands[1], 400.0, 1.0, 0.0);                  // parked at 0 dB: the state sits on the flush threshold
    bell (p.eqBands[2], 2350.0, 2.0, 0.0);                 // the armed dynamic point
    p.eqBands[2].dyn.on = true;
    p.eqBands[2].dyn.rangeDb = -6.0;
    p.eqBands[2].dyn.thrAuto = false;
    p.eqBands[2].dyn.thrDb = -30.0;
    p.monoBass.enabled = true;
    p.monoBass.frequencyHz = 110.0f;
    p.monoBass.lowWidth = 0.0f;
    p.compressor.thresholdDb = -18.0;
    p.compressor.ratio = 2.5;
    p.compressor.attackMs = 12.0;
    p.compressor.releaseMs = 180.0;
    p.compressor.makeupDb = 1.0;
    p.clipper.driveDb = 4.0f;
    p.clipper.mix = 1.0f;
    p.limiter.ceilingDbTp = -1.0;
    p.limiter.releaseMs = 60.0;
    p.dither.bits = 24;
    p.dither.shaping = dither::NoiseShaping::Weighted;
    p.dither.autoBlank = true;
    if (topology == 1)
    {
        p.stereoAir.enabled = true;              // the Side shelf, and its band reading
        p.stereoAir.gainDb = 2.0f;
        p.limiter.peakClip = true;               // the limiter's clipper, and its statistics
        p.compressorMix = 0.6;                   // parallel compression: the dry path in the sum
    }
    return p;
}

//==============================================================================
// The taps: caller-owned buffers with a capacity, and the streams they add up to across calls.
struct TapBuffers
{
    std::vector<float> comp, limGr, limPk, band;
    std::vector<std::vector<float>> pre;
    std::vector<float*> prePtr;
    mastering::MasteringChainTaps taps;

    void size (int nch, long long frameCap, long long osCap, long long quantaCap)
    {
        comp.assign ((std::size_t) frameCap, 0.0f);
        pre.assign ((std::size_t) nch, std::vector<float> ((std::size_t) frameCap, 0.0f));
        prePtr.clear();
        for (auto& v : pre) prePtr.push_back (v.data());
        limGr.assign ((std::size_t) osCap, 0.0f);
        limPk.assign ((std::size_t) osCap, 0.0f);
        band.assign ((std::size_t) quantaCap * (std::size_t) mastering::kBandGrStride, 0.0f);
        taps = {};
        taps.compressorGrDb = comp.data();
        taps.preLimiter = prePtr.data();
        taps.frameCapacity = (int) frameCap;
        taps.limiterGrDb = limGr.data();
        taps.limiterPeakLin = limPk.data();
        taps.osCapacity = (int) osCap;
        taps.bandDeltaDb = band.data();
        taps.bandQuantaCapacity = (int) quantaCap;
    }
};

struct TapStreams
{
    std::vector<float> comp, limGr, limPk, band;
    std::vector<std::vector<float>> pre;
    long long calls = 0;
    bool positionsConsistent = true;         // every sink call's tap position == the frames before it

    void take (const mastering::MasteringChainTaps& t, int nch)
    {
        ++calls;
        if (pre.size() != (std::size_t) nch) pre.assign ((std::size_t) nch, {});
        comp.insert (comp.end(), t.compressorGrDb, t.compressorGrDb + t.framesWritten);
        for (int c = 0; c < nch; ++c) pre[(std::size_t) c].insert (pre[(std::size_t) c].end(), t.preLimiter[c], t.preLimiter[c] + t.framesWritten);
        limGr.insert (limGr.end(), t.limiterGrDb, t.limiterGrDb + t.osWritten);
        limPk.insert (limPk.end(), t.limiterPeakLin, t.limiterPeakLin + t.osWritten);
        band.insert (band.end(), t.bandDeltaDb, t.bandDeltaDb + (std::size_t) t.bandQuantaWritten * (std::size_t) mastering::kBandGrStride);
    }
};

// What the chain COUNTED over the stream, read through its own accessors — the air shelf's band reading, the
// limiter's peak-clip statistics, the non-finite input count.
void putStats (Bits& o, const mastering::MasteringChain& c)
{
    o.i ("nonFiniteInputSamples", (std::int64_t) c.nonFiniteInputSamples());
    o.d ("airMidEnergy", c.airMidEnergy()); o.d ("airSideEnergyBefore", c.airSideEnergyBefore());
    o.d ("airSideEnergyAfter", c.airSideEnergyAfter()); o.d ("airWidthBefore", c.airWidthBefore());
    o.d ("airWidthAfter", c.airWidthAfter()); o.i ("airJudgedSamples", c.airJudgedSamples());
    o.d ("peakClipReductionMaxDb", c.peakClipReductionMaxDb()); o.d ("peakClipReductionP95Db", c.peakClipReductionP95Db());
    o.d ("peakClipOccupancy", c.peakClipOccupancy()); o.i ("peakClipRuns", c.peakClipRuns());
    o.i ("peakClipRunSamplesTotal", c.peakClipRunSamplesTotal()); o.i ("peakClipLongestRunSamples", c.peakClipLongestRunSamples());
}

// The render as bits: `frames` output samples per channel read at `offset` (the chain's own stream is read at D),
// then every tap stream whole.
void putRender (Bits& o, const std::vector<std::vector<float>>& planes, std::int64_t offset, std::int64_t frames, const TapStreams& t)
{
    o.i ("frames", frames);
    o.b ("tap.positionsConsistent", t.positionsConsistent);
    for (const auto& p : planes)
        for (std::int64_t k = 0; k < frames; ++k) o.f ("out", p[(std::size_t) (k + offset)]);
    o.i ("tap.compFrames", (std::int64_t) t.comp.size());
    for (float v : t.comp) o.f ("tap.compressorGrDb", v);
    for (const auto& p : t.pre) for (float v : p) o.f ("tap.preLimiter", v);
    o.i ("tap.osFrames", (std::int64_t) t.limGr.size());
    for (float v : t.limGr) o.f ("tap.limiterGrDb", v);
    for (float v : t.limPk) o.f ("tap.limiterPeakLin", v);
    o.i ("tap.bandWords", (std::int64_t) t.band.size());
    for (float v : t.band) o.f ("tap.bandDeltaDb", v);
}

std::string renderFacts (const std::vector<std::vector<float>>& planes, std::int64_t offset, std::int64_t frames,
                         const TapStreams& t, bool& ok)
{
    float peak = 0.0f, comp = 0.0f, lim = 0.0f, band = 0.0f;
    std::int64_t tailZeros = 0;
    for (const auto& p : planes)
        for (std::int64_t k = 0; k < frames; ++k) peak = std::max (peak, std::fabs (p[(std::size_t) (k + offset)]));
    // exact zeros on the left channel in the silent stretch before the last-sample transient: the auto-blank at work
    for (std::int64_t k = std::max<std::int64_t> (0, frames - 30000); k < frames - 5000; ++k)
        if (planes[0][(std::size_t) (k + offset)] == 0.0f) ++tailZeros;
    for (float v : t.comp) comp = std::min (comp, v);
    for (float v : t.limGr) lim = std::min (lim, v);
    for (float v : t.band) band = std::min (band, v);
    char b[240];
    std::snprintf (b, sizeof b, "output peak %.4f, compressor GR to %.2f dB, limiter GR to %.2f dB, band GR to %.2f dB, "
                   "%lld exact zeros in the silent stretch (auto-blank)", (double) peak, (double) comp, (double) lim, (double) band,
                   (long long) tailZeros);
    const bool tapped = ! t.comp.empty();
    ok = peak > 0.3f && tailZeros > 1000 && (! tapped || (comp < -1.0f && lim < -0.5f && band < -0.5f));
    return b;
}

//==============================================================================
// THE AUTOMATION TIMELINE — the parameter moves a live preview makes, as EVENTS at fixed stream positions. Law 8a
// promises nothing about events that arrive per call, so every run delivers these at the same samples and only the
// cutting around them moves: the adapter splits a call at an event and calls `setParams()` between the pieces, which
// is what a host does when a knob moves between two callbacks. The chain defers each one to its next quantum
// boundary, and from there every glide it starts — the gain nodes, the compressor mix and makeup, the clipper's
// drive, mono-bass's crossover, the air shelf, a dynamic point switched off while it ducks, and the bypass
// crossfades — runs on the samples of the quanta, never on the caller's calls. That is the claim this timeline
// holds every glide to: THE SAME BITS UNDER EVERY CUT, taps and statistics included. The positions are deliberately
// on no grid (not K, not 64), and the moves are large, so each glide is still running when the next cut lands in it.
struct AutoEvent { std::int64_t at; mastering::MasteringChainParams p; };

std::vector<AutoEvent> automation (int topology, std::int64_t n)
{
    std::vector<AutoEvent> ev;
    mastering::MasteringChainParams p = chainParams (topology);
    auto at = [&] (std::int64_t pos) { ev.push_back ({ pos, p }); };
    // the gain nodes and the parallel mix
    p.inputGainDb = 5.0; p.preLimiterGainDb = -2.0; p.compressorMix = 0.45;          at (n / 9 + 37);
    // the stages' own continuous parameters
    p.clipper.driveDb = 9.0f; p.clipper.mix = 0.7f; p.clipper.outputDb = -2.0f;
    p.compressor.makeupDb = 4.0; p.compressor.autoMakeup = true;
    if (topology == 0) { p.monoBass.frequencyHz = 240.0f; p.monoBass.lowWidth = 0.6f; }
    else               { p.stereoAir.frequencyHz = 9000.0f; p.stereoAir.gainDb = 5.0f; }
    at (n / 5 + 1001);
    // a dynamic point switched off while it works, and the ceiling moved
    p.eqBands[2].dyn.on = false; p.limiter.ceilingDbTp = -4.0;                        at (n / 3 + 17);
    // the bypasses in, and out again
    p.bypassClipper = true; p.bypassLimiter = true; p.bypassMonoBass = true; p.bypassCompressor = true;
    at (n / 2 + 555);
    p.bypassClipper = false; p.bypassLimiter = false; p.bypassMonoBass = false; p.bypassCompressor = false;
    if (topology == 0) p.monoBass.enabled = false; else p.stereoAir.enabled = false;
    at (n * 3 / 5 + 123);
    if (topology == 0) p.monoBass.enabled = true; else p.stereoAir.enabled = true;
    p.inputGainDb = 1.5; p.compressorMix = 0.9; p.eqBands[2].dyn.on = true; p.compressor.autoMakeup = false;
    at (n * 3 / 4 + 77);
    return ev;
}

//==============================================================================
// 1. THE CHAIN, streamed in place and drained — the adapter the harness cuts. `Taps == false` is the plain
// three-argument call: no trace requested, which is a different path through process() (`wantTaps`). `Auto` plays the
// automation timeline above into it.
template <int Topology, bool Taps = true, bool Auto = false>
struct ChainA
{
    static constexpr const char* kName = Auto ? (Topology == 0 ? "MasteringChain (full voicing, K 256, AUTOMATED)"
                                                               : "MasteringChain (air + peak clip + mix, K 96, 2x, AUTOMATED)")
                                       : Topology == 0 ? (Taps ? "MasteringChain (full voicing, K 256)" : "MasteringChain (full voicing, K 256, no taps)")
                                                       : (Taps ? "MasteringChain (air + peak clip + mix 0.6, K 96, 2x)" : "MasteringChain (air + peak clip, K 96, no taps)");
    static constexpr bool kClockOnly = false;   // the width is EXACT (law 11c): a zero-width call is a refusal
    static constexpr bool kNarrow    = false;   // …and so is a narrower one
    static constexpr bool kMaxBlock  = false;   // the chain takes no maxBlock: the quantum is its only block
    mastering::MasteringChain chain;
    std::vector<std::vector<float>> y;          // the stream, in place: the programme, then D zeros
    std::int64_t pos = 0;
    int nch = 0, D = 0;
    bool drained = false;
    TapBuffers tb;
    TapStreams got;
    std::vector<AutoEvent> events;
    std::size_t applied = 0;

    bool prepare (const Programme& p, int)
    {
        nch = p.channels();
        if (Auto) events = automation (Topology, p.declared);
        if (! chain.prepare (p.fs, nch, chainConfig (Topology))) return false;
        chain.setParams (chainParams (Topology));
        chain.reset();                          // exactly what OfflineRenderer::render does first
        D = chain.latencySamples();
        const int K = chain.internalBlock();
        y.assign ((std::size_t) nch, std::vector<float> ((std::size_t) (p.declared + D), 0.0f));
        const long long cap = p.declared + D + K;
        if (Taps) tb.size (nch, cap, cap * chain.tapOversampleFactor(), cap / K + 2);
        return true;
    }
    bool call (float* const* io, int width, int n)
    {
        if (! Taps) return chain.process (io, width, n);
        if (! chain.process (io, width, n, tb.taps)) return false;
        got.take (tb.taps, nch);
        return true;
    }
    bool feed (const float* const* in, int width, int n, int off)
    {
        std::vector<float*> io ((std::size_t) nch);
        for (int c = 0; c < nch; ++c)
        {
            float* dst = y[(std::size_t) c].data() + pos;
            if (c < width) std::copy (in[c] + off, in[c] + off + n, dst);
            io[(std::size_t) c] = dst;
        }
        if (! call (io.data(), width, n)) return false;
        pos += n;
        return true;
    }
    void applyDue()
    {
        while (applied < events.size() && events[applied].at <= pos) chain.setParams (events[applied++].p);
    }
    // AN EVENT IS DELIVERED JUST BEFORE THE SAMPLE AT ITS POSITION IS FED — never by a zero-length call. The
    // first spelling also delivered it on an empty call standing at that position, so a prefix ending exactly on
    // an event rendered differently with and without a trailing empty call (found by the code-review round).
    bool process (const float* const* in, int width, int n)
    {
        if (! Auto) return feed (in, width, n, 0);
        if (n == 0) return feed (in, width, 0, 0);
        for (int off = 0; off < n; )
        {
            applyDue();
            std::int64_t m = n - off;
            if (applied < events.size()) m = std::min<std::int64_t> (m, events[applied].at - pos);
            if (! feed (in, width, (int) m, off)) return false;
            off += (int) m;
        }
        return true;
    }
    // THE DRAIN IS THE RENDERER'S: the chain is fed D zeros, and the output is the stream read D samples late.
    void finish()
    {
        std::vector<float*> io ((std::size_t) nch);
        for (int c = 0; c < nch; ++c) io[(std::size_t) c] = y[(std::size_t) c].data() + pos;
        drained = call (io.data(), nch, D);
        if (! drained) got.calls = -1;          // a refused drain is visible in the bits
    }
    // Finished: the render, `out[n] = y[n + D]`. Not finished: what the stream has EMITTED so far, raw, from its
    // first sample — which is what a caller watching a stream mid-way holds.
    void serialise (Bits& o) const
    {
        o.i ("drainAccepted", got.calls >= 0 ? 1 : 0);
        o.i ("latency", D);
        putStats (o, chain);
        putRender (o, y, drained ? D : 0, pos, got);
    }
    bool witness (std::string& f) const
    {
        bool ok = false;
        f = renderFacts (y, D, pos, got, ok) + ", latency " + std::to_string (D);
        if (Topology == 1)
        {
            f += ", air judged " + std::to_string (chain.airJudgedSamples()) + " samples, " + std::to_string (chain.peakClipRuns())
               + " peak-clip runs";
            ok = ok && chain.airJudgedSamples() > 0 && chain.peakClipRuns() > 0;
        }
        if (Auto)
        {
            f += ", " + std::to_string (applied) + " of " + std::to_string (events.size()) + " automation events played";
            ok = ok && applied == events.size() && ! events.empty();
        }
        return ok;
    }
};

//==============================================================================
// 2. THE RENDERER. Bits in the chain adapter's own layout, so a render and a stream compare word for word. The output
// is filled with a SENTINEL first, so a sample the renderer never wrote cannot pass as a zero the chain produced; the
// sink checks that the tap position it is handed is the tap frames before it.
bool keepGoing (void*, const mastering::ProgressEvent&) { return true; }

template <int Topology>
Bits renderBits (const Programme& p, int block, bool withClock, bool& refused, bool inPlace = false,
                 mastering::ProgressCallback cb = mastering::ProgressCallback { &keepGoing, nullptr })
{
    Bits o;
    refused = true;
    mastering::MasteringChain chain;
    const int nch = p.channels();
    if (! chain.prepare (p.fs, nch, chainConfig (Topology))) return o;
    chain.setParams (chainParams (Topology));
    mastering::OfflineRenderer r;
    if (! r.prepare (nch, block)) return o;
    const int K = chain.internalBlock();
    const std::int64_t frames = p.len();
    TapBuffers tb;
    tb.size (nch, (long long) block + K, ((long long) block + K) * chain.tapOversampleFactor(), (long long) block / K + 2);
    TapStreams got;
    std::vector<std::vector<float>> out ((std::size_t) nch, std::vector<float> ((std::size_t) frames, 12345.0f));
    if (inPlace) for (int c = 0; c < nch; ++c) out[(std::size_t) c] = p.x[(std::size_t) c];
    std::vector<const float*> in;
    std::vector<float*> op;
    for (int c = 0; c < nch; ++c)
    {
        op.push_back (out[(std::size_t) c].data());
        in.push_back (inPlace ? out[(std::size_t) c].data() : p.x[(std::size_t) c].data());
    }
    auto sink = [&got, nch] (const mastering::MasteringChainTaps& t, long long tapPos)
    {
        if (tapPos != (long long) got.comp.size()) got.positionsConsistent = false;
        got.take (t, nch);
    };
    bool ok = false;
    if (withClock)
    {
        mastering::ProgressClock clock (cb);
        ok = clock.begin (mastering::ProgressStage::Render, 0, 0, frames + chain.latencySamples(), frames)
          && r.render (chain, in.data(), op.data(), nch, (int) frames, tb.taps, sink, &clock);
    }
    else ok = r.render (chain, in.data(), op.data(), nch, (int) frames, tb.taps, sink);
    if (! ok) return o;
    refused = false;
    o.i ("drainAccepted", 1);
    o.i ("latency", chain.latencySamples());
    putStats (o, chain);
    putRender (o, out, 0, frames, got);
    return o;
}

template <int Topology>
void rendererRows (const Programme& p)
{
    const std::string name = std::string ("OfflineRenderer / ") + ChainA<Topology>::kName;
    group ("split invariance — " + name);
    bool r0 = false;
    const Bits ref = runOne<ChainA<Topology>> (p, cuts (p, kWhole, false), 4096, r0);
    ok (! r0 && ref.w.size() > 1000, "the streamed reference is accepted");
    const std::int64_t whole = p.len() + 4096;
    struct R { const char* name; int block; bool clock, inPlace; };
    const R rows[] = { { "render, blocks of 1", 1, false, false }, { "render, blocks of 64", 64, false, false },
                       { "render, blocks of 480", 480, false, false }, { "render, blocks of 4096", 4096, false, false },
                       { "render, blocks of 7919", 7919, false, false },
                       { "render, the whole programme in one block", (int) whole, false, false },
                       { "render, blocks of 4096 under a ProgressClock", 4096, true, false },
                       { "render IN PLACE, blocks of 480", 480, false, true } };
    for (const R& row : rows)
    {
        bool r = false;
        const Bits got = renderBits<Topology> (p, row.block, row.clock, r, row.inPlace);
        record (name.c_str(), row.name, compare (ref, got, r), ref.w.size());
    }

    // SHORT PROGRAMMES, around the latency and the quantum — where the whole output comes out of the drain.
    mastering::MasteringChain probe;
    if (! probe.prepare (p.fs, p.channels(), chainConfig (Topology))) { ok (false, "the short-length probe prepares"); return; }
    const int D = probe.latencySamples(), K = probe.internalBlock();
    const Programme sp = shortFixture (2 * D + 2 * K + 16);
    for (const int L : { 0, 1, K - 1, K, K + 1, D - 1, D, D + 1, 2 * K + 3, 2 * D + 5 })
    {
        const Programme pre = sp.prefix (L);
        bool ra = false, rb = false;
        const Bits a = runOne<ChainA<Topology>> (pre, cuts (pre, { "ragged", 0, 12 }, false), 4096, ra);
        const Bits b = renderBits<Topology> (pre, 64, false, rb);
        record (name.c_str(), "a programme of " + std::to_string (L) + " frames (D " + std::to_string (D) + ", K " + std::to_string (K)
                + "): render in 64s against the stream cut raggedly", compare (a, b, ra || rb), a.w.size());
    }
}

// THE PROGRESS EVENTS ARE THE CALLER'S CLOCK, not the programme's — measured, and deliberately NOT a split row.
// `ProgressClock::advance()` emits once `step` samples have passed since the last emission, counted in the pieces the
// work was cut into, so the SEQUENCE of fractions a callback sees depends on the renderer's block while the audio does
// not. Printed so the number is on record; the audio is asserted.
struct EventLog { std::vector<double> fractions; };
bool logEvent (void* ctx, const mastering::ProgressEvent& e) { static_cast<EventLog*> (ctx)->fractions.push_back (e.fraction); return true; }

void progressObservation (const Programme& p)
{
    group ("split invariance — the progress events follow the pieces, the audio does not");
    EventLog a, b;
    bool ra = false, rb = false;
    const Bits x = renderBits<0> (p, 64, true, ra, false, mastering::ProgressCallback { &logEvent, &a });
    const Bits y = renderBits<0> (p, 4096, true, rb, false, mastering::ProgressCallback { &logEvent, &b });
    ok (compare (x, y, ra || rb).same(), "under a ProgressClock, blocks of 64 and of 4096 render the same bits, taps included");
    std::size_t same = 0;
    for (std::size_t k = 0; k < std::min (a.fractions.size(), b.fractions.size()); ++k)
        if (std::bit_cast<std::uint64_t> (a.fractions[k]) == std::bit_cast<std::uint64_t> (b.fractions[k])) ++same;
    std::printf ("    progress events: blocks of 64 -> %zu events (first %.6f), blocks of 4096 -> %zu events (first %.6f); "
                 "%zu of the paired fractions identical — the events follow the pieces, by construction\n",
                 a.fractions.size(), a.fractions.size() > 1 ? a.fractions[1] : -1.0, b.fractions.size(),
                 b.fractions.size() > 1 ? b.fractions[1] : -1.0, same);
}

//==============================================================================
// 3. THE SOLVER'S MEASUREMENT PATH — built exactly as TargetLoudnessSolver::measure() builds it.
struct SolverMetersA
{
    static constexpr const char* kName = "LoudnessMeter + ReferenceTruePeakMeter (the solver's measure)";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::LoudnessMeter lm;
    analysis::ReferenceTruePeakMeter tm;

    bool prepare (const Programme& p, int maxBlock)
    {
        // TargetLoudnessSolver::meterSamples(): the programme plus a second of margin; weights 1 (stereo). The solver
        // hands the true-peak meter `frames` as its maxBlock; the harness varies it instead (4096, 64, 65536).
        if (! lm.prepareForSamples (p.fs, p.channels(), (double) p.declared + std::ceil (p.fs))) return false;
        for (int c = 0; c < p.channels(); ++c) lm.setChannelWeight (c, 1.0);
        return tm.prepare (p.fs, maxBlock, p.channels());
    }
    bool process (const float* const* in, int nch, int n) { return lm.process (in, nch, n) && tm.process (in, nch, n); }
    void finish() { tm.drain(); }             // the FIR's own zeros, and not given to `lm` — as measure() does
    void serialise (Bits& o) const
    {
        o.d ("integratedLufs", lm.integratedLufs()); o.d ("loudnessRangeLu", lm.loudnessRangeLu());
        o.d ("momentaryLufs", lm.momentaryLufs()); o.d ("shortTermLufs", lm.shortTermLufs());
        o.i ("shortTermCount", lm.shortTermCount()); o.i ("droppedBlocks", lm.droppedBlocks());
        o.i ("droppedShortTermSamples", lm.droppedShortTermSamples()); o.i ("nonFiniteSubHops", (std::int64_t) lm.nonFiniteSubHops());
        o.i ("gatingBlockCount", lm.gatingBlockCount());
        for (double e : lm.gatingBlockEnergies()) o.d ("gatingBlockEnergy", e);
        o.d ("truePeakLinear", tm.truePeakLinear()); o.d ("samplePeakLinear", tm.samplePeakLinear()); o.d ("truePeakDb", tm.truePeakDb());
    }
    bool witness (std::string& f) const
    {
        char b[200];
        std::snprintf (b, sizeof b, "%.2f LUFS, LRA %.2f LU over %d short-term samples, %d gating blocks, true peak %.4f (sample %.4f)",
                       lm.integratedLufs(), lm.loudnessRangeLu(), lm.shortTermCount(), lm.gatingBlockCount(),
                       tm.truePeakLinear(), tm.samplePeakLinear());
        f = b;
        return lm.integratedLufs() > -40.0 && lm.loudnessRangeLu() > 1.0 && lm.shortTermCount() > 50
            && tm.truePeakLinear() > tm.samplePeakLinear() && lm.droppedBlocks() == 0;
    }
};

// …and the one output kept out of it: the LAST CALL's peak. Published, and call-scoped by its definition.
struct BlockPeakA : SolverMetersA
{
    void serialise (Bits& o) const { o.d ("truePeakLinearBlock", tm.truePeakLinearBlock()); o.d ("truePeakDbBlock", tm.truePeakDbBlock()); }
};

Programme renderedMaster (const Programme& src)
{
    Programme m = src;
    mastering::MasteringChain chain;
    mastering::OfflineRenderer r;
    const int nch = src.channels();
    std::vector<const float*> in;
    std::vector<float*> out;
    for (int c = 0; c < nch; ++c) { in.push_back (src.x[(std::size_t) c].data()); out.push_back (m.x[(std::size_t) c].data()); }
    const bool ok = chain.prepare (src.fs, nch, chainConfig (0)) && r.prepare (nch, 4096);
    if (ok) chain.setParams (chainParams (0));
    felitronics::test::ok (ok && r.render (chain, in.data(), out.data(), nch, (int) src.len()), "the master for the meters renders");
    return m;
}

void solverTieIn (const Programme& master)
{
    group ("split invariance — the solver reads the adapter's number");
    bool refused = false;
    const Bits ref = runOne<SolverMetersA> (master, cuts (master, kWhole, true), 4096, refused);
    double adapterLra = 0.0;
    for (std::size_t k = 0; k < ref.w.size(); ++k)
        if (std::string (ref.label[k]) == "loudnessRangeLu") { adapterLra = std::bit_cast<double> (ref.w[k]); break; }
    mastering::TargetLoudnessSolver solver;
    ok (solver.prepare (master.fs, master.channels(), 4096, 256, 4), "the solver prepares");
    std::vector<const float*> in;
    for (const auto& c : master.x) in.push_back (c.data());
    double plain = -1.0, heard = -2.0;
    const bool a = solver.measureInputLoudnessRange (in.data(), master.channels(), (int) master.len(), plain);
    const bool b = solver.measureInputLoudnessRange (in.data(), master.channels(), (int) master.len(), heard,
                                                     mastering::ProgressCallback { &keepGoing, nullptr });
    ok (! refused && a && b && std::bit_cast<std::uint64_t> (plain) == std::bit_cast<std::uint64_t> (adapterLra)
        && std::bit_cast<std::uint64_t> (heard) == std::bit_cast<std::uint64_t> (adapterLra),
        "measureInputLoudnessRange() is the adapter's one-call LRA bit for bit, with and without a callback (" + std::to_string (plain) + " LU)");
}

// THE DRAIN ON A STOPPED CHANNEL, made to matter. When a channel stops, ReferenceTruePeakMeter pushes its ring out
// through the FIR there and then (law 11a) — and a peak whose reconstruction was still inside that ring when the
// channel stopped is measured ONLY by that drain. Here the programme's largest reconstructed peak is a four-sample
// burst at fs/4 and phase pi/4 on the RIGHT channel, ending on the sample before the right channel stops, so a drain
// fired at a call boundary instead of at the stop — or not at all — would move the true peak itself.
void drainedPeakRows (const Programme& master)
{
    group ("split invariance — the solver's meters: the true peak lives in the drain of a stopped channel");
    Programme m = master;
    const std::int64_t n = m.len(), stop = n * 3 / 10 + 501, back = n * 7 / 10 + 13;
    for (int k = 0; k < 4; ++k) m.x[1][(std::size_t) (stop - 4 + k)] = (float) (1.5 * det::sin (0.7853981633974483 + 1.5707963267948966 * k));
    const Programme g = m.withSegments ({ { 0, stop, -1 }, { stop, back, 1 }, { back, n, -1 } });
    bool r0 = false;
    const Bits ref = runOne<SolverMetersA> (g, cuts (g, kWhole, true), 4096, r0);
    double tp = 0.0, sp = 0.0;
    for (std::size_t k = 0; k < ref.w.size(); ++k)
    {
        if (std::string (ref.label[k]) == "truePeakLinear") tp = std::bit_cast<double> (ref.w[k]);
        if (std::string (ref.label[k]) == "samplePeakLinear") sp = std::bit_cast<double> (ref.w[k]);
    }
    ok (! r0 && tp > 1.2 && sp < 1.1, "the edge burst is the programme's true peak (" + std::to_string (tp) + ") and not a sample peak ("
                                      + std::to_string (sp) + ")");
    const char* name = "LoudnessMeter + ReferenceTruePeakMeter, the peak in a stopped channel's drain";
    for (const Policy& pol : { kFix64, kFix480, kFix4k, Policy { "ragged 1..8192 #10", 0, 10 }, Policy { "ragged 1..8192 #11", 0, 11 } })
    {
        const auto calls = cuts (g, pol, true);
        bool r = false;
        const Bits got = runOne<SolverMetersA> (g, calls, 4096, r);
        record (name, std::string (pol.name) + " [" + callStats (calls) + "]", compare (ref, got, r), ref.w.size());
    }
}

// An untapped chain renders the same audio and counts the same statistics as a tapped one — the tap request is a
// separate path through process(), and it must not be a different stream.
template <int Topology>
void tapsAreNotTheStream (const Programme& p)
{
    group ("split invariance — asking for taps does not change the stream");
    bool ra = false, rb = false;
    const Bits a = runOne<ChainA<Topology, true>> (p, cuts (p, { "ragged", 0, 13 }, false), 4096, ra);
    const Bits b = runOne<ChainA<Topology, false>> (p, cuts (p, { "ragged", 0, 14 }, false), 4096, rb);
    auto untapped = [] (const Bits& x)
    {
        Bits y;
        for (std::size_t k = 0; k < x.w.size(); ++k)
            if (std::strncmp (x.label[k], "tap.", 4) != 0)
            {
                y.w.push_back (x.w[k]); y.label.push_back (x.label[k]); y.kind.push_back (x.kind[k]);
            }
        return y;
    };
    const Diff d = compare (untapped (a), untapped (b), ra || rb);
    ok (d.same(), std::string (ChainA<Topology>::kName) + ": tapped and untapped, cut differently, emit the same audio and "
                  "statistics (" + d.describe() + ")");
}

void blockPeakControl (const Programme& master)
{
    group ("split invariance — CONTROL: the call-scoped peak is reported as NOT invariant");
    bool r1 = false, r2 = false;
    const Bits a = runOne<BlockPeakA> (master, cuts (master, kWhole, true), 4096, r1);
    const Bits b = runOne<BlockPeakA> (master, cuts (master, { "ragged 1..8192 #1", 0, 1 }, true), 4096, r2);
    const Diff d = compare (a, b, r1 || r2);
    ok (! d.refused && ! d.same(), "ReferenceTruePeakMeter::truePeakLinearBlock() depends on the cut — the last call's peak, "
                                   "and not a programme number (" + d.describe() + ")");
    std::printf ("    truePeakLinearBlock: CONTROL — %s\n", d.describe().c_str());
}

} // namespace

int main()
{
    const Programme prog = chainFixture (4.0, 0x5eed1001u, true);
    suite<ChainA<0>> (prog);
    suite<ChainA<1>> (prog);
    suite<ChainA<0, false>> (prog);
    suite<ChainA<1, false>> (prog);
    suite<ChainA<0, true, true>> (prog);
    suite<ChainA<1, true, true>> (prog);
    tapsAreNotTheStream<0> (prog);
    tapsAreNotTheStream<1> (prog);
    rendererRows<0> (prog);
    rendererRows<1> (prog);
    progressObservation (prog);

    const Programme master = renderedMaster (chainFixture (12.0, 0x5eed1002u, false));
    suite<SolverMetersA> (master);
    drainedPeakRows (master);
    solverTieIn (master);
    blockPeakControl (master);

    printTable();
    return felitronics::test::report();
}
