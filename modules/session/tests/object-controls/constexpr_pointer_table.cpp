// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL, THE OTHER WAY ROUND — what the gate must ACCEPT. A constexpr table of pointers is constant data
// that needs relocating: position-independent code puts it in ELF's .data.rel.ro (.data.rel.ro.local for an internal
// one), Mach-O's __DATA,__const, COFF's .rdata, a wasm .rodata / .data.rel.ro segment — writable while the loader
// relocates it, read-only after, and never written by the code. The gate must pass this object, where
// pointer_array.cpp — the same table with mutable pointers — must fail. Built position-independent on purpose
// (modules/session/CMakeLists.txt), so the relocated sections really are the ones read.

namespace felitronics::session::control
{
constexpr const char* kPhaseLabels[] = { "convert", "stream", "report", "analyzers" };
inline constexpr const char* kUnitLabels[] = { "LUFS", "dBTP", "LU", "dB" };

const char* phaseLabel (int i) noexcept { return kPhaseLabels[i]; }
const char* unitLabel (int i) noexcept { return kUnitLabels[i]; }
const char* const* unitTable() noexcept { return kUnitLabels; }
}
