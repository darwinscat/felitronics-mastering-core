// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"

#include <felitronics/session/Session.h>

#include "BuildContract.h"
#include "FpProbes.h"

#include <cstdint>
#include <memory>

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
    // The checks in a fixed order, all before the one allocation, so a refused create requested nothing (law 11).
    Created c;
    if ((c.status = checkFloatingPointEnvironment()) != Status::Ok) return c;
    if (detail::keptCanaryContracts()) { c.status = Status::ContractedHelper; return c; }
    // `new`, not make_unique: the constructor is private, and a Session made anywhere but here would be one whose demand
    // nobody published.
    c.session = std::unique_ptr<Session> (new Session);
    return c;
}

Session::~Session() = default;

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
