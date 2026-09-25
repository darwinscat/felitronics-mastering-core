// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE SPLIT-INVARIANCE HARNESS — test support, shared by the two suites that measure one question on both modules:
// does a published output depend on where the caller cut the stream into `process()` calls?
//   modules/analysis_offline/tests/SplitInvarianceTests.cpp — every offline analyzer
//   modules/mastering/tests/SplitInvarianceTests.cpp        — the chain, the renderer and the solver's meters
// One harness so that "invariant" means the same cuts, the same comparison and the same report on every row.
//
//   · Programme — planes + the caller's TIMELINE (`segs`: each stretch carries the width it is handed over at —
//     full, narrower, or 0, law 11d's clock-only call) + the length a class that must be told its total is
//     prepared for.
//   · cuts() — one call per segment, fixed blocks on a global grid, or seeded lengths 1..8192 with zero-length
//     calls interleaved (always one at the head and one at the tail). Calls never straddle a segment: a width
//     change is an event on the caller's own timeline, and law 8a promises nothing about those.
//   · Bits — every published output as words, each labelled, compared as BITS (std::bit_cast), never with a
//     tolerance; compare() names the first differing output, how many words moved and by how much.
//   · suite<A>() — per adapter: every cut against one call, a second maxBlock, four TIMELINES where the contract
//     has them (a gap in the middle, a gap at the head, a finish inside a gap, a narrower stretch), finish on a
//     prefix, the same prefix read BEFORE finish (what a caller reporting progress mid-stream reads), and the
//     CONTROL: Nudged<A> moves the last sample of every call by one ulp, and the comparison must see it.
//   · printTable() — per class: INVARIANT only when every row is bit-identical AND the fixture was exercised AND the
//     control was caught; NOT when a row differs; UNPROVEN when a row agreed but the witness or the control failed.
//
// An adapter provides: kName, kClockOnly (a zero-width call is legal for it), kNarrow (so is a call narrower than the
// programme), kMaxBlock (it takes a maxBlock worth varying), prepare (p, maxBlock), process (planes, nch, n),
// finish(), serialise (Bits&), witness (std::string&) —
// the last says what the reference run actually exercised, because a fixture on which the class measured nothing
// compares bit-identically under every cut and proves nothing.
//
// Everything random is SplitMix64, and every fixture is shaped with core::det, so the programme and the cuts are the
// same bits on every row and a divergence measured here is a number another platform can reproduce.

#include <felitronics/core/DetMath.h>
#include <felitronics/core/Math.h>
#include <felitronics_test.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace felitronics::test::split
{

namespace det = felitronics::core::det;

//==============================================================================
// SplitMix64 — one generator for fixtures and cuts, identical on every standard library (std::uniform_*_distribution
// is not: libstdc++, libc++ and MSVC's STL draw different numbers from the same engine state).
struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed) {}
    std::uint64_t next()
    {
        std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    double uniform() { return (double) (next() >> 11) * 0x1.0p-53; }                // [0, 1)
    double bipolar() { return 2.0 * uniform() - 1.0; }                              // [-1, 1)
    std::int64_t below (std::int64_t n) { return (std::int64_t) (next() % (std::uint64_t) n); }
};

//==============================================================================
// Everything a run published, as words, each with the name of the output it came from.
struct Bits
{
    enum Kind : std::uint8_t { Int = 0, F64 = 1, F32 = 2 };
    std::vector<std::uint64_t> w;
    std::vector<const char*>   label;
    std::vector<std::uint8_t>  kind;
    void i (const char* l, std::int64_t v) { w.push_back ((std::uint64_t) v); label.push_back (l); kind.push_back (Int); }
    void b (const char* l, bool v) { i (l, v ? 1 : 0); }
    void d (const char* l, double v) { w.push_back (std::bit_cast<std::uint64_t> (v)); label.push_back (l); kind.push_back (F64); }
    void f (const char* l, float v) { w.push_back ((std::uint64_t) std::bit_cast<std::uint32_t> (v)); label.push_back (l); kind.push_back (F32); }
};

struct Diff
{
    bool        refused = false;
    std::size_t sizeRef = 0, sizeGot = 0, differing = 0;
    std::string firstLabel;
    std::size_t firstOccurrence = 0;
    double      maxAbs = 0.0, maxRel = 0.0;
    std::string maxLabel, labels;
    bool same() const { return ! refused && sizeRef == sizeGot && differing == 0; }
    std::string describe() const
    {
        if (refused) return "a call or the prepare was REFUSED";
        if (same()) return "bit-identical";
        char buf[512];
        std::snprintf (buf, sizeof buf, "%zu of %zu words differ (sizes %zu / %zu); first: %s #%zu; max |d| %.6g (rel %.3g) on %s; outputs: %s",
                       differing, sizeRef, sizeRef, sizeGot, firstLabel.c_str(), firstOccurrence, maxAbs, maxRel,
                       maxLabel.empty() ? "-" : maxLabel.c_str(), labels.c_str());
        return buf;
    }
};

inline double asDouble (std::uint64_t w, std::uint8_t k)
{
    if (k == Bits::F64) return std::bit_cast<double> (w);
    if (k == Bits::F32) return (double) std::bit_cast<float> ((std::uint32_t) w);
    return (double) (std::int64_t) w;
}

inline Diff compare (const Bits& ref, const Bits& got, bool refused)
{
    Diff d;
    d.refused = refused;
    d.sizeRef = ref.w.size();
    d.sizeGot = got.w.size();
    const std::size_t n = std::min (d.sizeRef, d.sizeGot);
    std::vector<std::string> names;
    for (std::size_t k = 0; k < n; ++k)
    {
        // The SCHEMA is compared as well as the value: the same word under another output's name is a difference.
        if (ref.w[k] == got.w[k] && ref.kind[k] == got.kind[k] && std::strcmp (ref.label[k], got.label[k]) == 0) continue;
        const std::string lab = ref.label[k];
        if (d.differing++ == 0)
        {
            d.firstLabel = lab;
            for (std::size_t j = 0; j < k; ++j) if (std::strcmp (ref.label[j], ref.label[k]) == 0) ++d.firstOccurrence;
        }
        if (std::find (names.begin(), names.end(), lab) == names.end()) names.push_back (lab);
        const double a = asDouble (ref.w[k], ref.kind[k]), b = asDouble (got.w[k], ref.kind[k]);
        const double ad = std::fabs (a - b);
        if (std::isfinite (ad) && ad > d.maxAbs)
        {
            d.maxAbs = ad;
            d.maxRel = std::fabs (a) > 0.0 ? ad / std::fabs (a) : ad;
            d.maxLabel = lab;
        }
    }
    for (std::size_t k = 0; k < names.size() && k < 8; ++k) d.labels += (k ? ", " : "") + names[k];
    if (names.size() > 8) d.labels += ", … (" + std::to_string (names.size()) + " outputs)";
    return d;
}

//==============================================================================
// THE PROGRAMME. `segs` is the caller's timeline: each stretch is handed over at its `width` — -1 is the programme's
// full width, 0 a gap (clock-only calls). `declared` is the length a class that must be told its total
// (WaveformPeaks, StereoColumns, BandCrest's capacity) is prepared for; a prefix keeps the full programme's.
struct Segment { std::int64_t begin = 0, end = 0; int width = -1; };

struct Programme
{
    double fs = 48000.0;
    std::vector<std::vector<float>> x;
    std::vector<Segment> segs;
    std::int64_t declared = 0;

    std::int64_t len() const { return x.empty() ? 0 : (std::int64_t) x[0].size(); }
    int channels() const { return (int) x.size(); }

    Programme prefix (std::int64_t n) const
    {
        Programme p;
        p.fs = fs;
        p.declared = declared;
        for (const auto& c : x) p.x.emplace_back (c.begin(), c.begin() + n);
        for (Segment s : segs)
            if (s.begin < n) { s.end = std::min (s.end, n); p.segs.push_back (s); }
        return p;
    }
    Programme withSegments (std::vector<Segment> s) const
    {
        Programme p = *this;
        p.segs = std::move (s);
        return p;
    }
};

struct Gen
{
    Programme p;
    Rng rng;
    Gen (double fs, int nch, std::int64_t n, std::uint64_t seed) : rng (seed)
    {
        p.fs = fs;
        p.x.assign ((std::size_t) nch, std::vector<float> ((std::size_t) n, 0.0f));
        p.segs = { { 0, n, -1 } };
        p.declared = n;
    }
    std::int64_t len() const { return p.len(); }
    float& at (int c, std::int64_t i) { return p.x[(std::size_t) c][(std::size_t) i]; }

    void noise (int c, std::int64_t from, std::int64_t to, double amp)
    {
        for (std::int64_t i = from; i < std::min (to, len()); ++i) at (c, i) += (float) (amp * rng.bipolar());
    }
    void tone (int c, std::int64_t from, std::int64_t to, double hz, double amp, double phase = 0.0)
    {
        const double w = 2.0 * core::kPi * hz / p.fs;
        for (std::int64_t i = from; i < std::min (to, len()); ++i)
            at (c, i) += (float) (amp * det::sin (w * (double) (i - from) + phase));
    }
    // Tone bursts with a raised-cosine edge of `edge` samples, `on` of every `period`, starting at `from`.
    void bursts (int c, std::int64_t from, std::int64_t to, std::int64_t period, std::int64_t on, std::int64_t edge,
                 double hz, double amp, double phase = 0.0)
    {
        const double w = 2.0 * core::kPi * hz / p.fs;
        for (std::int64_t i = from; i < std::min (to, len()); ++i)
        {
            const std::int64_t k = (i - from) % period;
            if (k >= on) continue;
            double g = 1.0;
            if (k < edge) g = 0.5 - 0.5 * det::cos (core::kPi * (double) k / (double) edge);
            else if (on - k <= edge) g = 0.5 - 0.5 * det::cos (core::kPi * (double) (on - k) / (double) edge);
            at (c, i) += (float) (g * amp * det::sin (w * (double) (i - from) + phase));
        }
    }
    // A decaying click: an exponential of `ms` on a tone of `hz` — a kick when hz is low, a tick when high.
    void hit (int c, std::int64_t pos, double amp, double hz, double ms)
    {
        const double k = det::pow10 (-1.0 / (ms * 1e-3 * p.fs) / 2.302585092994046);   // e^(-1/(ms·fs))
        const double w = 2.0 * core::kPi * hz / p.fs;
        double g = amp;
        for (std::int64_t i = pos; i < len() && g > 1e-6; ++i, g *= k)
            at (c, i) += (float) (g * det::sin (w * (double) (i - pos)));
    }
    void scale (int c, std::int64_t from, std::int64_t to, double g)
    {
        for (std::int64_t i = from; i < std::min (to, len()); ++i) at (c, i) = (float) ((double) at (c, i) * g);
    }
    void clamp (int c, std::int64_t from, std::int64_t to, float lo, float hi)
    {
        for (std::int64_t i = from; i < std::min (to, len()); ++i) at (c, i) = std::min (hi, std::max (lo, at (c, i)));
    }
    void zero (int c, std::int64_t from, std::int64_t to)
    {
        for (std::int64_t i = from; i < std::min (to, len()); ++i) at (c, i) = 0.0f;
    }
    void fade (int c, std::int64_t from, std::int64_t to, bool in)
    {
        const double span = (double) (to - from);
        for (std::int64_t i = from; i < std::min (to, len()); ++i)
        {
            const double t = (double) (i - from) / span;
            at (c, i) = (float) ((double) at (c, i) * (in ? t : 1.0 - t));
        }
    }
};

//==============================================================================
// THE CUTS.
struct Call { int nch = 0; std::int64_t at = 0; int n = 0; };

struct Policy
{
    const char*   name = "";
    int           fixed = 0;         // > 0: blocks of this many samples on a GLOBAL grid, also cut at the segments
    std::uint64_t seed = 0;          // fixed == 0 and seed != 0: lengths 1..8192 drawn from this seed
};
inline const Policy kWhole  { "one call",        0, 0 };
inline const Policy kFix64  { "blocks of 64",   64, 0 };
inline const Policy kFix480 { "blocks of 480", 480, 0 };
inline const Policy kFix4k  { "blocks of 4096", 4096, 0 };

inline std::vector<Call> cuts (const Programme& p, const Policy& pol, bool zeroWidthZeroLength)
{
    std::vector<Call> calls;
    const int full = p.channels();
    Rng r (pol.seed * 0x2545f4914f6cdd1dull + 7u);
    const bool random = pol.fixed == 0 && pol.seed != 0;
    if (random) calls.push_back ({ full, 0, 0 });                       // a zero-length call at the head, always
    for (const Segment& s : p.segs)
    {
        const int width = s.width < 0 ? full : s.width;
        for (std::int64_t at = s.begin; at < s.end; )
        {
            std::int64_t n = s.end - at;
            if (pol.fixed > 0) n = std::min (n, (at / pol.fixed + 1) * pol.fixed - at);
            else if (random)
            {
                if (r.below (6) == 0) calls.push_back ({ width, at, 0 });
                if (zeroWidthZeroLength && r.below (12) == 0) calls.push_back ({ 0, at, 0 });
                n = std::min (n, 1 + r.below (8192));
            }
            calls.push_back ({ width, at, (int) n });
            at += n;
        }
    }
    if (random) calls.push_back ({ full, p.len(), 0 });                 // …and one at the tail
    return calls;
}

inline std::string callStats (const std::vector<Call>& calls)
{
    std::size_t zero = 0, clock = 0, real = 0;
    for (const Call& c : calls) { if (c.n == 0) ++zero; else if (c.nch == 0) ++clock; else ++real; }
    return std::to_string (real) + " calls, " + std::to_string (clock) + " clock-only, " + std::to_string (zero) + " zero-length";
}

//==============================================================================
// THE HARNESS. An adapter owns one analyzer (or a pair, where the class's output IS a pair) and knows:
//   kName, kClockOnly (a zero-width call is legal for it), kMaxBlock (it takes a maxBlock worth varying),
//   prepare (p, maxBlock), process (planes, nch, n), finish(), serialise (Bits&).
// `witness`, when given, receives the adapter's account of what the run actually exercised — a fixture on which the
// analyzer measured nothing compares bit-identically under every cut and proves nothing.
// `finishIt == false` reads the object where the last call left it — what a caller reporting progress mid-stream sees.
template <class A>
Bits runOne (const Programme& p, const std::vector<Call>& calls, int maxBlock, bool& refused, std::string* witness = nullptr,
             bool* exercised = nullptr, bool finishIt = true)
{
    A a;
    Bits out;
    refused = false;
    if (! a.prepare (p, maxBlock)) { refused = true; return out; }
    std::vector<const float*> planes ((std::size_t) p.channels());
    for (const Call& c : calls)
    {
        for (int ch = 0; ch < p.channels(); ++ch) planes[(std::size_t) ch] = p.x[(std::size_t) ch].data() + c.at;
        if (! a.process (planes.data(), c.nch, c.n)) { refused = true; return out; }
    }
    if (finishIt) a.finish();
    a.serialise (out);
    if (witness != nullptr && exercised != nullptr) *exercised = a.witness (*witness);
    return out;
}

// THE CONTROL. The same adapter with a planted call-boundary defect: the LAST sample of every call is moved by one
// ulp before the analyzer sees it — the smallest change a per-call flush, a per-call rounding or a per-call reset
// could make. A harness that cannot see this at a ragged cut cannot see a real defect either; it is the mutant every
// row below is implicitly measured against.
template <class A>
struct Nudged : A
{
    std::vector<std::vector<float>> scratch;
    bool process (const float* const* in, int nch, int n)
    {
        if (nch <= 0 || n <= 0) return A::process (in, nch, n);
        scratch.resize ((std::size_t) nch);
        std::vector<const float*> p ((std::size_t) nch);
        for (int c = 0; c < nch; ++c)
        {
            scratch[(std::size_t) c].assign (in[c], in[c] + n);
            float& v = scratch[(std::size_t) c][(std::size_t) (n - 1)];
            v = std::nextafter (v, v >= 0.0f ? 2.0f : -2.0f);
            p[(std::size_t) c] = scratch[(std::size_t) c].data();
        }
        return A::process (p.data(), nch, n);
    }
};

struct Row { std::string analyzer, what; Diff d; };
inline std::vector<Row>& table() { static std::vector<Row> t; return t; }
// What keeps a class from INVARIANT although no row differed: a fixture that exercised nothing, a control not caught.
inline std::vector<std::pair<std::string, std::string>>& caveats() { static std::vector<std::pair<std::string, std::string>> c; return c; }

inline void record (const char* analyzer, const std::string& what, const Diff& d, std::size_t words)
{
    table().push_back ({ analyzer, what, d });
    felitronics::test::ok (d.same(), std::string (analyzer) + " — " + what + ": " + d.describe()
                                     + " (" + std::to_string (words) + " words)");
}

template <class A>
void suite (const Programme& full)
{
    felitronics::test::group (std::string ("split invariance — ") + A::kName);
    const int mb = 4096;
    const bool zw = A::kClockOnly;

    // 1. The full-width programme, every cut against one call.
    bool refused = false, exercised = false;
    std::string facts;
    const Bits ref = runOne<A> (full, cuts (full, kWhole, zw), mb, refused, &facts, &exercised);
    felitronics::test::ok (! refused && ref.w.size() > 64, std::string (A::kName) + ": the reference run is accepted and "
                           "publishes something (" + std::to_string (ref.w.size()) + " words)");
    felitronics::test::ok (exercised, std::string (A::kName) + ": the fixture exercises what the analyzer measures — " + facts);
    if (! exercised) caveats().push_back ({ A::kName, "the fixture did not exercise it: " + facts });
    std::printf ("    %s: %s\n", A::kName, facts.c_str());
    const Policy rows[] = { kFix64, kFix480, kFix4k, { "ragged 1..8192 #1", 0, 1 }, { "ragged 1..8192 #2", 0, 2 },
                            { "ragged 1..8192 #3", 0, 3 } };
    for (const Policy& pol : rows)
    {
        const auto calls = cuts (full, pol, zw);
        bool r = false;
        const Bits got = runOne<A> (full, calls, mb, r);
        record (A::kName, std::string (pol.name) + " [" + callStats (calls) + "]", compare (ref, got, r), ref.w.size());
    }
    {
        const auto calls = cuts (full, rows[3], zw);
        const bool hasZero = std::any_of (calls.begin(), calls.end(), [] (const Call& c) { return c.n == 0; });
        const bool hasBig  = std::any_of (calls.begin(), calls.end(), [] (const Call& c) { return c.n > 4096; });
        felitronics::test::ok (hasZero && hasBig && calls.size() > 20, std::string (A::kName) + ": the ragged cut carries "
                               "zero-length calls and calls past 4096 [" + callStats (calls) + "]");
    }

    // 1b. THE CONTROL: the planted one-ulp call-boundary defect must be SEEN at a ragged cut.
    {
        bool r1 = false, r2 = false;
        const Bits a = runOne<Nudged<A>> (full, cuts (full, kWhole, zw), mb, r1);
        const Bits b = runOne<Nudged<A>> (full, cuts (full, rows[3], zw), mb, r2);
        const Diff d = compare (a, b, r1 || r2);
        felitronics::test::ok (! d.refused && ! d.same(), std::string (A::kName) + ": CONTROL — a one-ulp nudge at every "
                               "call boundary is caught by this comparison (" + d.describe() + ")");
        if (d.refused || d.same()) caveats().push_back ({ A::kName, "the planted control was NOT caught" });
        std::printf ("    %s: CONTROL — %s\n", A::kName, d.describe().c_str());
    }

    // 2. A different prepared maxBlock, cut raggedly.
    if (A::kMaxBlock)
        for (int other : { 64, 1 << 16 })
        {
            const auto calls = cuts (full, { "ragged", 0, 4 }, zw);
            bool r = false;
            const Bits got = runOne<A> (full, calls, other, r);
            record (A::kName, "prepared maxBlock " + std::to_string (other) + " (reference 4096), ragged", compare (ref, got, r),
                    ref.w.size());
        }

    // 3. TIMELINES — width changes on the caller's own clock, each cut several ways, where the contract has them. The
    //    width change is an EVENT at fixed samples in every run; only the cutting inside and around it moves.
    {
        const std::int64_t n = full.len();
        const int narrow = std::max (1, full.channels() / 2);
        struct T { const char* name; std::vector<Segment> segs; bool ok; std::vector<Policy> pols; };
        const std::vector<Policy> five { kFix64, kFix480, kFix4k, { "ragged 1..8192 #5", 0, 5 }, { "ragged 1..8192 #6", 0, 6 } };
        const std::vector<Policy> two { kFix480, { "ragged 1..8192 #9", 0, 9 } };
        const T timelines[] = {
            { "gap", { { 0, n * 2 / 5 + 37, -1 }, { n * 2 / 5 + 37, n * 3 / 5 + 911, 0 }, { n * 3 / 5 + 911, n, -1 } }, A::kClockOnly, five },
            { "gap at the head", { { 0, n / 7 + 333, 0 }, { n / 7 + 333, n, -1 } }, A::kClockOnly, two },
            { "finish inside a gap", { { 0, n * 6 / 7 + 77, -1 }, { n * 6 / 7 + 77, n, 0 } }, A::kClockOnly, two },
            { "narrow stretch", { { 0, n * 3 / 10 + 501, -1 }, { n * 3 / 10 + 501, n * 7 / 10 + 13, narrow }, { n * 7 / 10 + 13, n, -1 } },
              A::kNarrow && full.channels() > 1, two },
        };
        for (const T& t : timelines)
        {
            if (! t.ok) continue;
            const Programme g = full.withSegments (t.segs);
            bool rg = false;
            const Bits gref = runOne<A> (g, cuts (g, kWhole, A::kClockOnly), mb, rg);
            felitronics::test::ok (! rg, std::string (A::kName) + ": " + t.name + ": the reference (one call per segment) is accepted");
            felitronics::test::ok (gref.w != ref.w, std::string (A::kName) + ": " + t.name + ": the width change is visible in "
                                   "the report — otherwise the rows below compare nothing it did");
            for (const Policy& pol : t.pols)
            {
                const auto calls = cuts (g, pol, A::kClockOnly);
                if (std::string (t.name).find ("gap") != std::string::npos)
                    felitronics::test::ok (std::count_if (calls.begin(), calls.end(), [] (const Call& c) { return c.nch == 0 && c.n > 0; }) > 0,
                                           std::string (A::kName) + ": " + t.name + ", " + pol.name + ": the cut carries clock-only calls");
                bool r = false;
                const Bits got = runOne<A> (g, calls, mb, r);
                record (A::kName, std::string (t.name) + ": " + pol.name + " [" + callStats (calls) + "]", compare (gref, got, r),
                        gref.w.size());
            }
        }
    }

    // 4. Finish on a prefix no window, hop or block lines up with.
    for (const std::int64_t cut : { full.len() * 3 / 7 + 13, full.len() * 5 / 6 + 1 })
    {
        const Programme pre = full.prefix (cut);
        bool rp = false;
        const Bits pref = runOne<A> (pre, cuts (pre, kWhole, zw), mb, rp);
        for (const Policy& pol : { Policy { "ragged 1..8192 #7", 0, 7 }, kFix480 })
        {
            const auto calls = cuts (pre, pol, zw);
            bool r = false;
            const Bits got = runOne<A> (pre, calls, mb, r);
            record (A::kName, "prefix " + std::to_string (cut) + " of " + std::to_string (full.len()) + ", " + pol.name,
                    compare (pref, got, r || rp), pref.w.size());
        }
        // …and read WITHOUT finish(): the state a caller reporting progress would read at that sample.
        bool ru = false, rv = false;
        const Bits uref = runOne<A> (pre, cuts (pre, kWhole, zw), mb, ru, nullptr, nullptr, false);
        const Bits ugot = runOne<A> (pre, cuts (pre, { "ragged 1..8192 #8", 0, 8 }, zw), mb, rv, nullptr, nullptr, false);
        record (A::kName, "prefix " + std::to_string (cut) + ", read before finish(), ragged 1..8192 #8",
                compare (uref, ugot, ru || rv), uref.w.size());
    }
}

//==============================================================================
inline void printTable()
{
    std::printf ("\n  split invariance — the table (one line per analyzer; a NOT names its rows)\n");
    std::vector<std::string> seen;
    int width = 0;
    for (const Row& r : table()) width = std::max (width, (int) r.analyzer.size());
    for (const Row& r : table())
    {
        if (std::find (seen.begin(), seen.end(), r.analyzer) != seen.end()) continue;
        seen.push_back (r.analyzer);
        int rows = 0, bad = 0, weak = 0;
        for (const Row& q : table()) if (q.analyzer == r.analyzer) { ++rows; if (! q.d.same()) ++bad; }
        for (const auto& c : caveats()) if (c.first == r.analyzer) ++weak;
        const char* verdict = bad > 0 ? "NOT      " : weak > 0 ? "UNPROVEN " : "INVARIANT";
        std::printf ("    %-*s  %s  (%d of %d cuts bit-identical)\n", width, r.analyzer.c_str(), verdict, rows - bad, rows);
        for (const Row& q : table())
            if (q.analyzer == r.analyzer && ! q.d.same())
                std::printf ("        %s: %s\n", q.what.c_str(), q.d.describe().c_str());
        for (const auto& c : caveats())
            if (c.first == r.analyzer) std::printf ("        %s\n", c.second.c_str());
    }
}

} // namespace felitronics::test::split
