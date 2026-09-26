// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// WASM CONTROL — a callable the facade's FC_EXPORT extractor does not see, exported anyway by
// EMSCRIPTEN_KEEPALIVE. tools/wasm/build.sh links it into a control copy of fcsession (build/controls/), and
// tools/wasm/session-check.mjs must refuse that module, naming `debug_probe`: the export check compares EVERY export of
// the artifact against the ABI and the runtime's own, not only the names that begin with fc_.

#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int debug_probe (void) { return 42; }
