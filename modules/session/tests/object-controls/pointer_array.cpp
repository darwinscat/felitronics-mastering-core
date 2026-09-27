// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — the global that READS as a constant: an array of pointers to const characters. The characters
// are const; the pointers are not.

namespace felitronics::session::control
{
const char* kPhaseNames[] = { "convert", "stream", "report" };
const char* phaseName (int i) noexcept { return kPhaseNames[i]; }
void renamePhase (int i, const char* name) noexcept { kPhaseNames[i] = name; }
}
