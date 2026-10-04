// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"

#include <felitronics/session/Session.h>
#include <felitronics/analysis/BandCrestResult.h>

#include <felitronics/session/Config.h>
#include <cmath>
#include <algorithm>
#include <bit>

#include "FpProbes.h"
#include "Rules.h"
#include "Dynamics.h"
#include "MeasurementPlan.h"
#include "MeasurementWorkspace.h"
#include "LiveMeasurements.h"
#include "SourceMeasurements.h"
#include "Needles.h"
#include "QueryState.h"
#include "MasterJob.h"
#include "Cost.h"
#include "BuildContract.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace felitronics::session
{
namespace
{
bool validBytes (double bytes) noexcept
{
    return std::isfinite (bytes) && bytes >= 0 && bytes < 9007199254740992.0
        && std::bit_cast<std::uint64_t> (double (std::uint64_t (bytes))) == std::bit_cast<std::uint64_t> (bytes);
}
}


// One expression for the request and for its price: create() asks the heap for exactly one Session, and this is what
// one Session costs.
std::uint64_t Session::createBytes (const Capabilities&) noexcept
{
    // The session object and its step's events: the two blocks create() asks for.
    return (std::uint64_t) sizeof (Session) + (std::uint64_t) sizeof (std::array<Notification, kEventBatch>);
}

Status Session::checkFloatingPointEnvironment() noexcept
{
    const bool ieeeDefault = ! probes::flushesSubnormalResults() && ! probes::readsSubnormalsAsZero()
                          && probes::roundsToNearest();
    return ieeeDefault ? Status::Ok : Status::FloatingPointEnvironment;
}

Status Session::checkCreate (const Capabilities& caps, std::uint64_t configVersion) noexcept
{
    if (const auto st = checkFloatingPointEnvironment(); st != Status::Ok) return st;
    if (configVersion != config::Config::versions().all) return Status::ConfigVersion;
    if (! validBytes (caps.heapCeilingBytes) || ! validBytes (caps.largestFreeBlockBytes)
        || caps.maxRateHz < kMinSampleRate || (caps.offeredDevices & ~kAllDevices) != 0) return Status::Capabilities;
    return double (createBytes (caps)) > caps.heapCeilingBytes || double (createBytes (caps)) > caps.largestFreeBlockBytes ? Status::Memory : Status::Ok;
}
Created Session::create() noexcept { return create ({}, config::Config::versions().all); }
Created Session::create (const Capabilities& caps, std::uint64_t configVersion) noexcept
{
    Created c;
    c.status = checkCreate (caps, configVersion);
    if (c.status != Status::Ok) return c;
    c.session = std::unique_ptr<Session> (new Session);
    c.session->events_.reset (new std::array<Notification, kEventBatch> {});
    c.session->capabilities_ = caps;
    c.session->project_.target = detail::rules().defaultRow;
    return c;
}
const Capabilities& Session::capabilities() const noexcept { return capabilities_; }
double Session::liveBytes() const noexcept
{
    std::uint64_t rows = 0;
    for (std::size_t i = 0; masterRows_ && i < masterRoom_; ++i)
    {
        if (masterRows_[i].passes)
            rows += 12u * sizeof (LandingPass)
                  + std::uint64_t (2u * masterRows_[i].traceCapacity) * sizeof (LandingTraceBucket);
        if (masterRows_[i].crest)
            rows += std::uint64_t (masterRows_[i].crestCapacity) * 15u * sizeof (double);
        if (masterRows_[i].costSeries) rows += std::uint64_t (masterRows_[i].costCapacity) * sizeof (double);
        if (masterRows_[i].sections) rows += std::uint64_t (masterRows_[i].costCapacity) * sizeof (MasterSection);
        if (masterRows_[i].costScratch) rows += std::uint64_t (masterRows_[i].costScratchCapacity) * sizeof (double);
        if (masterRows_[i].waveform) rows += std::uint64_t (masterRows_[i].waveformCapacity) * sizeof (MasterWaveformBucket);
        if (masterRows_[i].costMomentary) rows += std::uint64_t (masterRows_[i].costCapacity) * sizeof (double);
        if (masterRows_[i].axes) rows += std::uint64_t (masterRows_[i].axesCapacity) * sizeof (analysis::WaveformColumn);
        if (masterRows_[i].glueRows) rows += std::uint64_t (masterRows_[i].traceCapacity) * sizeof (LandingTraceBucket);
    }
    return double (createBytes (capabilities_) + (samples_ ? source_.frames * source_.channels * sizeof (float) : 0)
                   + source_.name.size() + masterRoom_ * (sizeof (Kept) + sizeof (detail::MasterRows))
                   + (leanMasters_ ? masterRoom_ * sizeof (Kept) : 0)
                   + rows + masterJobBytes_ + damageJobBytes_ + (masterAudio_.samples ? masterAudio_.frames * masterAudio_.channels * sizeof (float) : 0)
                   + measurementOwnedBytes_
                   + (sourceMeasurements_ ? sizeof (detail::SourceMeasurements) + sourceMeasurements_->bytes : 0)
                   + (liveMeasurements_ ? sizeof (detail::LiveMeasurements) + liveMeasurements_->bytes : 0)
                   + (measurementWorkspace_ ? measurementWorkspace_->bytes() : 0)
                   + (waveform_ ? sizeof (detail::WaveformState) + waveform_->bytes : 0)
                   + (queryCache_ ? queryCache_->bytes() : 0)
                   + (needlesWork_ ? needlesWork_->bytes : 0) + (needlesResult_ ? sizeof (detail::NeedlesResult) : 0));
}
void Session::clearMasters() noexcept
{
    mastering_ = false;
    job_ = 0;
    jobRecipe_ = {};
    jobWaiting_ = false;                       // a load ends a waiting master too: the needles are the new project's
    masterJob_.reset();
    masterJobBytes_ = 0;
    masterSummary_ = {}; masterTraceCursor_ = 0; masterTraceActive_ = false;
    masterAudio_ = {};
    masterAudioBits_ = 0;
    pendingMaster_ = {};
    masters_.reset();
    leanMasters_.reset();
    masterRows_.reset();
    masterCount_ = masterRoom_ = 0;
    crestJoinIndex_ = 0;
    crestJoin_ = false;
    crestJoinReason_ = MeasurementReason::None;
    masterUnit_ = 0;
    masterProgress_ = {};
    damageJob_.reset();
    damageJobId_ = 0;
    damageJobBytes_ = 0;
    damageProgress_ = {};
}
void Session::settleMasterCrest (MeasurementReason reason) noexcept
{
    crestJoin_ = masterCount_ != 0;
    crestJoinIndex_ = 0;
    crestJoinReason_ = reason;
}
void Session::stepMasterCrestJoin() noexcept
{
    if (crestJoinIndex_ >= masterCount_)
    { crestJoin_ = false; crestJoinIndex_ = 0; return; }
    const auto index = crestJoinIndex_;
    auto& master = masters_[index];
    if (! master.report || (master.report->crest.status != MeasurementStatus::Pending
        && ! (master.report->crest.status == MeasurementStatus::Ready && master.report->cost
            && masterRows_[index].crestCostBand < 5)))
    { ++crestJoinIndex_; return; }
    auto& c = master.report->crest;
    const auto publish = [&]() noexcept
    {
        ++revision_;
        Notification event;
        event.jobId = master.id;
        event.kind = EventKind::Fact;
        (void) event.payload.fact.assign (MasterReportText::crest (c));
        emit (event);
        if (c.status == MeasurementStatus::Ready && master.report->cost)
        {
            (void) event.payload.fact.assign (MasterReportText::impact (*master.report->cost));
            emit (event);
            if (const auto bands = MasterReportText::bands (*master.report->cost))
            { (void) event.payload.fact.assign (*bands); emit (event); }
        }
        ++crestJoinIndex_;
    };
    const auto& result = measurementResults_[std::size_t (Analyzer::Crest)];
    auto& rows = masterRows_[index];
    const auto unavailable = [&] (MeasurementReason reason) noexcept
    {
        c.status = MeasurementStatus::Unavailable; c.reason = reason;
        if (master.report->cost)
        {
            MasterCostValue* bands[] { &master.report->cost->crestLowDb, &master.report->cost->crestLowMidDb,
                &master.report->cost->crestHighMidDb, &master.report->cost->crestHighDb,
                &master.report->cost->crestFullDb };
            for (auto* band : bands) { *band = {}; band->reason = reason; }
        }
        publish();
    };
    if (master.id != rows.crestMasterId || master.recipe.source != rows.crestSource
        || rows.crestSource != source_.hash || rows.crestSourceKey != measurementKey_
        || result.key != rows.crestSourceKey || master.recipe.readyHash != rows.crestReadyHash
        || master.recipe.sound != rows.crestSound
        || master.recipe.deliveryRateHz != rows.crestDeliveryRate)
    { unavailable (MeasurementReason::Unsupported); return; }
    // Settle only on a terminal source result. A Pending one is a measurement resumed after the cancel that armed
    // this join: the master stays Pending, and the source's own completion arms the join again.
    if (result.status == MeasurementStatus::Pending) { ++crestJoinIndex_; return; }
    if (result.status != MeasurementStatus::Ready)
    { unavailable (crestJoinReason_); return; }
    const auto source = MeasurementCrest::view (result);
    if (! MasterCrestGrid::compatible (c, source, rows.crestParams))
    { unavailable (MeasurementReason::Unsupported); return; }
    if (! rows.crest || rows.crestCapacity < c.blocks)
    { unavailable (MeasurementReason::Memory); return; }
    const auto end = std::min<std::size_t> (std::size_t (c.blocks), rows.crestMaskCopied + 16u);
    auto* mask = rows.crest.get() + rows.crestCapacity * 10u;
    for (std::size_t row = rows.crestMaskCopied; row < end; ++row)
        for (std::size_t band = 0; band < 5u; ++band)
            mask[row * 5u + band] = source.mask[row * 5u + band];
    rows.crestMaskCopied = end;
    if (end == c.blocks)
    {
        c.sourceMask = { mask, end * 5u };
        c.status = MeasurementStatus::Ready;
        c.reason = MeasurementReason::None;
    }
    if (c.status == MeasurementStatus::Ready && master.report->cost && rows.crestCostBand < 5)
    {
        MasterCostValue* bands[] { &master.report->cost->crestLowDb, &master.report->cost->crestLowMidDb,
            &master.report->cost->crestHighMidDb, &master.report->cost->crestHighDb,
            &master.report->cost->crestFullDb };
        const std::span<double> scratch { rows.costScratch.get(), rows.costScratchCapacity };
        if (! rows.crestCostStarted)
        { rows.crestCostScan.start (source, c, rows.crestCostBand, scratch); rows.crestCostStarted = true; }
        if (rows.crestCostScan.step (source, c, scratch, 1))
        {
            *bands[rows.crestCostBand++] = rows.crestCostScan.answer;
            rows.crestCostStarted = false;
        }
    }
    if (c.status == MeasurementStatus::Ready && (! master.report->cost || rows.crestCostBand == 5)) publish();
}
MeasurementStorage Session::measurementStorage (const Pcm& pcm) const noexcept
{
    if (pcm.sampleRate < kMinSampleRate || pcm.channelCount < 1 || pcm.channelCount > 2 || pcm.frames == 0) return {};
    const auto plan = detail::MeasurementPlan::storageFor (pcm, detail::MeasurementPlan::parametersFor (pcm));
    auto result = plan.storage;
    // The old source is freed before the new PCM copy. Its live storage overlaps the caller's input,
    // not both input and replacement copy. New control objects can briefly coexist with the old ones.
    const auto controls = double (sizeof (detail::MeasurementWorkspace) + sizeof (detail::LiveMeasurements) + sizeof (detail::SourceMeasurements) + sizeof (detail::WaveformState));
    result.loadPeakBytes = std::max (result.loadPeakBytes + double (createBytes()),
                                    liveBytes() + result.sourceBytes + controls + result.allocatorBytes);
    result.workPeakBytes += double (createBytes());
    result.peakBytes = std::max (result.loadPeakBytes, result.workPeakBytes);
    return result;
}
Status Session::setCapacity (const Capacity& capacity) noexcept
{
    if (const auto st = checkFloatingPointEnvironment(); st != Status::Ok) return st;
    if (! validBytes (capacity.heapCeilingBytes) || ! validBytes (capacity.largestFreeBlockBytes)) return Status::Capabilities;
    capabilities_.heapCeilingBytes = capacity.heapCeilingBytes;
    capabilities_.largestFreeBlockBytes = capacity.largestFreeBlockBytes;
    return Status::Ok;
}
Checked Session::demand (const Checked& storage) const noexcept
{
    const auto bytes = storage.bytes;
    if (bytes == 0) return storage;
    const auto live = std::uint64_t (liveBytes());
    if (bytes > 9007199254740991ull - live) return { Rejection::TooLong, kNoField, 0 };
    const double need = double (live + bytes);
    if (need > capabilities_.heapCeilingBytes || double (storage.largestBlockBytes) > capabilities_.largestFreeBlockBytes)
        return { Rejection::Memory, kNoField, bytes, need, storage.largestBlockBytes };
    return storage;
}

Session::~Session() = default;

State Session::state() const noexcept { return state_; }
bool Session::placed() const noexcept { return devicesPlaced_ && plan_.status == PlanStatus::Ready; }
bool Session::mandatoryReady() const noexcept
{
    const auto& r = measurementResults_[std::size_t (Analyzer::Loudness)];
    bool lufs = false, peak = false;
    if (r.status != MeasurementStatus::Ready) return false;
    for (const auto& v : r.numbers)
        if (v.value && std::isfinite (*v.value))
        {
            if (v.name == "integratedLufs") lufs = true;
            if (v.name == "truePeakDb") peak = true;
        }
    return lufs && peak;
}
TempoChoice Session::tempoForDevice() const noexcept
{
    return detail::tempoChoice (detail::rules(), measurementResults_[std::size_t (Analyzer::Tempo)]);
}
bool Session::mastering() const noexcept { return mastering_; }
std::uint64_t Session::revision() const noexcept { return revision_; }
const Project& Session::project() const noexcept { return project_; }
Source Session::source() const noexcept { return source_; }
JobId Session::job() const noexcept { return job_; }
JobId Session::damageJob() const noexcept { return damageJobId_; }
const Recipe& Session::jobRecipe() const noexcept { return jobRecipe_; }
std::span<const Kept> Session::masters() const noexcept { return { masters_.get(), masterCount_ }; }
MasterToken Session::pendingMaster() const noexcept { return pendingMaster_; }
std::uint64_t Session::masterAudioBytes (MasterToken token) const noexcept
{
    if (token.source != pendingMaster_.source || token.revision != pendingMaster_.revision
        || token.job != pendingMaster_.job || token.master != pendingMaster_.master
        || token.master == 0 || ! masterAudio_.samples) return 0;
    return masterAudio_.frames * masterAudio_.channels * sizeof (float);
}
MasterAudioShape Session::masterAudioShape (MasterToken token) const noexcept
{
    return masterAudioBytes (token) == 0 ? MasterAudioShape {} : MasterAudioShape {
        masterAudio_.frames, masterAudio_.channels, masterAudio_.sampleRate };
}
MasterTransferStatus Session::copyMaster (MasterToken token, std::span<float> output) const noexcept
{
    const auto bytes = masterAudioBytes (token);
    if (bytes == 0) return pendingMaster_.master == 0 ? MasterTransferStatus::Unknown : MasterTransferStatus::Stale;
    const auto samples = std::size_t (bytes / sizeof (float));
    if (output.size() < samples) return MasterTransferStatus::TooSmall;
    std::copy_n (masterAudio_.samples.get(), samples, output.data());
    return MasterTransferStatus::Ok;
}
std::span<const float> Session::viewMaster (MasterToken token) const noexcept
{
    const auto bytes = masterAudioBytes (token);
    return bytes == 0 ? std::span<const float> {} : std::span<const float> {
        masterAudio_.samples.get(), std::size_t (bytes / sizeof (float)) };
}
MasterTransferStatus Session::takeMaster (MasterToken token, MasterAudio& output) noexcept
{
    if (masterAudioBytes (token) == 0)
        return pendingMaster_.master == 0 ? MasterTransferStatus::Unknown : MasterTransferStatus::Stale;
    output = std::move (masterAudio_);
    masterAudioBits_ = 0;
    pendingMaster_ = {};
    ++revision_;
    return MasterTransferStatus::Ok;
}
MasterTransferStatus Session::releaseMaster (MasterToken token) noexcept
{
    if (masterAudioBytes (token) == 0)
        return pendingMaster_.master == 0 ? MasterTransferStatus::Unknown : MasterTransferStatus::Stale;
    masterAudio_ = {};
    masterAudioBits_ = 0;
    pendingMaster_ = {};
    ++revision_;
    return MasterTransferStatus::Ok;
}

WavPlan Session::masterWavPlan (MasterToken token) const noexcept
{
    const auto shape = masterAudioShape (token);
    return shape.frames == 0 ? WavPlan {} : WavWriter::plan (
        shape.frames, shape.channels, shape.sampleRate, masterAudioBits_);
}

MasterTransferStatus Session::copyMasterWav (MasterToken token, std::uint64_t offset,
                                             std::span<std::uint8_t> output) const noexcept
{
    const auto pcm = viewMaster (token);
    if (pcm.empty()) return pendingMaster_.master == 0 ? MasterTransferStatus::Unknown : MasterTransferStatus::Stale;
    const auto plan = masterWavPlan (token);
    if (! plan || offset >= plan.bytes || output.empty() || output.size() > 65536u)
        return MasterTransferStatus::Contract;
    if (output.size() > plan.bytes - offset) return MasterTransferStatus::TooSmall;
    const auto outAddress = reinterpret_cast<std::uintptr_t> (output.data());
    const auto pcmAddress = reinterpret_cast<std::uintptr_t> (pcm.data());
    if ((outAddress <= pcmAddress && pcmAddress - outAddress < output.size())
        || (pcmAddress < outAddress && outAddress - pcmAddress < pcm.size_bytes()))
        return MasterTransferStatus::Contract;
    return WavWriter::copy (plan, pcm, offset, output) ? MasterTransferStatus::Ok : MasterTransferStatus::Contract;
}

std::string_view Session::targetName() const noexcept
{
    return detail::rules().row (project_.target).key;
}

Column Session::column() const noexcept
{
    switch (state_)
    {
        case State::MeasurementStopped:
            if (mastering_) return placed() ? Column::MasteringStopped : Column::MasteringStoppedUnplaced;
            if (stoppedState_ == State::Measured1 || stoppedState_ == State::Measured2)
                return placed() ? Column::StoppedMeasured : Column::StoppedMeasuredUnplaced;
            return Column::Stopped;
        case State::Empty:     return Column::Empty;
        case State::Loaded:    return Column::Loaded;
        case State::Measured1: return mastering_ ? (placed() ? Column::Mastering1 : Column::Mastering1Unplaced)
                                               : (placed() ? Column::Measured1 : Column::Measured1Unplaced);
        case State::Measured2: return mastering_ ? (placed() ? Column::Mastering2 : Column::Mastering2Unplaced)
                                               : (placed() ? Column::Measured2 : Column::Measured2Unplaced);
    }
    detail::storageOverflow();
}

Version Session::version() noexcept
{
    return { FELITRONICS_SESSION_VERSION_MAJOR, FELITRONICS_SESSION_VERSION_MINOR, FELITRONICS_SESSION_VERSION_PATCH };
}

Version Session::coreVersion() noexcept
{
    return { FELITRONICS_SESSION_CORE_VERSION_MAJOR, FELITRONICS_SESSION_CORE_VERSION_MINOR,
             FELITRONICS_SESSION_CORE_VERSION_PATCH };
}

} // namespace felitronics::session
