// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// CLI-only fault injection, as in SessionReplayTests: leave the production noexcept
// facade in an abandoned call. The termination handler never returns to that call.
#if defined(__EMSCRIPTEN__)
#include "session-script.h"
#else
#include "session-script.h"
#include "fc_session_abi.h"
#include <felitronics/session/Config.h>
#include <cstdlib>
#include <exception>
#include <new>
namespace { bool failAllocation = false; }
void* operator new (std::size_t size)
{
    if (failAllocation) { failAllocation = false; std::terminate(); }
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
[[noreturn]] void sessionScriptPoison (void (*continuation)())
{
    std::set_terminate (continuation);
    const fc_session_capabilities caps { sizeof (fc_session_capabilities), 67108864, 96000, FC_SESSION_DEVICES_ALL, 67108864 };
    const auto version = felitronics::session::config::Config::versions().all;
    fc_session handle = 0;
    failAllocation = true;
    // Terminate inside the allocator, while the production CallGuard is active.
    // This is the fatal allocation path on every exceptions-free native build,
    // independent of the host compiler's exception-unwinding conventions.
    (void) fc_session_create (&caps, std::uint32_t (version), std::uint32_t (version >> 32), &handle);
    std::abort(); // The armed allocation must not return.
}

#endif
