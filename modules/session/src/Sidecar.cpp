// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "BuildContract.h"
#include "QueryState.h"
#include "MeasurementWorkspace.h"
#include "LiveMeasurements.h"
#include "SourceMeasurements.h"
#include "Rules.h"
#include "Utf8.h"
#include <felitronics/session/Session.h>
#include <felitronics/session/Config.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace felitronics::session
{
namespace
{
struct Fnv
{
    std::uint64_t h = 0xCBF29CE484222325ull;
    void byte (std::uint8_t b) noexcept { h = (h ^ b) * 0x100000001B3ull; }
    void u32 (std::uint32_t v) noexcept { for (unsigned i = 0; i < 4; ++i) byte (std::uint8_t (v >> (8 * i))); }
    void u64 (std::uint64_t v) noexcept { for (unsigned i = 0; i < 8; ++i) byte (std::uint8_t (v >> (8 * i))); }
};
Checked refused (Rejection reason) noexcept { Checked c; c.rejection = reason; return c; }
MeasurementReason missing (const std::optional<double>& value) noexcept
{ return value ? (std::isfinite (*value) ? MeasurementReason::None : MeasurementReason::NonFinite) : MeasurementReason::Unsupported; }
}
Checked Session::loadMeasuredStorage (const MeasuredSource& facts) const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok) return refused (Rejection::FloatingPointEnvironment);
    if (const auto r = Table::commands[std::size_t (Command::Load)].cell[std::size_t (column())]; r != Rejection::None) return refused (r);
    if (! detail::validUtf8 (facts.name)) return refused (Rejection::InvalidUtf8);
    if (facts.channels < 1 || facts.channels > 2) return refused (Rejection::BadChannels);
    if (facts.sampleRate < kMinSampleRate) return refused (Rejection::BadRate);
    if (facts.sampleRate > capabilities_.maxRateHz) return refused (Rejection::RateAboveLimit);
    if (facts.frames == 0) return refused (Rejection::NoAudio);
    if (facts.frames > 9007199254740991ull / (sizeof (float) * facts.channels)
        || facts.frames > std::numeric_limits<std::size_t>::max() / (sizeof (float) * facts.channels)) return refused (Rejection::TooLong);
    if (facts.sourceHash == 0 || facts.bitDepth > 64u) return refused (Rejection::Contract);
    Checked c; c.bytes = facts.name.size(); c.largestBlockBytes = c.bytes;
    return demand (c);
}
Answer Session::loadMeasured (CommandId id, const MeasuredSource& facts) noexcept
{
    Answer answer; answer.command = id;
    const auto checked = loadMeasuredStorage (facts);
    if (checked.rejection != Rejection::None)
    { answer.rejection = checked.rejection; answer.needBytes = checked.needBytes; return reject (answer); }
    // Copy the potentially aliased name before releasing the old source.
    std::unique_ptr<char[]> name (facts.name.empty() ? nullptr : new char[facts.name.size()]);
    std::copy (facts.name.begin(), facts.name.end(), name.get());
    eventCount_ = 0;
    samples_.reset(); waveform_.reset(); queryCache_.reset();
    measurementWorkspace_.reset(); liveMeasurements_.reset(); sourceMeasurements_.reset();
    clearNeedles(); needlesSource_ = needlesKey_ = 0; needlesNeedDb_.reset(); needlesCeilingDb_.reset();
    needlesProgress_ = {}; needlesDemand_ = {};
    masters_.reset(); masterCount_ = masterRoom_ = 0; mastering_ = false; job_ = 0; jobRecipe_ = {};
    for (auto& owner : measurementOwners_) owner = {};
    measurementOwnedBytes_ = 0; measurementStorage_ = {};
    name_ = std::move (name);
    source_ = { facts.channels, facts.sampleRate, facts.frames, facts.sourceHash, facts.fileRate, facts.rateKnown,
                std::uint8_t (facts.bitDepth), { name_.get(), facts.name.size() } };
    measurementKey_ = facts.sourceHash;
    project_.core = version(); project_.manual = false; project_.devices = {};
    devicesPlaced_ = false; differenceCount_ = 0;
    measurementJob_ = 0; measurementUnit_ = masterUnit_ = 0; measurementProgress_ = {}; masterProgress_ = {};
    measurementsFromSidecar_ = true;
    sidecarNumbers_[0] = { "integratedLufs", facts.integratedLufs && std::isfinite (*facts.integratedLufs) ? facts.integratedLufs : std::nullopt,
                           missing (facts.integratedLufs), 0 };
    sidecarNumbers_[1] = { "truePeakDb", facts.truePeakDb && std::isfinite (*facts.truePeakDb) ? facts.truePeakDb : std::nullopt,
                           missing (facts.truePeakDb), 0 };
    for (std::size_t i = 0; i < kAnalyzers; ++i)
    {
        auto& r = measurementResults_[i]; r = {}; r.analyzer = Analyzer (i);
        r.key = r.analyzer == Analyzer::Forensics ? detail::MeasurementPlan::forensicsKey (measurementKey_, facts.bitDepth) : measurementKey_;
        r.status = MeasurementStatus::Unavailable; r.reason = MeasurementReason::NotImplemented;
    }
    auto& loudness = measurementResults_[std::size_t (Analyzer::Loudness)];
    loudness.numbers = sidecarNumbers_; loudness.framesRead = facts.frames;
    loudness.status = sidecarNumbers_[0].value && sidecarNumbers_[1].value ? MeasurementStatus::Ready : MeasurementStatus::Unavailable;
    loudness.reason = loudness.status == MeasurementStatus::Ready ? MeasurementReason::None
        : sidecarNumbers_[0].reason != MeasurementReason::None ? sidecarNumbers_[0].reason : sidecarNumbers_[1].reason;
    state_ = mandatoryReady() ? State::Measured2 : State::Loaded;
    ++revision_;
    for (const auto& r : measurementResults_)
    {
        Notification event; event.kind = EventKind::Measurement;
        event.payload.measurement = { r.analyzer, r.status, r.reason, r.key, source_.hash, revision_, r.framesRead, r.total, r.stored, r.complete };
        emit (event);
    }
    answer.revision = revision_; return answer;
}
Checked Session::attachAudioStorage (const Pcm& pcm) const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok) return refused (Rejection::FloatingPointEnvironment);
    if (! measurementsFromSidecar_ || source_.channels == 0) return refused (Rejection::NoSource);
    if (samples_ || measurementJob_ != 0 || job_ != 0) return refused (Rejection::Busy);
    if (pcm.channelCount != source_.channels || pcm.frames != source_.frames || pcm.sampleRate != source_.sampleRate)
        return refused (Rejection::Contract);
    if (lastJob_ == std::numeric_limits<JobId>::max()) return refused (Rejection::NoJobId);
    const auto wave = analysis::WaveformIndex::storageFor (pcm.sampleRate, pcm.channelCount, pcm.frames);
    if (! wave.ok) return refused (Rejection::TooLong);
    const auto audio = pcm.frames * pcm.channelCount * sizeof (float);
    const auto indexBytes = wave.bytes() + 256u;
    if (audio > 9007199254740991ull - indexBytes - sizeof (detail::WaveformState)) return refused (Rejection::TooLong);
    Checked c; c.bytes = audio + indexBytes + sizeof (detail::WaveformState);
    c.largestBlockBytes = std::max ({ audio, indexBytes, std::uint64_t (sizeof (detail::WaveformState)) });
    return demand (c);
}
Answer Session::attachAudio (CommandId id, const Pcm& pcm) noexcept
{
    Answer answer; answer.command = id;
    const auto checked = attachAudioStorage (pcm);
    if (checked.rejection != Rejection::None)
    { answer.rejection = checked.rejection; answer.needBytes = checked.needBytes; return reject (answer); }
    if (! pcm.channels) { answer.rejection = Rejection::NoAudio; return reject (answer); }
    for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
        if (! pcm.channels[c]) { answer.rejection = Rejection::NoAudio; return reject (answer); }
    Fnv hash; hash.u32 (pcm.sampleRate); hash.u32 (pcm.channelCount); hash.u64 (pcm.frames);
    for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
        for (std::size_t i = 0; i < std::size_t (pcm.frames); ++i) hash.u32 (std::bit_cast<std::uint32_t> (pcm.channels[c][i]));
    if (hash.h != source_.hash) { answer.rejection = Rejection::Contract; return reject (answer); }
    const auto frames = std::size_t (pcm.frames);
    std::unique_ptr<float[]> audio (new float[frames * pcm.channelCount]);
    for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
        std::copy_n (pcm.channels[c], frames, audio.get() + c * frames);
    std::unique_ptr<detail::WaveformState> wave (new detail::WaveformState);
    samples_ = std::move (audio); waveform_ = std::move (wave); queryCache_.reset();
    measurementStorage_.sourceBytes = double (frames * pcm.channelCount * sizeof (float));
    measurementStorage_.workspaceBytes = double (checked.bytes - frames * pcm.channelCount * sizeof (float));
    measurementStorage_.peakBytes = liveBytes() + double (checked.bytes - frames * pcm.channelCount * sizeof (float)
                                                         - sizeof (detail::WaveformState));
    auto& result = measurementResults_[std::size_t (Analyzer::Waveform)];
    result.status = MeasurementStatus::Pending; result.reason = MeasurementReason::Pending;
    measurementJob_ = ++lastJob_;
    measurementProgress_ = { PhaseName::Stream, 0.0, config::Config::versions().all, 0, 0, 0,
        std::uint32_t (std::min<std::uint64_t> ((pcm.frames + 1023u) / 1024u + 1u, 4294967295u)) };
    ++revision_;
    eventCount_ = 0;
    Notification event; event.kind = EventKind::Phase; event.jobId = measurementJob_; event.payload.phase = measurementProgress_; emit (event);
    answer.revision = revision_; answer.job = measurementJob_;
    requestNeedles();
    return answer;
}
} // namespace felitronics::session
