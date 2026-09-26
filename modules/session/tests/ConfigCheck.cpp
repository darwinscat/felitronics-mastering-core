// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// felitronics_session_config_check — the config's schema as a BUILD STEP, and as a tool for the checks around it.
//
//   config_check <targets.toml> <engine.toml>
//       Reads the config EMBEDDED in the library (config::load()) and prints every problem as
//       `<file>:<line>:<column>: error: <fault> <key path>`, the format compilers and IDEs understand, naming the two
//       source files given. modules/session/CMakeLists.txt runs it right after it is linked, so a config that breaks the
//       schema stops the build of this repository at its line and column.
//   config_check --files <targets.toml> <engine.toml>
//       The same schema over the two files as they are (config::bind()) — what the controls feed a planted mistake to.
//   config_check --expect <targets.toml> <engine.toml> <directory>
//       Writes what `fcore_session config` must print for these sources: <directory>/targets.toml and engine.toml, each
//       file through felitronics-toml's canonical writer, and version.txt, config::versionOf the two files — the walk
//       over their parsed text, where the library's own version() walks its embedded data.
//
// Exit status: 0 no problem (or written); 1 problems, one line each on stderr; 2 a usage error or a file that cannot be
// read or written.

#include <felitronics/session/Config.h>
#include <felitronics/toml/Toml.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

namespace config = felitronics::session::config;

namespace
{
bool readFile (const char* path, std::string& out)
{
    std::FILE* f = std::fopen (path, "rb");
    if (f == nullptr) return false;
    char buf[4096];
    for (std::size_t n; (n = std::fread (buf, 1, sizeof buf, f)) > 0;) out.append (buf, n);
    const bool failed = std::ferror (f) != 0;
    std::fclose (f);
    return ! failed;
}

bool writeFile (const std::string& path, const std::string& text)
{
    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool wrote = std::fwrite (text.data(), 1, text.size(), f) == text.size();
    return std::fclose (f) == 0 && wrote;
}

int report (const config::Loaded& loaded, const char* targetsPath, const char* enginePath)
{
    for (const config::Problem& p : loaded.problems)
    {
        const char* file = p.document == config::Document::Targets ? targetsPath : enginePath;
        const bool coded = p.fault == config::Fault::Syntax || p.fault == config::Fault::Refused;
        std::fprintf (stderr, "%s:%u:%u: error: %s%s%s%s%s\n", file, (unsigned) p.line, (unsigned) p.column,
                      config::name (p.fault), p.path.empty() ? "" : " ", p.path.c_str(), coded ? " — " : "",
                      coded ? p.code : "");
    }
    return loaded.ok() ? 0 : 1;
}

// The canonical text of a file, or false with the reason on stderr.
bool canonical (const char* path, std::string& out)
{
    std::string text;
    if (! readFile (path, text)) { std::fprintf (stderr, "config_check: cannot read %s\n", path); return false; }
    const auto parsed = felitronics::toml::parse (text);
    if (const auto* e = std::get_if<felitronics::toml::Error> (&parsed))
    {
        std::fprintf (stderr, "%s:%u:%u: error: Syntax — %s\n", path, (unsigned) e->line, (unsigned) e->column,
                      felitronics::toml::codeName (e->code));
        return false;
    }
    out = felitronics::toml::write (std::get<felitronics::toml::Table> (parsed));
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 3) return report (config::load(), argv[1], argv[2]);
    if (argc == 4 && std::strcmp (argv[1], "--files") == 0)
    {
        std::string targets, engine;
        if (! readFile (argv[2], targets)) { std::fprintf (stderr, "config_check: cannot read %s\n", argv[2]); return 2; }
        if (! readFile (argv[3], engine)) { std::fprintf (stderr, "config_check: cannot read %s\n", argv[3]); return 2; }
        return report (config::bind (targets, engine), argv[2], argv[3]);
    }
    if (argc == 5 && std::strcmp (argv[1], "--expect") == 0)
    {
        std::string targets, engine, targetsText, engineText;
        if (! canonical (argv[2], targets) || ! canonical (argv[3], engine)) return 2;
        if (! readFile (argv[2], targetsText) || ! readFile (argv[3], engineText)) return 2;
        const auto v = config::versionOf (targetsText, engineText);
        if (! v) return 2;   // canonical() above has already parsed both and said why
        char version[32];
        std::snprintf (version, sizeof version, "%016" PRIx64 "\n", *v);
        const std::string dir = argv[4];
        if (! writeFile (dir + "/targets.toml", targets) || ! writeFile (dir + "/engine.toml", engine)
            || ! writeFile (dir + "/version.txt", version))
        {
            std::fprintf (stderr, "config_check: cannot write into %s\n", argv[4]);
            return 2;
        }
        return 0;
    }
    std::fprintf (stderr, "usage: %s <targets.toml> <engine.toml>\n"
                          "       %s --files <targets.toml> <engine.toml>\n"
                          "       %s --expect <targets.toml> <engine.toml> <directory>\n",
                  argv[0], argv[0], argv[0]);
    return 2;
}
