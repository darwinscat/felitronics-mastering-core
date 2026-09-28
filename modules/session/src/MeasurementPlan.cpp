// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "MeasurementPlan.h"
#include "MeasurementWorkspace.h"
#include "LiveMeasurements.h"
#include "JsonNumber.h"
#include "Rules.h"
#include "BuildContract.h"
#include <felitronics/storage/VectorBytes.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace felitronics::session::detail
{
namespace
{
double number (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    storageOverflow();
}
struct Hash
{
    std::uint64_t value = 0xCBF29CE484222325ull;
    void add (std::uint64_t v) noexcept
    {
        for (unsigned i = 0; i < 8; ++i) value = (value ^ std::uint8_t (v >> (8 * i))) * 0x100000001B3ull;
    }
    void add (double v) noexcept { add (std::bit_cast<std::uint64_t> (v)); }
    void add (int v) noexcept { add (std::uint64_t (v)); }
};
// Each output has at most 128 named scalars and 16 named arrays; names are at most 64 UTF-8 bytes.
// Rows below count complete native output records, widened to at most one f64 per native byte.
// This also covers integer/enum fields without depending on native struct padding or pointer width.
constexpr std::uint64_t metadataBytes = sizeof (MeasurementResult) + 128u * (sizeof (MeasurementValue) + 64u)
                                     + 16u * (sizeof (MeasurementArray) + 64u);
}
MeasurementParameters MeasurementPlan::parametersFor (const Pcm& pcm) noexcept
{
    MeasurementParameters p;
    const auto engine = rules().engine;
    p.programme.maxDurationSec = double (pcm.frames) / double (pcm.sampleRate);
    // Rounding the duration down must never make the native preparation shorter than the source.
    if (p.programme.maxDurationSec * double (pcm.sampleRate) < double (pcm.frames))
        p.programme.maxDurationSec = std::bit_cast<double> (std::bit_cast<std::uint64_t> (p.programme.maxDurationSec) + 1u);
    const auto low = engine.find ("lowEnd");
    const auto run = low.find ("run");
    p.lowEnd.crossoverHz = number (run.find ("crossoverHz"));
    p.lowEnd.lowNoteHz = number (run.find ("lowNoteHz"));
    p.lowEnd.highNoteHz = number (run.find ("highNoteHz"));
    p.lowEnd.fftOrder = int (number (run.find ("fftOrder")));
    p.lowEnd.dutyThresholdDb = number (run.find ("dutyThresholdDb"));
    p.lowEnd.skipBlocks = int (number (run.find ("skipBlocks")));
    const auto blockFrames = std::max<std::uint64_t> (1, std::uint64_t (std::floor (double (pcm.sampleRate) * 0.01 + 0.5)));
    p.lowEnd.maxBlocks = int (std::min<std::uint64_t> (std::uint64_t (p.lowEnd.maxBlocks), pcm.frames / blockFrames + 1u));
    p.clipRuns = int (std::min<std::uint64_t> (std::uint64_t (p.clipRuns), pcm.frames * pcm.channelCount));
    // The all-target source readings include the 150 Hz split alongside the main 120 Hz and infra-low splits.
    p.lowEnd150 = p.lowEnd;
    p.lowEnd150.crossoverHz = 150;
    p.infraLow = p.lowEnd;
    p.infraLow.crossoverHz = number (low.find ("infraLowCrossoverHz"));
    const auto bursts = engine.find ("stereoBursts");
    p.bursts.bandLowHz = number (bursts.find ("bandLowHz"));
    p.bursts.bandHighHz = number (bursts.find ("bandHighHz"));
    p.bursts.hopMs = number (bursts.find ("hopMs"));
    p.bursts.baselineMs = number (bursts.find ("baselineMs"));
    p.bursts.enterDb = number (bursts.find ("enterDb"));
    p.bursts.exitDb = number (bursts.find ("exitDb"));
    p.bursts.maxEvents = int (number (bursts.find ("eventCapacity")));
    const auto burstHop = std::max<std::uint64_t> (1, std::uint64_t (std::floor (double (pcm.sampleRate) * p.bursts.hopMs / 1000.0 + 0.5)));
    // At most one event begins per hop; at most one distinct value arrives per source frame/channel.
    p.bursts.maxEvents = int (std::min<std::uint64_t> (std::uint64_t (p.bursts.maxEvents), pcm.frames / burstHop + 1u));
    p.forensics.maxDistinctValues = int (std::min<std::uint64_t> (std::uint64_t (p.forensics.maxDistinctValues), pcm.frames + 1u));
    const auto humGeometry = analysis::HumDetector::geometryFor (double (pcm.sampleRate), p.hum);
    if (humGeometry.ok)
        p.hum.maxStretches = int (std::min<std::uint64_t> (std::uint64_t (p.hum.maxStretches), pcm.frames / std::uint64_t (humGeometry.hop) + 1u));
    const auto crest = engine.find ("crest");
    for (std::size_t i = 0; i < 3; ++i) p.crest.bandEdgeHz[i] = number (crest.find ("bandEdgesHz")[i]);
    p.crest.hopMs = number (crest.find ("hopMs"));
    p.crest.blockHops = int (number (crest.find ("blockHops")));
    p.crest.programmeFloorDb = number (crest.find ("floorDb"));
    p.crest.bandShareFloorDb = number (crest.find ("bandShareFloorDb"));
    return p;
}
MeasurementPlan MeasurementPlan::storageFor (const Pcm& pcm, const MeasurementParameters& p) noexcept
{
    MeasurementPlan out; out.parameters = p;
    if (pcm.channelCount < 1 || pcm.channelCount > 2 || pcm.sampleRate < kMinSampleRate || pcm.frames == 0
        || pcm.frames > std::uint64_t (std::numeric_limits<std::size_t>::max()) / sizeof (float) / pcm.channelCount
        || pcm.frames > 9007199254740991ull / sizeof (float) / pcm.channelCount)
    { out.rejection = Rejection::TooLong; return out; }
    const double rate = double (pcm.sampleRate);
    const int channels = int (pcm.channelCount);
    const auto set = [&] (Analyzer id, bool ok, std::uint64_t workspace, std::uint64_t rows)
    {
        auto& a = out.analyzers[std::size_t (id)];
        a.available = ok;
        a.workspace = ok ? workspace : 0;
        a.rowValues = ok ? rows : 0;
        a.result = metadataBytes + a.rowValues * sizeof (double);
    };
    const auto report = analysis::ProgrammeReport::storageFor (rate, p.maxBlock, channels, p.programme);
    set (Analyzer::Programme, report.ok, sizeof (analysis::ProgrammeReport) + report.firstBytes(), 0);
    analysis::DeterministicLoudnessMeter::Storage meter {};
    const bool loudness = analysis::DeterministicLoudnessMeter::storageFor (rate, p.programme.maxDurationSec * rate, meter);
    const auto sub = std::max<std::uint64_t> (1, std::uint64_t (std::floor (rate * 0.01 + 0.5)));
    set (Analyzer::Loudness, loudness, sizeof (analysis::DeterministicLoudnessMeter) + meter.bytes()
         + storage::kLoudnessProxies * storage::kVectorProxyBytes, 4u * (pcm.frames / (10u * sub) + 1u));
    const auto clips = analysis::ClipDetector::storageFor (rate, channels, p.clipRuns);
    set (Analyzer::Clipping, clips.ok, sizeof (analysis::ClipDetector) + clips.firstBytes(), clips.runEntries * 6u);
    const std::array lowIds { Analyzer::LowEnd, Analyzer::InfraLow, Analyzer::LowEnd150 };
    const std::array lowParameters { p.lowEnd, p.infraLow, p.lowEnd150 };
    for (std::size_t i = 0; i < lowIds.size(); ++i)
    {
        const auto low = analysis::LowEnd::storageFor (rate, channels, lowParameters[i]);
        set (lowIds[i], low.ok, sizeof (analysis::LowEnd) + low.firstBytes(),
             low.blockRecords * 8u + low.bands * 12u + analysis::LowEnd::kHistogramBins);
    }
    const auto forensics = analysis::SourceForensics::storageFor (rate, channels, p.forensics);
    set (Analyzer::Forensics, forensics.ok, sizeof (analysis::SourceForensics) + forensics.firstBytes(),
         (pcm.channelCount + 1u) * (sizeof (analysis::SpectralWall) + sizeof (analysis::SampleGrid) + forensics.meanBins + forensics.cells));
    const auto stereo = analysis::StereoColumns::storageFor (channels, pcm.frames, p.columns);
    set (Analyzer::Stereo, stereo.ok, sizeof (analysis::StereoColumns) + stereo.firstBytes(), stereo.columns * 3u);
    const auto waveform = analysis::WaveformPeaks::storageFor (rate, channels, pcm.frames, p.waveformBuckets, p.waveformMix);
    set (Analyzer::Waveform, waveform.ok, sizeof (analysis::WaveformPeaks) + waveform.firstBytes(), waveform.buckets);
    const auto bursts = analysis::StereoBandBursts::storageFor (rate, p.bursts);
    set (Analyzer::StereoBursts, bursts.ok, sizeof (analysis::StereoBandBursts) + bursts.bytes(),
         2u * bursts.crosses * (sizeof (analysis::BandBurst) + sizeof (analysis::StereoBandBursts::Cross)));
    const auto crest = analysis::BandCrest::storageFor (rate, channels, static_cast<long long> (pcm.frames), p.crest);
    set (Analyzer::Crest, crest.ok, sizeof (analysis::BandCrest) + crest.firstBytes(), crest.cellEntries + crest.countEntries * 3u);
    const auto hum = analysis::HumDetector::storageFor (rate, channels, p.hum);
    set (Analyzer::Hum, hum.ok, sizeof (analysis::HumDetector) + hum.firstBytes(),
         pcm.channelCount * (sizeof (analysis::HumReport) + 2u * sizeof (analysis::HumCandidate))
         + hum.stretchEntries * 4u + hum.harmonicEntries * sizeof (analysis::HumHarmonic));
    const auto tempo = tempo::TempoDetector::storageFor (rate, channels, pcm.frames, p.tempo);
    set (Analyzer::Tempo, tempo.ok, sizeof (tempo::TempoDetector) + tempo.firstBytes(),
         tempo.pointRecords * 3u + tempo.peakRecords * 2u);
    // Target-dependent needles have a separate job and demand; no source-wide index is built.
    out.analyzers[std::size_t (Analyzer::Excursions)] = {};
    auto& s = out.storage;
    s.sourceBytes = double (pcm.frames * pcm.channelCount * sizeof (float));
    s.workspaceBytes = double (sizeof (MeasurementWorkspace) + sizeof (LiveMeasurements));
    s.copyBytes = double ((kAnalyzers - streaming.size()) * sizeof (MeasurementResult));
    for (const auto id : streaming)
    {
        const auto& a = out.analyzers[std::size_t (id)];
        s.workspaceBytes += double (a.workspace);
        // Scalar names and array descriptors are already inline in LiveMeasurements, priced above.
        s.resultBytes += double (a.rowValues * sizeof (double));
        s.copyBytes += double (a.result);
    }
    // fc_session transports numeric rows as binary f64. Scalar JSON uses exact decimals, which can
    // exceed a thousand bytes; reserve the writer's own capacity for every possible scalar. Text and
    // record framing retain the escaping bound. Plain JSON row exports query Codec::encodedBytes
    // separately: an optional export buffer is not a condition for admitting the web measurement.
    s.codecBytes = s.resultBytes + 6.0 * (s.copyBytes - s.resultBytes)
                 + double (streaming.size() * kMeasurementNumbers * JsonNumber::capacity);
    // The allocator allowance covers headers/alignment and MSVC's 31-byte large-vector padding.
    // At most 256 requests per analyzer for the declared one/two-channel preparations and output copies.
    s.allocatorBytes = double (streaming.size() * 256u * 64u);
    s.loadPeakBytes = 2.0 * s.sourceBytes + double (sizeof (MeasurementWorkspace) + sizeof (LiveMeasurements)) + s.allocatorBytes;
    // Streaming instruments coexist. Include one detached result and its serialization even if requested mid-stream.
    // Future instruments and target-dependent needles have their own admission; unused maxima are not load demand.
    s.workPeakBytes = s.sourceBytes + s.resultBytes + s.workspaceBytes + s.copyBytes + s.codecBytes + s.allocatorBytes;
    s.peakBytes = std::max (s.loadPeakBytes, s.workPeakBytes);
    s.largestBlockBytes = std::max ({ s.sourceBytes, s.resultBytes, s.workspaceBytes, s.codecBytes });
    if (! std::isfinite (s.peakBytes) || s.peakBytes >= 9007199254740992.0
        || s.largestBlockBytes > double (std::numeric_limits<std::size_t>::max())) out.rejection = Rejection::TooLong;
    return out;
}
std::uint64_t MeasurementPlan::key (std::uint64_t pcmHash, const MeasurementParameters& p,
                                   std::uint64_t configVersion, Version core, Version session) noexcept
{
    Hash h; h.add (pcmHash); h.add (configVersion);
    h.add (int (core.major)); h.add (int (core.minor)); h.add (int (core.patch));
    h.add (int (session.major)); h.add (int (session.minor)); h.add (int (session.patch));
    h.add (p.maxBlock); h.add (p.clipRuns); h.add (p.columns); h.add (p.waveformBuckets); h.add (int (p.waveformMix));
    h.add (p.programme.silenceThresholdDb);
    h.add (p.programme.tailWindowMs);
    h.add (p.programme.infraLowHz);
    h.add (p.programme.maxDurationSec);
    for (const auto& low : { p.lowEnd, p.infraLow, p.lowEnd150 })
    {
        h.add (low.crossoverHz);
        h.add (low.lowNoteHz);
        h.add (low.highNoteHz);
        h.add (low.tuningHz);
        h.add (low.fftOrder);
        h.add (low.hop);
        h.add (low.dutyThresholdDb);
        h.add (low.skipBlocks);
        h.add (low.maxBlocks);
    }
    h.add (p.forensics.fftOrder);
    h.add (p.forensics.hop);
    h.add (p.forensics.cellWidthHz);
    h.add (p.forensics.searchFromHz);
    h.add (p.forensics.plateauSpanHz);
    h.add (p.forensics.floorSpanHz);
    h.add (p.forensics.transitionStartDb);
    h.add (p.forensics.transitionEndDb);
    h.add (p.forensics.minDropDb);
    h.add (p.forensics.maxTransitionHz);
    h.add (p.forensics.exemptCells);
    h.add (p.forensics.nearNyquistFraction);
    h.add (p.forensics.emptyDb);
    h.add (p.forensics.emptyMinHz);
    h.add (p.forensics.gridOutlierFraction);
    h.add (p.forensics.maxDistinctValues);
    h.add (p.bursts.bandLowHz);
    h.add (p.bursts.bandHighHz);
    h.add (p.bursts.hopMs);
    h.add (p.bursts.baselineMs);
    h.add (p.bursts.enterDb);
    h.add (p.bursts.exitDb);
    h.add (p.bursts.maxEvents);
    for (double v : p.crest.bandEdgeHz) h.add (v);
    h.add (p.crest.hopMs);
    h.add (p.crest.blockHops);
    h.add (p.crest.programmeFloorDb);
    h.add (p.crest.bandShareFloorDb);
    h.add (p.hum.fftOrder);
    h.add (p.hum.hop);
    h.add (p.hum.quietThresholdDb);
    h.add (p.hum.toleranceHz);
    h.add (p.hum.searchHz);
    h.add (p.hum.floorSpanHz);
    h.add (p.hum.floorExcludeHz);
    h.add (p.hum.minProminenceDb);
    h.add (p.hum.minLevelDbfs);
    h.add (p.hum.harmonicToleranceBins);
    h.add (p.hum.minFramesPerObservation);
    h.add (p.hum.maxHarmonic);
    h.add (p.hum.maxStretches);
    h.add (p.hum.traceCapacity);
    h.add (p.tempo.minBpm);
    h.add (p.tempo.maxBpm);
    h.add (p.tempo.winSec);
    h.add (p.tempo.hopSec);
    return h.value;
}
} // namespace felitronics::session::detail
