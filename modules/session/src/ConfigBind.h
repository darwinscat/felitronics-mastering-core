// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE SCHEMA'S SEAM (internal to modules/session). src/ConfigSchema.cpp is the schema — it reads two parsed documents
// into the typed structs of Config.h — and it is compiled twice: into the library, beside src/Config.cpp, which hands it
// the documents compiled in; and into felitronics_session_config_check, the host tool every build runs over the source
// documents before the library is built (modules/session/CMakeLists.txt). So the build's gate and the library read the
// config by one schema, the same text of code, and the tool does not need the library it gates.

#include <felitronics/session/Config.h>
#include <felitronics/toml/Toml.h>

namespace felitronics::session::config::detail
{

// The source numbers the documents' positions carry.
inline constexpr std::uint32_t kTargetsSource = 1;
inline constexpr std::uint32_t kEngineSource = 2;
inline constexpr std::uint32_t kBandsSource = 3;

// The three documents by schema — the engine first, then the targets, whose rows are checked against the engine's knobs,
// then felitronics-bands' bands.toml, the filters of the engine's EQ devices. A null document (one that did not parse)
// binds nothing, and the checks that need it are left out.
[[nodiscard]] Loaded bindTables (const toml::Table* targets, const toml::Table* engine, const toml::Table* bands);

} // namespace felitronics::session::config::detail
