// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fcore_peaq — PEAQ Basic (ITU-R BS.1387) of a test programme against its reference, through analysis::Peaq.
//
//   fcore_peaq <ref.wav> <test.wav>
//   fcore_peaq --f32le <sampleRate> <channels> <ref.f32le> <test.f32le>
//
// The two programmes must be time-aligned and have the same channel count (1 or 2), rate and length: a length
// mismatch is refused, never silently cut to the common part (a test missing its second half would grade the half it
// kept). A rate other than 48 kHz is converted to 48 kHz by core::DeliveryResampler — the same converter, the same
// plan, for both — before the model sees it. NOTE: the converter designs its kernel through the system libm, so on a
// non-48 kHz input the bits may differ between rows (native vs wasm, glibc vs Apple); at 48 kHz they do not.
//
// MEMORY. Both programmes are held whole: the WAV file, its decoded doubles and the float planes. The tool estimates
// that from the file sizes before reading and refuses past kMaxToolBytes (2 GiB) — about 50 minutes of stereo float.
//
// Prints the verdict, the reported ODG, the network's ODG and DI, and the eleven MOVs, every number with 17
// significant digits: the same source compiled for wasm (tools/wasm/build.sh, fcpeaq.node.js) must print the same
// text, and CI diffs the two.
//
// Exit codes: 0 graded (Graded or Transparent), 1 not graded (NoSignal, NonFinite, Undefined, OutOfRange), 2 bad
// arguments or input.

#include <felitronics/analysis/Peaq.h>
#include <felitronics/core/DeliveryResampler.h>
#include <felitronics/io/Wav.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using Planar = std::vector<std::vector<float>>;
constexpr std::uint64_t kMaxToolBytes = 2ull << 30;

// The file's size; a size the platform's `long` cannot hold (past 2 GiB on wasm32) reads as too large. (No buffer on
// the stack: the wasm build's stack is 64 KiB.)
std::uint64_t fileBytes (const char* path)
{
    std::FILE* f = std::fopen (path, "rb");
    if (f == nullptr) return 0;
    const bool sought = std::fseek (f, 0, SEEK_END) == 0;
    const long n = sought ? std::ftell (f) : -1L;
    std::fclose (f);
    return n < 0 ? ~0ull : (std::uint64_t) n;
}

// The peak the tool will hold for one programme read from `bytes` of file: the file itself, its decoded doubles
// (at worst 8 per 2-byte sample) and the float planes, plus a 48 kHz copy when it is converted.
std::uint64_t peakFor (std::uint64_t bytes, bool wav) { return wav ? bytes + 4u * bytes + 2u * bytes + 2u * bytes : 3u * bytes; }

struct Input
{
    Planar ch;
    double sr = 0.0;
    bool ok = false;
};

Input readWavFile (const char* path)
{
    Input in;
    const auto w = felitronics::io::readWav (path);
    if (! w.ok) { std::fprintf (stderr, "%s: %s\n", path, w.error.c_str()); return in; }
    in.sr = w.sr;
    in.ch.resize (w.ch.size());
    for (std::size_t c = 0; c < w.ch.size(); ++c)
    {
        in.ch[c].resize (w.ch[c].size());
        for (std::size_t i = 0; i < w.ch[c].size(); ++i) in.ch[c][i] = (float) w.ch[c][i];
    }
    in.ok = true;
    return in;
}

Input readF32 (const char* path, double sr, int channels)
{
    Input in;
    std::FILE* f = std::fopen (path, "rb");
    if (f == nullptr) { std::fprintf (stderr, "%s: cannot open\n", path); return in; }
    std::vector<float> inter;
    float buf[4096];
    std::size_t got;
    while ((got = std::fread (buf, sizeof (float), 4096, f)) > 0) inter.insert (inter.end(), buf, buf + got);
    std::fclose (f);
    const std::size_t n = inter.size() / (std::size_t) channels;
    in.ch.assign ((std::size_t) channels, std::vector<float> (n));
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < channels; ++c) in.ch[(std::size_t) c][i] = inter[i * (std::size_t) channels + (std::size_t) c];
    in.sr = sr;
    in.ok = true;
    return in;
}

// Whole-programme conversion to 48 kHz, group delay drained by flush(): both programmes take the same plan, so their
// alignment is kept.
bool to48k (Input& in)
{
    using felitronics::core::DeliveryResampler;
    if (in.sr == felitronics::analysis::Peaq::kSampleRate) return true;
    DeliveryResampler::Params p;
    p.inRate = in.sr;
    p.outRate = felitronics::analysis::Peaq::kSampleRate;
    const int nch = (int) in.ch.size();
    constexpr int kBlock = 4096;
    DeliveryResampler rs;
    if (! rs.prepare (p, nch, kBlock)) { std::fprintf (stderr, "no 48 kHz conversion from %g Hz\n", in.sr); return false; }
    const int cap = rs.maxOutputFor (kBlock) > rs.maxFlushOutput() ? rs.maxOutputFor (kBlock) : rs.maxFlushOutput();
    Planar out ((std::size_t) nch), tmp ((std::size_t) nch, std::vector<float> ((std::size_t) cap));
    std::vector<const float*> ip ((std::size_t) nch);
    std::vector<float*> op ((std::size_t) nch);
    for (int c = 0; c < nch; ++c) op[(std::size_t) c] = tmp[(std::size_t) c].data();
    const std::size_t n = in.ch[0].size();
    for (std::size_t pos = 0; pos < n; pos += kBlock)
    {
        const int len = (int) (n - pos < (std::size_t) kBlock ? n - pos : (std::size_t) kBlock);
        for (int c = 0; c < nch; ++c) ip[(std::size_t) c] = in.ch[(std::size_t) c].data() + pos;
        int got = 0;
        if (! rs.process (ip.data(), nch, len, op.data(), cap, got)) return false;
        for (int c = 0; c < nch; ++c) out[(std::size_t) c].insert (out[(std::size_t) c].end(), op[(std::size_t) c], op[(std::size_t) c] + got);
    }
    int got = 0;
    if (! rs.flush (nch, op.data(), cap, got)) return false;
    for (int c = 0; c < nch; ++c) out[(std::size_t) c].insert (out[(std::size_t) c].end(), op[(std::size_t) c], op[(std::size_t) c] + got);
    in.ch = std::move (out);
    in.sr = felitronics::analysis::Peaq::kSampleRate;
    return true;
}

const char* verdictName (felitronics::analysis::PeaqVerdict v)
{
    using V = felitronics::analysis::PeaqVerdict;
    switch (v)
    {
        case V::Graded: return "graded";
        case V::Transparent: return "transparent";
        case V::NoSignal: return "no-signal";
        case V::NonFinite: return "non-finite";
        case V::Undefined: return "undefined";
        case V::OutOfRange: return "out-of-range";
        case V::NotRun: break;
    }
    return "not-run";
}
} // namespace

int main (int argc, char** argv)
{
    using namespace felitronics::analysis;
    Input ref, test;
    {
        const bool wav = argc == 3;
        const char* rpath = wav ? argv[1] : (argc == 6 ? argv[4] : nullptr);
        const char* tpath = wav ? argv[2] : (argc == 6 ? argv[5] : nullptr);
        if (rpath != nullptr && tpath != nullptr)
        {
            const std::uint64_t need = peakFor (fileBytes (rpath), wav) + peakFor (fileBytes (tpath), wav);
            if (need > kMaxToolBytes)
            {
                std::fprintf (stderr, "these programmes need about %.2f GiB in this tool, more than its %.0f GiB limit\n",
                              (double) need / (double) (1ull << 30), (double) kMaxToolBytes / (double) (1ull << 30));
                return 2;
            }
        }
    }
    if (argc == 3)
    {
        ref = readWavFile (argv[1]);
        test = readWavFile (argv[2]);
    }
    else if (argc == 6 && std::strcmp (argv[1], "--f32le") == 0)
    {
        const double sr = std::atof (argv[2]);
        const int ch = std::atoi (argv[3]);
        if (! (sr > 0.0) || ch < 1 || ch > Peaq::kMaxChannels) { std::fprintf (stderr, "bad rate or channel count\n"); return 2; }
        ref = readF32 (argv[4], sr, ch);
        test = readF32 (argv[5], sr, ch);
    }
    else
    {
        std::fprintf (stderr, "usage:\n  %s <ref.wav> <test.wav>\n  %s --f32le <sampleRate> <channels> <ref.f32le> <test.f32le>\n",
                      argv[0], argv[0]);
        return 2;
    }
    if (! ref.ok || ! test.ok) return 2;
    if (ref.ch.size() != test.ch.size() || ref.ch.empty() || ref.ch.size() > (std::size_t) Peaq::kMaxChannels)
    {
        std::fprintf (stderr, "reference and test need the same channel count, 1 or 2\n");
        return 2;
    }
    if (ref.sr != test.sr) { std::fprintf (stderr, "reference and test need the same sample rate\n"); return 2; }
    if (ref.ch[0].size() != test.ch[0].size())
    {
        std::fprintf (stderr, "reference has %zu samples per channel, test %zu: PEAQ compares aligned programmes of one "
                              "length — trim or pad them first\n", ref.ch[0].size(), test.ch[0].size());
        return 2;
    }
    if (! to48k (ref) || ! to48k (test)) return 2;

    const int nch = (int) ref.ch.size();
    const std::size_t n = ref.ch[0].size();
    Peaq peaq;
    if (! peaq.prepare (nch, (long long) n)) { std::fprintf (stderr, "cannot prepare for %zu samples\n", n); return 2; }
    constexpr std::size_t kBlock = 8192;
    const float* rp[Peaq::kMaxChannels] {};
    const float* tp[Peaq::kMaxChannels] {};
    for (std::size_t pos = 0; pos < n; pos += kBlock)
    {
        const int len = (int) (n - pos < kBlock ? n - pos : kBlock);
        for (int c = 0; c < nch; ++c) { rp[c] = ref.ch[(std::size_t) c].data() + pos; tp[c] = test.ch[(std::size_t) c].data() + pos; }
        if (! peaq.process (rp, tp, nch, len)) { std::fprintf (stderr, "process refused at %zu\n", pos); return 2; }
    }
    peaq.finish();
    const PeaqResult& r = peaq.result();
    std::printf ("verdict %s\n", verdictName (r.verdict));
    std::printf ("ODG %.17g\n", r.odg);
    std::printf ("modelODG %.17g\n", r.modelOdg);
    std::printf ("DI %.17g\n", r.distortionIndex);
    for (int i = 0; i < kPeaqMovCount; ++i) std::printf ("%s %.17g\n", kPeaqMovNames[i], r.mov[(std::size_t) i]);
    std::printf ("frames %lld first %lld last %lld\n", r.frames, r.firstFrame, r.lastFrame);
    return r.graded() ? 0 : 1;
}
