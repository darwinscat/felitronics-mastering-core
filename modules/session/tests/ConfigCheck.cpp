// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// felitronics_session_config_check — THE GATE: the config's schema, run by every build over the source documents before
// felitronics::session is built (modules/session/CMakeLists.txt; tools/wasm/build.sh runs it too). A host tool: it is
// compiled from this file and src/ConfigSchema.cpp — the library's own schema, the same text — and does not link the
// library it gates. It reads files, which the session may not (docs/SESSION.md, law 6), so it lives beside the suites,
// outside the module's laws.
//
//   config_check <targets.toml> <engine.toml>
//       Reads both documents by schema and prints every problem as `<file>:<line>:<column>: error: <fault> <key path>`,
//       the format compilers and IDEs understand. A problem is a red build, at its line.
//   config_check --expect <targets.toml> <engine.toml> <directory>
//       Writes what `fcore_session config` must print for these sources: <directory>/targets.toml and engine.toml, each
//       document through felitronics-toml's canonical writer, and version.txt and sound-version.txt, their versions.
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

// The canonical text of a document, or false with the reason on stderr.
bool canonical (const char* path, const std::string& text, std::string& out)
{
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

std::string hex (std::uint64_t v)
{
    char s[32];
    std::snprintf (s, sizeof s, "%016" PRIx64 "\n", v);
    return s;
}
} // namespace

int main (int argc, char** argv)
{
    const bool expect = argc == 5 && std::strcmp (argv[1], "--expect") == 0;
    if (argc != 3 && ! expect)
    {
        std::fprintf (stderr, "usage: %s <targets.toml> <engine.toml>\n"
                              "       %s --expect <targets.toml> <engine.toml> <directory>\n", argv[0], argv[0]);
        return 2;
    }
    const char* targetsPath = argv[expect ? 2 : 1];
    const char* enginePath = argv[expect ? 3 : 2];
    std::string targets, engine;
    if (! readFile (targetsPath, targets)) { std::fprintf (stderr, "config_check: cannot read %s\n", targetsPath); return 2; }
    if (! readFile (enginePath, engine)) { std::fprintf (stderr, "config_check: cannot read %s\n", enginePath); return 2; }
    if (! expect) return report (config::bind (targets, engine), targetsPath, enginePath);

    std::string targetsText, engineText;
    if (! canonical (targetsPath, targets, targetsText) || ! canonical (enginePath, engine, engineText)) return 2;
    const auto v = config::versionsOf (targets, engine);
    if (! v) return 2;   // canonical() above has already parsed both and said why
    const std::string dir = argv[4];
    if (! writeFile (dir + "/targets.toml", targetsText) || ! writeFile (dir + "/engine.toml", engineText)
        || ! writeFile (dir + "/version.txt", hex (v->all)) || ! writeFile (dir + "/sound-version.txt", hex (v->sound)))
    {
        std::fprintf (stderr, "config_check: cannot write into %s\n", argv[4]);
        return 2;
    }
    return 0;
}
