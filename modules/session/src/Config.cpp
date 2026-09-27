// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE CONFIG COMPILED INTO THE LIBRARY (Config.h). modules/session/CMakeLists.txt embeds modules/session/config/*.toml with
// felitronics_toml_embed; this file hands those two documents to the schema (src/ConfigSchema.cpp), writes them back as
// canonical text, hashes them (src/ConfigVersion.h), and hands them to the commands' reading in place (src/Rules.h).
// Nothing here reads a file.

#include "BuildGuards.h"

#include "ConfigBind.h"
#include "ConfigVersion.h"
#include "Rules.h"
#include "embedded/engine.h"    // generated at build time from modules/session/config/engine.toml
#include "embedded/targets.h"   // ... and from modules/session/config/targets.toml

#include <felitronics/session/Config.h>
#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <string>

namespace felitronics::session::config
{
namespace
{
namespace toml = felitronics::toml;

toml::embedded::View rootOf (Document document) noexcept
{
    return document == Document::Targets ? embedded::targets.root() : embedded::engine.root();
}
} // namespace

Loaded Config::load()
{
    const toml::Table targets = toml::embedded::toTable (rootOf (Document::Targets), detail::kTargetsSource);
    const toml::Table engine = toml::embedded::toTable (rootOf (Document::Engine), detail::kEngineSource);
    return detail::bindTables (&targets, &engine);
}

std::string Config::text (Document document)
{
    return toml::write (toml::embedded::toTable (rootOf (document)));
}

Versions Config::versions() noexcept
{
    const auto targets = rootOf (Document::Targets);
    const auto engine = rootOf (Document::Engine);
    return { detail::version (targets, engine, false), detail::version (targets, engine, true) };
}

} // namespace felitronics::session::config

namespace felitronics::session::detail
{
Rules rules() noexcept
{
    return readRules (config::embedded::targets.root(), config::embedded::engine.root());
}
DefaultsTables carriedDefaults() noexcept
{
    return { rules(), std::nullopt };
}
} // namespace felitronics::session::detail
