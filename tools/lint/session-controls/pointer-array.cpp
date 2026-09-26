// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — the global that READS as a constant: an array of pointers to const characters. The characters
// are const; the pointers are not, and any code can repoint one. run.sh plants this file in modules/session/src/ and
// requires [GLOBALS] on the marked line (`const char* const` would be allowed).

namespace felitronics::session
{
const char* kPhaseNames[] = { "convert", "stream", "report" };   // VIOLATION

const char* phaseName (int i) noexcept { return kPhaseNames[i]; }
}
