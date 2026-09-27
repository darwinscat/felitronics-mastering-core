// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/core/Config.h>
#include <felitronics/core/DetMath.h>
#include <felitronics/core/Math.h>
#include <felitronics/core/OfflineFft.h>
#include <felitronics/tempo/JsNumerics.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <felitronics/storage/Buffer.h>
#include <felitronics/storage/VectorBytes.h>

// A FUNCTION BOUNDARY THE OPTIMISER MUST KEEP — see "WHERE THE TIME IS SPENT" in the class comment for why this
// file has any. Spelled per compiler because the attribute is not standard C++; undefined again at the end of the
// file, because it is this header's own spelling and not part of its API.
#if defined(_MSC_VER)
    #define FELITRONICS_TEMPO_NOINLINE __declspec (noinline)
#else
    #define FELITRONICS_TEMPO_NOINLINE __attribute__ ((noinline))
#endif

namespace felitronics::tempo
{

// The search range and the tempo-over-time geometry — the spec's `opts`, field for field, with its defaults.
struct TempoParams
{
    double minBpm = 60.0;    // MIN_BPM: the slowest tempo the autocorrelation looks for, and the fold floor
    double maxBpm = 180.0;   // MAX_BPM: the fastest, and the fold ceiling
    double winSec = 6.0;     // the length of one tempo-over-time window
    double hopSec = 1.5;     // how far the window moves between two points of the curve
};

// The spec's `confidenceLabel`, with its thresholds: >= 0.5 high, >= 0.28 medium, > 0 low, else undetermined
// (a NaN confidence is undetermined too — it fails all three).
enum class ConfidenceLabel : std::uint8_t
{
    Undetermined = 0,
    Low          = 1,
    Medium       = 2,
    High         = 3
};

// The spec's single-tempo result (`assembleResult`). Where the JavaScript says `null` this says NaN.
struct TempoHeadline
{
    bool   determined = false;                          // false is the spec's UNDETERMINED
    double bpm = std::numeric_limits<double>::quiet_NaN();   // rounded to 0.1
    double confidence = 0.0;                            // rounded to 0.01 — may be NaN, see the header note
    ConfidenceLabel label = ConfidenceLabel::Undetermined;
    int    altCount = 0;                                // 0..2
    double alts[2] { 0.0, 0.0 };                        // round(bpm/2), round(bpm*2), each kept only inside the range
    double beatPeriodSec = std::numeric_limits<double>::quiet_NaN();
    double beatOffsetSec = std::numeric_limits<double>::quiet_NaN();
};

// One of the spec's `candidates`: an autocorrelation peak of the whole track, bpm rounded to 0.1, score raw.
struct TempoCandidate
{
    double bpm = 0.0;
    double score = 0.0;
};

// One point of the tempo curve: the window's centre in seconds (rounded to 0.1), its median-smoothed bpm
// (rounded to 0.1; absent — the spec's `null`, NaN here — where the window was not confident enough), and its
// confidence (rounded to 0.01, 0 where the window had nothing to lock on).
struct TempoPoint
{
    double t = 0.0;
    double bpm = std::numeric_limits<double>::quiet_NaN();
    double conf = 0.0;
    bool   hasBpm = false;
};

//==============================================================================
// felitronics::tempo::TempoDetector — the whole-track tempo, its confidence, its octave alternatives and the
// tempo over time, of a decoded programme.
//
// THIS IS A PORT, AND THE SPEC IS EXECUTABLE: the site's dsp/tempo.js (`tempoCurve`, which the page's analysis
// worker runs, and `detectTempo`, its whole-track half), over the mono mix the page's `toMono` makes. The
// algorithm is the JavaScript's to the operation, in its order, in binary64; anything below that reads like an
// improvement is a change to the answer, not to this file. Where this differs from the page it is said below,
// with the size of the difference.
//
// THE DEFINITION, step by step (the spec's names in brackets):
//   mix        mono = (sum of the channels) / channels, each step rounded to float32     [toMono]
//   onsets     1024-sample Hann frames (SYMMETRIC: cos(2 pi i / 1023)) every 512 samples; the magnitude spectrum
//              of each; the spectral flux sum(max(0, |X_t| - |X_t-1|)) over the 513 bins    [onsetEnvelope]
//              — one onset value per hop, at odfSr = sampleRate / 512; frame 0 has no predecessor and reads 0
//   detrend    subtract the mean over +/- round(0.15 odfSr) frames, keep the positive part      [detrend]
//   autocorr   ac[lag] = sum(odf[i] * odf[i-lag]) / (n - lag) for the lags of [minBpm, maxBpm]     [autocorr]
//   peaks      every local maximum strictly inside the lag range, bpm = 60 odfSr / lag
//   whole track the peak with the best ac * exp(-z^2/2), z = log2(bpm/120)/0.85 (the perceptual prior);
//              parabolic refinement of its lag; confidence = 0.6 margin + 0.4 strength, clamped to [0, 1]
//                                                                            [analyzeWindow, anchor = null]
//   over time  6 s windows every 1.5 s over the same onset curve, each peak weighted by closeness to the
//              whole-track tempo (sigma 0.35 octave), the winner octave-folded toward it; a window under 0.15
//              confidence is a gap; a median of 5 over the curve                     [tempoCurve, medianSmooth]
//   headline   the median of the smoothed points with confidence >= 0.2 when there are three or more, its
//              10th-90th percentile as the range, `varies` when that range is over 6 % AND at least 5 BPM
//
// TWO VIEWS OF ONE ANALYSIS. `headline()` is what tempoCurve() returns: the bpm is the curve's median, while the
// alternatives and the beat period come from that median folded toward the whole-track tempo (the spec's own
// arrangement — the two can differ by an octave, and are kept as it keeps them). `wholeTrack()` is what
// detectTempo() returns for the same input. The confidence, the label, the candidates and the beat offset are
// the whole track's in both.
//
// WHERE IT DIFFERS FROM THE PAGE, AND BY HOW MUCH. The operation order is the spec's, and every multiply-add
// this header writes that the page rounds twice is rounded twice here (core::det::mul / mulAdd pin them: fused,
// each would be a different number). The transform's butterflies are core::offline::fftInplace's own; no build
// measured fuses them (none on Apple clang at the tree's -ffp-contract=on; the wasm module is contract-off). So
// the difference is in the elementary functions alone:
//   * the Hann window's cos is core::det::cos, not V8's Math.cos — the same function on every row this
//     repository builds on, which V8's is not required to be. Measured against node 26: one ulp apart on 174 of
//     the 1024 coefficients. The FFT's ten twiddle seeds (core::det::cos/sin) agree with V8's bit for bit;
//   * log2 and exp in the octave weights are core::det::log2 and js::exp (JsNumerics.h), within a few ulps.
//   Math.hypot, Math.round, Math.min and Math.max are V8's exactly (JsNumerics.h). The transform is
//   core::offline::fftInplace — the same radix-2 decimation in time as the page's fft.js, unnormalised, with the
//   same bit reversal and the same twiddle recurrence.
//   What the page reports is rounded (bpm and t to 0.1, confidence to 0.01), so an ulp reaches it only where a
//   value lands on a rounding tie or two autocorrelation peaks tie; the cross-language comparison on real
//   programmes is out of tree, and its numbers are in the changelog entry that introduced this class. The
//   native-vs-wasm comparison is in tree and gated: `fcore_measure tempo` against tools/wasm/tempo-parity.mjs,
//   byte for byte, in CI's wasm job.
//
// THE SPEC'S OWN EDGES, KEPT, not fixed:
//   * A LONE ONSET HAS A TEMPO AND NO CONFIDENCE. When every autocorrelation lag in range is exactly zero (one
//     click in silence), every lag is a "peak", the best score is 0, the margin is 0/0 and the confidence is
//     NaN — so the label is undetermined while the bpm is a number (the fastest lag). The JavaScript answers
//     exactly that (its JSON carries the NaN as null); `confidence` is NaN here.
//   * The headline bpm is the curve's median while the alternatives are its folded twin's (above).
//   * A programme shorter than 1.5 s of onset frames, or one whose detrended onsets sum to zero (digital
//     silence, a constant), is undetermined — no fake number.
//   * A NON-FINITE SAMPLE IS NOT A HOLE HERE, because the spec has no holes: it goes through the same
//     arithmetic, and a NaN bin simply never counts as flux while an infinite one makes the flux infinite and
//     zeroes the detrended onsets from there on. That is what the page does with the same float32 samples —
//     except that an infinity inside the transform meets C++'s complex multiplication (Annex G recovery) where
//     fft.js has plain products, so the two may disagree about which bins are infinite, and the disagreement is
//     not small: 2048 samples at 8 kHz, zero but for one +Inf at 1200, give an infinite flux in frame 1 here and 0
//     on the page, and the infinity then zeroes every later detrended onset here and none there (found by a
//     review round). Decoded audio carries no non-finite sample; `nonFiniteSamples()` counts the mono ones so a
//     caller can decline to believe a report — a report with any is not a port of the page's answer.
//
// STREAMING. `prepare()` takes the programme's TOTAL length, because every buffer is sized by it (law 11d: the
// demand is published before it is paid). `process()` takes any split of the programme and gives the bits one
// call would — a frame is transformed on the sample that completes it, whatever call that sample arrives in. The
// width is EXACT (law 11c): the mix divides by the channel count, so a narrower call would be a different
// programme, and is refused. A call that would run past the prepared length is refused whole, before anything
// moves. `finish()` analyses every sample seen so far — the prepared length, or a prefix of it — and freezes.
//
// OFFLINE, RT-safe in its calls: prepare() is the only allocation; process() and finish() do no alloc / lock /
// throw. finish() costs O(frames x lags) and is meant for a worker thread, never an audio callback.
//
// WHERE THE TIME IS SPENT, AND WHY EACH PLACE IS A FUNCTION OF ITS OWN. Nearly all of an analysis is the onset
// curve — one 1024-point transform per 512 samples, 35 203 of them in a 6:15 programme at 48 kHz — and the rest is
// the mix before it and the autocorrelation after it. The page runs this as wasm, and V8 gives a wasm function
// optimised code only on its NEXT call: there is no on-stack replacement for wasm, so a loop inside a function that
// one analysis enters once runs that entire analysis on the baseline compiler. v0.2.1 was exactly that — the
// optimiser had folded the whole analysis into the one exported call — and the first analysis of that programme
// took 1.11 s against 0.48 s for every later one (node 26; 1.13 s against 0.49 s in headless Chromium), which a
// page could only hide with a warm-up run. So every loop that carries time runs in a function called many times per
// analysis: mixIn() and onsetFrame() once per frame, lagSum() once per lag, analyzeWindow() once per window. Each is
// FELITRONICS_TEMPO_NOINLINE, so the compiler keeps it; tools/wasm/build.sh keeps binaryen, emcc's post-link
// optimiser, from inlining a function with one caller regardless (it does, noinline or not); and
// tools/wasm/tierup-check.mjs fails the build when one of the four is missing from either module. What is left in
// once-called code — the detrend, the beat phase, the median — is a few hundred thousand operations, not tens of
// millions. The boundaries change no arithmetic: the same operations on the same operands in the same order, and
// the output is byte-identical to v0.2.1's, native and wasm.
class TempoDetector
{
    struct Peak
    {
        std::int64_t lag = 0;
        double bpm = 0.0, ac = 0.0, score = 0.0;
    };

    // One analyzeWindow() result. `ok == false` is the spec's `null`.
    struct Window
    {
        bool ok = false;
        double bpm = 0.0, confidence = 0.0, refinedLag = 0.0;
        std::int64_t peaks = 0;
    };

public:
    static constexpr int kFrame = 1024;                   // FRAME — the STFT frame, ~23 ms at 44.1 kHz
    static constexpr int kHop   = 512;                    // HOP   — the onset curve's sample period
    static constexpr int kBins  = kFrame / 2 + 1;         // the magnitude bins the flux is summed over
    static constexpr double kPrefCenterBpm   = 120.0;     // PREF_CENTER — the perceptual tempo centre
    static constexpr double kPrefSigmaOct    = 0.85;      // PREF_SIGMA_OCT — the whole-track prior's width
    static constexpr double kAnchorSigmaOct  = 0.35;      // a window's closeness-to-the-track width
    static constexpr double kDetrendSec      = 0.15;      // the detrend window, each side
    static constexpr double kMinAnalysisSec  = 1.5;       // shorter onset curves are undetermined
    static constexpr double kHighConfidence   = 0.5;
    static constexpr double kMediumConfidence = 0.28;
    static constexpr double kCurveConfidence  = 0.15;     // a window under this is a gap in the curve
    static constexpr double kPointConfidence  = 0.2;      // a smoothed point under this does not vote
    static constexpr double kVariesFraction   = 0.06;     // `varies`: the 10-90 % span over 6 % of the tempo...
    static constexpr double kVariesBpm        = 5.0;      // ...and at least 5 BPM
    static constexpr double kRunnerUpBpm      = 2.0;      // the whole track's runner-up is > 2 BPM away
    static constexpr int    kSmoothPoints     = 5;        // the curve's median window
    static constexpr int    kMaxCandidates    = 5;
    static constexpr int    kMinWindowFrames  = 8;
    static constexpr double kSqrt2 = 1.4142135623730951;  // Math.SQRT2

    // Admission bounds — not part of the answer. The rate range is the offline measurers' (the core's floor,
    // 768 kHz). The frame ceiling keeps every onset buffer under 2^24 doubles, i.e. addressable with a 32-bit
    // size_t (wasm32) and about 50 hours at 48 kHz. The BPM bounds keep the lag range a range of lags.
    static constexpr double        kMinSampleRate = core::kMinSampleRate;
    static constexpr double        kMaxSampleRate = 768000.0;
    static constexpr std::uint64_t kMaxFrames     = std::uint64_t (1) << 33;
    static constexpr double        kMinBpmLimit   = 1.0;
    static constexpr double        kMaxBpmLimit   = 1000.0;

    //==============================================================================
    // WHAT prepare() ASKS THE HEAP FOR (law 11d), from the function prepare() sizes AND validates itself with.
    // Every count is a function of (rate, length, params) and nothing else, so a caller can price a programme
    // before decoding it.
    static constexpr std::uint64_t constructBytes() noexcept { return storage::kVectorProxyBytes; }

    struct Storage
    {
        bool ok = false;
        std::size_t ringFloats    = 0;    // float, kFrame: the mono samples of the frame being filled
        std::size_t windowDoubles = 0;    // double, kFrame: the Hann window
        std::size_t fftComplex    = 0;    // complex<double>, kFrame: the transform's scratch
        std::size_t magDoubles    = 0;    // double, 2 * kBins: this frame's and the previous frame's magnitudes
        std::size_t odfDoubles    = 0;    // double, onset frames: the onset curve (detrended in place)
        std::size_t prefixDoubles = 0;    // double, onset frames + 1: the detrend's running sums
        std::size_t acDoubles     = 0;    // double, the largest lag + 1
        std::size_t peakRecords   = 0;    // Peak, per list: the whole track's, a window's, and the sort's scratch
        std::size_t pointRecords  = 0;    // TempoPoint, per list: the raw curve and the smoothed one
        std::size_t pointDoubles  = 0;    // double, one per point: the median's sort
        std::uint64_t odfFrames   = 0;    // how many onset frames the prepared length yields
        std::uint64_t points      = 0;    // how many curve points it yields at most
        std::uint64_t firstBytes() const noexcept { return TempoDetector::constructBytes() + bytes(); }
        std::uint64_t bytes() const noexcept
        {
            return (std::uint64_t) sizeof (float) * ringFloats
                 + (std::uint64_t) sizeof (double) * ((std::uint64_t) windowDoubles + magDoubles + odfDoubles
                                                      + prefixDoubles + acDoubles + pointDoubles)
                 + (std::uint64_t) sizeof (std::complex<double>) * fftComplex
                 + (std::uint64_t) sizeof (Peak) * 3u * (std::uint64_t) peakRecords
                 + (std::uint64_t) sizeof (TempoPoint) * 2u * (std::uint64_t) pointRecords;
        }
    };

    // The number of onset frames `frames` samples make: max(0, 1 + floor((frames - FRAME) / HOP)).
    static constexpr std::uint64_t onsetFramesFor (std::uint64_t frames) noexcept
    {
        return frames < (std::uint64_t) kFrame ? 0u : 1u + (frames - (std::uint64_t) kFrame) / (std::uint64_t) kHop;
    }

    // ok == false on exactly the arguments prepare() refuses, and then every count is zero.
    static Storage storageFor (double sampleRate, int channels, std::uint64_t totalFrames, const TempoParams& p) noexcept
    {
        Storage s;
        if (! (sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate)) return s;   // NaN fails
        if (channels < 1 || channels > core::kMaxChannels) return s;
        if (totalFrames > kMaxFrames) return s;
        if (! (p.minBpm >= kMinBpmLimit) || ! (p.maxBpm <= kMaxBpmLimit) || ! (p.minBpm <= p.maxBpm)) return s;
        if (! (p.winSec > 0.0) || ! std::isfinite (p.winSec)) return s;
        if (! (p.hopSec > 0.0) || ! std::isfinite (p.hopSec)) return s;

        const double odfSr = sampleRate / (double) kHop;
        const std::uint64_t n = onsetFramesFor (totalFrames);
        // The largest lag any analysis can index is min(n - 1, ceil(60 / minBpm * odfSr)) — a window is never
        // longer than the whole curve — so the array holds one more than that. Bounded in DOUBLE before the
        // conversion: a comparison is portable, a conversion of an out-of-range double is not.
        const double lagTop = std::ceil ((60.0 / p.minBpm) * odfSr);
        std::uint64_t ac = 0;
        if (n > 0) ac = (lagTop + 1.0 < (double) n) ? (std::uint64_t) lagTop + 1u : n;

        s.ok = true;
        s.ringFloats    = (std::size_t) kFrame;
        s.windowDoubles = (std::size_t) kFrame;
        s.fftComplex    = (std::size_t) kFrame;
        s.magDoubles    = 2u * (std::size_t) kBins;
        s.odfDoubles    = (std::size_t) n;
        s.prefixDoubles = n > 0 ? (std::size_t) n + 1u : 0u;
        s.acDoubles     = (std::size_t) ac;
        s.peakRecords   = (std::size_t) ac;          // the peaks are lags strictly inside the range: fewer than this
        const std::uint64_t pts = pointsFor (n, odfSr, p);
        s.pointRecords  = (std::size_t) pts;
        s.pointDoubles  = (std::size_t) pts;
        s.odfFrames     = n;
        s.points        = pts;
        return s;
    }

    void setParams (const TempoParams& p) noexcept { params_ = p; }   // structural: takes effect at the next prepare()
    const TempoParams& params() const noexcept { return installed_; } // what prepare() installed; the defaults when unprepared

    [[nodiscard]] bool prepare (double sampleRate, int channels, std::uint64_t totalFrames) noexcept
    {
        // Law 11b: disarm, validate, write — and disarming includes the report, so a refused prepare() after a
        // finished analysis leaves nothing readable.
        prepared_ = false;
        finished_ = false;
        channels_ = 0;
        seen_ = 0; odfFrames_ = 0; nonFinite_ = 0; total_ = 0;
        sampleRate_ = 0.0; odfSr_ = 0.0; winFramesSpec_ = 0.0; hopFramesSpec_ = 0.0;
        installed_ = TempoParams {};
        clearReport();
        const Storage st = storageFor (sampleRate, channels, totalFrames, params_);
        if (! st.ok) return false;

        installed_  = params_;
        sampleRate_ = sampleRate;
        odfSr_      = sampleRate / (double) kHop;
        channels_   = channels;
        total_      = totalFrames;
        // The curve's geometry as the spec states it, published unclamped (a window longer than the programme is
        // still the window the caller asked for; the loop in analyse() is what knows it does not fit).
        winFramesSpec_ = js::max ((double) kMinWindowFrames, js::round (installed_.winSec * odfSr_));
        hopFramesSpec_ = js::max (1.0, js::round (installed_.hopSec * odfSr_));

        ring_.assign (st.ringFloats, 0.0f);
        window_.assign (st.windowDoubles, 0.0);
        scratch_.assign (st.fftComplex, std::complex<double> {});
        mag_.assign (st.magDoubles, 0.0);
        odf_.assign (st.odfDoubles, 0.0);
        prefix_.assign (st.prefixDoubles, 0.0);
        ac_.assign (st.acDoubles, 0.0);
        globalPeaks_.assign (st.peakRecords, Peak {});
        windowPeaks_.assign (st.peakRecords, Peak {});
        sortScratch_.assign (st.peakRecords, Peak {});
        raw_.assign (st.pointRecords, TempoPoint {});
        curve_.assign (st.pointRecords, TempoPoint {});
        sortPts_.assign (st.pointDoubles, 0.0);

        // hann(FRAME): 0.5 - 0.5 cos(2 pi i / (n - 1)) — the SYMMETRIC form the spec builds, each coefficient
        // from its own argument, in the spec's order: (2 * pi * i) / (n - 1). 0.5 * c is exact, so the pin on
        // it changes nothing and is there so a reader need not prove that.
        for (int i = 0; i < kFrame; ++i)
        {
            const double c = core::det::cos ((2.0 * core::kPi * (double) i) / (double) (kFrame - 1));
            window_[(std::size_t) i] = 0.5 - core::det::mul (0.5, c);
        }

        prepared_ = true;
        reset();
        return true;
    }

    // Back to the head of the same programme: nothing seen, nothing reported.
    void reset() noexcept
    {
        finished_ = false;
        seen_ = 0;
        odfFrames_ = 0;
        nonFinite_ = 0;
        std::fill (ring_.begin(), ring_.end(), 0.0f);
        std::fill (mag_.begin(), mag_.end(), 0.0);
        std::fill (odf_.begin(), odf_.end(), 0.0);
        clearReport();
    }

    //==============================================================================
    // Law 11 order: malformed -> unprepared or finished -> the width (EXACT) -> n == 0 -> a plane missing ->
    // past the prepared length -> run. A refused call consumes nothing.
    [[nodiscard]] bool process (const float* const* in, int numChannels, int n) noexcept
    {
        if (numChannels < 0 || n < 0) return false;
        if (numChannels > 0 && in == nullptr) return false;
        if (! prepared_ || finished_) return false;
        if (numChannels != channels_) return false;
        if (n == 0) return true;
        for (int c = 0; c < channels_; ++c) if (in[c] == nullptr) return false;
        if ((std::uint64_t) n > total_ - seen_) return false;

        // One stretch per onset frame: the samples up to the one that completes the next frame, then that frame.
        // The same samples in the same order as a loop over all of them — the stretches exist so that the loops
        // run in functions called once per frame (WHERE THE TIME IS SPENT, above).
        for (int i = 0; i < n;)
        {
            const std::uint64_t frameEnd = nextFrameEnd();
            const int take = (int) std::min<std::uint64_t> ((std::uint64_t) (n - i), frameEnd - seen_);
            mixIn (in, i, take);
            i += take;
            if (seen_ == frameEnd) onsetFrame();
        }
        return true;
    }

    // End of the programme: detrend, the whole-track analysis, the curve, the headline. Analyses every sample
    // seen so far and freezes; process() refuses until reset(). Idempotent.
    [[nodiscard]] bool finish() noexcept
    {
        if (! prepared_) return false;
        if (finished_) return true;
        analyse();
        finished_ = true;
        return true;
    }

    //==============================================================================
    bool isPrepared() const noexcept { return prepared_; }
    bool isFinished() const noexcept { return finished_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int channels() const noexcept { return channels_; }
    std::uint64_t totalFrames() const noexcept { return total_; }
    std::uint64_t framesSeen() const noexcept { return seen_; }
    bool complete() const noexcept { return prepared_ && seen_ == total_; }
    std::uint64_t nonFiniteSamples() const noexcept { return nonFinite_; }   // mono samples that were not numbers

    // The onset curve's rate (the spec returns it as `odfSr`) and its length so far.
    double odfSampleRate() const noexcept { return odfSr_; }
    std::uint64_t onsetFrames() const noexcept { return odfFrames_; }
    // The onset curve itself — raw spectral flux until finish(), detrended after it. For inspection; null when
    // unprepared, so a refused prepare() does not leave the previous programme's curve readable.
    const double* onsetCurve() const noexcept { return (prepared_ && ! odf_.empty()) ? odf_.data() : nullptr; }

    // The curve's geometry at the installed parameters, in onset frames, as the spec computes it:
    // max(8, round(winSec * odfSr)) and max(1, round(hopSec * odfSr)). Doubles, because a caller may ask for a
    // window no programme could hold; 0 when unprepared.
    double windowFrames() const noexcept { return winFramesSpec_; }
    double hopFrames()    const noexcept { return hopFramesSpec_; }

    // --- the report (all zero / NaN / empty before finish()) ---
    const TempoHeadline& headline()   const noexcept { return headline_; }    // tempoCurve()
    const TempoHeadline& wholeTrack() const noexcept { return wholeTrack_; }  // detectTempo()
    bool determined() const noexcept { return headline_.determined; }

    bool varies() const noexcept { return varies_; }
    bool hasRange() const noexcept { return hasRange_; }
    double rangeLow()  const noexcept { return rangeLo_; }                     // rounded to 0.1
    double rangeHigh() const noexcept { return rangeHi_; }

    int candidateCount() const noexcept { return candidateCount_; }
    TempoCandidate candidate (int i) const noexcept
    {
        if (i < 0 || i >= candidateCount_) return {};
        const Peak& p = globalPeaks_[(std::size_t) i];
        return { round1 (p.bpm), p.score };
    }

    std::int64_t pointCount() const noexcept { return pointCount_; }
    TempoPoint point (std::int64_t i) const noexcept
    {
        return (i < 0 || i >= pointCount_) ? TempoPoint {} : curve_[(std::size_t) i];
    }
    // The same point BEFORE the median — the window's own rounded answer. Not part of the spec's output; kept
    // because it is what a reader needs to tell a smoothed-away spike from a gap.
    TempoPoint rawPoint (std::int64_t i) const noexcept
    {
        return (i < 0 || i >= pointCount_) ? TempoPoint {} : raw_[(std::size_t) i];
    }

    // The whole-track analysis before any rounding: the tempo the curve is anchored to, its confidence, the
    // refined lag the beat phase is taken at. NaN / 0 when undetermined.
    double anchorBpm()        const noexcept { return anchorBpm_; }
    double anchorConfidence() const noexcept { return anchorConfidence_; }
    double anchorLag()        const noexcept { return anchorLag_; }

    // The label rule, on its own (the spec's labelFor).
    static ConfidenceLabel labelFor (double c) noexcept
    {
        return c >= kHighConfidence   ? ConfidenceLabel::High
             : c >= kMediumConfidence ? ConfidenceLabel::Medium
             : c > 0.0                ? ConfidenceLabel::Low
                                      : ConfidenceLabel::Undetermined;
    }

    // The fold rule, on its own (the spec's octaveFold): double while under anchor/sqrt2 and the double stays
    // <= maxBpm, then halve while over anchor*sqrt2 and the half stays >= minBpm. `!anchor` in the spec: an anchor
    // of 0 or NaN leaves the tempo alone.
    static double foldToward (double bpm, double anchor, double minBpm, double maxBpm) noexcept
    {
        if (! (anchor < 0.0 || anchor > 0.0)) return bpm;
        double b = bpm;
        while (b < anchor / kSqrt2 && b * 2.0 <= maxBpm) b *= 2.0;
        while (b > anchor * kSqrt2 && b / 2.0 >= minBpm) b /= 2.0;
        return b;
    }

private:
    static double round1 (double v) noexcept { return js::round (v * 10.0) / 10.0; }
    static double round2 (double v) noexcept { return js::round (v * 100.0) / 100.0; }

    // How many windows `for (start = 0; start + winFrames <= n; start += hopFrames)` visits. The frame counts
    // are formed in DOUBLE and compared before any conversion, so an absurd winSec is "no window", not a UB cast.
    static std::uint64_t pointsFor (std::uint64_t n, double odfSr, const TempoParams& p) noexcept
    {
        std::int64_t win = 0, hop = 0;
        if (! curveGeometry (n, odfSr, p, win, hop)) return 0u;
        return ((std::uint64_t) ((std::int64_t) n - win)) / (std::uint64_t) hop + 1u;
    }

    // winFrames = max(8, round(winSec * odfSr)), hopFrames = max(1, round(hopSec * odfSr)). False when not even
    // one window fits in `n` frames. The hop is capped at n + 1, which visits exactly the windows the spec does.
    static bool curveGeometry (std::uint64_t n, double odfSr, const TempoParams& p,
                               std::int64_t& win, std::int64_t& hop) noexcept
    {
        const double w = js::max ((double) kMinWindowFrames, js::round (p.winSec * odfSr));
        const double h = js::max (1.0, js::round (p.hopSec * odfSr));
        if (! (w <= (double) n)) return false;
        win = (std::int64_t) w;
        hop = (h <= (double) n + 1.0) ? (std::int64_t) h : (std::int64_t) n + 1;
        return true;
    }

    void clearReport() noexcept
    {
        headline_ = TempoHeadline {};
        wholeTrack_ = TempoHeadline {};
        varies_ = false; hasRange_ = false;
        rangeLo_ = rangeHi_ = std::numeric_limits<double>::quiet_NaN();
        candidateCount_ = 0;
        pointCount_ = 0;
        anchorBpm_ = anchorLag_ = std::numeric_limits<double>::quiet_NaN();
        anchorConfidence_ = 0.0;
    }

    // The value seen_ has when the next onset frame is complete: FRAME, then every HOP after it — the samples
    // after which the per-sample loop this replaced called onsetFrame() (seen_ >= FRAME, (seen_ - FRAME) % HOP == 0).
    std::uint64_t nextFrameEnd() const noexcept
    {
        constexpr auto frame = (std::uint64_t) kFrame, hop = (std::uint64_t) kHop;
        return seen_ < frame ? frame : frame + ((seen_ - frame) / hop + 1u) * hop;
    }

    // toMono of `count` samples from index `from`, into the frame ring: a Float32Array accumulated channel by
    // channel, then divided by the count. Each step is a float operation here and a binary64 one rounded to float32
    // there, and for + and / those are the same number (binary64 has more than 2 x 24 + 2 bits, so the double
    // rounding is innocuous). At most one frame's worth per call, called once per frame: see WHERE THE TIME IS SPENT.
    FELITRONICS_TEMPO_NOINLINE void mixIn (const float* const* in, int from, int count) noexcept
    {
        for (int i = from; i < from + count; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < channels_; ++c) m += in[c][i];
            if (channels_ > 1) m /= (float) channels_;
            if (! std::isfinite (m)) ++nonFinite_;
            ring_[(std::size_t) (seen_ % (std::uint64_t) kFrame)] = m;
            ++seen_;
        }
    }

    // One onset frame, on the sample that completes it: the window over the last FRAME samples, the magnitude
    // spectrum, the positive flux against the previous frame's (onsetEnvelope's loop body). Called once per frame
    // and kept a function of its own on purpose — it is where the analysis spends its time (WHERE THE TIME IS SPENT).
    FELITRONICS_TEMPO_NOINLINE void onsetFrame() noexcept
    {
        const std::size_t start = (std::size_t) (seen_ % (std::uint64_t) kFrame);   // the oldest sample
        for (int i = 0; i < kFrame; ++i)
        {
            const double x = (double) ring_[(start + (std::size_t) i) % (std::size_t) kFrame];
            scratch_[(std::size_t) i] = std::complex<double> (x * window_[(std::size_t) i], 0.0);
        }
        core::offline::fftInplace (scratch_, -1);
        const std::size_t curAt  = (odfFrames_ & 1u) ? (std::size_t) kBins : 0u;   // the spec swaps two buffers
        const std::size_t prevAt = (odfFrames_ & 1u) ? 0u : (std::size_t) kBins;
        double* cur = mag_.data() + curAt;
        const double* prev = mag_.data() + prevAt;
        for (int k = 0; k < kBins; ++k)
            cur[k] = js::hypot (scratch_[(std::size_t) k].real(), scratch_[(std::size_t) k].imag());
        double flux = 0.0;
        if (odfFrames_ > 0)
            for (int k = 0; k < kBins; ++k) { const double d = cur[k] - prev[k]; if (d > 0.0) flux += d; }
        odf_[(std::size_t) odfFrames_] = flux;
        ++odfFrames_;
    }

    //==============================================================================
    // The spec's analysis, in its order.
    void analyse() noexcept
    {
        clearReport();
        const std::int64_t n = (std::int64_t) odfFrames_;
        detrend (n);

        const Window global = analyzeWindow (odf_.data(), n, false, 0.0, globalPeaks_.data());
        if (! global.ok) return;                                       // UNDETERMINED: no curve, no range

        anchorBpm_ = global.bpm;
        anchorConfidence_ = global.confidence;
        anchorLag_ = global.refinedLag;
        candidateCount_ = (int) std::min<std::int64_t> (global.peaks, kMaxCandidates);

        // --- over time: windows over the SAME detrended onset curve, anchored to the whole track ---
        const double anchor = global.bpm;
        std::int64_t count = 0, winFrames = 0, hopFrames = 0;
        if (curveGeometry ((std::uint64_t) n, odfSr_, installed_, winFrames, hopFrames))
            for (std::int64_t startAt = 0; startAt + winFrames <= n; startAt += hopFrames)
            {
                const Window w = analyzeWindow (odf_.data() + (std::size_t) startAt, winFrames, true, anchor, windowPeaks_.data());
                const double t = ((double) startAt + (double) winFrames / 2.0) / odfSr_;
                TempoPoint& p = raw_[(std::size_t) count++];
                p.t = round1 (t);
                p.hasBpm = w.ok && w.confidence >= kCurveConfidence;   // a NaN confidence is a gap
                p.bpm = p.hasBpm ? round1 (w.bpm) : std::numeric_limits<double>::quiet_NaN();
                p.conf = w.ok ? round2 (w.confidence) : 0.0;
            }
        pointCount_ = count;
        medianSmooth();

        // --- the robust whole-track number: the median of the smoothed confident points ---
        std::int64_t m = 0;
        for (std::int64_t i = 0; i < count; ++i)
            if (curve_[(std::size_t) i].hasBpm && curve_[(std::size_t) i].conf >= kPointConfidence)
                sortPts_[(std::size_t) m++] = curve_[(std::size_t) i].bpm;
        std::sort (sortPts_.data(), sortPts_.data() + (std::size_t) m);   // finite values: any sort is the spec's
        double globalBpm = global.bpm;
        if (m >= 3)
        {
            globalBpm = sortPts_[(std::size_t) (m / 2)];
            const double lo = sortPts_[(std::size_t) std::floor ((double) m * 0.1)];
            const double hi = sortPts_[(std::size_t) std::floor ((double) m * 0.9)];
            hasRange_ = true;
            rangeLo_ = round1 (lo);
            rangeHi_ = round1 (hi);
            varies_ = (hi - lo) / globalBpm > kVariesFraction && (hi - lo) >= kVariesBpm;
        }

        // --- the two headlines: tempoCurve's (bpm = the median, the rest off its fold) and detectTempo's ---
        headline_ = assemble (octaveFold (globalBpm, true, anchor), global);
        headline_.bpm = round1 (globalBpm);
        wholeTrack_ = assemble (global.bpm, global);
    }

    // Subtract a local moving mean (+/- round(0.15 odfSr) frames) and keep the positive part. In place: step i
    // reads odf[i] before it writes it, and every mean comes from the prefix sums of the RAW curve.
    void detrend (std::int64_t n) noexcept
    {
        if (n <= 0) return;
        const std::int64_t w = (std::int64_t) js::max (1.0, js::round (odfSr_ * kDetrendSec));
        prefix_[0] = 0.0;
        for (std::int64_t i = 0; i < n; ++i) prefix_[(std::size_t) i + 1] = prefix_[(std::size_t) i] + odf_[(std::size_t) i];
        for (std::int64_t i = 0; i < n; ++i)
        {
            const std::int64_t a = std::max<std::int64_t> (0, i - w), b = std::min<std::int64_t> (n, i + w + 1);
            const double mean = (prefix_[(std::size_t) b] - prefix_[(std::size_t) a]) / (double) (b - a);
            const double v = odf_[(std::size_t) i] - mean;
            odf_[(std::size_t) i] = v > 0.0 ? v : 0.0;
        }
    }

    static double lagToBpm (double lag, double odfSr) noexcept { return (60.0 * odfSr) / lag; }

    // The log-normal preference, in octaves around `center`.
    static double octaveWeight (double bpm, double center, double sigmaOct) noexcept
    {
        const double z = core::det::log2 (bpm / center) / sigmaOct;
        return js::exp (-0.5 * z * z);
    }

    // `ac[lag] || 0`: JavaScript's falsy — 0, -0 and NaN all read as +0.
    static double orZero (double v) noexcept { return (std::isnan (v) || ! (v < 0.0 || v > 0.0)) ? 0.0 : v; }

    // Parabolic interpolation around a peak — the sub-lag the fractional BPM comes from (refineLag).
    double refineLag (std::int64_t lag) const noexcept
    {
        const double y0 = orZero (ac_[(std::size_t) (lag - 1)]);
        const double y1 = orZero (ac_[(std::size_t) lag]);
        const double y2 = orZero (ac_[(std::size_t) (lag + 1)]);
        const double denom = (y0 - core::det::mul (2.0, y1)) + y2;
        if (! std::isnan (denom) && ! (denom < 0.0) && ! (denom > 0.0)) return (double) lag;   // denom === 0
        const double delta = (0.5 * (y0 - y2)) / denom;
        return (double) lag + js::max (-1.0, js::min (1.0, delta));
    }

    double octaveFold (double bpm, bool hasAnchor, double anchor) const noexcept
    {
        return hasAnchor ? foldToward (bpm, anchor, installed_.minBpm, installed_.maxBpm) : bpm;
    }

    // Stable, descending by score, without allocating: a bottom-up merge through the prepared scratch. V8's sort
    // is stable (TimSort) and the scores are finite, so this is the permutation `sort((a, b) => b.score -
    // a.score)` produces — ties keep their lag order.
    void sortByScore (Peak* v, std::int64_t count) noexcept
    {
        Peak* a = v;
        Peak* b = sortScratch_.data();
        for (std::int64_t width = 1; width < count; width *= 2)
        {
            for (std::int64_t lo = 0; lo < count; lo += 2 * width)
            {
                const std::int64_t mid = std::min (lo + width, count), hi = std::min (lo + 2 * width, count);
                std::int64_t i = lo, j = mid, k = lo;
                while (i < mid && j < hi) b[k++] = (a[j].score > a[i].score) ? a[j++] : a[i++];   // ties: the left one
                while (i < mid) b[k++] = a[i++];
                while (j < hi)  b[k++] = a[j++];
            }
            std::swap (a, b);
        }
        if (a != v) std::copy (a, a + count, v);
    }

    // One lag of autocorr(): sum(odf[i] * odf[i - lag]) over i in [lag, n), the product pinned because JavaScript
    // never fuses it. A function per LAG, not per window, because the whole track's window is entered once per
    // analysis and is the longest one (WHERE THE TIME IS SPENT).
    static FELITRONICS_TEMPO_NOINLINE double lagSum (const double* odf, std::int64_t n, std::int64_t lag) noexcept
    {
        double s = 0.0;
        for (std::int64_t i = lag; i < n; ++i) s = core::det::mulAdd (odf[i], odf[i - lag], s);
        return s;
    }

    // The spec's analyzeWindow(): autocorrelate one stretch of the onset curve, pick a peak, refine it, rate it.
    // `peaks` is the list this call fills (the whole track's own, or the windows' shared one).
    FELITRONICS_TEMPO_NOINLINE Window analyzeWindow (const double* odf, std::int64_t n, bool hasAnchor, double anchor,
                                                     Peak* peaks) noexcept
    {
        Window out;
        double energy = 0.0;
        for (std::int64_t i = 0; i < n; ++i) energy += odf[i];
        if ((double) n < odfSr_ * kMinAnalysisSec || energy <= 0.0) return out;

        // --- autocorr(): the lags of [minBpm, maxBpm], each an unbiased mean of lagged products ---
        const double minLagD = js::max (1.0, std::floor ((60.0 / installed_.maxBpm) * odfSr_));
        const double maxLagD = js::min ((double) (n - 1), std::ceil ((60.0 / installed_.minBpm) * odfSr_));
        if (! (minLagD <= maxLagD)) return out;                        // an empty range has no peak
        const std::int64_t minLag = (std::int64_t) minLagD, maxLag = (std::int64_t) maxLagD;
        for (std::int64_t lag = minLag; lag <= maxLag; ++lag)
            ac_[(std::size_t) lag] = lagSum (odf, n, lag) / (double) (n - lag);

        // --- every local maximum strictly inside the range ---
        std::int64_t count = 0;
        for (std::int64_t lag = minLag + 1; lag < maxLag; ++lag)
        {
            const double here = ac_[(std::size_t) lag];
            if (here >= ac_[(std::size_t) (lag - 1)] && here >= ac_[(std::size_t) (lag + 1)])
            {
                Peak& p = peaks[count++];
                p.lag = lag;
                p.bpm = lagToBpm ((double) lag, odfSr_);
                p.ac = here;
                p.score = here * octaveWeight (p.bpm, kPrefCenterBpm, kPrefSigmaOct);
            }
        }
        if (count == 0) return out;

        const Peak* runnerUp = nullptr;
        if (! hasAnchor)
        {
            sortByScore (peaks, count);                                // the perceptual octave choice
            for (std::int64_t k = 0; k < count; ++k)
                if (std::fabs (peaks[k].bpm - peaks[0].bpm) > kRunnerUpBpm) { runnerUp = &peaks[k]; break; }
            if (runnerUp == nullptr && count > 1) runnerUp = &peaks[1];
        }
        else
        {
            // closeness to the whole-track tempo replaces the perceptual prior, then the same stable sort
            for (std::int64_t k = 0; k < count; ++k)
                peaks[k].score = peaks[k].ac * octaveWeight (peaks[k].bpm, anchor, kAnchorSigmaOct);
            sortByScore (peaks, count);
            if (count > 1) runnerUp = &peaks[1];
        }
        const Peak& best = peaks[0];

        const double refinedLag = refineLag (best.lag);
        const double bpm = octaveFold (lagToBpm (refinedLag, odfSr_), hasAnchor, anchor);

        double acMean = 0.0;
        std::int64_t cnt = 0;
        for (std::int64_t lag = minLag; lag <= maxLag; ++lag) { acMean += ac_[(std::size_t) lag]; ++cnt; }
        acMean = cnt > 0 ? acMean / (double) cnt : 0.0;
        const double margin = runnerUp != nullptr ? (best.score - runnerUp->score) / best.score : 1.0;
        const double strength = acMean > 0.0 ? js::min (1.0, (best.ac / acMean - 1.0) / 4.0) : 0.0;
        // 0.6 * margin + 0.4 * strength with BOTH products stored: fused, one of them would round once
        const double blend = core::det::mul (0.6, js::max (0.0, margin)) + core::det::mul (0.4, strength);
        out.confidence = js::max (0.0, js::min (1.0, blend));
        out.ok = true;
        out.bpm = bpm;
        out.refinedLag = refinedLag;
        out.peaks = count;
        return out;
    }

    // Median of up to five neighbours, over the RAW points, skipping gaps; the upper median for an even count
    // (w[w.length >> 1]). A gap stays a gap.
    void medianSmooth() noexcept
    {
        const std::int64_t half = kSmoothPoints >> 1;
        for (std::int64_t i = 0; i < pointCount_; ++i)
        {
            TempoPoint p = raw_[(std::size_t) i];
            if (p.hasBpm)
            {
                double w[kSmoothPoints] {};
                int k = 0;
                const std::int64_t lo = std::max<std::int64_t> (0, i - half), hi = std::min<std::int64_t> (pointCount_ - 1, i + half);
                for (std::int64_t j = lo; j <= hi; ++j) if (raw_[(std::size_t) j].hasBpm) w[k++] = raw_[(std::size_t) j].bpm;
                std::sort (w, w + k);
                p.bpm = w[k >> 1];
            }
            curve_[(std::size_t) i] = p;
        }
    }

    // Which of `period`-spaced onset phases carries the most onset energy (beatPhase), over the WHOLE curve.
    std::int64_t beatPhase (double periodFrames) const noexcept
    {
        const double pd = js::max (1.0, js::round (periodFrames));
        if (! (pd >= 1.0)) return 0;                                   // NaN: the spec's loop does not run
        const std::int64_t n = (std::int64_t) odfFrames_;
        const std::int64_t p = pd <= (double) n + 1.0 ? (std::int64_t) pd : n + 1;   // past n every sum is 0
        std::int64_t bestOff = 0;
        double bestSum = -std::numeric_limits<double>::infinity();
        for (std::int64_t off = 0; off < p; ++off)
        {
            double s = 0.0;
            for (std::int64_t i = off; i < n; i += p) s += odf_[(std::size_t) i];
            if (s > bestSum) { bestSum = s; bestOff = off; }
        }
        return bestOff;
    }

    // assembleResult(): the public single-tempo fields from a tempo and the whole-track analysis.
    TempoHeadline assemble (double bpm, const Window& a) const noexcept
    {
        TempoHeadline h;
        h.determined = true;
        h.bpm = round1 (bpm);
        h.confidence = round2 (a.confidence);
        h.label = labelFor (a.confidence);
        const double cand[2] = { js::round (bpm / 2.0), js::round (bpm * 2.0) };
        for (double c : cand)
            if (c >= installed_.minBpm && c <= installed_.maxBpm) h.alts[h.altCount++] = c;
        h.beatPeriodSec = 60.0 / bpm;
        h.beatOffsetSec = (double) beatPhase (a.refinedLag) / odfSr_;
        return h;
    }

    TempoParams params_ {}, installed_ {};
    bool prepared_ = false, finished_ = false;
    double sampleRate_ = 0.0, odfSr_ = 0.0;
    int channels_ = 0;
    std::uint64_t total_ = 0, seen_ = 0, odfFrames_ = 0, nonFinite_ = 0;

    storage::Buffer<float> ring_;
    storage::Buffer<double> window_;
    std::vector<std::complex<double>> scratch_;
    storage::Buffer<double> mag_, odf_, prefix_, ac_;
    storage::Buffer<Peak> globalPeaks_, windowPeaks_, sortScratch_;
    storage::Buffer<TempoPoint> raw_, curve_;
    storage::Buffer<double> sortPts_;

    TempoHeadline headline_ {}, wholeTrack_ {};
    bool varies_ = false, hasRange_ = false;
    double rangeLo_ = std::numeric_limits<double>::quiet_NaN(), rangeHi_ = std::numeric_limits<double>::quiet_NaN();
    int candidateCount_ = 0;
    std::int64_t pointCount_ = 0;
    double winFramesSpec_ = 0.0, hopFramesSpec_ = 0.0;
    double anchorBpm_ = std::numeric_limits<double>::quiet_NaN(), anchorConfidence_ = 0.0,
           anchorLag_ = std::numeric_limits<double>::quiet_NaN();
};

} // namespace felitronics::tempo

#undef FELITRONICS_TEMPO_NOINLINE
