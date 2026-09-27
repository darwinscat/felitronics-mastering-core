// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// felitronics_session_text_check — THE TEXT'S GATE: the catalog and the formatting table checked by every build before
// felitronics::session is built (modules/session/CMakeLists.txt; tools/wasm/build.sh runs it too). A host tool compiled
// from this file and the library's own src/TextSchema.cpp — the same check the tests run in-process — so it does not link
// the library it gates. It reads files, which the session may not (docs/SESSION.md, law 6), so it lives beside the
// suites, outside the module's laws.
//
//   text_check <catalog.toml> <format.toml>
//       Checks both documents and prints every problem as `<file>:<line>:<column>: error: <fault> <key path>[ — <rule>]`,
//       the format compilers and IDEs understand. A problem is a red build, at its line.
//
// Exit status: 0 no problem; 1 problems, one line each on stderr; 2 a usage error or a file that cannot be read.

#include "TextSchema.h"

#include <cstdio>
#include <string>

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
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: %s <catalog.toml> <format.toml>\n", argv[0]);
        return 2;
    }
    std::string catalog, format;
    if (! readFile (argv[1], catalog)) { std::fprintf (stderr, "text_check: cannot read %s\n", argv[1]); return 2; }
    if (! readFile (argv[2], format)) { std::fprintf (stderr, "text_check: cannot read %s\n", argv[2]); return 2; }
    const auto problems = detail::checkText (catalog, format);
    for (const detail::Problem& p : problems)
    {
        const char* file = p.document == detail::Document::Catalog ? argv[1] : argv[2];
        const bool coded = p.code[0] != '\0';
        std::fprintf (stderr, "%s:%u:%u: error: %s%s%s%s%s\n", file, (unsigned) p.line, (unsigned) p.column,
                      detail::Problem::name (p.fault), p.path.empty() ? "" : " ", p.path.c_str(), coded ? " — " : "",
                      p.code);
    }
    return problems.empty() ? 0 : 1;
}
