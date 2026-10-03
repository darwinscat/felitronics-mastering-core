// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE CONFIG COMPILED INTO THE LIBRARY (Config.h). modules/session/CMakeLists.txt embeds modules/session/config/*.toml with
// felitronics_toml_embed, with felitronics-bands' bands.toml; this file hands those documents to the schema (src/ConfigSchema.cpp), writes them back as
// canonical text, hashes them (src/ConfigVersion.h), and hands them to the commands' reading in place (src/Rules.h).
// Nothing here reads a file.

#include "BuildGuards.h"

#include "ConfigBind.h"
#include "ConfigVersion.h"
#include "embedded/versions.h"
#include "Rules.h"
#include "embedded/bands.h"     // generated at build time from felitronics-bands' bands.toml
#include "embedded/engine.h"    // ... from modules/session/config/engine.toml
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
    switch (document)
    {
        case Document::Targets: return embedded::targets.root();
        case Document::Engine:  return embedded::engine.root();
        case Document::Bands:   return embedded::bands.root();
    }
    return {};
}
} // namespace

Loaded Config::load()
{
    const toml::Table targets = toml::embedded::toTable (rootOf (Document::Targets), detail::kTargetsSource);
    const toml::Table engine = toml::embedded::toTable (rootOf (Document::Engine), detail::kEngineSource);
    const toml::Table bands = toml::embedded::toTable (rootOf (Document::Bands), detail::kBandsSource);
    return detail::bindTables (&targets, &engine, &bands);
}

Loaded Config::bind (std::string_view targetsToml, std::string_view engineToml)
{
    return bind (targetsToml, engineToml, text (Document::Bands));
}

std::optional<Versions> Config::versionsOf (std::string_view targetsToml, std::string_view engineToml)
{
    return versionsOf (targetsToml, engineToml, text (Document::Bands));
}

std::string Config::text (Document document)
{
    return toml::write (toml::embedded::toTable (rootOf (document)));
}

Versions Config::versions() noexcept
{
    return { embedded::allVersion, embedded::soundVersion };
}

} // namespace felitronics::session::config

namespace felitronics::session::detail
{
Rules rules() noexcept
{
    return readRules (config::embedded::targets.root(), config::embedded::engine.root(),
                      config::embedded::bands.root().find ("bands"));
}
} // namespace felitronics::session::detail
