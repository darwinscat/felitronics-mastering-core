// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// The tempo surface of the probe ABI (fc_probe_tempo_*), natively, so it runs under ASan/UBSan on every row.
//
//   * THE ABI IS THE CLASS: every scalar, candidate and curve row the module publishes against the same programme
//     through tempo::TempoDetector directly, bit for bit — so a field published in the wrong slot, a NaN where a
//     number belongs, or a row that is not the point it claims to be cannot pass.
//   * THE FENCE: every getter under a null pointer, a zero capacity, a capacity one short of a row, and a binding
//     capacity, inside canaries — a getter that writes one element past its capacity is invisible to a diff of two
//     successful runs.
//   * THE PRICE (law 11d): fc_probe_tempo_storage_bytes against the core's own storageFor(), and against what the
//     FIRST run of this process asks the heap for, to the byte; asking costs nothing; a refused geometry quotes +0.0;
//     and the price is above zero exactly where the run is accepted.
//   * THE REFUSALS: a refused run leaves every getter silent, never the previous programme's numbers.
//
// TWO BINARIES RUN THIS FILE, one per module that publishes these entry points: felitronics_tempo_abi_tests links it
// against tools/wasm/fc_probe.cpp, felitronics_fctempo_abi_tests against tools/wasm/fc_tempo.cpp (the tempo detector
// alone), with FC_TEMPO_MODULE defined — which adds the one thing that module has and the probe has not: its version.
// Both compile the same text (tools/wasm/fc_tempo_entry.h), and this is the suite that says so, on both.

#include <felitronics_test.h>
#include <alloc_counter.h>   // installs the allocation counter: EVERY form of `new`, over-aligned included

#if defined(FC_TEMPO_MODULE)
  #include "fc_tempo_abi.h"
#endif

#include <felitronics/core/Config.h>
#include <felitronics/tempo/TempoDetector.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

extern "C"
{
    int           fc_probe_tempo_run          (const float*, std::uint32_t, std::uint32_t, double);
    int           fc_probe_tempo_run_with     (const float*, std::uint32_t, std::uint32_t, double, double, double, double, double);
    std::uint32_t fc_probe_tempo_scalars_len  (void);
    std::uint32_t fc_probe_tempo_cand_stride  (void);
    std::uint32_t fc_probe_tempo_point_stride (void);
    std::uint32_t fc_probe_tempo_scalars      (double*, std::uint32_t);
    std::uint32_t fc_probe_tempo_candidates   (double*, std::uint32_t);
    std::uint32_t fc_probe_tempo_curve        (double*, std::uint32_t);
    double        fc_probe_tempo_storage_bytes      (std::uint32_t, double, std::uint32_t);
    double        fc_probe_tempo_storage_bytes_with (std::uint32_t, double, std::uint32_t, double, double, double, double);
}

using felitronics::test::ok;
using felitronics::tempo::TempoDetector;
using felitronics::tempo::TempoParams;

namespace
{
using Getter = std::uint32_t (*) (double*, std::uint32_t);
constexpr double kCanary = 1234567.875;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::uint64_t bits (double d) { std::uint64_t u; std::memcpy (&u, &d, 8); return u; }
bool same (double a, double b) { return bits (a) == bits (b) || (std::isnan (a) && std::isnan (b)); }
std::string num (double v) { char b[48]; std::snprintf (b, sizeof b, "%.17g", v); return b; }

// A click train with a linear decay (no transcendental), on channel 0; channel c > 0 is the same train scaled and
// offset, with a little seeded noise, so the mix is a real mix. Planar: channel c at planar + c * frames.
std::vector<float> fixture (double bpm, double seconds, double sr, int channels)
{
    const auto n = (std::size_t) felitronics::tempo::js::round (seconds * sr);
    std::vector<float> v (n * (std::size_t) channels, 0.0f);
    const double period = (60.0 / bpm) * sr;
    const int burst = (int) felitronics::tempo::js::round (sr * 0.005);
    std::uint64_t st = 0x9E3779B97F4A7C15ull;
    for (int beat = 0; ; ++beat)
    {
        const double s = felitronics::tempo::js::round ((double) beat * period);
        if (s >= (double) n) break;
        for (int i = 0; i < burst && (std::size_t) s + (std::size_t) i < n; ++i)
            v[(std::size_t) s + (std::size_t) i] = (float) ((1.0 - (double) i / (double) burst) * ((i % 2) ? -1.0 : 1.0));
    }
    for (int c = 1; c < channels; ++c)
        for (std::size_t i = 0; i < n; ++i)
        {
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            v[(std::size_t) c * n + i] = v[i] * (float) (1.0 / (double) (c + 1)) + (float) (((double) (st >> 11) / 9007199254740992.0 - 0.5) * 0.02);
        }
    return v;
}

// What `g` wrote into `cap` doubles, as ELEMENTS: the scalar getter returns elements, the row getters return ROWS of
// `stride` — so the vector is cut at rows * stride, never at the row count (which read past size() before a review
// round caught it: five candidate rows came back as a five-element vector indexed to 9).
std::vector<double> read (Getter g, std::uint32_t cap, std::uint32_t stride = 1)
{
    std::vector<double> v ((std::size_t) cap, kCanary);
    const std::uint32_t got = g (v.data(), cap);
    v.resize ((std::size_t) got * stride);
    return v;
}

template <typename F>
long long asked (F&& f)
{
    const long long before = felitronics::test::alloc::bytes.load();
    f();
    return felitronics::test::alloc::bytes.load() - before;
}

// Calls `g` into exactly `cap` doubles fenced by canaries: the fence must hold and the rows must fit.
void fenced (Getter g, std::uint32_t cap, const std::string& name, std::uint32_t stride)
{
    std::vector<double> buf ((std::size_t) cap + 4, kCanary);
    const std::uint32_t rows = g (buf.data() + 2, cap);
    const bool fence = buf[0] == kCanary && buf[1] == kCanary && buf[buf.size() - 1] == kCanary && buf[buf.size() - 2] == kCanary;
    ok (fence, name + ": the fence around a " + std::to_string (cap) + "-double buffer holds");
    ok ((std::uint64_t) rows * stride <= (std::uint64_t) cap, name + ": " + std::to_string (rows) + " rows fit a capacity of " + std::to_string (cap));
}

//==============================================================================
void beforeAnyRun()
{
    felitronics::test::group ("before any run, every getter answers zero and writes nothing");
    for (auto [g, n] : { std::pair<Getter, const char*> { fc_probe_tempo_scalars, "scalars" },
                         { fc_probe_tempo_candidates, "candidates" }, { fc_probe_tempo_curve, "curve" } })
    {
        std::vector<double> buf (64, kCanary);
        bool clean = true;
        ok (g (buf.data(), 60) == 0, std::string (n) + ": answers zero");
        for (double d : buf) if (d != kCanary) clean = false;
        ok (clean, std::string (n) + ": and writes nothing");
    }
    ok (fc_probe_tempo_scalars_len() == 35u && fc_probe_tempo_cand_stride() == 2u && fc_probe_tempo_point_stride() == 5u,
        "the published widths: 35 scalars, 2 per candidate, 5 per point");
}

void thePriceIsTheCoresBudget()
{
    felitronics::test::group ("storage_bytes — the quoted price IS TempoDetector::storageFor(), and asking costs nothing");
    int rows = 0, wrong = 0;
    for (std::uint32_t ch : { 1u, 2u, 16u })
        for (double sr : { 8000.0, 44100.0, 48000.0, 96000.0, 768000.0 })
            for (std::uint32_t frames : { 0u, 1023u, 1024u, 48000u * 60u, 0xFFFFFFFFu })
            {
                double got = 0.0;
                const long long cost = asked ([&] { got = fc_probe_tempo_storage_bytes (ch, sr, frames); });
                const auto st = TempoDetector::storageFor (sr, (int) ch, frames, TempoParams {});
                const bool fits = (std::uint64_t) frames * ch * 4u <= 0xFFFFFFFFull;   // the run's own span bound
                ++rows;
                if (! (st.ok && cost == 0 && (fits ? (got == (double) st.bytes() && got > 0.0) : (got == 0.0 && ! std::signbit (got)))))
                    ++wrong;
            }
    ok (wrong == 0, "all " + std::to_string (rows) + " rows (widths 1/2/16, five rates, five lengths up to 2^32 - 1) quote "
                    "the core's own positive budget where the planes fit 4 GiB and +0.0 where they do not, and none of "
                    "the queries allocated");
    // THE SPAN BOUND, from both sides and against the run: the price knows the length, so it refuses what the run's
    // 32-bit span check refuses (a review round found it quoting a price for a programme no run could take).
    {
        std::vector<float> tiny (64, 0.0f);
        ok (fc_probe_tempo_storage_bytes (1u, 48000.0, 0x3FFFFFFFu) > 0.0
                && fc_probe_tempo_storage_bytes (1u, 48000.0, 0x40000000u) == 0.0
                && fc_probe_tempo_storage_bytes (16u, 48000.0, 0x3FFFFFFu) > 0.0
                && fc_probe_tempo_storage_bytes (16u, 48000.0, 0x4000000u) == 0.0
                && fc_probe_tempo_storage_bytes_with (1u, 48000.0, 0x40000000u, 60.0, 180.0, 6.0, 1.5) == 0.0,
            "mono: 2^30 - 1 frames priced, 2^30 at zero; sixteen channels: 2^26 - 1 priced, 2^26 at zero");
        ok (fc_probe_tempo_run (tiny.data(), 0x40000000u, 1u, 48000.0) == 0 && fc_probe_tempo_run (tiny.data(), 0x4000000u, 16u, 48000.0) == 0,
            "... and the run refuses exactly those spans, before reading a sample");
    }
    ok (fc_probe_tempo_storage_bytes (2u, 48000.0, 48000u * 600u) > fc_probe_tempo_storage_bytes (2u, 48000.0, 48000u * 60u),
        "ten times the programme costs more — the onset buffers are per hop");
    const TempoParams wide { 40.0, 300.0, 4.0, 1.0 };
    ok (fc_probe_tempo_storage_bytes_with (2u, 48000.0, 480000u, 60.0, 180.0, 6.0, 1.5) == fc_probe_tempo_storage_bytes (2u, 48000.0, 480000u)
            && fc_probe_tempo_storage_bytes_with (2u, 48000.0, 480000u, 40.0, 300.0, 4.0, 1.0)
                   == (double) TempoDetector::storageFor (48000.0, 2, 480000u, wide).bytes()
            && fc_probe_tempo_storage_bytes_with (2u, 48000.0, 480000u, 40.0, 300.0, 4.0, 1.0) != fc_probe_tempo_storage_bytes (2u, 48000.0, 480000u),
        "the parameterised price is the default one at the defaults, the core's budget at others, and moves with them");

    felitronics::test::group ("storage_bytes — a refused geometry quotes +0.0");
    const double inf = std::numeric_limits<double>::infinity();
    for (double sr : { 0.0, -1.0, 44.1, 7999.0, std::nextafter (8000.0, 0.0), 768000.5, inf, kNaN })
    {
        const double b = fc_probe_tempo_storage_bytes (2u, sr, 48000u);
        ok (b == 0.0 && ! std::signbit (b), "a rate of " + num (sr) + " quotes +0.0");
    }
    for (std::uint32_t ch : { 0u, 17u, 0x80000000u, 0xFFFFFFFFu })
        ok (fc_probe_tempo_storage_bytes (ch, 48000.0, 48000u) == 0.0, "a width of " + std::to_string (ch) + " quotes +0.0");
    ok (fc_probe_tempo_storage_bytes_with (2u, 48000.0, 48000u, 120.0, 90.0, 6.0, 1.5) == 0.0
            && fc_probe_tempo_storage_bytes_with (2u, 48000.0, 48000u, 60.0, 180.0, 0.0, 1.5) == 0.0
            && fc_probe_tempo_storage_bytes_with (2u, 48000.0, 48000u, 60.0, 180.0, 6.0, kNaN) == 0.0,
        "an inverted range, a zero window and a NaN hop quote +0.0");
}

// THE FIRST RUN OF THIS PROCESS, and it has to be: vector capacity is kept, so any later run asks for less.
void theFirstRunAsksExactlyThePrice()
{
    felitronics::test::group ("storage_bytes — the first run asks the heap for exactly the published price");
    const std::uint32_t ch = 6u;
    const double sr = 96000.0;
    const std::vector<float> planar = fixture (126.0, 4.0, sr, (int) ch);
    const auto frames = (std::uint32_t) (planar.size() / ch);
    const double price = fc_probe_tempo_storage_bytes (ch, sr, frames);
    int accepted = 0;
    const long long spent = asked ([&] { accepted = fc_probe_tempo_run (planar.data(), frames, ch, sr); });
    ok (accepted == 1 && (double) spent == price,
        "6 x 96 kHz, 4 s: the run asked for " + std::to_string (spent) + " bytes against a published " + num (price)
        + " — prepare(), process() and finish() together");
    int again = 0;
    const long long second = asked ([&] { again = fc_probe_tempo_run (planar.data(), frames, ch, sr); });
    ok (again == 1 && second == 0, "a second run at the same geometry asks for nothing (" + std::to_string (second) + " bytes)");
}

//==============================================================================
// The published rows against the class, driven with the same planes.
void theAbiIsTheClass()
{
    felitronics::test::group ("the ABI publishes exactly what TempoDetector reports — scalars, candidates and curve, bit for bit");
    struct Case { double bpm, seconds, sr; std::uint32_t ch; TempoParams p; bool with; };
    const Case cases[] = { { 120.0, 20.0, 48000.0, 2u, {}, false }, { 97.0, 14.0, 44100.0, 1u, {}, false },
                           { 140.0, 16.0, 22050.0, 3u, { 40.0, 300.0, 4.0, 1.0 }, true } };
    for (const Case& c : cases)
    {
        const std::vector<float> planar = fixture (c.bpm, c.seconds, c.sr, (int) c.ch);
        const auto frames = (std::uint32_t) (planar.size() / c.ch);
        const int ran = c.with ? fc_probe_tempo_run_with (planar.data(), frames, c.ch, c.sr, c.p.minBpm, c.p.maxBpm, c.p.winSec, c.p.hopSec)
                               : fc_probe_tempo_run (planar.data(), frames, c.ch, c.sr);
        TempoDetector d;
        d.setParams (c.p);
        std::vector<const float*> v; for (std::uint32_t k = 0; k < c.ch; ++k) v.push_back (planar.data() + (std::size_t) k * frames);
        const bool direct = d.prepare (c.sr, (int) c.ch, frames) && d.process (v.data(), (int) c.ch, (int) frames) && d.finish();
        const std::string tag = num (c.bpm) + " BPM x " + std::to_string (c.ch) + " @ " + num (c.sr) + ": ";
        ok (ran == 1 && direct && d.determined(), tag + "both roads ran, and the programme is determined");

        const std::vector<double> s = read (fc_probe_tempo_scalars, 64);
        const auto& h = d.headline();
        const auto& w = d.wholeTrack();
        const auto alt = [] (const felitronics::tempo::TempoHeadline& x, int i) { return i < x.altCount ? x.alts[i] : kNaN; };
        const double want[35] = { c.sr, (double) c.ch, (double) frames, d.odfSampleRate(), (double) d.onsetFrames(),
                                  c.p.minBpm, c.p.maxBpm, c.p.winSec, c.p.hopSec, (double) d.windowFrames(), (double) d.hopFrames(),
                                  1.0, h.bpm, h.confidence, (double) (int) h.label, (double) h.altCount, alt (h, 0), alt (h, 1),
                                  h.beatPeriodSec, h.beatOffsetSec, d.varies() ? 1.0 : 0.0, d.hasRange() ? 1.0 : 0.0,
                                  d.rangeLow(), d.rangeHigh(), (double) d.candidateCount(), (double) d.pointCount(),
                                  w.bpm, (double) w.altCount, alt (w, 0), alt (w, 1), w.beatPeriodSec,
                                  d.anchorBpm(), d.anchorConfidence(), d.anchorLag(), (double) d.nonFiniteSamples() };
        int bad = 0;
        for (std::size_t i = 0; i < 35 && i < s.size(); ++i) if (! same (s[i], want[i])) ++bad;
        ok (s.size() == 35 && bad == 0, tag + "all 35 scalars, in their published order (" + std::to_string (bad) + " differ)");

        const std::vector<double> cand = read (fc_probe_tempo_candidates, 64, 2u);
        bool candOk = cand.size() == 2u * (std::size_t) d.candidateCount();
        for (int k = 0; candOk && k < d.candidateCount(); ++k)
            candOk = same (cand[(std::size_t) k * 2], d.candidate (k).bpm) && same (cand[(std::size_t) k * 2 + 1], d.candidate (k).score);
        ok (candOk && d.candidateCount() > 0, tag + std::to_string (d.candidateCount()) + " candidate rows");

        const std::vector<double> curve = read (fc_probe_tempo_curve, 4096, 5u);
        bool curveOk = curve.size() == 5u * (std::size_t) d.pointCount() && d.pointCount() > 0;
        for (std::int64_t k = 0; curveOk && k < d.pointCount(); ++k)
        {
            const double* r = curve.data() + (std::size_t) k * 5;
            const auto q = d.point (k), raw = d.rawPoint (k);
            curveOk = same (r[0], q.t) && same (r[1], q.hasBpm ? 1.0 : 0.0) && same (r[2], q.hasBpm ? q.bpm : kNaN)
                   && same (r[3], q.conf) && same (r[4], raw.hasBpm ? raw.bpm : kNaN);
        }
        ok (curveOk, tag + std::to_string (d.pointCount()) + " curve rows: t, hasBpm, bpm, conf, raw bpm");
    }

    felitronics::test::group ("run_with at the documented defaults IS the default road; the mix is the detector's");
    const std::vector<float> st = fixture (111.0, 12.0, 48000.0, 2);
    const auto frames = (std::uint32_t) (st.size() / 2);
    const bool plain = fc_probe_tempo_run (st.data(), frames, 2u, 48000.0) == 1;
    const std::vector<double> a = read (fc_probe_tempo_scalars, 64);
    const bool withDefaults = fc_probe_tempo_run_with (st.data(), frames, 2u, 48000.0, 60.0, 180.0, 6.0, 1.5) == 1;
    const std::vector<double> b = read (fc_probe_tempo_scalars, 64);
    int differ = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) if (! same (a[i], b[i])) ++differ;
    ok (plain && withDefaults && a.size() == 35 && b.size() == 35 && differ == 0,
        "run_with(60, 180, 6, 1.5) answers the default road's scalars, bit for bit (" + std::to_string (differ) + " differ)");
    // The page's toMono of the same two planes, passed as ONE channel: every scalar but the width is the same.
    std::vector<float> mono (frames);
    for (std::uint32_t i = 0; i < frames; ++i) { float m = 0.0f; m += st[i]; m += st[frames + i]; mono[i] = m / 2.0f; }
    const bool monoRan = fc_probe_tempo_run (mono.data(), frames, 1u, 48000.0) == 1;
    const std::vector<double> m = read (fc_probe_tempo_scalars, 64);
    differ = 0;
    for (std::size_t i = 0; i < m.size() && i < a.size(); ++i) if (i != 1 && ! same (m[i], a[i])) ++differ;
    ok (monoRan && m.size() == 35 && m[1] == 1.0 && a[1] == 2.0 && differ == 0,
        "the stereo run and a run on its toMono mix agree in every scalar but the width (" + std::to_string (differ) + " differ)");
}

//==============================================================================
void theGettersUnderTheFence()
{
    felitronics::test::group ("every getter under the fence: null, zero, short of a row, and a capacity that binds");
    const std::vector<float> planar = fixture (123.0, 30.0, 44100.0, 2);
    ok (fc_probe_tempo_run (planar.data(), (std::uint32_t) (planar.size() / 2), 2u, 44100.0) == 1, "a 30 s programme ran");
    const std::pair<Getter, std::uint32_t> gs[] = { { fc_probe_tempo_scalars, 35u }, { fc_probe_tempo_candidates, 2u },
                                                    { fc_probe_tempo_curve, 5u } };
    const char* names[] = { "scalars", "candidates", "curve" };
    for (int k = 0; k < 3; ++k)
    {
        const Getter g = gs[k].first;
        const std::uint32_t stride = gs[k].second;
        const std::string name = names[k];
        ok (g (nullptr, 0) == 0 && g (nullptr, 1000) == 0, name + ": a null pointer writes nothing, at any capacity");
        for (std::uint32_t cap : { 0u, 1u, 2u, 3u, 7u, 64u }) fenced (g, cap, name, k == 0 ? 1u : stride);
        if (k > 0)
        {
            fenced (g, stride - 1u, name + " [one short of a row]", stride);
            const std::uint32_t full = (std::uint32_t) read (g, 4096, stride).size() / stride;
            ok (full >= 2, name + ": the fixture has " + std::to_string (full) + " rows, so the capacity can bind");
            std::vector<double> buf ((std::size_t) (full - 1) * stride + 4, kCanary);
            const std::uint32_t got = g (buf.data() + 2, (full - 1) * stride);
            const bool fence = buf[0] == kCanary && buf[1] == kCanary && buf[buf.size() - 1] == kCanary && buf[buf.size() - 2] == kCanary;
            ok (got == full - 1 && fence, name + ": a capacity of " + std::to_string (full - 1) + " rows returns exactly that many, "
                                          "and writes nothing past them");
        }
    }
    std::vector<double> shortScalars (40, kCanary);
    ok (fc_probe_tempo_scalars (shortScalars.data(), 34u) == 0 && shortScalars[0] == kCanary,
        "scalars: a capacity one short of the block writes nothing (all of it or none of it)");
}

void theRefusals()
{
    felitronics::test::group ("a refused run goes silent — never the previous programme's numbers");
    const std::vector<float> planar = fixture (120.0, 12.0, 48000.0, 2);
    const auto frames = (std::uint32_t) (planar.size() / 2);
    // Silent means ALL THREE getters: a refusal that cleared the scalars and left the previous candidates or curve
    // readable would pass a check of the scalars alone.
    const auto silent = []
    {
        bool quiet = true;
        for (Getter g : { fc_probe_tempo_scalars, fc_probe_tempo_candidates, fc_probe_tempo_curve })
        {
            std::vector<double> v (64, kCanary);
            quiet = quiet && g (v.data(), 64) == 0 && v[0] == kCanary;
        }
        return quiet;
    };
    const auto live = [&] { return fc_probe_tempo_run (planar.data(), frames, 2u, 48000.0) == 1 && ! silent(); };
    ok (live() && fc_probe_tempo_run (planar.data(), frames, 17u, 48000.0) == 0 && silent(), "17 channels");
    ok (live() && fc_probe_tempo_run (planar.data(), frames, 0u, 48000.0) == 0 && silent(), "0 channels");
    ok (live() && fc_probe_tempo_run (planar.data(), frames, 2u, 7999.0) == 0 && silent(), "7999 Hz (under the core's floor)");
    ok (live() && fc_probe_tempo_run (planar.data(), frames, 2u, kNaN) == 0 && silent(), "a NaN rate");
    ok (live() && fc_probe_tempo_run (nullptr, frames, 2u, 48000.0) == 0 && silent(), "a null buffer with frames");
    ok (live() && fc_probe_tempo_run (reinterpret_cast<const float*> (reinterpret_cast<const char*> (planar.data()) + 1), 100u, 1u, 48000.0) == 0
            && silent(), "a misaligned buffer");
    ok (live() && fc_probe_tempo_run_with (planar.data(), frames, 2u, 48000.0, 120.0, 90.0, 6.0, 1.5) == 0 && silent(), "an inverted BPM range");
    ok (live() && fc_probe_tempo_run_with (planar.data(), frames, 2u, 48000.0, 60.0, 180.0, 6.0, -1.0) == 0 && silent(), "a negative hop");

    felitronics::test::group ("an empty programme is a measurement: undetermined, as the spec answers it");
    ok (fc_probe_tempo_run (nullptr, 0u, 2u, 48000.0) == 1, "zero frames (and a null pointer) are accepted");
    const std::vector<double> s = read (fc_probe_tempo_scalars, 64);
    ok (s.size() == 35 && s[11] == 0.0 && std::isnan (s[12]) && s[13] == 0.0 && s[14] == 0.0 && s[15] == 0.0
            && std::isnan (s[18]) && s[24] == 0.0 && s[25] == 0.0,
        "undetermined: determined 0, bpm NaN, confidence 0, label 0, no alternatives, no beat, no candidates, no points");
    ok (read (fc_probe_tempo_curve, 64, 5u).empty() && read (fc_probe_tempo_candidates, 64, 2u).empty(), "and the lists are empty");

    felitronics::test::group ("the price is above zero exactly where the run is accepted");
    const std::vector<float> wide = fixture (130.0, 0.1, 8000.0, felitronics::core::kMaxChannels);   // in bounds at every width
    const auto wf = (std::uint32_t) (wide.size() / (std::size_t) felitronics::core::kMaxChannels);
    const double inf = std::numeric_limits<double>::infinity();
    int rows = 0, disagreed = 0;
    for (double sr : { 0.0, 44.1, 7999.0, 8000.0, 44100.0, 768000.0, 768001.0, inf, kNaN })
        for (std::uint32_t ch : { 0u, 1u, 2u, 16u, 17u })
            for (std::uint32_t n : { 0u, 64u, wf })
            {
                const bool priced = fc_probe_tempo_storage_bytes (ch, sr, n) > 0.0;
                const bool ran = fc_probe_tempo_run (wide.data(), n, ch, sr) == 1;
                ++rows;
                if (priced != ran) { ++disagreed; ok (false, "priced " + std::string (priced ? "above zero" : "at zero") + " but the run "
                                                          + (ran ? "accepted" : "refused") + " at " + std::to_string (ch) + " x " + num (sr)
                                                          + " with " + std::to_string (n) + " frames"); }
            }
    ok (disagreed == 0, "the price and the run agree on every one of " + std::to_string (rows) + " rows");
}
#if defined(FC_TEMPO_MODULE)
// The version is a LITERAL here as well as the header's constant, as AbiTests.cpp holds fc_probe's: a bump is made on
// purpose, in both places.
void theModuleSpeaksItsVersion()
{
    felitronics::test::group ("fctempo: the module answers its own version, not fc_probe's");
    static_assert (FC_TEMPO_ABI_VERSION == 1u, "fc_tempo starts at ABI version 1; a bump edits this line on purpose");
    ok (fc_tempo_abi_version() == FC_TEMPO_ABI_VERSION && fc_tempo_abi_version() == 1u,
        "fc_tempo_abi_version() answers FC_TEMPO_ABI_VERSION, 1");
}
#endif
} // namespace

int main()
{
#if defined(FC_TEMPO_MODULE)
    std::printf ("felitronics fc_probe_tempo_* ABI tests — the fctempo module (tools/wasm/fc_tempo.cpp)\n");
    theModuleSpeaksItsVersion();           // allocates nothing: the first-run oracle below is still the first run
#else
    std::printf ("felitronics fc_probe_tempo_* ABI tests — the fcprobe module (tools/wasm/fc_probe.cpp)\n");
#endif
    beforeAnyRun();
    thePriceIsTheCoresBudget();
    theFirstRunAsksExactlyThePrice();      // the first tempo run of this process — see its note
    theAbiIsTheClass();
    theGettersUnderTheFence();
    theRefusals();
    return felitronics::test::report();
}
