// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/session/Session.h>

#include <cstdint>
#include <memory>

// THE TWO RELEASES, stated by the build that compiles this file — modules/session/CMakeLists.txt reads them from the
// project() lines of both repositories, tools/wasm/build.sh reads the same two lines. No default: a build that does
// not say which releases it is would compile a library that answers a version nobody built.
#if ! defined (FELITRONICS_SESSION_VERSION_MAJOR) || ! defined (FELITRONICS_SESSION_VERSION_MINOR) \
    || ! defined (FELITRONICS_SESSION_VERSION_PATCH)
    #error "felitronics::session: FELITRONICS_SESSION_VERSION_{MAJOR,MINOR,PATCH} are not defined — build it through modules/session/CMakeLists.txt or tools/wasm/build.sh"
#endif
#if ! defined (FELITRONICS_SESSION_CORE_VERSION_MAJOR) || ! defined (FELITRONICS_SESSION_CORE_VERSION_MINOR) \
    || ! defined (FELITRONICS_SESSION_CORE_VERSION_PATCH)
    #error "felitronics::session: FELITRONICS_SESSION_CORE_VERSION_{MAJOR,MINOR,PATCH} are not defined — build it through modules/session/CMakeLists.txt or tools/wasm/build.sh"
#endif

namespace felitronics::session
{

// One expression for the request and for its price: create() asks the heap for exactly one Session, and this is
// what one Session costs. When the object comes to own storage of its own, both grow here, together.
std::uint64_t Session::createBytes() noexcept
{
    return (std::uint64_t) sizeof (Session);
}

std::unique_ptr<Session> Session::create() noexcept
{
    // `new`, not make_unique: the constructor is private, and a Session made anywhere but here would be one whose
    // demand nobody published.
    return std::unique_ptr<Session> (new Session);
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
