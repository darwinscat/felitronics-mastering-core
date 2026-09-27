// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// What the config suites share: reading the source documents, planting one change in a copy of one, and exact equality of
// two doubles that are the correctly rounded values of decimals.

#include <felitronics/toml/Toml.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <variant>

namespace felitronics::session::config::testing
{

inline bool readFile (const char* path, std::string& out)
{
    std::FILE* f = std::fopen (path, "rb");
    if (f == nullptr) return false;
    char buf[4096];
    for (std::size_t n; (n = std::fread (buf, 1, sizeof buf, f)) > 0;) out.append (buf, n);
    const bool failed = std::ferror (f) != 0;
    std::fclose (f);
    return ! failed;
}

// Exact equality without -Wfloat-equal's objection: every number compared is a decimal of a document, read through
// felitronics-toml's correctly rounded conversion, so it is the literal's own double.
inline bool same (double a, double b) { return ! (a < b) && ! (b < a); }

inline std::string canonicalOf (const std::string& text)
{
    const auto parsed = felitronics::toml::parse (text);
    const auto* t = std::get_if<felitronics::toml::Table> (&parsed);
    return t != nullptr ? felitronics::toml::write (*t) : std::string{};
}

struct Plant
{
    std::string text;
    std::uint32_t line = 0, column = 0;   // where `at` begins, searched from where the plant went in
    bool planted = false;
};

// `text` with its one occurrence of `from` replaced by `to`, and the position of `at`, found from the plant onwards. A
// `from` that is not in the text exactly once is a control that has rotted, and fails as one.
inline Plant plant (const std::string& text, std::string_view from, std::string_view to, std::string_view at)
{
    Plant p;
    const auto first = text.find (from);
    if (first == std::string::npos || text.find (from, first + 1) != std::string::npos) return p;
    p.text = text.substr (0, first) + std::string (to) + text.substr (first + from.size());
    const auto where = p.text.find (at, first);
    if (where == std::string::npos) return p;
    p.line = 1;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < where; ++i)
        if (p.text[i] == '\n') { ++p.line; lineStart = i + 1; }
    p.column = 1;
    for (std::size_t i = lineStart; i < where; ++i)
        if ((static_cast<unsigned char> (p.text[i]) & 0xC0u) != 0x80u) ++p.column;   // characters, not bytes
    p.planted = true;
    return p;
}

} // namespace felitronics::session::config::testing
