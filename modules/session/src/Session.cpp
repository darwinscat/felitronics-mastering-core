// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"

#include <felitronics/session/Session.h>

#include <felitronics/session/Config.h>
#include <cmath>
#include <bit>

#include "FpProbes.h"
#include "Rules.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace felitronics::session
{

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
    if (! detail::rules().complete) return Status::Config;
    if (configVersion != config::Config::versions().all) return Status::ConfigVersion;
    if (! std::isfinite (caps.heapCeilingBytes) || caps.heapCeilingBytes < 0.0
        || caps.heapCeilingBytes >= 9007199254740992.0
        || std::bit_cast<std::uint64_t> (double (std::uint64_t (caps.heapCeilingBytes))) != std::bit_cast<std::uint64_t> (caps.heapCeilingBytes)
        || caps.maxRateHz < kMinSampleRate || (caps.offeredDevices & ~255u) != 0) return Status::Capabilities;
    return double (createBytes (caps)) > caps.heapCeilingBytes ? Status::Memory : Status::Ok;
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
                   + source_.name.size() + masterRoom_ * sizeof (Kept));
}
Checked Session::demand (std::uint64_t bytes) const noexcept
{
    const auto live = std::uint64_t (liveBytes());
    if (bytes > 9007199254740991ull - live) return { Rejection::TooLong, kNoField, 0 };
    const double need = double (live + bytes);
    if (need > capabilities_.heapCeilingBytes) return { Rejection::Memory, kNoField, 0, need };
    return { Rejection::None, kNoField, bytes };
}

Session::~Session() = default;

State Session::state() const noexcept { return state_; }
bool Session::placed() const noexcept { return state_ == State::Measured1 || state_ == State::Measured2; }
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
        case State::Empty:     return Column::Empty;
        case State::Loaded:    return Column::Loaded;
        case State::Measured1: return mastering_ ? Column::Mastering1 : Column::Measured1;
        case State::Measured2: return mastering_ ? Column::Mastering2 : Column::Measured2;
    }
    return Column::Empty;
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
