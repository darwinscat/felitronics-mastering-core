// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE TEXT'S GATE (src/TextSchema.h says what it holds, rule by rule). It reads the two documents as felitronics-toml
// parses them — every value and key with its line and column — and walks them against src/TextFacts.h: what the tables
// require and is absent is Missing at the table that lacks it; what nothing reads is UnknownKey at its key; a rule that
// refuses a value names itself in `code`. It allocates (it parses); the library's renderer never calls it.

#include "BuildGuards.h"

#include "TextSchema.h"
#include "TextFacts.h"

#include <felitronics/session/Text.h>
#include <felitronics/toml/Toml.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace felitronics::session::text::detail
{
namespace
{
namespace toml = felitronics::toml;

struct Checker
{
    Document document;
    std::vector<Problem>& out;

    void report (Fault fault, const toml::Position& at, std::string path, const char* code = "")
    {
        out.push_back ({ document, fault, at.line, at.column, std::move (path), code });
    }
};

std::string join (const std::string& path, std::string_view key)
{
    return path.empty() ? std::string (key) : path + "." + std::string (key);
}

const toml::Table* tableOf (const toml::Value* v) noexcept { return v != nullptr ? std::get_if<toml::Table> (&v->data) : nullptr; }
const std::string* stringOf (const toml::Value* v) noexcept { return v != nullptr ? std::get_if<std::string> (&v->data) : nullptr; }

// A table the tables require: Missing at its parent when absent, WrongType when it is something else.
const toml::Table* requireTable (Checker& c, const toml::Table& parent, std::string_view key, const std::string& path)
{
    const toml::Value* v = parent.find (key);
    if (v == nullptr) { c.report (Fault::Missing, parent.position, join (path, key)); return nullptr; }
    const toml::Table* t = tableOf (v);
    if (t == nullptr) c.report (Fault::WrongType, v->position, join (path, key));
    return t;
}

// A string the tables require, not empty.
const std::string* requireString (Checker& c, const toml::Table& parent, std::string_view key, const std::string& path)
{
    const toml::Value* v = parent.find (key);
    if (v == nullptr) { c.report (Fault::Missing, parent.position, join (path, key)); return nullptr; }
    const std::string* s = stringOf (v);
    if (s == nullptr) { c.report (Fault::WrongType, v->position, join (path, key)); return nullptr; }
    if (s->empty()) { c.report (Fault::Refused, v->position, join (path, key), "Empty"); return nullptr; }
    return s;
}

// Every key of `t` that `known` does not accept: nothing reads it.
template <typename Known>
void unknownKeys (Checker& c, const toml::Table& t, const std::string& path, Known&& known)
{
    for (const toml::Entry& e : t.entries())
        if (! known (std::string_view (e.key))) c.report (Fault::UnknownKey, e.keyPosition, join (path, e.key));
}

bool declares (const std::vector<Lang>& langs, std::string_view code) noexcept
{
    for (const Lang l : langs)
        if (kLangCodes[(std::size_t) l] == code) return true;
    return false;
}

bool braced (std::string_view s) noexcept
{
    return s.find ('{') != std::string_view::npos || s.find ('}') != std::string_view::npos;
}

//==============================================================================
// THE CATALOG

// Which arguments a language's texts place.
using Placed = std::array<bool, Fact::kMaxArgs>;

void scanMessage (Checker& c, const FactShape& shape, const std::string& text, const toml::Position& at,
                  const std::string& path, Placed& placed)
{
    if (text.empty()) { c.report (Fault::Refused, at, path, "Empty"); return; }
    for (std::size_t i = 0; i < text.size();)
    {
        const Piece p = pieceAt (text, i);
        if (p.kind == Piece::Kind::Malformed) { c.report (Fault::Refused, at, path, "BadPlaceholder"); return; }
        if (p.kind == Piece::Kind::Placeholder)
        {
            const int a = argIndex (shape, p.text);
            if (a < 0) c.report (Fault::Refused, at, path + ".{" + std::string (p.text) + "}", "StrayPlaceholder");
            else placed[(std::size_t) a] = true;
        }
        i += length (p);
    }
}

void checkMessage (Checker& c, const FactShape& shape, const toml::Table& message, const std::string& path,
                   const std::vector<Lang>& langs)
{
    const toml::Value* plural = message.find ("plural");
    const toml::Value* select = message.find ("select");
    if (plural != nullptr && select != nullptr)
        c.report (Fault::Refused, select->position, join (path, "select"), "TwoSelectors");
    const toml::Value* selectorValue = plural != nullptr ? plural : select;
    const bool isPlural = plural != nullptr, isSelect = ! isPlural && select != nullptr;
    int selector = -1;
    if (selectorValue != nullptr)
    {
        const std::string selectorPath = join (path, isPlural ? "plural" : "select");
        const std::string* name = stringOf (selectorValue);
        if (name == nullptr) c.report (Fault::WrongType, selectorValue->position, selectorPath);
        else if ((selector = argIndex (shape, *name)) < 0)
            c.report (Fault::Refused, selectorValue->position, selectorPath, "NotAnArgument");
        else
        {
            const ArgKind kind = shape.args[(std::size_t) selector].kind;
            const bool fits = isPlural ? kind == ArgKind::Value || kind == ArgKind::Count : kind == ArgKind::Term;
            if (! fits) { c.report (Fault::Refused, selectorValue->position, selectorPath, "SelectorKind"); selector = -1; }
        }
    }

    // The variants a language must give: its plural categories, or the selector's term group.
    const auto isVariant = [&] (Lang lang, std::string_view key) -> bool
    {
        if (isPlural)
        {
            for (std::size_t p = 0; p < kPluralKeys.size(); ++p)
                if (kPluralKeys[p] == key) return (pluralSet (lang) & bit ((Plural) p)) != 0;
            return false;
        }
        for (const TermShape& t : kTerms)
            if (t.group == shape.args[(std::size_t) selector].group && t.key == key) return true;
        return false;
    };

    std::vector<std::pair<Lang, Placed>> sets;
    for (const Lang lang : langs)
    {
        const std::string_view code = kLangCodes[(std::size_t) lang];
        const std::string langPath = join (path, code);
        const toml::Value* v = message.find (code);
        if (v == nullptr) { c.report (Fault::Missing, message.position, langPath); continue; }
        Placed placed {};
        if (selectorValue != nullptr)
        {
            const toml::Table* variants = tableOf (v);
            if (variants == nullptr) { c.report (Fault::WrongType, v->position, langPath); continue; }
            if (selector >= 0)
            {
                // Every variant the language needs, each a text; and none it cannot select.
                if (isPlural)
                {
                    for (std::size_t p = 0; p < kPluralKeys.size(); ++p)
                        if ((pluralSet (lang) & bit ((Plural) p)) != 0) (void) requireString (c, *variants, kPluralKeys[p], langPath);
                }
                else
                {
                    for (const TermShape& t : kTerms)
                        if (t.group == shape.args[(std::size_t) selector].group) (void) requireString (c, *variants, t.key, langPath);
                }
                unknownKeys (c, *variants, langPath, [&] (std::string_view key) { return isVariant (lang, key); });
            }
            // Each variant's text. With a working selector, what is absent, not a text or empty was reported above.
            for (const toml::Entry& e : variants->entries())
            {
                if (selector >= 0 && ! isVariant (lang, e.key)) continue;
                const std::string* s = stringOf (&e.value);
                if (s == nullptr && selector < 0) c.report (Fault::WrongType, e.value.position, join (langPath, e.key));
                if (s == nullptr || (s->empty() && selector >= 0)) continue;
                scanMessage (c, shape, *s, e.value.position, join (langPath, e.key), placed);
            }
        }
        else
        {
            const std::string* s = stringOf (v);
            if (s == nullptr) { c.report (Fault::WrongType, v->position, langPath); continue; }
            scanMessage (c, shape, *s, v->position, langPath, placed);
        }
        // Every argument placed — but the one a select chose on, which its variant already says.
        for (std::size_t a = 0; a < shape.argCount; ++a)
            if (! placed[a] && ! (isSelect && (int) a == selector))
                c.report (Fault::Refused, v->position, langPath + ".{" + std::string (shape.args[a].name) + "}", "LostPlaceholder");
        sets.emplace_back (lang, placed);
    }
    // The same placeholders in every language: repeated or reordered freely, never another set.
    for (std::size_t i = 1; i < sets.size(); ++i)
        if (sets[i].second != sets[0].second)
        {
            const std::string_view code = kLangCodes[(std::size_t) sets[i].first];
            c.report (Fault::Refused, message.find (code)->position, join (path, code), "PlaceholderSet");
        }
    unknownKeys (c, message, path, [&] (std::string_view key)
    {
        return key == "plural" || key == "select" || declares (langs, key);
    });
}

void checkCatalog (Checker& c, const toml::Table& root)
{
    std::vector<Lang> langs;
    if (const toml::Value* v = root.find ("languages"); v == nullptr) c.report (Fault::Missing, root.position, "languages");
    else if (const auto* items = std::get_if<toml::Array> (&v->data); items == nullptr)
        c.report (Fault::WrongType, v->position, "languages");
    else
    {
        if (items->empty()) c.report (Fault::Refused, v->position, "languages", "NoLanguage");
        for (const toml::Value& item : *items)
        {
            const std::string* code = stringOf (&item);
            if (code == nullptr) { c.report (Fault::WrongType, item.position, "languages"); continue; }
            const auto lang = langOfCode (*code);
            if (! lang) { c.report (Fault::Refused, item.position, "languages", "NotALanguage"); continue; }
            if (declares (langs, *code)) { c.report (Fault::Refused, item.position, "languages", "Duplicate"); continue; }
            langs.push_back (*lang);
        }
    }

    if (const toml::Table* terms = requireTable (c, root, "terms", ""))
    {
        for (std::size_t i = 0; i < kTermCount; ++i)
        {
            const std::string_view group = kTerms[i].group;
            bool first = true;                                  // each group once, where it first appears
            for (std::size_t j = 0; j < i; ++j) first = first && kTerms[j].group != group;
            if (! first) continue;
            const std::string groupPath = join ("terms", group);
            const toml::Table* g = requireTable (c, *terms, group, "terms");
            if (g == nullptr) continue;
            for (const TermShape& t : kTerms)
            {
                if (t.group != group) continue;
                const std::string termPath = join (groupPath, t.key);
                const toml::Table* words = requireTable (c, *g, t.key, groupPath);
                if (words == nullptr) continue;
                for (const Lang lang : langs)
                    if (const std::string* s = requireString (c, *words, kLangCodes[(std::size_t) lang], termPath); s != nullptr && braced (*s))
                        c.report (Fault::Refused, words->find (kLangCodes[(std::size_t) lang])->position,
                                  join (termPath, kLangCodes[(std::size_t) lang]), "Brace");
                unknownKeys (c, *words, termPath, [&] (std::string_view key) { return declares (langs, key); });
            }
            unknownKeys (c, *g, groupPath, [&] (std::string_view key)
            {
                for (const TermShape& t : kTerms)
                    if (t.group == group && t.key == key) return true;
                return false;
            });
        }
        unknownKeys (c, *terms, "terms", [] (std::string_view key)
        {
            for (const TermShape& t : kTerms)
                if (t.group == key) return true;
            return false;
        });
    }

    if (const toml::Table* messages = requireTable (c, root, "messages", ""))
    {
        for (const FactShape& shape : kFacts)
            if (const toml::Table* m = requireTable (c, *messages, shape.key, "messages"))
                checkMessage (c, shape, *m, join ("messages", shape.key), langs);
        unknownKeys (c, *messages, "messages", [] (std::string_view key)
        {
            for (const FactShape& shape : kFacts)
                if (shape.key == key) return true;
            return false;
        });
    }
    unknownKeys (c, root, "", [] (std::string_view key) { return key == "languages" || key == "terms" || key == "messages"; });
}

//==============================================================================
// THE FORMATTING TABLE

constexpr std::array<std::string_view, 7> kSigns = { "decimal", "group", "minus", "plus", "atLeast", "atMost", "absent" };

bool separatorShaped (std::string_view s) noexcept
{
    for (const char ch : s)
        if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-') return false;
    return true;
}

void checkNumbers (Checker& c, const toml::Table& row, const std::string& path)
{
    std::array<const std::string*, kSigns.size()> sign {};
    for (std::size_t i = 0; i < kSigns.size(); ++i) sign[i] = requireString (c, row, kSigns[i], path);
    for (std::size_t i = 0; i < 2; ++i)                  // decimal, group
        if (sign[i] != nullptr && ! separatorShaped (*sign[i]))
            c.report (Fault::Refused, row.find (kSigns[i])->position, join (path, kSigns[i]), "NotASeparator");
    if (sign[0] != nullptr && sign[1] != nullptr && *sign[0] == *sign[1])
        c.report (Fault::Refused, row.find ("group")->position, join (path, "group"), "DecimalIsGroup");

    if (const toml::Value* v = row.find ("minimumGrouping"); v == nullptr)
        c.report (Fault::Missing, row.position, join (path, "minimumGrouping"));
    else if (const auto* n = std::get_if<std::int64_t> (&v->data); n == nullptr)
        c.report (Fault::WrongType, v->position, join (path, "minimumGrouping"));
    else if (*n < 1 || *n > 4)
        c.report (Fault::OutOfRange, v->position, join (path, "minimumGrouping"));

    if (const toml::Value* v = row.find ("notes"); v == nullptr) c.report (Fault::Missing, row.position, join (path, "notes"));
    else if (const auto* names = std::get_if<toml::Array> (&v->data); names == nullptr)
        c.report (Fault::WrongType, v->position, join (path, "notes"));
    else
    {
        if (names->size() != 12) c.report (Fault::Refused, v->position, join (path, "notes"), "NotTwelveNotes");
        for (const toml::Value& name : *names)
        {
            const std::string* s = stringOf (&name);
            if (s == nullptr) c.report (Fault::WrongType, name.position, join (path, "notes"));
            else if (s->empty()) c.report (Fault::Refused, name.position, join (path, "notes"), "Empty");
        }
    }
    unknownKeys (c, row, path, [] (std::string_view key)
    {
        for (const std::string_view k : kSigns)
            if (k == key) return true;
        return key == "minimumGrouping" || key == "notes";
    });
}

// A unit's pattern: exactly one {n}, where the number goes, and no other brace.
bool patternShaped (std::string_view pattern) noexcept
{
    const std::size_t at = pattern.find ("{n}");
    if (at == std::string_view::npos) return false;
    return ! braced (pattern.substr (0, at)) && ! braced (pattern.substr (at + 3));
}

void checkFormat (Checker& c, const toml::Table& root)
{
    const auto isLang = [] (std::string_view key) { return langOfCode (key).has_value(); };
    if (const toml::Table* numbers = requireTable (c, root, "numbers", ""))
    {
        for (const std::string_view code : kLangCodes)
            if (const toml::Table* row = requireTable (c, *numbers, code, "numbers"))
                checkNumbers (c, *row, join ("numbers", code));
        unknownKeys (c, *numbers, "numbers", isLang);
    }
    if (const toml::Table* units = requireTable (c, root, "units", ""))
    {
        for (std::size_t u = 1; u < kUnitCount; ++u)
        {
            const std::string unitPath = join ("units", kUnitKeys[u]);
            const toml::Table* patterns = requireTable (c, *units, kUnitKeys[u], "units");
            if (patterns == nullptr) continue;
            for (const std::string_view code : kLangCodes)
                if (const std::string* p = requireString (c, *patterns, code, unitPath); p != nullptr && ! patternShaped (*p))
                    c.report (Fault::Refused, patterns->find (code)->position, join (unitPath, code), "Pattern");
            unknownKeys (c, *patterns, unitPath, isLang);
        }
        unknownKeys (c, *units, "units", [] (std::string_view key)
        {
            for (std::size_t u = 1; u < kUnitCount; ++u)
                if (kUnitKeys[u] == key) return true;
            return false;
        });
    }
    unknownKeys (c, root, "", [] (std::string_view key) { return key == "numbers" || key == "units"; });
}

template <typename Check>
void checkDocument (Document document, std::string_view text, std::vector<Problem>& out, Check&& check)
{
    Checker c { document, out };
    const auto parsed = toml::parse (text);
    if (const auto* e = std::get_if<toml::Error> (&parsed))
    {
        out.push_back ({ document, Fault::Syntax, e->line, e->column, "", toml::codeName (e->code) });
        return;
    }
    // get_if, not get: std::get's refusal is a throw, which a build without exceptions turns into an abort call.
    if (const auto* root = std::get_if<toml::Table> (&parsed)) check (c, *root);
}
} // namespace

const char* Problem::name (Document document) noexcept
{
    return document == Document::Catalog ? "catalog" : "format";
}

const char* Problem::name (Fault fault) noexcept
{
    switch (fault)
    {
        case Fault::Syntax: return "Syntax";
        case Fault::Missing: return "Missing";
        case Fault::WrongType: return "WrongType";
        case Fault::OutOfRange: return "OutOfRange";
        case Fault::UnknownKey: return "UnknownKey";
        case Fault::Refused: return "Refused";
    }
    return "";
}

std::vector<Problem> checkText (std::string_view catalogToml, std::string_view formatToml)
{
    std::vector<Problem> out;
    checkDocument (Document::Catalog, catalogToml, out, checkCatalog);
    checkDocument (Document::Format, formatToml, out, checkFormat);
    return out;
}

} // namespace felitronics::session::text::detail
