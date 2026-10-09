// SPDX-License-Identifier: AGPL-3.0-or-later
#include <alloc_counter.h>
#include <cstring>
#include <felitronics/storage/Buffer.h>
#include <felitronics/storage/VectorBytes.h>
#include <felitronics/analysis/ProgrammeReport.h>
#include <felitronics/analysis/HumDetector.h>
#include <felitronics/analysis/LowEnd.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/PeakExcursions.h>
#include <felitronics/analysis/ClipDetector.h>
#include <felitronics/analysis/StereoColumns.h>
#include <felitronics/analysis/WaveformPeaks.h>
#include <felitronics/analysis/SourceForensics.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include <felitronics/mastering/DeliveryConverter.h>
#include <felitronics/tempo/TempoDetector.h>

#if defined(_MSVC_STL_VERSION) && defined(_DEBUG)
static_assert (_ITERATOR_DEBUG_LEVEL == 2, "Debug storage controls require full iterator debugging");
#endif

using namespace felitronics;

#include "ConfigureSequences.h"

template <class T>
void constructor (std::uint64_t expected, const char* name)
{
    const auto before = test::alloc::bytes.load();
    { T object; (void) object; }
    const auto got = test::alloc::bytes.load() - before;
    std::printf ("  %s: %lld bytes, published %llu\n", name, got, (unsigned long long) expected);
    test::ok (got == (long long) expected, name);
}

void configureMatchesPreparation()
{
    mastering::MasteringChain a, b;
    mastering::MasteringChainConfig cfg;
    cfg.clipper = cfg.monoBass = cfg.stereoAir = true;
    test::ok (a.prepare (48000.0, 2, cfg) && b.prepare (48000.0, 2, cfg), "two prepared full chains");
    constexpr int n = 4096;
    std::vector<float> x (2 * n), y;
    const auto fill = [&]
    {
        for (int i = 0; i < 2 * n; ++i) x[(std::size_t) i] = (float) (i % 137 - 68) / 100.0f;
        y = x;
    };
    fill();
    float* ax[] { x.data(), x.data() + n };
    float* bx[] { y.data(), y.data() + n };
    test::ok (a.process (ax, 2, n) && b.process (bx, 2, n), "both chains have audio state to discard");
    mastering::MasteringChainParams p;
    p.inputGainDb = 3.0;
    p.preLimiterGainDb = -2.0;
    p.compressor.thresholdDb = -18.0;
    p.compressor.ratio = 4.0;
    p.clipper.driveDb = 6.0f;
    p.monoBass.frequencyHz = 180.0f;
    p.monoBass.lowWidth = 0.3f;
    p.eqBands[0].on = true;
    p.eqBands[0].lane (eq::Lane::Stereo).gainDb = 6.0;
    p.eqBands[0].lane (eq::Lane::Stereo).freq = 120.0;
    const auto configureBudget = a.configureBytes();
    const auto before = test::alloc::bytes.load();
    const bool configured = a.configure (p);
    const auto configureSpent = test::alloc::bytes.load() - before;
    test::ok (configured && configureSpent == (long long) configureBudget,
              "configure publishes the re-preparation's temporary proxies exactly");
    b.setParams (p);
    const auto budget = b.reprepareBytes (48000.0, 2, cfg);
    const auto bytes = test::alloc::bytes.load();
    const bool prepared = b.prepare (48000.0, 2, cfg);
    const auto spent = test::alloc::bytes.load() - bytes;
    test::ok (prepared && spent == (long long) budget, "explicit reprepare publishes its temporary proxies exactly");
    fill();
    ax[0] = x.data(); ax[1] = x.data() + n;
    bx[0] = y.data(); bx[1] = y.data() + n;
    test::ok (a.process (ax, 2, n) && b.process (bx, 2, n)
                  && std::memcmp (x.data(), y.data(), x.size() * sizeof (float)) == 0,
              "configure renders the same samples as a full preparation with new parameters");
}

// THE START CLIPPER ON A FRESH CHAIN: prepare() allocates what prepareBytes() publishes, byte for byte, the MSVC
// Debug proxies included. This suite runs with iterator debugging; the chain's own budget matrix does not.
void startClipperPreparation()
{
    mastering::MasteringChainConfig cfg;
    cfg.startClipper = true;
    for (const int nch : { 1, 2 })
    {
        const auto budget = mastering::MasteringChain::prepareBytes (48000.0, nch, cfg);
        mastering::MasteringChain chain;
        const auto before = test::alloc::bytes.load();
        const bool prepared = chain.prepare (48000.0, nch, cfg);
        const auto got = test::alloc::bytes.load() - before;
        std::printf ("  start clipper chain, %d ch: %lld bytes, published %llu\n", nch, got, (unsigned long long) budget);
        test::ok (prepared && budget > 0u && got == (long long) budget,
                  "a fresh chain with the start clipper allocates its prepareBytes(), to the byte");
    }
}

void analyzerStorage()
{
    const auto exercise = []<class T> (const char* name, std::uint64_t declared, auto prepare)
    {
        const auto before = test::alloc::bytes.load();
        bool prepared = false;
        { T object; prepared = prepare (object); }
        const auto got = test::alloc::bytes.load() - before;
        std::printf ("  %s: %lld bytes, published %llu\n", name, got, (unsigned long long) declared);
        test::ok (prepared && got <= (long long) declared, name);
    };
    exercise.template operator()<analysis::WaveformPeaks> ("waveform first preparation",
        analysis::WaveformPeaks::storageFor (48000, 2, 48000, 1100).firstBytes(),
        [] (auto& a) { return a.prepare (48000, 2, 48000, 1100); });
    exercise.template operator()<analysis::StereoColumns> ("stereo columns first preparation",
        analysis::StereoColumns::storageFor (2, 48000, 1100).firstBytes(),
        [] (auto& a) { return a.prepare (2, 48000, 1100); });
    exercise.template operator()<analysis::BandCrest> ("band crest first preparation",
        analysis::BandCrest::storageFor (48000, 2, 48000, {}).firstBytes(),
        [] (auto& a) { return a.prepare (48000, 2, 48000); });
    exercise.template operator()<analysis::PeakExcursions> ("peak excursions first preparation",
        analysis::PeakExcursions::storageFor (48000, 2, {}).firstBytes(),
        [] (auto& a) { return a.prepare (48000, 2); });
    exercise.template operator()<analysis::ClipDetector> ("clip detector first preparation",
        analysis::ClipDetector::storageFor (48000, 2, analysis::ClipDetectorParams {}.maxRuns).firstBytes(),
        [] (auto& a) { return a.prepare (48000, 0, 2); });
    test::ok (!analysis::WaveformPeaks::storageFor (0, 2, 100).ok
        && !analysis::WaveformPeaks::storageFor (48000, 2, 100, 0).ok
        && !analysis::StereoColumns::storageFor (2, 0).ok
        && !analysis::StereoColumns::storageFor (0, 100).ok, "shape budgets refuse the preparation domain");
    const auto small = analysis::StereoColumns::storageFor (1, 2, 1100);
    test::ok (small.ok && small.columns == 2 && small.bytes() == 24, "columns price the actual short source");
}

int main()
{
    constructor<std::vector<double>> (storage::kVectorProxyBytes, "one STL vector proxy");
    constructor<oversampling::PolyphaseOversampler> (storage::kPolyphaseProxies * storage::kVectorProxyBytes, "polyphase: six vectors");
    constructor<analysis::LoudnessMeter> (storage::kLoudnessProxies * storage::kVectorProxyBytes, "loudness: three vectors");
    constructor<analysis::ReferenceTruePeakMeter> ((1u + storage::kPolyphaseProxies * core::kMaxChannels) * storage::kVectorProxyBytes,
                                                  "reference peak: all channel slots and scratch");
    constructor<analysis::ProgrammeReport> (analysis::ProgrammeReport::constructBytes(), "programme report constructor");
    constructor<analysis::SpectrumFrames> (analysis::SpectrumFrames::constructBytes(), "spectrum constructor");
    constructor<analysis::HumDetector> (analysis::HumDetector::constructBytes(), "hum constructor");
    constructor<analysis::SourceForensics> (analysis::SourceForensics::constructBytes(), "forensics constructor");
    constructor<analysis::LowEnd> (analysis::LowEnd::constructBytes(), "low-end constructor");
    constructor<tempo::TempoDetector> (tempo::TempoDetector::constructBytes(), "tempo constructor");
    constructor<mastering::MasteringChain> (mastering::MasteringChain::constructBytes(), "chain constructor");
    constructor<mastering::TargetLoudnessSolver> (mastering::TargetLoudnessSolver::constructBytes(), "solver constructor");
    constructor<mastering::DeliveryConverter> (mastering::DeliveryConverter::constructBytes(), "delivery constructor");
    constructor<mastering::LoudnessSolution> (mastering::TargetLoudnessSolver::solutionConstructBytes(), "solution constructor");

    constructor<storage::Buffer<double>> (0, "empty owned buffer allocates nothing");
    storage::Buffer<double> a;
    const auto before = test::alloc::bytes.load();
    a.assign (4096, 2.0);
    const auto got = test::alloc::bytes.load() - before;
    test::ok (got == 4096 * (long long) sizeof (double), "one exact array, including at the STL alignment threshold");
    const auto calls = test::alloc::count.load();
    a.assign (8, 3.0);
    a.resize (4096);
    storage::Buffer<double> b (std::move (a));
    a = std::move (b);
    const bool quiet = test::alloc::count.load() == calls;
    test::ok (quiet && a[0] == 3.0 && a[8] == 0.0 && b.empty() && b.capacity() == 0,
              "shrink, regrow and move retain storage without allocating");
    const auto growBefore = test::alloc::bytes.load();
    a.assign (4097, 3.0);
    const auto growBytes = test::alloc::bytes.load() - growBefore;
    test::ok (growBytes == 4097 * (long long) sizeof (double) && a.capacity() == 4097,
              "growth past capacity requests exactly the new size");
    storage::Buffer<double> c (a);
    c[0] = 4.0;
    test::ok (a[0] == 3.0 && c[0] == 4.0 && c.size() == a.size(), "a copy owns its own array");
    analyzerStorage();
    startClipperPreparation();
    configureMatchesPreparation();
    configureSequences();
    return test::report();
}
