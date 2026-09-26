// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fc_tempo — the tempo detector alone, the translation unit of the fctempo module (tools/wasm/build.sh).
//
// A page that measures a tempo and nothing else used to load all of fcprobe for it: every offline analyzer, of
// which it calls one. This module is that one — tempo::TempoDetector and the ten fc_probe_tempo_* entry points,
// which are NOT written here: tools/wasm/fc_tempo_entry.h is the text fc_probe.cpp compiles too, so the names, the
// rows and the refusals are the probe's by construction, and a page switches module without changing a call.
// tempo-parity.mjs diffs both modules against native `fcore_measure tempo`, byte for byte.
//
// What this file adds is the version (tools/fc_tempo_abi.h says why it is not fc_probe's), and nothing else: an
// entry point of any other analyzer here would put its code back into a module that exists to be without it —
// storage-probe.mjs holds the module to its exact export set.
//
// Like fc_probe.cpp it compiles natively as well (FC_EXPORT degrades to extern "C"), which is how
// felitronics_fctempo_abi_tests runs the tempo suite against THIS translation unit under ctest, ASan and UBSan.

#include "fc_tempo_abi.h"
#include "fc_abi_guards.h"
#include "fc_tempo_entry.h"

#include <cstdint>

// WHICH SURFACE THIS MODULE SPEAKS (tools/fc_tempo_abi.h, where the append-only rule that moves it is written). A
// page's loader asks this first and refuses a module older than the page. Reads no measurement, cannot fail.
FC_EXPORT std::uint32_t fc_tempo_abi_version (void) { return FC_TEMPO_ABI_VERSION; }
