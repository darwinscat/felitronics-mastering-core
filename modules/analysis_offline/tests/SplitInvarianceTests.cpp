// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Split invariance, measured the same way for every offline analyzer — the question a caller that feeds them in
// chunks it chooses itself (a time budget per step, not a block size) has to have answered before it is designed:
// does the report depend on where the `process()` calls were cut?
//
// Each suite beside this one already runs a law-8a re-slicing section of its own, on its own fixture, with its own
// list of slicings. This file is the other axis: ONE harness (tests/split_invariance.h), ONE set of cuts and ONE
// comparison for all of them, so "invariant" means the same thing on every row of the table it prints. Per analyzer:
//
//   · the same programme — a structured signal the analyzer measures (hum in quiet stretches, bursts in a band,
//     overs between samples, clipped runs, wide bass, a spectral wall on a 16-bit grid …) over a seeded bed —
//     through ONE call, fixed blocks of 64, 480 and 4096, and three seeded sequences of lengths 1..8192 with
//     zero-length calls interleaved (always one at the head and one at the tail);
//   · a SECOND prepared maxBlock (64 and 65536 against 4096), where the class takes one, under a ragged cut;
//   · four TIMELINES, wherever the class's own contract has them: a GAP in the middle — a stretch handed over at
//     width 0, i.e. `nch == 0, n > 0` clock-only calls (law 11d) — cut five ways; a gap at the HEAD; a programme that
//     FINISHES inside a gap; and a NARROWER stretch (2 -> 1 -> 2). A width change is part of the TIMELINE, not of
//     the cut: law 8a promises nothing about events that arrive per call, so every run carries it at the same
//     samples and only the cutting inside and around it moves. StereoBandBursts and PeakExcursions refuse
//     `nch < 1` and any width change, WaveformPeaks and StereoColumns have an EXACT width: for those four there are
//     no timelines, by their contracts;
//   · FINISH ON A PREFIX: the programme stopped at two positions no window, hop or block lines up with, cut
//     raggedly and finished there, against the same prefix in one call — and the same prefix read BEFORE
//     finish(), which is what a caller reporting progress mid-stream reads. For WaveformPeaks and StereoColumns,
//     whose geometry is a function of the prepared TOTAL length, there is no finish: the prefix is the
//     in-progress state of the full-length preparation, which is what a caller stopping early would hold;
//   · the CONTROL: the same adapter with the last sample of every call moved by one ulp, which the comparison
//     must see — the smallest thing a per-call flush, rounding or reset could do.
//
// EVERY output the stream decides is compared, as bits: scalars, per-channel records, per-band rows, event lists,
// histograms, lifecycle state, and the trace or observer stream where the class publishes one — because a final
// report can agree while its intermediates moved (felitronics-core docs/LAW8-KWEIGHTING.md:78). What is NOT read is
// what prepare() alone decides — parameter echoes and window/bin geometry — which no cut can reach. Each word
// carries a label and a type, so a divergence is reported as WHICH output, by HOW MUCH, at WHICH cut.
//
// SpectrumFrames has no process(): it is fed a sample at a time (push / tick) and the call boundary exists only in
// its consumers. It is driven here through the consumers' own loop — the absent channel of a narrow call becomes a
// hole — so that row checks the producer's clock and its hole accounting under the same cuts, not a call contract
// it does not have.
//
// MEASURED, v0.1.0 on felitronics-core v0.52.0: all twelve INVARIANT — 246 cut rows, every fixture exercised, every
// control caught — on Apple clang 21 arm64, gcc 14.2 x86-64, MSVC 19.44 x64 and wasm32 (emsdk 6.0.9). The table at
// the end of the run is the record. Two contract edges found beside it were first pinned as known findings and are
// fixed and ASSERTED now (the last two groups): BandCrest accepts a clock-only call whose plane array is null (law
// 11a allows it), and StereoBandBursts and PeakExcursions no longer latch their width on an EMPTY call (law 11d: n == 0
// moves nothing). Neither fix moves a number — every cut row above is unchanged.

#include <felitronics/analysis/BandBursts.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/ClipDetector.h>
#include <felitronics/analysis/HumDetector.h>
#include <felitronics/analysis/LowEnd.h>
#include <felitronics/analysis/PeakExcursions.h>
#include <felitronics/analysis/ProgrammeReport.h>
#include <felitronics/analysis/SourceForensics.h>
#include <felitronics/analysis/SpectrumFrames.h>
#include <felitronics/analysis/StereoBandBursts.h>
#include <felitronics/analysis/StereoColumns.h>
#include <felitronics/analysis/WaveformPeaks.h>
#include <felitronics/core/DetMath.h>
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

namespace
{

//==============================================================================
// THE ADAPTERS — one per analyzer. `serialise` reads EVERY public output the class has.

//------------------------------------------------------------------------------ ProgrammeReport
struct ProgrammeReportA
{
    static constexpr const char* kName = "ProgrammeReport";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::ProgrammeReport r;
    std::vector<analysis::ProgrammeTraceEvent> trace = std::vector<analysis::ProgrammeTraceEvent> (1u << 15);

    bool prepare (const Programme& p, int maxBlock)
    {
        analysis::ProgrammeReportParams q;
        q.maxDurationSec = 60.0;
        r.setParams (q);
        if (! r.prepare (p.fs, maxBlock, p.channels())) return false;
        r.setTraceBuffer (trace.data(), trace.size());
        return true;
    }
    bool process (const float* const* in, int nch, int n) { return r.process (in, nch, n); }
    void finish() { r.finish(); }
    void serialise (Bits& o) const
    {
        const auto& rep = r.report();
        o.b ("finished", r.isFinished()); o.i ("samplesProcessed", r.samplesProcessed()); o.i ("channels", r.channels());
        o.d ("sampleRate", rep.sampleRate);
        rep.visitValues ([&o] (const char* name, int, const analysis::ProgrammeValue& v)
                         { o.d (name, v.value); o.b (name, v.valid); o.i (name, (std::int64_t) v.reason); });
        rep.visitCounts ([&o] (const char* name, int, std::int64_t v) { o.i (name, v); });
        o.i ("traceCount", r.traceCount());
        o.i ("traceOverflow", r.traceOverflow());
        const std::int64_t stored = std::min<std::int64_t> (r.traceCount(), (std::int64_t) trace.size());
        for (std::int64_t k = 0; k < stored; ++k)
        {
            const auto& e = trace[(std::size_t) k];
            o.i ("trace.kind", (std::int64_t) e.kind); o.i ("trace.channel", e.channel); o.i ("trace.index", e.index);
            o.i ("trace.begin", e.begin); o.i ("trace.end", e.end);
            o.d ("trace.a", e.a); o.d ("trace.b", e.b); o.d ("trace.c", e.c);
        }
    }

    bool witness (std::string& f) const
    {
        const auto& rep = r.report();
        char b[256];
        std::snprintf (b, sizeof b, "LRA %.2f LU over %lld gated observations, infra-low %.4f, lead/trail digital silence %lld/%lld, "
                       "corr %.3f, %lld trace events", rep.lraLu.value, (long long) rep.shortTermGatedObservations,
                       rep.infraLowFraction.value, (long long) rep.leadingDigitalSilenceSamples,
                       (long long) rep.trailingDigitalSilenceSamples, rep.stereoCorrelation.value, (long long) r.traceCount());
        f = b;
        return rep.lraLu.valid && rep.lraLu.value > 1.0 && rep.infraLowFraction.valid && rep.stereoCorrelation.valid
            && rep.leadingDigitalSilenceSamples > 0 && rep.trailingDigitalSilenceSamples > 0
            && rep.leadingSilenceSamples.value > rep.leadingDigitalSilenceSamples && r.traceOverflow() == 0;
    }
};

// Loudness steps (so LRA and the short-term percentiles have a distribution), infra-low content, a stereo image
// that is neither mono nor independent, leading digital silence then a sub-threshold hiss, a fade cut short, and
// trailing digital silence.
Programme programmeReportFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 2, 11 * 48000 + 1234, 0x5eed0001u);
    const std::int64_t lead = 24000, hiss = 40000, end = g.len() - 9000;
    for (int c = 0; c < 2; ++c) g.noise (c, lead, hiss, 3.0e-6);                     // under -96 dBFS: "silence"
    for (int c = 0; c < 2; ++c)
    {
        g.noise (c, hiss, end, 0.05);
        g.tone (c, hiss, end, 22.0, 0.08, 0.3 * c);                                   // under the 30 Hz split
        g.tone (c, hiss, end, 41.0, 0.12);
        g.bursts (c, hiss, end, 19200, 9600, 480, 997.0, 0.25, 0.9 * c);              // 400 ms on / off
    }
    for (std::int64_t s = hiss; s < end; s += 72000) g.scale (0, s, s + 36000, 0.2), g.scale (1, s, s + 36000, 0.2);
    g.fade (0, end - 30000, end, false);
    g.fade (1, end - 30000, end, false);
    g.scale (1, 0, g.len(), 0.8);                                                     // a balance, not a copy
    return g.p;
}

//------------------------------------------------------------------------------ SourceForensics
struct SourceForensicsA
{
    static constexpr const char* kName = "SourceForensics";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::SourceForensics sf;
    Bits trace;

    static void putGrid (Bits& o, const analysis::SampleGrid& g)
    {
        o.b ("grid.valid", g.valid); o.i ("grid.reason", (std::int64_t) g.reason); o.i ("grid.gridExponent", g.gridExponent);
        o.b ("grid.pcmCompatible", g.pcmCompatible); o.b ("grid.outsidePcmRange", g.outsidePcmRange);
        o.i ("grid.minExactPcmBits", g.minExactPcmBits); o.i ("grid.robustGridExponent", g.robustGridExponent);
        o.i ("grid.robustPcmBits", g.robustPcmBits); o.d ("grid.absPeak", g.absPeak); o.d ("grid.sampleMin", g.sampleMin);
        o.d ("grid.sampleMax", g.sampleMax); o.i ("grid.nonZeroSamples", g.nonZeroSamples); o.i ("grid.zeroSamples", g.zeroSamples);
        o.i ("grid.nonFiniteSamples", g.nonFiniteSamples); o.i ("grid.absentSamples", g.absentSamples);
        o.i ("grid.offGridSamples", g.offGridSamples); o.i ("grid.firstOffGridSample", g.firstOffGridSample);
        o.i ("grid.firstMaxGridSample", g.firstMaxGridSample); o.i ("grid.distinctValues", g.distinctValues);
        o.b ("grid.distinctComplete", g.distinctComplete);
        for (int bits : { 16, 24, 32 }) o.i ("grid.alwaysZeroLowBits", g.alwaysZeroLowBits (bits));
    }
    static void putWall (Bits& o, const analysis::SpectralWall& w)
    {
        o.b ("wall.valid", w.valid); o.i ("wall.reason", (std::int64_t) w.reason); o.b ("wall.sharp", w.sharp);
        o.b ("wall.nearNyquist", w.nearNyquist); o.d ("wall.cutoffHz", w.cutoffHz);
        o.d ("wall.cutoffFractionOfNyquist", w.cutoffFractionOfNyquist); o.d ("wall.steepestHz", w.steepestHz);
        o.d ("wall.transitionEndHz", w.transitionEndHz); o.d ("wall.transitionHz", w.transitionHz);
        o.b ("wall.transitionClipped", w.transitionClipped); o.b ("wall.truncatedAtNyquist", w.truncatedAtNyquist);
        o.d ("wall.plateauPower", w.plateauPower); o.d ("wall.floorLocalPower", w.floorLocalPower);
        o.d ("wall.maxAbovePower", w.maxAbovePower); o.d ("wall.sufMaxPower", w.sufMaxPower);
        o.i ("wall.exemptedCells", w.exemptedCells); o.d ("wall.dropDb", w.dropDb); o.d ("wall.strictDropDb", w.strictDropDb);
        o.d ("wall.localDropDb", w.localDropDb); o.d ("wall.recoveryDb", w.recoveryDb);
        o.d ("wall.plateauSpreadDb", w.plateauSpreadDb); o.d ("wall.steepnessDbPerOctave", w.steepnessDbPerOctave);
        o.b ("wall.secondValid", w.secondValid); o.b ("wall.secondSharp", w.secondSharp);
        o.b ("wall.secondTransitionClipped", w.secondTransitionClipped);
        o.b ("wall.secondTruncatedAtNyquist", w.secondTruncatedAtNyquist); o.i ("wall.secondReason", (std::int64_t) w.secondReason);
        o.d ("wall.secondCutoffHz", w.secondCutoffHz); o.d ("wall.secondDropDb", w.secondDropDb);
        o.d ("wall.secondTransitionHz", w.secondTransitionHz); o.b ("wall.emptyAboveValid", w.emptyAboveValid);
        o.i ("wall.emptyAboveReason", (std::int64_t) w.emptyAboveReason); o.d ("wall.emptyAboveHz", w.emptyAboveHz);
        o.d ("wall.emptyAboveFractionOfNyquist", w.emptyAboveFractionOfNyquist);
        o.d ("wall.emptyThresholdPower", w.emptyThresholdPower); o.d ("wall.peakCellPower", w.peakCellPower);
        o.d ("wall.binHz", w.binHz); o.d ("wall.cellHz", w.cellHz); o.d ("wall.nyquistHz", w.nyquistHz);
        o.d ("wall.searchFromHz", w.searchFromHz); o.d ("wall.searchToHz", w.searchToHz);
        o.i ("wall.framesUsed", w.framesUsed); o.i ("wall.framesHoled", w.framesHoled);
    }
    // The observer fires INSIDE process(), at the sample the event happened on — the canonical moment.
    static void observer (void* user, const analysis::SourceForensics& s, analysis::ForensicsEvent ev, int ch) noexcept
    {
        Bits& o = *static_cast<Bits*> (user);
        o.i ("event", (std::int64_t) ev); o.i ("event.channel", ch); o.i ("event.at", s.samplesProcessed());
        o.i ("event.frameIndex", s.frames().frameIndex()); o.i ("event.frameStart", s.frames().frameStart());
        o.i ("event.tailUncovered", s.tailUncoveredSamples());
        for (int c = 0; c < s.channels(); ++c)
        {
            o.b ("event.lastFrameUsed", s.lastFrameUsed (c)); o.i ("event.framesUsed", s.framesUsed (c));
            o.i ("event.framesHoled", s.framesHoled (c)); o.b ("event.frameFinite", s.frames().frameFinite (c));
            putGrid (o, s.sampleGrid (c));
            const std::int64_t* h = s.gridExponentHistogram (c);
            for (int k = 0; k < analysis::SourceForensics::gridExponentBuckets(); ++k) o.i ("event.kHist", h[k]);
            if (ev != analysis::ForensicsEvent::FrameClosed) continue;
            const double* ps = s.powerSum (c);
            const double* pc = s.powerSumCompensation (c);
            for (int b = 0; b < s.bins(); ++b) { o.d ("event.powerSum", ps[b]); o.d ("event.powerComp", pc[b]); }
        }
    }

    bool prepare (const Programme& p, int maxBlock)
    {
        analysis::SourceForensicsParams q;
        q.fftOrder = 12;                 // 85 ms windows: ~70 frames over the fixture, so the accumulation is long
        q.maxDistinctValues = 1 << 10;   // small enough that the distinct set saturates mid-programme
        sf.setParams (q);
        sf.setObserver (&observer, &trace);
        return sf.prepare (p.fs, maxBlock, p.channels());
    }
    bool process (const float* const* in, int nch, int n) { return sf.process (in, nch, n); }
    void finish() { sf.finish(); }
    void serialise (Bits& o) const
    {
        o = trace;
        o.b ("finished", sf.isFinished());
        o.i ("samplesProcessed", sf.samplesProcessed()); o.i ("tailUncovered", sf.tailUncoveredSamples());
        o.i ("frameCount", sf.frames().frameCount()); o.i ("frames.totalSamples", sf.frames().totalSamples());
        o.i ("frames.frameIndex", sf.frames().frameIndex()); o.i ("frames.frameEnd", sf.frames().frameEnd());
        for (int c = 0; c < sf.channels(); ++c)
        {
            putWall (o, sf.wall (c));
            putGrid (o, sf.sampleGrid (c));
            o.i ("framesUsed", sf.framesUsed (c)); o.i ("framesHoled", sf.framesHoled (c)); o.b ("lastFrameUsed", sf.lastFrameUsed (c));
            for (int b = 0; b < sf.bins(); ++b) o.d ("meanPower", sf.meanPower (c, b));
            // the final snapshot of the raw evidence, and the last frame's own spectrum
            const double* ps = sf.powerSum (c);
            const double* pc = sf.powerSumCompensation (c);
            const double* pw = sf.frames().power (c);
            o.b ("frames.frameFinite", sf.frames().frameFinite (c));
            for (int b = 0; b < sf.bins(); ++b) { o.d ("powerSum", ps[b]); o.d ("powerComp", pc[b]); o.d ("frames.power", pw[b]); }
            const std::int64_t* h = sf.gridExponentHistogram (c);
            for (int k = 0; k < analysis::SourceForensics::gridExponentBuckets(); ++k) o.i ("kHist", h[k]);
        }
        putWall (o, sf.wall());
    }

    bool witness (std::string& f) const
    {
        const auto w = sf.wall (0);
        const auto g0 = sf.sampleGrid (0), g1 = sf.sampleGrid (1);
        char b[256];
        std::snprintf (b, sizeof b, "wall %.0f Hz (sharp %d, drop %.1f dB), %lld frames, grid %d bits / off-grid %lld, distinct "
                       "complete %d", w.cutoffHz, (int) w.sharp, w.dropDb, (long long) sf.frames().frameCount(),
                       g0.minExactPcmBits, (long long) g1.offGridSamples, (int) g0.distinctComplete);
        f = b;
        return w.valid && w.cutoffHz > 14000.0 && w.cutoffHz < 17000.0 && sf.frames().frameCount() > 50
            && g0.minExactPcmBits == 16 && g1.offGridSamples == 1 && ! g0.distinctComplete;
    }
};

// Band-limited noise with a sharp wall at 16 kHz (a 255-tap windowed-sinc low-pass of a seeded white source), on a
// 16-bit grid, with digital silence at the head, one off-grid sample and a gap of exact zeros.
Programme sourceForensicsFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 2, 3 * 48000 + 777, 0x5eed0002u);
    constexpr int taps = 255, half = taps / 2;
    double h[taps];
    for (int k = 0; k < taps; ++k)
    {
        const double t = (double) (k - half), fc = 16000.0 / fs;
        const double sinc = k == half ? 2.0 * fc : det::sin (2.0 * core::kPi * fc * t) / (core::kPi * t);
        h[k] = sinc * (0.42 - 0.5 * det::cos (2.0 * core::kPi * k / (taps - 1)) + 0.08 * det::cos (4.0 * core::kPi * k / (taps - 1)));
    }
    std::vector<double> white ((std::size_t) g.len() + taps);
    for (int c = 0; c < 2; ++c)
    {
        for (auto& v : white) v = 0.4 * g.rng.bipolar();
        for (std::int64_t i = 4000; i < g.len(); ++i)
        {
            double acc = 0.0;
            for (int k = 0; k < taps; ++k) acc += h[k] * white[(std::size_t) i + (std::size_t) k];
            g.at (c, i) = (float) (std::round (acc * 32768.0) / 32768.0);           // a 16-bit word
        }
    }
    g.at (1, 70001) = 0.1f;                                                         // off the 24-bit grid
    g.zero (0, 90000, 91000);
    return g.p;
}

//------------------------------------------------------------------------------ HumDetector
struct HumDetectorA
{
    static constexpr const char* kName = "HumDetector";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::HumDetector d;
    analysis::HumDetectorParams q;

    static void putPeak (Bits& o, const analysis::HumPeak& p)
    {
        o.b ("peak.found", p.found); o.b ("peak.prominent", p.prominent); o.b ("peak.accepted", p.accepted);
        o.i ("peak.bin", p.bin); o.d ("peak.hz", p.hz); o.d ("peak.tonePower", p.tonePower);
        o.d ("peak.peakBinPower", p.peakBinPower); o.d ("peak.floorPower", p.floorPower); o.d ("peak.prominenceDb", p.prominenceDb);
    }
    static void putReport (Bits& o, const analysis::HumReport& r)
    {
        o.b ("report.valid", r.valid); o.i ("report.reason", (std::int64_t) r.reason); o.i ("report.mains", (std::int64_t) r.mains);
        putPeak (o, r.line);
        o.d ("report.fundamentalHz", r.fundamentalHz); o.b ("report.fundamentalObserved", r.fundamentalObserved);
        o.b ("report.fundamentalDerived", r.fundamentalDerived); o.i ("report.baseHarmonic", r.baseHarmonic);
        o.i ("report.harmonicsObserved", r.harmonicsObserved); o.i ("report.frames", r.frames);
        o.i ("report.finiteFrames", r.finiteFrames); o.i ("report.holedFrames", r.holedFrames);
        o.i ("report.quietFrames", r.quietFrames); o.i ("report.silentFrames", r.silentFrames);
        o.i ("report.quietStretches", r.quietStretches); o.i ("report.eligibleStretches", r.eligibleStretches);
        o.i ("report.storedStretches", r.storedStretches); o.b ("report.stretchesComplete", r.stretchesComplete);
        o.i ("report.totalSamples", r.totalSamples); o.i ("report.tailUncoveredSamples", r.tailUncoveredSamples);
        o.d ("report.binHz", r.binHz); o.i ("report.windowSamples", r.windowSamples);
    }

    bool prepare (const Programme& p, int maxBlock)
    {
        q.traceCapacity = 4096;
        q.maxStretches = 64;
        d.setParams (q);
        return d.prepare (p.fs, maxBlock, p.channels());
    }
    bool process (const float* const* in, int nch, int n) { return d.process (in, nch, n); }
    void finish() { d.finish(); }
    void serialise (Bits& o) const
    {
        o.b ("finished", d.isFinished());
        o.i ("samplesProcessed", d.samplesProcessed()); o.i ("traceCount", d.traceCount()); o.b ("traceComplete", d.traceComplete());
        for (std::int64_t k = 0; k < d.storedTraceCount(); ++k)
        {
            const auto t = d.traceAt (k);
            o.i ("trace.frameStart", t.frameStart); o.i ("trace.frameEnd", t.frameEnd); o.i ("trace.stretchIndex", t.stretchIndex);
            o.d ("trace.meanSquare", t.meanSquare); o.d ("trace.bandSum", t.bandSum);
            for (int h = 0; h < 4; ++h)
            {
                o.d ("trace.hypHz", t.hypHz[h]); o.d ("trace.hypTone", t.hypTone[h]); o.d ("trace.hypFloor", t.hypFloor[h]);
                o.i ("trace.hypBin", t.hypBin[h]);
            }
            o.i ("trace.hypAccepted", t.hypAccepted); o.i ("trace.channel", t.channel);
            o.i ("trace.finite", t.finite); o.i ("trace.quiet", t.quiet);
        }
        o.i ("strongestChannel", d.strongestChannel());
        putReport (o, d.report());
        for (int c = 0; c < d.channels(); ++c)
        {
            putReport (o, d.report (c));
            for (int cand = 0; cand < analysis::HumDetector::kCandidates; ++cand)
            {
                const auto k = d.candidate (c, cand);
                o.d ("cand.nominalHz", k.nominalHz); o.b ("cand.baseFound", k.baseFound); o.i ("cand.baseHarmonic", k.baseHarmonic);
                o.d ("cand.fundamentalHz", k.fundamentalHz); o.b ("cand.fundamentalObserved", k.fundamentalObserved);
                putPeak (o, k.base); putPeak (o, k.windowPeak);
                o.i ("cand.harmonicsObserved", k.harmonicsObserved); o.i ("cand.lowestHarmonicObserved", k.lowestHarmonicObserved);
                o.b ("cand.combWithoutBase", k.combWithoutBase); o.d ("cand.combFundamentalHz", k.combFundamentalHz);
                o.i ("cand.stretchObservations", k.stretchObservations); o.i ("cand.stretchOffTolerance", k.stretchOffTolerance);
                o.i ("cand.frameObservations", k.frameObservations); o.d ("cand.stretchSpreadHz", k.stretchSpreadHz);
                o.d ("cand.frameSpreadHz", k.frameSpreadHz); o.d ("cand.maxIntraStretchSpreadHz", k.maxIntraStretchSpreadHz);
                o.b ("cand.stationary", k.stationary); o.b ("cand.passed", k.passed);
                for (int h = 1; h <= q.maxHarmonic; ++h)
                {
                    const auto hh = d.harmonic (c, cand, h);
                    o.i ("harm.index", hh.index); o.b ("harm.inBand", hh.inBand); putPeak (o, hh.peak);
                }
            }
            for (int h = 1; h <= q.maxHarmonic; ++h) { const auto hh = d.harmonic (c, h); o.i ("harmSel.index", hh.index); o.b ("harmSel.inBand", hh.inBand); putPeak (o, hh.peak); }
            o.i ("stretchCount", d.stretchCount (c)); o.b ("stretchesComplete", d.stretchesComplete (c));
            for (std::int64_t k = 0; k < d.storedStretchCount (c); ++k)
            {
                const auto s = d.stretch (c, k);
                o.i ("stretch.index", s.index); o.i ("stretch.start", s.startSample); o.i ("stretch.end", s.endSample);
                o.i ("stretch.frames", s.frames);
            }
            o.i ("selectedFrames", d.selectedFrames (c));
            const double* band = d.bandPowerSum (c);
            for (int k = 0; k < d.bandBins(); ++k) o.d ("bandPowerSum", band[k]);
        }
    }

    bool witness (std::string& f) const
    {
        const auto r = d.report (0);
        char b[256];
        std::snprintf (b, sizeof b, "mains %d at %.3f Hz, %lld harmonics, %lld frames / %lld quiet in %lld stretches",
                       (int) r.mains, r.line.hz, (long long) r.harmonicsObserved, (long long) r.frames,
                       (long long) r.quietFrames, (long long) r.quietStretches);
        f = b;
        return r.valid && r.mains == analysis::HumMains::Hz50 && r.quietStretches == 2 && d.traceComplete();
    }
};

// HumDetectorTests' shape at 12 kHz (AUTO order 15: N 32768, hop 16384): two quiet stretches carrying 50.14 Hz and
// its comb under a faint bed, a loud programme between them, a quiet tail — both channels, different phases.
Programme humFixture()
{
    const double fs = 12000.0;
    const std::int64_t N = 32768, H = 16384;
    const std::int64_t qa1 = N + 2 * H, l1 = qa1 + 4 * H, qb1 = l1 + N + 3 * H;
    Gen g (fs, 2, qb1 + 5000, 0x5eed0003u);
    for (int c = 0; c < 2; ++c)
    {
        g.noise (c, 0, qa1, 3.0e-4);
        g.noise (c, qa1, l1, 0.3);
        g.noise (c, l1, g.len(), 3.0e-4);
        for (int h = 1; h <= 4; ++h)
        {
            const double a = 1.0e-3 / (double) (h * h);
            g.tone (c, 0, qa1, 50.14 * h, a, 0.4 * c + 0.1 * h);
            g.tone (c, l1, g.len(), 50.14 * h, a, 0.7 + 0.4 * c);
        }
        g.tone (c, qa1, l1, 49.0, 0.2);                                               // a G1 in the loud part
    }
    return g.p;
}

//------------------------------------------------------------------------------ LowEnd
struct LowEndA
{
    static constexpr const char* kName = "LowEnd";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::LowEnd le;
    Bits trace;

    static void onTrace (void* user, const analysis::LowEndTrace& t)
    {
        Bits& o = *static_cast<Bits*> (user);
        o.i ("trace.kind", (std::int64_t) t.kind); o.i ("trace.index", t.index); o.i ("trace.start", t.start);
        o.i ("trace.end", t.end); o.b ("trace.valid", t.valid); o.i ("trace.finiteSamples", t.finiteSamples);
        o.i ("trace.holes", t.holes); o.d ("trace.midEnergy", t.midEnergy); o.d ("trace.sideEnergy", t.sideEnergy);
        o.d ("trace.frameEnergy", t.frameEnergy); o.i ("trace.bandCount", t.bandCount);
        o.b ("trace.bandMidPresent", t.bandMid != nullptr); o.b ("trace.bandSidePresent", t.bandSide != nullptr);
        for (int b = 0; b < t.bandCount; ++b)
        {
            o.d ("trace.bandMid", t.bandMid != nullptr ? t.bandMid[b] : 0.0);
            o.d ("trace.bandSide", t.bandSide != nullptr ? t.bandSide[b] : 0.0);
        }
    }

    bool prepare (const Programme& p, int maxBlock)
    {
        if (! le.prepare (p.fs, maxBlock, p.channels())) return false;
        le.setTrace (&onTrace, &trace);
        return true;
    }
    bool process (const float* const* in, int nch, int n) { return le.process (in, nch, n); }
    void finish() { (void) le.finish(); }
    void serialise (Bits& o) const
    {
        o = trace;
        o.b ("finished", le.isFinished()); o.i ("channels", le.channels()); o.b ("widthValid", le.widthValid());
        o.b ("noteValid", le.noteValid()); o.i ("widthReason", (std::int64_t) le.widthReason());
        o.i ("noteReason", (std::int64_t) le.noteReason()); o.i ("samplesProcessed", le.samplesProcessed());
        o.i ("blockSamples", le.blockSamples()); o.i ("analysedChannels", le.analysedChannels()); o.d ("crossoverHz", le.crossoverHz());
        o.d ("lowMidEnergy", le.lowMidEnergy()); o.d ("lowSideEnergy", le.lowSideEnergy()); o.d ("highMidEnergy", le.highMidEnergy());
        o.d ("highSideEnergy", le.highSideEnergy()); o.d ("rawMidEnergy", le.rawMidEnergy()); o.d ("rawSideEnergy", le.rawSideEnergy());
        o.d ("lowBandEnergy", le.lowBandEnergy()); o.d ("lowSideFraction", le.lowSideFraction()); o.d ("infraLowShare", le.infraLowShare());
        o.d ("highSideFraction", le.highSideFraction()); o.d ("rawSideFraction", le.rawSideFraction());
        o.i ("finiteSamples", le.finiteSamples()); o.i ("holeSamples", le.holeSamples()); o.i ("nonFiniteSamples", le.nonFiniteSamples());
        o.i ("absentSamples", le.absentSamples()); o.i ("filterNonFiniteSamples", le.filterNonFiniteSamples());
        o.i ("firstHoleSample", le.firstHoleSample()); o.i ("lastHoleSample", le.lastHoleSample());
        o.i ("blockCount", le.blockCount()); o.i ("storedBlockCount", le.storedBlockCount()); o.b ("blocksComplete", le.blocksComplete());
        for (std::int64_t b = 0; b < le.storedBlockCount(); ++b)
        {
            const auto r = le.block (b);
            o.i ("block.index", r.index); o.i ("block.start", r.start); o.i ("block.samples", r.samples);
            o.i ("block.finiteSamples", r.finiteSamples); o.i ("block.holes", r.holes); o.d ("block.midEnergy", r.midEnergy);
            o.d ("block.sideEnergy", r.sideEnergy); o.b ("block.valid", r.valid);
        }
        for (int b = 0; b < analysis::LowEnd::kHistogramBins; ++b) o.i ("histogram", le.histogram (b));
        o.i ("skippedBlocks", le.skippedBlocks()); o.i ("histogramSamples", le.histogramSamples());
        o.i ("worstFractionBlock", le.worstFractionBlock()); o.d ("worstFraction", le.worstFraction());
        o.d ("worstFractionEnergy", le.worstFractionEnergy()); o.i ("peakEnergyBlock", le.peakEnergyBlock());
        o.d ("peakBlockEnergy", le.peakBlockEnergy()); o.d ("peakEnergyBlockFraction", le.peakEnergyBlockFraction());
        o.i ("peakSideEnergyBlock", le.peakSideEnergyBlock()); o.d ("peakBlockSideEnergy", le.peakBlockSideEnergy());
        o.d ("peakLowSideAmplitude", le.peakLowSideAmplitude()); o.i ("peakLowSideAmplitudeAt", le.peakLowSideAmplitudeAt());
        o.i ("bandCount", le.bandCount()); o.i ("underResolvedBands", le.underResolvedBands());
        o.i ("firstResolvedBand", le.firstResolvedBand()); o.d ("resolvedAboveHz", le.resolvedAboveHz());
        o.i ("dutyFrames", le.dutyFrames()); o.d ("dutyThresholdDb", le.dutyThresholdDb());
        for (int b = 0; b < le.bandCount(); ++b)
        {
            const auto r = le.band (b);
            o.i ("band.midi", r.midi); o.d ("band.centreHz", r.centreHz); o.d ("band.widthHz", r.widthHz);
            o.d ("band.binsPerBand", r.binsPerBand); o.d ("band.midEnergy", r.midEnergy); o.d ("band.sideEnergy", r.sideEnergy);
            o.d ("band.energy", r.energy); o.d ("band.density", r.density); o.d ("band.centroidHz", r.centroidHz);
            o.d ("band.centsOffset", r.centsOffset);
            o.i ("band.dutyCount", le.dutyCount (b)); o.d ("band.duty", le.duty (b));
            o.d ("band.levelWhenOnDb", le.levelWhenOnDb (b)); o.d ("band.marginWhenOnDb", le.marginWhenOnDb (b));
        }
        for (double fc : { 30.0, 60.0, 90.0, 120.0 }) o.d ("sideFractionBelow", le.sideFractionBelow (fc));
        for (double duty : { 0.05, 0.3, 0.8 })
        {
            const auto lo = le.lowestOccupiedBand (duty);
            o.i ("lowest.band", lo.band); o.i ("lowest.midi", lo.midi); o.d ("lowest.centreHz", lo.centreHz);
            o.i ("lowest.count", lo.count); o.d ("lowest.duty", lo.duty); o.d ("lowest.levelWhenOnDb", lo.levelWhenOnDb);
            o.d ("lowest.marginWhenOnDb", lo.marginWhenOnDb);
        }
        o.i ("peakBand", le.peakBand()); o.i ("peakDensityBand", le.peakDensityBand()); o.i ("secondBand", le.secondBand());
        o.d ("backgroundDensity", le.backgroundDensity()); o.d ("peakBandEnergy", le.peakBandEnergy());
        o.d ("peakBandWidthHz", le.peakBandWidthHz()); o.d ("secondBandEnergy", le.secondBandEnergy());
        o.d ("totalBandEnergy", le.totalBandEnergy()); o.d ("frameEnergy", le.frameEnergy()); o.d ("bandRangeShare", le.bandRangeShare());
        o.d ("peakShare", le.peakShare()); o.i ("peakMidi", le.peakMidi()); o.d ("peakNoteHz", le.peakNoteHz());
        o.d ("peakCentroidHz", le.peakCentroidHz()); o.d ("peakCentsOffset", le.peakCentsOffset());
        o.d ("peakBandSideFraction", le.peakBandSideFraction());
        o.i ("usedFrames", le.usedFrames()); o.i ("holedFrames", le.holedFrames()); o.i ("tailUncoveredSamples", le.tailUncoveredSamples());
        o.i ("windowSamples", le.windowSamples()); o.i ("hopSamples", le.hopSamples()); o.d ("binHz", le.binHz());
    }

    bool witness (std::string& f) const
    {
        char b[256];
        std::snprintf (b, sizeof b, "peak MIDI %d (%.2f Hz), low side fraction %.4f, %lld blocks, %lld frames used, infra-low %.4f",
                       le.peakMidi(), le.peakNoteHz(), le.lowSideFraction(), (long long) le.blockCount(),
                       (long long) le.usedFrames(), le.infraLowShare());
        f = b;
        return le.widthValid() && le.noteValid() && le.peakMidi() == 33 && le.usedFrames() >= 4 && le.lowSideFraction() > 0.0;
    }
};

// A sustained A1 (55 Hz) with harmonics in Mid, kicks every half second, a stretch of anti-phase 70 Hz (wide bass),
// an infra-low rumble, and a mid-range bed.
Programme lowEndFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 2, 10 * 48000 + 4321, 0x5eed0004u);
    for (int c = 0; c < 2; ++c)
    {
        g.noise (c, 0, g.len(), 0.02);
        g.tone (c, 0, g.len(), 55.0, 0.3);
        g.tone (c, 0, g.len(), 110.0, 0.12);
        g.tone (c, 0, g.len(), 16.0, 0.05);
        g.tone (c, 0, g.len(), 440.0, 0.05, 1.1 * c);
    }
    g.tone (0, 150000, 300000, 70.0, 0.25);
    g.tone (1, 150000, 300000, 70.0, -0.25);
    for (std::int64_t k = 3000; k < g.len(); k += 24000) { g.hit (0, k, 0.6, 50.0, 90.0); g.hit (1, k, 0.55, 50.0, 90.0); }
    return g.p;
}

//------------------------------------------------------------------------------ BandBursts (and its stereo sibling)
void putBandBursts (Bits& o, const analysis::BandBursts& b)
{
    o.b ("finished", b.isFinished()); o.i ("samplesProcessed", b.samplesProcessed()); o.i ("channels", b.channels());
    o.d ("sampleRate", b.sampleRate()); o.i ("hopSamples", b.hopSamples()); o.i ("baselineHops", b.baselineHops());
    o.d ("bandLowHz", b.bandLowHz()); o.d ("bandHighHz", b.bandHighHz());
    o.i ("hopCount", b.hopCount()); o.i ("eligibleHops", b.eligibleHops()); o.i ("zeroBaselineHops", b.zeroBaselineHops());
    o.i ("burstHops", b.burstHops()); o.i ("damagedHops", b.damagedHops()); o.i ("tailPartialSamples", b.tailPartialSamples());
    o.d ("tailPartialEnergy", b.tailPartialEnergy());
    o.i ("eventCount", b.eventCount()); o.i ("storedEventCount", b.storedEventCount()); o.b ("eventsComplete", b.eventsComplete());
    for (std::int64_t k = 0; k < b.storedEventCount(); ++k)
    {
        const auto e = b.event (k);
        o.i ("event.start", e.start); o.i ("event.length", e.length); o.i ("event.peakAt", e.peakAt);
        o.d ("event.peakPower", e.peakPower); o.d ("event.peakBaseline", e.peakBaseline); o.d ("event.peakExcessDb", e.peakExcessDb);
        o.d ("event.peakWidePower", e.peakWidePower); o.d ("event.energy", e.energy); o.i ("event.hops", e.hops);
        o.b ("event.touchedNonFinite", e.touchedNonFinite); o.b ("event.baselineTouchedNonFinite", e.baselineTouchedNonFinite);
        o.b ("event.closedByFinish", e.closedByFinish);
    }
    o.b ("eventsValid", b.eventsValid()); o.i ("eventsInvalidReason", (std::int64_t) b.eventsInvalidReason());
    for (int c = 0; c < b.channels(); ++c)
    {
        o.d ("bandEnergy", b.bandEnergy (c)); o.i ("nonFiniteSamples", b.nonFiniteSamples (c)); o.i ("absentSamples", b.absentSamples (c));
    }
    o.i ("overflowSamples", b.overflowSamples()); o.i ("firstNonFiniteAt", b.firstNonFiniteAt());
    o.b ("programmeEnergyValid", b.programmeEnergyValid());
    o.i ("programmeEnergyInvalidReason", (std::int64_t) b.programmeEnergyInvalidReason());
    o.i ("onsetCount", b.onsetCount()); o.d ("onsetsPerSecond", b.onsetsPerSecond()); o.i ("intervalCount", b.intervalCount());
    o.i ("intervalOverflow", b.intervalOverflow());
    for (int h = 1; h <= analysis::BandBursts::kIoiBins; ++h) o.i ("intervalBin", b.intervalBin (h));
    for (int h = 1; h <= analysis::BandBursts::kMaxLag; ++h) o.i ("lagBin", b.lagBin (h));
    o.i ("modalIntervalHops", b.modalIntervalHops()); o.i ("modalIntervalMass", b.modalIntervalMass());
}

struct BandBurstsA
{
    static constexpr const char* kName = "BandBursts";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::BandBursts bb;
    Bits trace;

    static void onHop (void* user, const analysis::BandBurstsHopTrace& t) noexcept
    {
        Bits& o = *static_cast<Bits*> (user);
        o.i ("hop.index", t.hopIndex); o.i ("hop.start", t.startSample); o.i ("hop.end", t.endSample);
        o.d ("hop.energy", t.energy); o.d ("hop.wideEnergy", t.wideEnergy); o.d ("hop.baseline", t.baseline);
        o.b ("hop.full", t.full); o.b ("hop.eligible", t.eligible); o.b ("hop.damaged", t.damaged); o.b ("hop.above", t.above);
        o.b ("hop.inEvent", t.inEvent); o.i ("hop.eventStart", t.eventStart); o.i ("hop.peakAt", t.peakAt);
    }
    bool prepare (const Programme& p, int maxBlock)
    {
        bb.setObserver (&onHop, &trace);
        return bb.prepare (p.fs, maxBlock, p.channels());
    }
    bool process (const float* const* in, int nch, int n) { return bb.process (in, nch, n); }
    void finish() { bb.finish(); }
    void serialise (Bits& o) const { o = trace; putBandBursts (o, bb); }

    bool witness (std::string& f) const
    {
        char b[256];
        std::snprintf (b, sizeof b, "%lld events, %lld onsets, %lld eligible of %lld hops, modal interval %d hops",
                       (long long) bb.eventCount(), (long long) bb.onsetCount(), (long long) bb.eligibleHops(),
                       (long long) bb.hopCount(), bb.modalIntervalHops());
        f = b;
        return bb.eventCount() >= 5 && bb.eligibleHops() > 100 && bb.intervalCount() > 3;
    }
};

// A bed, 7 kHz sibilance bursts of 60 ms every 250 ms through the middle, a stretch of irregular ticks, and in the
// right channel alone a second train — so Mid and Side see different bursts in the stereo sibling.
Programme burstsFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 2, 7 * 48000 + 555, 0x5eed0005u);
    for (int c = 0; c < 2; ++c) g.noise (c, 0, g.len(), 0.03);
    for (int c = 0; c < 2; ++c) g.bursts (c, 110000, 250000, 12000, 2880, 96, 7000.0, 0.2);
    for (std::int64_t k = 260000; k < 320000; k += 3000 + (k % 7) * 911) { g.hit (0, k, 0.3, 6500.0, 8.0); g.hit (1, k, 0.3, 6500.0, 8.0); }
    g.bursts (1, 150000, 300000, 17000, 2400, 96, 6200.0, 0.15);
    return g.p;
}

struct StereoBandBurstsA
{
    static constexpr const char* kName = "StereoBandBursts";
    static constexpr bool kClockOnly = false, kNarrow = false, kMaxBlock = false;   // refuses nch < 1, and a width change
    analysis::StereoBandBursts sb;

    bool prepare (const Programme& p, int) { return sb.prepare (p.fs, p.channels()); }
    bool process (const float* const* in, int nch, int n) { return sb.process (in, nch, n); }
    void finish() { (void) sb.finish(); }
    void serialise (Bits& o) const
    {
        o.b ("finished", sb.isFinished()); o.i ("channels", sb.channels()); o.i ("samplesProcessed", sb.samplesProcessed());
        o.b ("sideAbsent", sb.sideAbsent()); o.d ("domeShare", sb.domeShare()); o.d ("domeHz", sb.domeHz());
        o.d ("sampleRate", sb.sampleRate());
        for (int a = 0; a < analysis::StereoBandBursts::kAxes; ++a)
        {
            putBandBursts (o, sb.axis (a));
            o.i ("exactZeroHops", sb.exactZeroHops (a));
            for (std::int64_t k = 0; k < sb.axis (a).storedEventCount(); ++k)
            {
                const auto x = sb.crossAt (a, k);
                o.d ("cross.power", x.power); o.d ("cross.baseline", x.baseline); o.b ("cross.eligible", x.eligible); o.i ("cross.hop", x.hop);
            }
        }
    }

    bool witness (std::string& f) const
    {
        char b[160];
        std::snprintf (b, sizeof b, "Mid %lld events, Side %lld events, dome %.4f at %.0f Hz", (long long) sb.mid().eventCount(),
                       (long long) sb.side().eventCount(), sb.domeShare(), sb.domeHz());
        f = b;
        return sb.mid().eventCount() > 0 && sb.side().eventCount() > 0;
    }
};

//------------------------------------------------------------------------------ BandCrest (a source and its master)
// The class's output is two runs AND the paired loss between them, so the adapter carries both: planes 0-1 are the
// source, planes 2-3 the master, each cut the same way (a pump runs the two side by side).
void putBandCrest (Bits& o, const analysis::BandCrest& b)
{
    using BC = analysis::BandCrest;
    o.d ("sampleRate", b.sampleRate()); o.i ("hopSamples", b.hopSamples()); o.i ("blockHops", b.blockHops());
    o.i ("channels", b.channels()); o.i ("hopCount", b.hopCount()); o.i ("basePeakHops", b.basePeakHops());
    o.i ("droppedHops", b.droppedHops()); o.i ("samplesProcessed", b.samplesProcessed());
    o.i ("nonFiniteSamples", b.nonFiniteSamples()); o.i ("overflowedSamples", b.overflowedSamples());
    o.i ("narrowedSamples", b.narrowedSamples()); o.i ("widestChannels", b.widestChannels());
    o.i ("firstNonFiniteAt", b.firstNonFiniteAt()); o.i ("blockCount", b.blockCount());
    o.i ("invalidReason", (std::int64_t) b.invalidReason()); o.b ("valid", b.valid());
    o.d ("programmeMeanSquareDb", b.programmeMeanSquareDb()); o.d ("fullBandPeakLin", b.fullBandPeakLin());
    for (int band = 0; band < BC::kBands; ++band)
    {
        o.i ("activeBlocks", b.activeBlocks (band));
        for (long long j = 0; j < b.blockCount(); ++j)
        {
            o.d ("blockPeakLin", b.blockPeakLin (j, band)); o.d ("blockMeanSq", b.blockMeanSq (j, band));
            o.d ("blockCrestDb", b.blockCrestDb (j, band)); o.b ("blockActive", b.blockActive (j, band));
        }
    }
}

struct BandCrestA
{
    static constexpr const char* kName = "BandCrest (+ paired loss)";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = false;
    analysis::BandCrest in, out;

    bool prepare (const Programme& p, int)
    {
        return in.prepare (p.fs, 2, (long long) p.declared) && out.prepare (p.fs, 2, (long long) p.declared);
    }
    bool process (const float* const* x, int nch, int n)
    {
        const int w = nch / 2;                  // 4 planes are 2 + 2; a narrow stretch of 2 is one channel each
        return in.process (x, w, n) && out.process (x + 2, w, n);
    }
    void finish() { in.finish(); out.finish(); }
    void serialise (Bits& o) const
    {
        putBandCrest (o, in);
        putBandCrest (o, out);
        std::vector<double> scratch;
        for (int band = 0; band < analysis::BandCrest::kBands; ++band)
        {
            const auto l = analysis::bandCrestLoss (in, out, band, scratch);
            o.i ("loss.blocks", l.blocks); o.i ("loss.inActive", l.inActive); o.i ("loss.usable", l.usable);
            o.i ("loss.outSilent", l.outSilent); o.i ("loss.lagBlocks", l.lagBlocks); o.d ("loss.p50Db", l.p50Db);
            o.d ("loss.p95Db", l.p95Db); o.d ("loss.cvar95Db", l.cvar95Db); o.d ("loss.meanDb", l.meanDb); o.d ("loss.maxDb", l.maxDb);
            o.d ("loss.p5Db", l.p5Db); o.i ("loss.over1Db", l.over1Db); o.i ("loss.over3Db", l.over3Db); o.i ("loss.over6Db", l.over6Db);
            o.d ("loss.peakShiftDb", l.peakShiftDb); o.d ("loss.levelShiftDb", l.levelShiftDb); o.b ("loss.valid", l.valid);
        }
    }

    bool witness (std::string& f) const
    {
        std::vector<double> scratch;
        const auto l = analysis::bandCrestLoss (in, out, analysis::BandCrest::kFull, scratch);
        char b[200];
        std::snprintf (b, sizeof b, "%lld blocks, full-band loss p50 %.2f / p95 %.2f dB over %lld usable blocks",
                       in.blockCount(), l.p50Db, l.p95Db, l.usable);
        f = b;
        return in.valid() && out.valid() && in.blockCount() > 20 && l.valid && l.p50Db > 0.5;
    }
};

// Drum-like hits and a tone in every band as the source; the master is the same programme hard-clipped at 0.35 and
// made up by 3 dB — a crest loss that is not uniform across the bands.
Programme bandCrestFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 4, 4 * 48000 + 999, 0x5eed0006u);
    for (int c = 0; c < 2; ++c)
    {
        g.noise (c, 0, g.len(), 0.02);
        g.tone (c, 0, g.len(), 60.0, 0.12);
        g.tone (c, 0, g.len(), 700.0, 0.06, 0.5 * c);
        g.tone (c, 0, g.len(), 3500.0, 0.03);
        g.tone (c, 0, g.len(), 9000.0, 0.015);
        for (std::int64_t k = 2000 + 377 * c; k < g.len(); k += 11000) g.hit (c, k, 0.7, 80.0 + 900.0 * (double) ((k / 11000) % 3), 25.0);
        for (std::int64_t k = 5000; k < g.len(); k += 6000) g.hit (c, k, 0.4, 7000.0, 3.0);
    }
    for (int c = 0; c < 2; ++c)
    {
        g.p.x[(std::size_t) (c + 2)] = g.p.x[(std::size_t) c];
        g.clamp (c + 2, 0, g.len(), -0.35f, 0.35f);
        g.scale (c + 2, 0, g.len(), 1.4125375446227544);
    }
    return g.p;
}

//------------------------------------------------------------------------------ PeakExcursions
struct PeakExcursionsA
{
    static constexpr const char* kName = "PeakExcursions";
    static constexpr bool kClockOnly = false, kNarrow = false, kMaxBlock = false;   // refuses nch < 1, and a width change
    analysis::PeakExcursions pe;

    bool prepare (const Programme& p, int)
    {
        analysis::PeakExcursions::Params q;
        q.thresholdDbtp = -1.0;
        q.mergeMs = 0.5;
        pe.setParams (q);
        return pe.prepare (p.fs, p.channels());
    }
    bool process (const float* const* in, int nch, int n) { return pe.process (in, nch, n); }
    void finish() { (void) pe.finish(); }
    void serialise (Bits& o) const
    {
        using PE = analysis::PeakExcursions;
        o.b ("finished", pe.isFinished());
        o.i ("reason", (std::int64_t) pe.reason()); o.b ("valid", pe.valid()); o.i ("channels", pe.channels());
        o.d ("thresholdLinear", pe.thresholdLinear()); o.i ("samplesProcessed", pe.samplesProcessed());
        o.i ("measuredOs", pe.measuredOs()); o.d ("reconstructedPeak", pe.reconstructedPeak());
        o.d ("samplePeakLinear", pe.samplePeakLinear()); o.d ("truePeakLinear", pe.truePeakLinear());
        o.i ("runCount", pe.runCount()); o.i ("storedRunCount", pe.storedRunCount()); o.b ("runsComplete", pe.runsComplete());
        for (std::int64_t k = 0; k < pe.storedRunCount(); ++k)
        {
            const auto r = pe.run (k);
            o.i ("run.startOs", r.startOs); o.i ("run.lengthOs", r.lengthOs); o.i ("run.aboveOs", r.aboveOs);
            o.d ("run.peak", r.peak); o.d ("run.dose", r.dose); o.b ("run.closedByFinish", r.closedByFinish);
            o.d ("run.crestHz", r.crestHz (pe.sampleRate(), pe.thresholdLinear()));
        }
        o.i ("aboveOs", pe.aboveOs());
        for (int c = 0; c < pe.channels(); ++c) o.i ("aboveOs.channel", pe.aboveOs (c));
        o.d ("occupancy", pe.occupancy()); o.d ("runsPerMinute", pe.runsPerMinute()); o.d ("totalDose", pe.totalDose());
        o.d ("maxExcess", pe.maxExcess());
        for (int k = 0; k < PE::kClasses; ++k) { o.i ("classCount", pe.classCount (k)); o.d ("classDose", pe.classDose (k)); }
        o.d ("p90Ms", pe.p90Ms()); o.b ("p90Saturated", pe.p90Saturated());
        for (int b = 0; b < PE::kDurationBins; ++b) o.i ("durationBin", pe.durationBin (b));
        for (int b = 0; b < PE::kCrestBins; ++b) { o.i ("crestBinCount", pe.crestBinCount (b)); o.d ("crestBinDose", pe.crestBinDose (b)); }
        for (double hz : { 100.0, 1000.0, 5000.0 }) o.d ("doseShareBelow", pe.doseShareBelow (hz));
        o.i ("ceilingMaxima", pe.ceilingMaxima()); o.d ("ceilingDensity", pe.ceilingDensity());
        o.d ("ceilingDensityAbove", pe.ceilingDensityAbove (3.0));
        o.d ("ceilingDensityAbove.wide", pe.ceilingDensityAbove (12.0, 0.5));
        for (int b = 0; b < PE::kCeilingBins; ++b) o.i ("ceilingBin", pe.ceilingBin (b));
        o.i ("nonFiniteSamples", (std::int64_t) pe.nonFiniteSamples()); o.i ("firstNonFiniteAt", pe.firstNonFiniteAt());
    }

    bool witness (std::string& f) const
    {
        char b[200];
        std::snprintf (b, sizeof b, "%lld runs, true peak %.3f (sample %.3f), dose %.3f, p90 %.3f ms", (long long) pe.runCount(),
                       pe.truePeakLinear(), pe.samplePeakLinear(), pe.totalDose(), pe.p90Ms());
        f = b;
        return pe.valid() && pe.runCount() > 5 && pe.reconstructedPeak() > 1.05 * pe.samplePeakLinear();
    }
};

// A render made WITHOUT the limiter. Bass under the ceiling on its own, kicks that take it over when they land on a
// crest, and bursts of a tone at exactly fs/4 and phase pi/4 — every sample of it sits at 0.707 of its crest, so
// its excursions exist only BETWEEN the samples, which is the case this instrument exists for.
Programme excursionsFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 2, 3 * 48000 + 321, 0x5eed0007u);
    for (int c = 0; c < 2; ++c)
    {
        g.noise (c, 0, g.len(), 0.02);
        g.bursts (c, 0, g.len(), 30000, 14000, 600, 60.0, 0.6, 0.2 * c);
        g.bursts (c, 7000, g.len(), 21000, 9000, 300, 12000.0, 0.97, 0.7853981633974483);
        for (std::int64_t k = 4000 + 131 * c; k < g.len(); k += 9600) g.hit (c, k, 0.45, 120.0, 12.0);
    }
    return g.p;
}

//------------------------------------------------------------------------------ ClipDetector
struct ClipDetectorA
{
    static constexpr const char* kName = "ClipDetector";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::ClipDetector cd;

    bool prepare (const Programme& p, int maxBlock) { return cd.prepare (p.fs, maxBlock, p.channels()); }
    bool process (const float* const* in, int nch, int n) { return cd.process (in, nch, n); }
    void finish() { cd.finish(); }
    void serialise (Bits& o) const
    {
        o.b ("finished", cd.isFinished()); o.b ("clipped", cd.clipped()); o.i ("runCount", cd.runCount());
        o.i ("storedRunCount", cd.storedRunCount()); o.b ("runsComplete", cd.runsComplete());
        o.i ("samplesProcessed", cd.samplesProcessed()); o.i ("channels", cd.channels());
        o.i ("decisionDelaySamples", cd.decisionDelaySamples());
        for (std::int64_t k = 0; k < cd.storedRunCount(); ++k)
        {
            const auto r = cd.run (k);
            o.i ("run.start", r.start); o.i ("run.length", r.length); o.d ("run.level", r.level); o.i ("run.channel", r.channel);
            o.i ("run.sign", r.sign); o.i ("run.evidence", (std::int64_t) r.evidence);
        }
        o.d ("samplePeak", cd.samplePeak()); o.d ("samplePeakDb", cd.samplePeakDb());
        for (int c = 0; c < cd.channels(); ++c)
        {
            o.d ("samplePeak.channel", cd.samplePeak (c)); o.d ("samplePeakDb.channel", cd.samplePeakDb (c));
            o.d ("dcOffset", cd.dcOffset (c)); o.i ("finiteSamples", cd.finiteSamples (c)); o.i ("nonFiniteSamples", cd.nonFiniteSamples (c));
            o.i ("runCount.channel", cd.runCount (c)); o.i ("clippedSamples", cd.clippedSamples (c)); o.i ("longestRun", cd.longestRun (c));
        }
    }

    bool witness (std::string& f) const
    {
        char b[200];
        std::snprintf (b, sizeof b, "%lld runs (L %lld / R %lld), clipped %lld / %lld samples, DC %.4f", (long long) cd.runCount(),
                       (long long) cd.runCount (0), (long long) cd.runCount (1), (long long) cd.clippedSamples (0),
                       (long long) cd.clippedSamples (1), cd.dcOffset (0));
        f = b;
        return cd.runCount (0) > 5 && cd.runCount (1) > 5;
    }
};

// A programme driven into a hard ceiling in stretches (flat tops at +-0.98 and a lower ceiling at 0.7 on the right),
// a DC offset, and clean music between — flat runs of many lengths, some a few samples long.
Programme clipFixture()
{
    const double fs = 48000.0;
    Gen g (fs, 2, 3 * 48000 + 1777, 0x5eed0008u);
    for (int c = 0; c < 2; ++c)
    {
        g.noise (c, 0, g.len(), 0.08);
        g.tone (c, 0, g.len(), 110.0, 0.55, 0.3 * c);
        g.tone (c, 0, g.len(), 2350.0, 0.3);
        for (std::int64_t k = 1000; k < g.len(); k += 9600) g.hit (c, k, 0.9, 90.0, 30.0);
    }
    g.scale (0, 30000, 70000, 2.2);  g.clamp (0, 30000, 70000, -0.98f, 0.98f);
    g.scale (1, 60000, 110000, 1.9); g.clamp (1, 60000, 110000, -0.7f, 0.7f);
    for (std::int64_t i = 0; i < g.len(); ++i) g.at (0, i) += 0.01f;
    return g.p;
}

//------------------------------------------------------------------------------ WaveformPeaks + StereoColumns
struct WaveformPeaksA
{
    static constexpr const char* kName = "WaveformPeaks";
    static constexpr bool kClockOnly = false, kNarrow = false, kMaxBlock = false;   // the width is EXACT
    analysis::WaveformPeaks avr, mx, left;

    bool prepare (const Programme& p, int)
    {
        const auto n = (std::uint64_t) p.declared;
        return avr.prepare (p.fs, p.channels(), n, 1000, analysis::PeakMix::Average)
            && mx.prepare (p.fs, p.channels(), n, 777, analysis::PeakMix::Max)
            && left.prepare (p.fs, p.channels(), n, 1200, analysis::PeakMix::Left);
    }
    bool process (const float* const* in, int nch, int n)
    {
        return avr.process (in, nch, n) && mx.process (in, nch, n) && left.process (in, nch, n);
    }
    void finish() {}
    static void put (Bits& o, const analysis::WaveformPeaks& w)
    {
        o.b ("complete", w.complete()); o.i ("framesSeen", (std::int64_t) w.framesSeen()); o.i ("bucketsEmitted", w.bucketsEmitted());
        o.i ("decimation", w.decimation());
        for (double v : w.peaks()) o.d ("peaks", v);
        for (int k = 0; k < w.buckets(); ++k) o.f ("peakAsFloat32", w.peakAsFloat32 (k));
    }
    void serialise (Bits& o) const { put (o, avr); put (o, mx); put (o, left); }

    bool witness (std::string& f) const
    {
        f = std::to_string (avr.bucketsEmitted()) + " / " + std::to_string (mx.bucketsEmitted()) + " / "
          + std::to_string (left.bucketsEmitted()) + " buckets emitted, decimation " + std::to_string (avr.decimation());
        return avr.complete() && mx.complete() && left.complete() && avr.bucketsEmitted() > 900;
    }
};

struct StereoColumnsA
{
    static constexpr const char* kName = "StereoColumns";
    static constexpr bool kClockOnly = false, kNarrow = false, kMaxBlock = false;   // the width is EXACT
    analysis::StereoColumns sc, narrow;

    bool prepare (const Programme& p, int)
    {
        return sc.prepare (p.channels(), (std::uint64_t) p.declared)
            && narrow.prepare (p.channels(), (std::uint64_t) p.declared, 97);
    }
    bool process (const float* const* in, int nch, int n) { return sc.process (in, nch, n) && narrow.process (in, nch, n); }
    void finish() {}
    static void put (Bits& o, const analysis::StereoColumns& s)
    {
        o.b ("complete", s.complete()); o.i ("framesSeen", (std::int64_t) s.framesSeen()); o.i ("columns", s.columns());
        o.b ("isMono", s.isMono()); o.d ("maxRms", s.maxRms());
        for (float v : s.width()) o.f ("width", v);
        for (float v : s.correlation()) o.f ("correlation", v);
        for (float v : s.rms()) o.f ("rms", v);
        for (int k = 0; k < s.columns(); ++k) { o.i ("columnStart", (std::int64_t) s.columnStart (k)); o.i ("columnEnd", (std::int64_t) s.columnEnd (k)); }
    }
    void serialise (Bits& o) const { put (o, sc); put (o, narrow); }

    bool witness (std::string& f) const
    {
        const auto w = sc.width();
        const float lo = *std::min_element (w.begin(), w.end()), hi = *std::max_element (w.begin(), w.end());
        f = std::to_string (sc.columns()) + " columns, width " + std::to_string (lo) + " .. " + std::to_string (hi);
        return sc.complete() && narrow.complete() && lo < 0.05f && hi > 0.9f;
    }
};

// 44.1 kHz (decimation 6), a width that moves: mono, wide, anti-phase, one-sided, and a quiet stretch.
Programme shapesFixture()
{
    const double fs = 44100.0;
    Gen g (fs, 2, 5 * 44100 + 1601, 0x5eed0009u);
    const std::int64_t n = g.len();
    for (int c = 0; c < 2; ++c) g.tone (c, 0, n / 4, 220.0, 0.5);                    // mono
    g.noise (0, n / 4, n / 2, 0.4); g.noise (1, n / 4, n / 2, 0.4);                 // wide
    g.tone (0, n / 2, 3 * n / 4, 330.0, 0.6); g.tone (1, n / 2, 3 * n / 4, 330.0, -0.6);   // anti-phase
    g.tone (0, 3 * n / 4, n - 20000, 150.0, 0.7);                                  // left alone
    for (std::int64_t k = 100; k < n; k += 5000) g.hit (1, k, 0.8, 3000.0, 2.0);
    g.noise (0, n - 20000, n, 1e-4); g.noise (1, n - 20000, n, 1e-4);
    return g.p;
}

//------------------------------------------------------------------------------ SpectrumFrames (through its consumers' loop)
struct SpectrumFramesA
{
    static constexpr const char* kName = "SpectrumFrames";
    static constexpr bool kClockOnly = true, kNarrow = true, kMaxBlock = true;
    analysis::SpectrumFrames sf;
    Bits frames;

    bool prepare (const Programme& p, int maxBlock)
    {
        analysis::SpectrumFramesParams q;
        q.fftOrder = 10;
        q.hop = 300;                    // N % hop != 0: the end-anchored trigger is the one that must hold
        sf.setParams (q);
        return sf.prepare (p.fs, maxBlock, p.channels());
    }
    bool process (const float* const* in, int nch, int n)
    {
        if (nch < 0 || nch > 2 || n < 0) return false;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c) sf.push (c, c < nch ? in[c][i] : 0.0f, c < nch);
            if (sf.tick())
            {
                frames.i ("frame.index", sf.frameIndex()); frames.i ("frame.start", sf.frameStart()); frames.i ("frame.end", sf.frameEnd());
                for (int c = 0; c < 2; ++c)
                {
                    frames.b ("frame.finite", sf.frameFinite (c));
                    const double* pw = sf.power (c);
                    for (int k = 0; k < sf.bins(); ++k) frames.d ("frame.power", pw[k]);
                }
            }
        }
        return true;
    }
    void finish() { sf.finish(); }
    void serialise (Bits& o) const
    {
        o = frames;
        o.b ("finished", sf.isFinished());
        o.i ("totalSamples", sf.totalSamples()); o.i ("tailUncovered", sf.tailUncoveredSamples()); o.i ("frameCount", sf.frameCount());
        o.d ("windowPowerGain", sf.windowPowerGain()); o.d ("windowCoherentGain", sf.windowCoherentGain());
    }

    bool witness (std::string& f) const
    {
        f = std::to_string (sf.frameCount()) + " frames, " + std::to_string (sf.tailUncoveredSamples()) + " tail samples uncovered";
        return sf.frameCount() > 100 && sf.tailUncoveredSamples() > 0;
    }
};

Programme framesFixture()
{
    Gen g (48000.0, 2, 60000 + 17, 0x5eed000au);
    for (int c = 0; c < 2; ++c) { g.noise (c, 0, g.len(), 0.2); g.tone (c, 0, g.len(), 1234.5, 0.5, 0.3 * c); }
    return g.p;
}

//==============================================================================
// THE GAP WITHOUT PLANES. Law 11a: at `nch == 0` the plane array "may be null" — a clock-only call carries no audio
// and a caller has no plane to point at. Every class above that takes a zero-width call is asked for 480 samples of
// it with `in == nullptr`. BandCrest used to refuse it — its null check sat after the `n == 0` exit and before any
// width test, so a caller spending a gap on it had to pass a dummy array (which the rows above still do, and which
// stays legal). It refuses a null array only at `numChannels > 0` now, LowEnd's spelling. For the classes that
// check a null array at all (`checksNull`) the negative half is asserted too: where a plane WILL be read, a null array
// is still a refusal — so the fix narrowed nothing but the gap. (The others take a non-null array at a live width as
// the caller's precondition; that is their contract and not this test's to change.)
template <class T, class Prep>
void nullGap (const char* name, Prep prep, bool checksNull)
{
    T t;
    const bool prepared = prep (t);
    felitronics::test::ok (prepared && t.process (nullptr, 0, 480),
                           std::string (name) + ": a clock-only call with a null plane array is accepted");
    if (checksNull)
        felitronics::test::ok (prepared && ! t.process (nullptr, 1, 480),
                               std::string (name) + ": a null plane array at width 1 is still refused");
}

// THE EMPTY CALL IS A NO-OP. Law 11d: `n == 0` is the one true no-op — no time, no edge, nothing. StereoBandBursts
// and PeakExcursions used to latch their width on the first call BEFORE they looked at `n`, so an empty call at width
// 1 before the audio made the stereo programme after it a refused width change. The latch sits below the `n == 0`
// exit now, and below the plane check, so neither an empty call nor a refused one latches anything. What the latch is
// FOR still holds and is asserted beside it: once audio has fixed the width, a different one is refused — an empty
// call included, because the width check comes before the `n == 0` exit (law 11's order).
template <class T, class Prep>
void emptyLatch (const char* name, Prep prep)
{
    const float z[4] {};
    const float* planes[2] { z, z };
    {
        T t;
        const bool prepared = prep (t);
        const bool empty = prepared && t.process (planes, 1, 0);
        felitronics::test::ok (empty, std::string (name) + ": prepared, and an empty call at width 1 is accepted");
        felitronics::test::ok (empty && t.process (planes, 2, 4),
                               std::string (name) + ": after process (planes, 1, 0), a stereo call is accepted");
        felitronics::test::ok (! t.process (planes, 1, 4), std::string (name) + ": then a mono call is a refused width change");
        felitronics::test::ok (! t.process (planes, 1, 0), std::string (name) + ": ...an empty one included");
    }
    {
        T t;
        const bool prepared = prep (t);
        const float* holed[2] { z, nullptr };
        felitronics::test::ok (prepared && ! t.process (holed, 2, 4), std::string (name) + ": a null plane is refused");
        felitronics::test::ok (t.process (planes, 1, 4), std::string (name) + ": ...and latched nothing: mono audio after it is accepted");
    }
}

void nullGapRows()
{
    felitronics::test::group ("the gap without planes — process (nullptr, 0, n), law 11a");
    nullGap<analysis::ProgrammeReport> ("ProgrammeReport", [] (auto& x) { return x.prepare (48000.0, 512, 2); }, true);
    nullGap<analysis::SourceForensics> ("SourceForensics", [] (auto& x) { return x.prepare (48000.0, 512, 2); }, false);
    nullGap<analysis::HumDetector>     ("HumDetector",     [] (auto& x) { return x.prepare (48000.0, 512, 2); }, false);
    nullGap<analysis::LowEnd>          ("LowEnd",          [] (auto& x) { return x.prepare (48000.0, 512, 2); }, true);
    nullGap<analysis::BandBursts>      ("BandBursts",      [] (auto& x) { return x.prepare (48000.0, 512, 2); }, false);
    nullGap<analysis::ClipDetector>    ("ClipDetector",    [] (auto& x) { return x.prepare (48000.0, 512, 2); }, false);
    nullGap<analysis::BandCrest>       ("BandCrest",       [] (auto& x) { return x.prepare (48000.0, 2, 480000); }, true);

    felitronics::test::group ("the empty call — process (planes, 1, 0) before stereo audio, law 11d");
    emptyLatch<analysis::StereoBandBursts> ("StereoBandBursts", [] (auto& x) { return x.prepare (48000.0, 2); });
    emptyLatch<analysis::PeakExcursions>   ("PeakExcursions",   [] (auto& x) { return x.prepare (48000.0, 2); });
}

} // namespace

int main()
{
    suite<ProgrammeReportA>  (programmeReportFixture());
    suite<SourceForensicsA>  (sourceForensicsFixture());
    suite<HumDetectorA>      (humFixture());
    suite<LowEndA>           (lowEndFixture());
    suite<BandBurstsA>       (burstsFixture());
    suite<StereoBandBurstsA> (burstsFixture());
    suite<BandCrestA>        (bandCrestFixture());
    suite<PeakExcursionsA>   (excursionsFixture());
    suite<ClipDetectorA>     (clipFixture());
    suite<WaveformPeaksA>    (shapesFixture());
    suite<StereoColumnsA>    (shapesFixture());
    suite<SpectrumFramesA>   (framesFixture());
    nullGapRows();
    printTable();
    return felitronics::test::report();
}
