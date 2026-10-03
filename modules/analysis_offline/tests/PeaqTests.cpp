// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// analysis::Peaq (PEAQ Basic, ITU-R BS.1387) self-tests.
//
//   · parts of the model against the equations computed here by hand (eqs. 7, 13, 61; the network of 6.1 against
//     the DI and ODG GstPEAQ printed for its own MOVs)
//   · the ORACLE: five synthetic pairs, each built to exercise one reading of the text, against the MOVs and grade
//     GstPEAQ gives on the very same files (`PeaqTests --write <dir>` writes them; the numbers below were read from
//     `gst-launch-1.0 ... peaq console-output=true`, GstPEAQ run as a black box)
//   · pins of this implementation's own grade on those pairs
//   · transparent input, silence, non-finite input, mono = dual mono
//   · the analyzer contract: any slicing gives the same bits, refusals move nothing, reset() replays, prepare()
//     allocates exactly storageFor(), process()/finish()/reset() allocate nothing

#include <felitronics_test.h>
#include <alloc_counter.h>   // installs the allocation counter: EVERY form of `new`, over-aligned included
#include <felitronics/analysis/Peaq.h>
#include <felitronics/io/Wav.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics;

using analysis::Peaq;
using analysis::PeaqMov;
using analysis::PeaqResult;
using analysis::PeaqVerdict;

namespace
{
using Planar = std::vector<std::vector<float>>;
constexpr double kSr = 48000.0;
constexpr long kLen = 192500;             // 4 s and a ragged tail: 186 full frames + the tail frame

struct Pair
{
    const char* name;
    Planar ref, test;
};

struct Lcg
{
    std::uint32_t s;
    double next() noexcept { s = s * 1664525u + 1013904223u; return (double) (s >> 8) / 8388608.0 - 1.0; }   // [-1, 1)
};

// A deterministic stand-in for music: plucked harmonic notes on a short scale, a shimmer of partials from 6 to 16 kHz
// (so the reference has the bandwidth PEAQ needs — under 346 lines its bandwidth MOVs are undefined), a little
// low-passed noise, two channels that differ. det::sin keeps it the same input on every row.
Planar programme (long n, long silentFrom = 0, long silentTo = 0)
{
    Planar x (2, std::vector<float> ((std::size_t) n, 0.0f));
    const double scale[8] { 220.0, 246.94, 277.18, 293.66, 329.63, 369.99, 415.30, 440.0 };
    for (int c = 0; c < 2; ++c)
    {
        Lcg rng { 12345u + 977u * (std::uint32_t) c };
        double lp = 0.0;
        for (long i = 0; i < n; ++i)
        {
            const long note = i / 12000;                          // a note every 0.25 s
            const double t = (double) (i - note * 12000) / kSr;
            const double f0 = scale[(note * 3 + c) % 8];
            const double env = core::det::exp2 (-t * 9.0);
            double v = 0.0;
            for (int h = 1; h <= 6; ++h)
                v += (0.32 / (double) h) * core::det::sin (6.283185307179586 * f0 * (double) h * t + 0.3 * (double) (c * h));
            double shimmer = 0.0;
            for (int k = 0; k < 12; ++k)
                shimmer += core::det::sin (6.283185307179586 * (6000.0 + 850.0 * (double) k + 37.0 * (double) c) * (double) i / kSr);
            lp = 0.85 * lp + 0.15 * rng.next();
            x[(std::size_t) c][(std::size_t) i] = (float) (env * v + 0.004 * shimmer * (0.6 + 0.4 * env) + 0.01 * lp);
        }
        for (long i = silentFrom; i < silentTo; ++i) x[(std::size_t) c][(std::size_t) i] = 0.0f;
    }
    return x;
}

std::vector<Pair> pairs()
{
    std::vector<Pair> p;
    {   // A: hard clipping — the damage the measure exists for
        Pair a { "clip", programme (kLen), {} };
        a.test = a.ref;
        for (auto& ch : a.test) for (float& v : ch) v = v > 0.18f ? 0.18f : (v < -0.18f ? -0.18f : v);
        p.push_back (a);
    }
    {   // B: 0.4 s of digital silence first (the data boundary starts at frame 18), then added noise: the delayed
        // averaging's start decides WinModDiff1, AvgModDiff1/2 and RmsNoiseLoud
        Pair b { "leadin", programme (kLen, 0, 19200), {} };
        b.test = b.ref;
        Lcg rng { 777u };
        for (auto& ch : b.test) for (long i = 19200; i < kLen; ++i) ch[(std::size_t) i] += (float) (0.004 * rng.next());
        p.push_back (b);
    }
    {   // C: 0.6 s of digital silence in the middle, test low-passed: silent frames must not count in the bandwidths
        Pair c { "gap-lowpass", programme (kLen, 96000, 124800), {} };
        c.test = c.ref;
        for (auto& ch : c.test)
        {
            double y = 0.0;
            for (float& v : ch) { y = 0.45 * y + 0.55 * (double) v; v = (float) y; }
        }
        p.push_back (c);
    }
    {   // D: requantised to 7 bits: an error with harmonic structure (EHS)
        Pair d { "requant", programme (kLen), {} };
        d.test = d.ref;
        for (auto& ch : d.test) for (float& v : ch) v = (float) (std::nearbyint ((double) v * 64.0) / 64.0);
        p.push_back (d);
    }
    {   // E: the reference with its last bit dithered — every sample moved one ulp up or down at random, what a float
        // render of an untouched programme leaves. Inaudible by any reading; the noise-to-mask ratio sits near -112 dB.
        Pair e { "near-identical", programme (kLen), {} };
        e.test = e.ref;
        Lcg rng { 4242u };
        for (auto& ch : e.test) for (float& v : ch) v = std::nextafter (v, rng.next() < 0.0 ? -2.0f : 2.0f);
        p.push_back (e);
    }
    return p;
}

PeaqResult run (const Planar& ref, const Planar& test, long block = 8192)
{
    Peaq q;
    const int nch = (int) ref.size();
    const long n = (long) ref[0].size();
    if (! q.prepare (nch, n)) return {};
    const float* rp[2] {};
    const float* tp[2] {};
    for (long pos = 0; pos < n; pos += block)
    {
        const int len = (int) (n - pos < block ? n - pos : block);
        for (int c = 0; c < nch; ++c) { rp[c] = ref[(std::size_t) c].data() + pos; tp[c] = test[(std::size_t) c].data() + pos; }
        if (! q.process (rp, tp, nch, len)) return {};
    }
    q.finish();
    return q.result();
}

bool sameBits (double a, double b) { return std::memcmp (&a, &b, sizeof a) == 0; }
bool sameResult (const PeaqResult& a, const PeaqResult& b)
{
    bool same = a.verdict == b.verdict && a.frames == b.frames && a.firstFrame == b.firstFrame && a.lastFrame == b.lastFrame
             && sameBits (a.odg, b.odg) && sameBits (a.modelOdg, b.modelOdg) && sameBits (a.distortionIndex, b.distortionIndex);
    for (int i = 0; i < analysis::kPeaqMovCount; ++i) same = same && sameBits (a.mov[(std::size_t) i], b.mov[(std::size_t) i]);
    return same;
}

int writeFixtures (const char* dir)
{
    for (const Pair& p : pairs())
    {
        const std::string d = std::string (dir) + "/" + p.name;
        std::vector<std::vector<double>> r (2), t (2);
        for (int c = 0; c < 2; ++c)
        {
            r[(std::size_t) c].assign (p.ref[(std::size_t) c].begin(), p.ref[(std::size_t) c].end());
            t[(std::size_t) c].assign (p.test[(std::size_t) c].begin(), p.test[(std::size_t) c].end());
        }
        if (! io::writeWav (d + "/REF.wav", r, kSr, 32, true) || ! io::writeWav (d + "/T.wav", t, kSr, 32, true))
        {
            std::fprintf (stderr, "cannot write %s (the directory must exist)\n", d.c_str());
            return 2;
        }
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 3 && std::strcmp (argv[1], "--write") == 0) return writeFixtures (argv[2]);
    std::printf ("analysis::Peaq\n");

    // ---------------------------------------------------------------------------------------------- the model by hand
    test::group ("the model's constant parts against the equations, computed here with the system libm");
    {
        Peaq q;
        test::run (q.prepare (2, 4096));
        double worstW = 0.0;
        for (int k = 1; k < Peaq::kBins; ++k)
        {
            const double f = k * 23.4375 / 1000.0;
            const double w = -0.6 * 3.64 * std::pow (f, -0.8) + 6.5 * std::exp (-0.6 * (f - 3.3) * (f - 3.3)) - 1e-3 * std::pow (f, 3.6);
            const double want = std::pow (10.0, w / 10.0);
            if (want > 1e-300) worstW = std::max (worstW, std::fabs (q.outerEarWeight (k) - want) / want);
        }
        test::ok (worstW < 1e-12 && q.outerEarWeight (0) == 0.0, "outer and middle ear, eq. 7, every line (worst relative "
                  + std::to_string (worstW) + ")");
        const double fc0 = 91.708, fc108 = 17690.045;
        test::approx (q.internalNoise (0), std::pow (10.0, 0.4 * 0.364 * std::pow (fc0 / 1000.0, -0.8)), 1e-12, "internal noise, eq. 13, band 0");
        test::approx (q.internalNoise (108), std::pow (10.0, 0.4 * 0.364 * std::pow (fc108 / 1000.0, -0.8)), 1e-12, "internal noise, eq. 13, band 108");
        double worstS = 0.0;
        for (int b = 0; b < Peaq::kBands; ++b)
        {
            const double fc = analysis::peaq_detail::kBandCentreHz[b];
            const double want = std::pow (10.0, (-2.0 - 2.05 * std::atan (fc / 4000.0) - 0.75 * std::atan ((fc / 1600.0) * (fc / 1600.0))) / 10.0);
            worstS = std::max (worstS, std::fabs (q.loudnessIndex (b) - want) / want);
        }
        test::ok (worstS < 1e-13, "threshold index s, eq. 61, every band (worst relative " + std::to_string (worstS) + ")");
        double worstA = 0.0;
        for (int i = -4000; i <= 4000; ++i)
        {
            const double x = i * 0.0137;
            worstA = std::max (worstA, std::fabs (analysis::peaq_detail::atan (x) - std::atan (x)));
        }
        char buf[32];
        std::snprintf (buf, sizeof buf, "%.3g", worstA);
        test::ok (worstA < 2e-15, std::string ("the IEEE-only atan against libm's, |x| <= 55, within 4 ulp of pi/2 (worst ") + buf + ")");
        test::ok (q.spreadNorm (0) > 0.0 && q.spreadNorm (54) > q.spreadNorm (0), "NormSP, eqs. 19-20: positive, larger mid-scale than at the edge");
        test::ok (q.windowSample (0) == 0.0 && q.windowSample (1023) > q.windowSample (100), "the window is a Hann (eq. 2), zero at its ends");
    }

    test::group ("the network (6.1, Tables 13-16) on GstPEAQ's own MOVs gives GstPEAQ's DI and ODG");
    {
        // GstPEAQ on ext-in bassdrums L5 (max-measure): it printed these eleven MOVs, DI -0.361 and ODG -2.255.
        const std::array<double, 11> mov { 863.018405, 859.763804, 0.689622, 32.622619, 2.564993, 0.187848,
                                           31.694710, 47.002060, 1.431494, 1.000000, 0.599085 };
        const double di = Peaq::distortionIndex (mov);
        test::approx (di, -0.361, 0.0005, "DI");
        test::approx (Peaq::odgFromDi (di), -2.255, 0.0005, "ODG");
        test::approx (Peaq::odgFromDi (-1e9), -3.98, 1e-12, "ODG floor bmin");
        test::approx (Peaq::odgFromDi (1e9), 0.22, 1e-12, "ODG ceiling bmax");
    }

    // ---------------------------------------------------------------------------------------------- the oracle
    const std::vector<Pair> ps = pairs();
    test::group ("the oracle: each synthetic pair against GstPEAQ on the same files");
    {
        struct Oracle { double odg; std::array<double, 11> mov; };
        // gst-launch-1.0 peaq console-output=true, the pairs written by `PeaqTests --write`.
        const Oracle gst[4] {
            { -3.908, { 658.449198, 657.951872, 12.379035, 49.830959, 2.805852, 1.523124, 49.689908, 136.496210, 12.220666, 0.976761, 0.732620 } },   // clip
            { -2.191, { 656.858824, 656.858824, -6.253159, 10.553441, 1.948086, 0.203561, 9.958050, 24.490092, 0.788291, 0.998444, 0.788235 } },   // leadin
            { -0.982, { 717.537267, 659.913043, -6.622930, 0.940279, 2.134004, 0.172777, 0.673729, 0.495280, 0.085776, 0.999955, 0.818182 } },   // gap-lowpass
            { -3.307, { 656.901070, 656.901070, 0.707712, 14.957034, 2.361189, 0.103784, 14.572837, 36.870988, 3.015940, 1.000000, 0.882353 } },   // requant
        };
        for (int i = 0; i < 4; ++i)
        {
            const PeaqResult r = run (ps[(std::size_t) i].ref, ps[(std::size_t) i].test);
            const std::string nm = ps[(std::size_t) i].name;
            test::ok (r.verdict == PeaqVerdict::Graded, nm + ": graded");
            test::approx (r.modelOdg, gst[i].odg, 0.006, nm + ": ODG");
            for (int m = 0; m < 11; ++m)
            {
                const double want = gst[i].mov[(std::size_t) m];
                // EHS within 3 %: the reading of 4.8.1 agrees with GstPEAQ's to 0.0008 on 48 drum pairs, not to the
                // last digit. ADB within 0.05 %: its step count truncates e (eq. 78), so a last-digit difference in a
                // level can move one band's count by a step. Everything else to the oracle's printed digits.
                const double tol = m == (int) PeaqMov::Ehs ? 0.03 * std::fabs (want) + 1e-3
                                 : m == (int) PeaqMov::Adb ? 5e-4 * std::fabs (want) + 1e-4
                                                           : 2e-5 * std::fabs (want) + 2e-5;
                test::approx (r.mov[(std::size_t) m], want, tol, nm + ": " + analysis::kPeaqMovNames[m]);
            }
        }
    }

    test::group ("the tail: 192500 samples are 186 full frames and one frame completed with zeros");
    {
        const PeaqResult r = run (ps[0].ref, ps[0].test);
        test::ok (r.frames == 187 && r.lastFrame == 186, "187 frames, the last one counted (" + std::to_string (r.frames) + ")");
        const PeaqResult b = run (ps[1].ref, ps[1].test);
        test::ok (b.firstFrame == 17, "lead-in: the data boundary starts at the first frame reaching sample 19200 (" + std::to_string (b.firstFrame) + ")");
    }

    test::group ("pins: this implementation's own grade on the pairs (relative 1e-9 — a change here is a change of the model)");
    {
        const double pin[4] { -3.9082499497061707, -2.1913005809969262, -0.98206267600682651, -3.3072669063523068 };
        for (int i = 0; i < 4; ++i)
        {
            const PeaqResult r = run (ps[(std::size_t) i].ref, ps[(std::size_t) i].test);
            test::approx (r.modelOdg, pin[i], 1e-9 * std::fabs (pin[i]), std::string (ps[(std::size_t) i].name) + ": pinned ODG");
        }
    }

    // ---------------------------------------------------------------------------------------------- verdicts
    // The network was never asked about a test that IS the reference: on a real master that left the programme alone
    // (max-measure, children-of-test-tubes at three loudness steps, Total NMR -103 dB) GstPEAQ and this model both
    // read EHS 52-67 instead of < 1 and grade -2.1..-2.2. That programme cannot ship in a public suite; the pairs
    // below hold the rule itself: under kTransparentNmrDb the report says 0, whatever the network says.
    test::group ("transparent input: under the NMR line the grade is 0 and the network's answer stays readable");
    {
        const PeaqResult r = run (ps[4].ref, ps[4].test);
        test::ok (r.verdict == PeaqVerdict::Transparent && r.odg == 0.0, "near-identical: Transparent, ODG 0 (GstPEAQ: 0.197, Total NMR -112.37 dB)");
        test::ok (r.mov[(std::size_t) PeaqMov::TotalNmr] < Peaq::kTransparentNmrDb, "its Total NMR is below the line ("
                  + std::to_string (r.mov[(std::size_t) PeaqMov::TotalNmr]) + " dB)");
        test::ok (r.modelOdg != 0.0 && std::isfinite (r.modelOdg), "and the network's own grade is kept as modelOdg ("
                  + std::to_string (r.modelOdg) + ")");
        const PeaqResult same = run (ps[0].ref, ps[0].ref);
        test::ok (same.verdict == PeaqVerdict::Transparent && same.odg == 0.0, "the reference against itself: Transparent, ODG 0");
        const PeaqResult clip = run (ps[0].ref, ps[0].test);
        test::ok (clip.verdict == PeaqVerdict::Graded && clip.odg == clip.modelOdg && clip.odg < -1.0, "real damage stays graded");
    }

    test::group ("no signal, non-finite input, mono = dual mono");
    {
        Planar silent (2, std::vector<float> ((std::size_t) kLen, 0.0f));
        const PeaqResult s = run (silent, ps[0].test);
        test::ok (s.verdict == PeaqVerdict::NoSignal && ! s.graded() && s.firstFrame == -1, "a silent reference: NoSignal");
        Planar shortRef (2, std::vector<float> (100, 0.0f));
        test::ok (run (shortRef, shortRef).verdict == PeaqVerdict::NoSignal, "100 silent samples: NoSignal");
        Planar nan = ps[0].test;
        nan[1][5000] = std::numeric_limits<float>::quiet_NaN();
        const PeaqResult n = run (ps[0].ref, nan);
        test::ok (n.verdict == PeaqVerdict::NonFinite && n.nonFiniteSamples == 1 && ! n.graded(), "one NaN: NonFinite, counted");
        const Planar monoR { ps[0].ref[0] }, monoT { ps[0].test[0] };
        const Planar dualR { ps[0].ref[0], ps[0].ref[0] }, dualT { ps[0].test[0], ps[0].test[0] };
        test::ok (sameResult (run (monoR, monoT), run (dualR, dualT)), "mono gives the bits of the same channel twice");
    }

    // ---------------------------------------------------------------------------------------------- the contract
    test::group ("any slicing gives the same bits");
    {
        const PeaqResult whole = run (ps[1].ref, ps[1].test, kLen);
        bool all = true;
        for (long b : { 1L, 7L, 1023L, 1024L, 1025L, 4096L, 65537L }) all = all && sameResult (whole, run (ps[1].ref, ps[1].test, b));
        test::ok (all, "one call, and blocks of 1, 7, 1023, 1024, 1025, 4096, 65537");
        // ragged cuts with zero-length calls in between
        Peaq q;
        test::run (q.prepare (2, kLen));
        Lcg rng { 99u };
        long pos = 0;
        while (pos < kLen)
        {
            const long len = std::min (kLen - pos, (long) ((rng.next() + 1.0) * 3000.0));
            const float* rp[2] { ps[1].ref[0].data() + pos, ps[1].ref[1].data() + pos };
            const float* tp[2] { ps[1].test[0].data() + pos, ps[1].test[1].data() + pos };
            test::run (q.process (rp, tp, 2, (int) len));
            test::run (q.process (rp, tp, 2, 0));
            pos += len;
        }
        q.finish();
        test::ok (sameResult (whole, q.result()), "random cuts with zero-length calls");
    }

    test::group ("refusals move nothing; finish() ends the programme; reset() replays it");
    {
        Peaq q;
        test::ok (! q.prepare (0, 100) && ! q.prepare (3, 100) && ! q.prepare (2, -1), "prepare() refuses 0 or 3 channels and a negative length");
        test::ok (! Peaq::storageFor (3, 10).ok && Peaq::storageFor (1, 0).ok, "storageFor() refuses exactly what prepare() refuses");
        test::run (q.prepare (2, 10000));
        const float* rp[2] { ps[0].ref[0].data(), ps[0].ref[1].data() };
        const float* tp[2] { ps[0].test[0].data(), ps[0].test[1].data() };
        const float* half[2] { ps[0].ref[0].data(), nullptr };
        test::ok (! q.process (rp, tp, 1, 100), "a channel count other than the prepared one");
        test::ok (! q.process (rp, tp, 2, -1), "n < 0");
        test::ok (! q.process (half, tp, 2, 100) && ! q.process (nullptr, tp, 2, 100), "null planes");
        test::ok (! q.process (rp, tp, 2, 10001), "more samples than prepare() was sized for");
        test::ok (q.samplesProcessed() == 0, "and none of them moved the clock");
        test::run (q.process (rp, tp, 2, 6000));
        test::ok (! q.process (rp, tp, 2, 4001) && q.samplesProcessed() == 6000, "the capacity counts what already arrived");
        q.finish();
        test::ok (! q.process (rp, tp, 2, 10), "process() after finish()");
        const PeaqResult first = q.result();
        q.finish();
        test::ok (sameResult (first, q.result()), "a second finish() changes nothing");
        q.reset();
        test::ok (q.result().verdict == PeaqVerdict::NotRun, "reset() clears the result");
        test::run (q.process (rp, tp, 2, 6000));
        q.finish();
        test::ok (sameResult (first, q.result()), "reset() replays the programme to the same bits");
    }

    test::group ("RT: prepare() allocates exactly storageFor(); process(), finish() and reset() allocate nothing");
    {
        Peaq q;
        const long long b0 = alloc::bytes.load();
        const long long al0 = alloc::count.load();
        test::run (q.prepare (2, kLen));
        const long long used = alloc::bytes.load() - b0;
        const long long blocks = alloc::count.load() - al0;
        const auto st = Peaq::storageFor (2, kLen);
        test::ok (st.ok && (long long) st.bytes() == used && blocks == 4, "prepare() requested exactly storageFor().bytes() in four blocks ("
                  + std::to_string (used) + " bytes, " + std::to_string (blocks) + " blocks)");
        const float* rp[2] { ps[3].ref[0].data(), ps[3].ref[1].data() };
        const float* tp[2] { ps[3].test[0].data(), ps[3].test[1].data() };
        const long long before = alloc::count.load();
        for (long pos = 0; pos + 777 <= kLen; pos += 777)
        {
            rp[0] = ps[3].ref[0].data() + pos; rp[1] = ps[3].ref[1].data() + pos;
            tp[0] = ps[3].test[0].data() + pos; tp[1] = ps[3].test[1].data() + pos;
            (void) q.process (rp, tp, 2, 777);
        }
        q.finish();
        q.reset();
        const bool noAlloc = alloc::count.load() == before;   // read before the message string exists
        test::okNoAlloc (noAlloc, "no allocation in process / finish / reset");
    }

    return test::report();
}
