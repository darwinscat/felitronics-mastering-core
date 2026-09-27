// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE SESSION'S OWN TRANSITIONS (internal to modules/session; not public API). A measurement or a master ends by the work
// that ran it, never by a shell's command — so the endings are not Requests: the work that measures and renders calls
// them here. Each checks the thread's floating-point environment (it computes: the devices are placed on the config's
// numbers) and then Table::events (Commands.h) for the session's column, and changes nothing on a thread the session
// refuses or where the table does not allow it. Where it is allowed it moves the revision by one, as a command does.
//
// Session names this struct its friend; nothing but the library's own sources and its suites include this file.

#include <felitronics/session/Commands.h>
#include <felitronics/session/Session.h>

namespace felitronics::session::detail
{

struct Driver
{
    // The first measurement ended: Loaded becomes Measured1, and the devices are placed.
    [[nodiscard]] static bool measured1 (Session& session) noexcept;
    // The second measurement ended: Measured1 becomes Measured2, with a master being made or not.
    [[nodiscard]] static bool measured2 (Session& session) noexcept;
    // The master being made is done: it is kept under its job's id, and the overlay ends. Its room among the masters was
    // taken when it was asked for, so this asks the heap for nothing.
    [[nodiscard]] static bool mastered (Session& session) noexcept;

    // Is `event` allowed in the session's column — Table::events, read.
    [[nodiscard]] static bool allowed (const Session& session, Event event) noexcept;
};

} // namespace felitronics::session::detail
