// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE TEXT'S GATE AND ITS SEAM (internal to modules/session). src/TextSchema.cpp checks the catalog
// (modules/session/text/catalog.toml) and the formatting table (modules/session/text/format.toml) against the shapes of
// src/TextFacts.h. It is compiled twice, as the config's schema is: into the library, where the tests call it on planted
// copies, and into felitronics_session_text_check, the host tool every build runs over the source documents before the
// library is built (modules/session/CMakeLists.txt) — one check, one text of code, and the tool does not link the library
// it gates. What it holds is <felitronics/session/Text.h>'s "The build holds the catalog", item by item:
//
//   catalog   `languages`: one or more of the twelve codes, none twice. [terms.<group>]: every term of kTerms, in every
//             declared language, a non-empty string with no brace. [messages.<key>]: every fact of kFacts, in every
//             declared language — a string, or, under `plural = "<argument>"` (a Value or a Count) exactly the
//             language's CLDR categories, under `select = "<argument>"` (a Term) exactly its group's term keys. Every
//             message text — every variant on its own — no malformed brace, no placeholder that names no argument, every
//             argument placed (a select argument may be left out: its variant already says it); and the same set of
//             placeholders in every language. No key nothing reads: no message, term, group, language or field the
//             tables do not know.
//   format    [numbers.<code>] for each of the twelve: decimal and group (non-empty, no digit, no sign — ASCII's or the
//             row's own minus and plus — and not equal),
//             minimumGrouping (1 … 4), minus, plus, atLeast, atMost, absent (non-empty), notes (twelve non-empty names,
//             from C). [units.<key>] for each unit but None: a pattern for each of the twelve with exactly one {n} and
//             no other brace. No key nothing reads.

#include <felitronics/session/Text.h>
#include <felitronics/toml/Embedded.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace felitronics::session::text::detail
{

enum class Document : std::uint8_t { Catalog, Format };

enum class Fault : std::uint8_t
{
    Syntax,        // the document does not parse; `code` is the parser's
    Missing,       // a key the tables require is absent; points at the table that lacks it
    WrongType,     // the value has another type
    OutOfRange,    // a number outside its range
    UnknownKey,    // a key nothing reads — a typo, a language the catalog does not declare, a message no fact has
    Refused        // a rule refused it; `code` names the rule
};

struct Problem
{
    Document document = Document::Catalog;
    Fault fault = Fault::Syntax;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string path;
    const char* code = "";         // the parser's code for Syntax, the rule for Refused

    [[nodiscard]] static const char* name (Document document) noexcept;   // "catalog", "format"
    [[nodiscard]] static const char* name (Fault fault) noexcept;
};

// Both documents, whole; no problem when they hold. A document that does not parse is one Syntax problem, and the checks
// that need it are left out.
[[nodiscard]] std::vector<Problem> checkText (std::string_view catalogToml, std::string_view formatToml);

// The two documents compiled into the library (src/Text.cpp): what the tests hold to the sources.
[[nodiscard]] felitronics::toml::embedded::View catalogRoot() noexcept;
[[nodiscard]] felitronics::toml::embedded::View formatRoot() noexcept;

// One argument alone, as a message would place it, in any of the twelve languages — whether the catalog declares it or
// not: how the tests reach the formatting table's rows the catalog does not use yet (src/Text.cpp).
[[nodiscard]] std::string argText (const Arg& arg, Lang lang);

} // namespace felitronics::session::text::detail
