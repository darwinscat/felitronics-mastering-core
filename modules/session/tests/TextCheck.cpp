// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// felitronics_session_text_check — THE TEXT'S GATE: the catalog and the formatting table checked by every build before
// felitronics::session is built (modules/session/CMakeLists.txt; tools/wasm/build.sh runs it too). A host tool compiled
// from this file and the library's own src/TextSchema.cpp — the same check the tests run in-process — so it does not link
// the library it gates. It reads files, which the session may not (docs/SESSION.md, law 6), so it lives beside the
// suites, outside the module's laws.
//
//   text_check <catalog.toml> <format.toml> <felitronics-bands checkout>
// — the checkout's languages.toml and text/<code>.toml give the named bands' words (src/TextSchema.h, checkBands).
//       Checks both documents and prints every problem as `<file>:<line>:<column>: error: <fault> <key path>[ — <rule>]`,
//       the format compilers and IDEs understand. A problem is a red build, at its line.
//
// Exit status: 0 no problem; 1 problems, one line each on stderr; 2 a usage error or a file that cannot be read.

#include "TextSchema.h"

#include <cstdio>
#include <string>
#include <vector>

namespace detail = felitronics::session::text::detail;

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
} // namespace

int main (int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf (stderr, "usage: %s <catalog.toml> <format.toml> <felitronics-bands checkout>\n", argv[0]);
        return 2;
    }
    std::string catalog, format, languages;
    const std::string bands = argv[3], languagesPath = bands + "/languages.toml";
    if (! readFile (argv[1], catalog)) { std::fprintf (stderr, "text_check: cannot read %s\n", argv[1]); return 2; }
    if (! readFile (argv[2], format)) { std::fprintf (stderr, "text_check: cannot read %s\n", argv[2]); return 2; }
    if (! readFile (languagesPath.c_str(), languages)) { std::fprintf (stderr, "text_check: cannot read %s\n", languagesPath.c_str()); return 2; }
    const std::vector<std::string> codes = detail::bandLanguages (languages);
    std::vector<std::string> sources (codes.size());
    std::vector<detail::BandText> texts;
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        const std::string path = bands + "/text/" + codes[i] + ".toml";
        if (! readFile (path.c_str(), sources[i])) { std::fprintf (stderr, "text_check: cannot read %s\n", path.c_str()); return 2; }
        texts.push_back ({ codes[i], sources[i] });
    }
    auto problems = detail::checkText (catalog, format);
    for (auto& p : detail::checkBands (catalog, languages, texts)) problems.push_back (std::move (p));
    for (const detail::Problem& p : problems)
    {
        std::string file = p.document == detail::Document::Catalog ? argv[1] : p.document == detail::Document::Format ? argv[2]
                         : p.document == detail::Document::BandLanguages ? languagesPath
                                                                          : bands + "/text/" + std::string (p.lang) + ".toml";
        const bool coded = p.code[0] != '\0';
        std::fprintf (stderr, "%s:%u:%u: error: %s%s%s%s%s\n", file.c_str(), (unsigned) p.line, (unsigned) p.column,
                      detail::Problem::name (p.fault), p.path.empty() ? "" : " ", p.path.c_str(), coded ? " — " : "",
                      p.code);
    }
    return problems.empty() ? 0 : 1;
}
