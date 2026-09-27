// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE SHAPES OF THE FACTS (internal to modules/session): what the code states and the catalog is held to. The renderer
// (src/Text.cpp) and the build's gate (src/TextSchema.cpp, run by felitronics_session_text_check before the library is
// built) read the same tables from here, so the catalog is checked against the declarations the renderer uses.
//
//   * kFacts — for each FactId, its key in modules/session/text/catalog.toml and its arguments IN ORDER, each with the
//     name its placeholder spells ({passes}) and its kind; an argument of kind Term names the term group it takes.
//   * kTerms — for each Term, its group and its key: terms.<group>.<key> in the catalog.
//   * the twelve languages' codes, the units' keys in modules/session/text/format.toml, and each language's CLDR plural
//     categories (the gate requires exactly these in a plural message; src/TextNumber.cpp selects among them).
//   * the placeholder grammar: `{` name `}`, the name an ASCII letter followed by letters and digits. Nothing else in a
//     message may be a brace: the gate refuses one, so a message never needs an escape.
//
// ADDING A FACT: a FactId in <felitronics/session/Text.h> with the next number, its row here, and its message in every
// language the catalog declares — the build is red until all three agree.

#include <felitronics/session/Text.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace felitronics::session::text::detail
{

struct ArgShape
{
    std::string_view name;            // what its placeholder spells, without the braces
    ArgKind kind = ArgKind::None;
    std::string_view group;           // Term: the term group of the catalog it takes; empty otherwise
};

struct FactShape
{
    FactId id = FactId::Value;
    std::string_view key;             // messages.<key> in the catalog; the id a missing message renders as
    std::array<ArgShape, Fact::kMaxArgs> args {};
    std::size_t argCount = 0;
};

inline constexpr FactShape kFacts[] = {
    { FactId::Value, "value", { { { "value", ArgKind::Value, {} } } }, 1 },
    { FactId::LandingPass, "landingPass",
      { { { "pass", ArgKind::Count, {} }, { "passes", ArgKind::Count, {} } } }, 2 },
    { FactId::LandingConverged, "landingConverged", { { { "passes", ArgKind::Count, {} } } }, 1 },
    { FactId::PairsConsistent, "pairsConsistent",
      { { { "agreed", ArgKind::Count, {} }, { "pairs", ArgKind::Count, {} } } }, 2 },
    { FactId::LoudestLowNote, "loudestLowNote", { { { "note", ArgKind::Midi, {} } } }, 1 },
    { FactId::WideBass, "wideBass", { { { "side", ArgKind::Value, {} } } }, 1 },
    { FactId::RateAboveLimit, "rateAboveLimit",
      { { { "rate", ArgKind::Value, {} }, { "limit", ArgKind::Value, {} }, { "platform", ArgKind::Term, "platform" } } },
      3 },
};
inline constexpr std::size_t kFactCount = sizeof (kFacts) / sizeof (kFacts[0]);

struct TermShape
{
    Term id {};
    std::string_view group;           // terms.<group>.<key> in the catalog
    std::string_view key;
};

inline constexpr TermShape kTerms[] = {
    { Term::PlatformWeb, "platform", "web" },
    { Term::PlatformDesktop, "platform", "desktop" },
};
inline constexpr std::size_t kTermCount = sizeof (kTerms) / sizeof (kTerms[0]);

// The position of the argument a placeholder names, or −1 for a name the fact does not have.
[[nodiscard]] constexpr int argIndex (const FactShape& shape, std::string_view name) noexcept
{
    for (std::size_t i = 0; i < shape.argCount; ++i)
        if (shape.args[i].name == name) return (int) i;
    return -1;
}

// The ids are the tables' positions plus one: a lookup is an index, and the tables are in id order with no gap.
[[nodiscard]] constexpr bool idsArePositions() noexcept
{
    for (std::size_t i = 0; i < kFactCount; ++i)
        if ((std::size_t) kFacts[i].id != i + 1 || kFacts[i].argCount > Fact::kMaxArgs) return false;
    for (std::size_t i = 0; i < kTermCount; ++i)
        if ((std::size_t) kTerms[i].id != i + 1) return false;
    return true;
}
static_assert (idsArePositions(), "kFacts and kTerms list every id in order, from 1, with no gap");

// The shape of a fact, or null for an id the table does not have.
[[nodiscard]] constexpr const FactShape* shapeOf (FactId id) noexcept
{
    const auto i = (std::size_t) id;
    return i >= 1 && i <= kFactCount ? &kFacts[i - 1] : nullptr;
}
[[nodiscard]] constexpr const TermShape* shapeOf (Term id) noexcept
{
    const auto i = (std::size_t) id;
    return i >= 1 && i <= kTermCount ? &kTerms[i - 1] : nullptr;
}

//==============================================================================
// Languages and units, by their keys in the documents.

inline constexpr std::array<std::string_view, kLangCount> kLangCodes = {
    "en", "de", "ru", "uk", "cs", "es", "fr", "it", "pl", "pt", "ro", "tr" };

// The language of a code, or nullopt for one that is not one of the twelve.
[[nodiscard]] constexpr std::optional<Lang> langOfCode (std::string_view code) noexcept
{
    for (std::size_t i = 0; i < kLangCount; ++i)
        if (kLangCodes[i] == code) return (Lang) i;
    return std::nullopt;
}

// Unit::None has no key: the number alone has no pattern.
inline constexpr std::array<std::string_view, kUnitCount> kUnitKeys = {
    "", "percent", "db", "dbtp", "dbfs", "lufs", "lu", "hz", "khz", "ms", "s", "bpm" };

//==============================================================================
// CLDR's plural categories: their keys in a plural message, and each language's set (CLDR 48 cardinal rules, as ICU 78
// states them). A set is a bit per category.

inline constexpr std::array<std::string_view, 6> kPluralKeys = { "zero", "one", "two", "few", "many", "other" };

[[nodiscard]] constexpr std::uint8_t bit (Plural p) noexcept { return (std::uint8_t) (1u << (unsigned) p); }

[[nodiscard]] constexpr std::uint8_t pluralSet (Lang lang) noexcept
{
    const std::uint8_t oneOther = bit (Plural::One) | bit (Plural::Other);
    switch (lang)
    {
        case Lang::En: case Lang::De: case Lang::Tr:
            return oneOther;
        case Lang::Es: case Lang::Fr: case Lang::It: case Lang::Pt:
            return oneOther | bit (Plural::Many);
        case Lang::Ro:
            return oneOther | bit (Plural::Few);
        case Lang::Ru: case Lang::Uk: case Lang::Pl: case Lang::Cs:
            return oneOther | bit (Plural::Few) | bit (Plural::Many);
    }
    return bit (Plural::Other);
}

//==============================================================================
// THE PLACEHOLDER GRAMMAR, one scanner for the renderer and the gate. A message is text and placeholders; `{name}` is a
// placeholder when the name is an ASCII letter followed by ASCII letters and digits. Any other brace is malformed.

struct Piece
{
    enum class Kind : std::uint8_t { Text, Placeholder, Malformed } kind = Kind::Text;
    std::string_view text;            // Text: the bytes; Placeholder: the name; Malformed: from the brace on
    std::size_t offset = 0;           // where it starts in the message, in bytes
};

[[nodiscard]] constexpr bool nameStart (char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] constexpr bool nameChar (char c) noexcept { return nameStart (c) || (c >= '0' && c <= '9'); }

// The piece of `message` that starts at `at`; a message is walked by calling this until `at` reaches its size.
[[nodiscard]] constexpr Piece pieceAt (std::string_view message, std::size_t at) noexcept
{
    if (at >= message.size()) return { Piece::Kind::Text, {}, at };
    const char c = message[at];
    if (c == '}') return { Piece::Kind::Malformed, message.substr (at), at };
    if (c != '{')
    {
        std::size_t end = at;
        while (end < message.size() && message[end] != '{' && message[end] != '}') ++end;
        return { Piece::Kind::Text, message.substr (at, end - at), at };
    }
    std::size_t end = at + 1;
    if (end >= message.size() || ! nameStart (message[end])) return { Piece::Kind::Malformed, message.substr (at), at };
    while (end < message.size() && nameChar (message[end])) ++end;
    if (end >= message.size() || message[end] != '}') return { Piece::Kind::Malformed, message.substr (at), at };
    return { Piece::Kind::Placeholder, message.substr (at + 1, end - at - 1), at };
}

// How many bytes of the message the piece took (a malformed piece takes the rest: nothing after it is read).
[[nodiscard]] constexpr std::size_t length (const Piece& p) noexcept
{
    return p.kind == Piece::Kind::Placeholder ? p.text.size() + 2 : p.text.size();
}

} // namespace felitronics::session::text::detail
