// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"

#include <felitronics/session/Session.h>

#include <felitronics/session/Config.h>
#include <cmath>
#include <algorithm>
#include <bit>

#include "FpProbes.h"
#include "Rules.h"
#include "MeasurementPlan.h"
#include "MeasurementWorkspace.h"
#include "LiveMeasurements.h"
#include "SourceMeasurements.h"
#include "Needles.h"
#include "QueryState.h"
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
    return (std::uint64_t) sizeof (Session);
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
        || caps.maxRateHz < kMinSampleRate || (caps.offeredDevices & ~255u) != 0) return Status::Capabilities;
    return double (createBytes (caps)) > caps.heapCeilingBytes || double (createBytes (caps)) > caps.largestFreeBlockBytes ? Status::Memory : Status::Ok;
}
Created Session::create() noexcept { return create ({}, config::Config::versions().all); }
Created Session::create (const Capabilities& caps, std::uint64_t configVersion) noexcept
{
    Created c;
    c.status = checkCreate (caps, configVersion);
    if (c.status != Status::Ok) return c;
    c.session = std::unique_ptr<Session> (new Session);
    c.session->capabilities_ = caps;
    c.session->project_.target = detail::rules().defaultRow;
    c.session->project_.core = version();
    return c;
}
const Capabilities& Session::capabilities() const noexcept { return capabilities_; }
double Session::liveBytes() const noexcept
{
    return double (createBytes (capabilities_) + source_.frames * source_.channels * sizeof (float)
                   + source_.name.size() + masterRoom_ * sizeof (Kept) + measurementOwnedBytes_
                   + (sourceMeasurements_ ? sizeof (detail::SourceMeasurements) + sourceMeasurements_->bytes : 0)
                   + (liveMeasurements_ ? sizeof (detail::LiveMeasurements) + liveMeasurements_->bytes : 0)
                   + (measurementWorkspace_ ? measurementWorkspace_->bytes() : 0)
                   + (waveform_ ? sizeof (detail::WaveformState) + waveform_->bytes : 0)
                   + (queryCache_ ? queryCache_->bytes() : 0)
                   + (needlesWork_ ? needlesWork_->bytes : 0) + (needlesResult_ ? sizeof (detail::NeedlesResult) : 0));
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
bool Session::placed() const noexcept { return devicesPlaced_; }
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
bool Session::masterRequiresTempo() const noexcept
{
    const auto target = detail::rules().row (project_.target).key;
    if (target == "cd" || target == "cdDynamic") return true;
    if ((capabilities_.offeredDevices & (1u << unsigned (Device::Glue))) == 0) return false;
    const auto& glue = project_.devices.glue;
    return glue.hand.on.value_or (glue.machine.on)
        && glue.hand.upToDb.value_or (glue.machine.upToDb) > 0.0;
}
TempoChoice Session::tempoForDevice() const noexcept
{
    const auto& result = measurementResults_[std::size_t (Analyzer::Tempo)];
    if (result.status == MeasurementStatus::Pending || result.status == MeasurementStatus::Cancelled)
        return { false, false, 0.0, result.reason };
    double bpm = 0.0; bool hasBpm = false, high = false;
    if (result.status == MeasurementStatus::Ready)
        for (const auto& value : result.numbers) if (value.value)
        {
            if (value.name == "headlineBpm") { bpm = *value.value; hasBpm = true; }
            else if (value.name == "headlineLabel")
                high = std::bit_cast<std::uint64_t> (*value.value)
                    == std::bit_cast<std::uint64_t> (double (tempo::ConfidenceLabel::High));
        }
    if (result.status == MeasurementStatus::Ready && hasBpm && high && std::isfinite (bpm) && bpm > 0)
        return { true, true, bpm, MeasurementReason::None };
    const auto configured = detail::rules().engine.find ("compressor").find ("tempo").find ("bpmWhenUnsure");
    const double fallback = configured.decimal() ? configured.decimal()->toDouble() : double (*configured.integer());
    const auto reason = result.status == MeasurementStatus::Ready ? MeasurementReason::NoSignal : result.reason;
    return { true, false, fallback, reason };
}
bool Session::mastering() const noexcept { return mastering_; }
std::uint64_t Session::revision() const noexcept { return revision_; }
const Project& Session::project() const noexcept { return project_; }
Source Session::source() const noexcept { return source_; }
JobId Session::job() const noexcept { return job_; }
const Recipe& Session::jobRecipe() const noexcept { return jobRecipe_; }
std::span<const Kept> Session::masters() const noexcept { return { masters_.get(), masterCount_ }; }

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
