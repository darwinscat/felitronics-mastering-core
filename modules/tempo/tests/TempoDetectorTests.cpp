// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for tempo::TempoDetector — a port whose spec is executable (the site's dsp/tempo.js). The
// cross-language comparison on real programmes runs out of tree; what is pinned HERE is:
//   * THE SPEC'S BEHAVIOURS, as properties: a click train at N BPM reads N and offers exactly the in-range octaves,
//     a steady train is a flat curve that does not vary, a tempo that changes mid-programme shows where it changed,
//     silence and a short clip are undetermined, noise is never confidently wrong, the range option binds;
//   * THE SPEC'S OWN NUMBERS on four fixtures built without a transcendental (so node and C++ start from the same
//     float32 bits), each computed by tempo.js in node 26 and copied as a literal — the rounded fields exactly, the
//     unrounded ones to a tolerance that names why they are not exact;
//   * A REFERENCE OF A DIFFERENT CONSTRUCTION: tempo.js translated line by line into whole-buffer C++ (fresh arrays
//     per call, subarray copies, std::stable_sort, the detrend into a new array) against the streaming class, bit
//     for bit — the class's ring buffer, in-place detrend, shared lag table, merge sort and curve bookkeeping are
//     exactly what that reference does not share;
//   * the transform against a direct DFT, the mix against the page's toMono, and the call contract: split
//     invariance, the refusals, the exact width, reset(), a prefix, and law 11d — the demand IS the allocation.

#include <felitronics_test.h>
#include <alloc_counter.h>   // installs the allocation counter: EVERY form of `new`, over-aligned included
#include <felitronics/tempo/TempoDetector.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics;
using tempo::ConfidenceLabel;
using tempo::TempoDetector;
using tempo::TempoParams;
using felitronics::test::ok;

namespace
{
using Plane  = std::vector<float>;
using Planes = std::vector<Plane>;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::uint64_t bits (double d) { std::uint64_t u; std::memcpy (&u, &d, 8); return u; }
// Bit equality, NaN matching NaN — the only equality a port can be held to.
bool same (double a, double b) { return bits (a) == bits (b) || (std::isnan (a) && std::isnan (b)); }
std::string num (double v) { char b[48]; std::snprintf (b, sizeof b, "%.17g", v); return b; }

//==============================================================================
// FIXTURES. The click is a LINEAR decay with alternating sign — the spec's own test uses Math.exp, which is not the
// same function in two languages, and a fixture that differs by one float32 bit is a different programme. This one
// is exact rational arithmetic rounded once to float32, identical in node and here.
Plane clickTrain (double bpm, double seconds, double sr)
{
    const auto n = (std::size_t) tempo::js::round (seconds * sr);
    Plane sig (n, 0.0f);
    const double period = (60.0 / bpm) * sr;
    const int burst = (int) tempo::js::round (sr * 0.005);
    for (int beat = 0; ; ++beat)
    {
        const double s = tempo::js::round ((double) beat * period);
        if (s >= (double) n) break;
        const auto start = (std::size_t) s;
        for (int i = 0; i < burst && start + (std::size_t) i < n; ++i)
            sig[start + (std::size_t) i] = (float) ((1.0 - (double) i / (double) burst) * ((i % 2) ? -1.0 : 1.0));
    }
    return sig;
}

Plane concat (const Plane& a, const Plane& b) { Plane s (a); s.insert (s.end(), b.begin(), b.end()); return s; }

// One click at 3 s in 8 s of silence.
Plane loneClick (double sr)
{
    Plane sig ((std::size_t) (sr * 8.0), 0.0f);
    const int burst = (int) tempo::js::round (sr * 0.005);
    const auto at = (std::size_t) (sr * 3.0);
    for (int i = 0; i < burst; ++i)
        sig[at + (std::size_t) i] = (float) ((1.0 - (double) i / (double) burst) * ((i % 2) ? -1.0 : 1.0));
    return sig;
}

// The spec's own noise fixture, EXACTLY: `seed = (seed * 1103515245 + 12345) & 0x7fffffff` in JavaScript is a
// binary64 product (inexact above 2^53), then ToInt32 and the mask; `seed / 0x7fffffff - 0.5` in binary64; the
// sample rounded to float32. Reproduced step by step, so this is the programme the spec's own test measures.
Plane jsLcgNoise (std::size_t n)
{
    Plane v (n, 0.0f);
    double seed = 777.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        // The product is PINNED: above 2^53 it rounds, and `seed * a + c` fused into one FMA (the tree's -ffp-contract=on
        // does, on arm64) rounds once where JavaScript rounds twice — a different generator. Found by the witness below.
        const double t = core::det::mul (seed, 1103515245.0) + 12345.0;     // an integer-valued double < 2^63
        const auto asInt = (std::uint64_t) (std::int64_t) t;               // ToInt32: truncate, then modulo 2^32
        seed = (double) (asInt & 0x7fffffffull);
        v[i] = (float) ((seed / 2147483647.0 - 0.5) * 0.5);
    }
    return v;
}

//==============================================================================
// Everything the detector reports, captured for a bit-for-bit comparison.
struct Report
{
    bool ok = false;
    tempo::TempoHeadline head, whole;
    bool varies = false, hasRange = false;
    double lo = 0.0, hi = 0.0, anchorBpm = 0.0, anchorConf = 0.0, anchorLag = 0.0;
    std::vector<tempo::TempoCandidate> cand;
    std::vector<tempo::TempoPoint> curve, raw;
    std::vector<double> odf;
    std::uint64_t nonFinite = 0;
};

Report capture (const TempoDetector& d)
{
    Report r;
    r.ok = d.isFinished();
    r.head = d.headline(); r.whole = d.wholeTrack();
    r.varies = d.varies(); r.hasRange = d.hasRange(); r.lo = d.rangeLow(); r.hi = d.rangeHigh();
    r.anchorBpm = d.anchorBpm(); r.anchorConf = d.anchorConfidence(); r.anchorLag = d.anchorLag();
    for (int i = 0; i < d.candidateCount(); ++i) r.cand.push_back (d.candidate (i));
    for (std::int64_t i = 0; i < d.pointCount(); ++i) { r.curve.push_back (d.point (i)); r.raw.push_back (d.rawPoint (i)); }
    for (std::uint64_t i = 0; i < d.onsetFrames(); ++i) r.odf.push_back (d.onsetCurve()[i]);
    r.nonFinite = d.nonFiniteSamples();
    return r;
}

// Feeds the planes in the given split (a list of call lengths, repeated), then finishes.
Report runSplit (const Planes& p, double sr, const TempoParams& params, const std::vector<int>& split)
{
    TempoDetector d;
    d.setParams (params);
    const auto frames = (std::uint64_t) p[0].size();
    if (! d.prepare (sr, (int) p.size(), frames)) return {};
    std::uint64_t at = 0; std::size_t k = 0;
    std::vector<const float*> v (p.size());
    while (at < frames)
    {
        const int n = (int) std::min<std::uint64_t> ((std::uint64_t) split[k++ % split.size()], frames - at);
        for (std::size_t c = 0; c < p.size(); ++c) v[c] = p[c].data() + at;
        if (! d.process (v.data(), (int) p.size(), n)) return {};
        at += (std::uint64_t) n;
    }
    if (! d.finish()) return {};
    return capture (d);
}

Report run (const Plane& mono, double sr, const TempoParams& params = {})
{
    return runSplit ({ mono }, sr, params, { 1 << 30 });
}

bool sameHeadline (const tempo::TempoHeadline& a, const tempo::TempoHeadline& b)
{
    return a.determined == b.determined && same (a.bpm, b.bpm) && same (a.confidence, b.confidence)
        && a.label == b.label && a.altCount == b.altCount && same (a.alts[0], b.alts[0]) && same (a.alts[1], b.alts[1])
        && same (a.beatPeriodSec, b.beatPeriodSec) && same (a.beatOffsetSec, b.beatOffsetSec);
}

bool samePoints (const std::vector<tempo::TempoPoint>& a, const std::vector<tempo::TempoPoint>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].hasBpm != b[i].hasBpm || ! same (a[i].t, b[i].t) || ! same (a[i].bpm, b[i].bpm) || ! same (a[i].conf, b[i].conf))
            return false;
    return true;
}

// Everything, bit for bit. `withRaw` because the reference below has no "raw point" concept of its own to compare.
bool sameReport (const Report& a, const Report& b, bool withRaw = true)
{
    if (a.ok != b.ok || ! sameHeadline (a.head, b.head) || ! sameHeadline (a.whole, b.whole)) return false;
    if (a.varies != b.varies || a.hasRange != b.hasRange || ! same (a.lo, b.lo) || ! same (a.hi, b.hi)) return false;
    if (! same (a.anchorBpm, b.anchorBpm) || ! same (a.anchorConf, b.anchorConf) || ! same (a.anchorLag, b.anchorLag)) return false;
    if (a.cand.size() != b.cand.size()) return false;
    for (std::size_t i = 0; i < a.cand.size(); ++i)
        if (! same (a.cand[i].bpm, b.cand[i].bpm) || ! same (a.cand[i].score, b.cand[i].score)) return false;
    if (! samePoints (a.curve, b.curve) || (withRaw && ! samePoints (a.raw, b.raw))) return false;
    if (a.odf.size() != b.odf.size()) return false;
    for (std::size_t i = 0; i < a.odf.size(); ++i) if (! same (a.odf[i], b.odf[i])) return false;
    return a.nonFinite == b.nonFinite;
}

//==============================================================================
// THE REFERENCE: tempo.js, translated line by line, whole-buffer, allocating freely. Shares only the numerics the
// spec's arithmetic is DEFINED by here (core::det::cos for the window, core::offline::fftInplace, js::hypot/round/
// min/max/exp, core::det::log2, the pinned multiply-adds) — none of the class's bookkeeping.
namespace ref
{
struct Peak { std::int64_t lag; double bpm, ac, score; };
struct Win  { bool ok = false; double bpm = 0, confidence = 0, refinedLag = 0; std::vector<Peak> peaks; };

std::vector<double> hann (int n)
{
    std::vector<double> w ((std::size_t) n);
    for (int i = 0; i < n; ++i) w[(std::size_t) i] = 0.5 - 0.5 * core::det::cos ((2.0 * core::kPi * (double) i) / (double) (n - 1));
    return w;
}

std::vector<double> magnitude (const std::vector<double>& frame)
{
    std::vector<std::complex<double>> x (frame.size());
    for (std::size_t i = 0; i < frame.size(); ++i) x[i] = { frame[i], 0.0 };
    core::offline::fftInplace (x, -1);
    std::vector<double> mag (frame.size() / 2 + 1);
    for (std::size_t k = 0; k < mag.size(); ++k) mag[k] = tempo::js::hypot (x[k].real(), x[k].imag());
    return mag;
}

std::vector<double> onsetEnvelope (const Plane& sig)
{
    const std::vector<double> win = hann (1024);
    const std::int64_t len = (std::int64_t) sig.size();
    const std::int64_t nFrames = std::max<std::int64_t> (0, 1 + (std::int64_t) std::floor ((double) (len - 1024) / 512.0));
    std::vector<double> odf ((std::size_t) nFrames, 0.0), prev (513, 0.0);
    for (std::int64_t f = 0; f < nFrames; ++f)
    {
        std::vector<double> frame (1024);
        for (int i = 0; i < 1024; ++i) frame[(std::size_t) i] = (double) sig[(std::size_t) (f * 512 + i)] * win[(std::size_t) i];
        const std::vector<double> cur = magnitude (frame);
        if (f > 0)
        {
            double flux = 0.0;
            for (std::size_t k = 0; k < 513; ++k) { const double d = cur[k] - prev[k]; if (d > 0) flux += d; }
            odf[(std::size_t) f] = flux;
        }
        prev = cur;
    }
    return odf;
}

std::vector<double> detrend (const std::vector<double>& odf, double odfSr)
{
    const std::int64_t n = (std::int64_t) odf.size();
    std::vector<double> out ((std::size_t) n, 0.0);
    if (n == 0) return out;
    const std::int64_t w = (std::int64_t) tempo::js::max (1.0, tempo::js::round (odfSr * 0.15));
    std::vector<double> prefix ((std::size_t) n + 1, 0.0);
    for (std::int64_t i = 0; i < n; ++i) prefix[(std::size_t) i + 1] = prefix[(std::size_t) i] + odf[(std::size_t) i];
    for (std::int64_t i = 0; i < n; ++i)
    {
        const std::int64_t a = std::max<std::int64_t> (0, i - w), b = std::min<std::int64_t> (n, i + w + 1);
        const double mean = (prefix[(std::size_t) b] - prefix[(std::size_t) a]) / (double) (b - a);
        const double v = odf[(std::size_t) i] - mean;
        out[(std::size_t) i] = v > 0 ? v : 0;
    }
    return out;
}

double weight (double bpm, double centre, double sigma)
{
    const double z = core::det::log2 (bpm / centre) / sigma;
    return tempo::js::exp (-0.5 * z * z);
}

double fold (double bpm, double anchor, bool has, const TempoParams& p)
{
    if (! has) return bpm;
    double b = bpm;
    while (b < anchor / 1.4142135623730951 && b * 2 <= p.maxBpm) b *= 2;
    while (b > anchor * 1.4142135623730951 && b / 2 >= p.minBpm) b /= 2;
    return b;
}

Win analyzeWindow (const std::vector<double>& odf, double odfSr, const TempoParams& p, bool has, double anchor)
{
    Win out;
    double energy = 0; for (double v : odf) energy += v;
    const std::int64_t n = (std::int64_t) odf.size();
    if ((double) n < odfSr * 1.5 || energy <= 0) return out;
    const std::int64_t minLag = (std::int64_t) std::max (1.0, std::floor ((60 / p.maxBpm) * odfSr));
    const std::int64_t maxLag = (std::int64_t) std::min ((double) (n - 1), std::ceil ((60 / p.minBpm) * odfSr));
    std::vector<double> ac ((std::size_t) std::max<std::int64_t> (0, maxLag + 1), 0.0);
    for (std::int64_t lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (std::int64_t i = lag; i < n; ++i) s = core::det::mulAdd (odf[(std::size_t) i], odf[(std::size_t) (i - lag)], s);
        ac[(std::size_t) lag] = s / (double) (n - lag);
    }
    for (std::int64_t lag = minLag + 1; lag < maxLag; ++lag)
        if (ac[(std::size_t) lag] >= ac[(std::size_t) lag - 1] && ac[(std::size_t) lag] >= ac[(std::size_t) lag + 1])
        {
            const double bpm = (60 * odfSr) / (double) lag;
            out.peaks.push_back ({ lag, bpm, ac[(std::size_t) lag], ac[(std::size_t) lag] * weight (bpm, 120.0, 0.85) });
        }
    if (out.peaks.empty()) return out;
    const auto desc = [] (const Peak& a, const Peak& b) { return a.score > b.score; };
    const Peak* runnerUp = nullptr;
    if (! has)
    {
        std::stable_sort (out.peaks.begin(), out.peaks.end(), desc);
        for (const Peak& q : out.peaks) if (std::fabs (q.bpm - out.peaks[0].bpm) > 2) { runnerUp = &q; break; }
        if (runnerUp == nullptr && out.peaks.size() > 1) runnerUp = &out.peaks[1];
    }
    else
    {
        for (Peak& q : out.peaks) q.score = q.ac * weight (q.bpm, anchor, 0.35);
        std::stable_sort (out.peaks.begin(), out.peaks.end(), desc);
        if (out.peaks.size() > 1) runnerUp = &out.peaks[1];
    }
    const Peak best = out.peaks[0];
    const auto orZero = [] (double v) { return (std::isnan (v) || ! (v < 0 || v > 0)) ? 0.0 : v; };
    const double y0 = orZero (ac[(std::size_t) best.lag - 1]), y1 = orZero (ac[(std::size_t) best.lag]),
                 y2 = orZero (ac[(std::size_t) best.lag + 1]);
    const double denom = (y0 - 2 * y1) + y2;
    double refined = (double) best.lag;
    if (! (! std::isnan (denom) && ! (denom < 0) && ! (denom > 0)))
        refined = (double) best.lag + tempo::js::max (-1.0, tempo::js::min (1.0, (0.5 * (y0 - y2)) / denom));
    double acMean = 0; std::int64_t cnt = 0;
    for (std::int64_t lag = minLag; lag <= maxLag; ++lag) { acMean += ac[(std::size_t) lag]; ++cnt; }
    acMean = cnt ? acMean / (double) cnt : 0;
    const double margin = runnerUp ? (best.score - runnerUp->score) / best.score : 1;
    const double strength = acMean > 0 ? tempo::js::min (1.0, (best.ac / acMean - 1) / 4) : 0;
    const double blend = core::det::mul (0.6, tempo::js::max (0.0, margin)) + core::det::mul (0.4, strength);
    out.confidence = tempo::js::max (0.0, tempo::js::min (1.0, blend));
    out.bpm = fold ((60 * odfSr) / refined, anchor, has, p);
    out.refinedLag = refined;
    out.ok = true;
    return out;
}

double round1 (double v) { return tempo::js::round (v * 10) / 10; }
double round2 (double v) { return tempo::js::round (v * 100) / 100; }

tempo::TempoHeadline assemble (double bpm, const Win& a, const std::vector<double>& odf, double odfSr, const TempoParams& p)
{
    tempo::TempoHeadline h;
    h.determined = true;
    h.bpm = round1 (bpm);
    h.confidence = round2 (a.confidence);
    h.label = TempoDetector::labelFor (a.confidence);
    for (double c : { tempo::js::round (bpm / 2), tempo::js::round (bpm * 2) })
        if (c >= p.minBpm && c <= p.maxBpm) h.alts[h.altCount++] = c;
    h.beatPeriodSec = 60 / bpm;
    const std::int64_t per = (std::int64_t) tempo::js::max (1.0, tempo::js::round (a.refinedLag));
    std::int64_t bestOff = 0; double bestSum = -std::numeric_limits<double>::infinity();
    for (std::int64_t off = 0; off < per; ++off)
    {
        double s = 0;
        for (std::int64_t i = off; i < (std::int64_t) odf.size(); i += per) s += odf[(std::size_t) i];
        if (s > bestSum) { bestSum = s; bestOff = off; }
    }
    h.beatOffsetSec = (double) bestOff / odfSr;
    return h;
}

Report tempoCurve (const Plane& mono, double sr, const TempoParams& p)
{
    Report r;
    r.ok = true;
    const double odfSr = sr / 512;
    const std::vector<double> odf = detrend (onsetEnvelope (mono), odfSr);
    r.odf = odf;
    r.anchorBpm = kNaN; r.anchorConf = 0; r.anchorLag = kNaN; r.lo = kNaN; r.hi = kNaN;
    for (float x : mono) if (! std::isfinite (x)) ++r.nonFinite;
    const Win g = analyzeWindow (odf, odfSr, p, false, 0);
    if (! g.ok) return r;
    r.anchorBpm = g.bpm; r.anchorConf = g.confidence; r.anchorLag = g.refinedLag;
    for (std::size_t i = 0; i < g.peaks.size() && i < 5; ++i) r.cand.push_back ({ round1 (g.peaks[i].bpm), g.peaks[i].score });
    const double winFrames = std::max (8.0, tempo::js::round (p.winSec * odfSr));
    const double hopFrames = std::max (1.0, tempo::js::round (p.hopSec * odfSr));
    std::vector<tempo::TempoPoint> raw;
    for (double start = 0; start + winFrames <= (double) odf.size(); start += hopFrames)
    {
        const std::vector<double> sub (odf.begin() + (std::ptrdiff_t) start, odf.begin() + (std::ptrdiff_t) (start + winFrames));
        const Win w = analyzeWindow (sub, odfSr, p, true, g.bpm);
        tempo::TempoPoint q;
        q.t = round1 ((start + winFrames / 2) / odfSr);
        q.hasBpm = w.ok && w.confidence >= 0.15;
        q.bpm = q.hasBpm ? round1 (w.bpm) : kNaN;
        q.conf = w.ok ? round2 (w.confidence) : 0;
        raw.push_back (q);
    }
    std::vector<tempo::TempoPoint> curve;
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        tempo::TempoPoint q = raw[i];
        if (q.hasBpm)
        {
            std::vector<double> w;
            for (std::size_t j = (i >= 2 ? i - 2 : 0); j <= std::min (raw.size() - 1, i + 2); ++j) if (raw[j].hasBpm) w.push_back (raw[j].bpm);
            std::sort (w.begin(), w.end());
            q.bpm = w[w.size() >> 1];
        }
        curve.push_back (q);
    }
    std::vector<double> pts;
    for (const auto& q : curve) if (q.hasBpm && q.conf >= 0.2) pts.push_back (q.bpm);
    std::sort (pts.begin(), pts.end());
    double globalBpm = g.bpm;
    if (pts.size() >= 3)
    {
        globalBpm = pts[pts.size() / 2];
        const double lo = pts[(std::size_t) std::floor ((double) pts.size() * 0.1)], hi = pts[(std::size_t) std::floor ((double) pts.size() * 0.9)];
        r.hasRange = true; r.lo = round1 (lo); r.hi = round1 (hi);
        r.varies = (hi - lo) / globalBpm > 0.06 && (hi - lo) >= 5;
    }
    r.head = assemble (fold (globalBpm, g.bpm, true, p), g, odf, odfSr, p);
    r.head.bpm = round1 (globalBpm);
    r.whole = assemble (g.bpm, g, odf, odfSr, p);
    r.curve = curve;
    return r;
}
} // namespace ref

//==============================================================================
void clickTrainsReadTheirTempo()
{
    felitronics::test::group ("click trains at known tempi read their tempo, and offer exactly the in-range octaves");
    for (double sr : { 44100.0, 48000.0 })
        for (double bpm : { 90.0, 100.0, 120.0, 128.0, 140.0 })
        {
            const Report r = run (clickTrain (bpm, 12.0, sr), sr);
            const auto& w = r.whole;
            ok (r.ok && w.determined && std::fabs (w.bpm - bpm) <= 1.0,
                "a " + num (bpm) + " BPM train at " + num (sr) + " Hz reads " + num (w.bpm));
            ok (w.label == ConfidenceLabel::High || w.label == ConfidenceLabel::Medium,
                "... with a high or medium label (confidence " + num (w.confidence) + ")");
            // The alternatives ARE round(bpm/2) and round(bpm*2), each kept only inside [60, 180].
            int want = 0; double alts[2] {};
            for (double c : { tempo::js::round (w.bpm / 2.0), tempo::js::round (w.bpm * 2.0) })
                if (c >= 60.0 && c <= 180.0) alts[want++] = c;
            ok (w.altCount == want && (want < 1 || w.alts[0] == alts[0]) && (want < 2 || w.alts[1] == alts[1]),
                "... and its alternatives are the in-range half/double of " + num (w.bpm) + " (" + std::to_string (w.altCount) + ")");
            ok (w.beatPeriodSec > 0.0 && std::fabs (w.beatPeriodSec - 60.0 / w.bpm) <= 0.01 && w.beatOffsetSec >= 0.0,
                "... a beat period of 60/bpm and a beat offset");
        }
    // A range wide enough for both octaves: the half and the double are BOTH offered, half first.
    const Report wide = run (clickTrain (100.0, 12.0, 44100.0), 44100.0, TempoParams { 40.0, 300.0, 6.0, 1.5 });
    ok (wide.whole.altCount == 2 && wide.whole.alts[0] == tempo::js::round (wide.whole.bpm / 2.0)
            && wide.whole.alts[1] == tempo::js::round (wide.whole.bpm * 2.0),
        "with [40, 300] a 100 BPM train offers its half and its double (" + num (wide.whole.alts[0]) + ", "
        + num (wide.whole.alts[1]) + ")");
}

void aSteadyTrainIsAFlatCurve()
{
    felitronics::test::group ("tempoCurve: a steady train is a flat curve that does not vary");
    const Report r = run (clickTrain (120.0, 20.0, 44100.0), 44100.0);
    ok (r.curve.size() > 3, "the curve has " + std::to_string (r.curve.size()) + " points");
    bool flat = true, placed = true;
    for (std::size_t i = 0; i < r.curve.size(); ++i)
    {
        if (! r.curve[i].hasBpm || std::fabs (r.curve[i].bpm - 120.0) > 0.5) flat = false;
        if (i > 0 && ! (r.curve[i].t > r.curve[i - 1].t)) placed = false;
    }
    ok (flat, "every point reads 120 +/- 0.5");
    ok (placed, "and the points advance in time");
    ok (! r.varies && r.hasRange && r.hi - r.lo < 0.5, "varies is false and the range is " + num (r.lo) + ".." + num (r.hi));
    ok (std::fabs (r.head.bpm - 120.0) <= 0.5, "the headline (the curve's median) reads " + num (r.head.bpm));
}

void aTempoChangeShowsWhereItChanged()
{
    felitronics::test::group ("tempoCurve: a tempo that changes at 16 s shows in the curve, where it changed");
    const double sr = 44100.0, switchSec = 16.0;
    const Report r = run (concat (clickTrain (100.0, switchSec, sr), clickTrain (150.0, 16.0, sr)), sr);
    ok (r.varies, "varies is true");
    ok (r.hasRange && r.hi - r.lo >= 5.0, "the range spans " + num (r.lo) + ".." + num (r.hi));
    // A 6 s window centred at t covers [t-3, t+3]. Every point wholly before the change reads the 100 family, every
    // point wholly after it reads the 150 family — folded toward the whole-track anchor, so 150 may read as 75.
    const auto family = [] (double bpm, double base)
    {
        for (double k : { 0.25, 0.5, 1.0, 2.0, 4.0 }) if (std::fabs (bpm - base * k) <= 1.0) return true;
        return false;
    };
    int before = 0, after = 0, wrong = 0;
    double lastBefore = -1.0, firstAfter = -1.0;
    for (const auto& p : r.curve)
    {
        if (! p.hasBpm) continue;
        if (p.t + 3.0 <= switchSec) { ++before; if (! family (p.bpm, 100.0)) ++wrong; lastBefore = p.bpm; }
        if (p.t - 3.0 >= switchSec) { ++after;  if (! family (p.bpm, 150.0)) ++wrong; if (firstAfter < 0) firstAfter = p.bpm; }
    }
    ok (before >= 4 && after >= 4 && wrong == 0,
        std::to_string (before) + " points before the change read the 100 family and " + std::to_string (after)
        + " after it the 150 family (" + std::to_string (wrong) + " do not)");
    ok (std::fabs (lastBefore - firstAfter) >= 5.0, "and the curve moves across it: " + num (lastBefore) + " -> " + num (firstAfter));
}

void silenceAndAShortClipAreUndetermined()
{
    felitronics::test::group ("silence and a short clip are undetermined — no fake number");
    const auto undetermined = [] (const Report& r)
    {
        const auto isUndetermined = [] (const tempo::TempoHeadline& h)
        {
            return ! h.determined && std::isnan (h.bpm) && same (h.confidence, 0.0) && h.label == ConfidenceLabel::Undetermined
                && h.altCount == 0 && std::isnan (h.beatPeriodSec) && std::isnan (h.beatOffsetSec);
        };
        return r.ok && isUndetermined (r.head) && isUndetermined (r.whole) && r.cand.empty() && r.curve.empty()
            && ! r.varies && ! r.hasRange;
    };
    ok (undetermined (run (Plane (44100 * 4, 0.0f), 44100.0)), "4 s of digital silence");
    ok (undetermined (run (clickTrain (120.0, 1.0, 44100.0), 44100.0)), "a 1 s click train (< 1.5 s of onsets)");
    ok (undetermined (run (Plane (48000 * 10, 0.25f), 48000.0)), "10 s of a constant (no onset after detrend)");
    ok (undetermined (run (Plane {}, 48000.0)), "an empty programme");
    ok (undetermined (run (Plane (1023, 0.5f), 48000.0)), "a programme one sample short of a frame");
}

void noiseIsNeverConfidentlyWrong()
{
    felitronics::test::group ("white noise is never a confident answer (the spec's own fixture and bound)");
    const Plane noise = jsLcgNoise (44100 * 6);
    // Three samples as node printed them from the spec's generator: the reproduction is exact, not merely similar.
    ok ((double) noise[0] == -0.1137050986289978 && (double) noise[1] == -0.1742745041847229
            && (double) noise[100000] == 0.17666321992874146,
        "the fixture is the spec's own noise, sample for sample");
    const Report r = run (noise, 44100.0);
    // `r.ok` first: a run that failed is an empty Report, whose confidence of 0 would satisfy the bound.
    ok (r.ok && r.head.determined && r.head.confidence < 0.55, "noise confidence " + num (r.head.confidence) + " < 0.55");
}

void theRangeBinds()
{
    felitronics::test::group ("the BPM range binds the answer and the alternatives");
    const Report r = run (clickTrain (100.0, 12.0, 44100.0), 44100.0, TempoParams { 80.0, 160.0, 6.0, 1.5 });
    ok (r.whole.bpm >= 80.0 && r.whole.bpm <= 160.0, "bpm " + num (r.whole.bpm) + " in [80, 160]");
    bool inRange = true;
    for (int i = 0; i < r.whole.altCount; ++i) if (r.whole.alts[i] < 80.0 || r.whole.alts[i] > 160.0) inRange = false;
    ok (inRange, "and so is every alternative");
}

void aLoneClickHasATempoAndNoConfidence()
{
    felitronics::test::group ("the spec's own edge: a lone click has a tempo and a NaN confidence");
    const Report r = run (loneClick (44100.0), 44100.0);
    ok (r.head.determined && ! std::isnan (r.head.bpm), "a bpm is reported (" + num (r.head.bpm) + ")");
    ok (std::isnan (r.head.confidence) && r.head.label == ConfidenceLabel::Undetermined,
        "with a NaN confidence and the undetermined label — every lag scored 0, and the margin is 0/0");
    bool gaps = ! r.curve.empty();
    for (const auto& p : r.curve) if (p.hasBpm || ! std::isnan (p.conf)) gaps = false;
    ok (gaps, "every curve point is a gap whose confidence is NaN (" + std::to_string (r.curve.size()) + " points)");
}

//==============================================================================
// THE SPEC'S NUMBERS. Each literal was printed by tempo.js (node 26, V8 14.6) on the SAME float32 programme; the
// JavaScript's `null` is NaN here. The rounded fields are exact by construction. The two unrounded ones — the
// whole track's beat period, 60 / an unrounded tempo — are held to 1e-12 s: they carry the last bits of the
// onset curve, which differ from the page's by the Hann window's cos (see the header), and are equal today only
// because no rounding in between was close to a tie.
void theSpecsOwnNumbers()
{
    felitronics::test::group ("the spec's own numbers, from tempo.js in node, on transcendental-free fixtures");
    {
        const Report r = run (clickTrain (120.0, 12.0, 44100.0), 44100.0);
        const auto& w = r.whole;
        ok (w.bpm == 120.2 && w.confidence == 0.71 && w.label == ConfidenceLabel::High && w.altCount == 1 && w.alts[0] == 60.0,
            "detectTempo(click 120, 12 s, 44.1k): 120.2, 0.71, high, [60] — got " + num (w.bpm) + ", " + num (w.confidence));
        ok (std::fabs (w.beatPeriodSec - 0.4993681169536787) <= 1.0e-12 && w.beatOffsetSec == 0.0,
            "... beatPeriodSec 0.4993681169536787, beatOffsetSec 0");
        ok (r.cand.size() == 5 && r.cand[0].bpm == 120.2 && r.cand[1].bpm == 60.1 && r.cand[2].bpm == 178.2
                && r.cand[3].bpm == 172.3 && r.cand[4].bpm == 166.7 && r.cand[2].score == 0.0,
            "... candidates 120.2, 60.1, 178.2, 172.3, 166.7 (the last three at score 0)");
        ok (std::fabs (r.cand[0].score - 18216.668583366165) <= 1.0e-9 * 18216.668583366165,
            "... the first scoring " + num (r.cand[0].score) + " against the page's 18216.668583366165");
    }
    {
        const Report r = run (clickTrain (120.0, 20.0, 44100.0), 44100.0);
        ok (r.head.bpm == 120.1 && r.head.confidence == 0.71 && r.head.altCount == 1 && r.head.alts[0] == 60.0
                && ! r.varies && r.hasRange && r.lo == 120.1 && r.hi == 120.2,
            "tempoCurve(click 120, 20 s, 44.1k): 120.1, 0.71, [60], steady, range 120.1..120.2");
        ok (r.head.beatPeriodSec == 60.0 / 120.1 && r.head.beatOffsetSec == 0.0,
            "... beatPeriodSec 60/120.1 exactly (the folded median), beatOffsetSec 0");
        const double t[10] = { 3, 4.5, 6, 7.5, 9, 10.5, 12, 13.5, 15, 16.5 };
        const double b[10] = { 120.1, 120.1, 120.1, 120.1, 120.1, 120.1, 120.1, 120.1, 120.2, 120.2 };
        bool match = r.curve.size() == 10;
        for (std::size_t i = 0; match && i < 10; ++i)
            match = r.curve[i].hasBpm && r.curve[i].t == t[i] && r.curve[i].bpm == b[i] && r.curve[i].conf == 0.99;
        ok (match, "... and its ten points, t / bpm / conf, as the page prints them");
    }
    {
        const Report r = run (concat (clickTrain (100.0, 16.0, 44100.0), clickTrain (150.0, 16.0, 44100.0)), 44100.0);
        ok (r.head.bpm == 74.9 && r.head.confidence == 0.54 && r.head.altCount == 1 && r.head.alts[0] == 150.0
                && r.varies && r.lo == 74.9 && r.hi == 99.6,
            "tempoCurve(100 -> 150 at 16 s, 44.1k): 74.9, 0.54, [150], varies, range 74.9..99.6");
        const double b[18] = { 99.6, 99.6, 99.6, 99.6, 99.6, 99.6, 99.6, 99.6, 74.9, 74.9, 74.9, 74.9, 74.9, 74.9, 74.9, 74.9, 74.9, 74.9 };
        const double c[18] = { 1, 1, 1, 1, 1, 1, 1, 0.81, 0.63, 0.91, 0.94, 0.99, 1, 0.99, 0.99, 0.99, 0.99, 0.99 };
        bool match = r.curve.size() == 18;
        for (std::size_t i = 0; match && i < 18; ++i)
            match = r.curve[i].hasBpm && r.curve[i].t == 3.0 + 1.5 * (double) i && r.curve[i].bpm == b[i] && r.curve[i].conf == c[i];
        ok (match, "... and its eighteen points as the page prints them — 99.6 through 13.5 s, 74.9 from 15 s");
        ok (std::fabs (r.head.beatOffsetSec - 0.7546485260770975) <= 1.0e-15 && r.head.beatPeriodSec == 60.0 / 74.9,
            "... beatOffsetSec 0.7546485260770975, beatPeriodSec 60/74.9");
    }
    {
        // The spec's own noise: a dense, random onset curve, where every autocorrelation lag is a real number.
        const Report r = run (jsLcgNoise (44100 * 6), 44100.0);
        ok (r.head.bpm == 126.0 && r.head.confidence == 0.5 && r.head.label == ConfidenceLabel::High && r.head.altCount == 1
                && r.head.alts[0] == 63.0 && r.curve.empty() && ! r.hasRange,
            "tempoCurve(the spec's noise, 6 s): 126, 0.5, high, [63], no curve (the 6 s window does not fit 516 onsets)");
        ok (r.cand.size() == 5 && r.cand[0].bpm == 126.0 && r.cand[1].bpm == 89.1 && r.cand[2].bpm == 112.3
                && r.cand[3].bpm == 84.7 && r.cand[4].bpm == 79.5,
            "... candidates 126, 89.1, 112.3, 84.7, 79.5");
        ok (std::fabs (r.head.beatPeriodSec - 0.47617165909207304) <= 1.0e-12 && r.head.beatOffsetSec == 0.25541950113378686,
            "... beatPeriodSec 0.47617165909207304, beatOffsetSec 0.25541950113378686");
    }
    {
        const Report r = run (loneClick (48000.0), 48000.0);
        ok (r.head.bpm == 175.8 && std::isnan (r.head.confidence) && r.head.altCount == 1 && r.head.alts[0] == 88.0
                && r.curve.size() == 2 && ! r.curve[0].hasBpm && std::isnan (r.curve[0].conf) && r.curve[0].t == 3.0
                && r.curve[1].t == 4.5,
            "tempoCurve(a lone click, 48k): 175.8, confidence null, [88], two gaps at 3 and 4.5 s");
        ok (r.head.beatPeriodSec == 0.3413333333333333 && r.head.beatOffsetSec == 0.256,
            "... beatPeriodSec 0.3413333333333333, beatOffsetSec 0.256");
    }
}

//==============================================================================
void theReferenceNull()
{
    felitronics::test::group ("the streaming class against tempo.js translated line by line — bit for bit");
    struct Case { const char* name; Plane mono; double sr; TempoParams p; };
    const std::vector<Case> cases = {
        { "click 128, 20 s, 44.1k",           clickTrain (128.0, 20.0, 44100.0), 44100.0, {} },
        { "100 -> 150, 48k",                  concat (clickTrain (100.0, 16.0, 48000.0), clickTrain (150.0, 16.0, 48000.0)), 48000.0, {} },
        { "the spec's noise, 12 s, 44.1k",    jsLcgNoise (44100 * 12), 44100.0, {} },
        { "a lone click, 44.1k",              loneClick (44100.0), 44100.0, {} },
        { "silence, 3 s",                     Plane (48000 * 3, 0.0f), 48000.0, {} },
        { "click 90 at [40, 300], 4 s / 1 s", clickTrain (90.0, 15.0, 22050.0), 22050.0, { 40.0, 300.0, 4.0, 1.0 } },
        { "click 140 at [100, 200], 2 s / 0.37 s, 96k", clickTrain (140.0, 9.0, 96000.0), 96000.0, { 100.0, 200.0, 2.0, 0.37 } },
    };
    for (const Case& c : cases)
    {
        const Report a = run (c.mono, c.sr, c.p);
        const Report b = ref::tempoCurve (c.mono, c.sr, c.p);
        ok (sameReport (a, b, false), std::string (c.name) + ": the headlines, the curve, the candidates, the anchor and "
                                      "the detrended onset curve agree bit for bit (" + std::to_string (a.curve.size())
                                      + " points, " + std::to_string (a.odf.size()) + " onsets)");
    }
}

// The transform, against a direct DFT of a different construction. Not bit-for-bit — a DFT sums in another order —
// but it catches a normalisation, a bin off by one, the wrong window or a window applied twice, a frame taken at the
// wrong offset. NOT the sign of the exponent: the magnitude of a real frame's spectrum is the same under either sign,
// so no test of this detector can see it, and none needs to.
void theTransformIsTheDft()
{
    felitronics::test::group ("the onset flux from the FFT matches the same flux from a direct DFT");
    const double sr = 44100.0;
    Plane sig = clickTrain (131.0, 0.2, sr);                       // 8820 samples: 16 onset frames
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    for (auto& x : sig) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; x += (float) (((double) (s >> 11) / 9007199254740992.0 - 0.5) * 0.01); }
    TempoDetector d;
    ok (d.prepare (sr, 1, sig.size()), "prepared");
    const float* pl[1] = { sig.data() };
    ok (d.process (pl, 1, (int) sig.size()), "fed");
    // BEFORE finish(): the onset curve is still the raw spectral flux.
    std::vector<double> prev (513, 0.0);
    const std::vector<double> w = ref::hann (1024);
    double worst = 0.0;
    for (std::uint64_t f = 0; f < d.onsetFrames(); ++f)
    {
        std::vector<double> cur (513, 0.0);
        for (int k = 0; k < 513; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int n = 0; n < 1024; ++n)
            {
                const double x = (double) sig[(std::size_t) (f * 512 + (std::uint64_t) n)] * w[(std::size_t) n];
                const double a = -2.0 * core::kPi * (double) ((k * n) % 1024) / 1024.0;
                re += x * std::cos (a); im += x * std::sin (a);
            }
            cur[(std::size_t) k] = std::sqrt (re * re + im * im);
        }
        double flux = 0.0;
        if (f > 0) for (int k = 0; k < 513; ++k) { const double dd = cur[(std::size_t) k] - prev[(std::size_t) k]; if (dd > 0) flux += dd; }
        const double got = d.onsetCurve()[f];
        worst = std::max (worst, std::fabs (got - flux) / std::max (1.0, flux));
        prev = cur;
    }
    ok (d.onsetFrames() == 16 && worst < 1.0e-9, "16 frames, worst relative flux error " + num (worst));
    ok (same (d.onsetCurve()[0], 0.0), "frame 0 has no predecessor and reads 0");
}

void theMixIsThePagesToMono()
{
    felitronics::test::group ("the channels are mixed as the page's toMono mixes them — float32, channel by channel");
    const double sr = 48000.0;
    const Plane l = clickTrain (123.0, 8.0, sr);
    Plane r (l.size()), c (l.size());
    std::uint64_t s = 0x2545F4914F6CDD1Dull;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        r[i] = (float) ((double) (s >> 11) / 9007199254740992.0 * 0.6 - 0.3) + l[i] * 0.5f;
        c[i] = l[i] * -0.25f + (float) (i % 7) * 1.0e-3f;
    }
    Plane m2 (l.size()), m3 (l.size());
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        float a = 0.0f; a += l[i]; a += r[i]; m2[i] = a / 2.0f;                      // Float32Array `+=`, then `/=`
        float b = 0.0f; b += l[i]; b += r[i]; b += c[i]; m3[i] = b / 3.0f;
    }
    // Each side asserted to have RUN and determined a tempo: two failed runs are two empty Reports, and equal.
    const Report mono2 = run (m2, sr), mono3 = run (m3, sr);
    ok (mono2.ok && mono2.head.determined && mono3.ok && mono3.head.determined, "the mono references ran and read a tempo");
    ok (sameReport (runSplit ({ l, r }, sr, {}, { 4096 }), mono2), "stereo is the mono mix of its two planes, bit for bit");
    ok (sameReport (runSplit ({ l, r, c }, sr, {}, { 777 }), mono3), "three channels divide by three, in the same order");
}

void anySplitGivesTheSameBits()
{
    felitronics::test::group ("process() in any split gives the bits of one call");
    const double sr = 44100.0;
    const Plane x = concat (clickTrain (97.0, 10.0, sr), jsLcgNoise (44100 * 4));
    const Report whole = run (x, sr);
    ok (whole.ok && whole.head.determined && ! whole.curve.empty(), "the whole-call reference ran, read a tempo and a curve");
    for (const std::vector<int>& split : std::vector<std::vector<int>> { { 1 }, { 511 }, { 512 }, { 513 }, { 1023, 1, 2048 },
                                                                         { 7, 5000, 3 }, { 1024 } })
        ok (sameReport (runSplit ({ x }, sr, {}, split), whole), "split starting " + std::to_string (split[0]) + " matches the whole call");
}

// process() walks a call in stretches, each ending on the sample that completes a frame (the stretches are what keep
// the loops in functions called once per frame — TempoDetector.h, WHERE THE TIME IS SPENT). The contract it keeps:
// after ANY call, exactly the frames its samples completed have been transformed — none waits for the next call,
// none runs early — whether a call ends one sample short of a frame, on it, or one past it, and in two channels.
void aFrameIsTransformedOnTheSampleThatCompletesIt()
{
    felitronics::test::group ("a frame is transformed on the sample that completes it, whatever call brings that sample");
    const double sr = 44100.0;
    const Plane l = concat (clickTrain (97.0, 6.0, sr), jsLcgNoise (44100 * 2));
    Plane r (l.size());
    for (std::size_t i = 0; i < l.size(); ++i) r[i] = 0.5f * l[(i * 7u) % l.size()];
    const Report whole = runSplit ({ l, r }, sr, {}, { 1 << 30 });
    ok (whole.ok && whole.head.determined && whole.odf.size() > 600, "the whole-call reference ran and read a tempo");
    for (const std::vector<int>& split : std::vector<std::vector<int>> { { 1023, 1, 1 }, { 1024, 511, 1, 512 }, { 1025 },
                                                                         { 1535, 2, 510 }, { 3 }, { 4097 } })
    {
        TempoDetector d;
        const auto frames = (std::uint64_t) l.size();
        bool prepared = d.prepare (sr, 2, frames), counted = true;
        std::uint64_t at = 0; std::size_t k = 0;
        while (prepared && at < frames)
        {
            const int n = (int) std::min<std::uint64_t> ((std::uint64_t) split[k++ % split.size()], frames - at);
            const float* v[2] = { l.data() + at, r.data() + at };
            prepared = d.process (v, 2, n);
            at += (std::uint64_t) n;
            counted = counted && d.onsetFrames() == TempoDetector::onsetFramesFor (d.framesSeen());
        }
        ok (prepared && counted, "split starting " + std::to_string (split[0])
                                 + ": after every call, the frames are exactly those its samples completed");
        ok (prepared && d.finish() && sameReport (capture (d), whole), "... and the report is the whole call's, bit for bit");
    }
}

void thePrefixAndTheReset()
{
    felitronics::test::group ("finish() on a prefix is the analysis of that prefix; reset() starts the programme over");
    const double sr = 48000.0;
    const Plane x = clickTrain (111.0, 14.0, sr);
    const Plane head (x.begin(), x.begin() + (std::ptrdiff_t) (sr * 9.0));
    TempoDetector d;
    ok (d.prepare (sr, 1, x.size()), "prepared for the whole programme");
    const float* pl[1] = { head.data() };
    ok (d.process (pl, 1, (int) head.size()) && d.finish() && ! d.complete(), "fed nine seconds of fourteen, finished early");
    ok (sameReport (capture (d), run (head, sr)), "... and it reports exactly what a nine-second programme reports");
    d.reset();
    ok (d.framesSeen() == 0 && ! d.isFinished() && ! d.determined() && d.pointCount() == 0, "reset() forgets the programme and the report");
    const float* all[1] = { x.data() };
    ok (d.process (all, 1, (int) x.size()) && d.finish() && d.complete(), "the whole programme again");
    ok (sameReport (capture (d), run (x, sr)), "... and it reports what a fresh detector reports");
}

void stagedFinishMatchesSynchronous()
{
    felitronics::test::group ("staged finish preserves every tempo bit and bounds each return to the caller");
    using Clock = std::chrono::steady_clock;
    std::chrono::microseconds longest { 0 };
    for (const auto stereo : { false, true })
    {
        const double sr = 48000.0;
        Plane left = clickTrain (123.0, 25.0, sr);
        Plane right = left;
        for (auto& sample : right) sample *= 0.5f;
        const Planes planes = stereo ? Planes { left, right } : Planes { left };
        const auto reference = runSplit (planes, sr, {}, { 317, 1024, 777 });
        TempoDetector detector;
        ok (detector.prepare (sr, int (planes.size()), left.size()), "prepare staged detector");
        std::vector<const float*> input;
        for (const auto& plane : planes) input.push_back (plane.data());
        ok (detector.process (input.data(), int (input.size()), int (left.size())), "feed staged detector");
        unsigned calls = 0;
        while (! detector.isFinished())
        {
            const auto before = Clock::now();
            const bool done = detector.finishStep();
            longest = std::max (longest, std::chrono::duration_cast<std::chrono::microseconds> (Clock::now() - before));
            if (! done) ok (! detector.process (input.data(), int (input.size()), 0), "finishing freezes input between calls");
            ++calls;
        }
        ok (calls > 20 && detector.finishStage() == TempoDetector::FinishStage::Done,
            "long finish yields repeatedly and reaches its terminal stage");
        ok (sameReport (capture (detector), reference), "staged and synchronous reports, including ODF, match bit for bit");
    }
    std::printf ("tempo-finish-max-step-us=%lld\n", static_cast<long long> (longest.count()));
}

void theCallContract()
{
    felitronics::test::group ("law 11: the refusals, the exact width, the prepared length");
    const double sr = 48000.0;
    const Plane x (48000, 0.1f);
    const float* one[1] = { x.data() };
    const float* two[2] = { x.data(), x.data() };
    const float* nul[1] = { nullptr };
    TempoDetector d;
    ok (! d.process (one, 1, 100), "an unprepared detector refuses");
    ok (! d.finish(), "... and refuses to finish");
    ok (d.prepare (sr, 1, 48000u), "prepared: mono, one second");
    ok (! d.process (one, 1, -1) && ! d.process (one, -1, 10), "a negative length or width is refused");
    ok (! d.process (nullptr, 1, 10), "a null table is refused");
    ok (! d.process (nul, 1, 10), "a null plane is refused");
    ok (! d.process (two, 2, 10) && ! d.process (one, 0, 10), "a wider or narrower call is refused (the width is exact)");
    ok (d.process (one, 1, 0), "n == 0 is a legal no-op");
    ok (d.framesSeen() == 0, "... and every refusal above consumed nothing");
    ok (d.process (one, 1, 40000) && ! d.process (one, 1, 8001) && d.framesSeen() == 40000,
        "a call that would run past the prepared length is refused whole");
    ok (d.process (one, 1, 8000) && d.complete(), "... while one that ends on it is taken");
    ok (d.finish() && d.finish(), "finish() is idempotent");
    ok (! d.process (one, 1, 0), "a finished detector refuses until reset()");

    felitronics::test::group ("law 11b: prepare() refuses what it cannot honour, disarmed — the report included");
    const Plane click = clickTrain (120.0, 12.0, sr);
    const float* pc[1] = { click.data() };
    ok (d.prepare (sr, 1, click.size()) && d.process (pc, 1, (int) click.size()) && d.finish() && d.determined(),
        "a finished, determined analysis");
    const double inf = std::numeric_limits<double>::infinity();
    for (double bad : { 0.0, -48000.0, 7999.0, 44.1, 768001.0, inf, kNaN })
    {
        const bool refused = ! d.prepare (bad, 1, 1000u);
        // Law 11b's disarm is WHOLE: not only the flags and the report, but the geometry and the onset curve of the
        // programme before (a review round found the rate, the length, the params and the curve pointer standing).
        ok (refused && ! d.isPrepared() && ! d.determined() && d.pointCount() == 0 && d.candidateCount() == 0
                && d.sampleRate() == 0.0 && d.odfSampleRate() == 0.0 && d.totalFrames() == 0 && d.framesSeen() == 0
                && d.onsetFrames() == 0 && d.onsetCurve() == nullptr && d.windowFrames() == 0.0 && d.channels() == 0
                && d.params().minBpm == 60.0 && std::isnan (d.anchorBpm()),
            "a rate of " + num (bad) + " is refused and leaves nothing prepared or readable");
        ok (d.prepare (sr, 1, click.size()) && d.process (pc, 1, (int) click.size()) && d.finish() && d.determined(),
            "... and the detector is re-armed with a finished analysis for the next refusal");
    }
    ok (! d.prepare (sr, 0, 1000u) && ! d.prepare (sr, core::kMaxChannels + 1, 1000u), "a width of 0 or 17 is refused");
    ok (! d.prepare (sr, 1, TempoDetector::kMaxFrames + 1u) && TempoDetector::storageFor (sr, 1, TempoDetector::kMaxFrames, {}).ok,
        "a length past the ceiling is refused, the ceiling itself is not");
    const TempoParams bad[] = { { 0.5, 180, 6, 1.5 }, { 60, 1001, 6, 1.5 }, { 120, 90, 6, 1.5 }, { kNaN, 180, 6, 1.5 },
                                { 60, 180, 0, 1.5 }, { 60, 180, inf, 1.5 }, { 60, 180, 6, -1 }, { 60, 180, 6, kNaN } };
    for (const TempoParams& p : bad)
    {
        TempoDetector e;
        e.setParams (p);
        ok (! e.prepare (sr, 1, 1000u) && ! TempoDetector::storageFor (sr, 1, 1000u, p).ok,
            "params { " + num (p.minBpm) + ", " + num (p.maxBpm) + ", " + num (p.winSec) + ", " + num (p.hopSec)
            + " } are refused by prepare() and by storageFor() alike");
    }
    TempoDetector e;
    e.setParams ({ 90.0, 90.0, 6.0, 1.5 });
    ok (e.prepare (sr, 1, click.size()) && e.process (pc, 1, (int) click.size()) && e.finish() && ! e.determined(),
        "a one-point range (min == max) is taken, and is undetermined: no lag lies strictly inside it");
}

void theDemandIsTheAllocation()
{
    felitronics::test::group ("law 11d: prepare() asks for exactly storageFor().bytes(); process() and finish() ask for nothing");
    struct Geo { double sr; int ch; std::uint64_t frames; TempoParams p; };
    const Geo geos[] = { { 48000.0, 2, 48000u * 30u, {} }, { 44100.0, 1, 1000u, {} }, { 8000.0, 16, 8000u * 20u, { 40, 300, 4, 1 } },
                         { 768000.0, 1, 768000u * 3u, {} }, { 96000.0, 1, 0u, {} } };
    for (const Geo& g : geos)
    {
        const auto st = TempoDetector::storageFor (g.sr, g.ch, g.frames, g.p);
        TempoDetector d;
        d.setParams (g.p);
        const long long before = felitronics::test::alloc::bytes.load();
        const bool prepared = d.prepare (g.sr, g.ch, g.frames);
        const long long spent = felitronics::test::alloc::bytes.load() - before;
        ok (prepared && st.ok && (unsigned long long) spent == st.bytes(),
            num (g.sr) + " Hz x " + std::to_string (g.ch) + ", " + std::to_string (g.frames) + " frames: prepare() asked for "
            + std::to_string (spent) + " bytes against a published " + std::to_string (st.bytes()));
        Planes p ((std::size_t) g.ch, clickTrain (125.0, (double) g.frames / g.sr, g.sr));
        std::vector<const float*> v; for (auto& c : p) v.push_back (c.data());
        // Every count is read into a local BEFORE a message is built: the message is a std::string, and the order in
        // which a call's arguments are evaluated is unspecified, so a string built in the same call can land inside
        // the measured window.
        const long long calls = felitronics::test::alloc::count.load();
        const bool fed = g.frames == 0 || d.process (v.data(), g.ch, (int) g.frames);
        const bool done = d.finish();
        const bool quiet = felitronics::test::alloc::count.load() == calls;
        felitronics::test::okNoAlloc (fed && done && quiet, "... and process() + finish() allocated nothing");
        const long long again = felitronics::test::alloc::count.load();
        const bool re = d.prepare (g.sr, g.ch, g.frames);
        const bool reQuiet = felitronics::test::alloc::count.load() == again;
        ok (re, "re-prepared at the same geometry");
        felitronics::test::okNoAlloc (reQuiet, "... without a new allocation");
        if (d.isPrepared() && g.frames > 0)
        {
            // The published counts are the ones the programme actually uses.
            ok (d.process (v.data(), g.ch, (int) g.frames) && d.finish() && d.onsetFrames() == st.odfFrames
                    && (! d.determined() || (std::uint64_t) d.pointCount() == st.points),
                "... and the programme fills exactly " + std::to_string (st.odfFrames) + " onset frames and, when determined, "
                + std::to_string (st.points) + " points");
        }
    }
    ok (TempoDetector::onsetFramesFor (0) == 0 && TempoDetector::onsetFramesFor (1023) == 0 && TempoDetector::onsetFramesFor (1024) == 1
            && TempoDetector::onsetFramesFor (1535) == 1 && TempoDetector::onsetFramesFor (1536) == 2,
        "onset frames: max(0, 1 + floor((n - 1024) / 512)), at its edges");
    const auto a = TempoDetector::storageFor (48000.0, 2, 48000u * 60u, {}), b = TempoDetector::storageFor (48000.0, 2, 48000u * 600u, {});
    ok (a.ok && b.ok && b.bytes() > a.bytes() && b.odfFrames > a.odfFrames && b.points > a.points,
        "ten times the programme costs more (" + std::to_string (a.bytes()) + " -> " + std::to_string (b.bytes()) + " bytes)");
}

// The two rules no programme fixture reaches the edges of — a fold landing EXACTLY on the range bound, a confidence
// exactly on a label threshold. Each expected value was printed by the spec's own octaveFold / labelFor in node.
void theRulesAtTheirEdges()
{
    felitronics::test::group ("octaveFold and labelFor at their edges, as the spec answers");
    const double nan = kNaN;
    struct F { double bpm, anchor, lo, hi, want; const char* what; };
    const F f[] = { { 90.0, 200.0, 60.0, 180.0, 180.0, "a double that lands exactly on the ceiling is taken" },
                    { 90.5, 200.0, 60.0, 180.0, 90.5,  "a double past the ceiling is not" },
                    { 120.0, 40.0, 60.0, 180.0, 60.0,  "a half that lands exactly on the floor is taken" },
                    { 119.0, 40.0, 60.0, 180.0, 119.0, "a half under the floor is not" },
                    { 50.0, 120.0, 20.0, 400.0, 100.0, "doubling stops once over anchor/sqrt2" },
                    { 300.0, 100.0, 60.0, 180.0, 75.0, "halving stops once under anchor*sqrt2" },
                    { 120.0, 0.0, 60.0, 180.0, 120.0,  "an anchor of 0 leaves the tempo alone (!anchor)" },
                    { 120.0, nan, 60.0, 180.0, 120.0,  "and so does a NaN anchor" } };
    for (const F& t : f)
        ok (TempoDetector::foldToward (t.bpm, t.anchor, t.lo, t.hi) == t.want,
            std::string ("fold(") + num (t.bpm) + " toward " + num (t.anchor) + ") = " + num (t.want) + ": " + t.what);
    struct L { double c; ConfidenceLabel want; };
    const L l[] = { { 0.5, ConfidenceLabel::High }, { 0.49999999999999994, ConfidenceLabel::Medium },
                    { 0.28, ConfidenceLabel::Medium }, { 0.27999999999999997, ConfidenceLabel::Low },
                    { 5e-324, ConfidenceLabel::Low }, { 0.0, ConfidenceLabel::Undetermined }, { -0.0, ConfidenceLabel::Undetermined },
                    { nan, ConfidenceLabel::Undetermined }, { 1.0, ConfidenceLabel::High } };
    for (const L& t : l)
        ok (TempoDetector::labelFor (t.c) == t.want, "labelFor(" + num (t.c) + ") is label " + std::to_string ((int) t.want));
}

void aNonFiniteSampleIsCountedAndSurvived()
{
    felitronics::test::group ("a non-finite sample goes through the spec's arithmetic, and is counted");
    const double sr = 44100.0;
    Plane x = clickTrain (120.0, 12.0, sr);
    x[5000] = std::numeric_limits<float>::quiet_NaN();
    x[5001] = std::numeric_limits<float>::quiet_NaN();
    const Report r = run (x, sr);
    ok (r.ok && r.nonFinite == 2, "two NaN samples are counted (" + std::to_string (r.nonFinite) + ")");
    bool finite = true;
    for (double v : r.odf) if (! std::isfinite (v)) finite = false;
    ok (finite && r.whole.determined && std::fabs (r.whole.bpm - 120.0) <= 1.0,
        "a NaN bin never counts as flux, so the onset curve stays finite and the tempo still reads " + num (r.whole.bpm));
    ok (sameReport (r, ref::tempoCurve (x, sr, {}), false), "... exactly as the line-by-line translation of the spec says");
}
} // namespace

int main()
{
    std::printf ("felitronics tempo::TempoDetector tests\n");
    clickTrainsReadTheirTempo();
    aSteadyTrainIsAFlatCurve();
    aTempoChangeShowsWhereItChanged();
    silenceAndAShortClipAreUndetermined();
    noiseIsNeverConfidentlyWrong();
    theRangeBinds();
    aLoneClickHasATempoAndNoConfidence();
    theSpecsOwnNumbers();
    theReferenceNull();
    theTransformIsTheDft();
    theMixIsThePagesToMono();
    anySplitGivesTheSameBits();
    aFrameIsTransformedOnTheSampleThatCompletesIt();
    thePrefixAndTheReset();
    stagedFinishMatchesSynchronous();
    theCallContract();
    theDemandIsTheAllocation();
    theRulesAtTheirEdges();
    aNonFiniteSampleIsCountedAndSurvived();
    return felitronics::test::report();
}
