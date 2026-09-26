// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — a mutable namespace-scope variable, in an anonymous namespace where it looks private.
// run.sh plants this file in modules/session/src/ and requires the lint to fail with [GLOBALS] on the marked line.

namespace felitronics::session
{
namespace
{
    int lastRevision = 0;   // VIOLATION
}

int nextRevision() noexcept { return ++lastRevision; }
}
