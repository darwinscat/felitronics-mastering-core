// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"

#include <felitronics/session/Session.h>

#include "Devices.h"
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
std::uint64_t Session::createBytes() noexcept
{
    return (std::uint64_t) sizeof (Session);
}

Status Session::checkFloatingPointEnvironment() noexcept
{
    const bool ieeeDefault = ! probes::flushesSubnormalResults() && ! probes::readsSubnormalsAsZero()
                          && probes::roundsToNearest();
    return ieeeDefault ? Status::Ok : Status::FloatingPointEnvironment;
}

Created Session::create() noexcept
{
    // The checks before the one allocation, so a refused create requested nothing (law 11): the thread, then the config
    // the commands will check against, read in place (src/Rules.h — reading it allocates nothing).
    Created c;
    if ((c.status = checkFloatingPointEnvironment()) != Status::Ok) return c;
    const detail::Rules rules = detail::rules();
    if (! rules.complete)
    {
        c.status = Status::Config;
        return c;
    }
    // `new`, not make_unique: the constructor is private, and a Session made anywhere but here would be one whose demand
    // nobody published. The vectors it holds are empty and ask for nothing.
    c.session = std::unique_ptr<Session> (new Session);
    Project& project = c.session->project_;
    project.target = rules.defaultRow;
    detail::placeMachine (rules, project.target, 0, project.devices);
    return c;
}

Session::~Session() = default;

State Session::state() const noexcept { return state_; }
bool Session::mastering() const noexcept { return mastering_; }
std::uint64_t Session::revision() const noexcept { return revision_; }
const Project& Session::project() const noexcept { return project_; }
Source Session::source() const noexcept { return source_; }
JobId Session::job() const noexcept { return job_; }
const Recipe& Session::jobRecipe() const noexcept { return jobRecipe_; }
std::span<const Kept> Session::masters() const noexcept { return { masters_.data(), masters_.size() }; }

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
