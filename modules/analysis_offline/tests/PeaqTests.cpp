// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// analysis::Peaq (PEAQ Basic, ITU-R BS.1387) self-tests.
//
//   · parts of the model against the equations computed here by hand (eqs. 7, 13, 61; the network of 6.1 against
//     the DI and ODG GstPEAQ printed for its own MOVs)
//   · the ORACLE: synthetic pairs, each built to exercise one reading of the text, against the MOVs and grade GstPEAQ
//     gives on the very same files (`felitronics_peaq_tests --write <dir>` writes them; the numbers below were read
//     from `gst-launch-1.0 ... peaq console-output=true`, GstPEAQ run as a black box). Where this implementation
//     follows the text and GstPEAQ does not (the bandwidth of a frame with a silent test), the pair pins the text and
//     the size of the difference.
//   · pins of this implementation's own grade on those pairs
//   · the verdicts: Transparent per channel, Undefined before Transparent, NoSignal, NonFinite, OutOfRange
//   · mono = dual mono
//   · the analyzer contract: any slicing gives the same bits, refusals move nothing, reset() replays, prepare()
//     allocates exactly storageFor(), process()/finish()/reset() allocate nothing

#include <felitronics_test.h>
#include <alloc_counter.h>   // installs the allocation counter: EVERY form of `new`, over-aligned included
#include <felitronics/analysis/Peaq.h>
#include <felitronics/io/Wav.h>

#include <array>
#include <cmath>
#include <functional>
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

Planar tonal (long n)
{
    // A sustained bright harmonic tone (196 Hz, 80 harmonics to 15.7 kHz, re-struck every second): a spectrum of regular
    // peaks and deep valleys, the material EHS exists for.
    Planar x (2, std::vector<float> ((std::size_t) n, 0.0f));
    for (int c = 0; c < 2; ++c)
        for (long i = 0; i < n; ++i)
        {
            const double t = (double) (i % 48000) / kSr;
            const double env = core::det::exp2 (-t * 2.0);
            double v = 0.0;
            for (int h = 1; h <= 80; ++h)
                v += (0.2 / core::det::pow ((double) h, 0.7)) * core::det::sin (6.283185307179586 * 196.0 * (double) h * (double) i / kSr + 0.7 * (double) (h * (c + 1)));
            x[(std::size_t) c][(std::size_t) i] = (float) (env * v);
        }
    return x;
}

void addNoise (Planar& x, double amp, std::uint32_t seed, long from = 0)
{
    Lcg rng { seed };
    for (auto& ch : x) for (long i = from; i < (long) ch.size(); ++i) ch[(std::size_t) i] += (float) (amp * rng.next());
}

std::vector<Pair> pairs()
{
    std::vector<Pair> p;
    {   // 0 clip: hard clipping — the damage the measure exists for
        Pair a { "clip", programme (kLen), {} };
        a.test = a.ref;
        for (auto& ch : a.test) for (float& v : ch) v = v > 0.18f ? 0.18f : (v < -0.18f ? -0.18f : v);
        p.push_back (a);
    }
    {   // 1 leadin: 0.4 s of digital silence first (the data boundary starts at frame 17), then added noise — the
        // delayed averaging's start decides WinModDiff1, AvgModDiff1/2 and RmsNoiseLoud
        Pair b { "leadin", programme (kLen, 0, 19200), {} };
        b.test = b.ref;
        addNoise (b.test, 0.004, 777u, 19200);
        p.push_back (b);
    }
    {   // 2 gap-lowpass: 0.6 s of digital silence in the middle, test low-passed: silent frames in the bandwidths
        Pair c { "gap-lowpass", programme (kLen, 96000, 124800), {} };
        c.test = c.ref;
        for (auto& ch : c.test)
        {
            double y = 0.0;
            for (float& v : ch) { y = 0.45 * y + 0.55 * (double) v; v = (float) y; }
        }
        p.push_back (c);
    }
    {   // 3 requant: requantised to 7 bits — an error with structure (EHS)
        Pair d { "requant", programme (kLen), {} };
        d.test = d.ref;
        for (auto& ch : d.test) for (float& v : ch) v = (float) (std::nearbyint ((double) v * 64.0) / 64.0);
        p.push_back (d);
    }
    {   // 4 near-identical: the last bit dithered — every sample one ulp up or down at random; NMR near -112 dB
        Pair e { "near-identical", programme (kLen), {} };
        e.test = e.ref;
        Lcg rng { 4242u };
        for (auto& ch : e.test) for (float& v : ch) v = std::nextafter (v, rng.next() < 0.0 ? -2.0f : 2.0f);
        p.push_back (e);
    }
    {   // 5 tonal-requant: the harmonic tone requantised to 6 bits — a periodic error on a periodic signal
        Pair f { "tonal-requant", tonal (kLen), {} };
        f.test = f.ref;
        for (auto& ch : f.test) for (float& v : ch) v = (float) (std::nearbyint ((double) v * 32.0) / 32.0);
        p.push_back (f);
    }
    {   // 6 tonal-noise: the harmonic tone plus white noise at -48 dBFS — noise in the valleys between the harmonics
        Pair g { "tonal-noise", tonal (kLen), {} };
        g.test = g.ref;
        addNoise (g.test, 0.004, 31337u);
        p.push_back (g);
    }
    {   // 7 staggered: the reference is loud for its first 1.5 s, the test from 2.0 s on, each otherwise only a 40 Hz
        // tone at -40 dBFS (below every band, so under 0.1 sone, yet inside the data boundary). Each programme reaches
        // 0.1 sone once, half a second apart, and no frame has both above it: the loudness threshold of 5.2.4.2 binds
        Pair h { "staggered", programme (kLen), {} };
        h.test = h.ref;
        for (int c = 0; c < 2; ++c)
            for (long i = 0; i < kLen; ++i)
            {
                const float hum = (float) (0.01 * core::det::sin (6.283185307179586 * 40.0 * (double) i / kSr));
                const float music = h.ref[(std::size_t) c][(std::size_t) i];
                h.ref[(std::size_t) c][(std::size_t) i] = i < 72000 ? music + hum : hum;
                h.test[(std::size_t) c][(std::size_t) i] = i < 96000 ? hum : music + hum;
            }
        p.push_back (h);
    }
    {   // 8 leadin-long: 0.8 s of digital silence first, longer than the delayed averaging's 0.5 s
        Pair k { "leadin-long", programme (kLen, 0, 38400), {} };
        k.test = k.ref;
        addNoise (k.test, 0.004, 99u, 38400);
        p.push_back (k);
    }
    {   // 9 tail-silence: the last 1.2 s digitally silent in the reference; the test carries noise throughout
        Pair m { "tail-silence", programme (kLen, 134900, kLen), {} };
        m.test = m.ref;
        addNoise (m.test, 0.003, 5u);
        p.push_back (m);
    }
    {   // 10 dropout: the test drops out to digital silence for 100 ms in the middle, under a playing reference
        Pair d { "dropout", programme (kLen), {} };
        d.test = d.ref;
        for (auto& ch : d.test) for (long i = 96000; i < 100800; ++i) ch[(std::size_t) i] = 0.0f;
        p.push_back (d);
    }
    {   // 11 mono-clip: the clip pair's left channel alone, as a mono file
        Pair q { "mono-clip", { p[0].ref[0] }, { p[0].test[0] } };
        p.push_back (q);
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

std::string num (double v) { char b[40]; std::snprintf (b, sizeof b, "%.6g", v); return b; }

// The worst of a set of errors, NaN-aware: a NaN error makes the result NaN, so a `< tol` check on it fails.
struct Worst
{
    double v = 0.0;
    void add (double e) { if (! (e <= v)) v = e; }
};

int writeFixtures (const char* dir)
{
    for (const Pair& p : pairs())
    {
        const std::string d = std::string (dir) + "/" + p.name;
        std::vector<std::vector<double>> r (p.ref.size()), t (p.ref.size());
        for (std::size_t c = 0; c < p.ref.size(); ++c)
        {
            r[c].assign (p.ref[c].begin(), p.ref[c].end());
            t[c].assign (p.test[c].begin(), p.test[c].end());
        }
        if (! io::writeWav (d + "/REF.wav", r, kSr, 32, true) || ! io::writeWav (d + "/T.wav", t, kSr, 32, true))
        {
            std::fprintf (stderr, "cannot write %s (the directory must exist)\n", d.c_str());
            return 2;
        }
    }
    return 0;
}

struct Oracle { const char* name; double odg; std::array<double, 11> mov; };
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();   // GstPEAQ printed nan
// gst-launch-1.0 peaq console-output=true on the pairs `--write` produces (GstPEAQ, black box).
const Oracle kGst[] {
    { "clip", -3.908, { 658.449198, 657.951872, 12.379035, 49.830959, 2.805852, 1.523124, 49.689908, 136.496210, 12.220666, 0.976761, 0.732620 } },
    { "dropout", -1.404, { 662.911765, 662.235294, -11.652097, 52.809046, 3.014672, 0.047138, 13.616809, 34.888917, 3.441026, 0.650074, 0.037433 } },
    { "gap-lowpass", -0.982, { 717.537267, 659.913043, -6.622930, 0.940279, 2.134004, 0.172777, 0.673729, 0.495280, 0.085776, 0.999955, 0.818182 } },
    { "leadin-long", -2.217, { 657.503311, 657.503311, -6.153153, 10.673520, 1.948900, 0.200452, 10.003723, 24.856961, 0.801938, 0.999507, 0.751656 } },
    { "leadin", -2.191, { 656.858824, 656.858824, -6.253159, 10.553441, 1.948086, 0.203561, 9.958050, 24.490092, 0.788291, 0.998444, 0.788235 } },
    { "mono-clip", -3.908, { 657.000000, 656.994652, 12.842578, 49.510433, 2.747974, 1.753591, 49.331147, 142.827336, 8.808235, 0.976164, 0.743316 } },
    { "near-identical", 0.197, { 657.962567, 657.962567, -112.373630, 0.000090, 0.000000, 0.291834, 0.000081, 0.000162, 0.000001, 0.000000, 0.000000 } },
    { "requant", -3.307, { 656.901070, 656.901070, 0.707712, 14.957034, 2.361189, 0.103784, 14.572837, 36.870988, 3.015940, 1.000000, 0.882353 } },
    { "staggered", kNaN, { 825.609195, 751.436782, 55.156181, 205.955432, 3.617134, 0.000407, 74.400866, 2363.701343, kNaN, 0.999967, 0.885027 } },
    { "tail-silence", -1.542, { 655.344697, 655.344697, -8.946313, 8.552682, 1.719316, 0.202625, 7.948123, 19.923373, 0.378590, 0.994715, 0.625000 } },
    { "tonal-noise", 0.028, { 671.002674, 671.002674, -28.859683, 2.172314, 0.000000, 4.079016, 1.928305, 4.675020, 0.025340, 0.012512, 0.000000 } },
    { "tonal-requant", -1.225, { 621.114975, 621.048798, -16.072612, 8.759330, 0.832426, 5.611013, 7.963517, 22.856792, 0.157873, 0.963943, 0.000000 } },
};
const Oracle* gstFor (const char* name)
{
    for (const Oracle& o : kGst) if (std::strcmp (o.name, name) == 0) return &o;
    return nullptr;
}

// MOV agreement with the oracle. EHS within 3 %: the reading of 4.8.1 agrees with GstPEAQ's to 0.0008 on 48 drum
// pairs, not to the last digit. ADB within 0.05 %: its step count truncates e (eq. 78), so a last-digit difference in a
// level moves a band's count by a step. Everything else to the oracle's printed digits. `skip` lists MOVs where this
// implementation follows the text and GstPEAQ does not; those are checked by their own groups.
void againstOracle (const Pair& p, std::initializer_list<int> skip = {})
{
    const Oracle* o = gstFor (p.name);
    test::ok (o != nullptr, std::string (p.name) + ": has an oracle row");
    if (o == nullptr) return;
    const PeaqResult r = run (p.ref, p.test);
    for (int m = 0; m < 11; ++m)
    {
        bool skipped = false;
        for (int k : skip) skipped = skipped || k == m;
        if (skipped) continue;
        const double want = o->mov[(std::size_t) m];
        const double tol = m == (int) PeaqMov::Ehs ? 0.03 * std::fabs (want) + 1e-3
                         : m == (int) PeaqMov::Adb ? 5e-4 * std::fabs (want) + 1e-4
                                                   : 2e-5 * std::fabs (want) + 2e-5;
        test::approx (r.mov[(std::size_t) m], want, tol, std::string (p.name) + ": " + analysis::kPeaqMovNames[m]);
    }
    if (skip.size() == 0) test::approx (r.modelOdg, o->odg, 0.006, std::string (p.name) + ": ODG");
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
        Worst w;
        for (int k = 1; k < Peaq::kBins; ++k)
        {
            const double f = k * 23.4375 / 1000.0;
            const double wd = -0.6 * 3.64 * std::pow (f, -0.8) + 6.5 * std::exp (-0.6 * (f - 3.3) * (f - 3.3)) - 1e-3 * std::pow (f, 3.6);
            const double want = std::pow (10.0, wd / 10.0);
            if (want > 1e-300) w.add (std::fabs (q.outerEarWeight (k) - want) / want);
        }
        test::ok (w.v < 1e-12 && q.outerEarWeight (0) == 0.0, "outer and middle ear, eq. 7, every line (worst relative " + num (w.v) + ")");
        Worst wn, ws;
        for (int b = 0; b < Peaq::kBands; ++b)
        {
            const double fc = analysis::peaq_detail::kBandCentreHz[b];
            const double n = std::pow (10.0, 0.4 * 0.364 * std::pow (fc / 1000.0, -0.8));
            const double sl = std::pow (10.0, (-2.0 - 2.05 * std::atan (fc / 4000.0) - 0.75 * std::atan ((fc / 1600.0) * (fc / 1600.0))) / 10.0);
            wn.add (std::fabs (q.internalNoise (b) - n) / n);
            ws.add (std::fabs (q.loudnessIndex (b) - sl) / sl);
        }
        test::ok (wn.v < 1e-13, "internal noise, eq. 13, every band (worst relative " + num (wn.v) + ")");
        test::ok (ws.v < 1e-13, "threshold index s, eq. 61, every band (worst relative " + num (ws.v) + ")");
        // NormSP, eqs. 19-20, summed directly: the spread of a 0 dB pattern, lower slope 27 dB/Bark, upper -24 - 230/fc.
        Worst wsp;
        for (int k = 0; k < Peaq::kBands; ++k)
        {
            double sum = 0.0;
            for (int j = 0; j < Peaq::kBands; ++j)
            {
                const double su = -24.0 - 230.0 / analysis::peaq_detail::kBandCentreHz[j];
                double den = 0.0;
                for (int v = 0; v < Peaq::kBands; ++v)
                    den += v < j ? std::pow (10.0, -0.25 * (j - v) * 27.0 / 10.0) : std::pow (10.0, 0.25 * (v - j) * su / 10.0);
                const double num1 = k < j ? std::pow (10.0, -0.25 * (j - k) * 27.0 / 10.0) : std::pow (10.0, 0.25 * (k - j) * su / 10.0);
                sum += std::pow (num1 / den, 0.4);
            }
            const double want = std::pow (sum, 1.0 / 0.4);
            wsp.add (std::fabs (q.spreadNorm (k) - want) / want);
        }
        test::ok (wsp.v < 1e-11, "NormSP, eqs. 19-20, against a direct double sum (worst relative " + num (wsp.v) + ")");
        // 2.1.3: the window is scaled so that a 0 dBFS sine at 1019.5 Hz peaks at 92 dB SPL over 10 frames. Two lines
        // (43 and 44, either side of it) by a direct DFT with the system libm.
        double peak = 0.0;
        for (int fr = 0; fr < 10; ++fr)
            for (int kk = 43; kk <= 44; ++kk)
            {
                double re = 0.0, im = 0.0;
                for (int n = 0; n < Peaq::kFrame; ++n)
                {
                    const double x = q.windowSample (n) * std::sin (6.283185307179586 * 1019.5 * (double) (fr * 1024 + n) / kSr);
                    re += x * std::cos (6.283185307179586 * kk * n / 2048.0);
                    im -= x * std::sin (6.283185307179586 * kk * n / 2048.0);
                }
                peak = std::max (peak, std::sqrt (re * re + im * im));
            }
        test::approx (20.0 * std::log10 (peak), 92.0, 1e-9, "the playback level, eqs. 5-6: the calibration sine peaks at 92 dB");
        Worst wa;
        for (int i = -4000; i <= 4000; ++i)
        {
            const double x = i * 0.0137;
            wa.add (std::fabs (analysis::peaq_detail::atan (x) - std::atan (x)));
        }
        test::ok (wa.v < 2e-15, "the IEEE-only atan against libm's, |x| <= 55, within 4 ulp of pi/2 (worst " + num (wa.v) + ")");
        test::ok (Peaq::disturbedFrame (core::det::pow10 (0.15)) && ! Peaq::disturbedFrame (std::nextafter (core::det::pow10 (0.15), 0.0)),
                  "4.6: a band at exactly 1.5 dB disturbs the frame (>=), one ulp under does not");
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
    test::group ("the oracle: GstPEAQ on the same files — clip, lead-in, requant, two tonal pairs, long lead-in, "
                 "trailing silence, a 100 ms dropout, mono");
    for (int i : { 0, 1, 3, 5, 6, 8, 9, 10, 11 }) againstOracle (ps[(std::size_t) i]);

    test::group ("the bandwidth of a frame whose test is digitally silent follows the text, not the oracle (4.4.1)");
    {
        // A test silent from start to end under a playing reference: ZeroThreshold is -inf in every frame, every
        // comparison holds at the first line tried, and both bandwidths read 921 and 920 + 1 = 921 lines.
        Planar silent (2, std::vector<float> ((std::size_t) kLen, 0.0f));
        const PeaqResult r = run (ps[0].ref, silent);
        test::ok (r.mov[(std::size_t) PeaqMov::BandwidthRef] == 921.0 && r.mov[(std::size_t) PeaqMov::BandwidthTest] == 921.0,
                  "a silent test: BandwidthRefB = BandwidthTestB = 921 (" + num (r.mov[0]) + ", " + num (r.mov[1]) + ")");
        // A dropout of the test under a playing reference reads the same in GstPEAQ (the dropout pair agrees in full,
        // above). Where both programmes are digitally silent, GstPEAQ does not count the frame and the text does: the
        // gap pair agrees with it in everything but the two bandwidths, which carry those frames at 921 lines.
        againstOracle (ps[2], { (int) PeaqMov::BandwidthRef, (int) PeaqMov::BandwidthTest });
        const PeaqResult g = run (ps[2].ref, ps[2].test);
        const Oracle* o = gstFor ("gap-lowpass");
        test::ok (o != nullptr && g.mov[(std::size_t) PeaqMov::BandwidthRef] > o->mov[(std::size_t) PeaqMov::BandwidthRef]
                  && g.mov[(std::size_t) PeaqMov::BandwidthTest] > o->mov[(std::size_t) PeaqMov::BandwidthTest],
                  "gap: the silent frames count, so both bandwidths sit above GstPEAQ's (" + num (g.mov[0]) + ", " + num (g.mov[1])
                  + " against " + (o != nullptr ? num (o->mov[0]) + ", " + num (o->mov[1]) : std::string ("?")) + ")");
    }

    test::group ("the loudness threshold binds: each programme reaches 0.1 sone once, at different times (5.2.4.2)");
    {
        const PeaqResult r = run (ps[7].ref, ps[7].test);
        test::ok (std::isfinite (r.mov[(std::size_t) PeaqMov::RmsNoiseLoud]),
                  "staggered: RmsNoiseLoudB is measured from 50 ms after the later mark (" + num (r.mov[8]) + ")");
        // GstPEAQ agrees on every other MOV and prints nan here: it waits for both programmes above the threshold in
        // one frame, which this pair never has. The text says the loudness "has once reached" 0.1 sone for both —
        // two moments, not one — and this implementation follows the text.
        againstOracle (ps[7], { (int) PeaqMov::RmsNoiseLoud });
        const Oracle* o = gstFor ("staggered");
        test::ok (o != nullptr && std::isnan (o->mov[(std::size_t) PeaqMov::RmsNoiseLoud]), "staggered: GstPEAQ's RmsNoiseLoudB is nan");
    }

    test::group ("the tail: 192500 samples are 186 full frames and one frame completed with zeros");
    {
        const PeaqResult r = run (ps[0].ref, ps[0].test);
        test::ok (r.frames == 187 && r.lastFrame == 186, "187 frames, the last one counted (" + std::to_string (r.frames) + ")");
        const PeaqResult b = run (ps[1].ref, ps[1].test);
        test::ok (b.firstFrame == 17, "lead-in: the data boundary starts at the first frame reaching sample 19200 (" + std::to_string (b.firstFrame) + ")");
        const PeaqResult t = run (ps[9].ref, ps[9].test);
        test::ok (t.lastFrame == 131, "trailing silence: the boundary ends at the last frame reaching sample 134899 (" + std::to_string (t.lastFrame) + ")");
    }

    test::group ("pins: this implementation's own grade (relative 1e-9 — a change here is a change of the model)");
    {
        const std::pair<int, double> pin[] { { 0, -3.9082420313146327 }, { 1, -2.1913005809968773 }, { 2, -1.0012821225645525 }, { 3, -3.3072669063523081 }, { 5, -1.2276055382810176 }, { 6, 0.027665468898018464 }, { 7, -3.9105748326784164 }, { 11, -3.9075643242176112 } };
        for (const auto& [i, want] : pin)
        {
            const PeaqResult r = run (ps[(std::size_t) i].ref, ps[(std::size_t) i].test);
            test::approx (r.modelOdg, want, 1e-9 * std::fabs (want), std::string (ps[(std::size_t) i].name) + ": pinned ODG");
        }
    }

    // ---------------------------------------------------------------------------------------------- verdicts
    // The network was never asked about a test that IS the reference: on a real master that left the programme alone
    // (max-measure, children-of-test-tubes at three loudness steps, Total NMR -103 dB) GstPEAQ and this model both read
    // EHS 52-67 instead of < 1 and grade -2.1..-2.2. That programme cannot ship in a public suite; the pairs below hold
    // the rule itself.
    test::group ("transparent input: every channel under the NMR line and its waveform error small -> 0");
    {
        const PeaqResult r = run (ps[4].ref, ps[4].test);
        test::ok (r.verdict == PeaqVerdict::Transparent && r.odg == 0.0, "near-identical: Transparent, ODG 0 (GstPEAQ: 0.197)");
        test::ok (r.channelNmrDb[0] < Peaq::kTransparentNmrDb && r.channelNmrDb[1] < Peaq::kTransparentNmrDb,
                  "both channels under the line (" + num (r.channelNmrDb[0]) + ", " + num (r.channelNmrDb[1]) + " dB)");
        test::ok (std::isfinite (r.modelOdg) && r.modelOdg != 0.0, "the network's own grade is kept as modelOdg (" + num (r.modelOdg) + ")");

        const PeaqResult same = run (ps[0].ref, ps[0].ref);
        bool zero = true;
        for (PeaqMov m : { PeaqMov::WinModDiff1, PeaqMov::Adb, PeaqMov::Ehs, PeaqMov::AvgModDiff1, PeaqMov::AvgModDiff2,
                           PeaqMov::RmsNoiseLoud, PeaqMov::Mfpd, PeaqMov::RelDistFrames })
            zero = zero && same.mov[(std::size_t) m] == 0.0;
        test::ok (same.verdict == PeaqVerdict::Transparent && same.odg == 0.0 && zero,
                  "the reference against itself: Transparent, and every error MOV exactly 0 (EHS " + num (same.mov[5]) + ")");

        Planar right = ps[0].ref;
        for (float& v : right[1]) v = (float) (0.999 * (double) v);
        const PeaqResult one = run (ps[0].ref, right);
        test::ok (one.verdict == PeaqVerdict::Graded && one.channelNmrDb[1] > Peaq::kTransparentNmrDb
                  && one.mov[(std::size_t) PeaqMov::TotalNmr] < Peaq::kTransparentNmrDb,
                  "left untouched, right at 0.999x: the mean NMR is under the line (" + num (one.mov[2]) + " dB), the right channel "
                  "is not (" + num (one.channelNmrDb[1]) + " dB) — Graded");
        Planar flipped = ps[0].ref;
        for (float& v : flipped[1]) v = -v;
        const PeaqResult fl = run (ps[0].ref, flipped);
        test::ok (fl.verdict == PeaqVerdict::Graded && fl.channelNmrDb[1] < Peaq::kTransparentNmrDb,
                  "right channel's polarity flipped: the magnitude spectra see nothing (NMR " + num (fl.channelNmrDb[1])
                  + " dB), the waveform error does — Graded");

        // Around the line: the reference plus noise at falling levels (channel NMR about -78, -84, -92, -98, -104 dB).
        // Each verdict must match its own channel NMRs.
        bool consistent = true, sawGraded = false, sawTransparent = false;
        for (double amp : { 1e-6, 5e-7, 2e-7, 1e-7, 5e-8 })
        {
            Planar t = ps[0].ref;
            addNoise (t, amp, 2024u);
            const PeaqResult a = run (ps[0].ref, t);
            const bool under = a.channelNmrDb[0] < Peaq::kTransparentNmrDb && a.channelNmrDb[1] < Peaq::kTransparentNmrDb;
            consistent = consistent && ((a.verdict == PeaqVerdict::Transparent) == under) && a.graded();
            sawGraded = sawGraded || a.verdict == PeaqVerdict::Graded;
            sawTransparent = sawTransparent || a.verdict == PeaqVerdict::Transparent;
        }
        test::ok (consistent && sawGraded && sawTransparent, "noise from -120 to -146 dBFS: Transparent exactly when both channels are under the line, both outcomes seen");
    }

    test::group ("Undefined comes before Transparent; no signal, non-finite and out-of-range input; mono = dual mono");
    {
        Planar shortRef (2, std::vector<float> (4000, 0.0f));
        for (auto& ch : shortRef) for (std::size_t i = 0; i < ch.size(); ++i) ch[i] = (float) (0.3 * core::det::sin (0.1 * (double) i));
        const PeaqResult u = run (shortRef, shortRef);
        test::ok (u.verdict == PeaqVerdict::Undefined && ! u.graded() && std::isnan (u.mov[(std::size_t) PeaqMov::WinModDiff1]),
                  "4000 identical samples, under 0.5 s: Undefined (the modulation MOVs have no frame), not Transparent");
        Planar sine (2, std::vector<float> ((std::size_t) kLen, 0.0f));
        for (auto& ch : sine) for (std::size_t i = 0; i < ch.size(); ++i) ch[i] = (float) (0.3 * core::det::sin (6.283185307179586 * 1000.0 * (double) i / kSr));
        Planar sineClip = sine;
        for (auto& ch : sineClip) for (float& v : ch) v = v > 0.2f ? 0.2f : (v < -0.2f ? -0.2f : v);
        const PeaqResult nb = run (sine, sineClip);
        test::ok (nb.verdict == PeaqVerdict::Undefined && std::isnan (nb.mov[(std::size_t) PeaqMov::BandwidthRef]),
                  "a 1 kHz sine never reaches 346 lines: the bandwidths are NaN and the grade Undefined");

        Planar silent (2, std::vector<float> ((std::size_t) kLen, 0.0f));
        const PeaqResult s = run (silent, ps[0].test);
        test::ok (s.verdict == PeaqVerdict::NoSignal && ! s.graded() && s.firstFrame == -1, "a silent reference: NoSignal");
        Planar shortSilent (2, std::vector<float> (100, 0.0f));
        test::ok (run (shortSilent, shortSilent).verdict == PeaqVerdict::NoSignal, "100 silent samples: NoSignal");
        Planar nan = ps[0].test;
        nan[1][5000] = std::numeric_limits<float>::quiet_NaN();
        const PeaqResult n = run (ps[0].ref, nan);
        test::ok (n.verdict == PeaqVerdict::NonFinite && n.nonFiniteSamples == 1 && ! n.graded(), "one NaN: NonFinite, counted");
        for (float big : { 9.0f, 1e19f, 1e37f })
        {
            Planar loud = ps[0].ref, loudT = ps[0].test;
            loud[0][7000] = big;
            loudT[0][7000] = big;
            const PeaqResult o = run (loud, loudT);
            test::ok (o.verdict == PeaqVerdict::OutOfRange && o.outOfRangeSamples == 1 && ! o.graded(),
                      "one sample at " + num (big) + " in both: OutOfRange (the model holds to +18 dBFS)");
        }
        Planar edge = ps[0].ref, edgeT = ps[0].test;
        edge[0][7000] = (float) Peaq::kMaxAbsSample;
        edgeT[0][7000] = (float) Peaq::kMaxAbsSample;
        test::ok (run (edge, edgeT).graded(), "a sample at exactly kMaxAbsSample is still inside the range");
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
        test::ok (all && whole.graded(), "one call, and blocks of 1, 7, 1023, 1024, 1025, 4096, 65537");
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
        test::ok (sameResult (first, q.result()) && first.frames == 5, "reset() replays the programme to the same bits");
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
        const long long before = alloc::count.load();
        bool accepted = true;
        for (long pos = 0; pos + 777 <= kLen; pos += 777)
        {
            const float* rp[2] { ps[3].ref[0].data() + pos, ps[3].ref[1].data() + pos };
            const float* tp[2] { ps[3].test[0].data() + pos, ps[3].test[1].data() + pos };
            accepted = accepted && q.process (rp, tp, 2, 777);
        }
        q.finish();
        q.reset();
        const long long after = alloc::count.load();   // read before any message string exists
        // A plain check, not okNoAlloc: that helper passes unconditionally off libc++, and this counter counts every
        // form of `new` on every standard library this repository builds with.
        test::ok (accepted && after == before, "no allocation in 247 process calls, finish and reset ("
                  + std::to_string (after - before) + " allocations)");
    }

    return test::report();
}
