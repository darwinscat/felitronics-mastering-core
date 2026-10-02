// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE RENDERER (<felitronics/session/Text.h>). modules/session/CMakeLists.txt embeds modules/session/text/catalog.toml and
// format.toml with felitronics_toml_embed; this file walks those two constexpr documents — a lookup is a binary search
// over a table's keys, nothing is parsed and nothing allocated — and writes a fact's message with its arguments
// formatted in place. One walk serves both size() and write(): it counts, or it copies, so the two cannot disagree.
//
// What the build's gate has proved (src/TextSchema.cpp) is relied on, and what it cannot prove is not: a fact built
// with arguments that do not match its declaration — one missing, one too many, one of another kind or with a field the
// renderer does not know — is incomplete and prints nothing (Text::complete), never its template; and a message the
// catalog does not have prints its id, never another language's.
//
// No floating-point operation decides what is printed: the digits are std::to_chars's shortest decimal rounded in
// characters and integers (src/TextNumber.cpp) and the signs are read from the bits, so the thread's floating-point
// environment cannot change a rendering. Text::parse does
// divide, and asks for the default environment first.

#include "BuildGuards.h"

#include <felitronics/session/Session.h>
#include <felitronics/session/Text.h>

#include "TextFacts.h"
#include "Grid.h"
#include "BuildContract.h"
#include "TextNumber.h"
#include "TextSchema.h"
#include "embedded/catalog.h"   // generated at build time from modules/session/text/catalog.toml
#include "embedded/format.h"    // ... and from modules/session/text/format.toml

#include <felitronics/toml/Embedded.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace felitronics::session::text
{
namespace detail
{
felitronics::toml::embedded::View catalogRoot() noexcept { return embedded::catalog.root(); }
felitronics::toml::embedded::View formatRoot() noexcept { return embedded::format.root(); }
} // namespace detail

namespace
{
using View = felitronics::toml::embedded::View;
using detail::Digits;

std::string_view textOf (View v) noexcept
{
    const auto s = v.string();
    if (! s) felitronics::session::detail::storageOverflow();
    return *s;
}

bool known (Lang lang) noexcept { return (std::size_t) lang < kLangCount; }

// Where the rendered bytes go: counted only (`out` null), or copied into `out`, never past `capacity`.
struct Sink
{
    char* out = nullptr;
    std::size_t capacity = 0;
    std::size_t n = 0;

    void put (std::string_view s) noexcept
    {
        if (out != nullptr && ! s.empty())
        {
            felitronics::session::detail::debugBound (n <= capacity && s.size() <= capacity - n);
            std::memcpy (out + n, s.data(), s.size());
        }
        n += s.size();
    }
};

// One language's row of the formatting table.
View numbersOf (Lang lang) noexcept
{
    return detail::formatRoot().find ("numbers").find (detail::kLangCodes[(std::size_t) lang]);
}

// The integer digits, grouped as the row says, then the decimal sign and the fraction digits.
void putDigits (Sink& s, const Digits& d, View row) noexcept
{
    const std::string_view integer = d.integer();
    const auto grouping = row.find ("minimumGrouping").integer();
    if (! grouping) felitronics::session::detail::storageOverflow();
    const std::int64_t minimum = *grouping;
    if (integer.size() < 3 + (std::size_t) minimum) s.put (integer);
    else
    {
        const std::string_view group = textOf (row.find ("group"));
        std::size_t head = integer.size() % 3;
        if (head == 0) head = 3;
        s.put (integer.substr (0, head));
        for (std::size_t i = head; i < integer.size(); i += 3)
        {
            s.put (group);
            s.put (integer.substr (i, 3));
        }
    }
    if (d.fractionDigits != 0)
    {
        s.put (textOf (row.find ("decimal")));
        s.put (d.fraction());
    }
}

// A number: [bound] [sign] the unit's pattern around the digits — or the absent sign alone.
void putValue (Sink& s, const Arg& a, Lang lang) noexcept
{
    const View row = numbersOf (lang);
    Digits d;
    if (! detail::gridDigits (a.number, a.precision, d)) { s.put (textOf (row.find ("absent"))); return; }
    if (a.bound == Bound::AtLeast) s.put (textOf (row.find ("atLeast")));
    else if (a.bound == Bound::AtMost) s.put (textOf (row.find ("atMost")));
    if (d.negative) s.put (textOf (row.find ("minus")));
    else if (a.sign == Sign::Always && detail::signOf (a.number) > 0 && ! d.zero()) s.put (textOf (row.find ("plus")));
    const std::string_view pattern = a.unit == Unit::None
        ? std::string_view ("{n}")
        : textOf (detail::formatRoot().find ("units").find (detail::kUnitKeys[(std::size_t) a.unit])
                                      .find (detail::kLangCodes[(std::size_t) lang]));
    const std::size_t at = pattern.find ("{n}");
    if (at == std::string_view::npos) felitronics::session::detail::storageOverflow();
    s.put (pattern.substr (0, at));
    putDigits (s, d, row);
    s.put (pattern.substr (at + 3));
}

void putCount (Sink& s, std::int64_t n, Lang lang) noexcept
{
    const View row = numbersOf (lang);
    Digits d;
    detail::integerDigits (n, d);
    if (d.negative) s.put (textOf (row.find ("minus")));
    putDigits (s, d, row);
}

// A MIDI note: the language's name of its pitch class and the octave of scientific pitch notation (60 is C4, 21 is A0,
// 0 is C−1), or the absent sign outside 0 … 127.
void putNote (Sink& s, std::int64_t note, Lang lang) noexcept
{
    const View row = numbersOf (lang);
    if (note < 0 || note > 127) { s.put (textOf (row.find ("absent"))); return; }
    s.put (textOf (row.find ("notes")[(std::size_t) (note % 12)]));
    const std::int64_t octave = note / 12 - 1;
    if (octave < 0) s.put (textOf (row.find ("minus")));
    const char digit = (char) ('0' + (octave < 0 ? -octave : octave));
    s.put (std::string_view (&digit, 1));
}

void putTerm (Sink& s, const detail::TermShape& t, Lang lang) noexcept
{
    const View word = detail::catalogRoot().find ("terms").find (t.group).find (t.key).find (detail::kLangCodes[(std::size_t) lang]);
    if (Text::speaks (lang)) s.put (textOf (word));
    else { s.put (t.group); s.put ("."); s.put (t.key); }
}

// Does the argument match its declaration, and is every field of it one the renderer knows?
bool fits (const Arg& a, const detail::ArgShape& shape) noexcept
{
    if (a.kind != shape.kind) return false;
    switch (a.kind)
    {
        case ArgKind::Value:
            return a.precision <= detail::kMaxPrecision && (std::size_t) a.unit < kUnitCount
                && (a.sign == Sign::Negative || a.sign == Sign::Always)
                && (a.bound == Bound::Exact || a.bound == Bound::AtLeast || a.bound == Bound::AtMost);
        case ArgKind::Term:
        {
            const detail::TermShape* t = detail::shapeOf (a.termId);
            return t != nullptr && t->group == shape.group;
        }
        case ArgKind::None: return false;
        case ArgKind::Count: case ArgKind::Midi: case ArgKind::UserText: return true;
    }
    return false;
}

// Does the fact carry exactly the arguments its declaration names, each fitting it?
bool whole (const Fact& fact, const detail::FactShape& shape) noexcept
{
    if (fact.argCount != shape.argCount || fact.argCount > Fact::kMaxArgs) return false;
    for (std::size_t i = 0; i < shape.argCount; ++i)
        if (! fits (fact.args[i], shape.args[i])) return false;
    return true;
}

// The fact's argument at `index`, if the fact has it and it fits.
const Arg* argAt (const Fact& fact, const detail::FactShape& shape, std::size_t index) noexcept
{
    if (index >= shape.argCount || index >= fact.argCount || index >= Fact::kMaxArgs) return nullptr;
    return fits (fact.args[index], shape.args[index]) ? &fact.args[index] : nullptr;
}

void putArg (Sink& s, const Fact& fact, const detail::FactShape& shape, std::size_t index, Lang lang) noexcept
{
    const Arg* a = argAt (fact, shape, index);
    if (a == nullptr) return;   // not reached: render() takes only a whole fact
    switch (a->kind)
    {
        case ArgKind::Value: putValue (s, *a, lang); return;
        case ArgKind::Count: putCount (s, a->integer, lang); return;
        case ArgKind::Midi: putNote (s, a->integer, lang); return;
        case ArgKind::UserText: s.put (a->userText); return;
        case ArgKind::Term: putTerm (s, *detail::shapeOf (a->termId), lang); return;
        case ArgKind::None: return;
    }
}

Plural categoryOf (const Arg& a, Lang lang) noexcept
{
    Digits d;
    if (a.kind == ArgKind::Value)
    {
        if (! detail::gridDigits (a.number, a.precision, d)) return Plural::Other;
    }
    else if (a.kind == ArgKind::Count) detail::integerDigits (a.integer, d);
    else return Plural::Other;
    return detail::pluralOf (lang, d.integer(), d.fraction());
}

// The text a fact's message has in `lang` for these arguments: the plain message, or the variant its selector chose.
// Empty when the catalog has none.
std::string_view chosenText (const Fact& fact, const detail::FactShape& shape, Lang lang) noexcept
{
    if (! Text::speaks (lang)) return {};
    const View message = detail::catalogRoot().find ("messages").find (shape.key);
    const View entry = message.find (detail::kLangCodes[(std::size_t) lang]);
    if (entry.is (felitronics::toml::embedded::Type::String)) return textOf (entry);
    if (! entry.is (felitronics::toml::embedded::Type::Table)) felitronics::session::detail::storageOverflow();
    if (const auto name = message.find ("plural").string())
    {
        const int i = detail::argIndex (shape, *name);
        const Arg* a = i >= 0 ? argAt (fact, shape, (std::size_t) i) : nullptr;
        return a != nullptr ? textOf (entry.find (detail::kPluralKeys[(std::size_t) categoryOf (*a, lang)])) : std::string_view {};
    }
    if (const auto name = message.find ("select").string())
    {
        const int i = detail::argIndex (shape, *name);
        const Arg* a = i >= 0 ? argAt (fact, shape, (std::size_t) i) : nullptr;
        return a != nullptr && a->kind == ArgKind::Term ? textOf (entry.find (detail::shapeOf (a->termId)->key)) : std::string_view {};
    }
    return {};
}

// One argument, by itself (detail::argText).
void renderArg (Sink& s, const Arg& a, Lang lang) noexcept
{
    switch (a.kind)
    {
        case ArgKind::Value:
            if (a.precision <= detail::kMaxPrecision && (std::size_t) a.unit < kUnitCount) putValue (s, a, lang);
            return;
        case ArgKind::Count: putCount (s, a.integer, lang); return;
        case ArgKind::Midi: putNote (s, a.integer, lang); return;
        case ArgKind::UserText: s.put (a.userText); return;
        case ArgKind::Term:
            if (const detail::TermShape* t = detail::shapeOf (a.termId)) putTerm (s, *t, lang);
            return;
        case ArgKind::None: return;
    }
}

void render (Sink& s, const Fact& fact, Lang lang) noexcept
{
    const detail::FactShape* shape = detail::shapeOf (fact.id);
    if (shape == nullptr)
    {
        // Not an id of this library at all: its number, so the mistake is visible.
        Digits d;
        detail::integerDigits ((std::int64_t) fact.id, d);
        s.put ("#");
        s.put (d.integer());
        return;
    }
    if (! whole (fact, *shape)) return;   // incomplete: nothing, never the template with a hole in it
    const std::string_view message = known (lang) ? chosenText (fact, *shape, lang) : std::string_view {};
    if (message.empty()) { s.put (shape->key); return; }
    for (std::size_t at = 0; at < message.size();)
    {
        const detail::Piece p = detail::pieceAt (message, at);
        const int arg = p.kind == detail::Piece::Kind::Placeholder ? detail::argIndex (*shape, p.text) : -1;
        if (arg >= 0) putArg (s, fact, *shape, (std::size_t) arg, lang);
        else if (p.kind == detail::Piece::Kind::Placeholder) { s.put ("{"); s.put (p.text); s.put ("}"); }
        else s.put (p.text);
        at += detail::length (p);
    }
}
} // namespace

//==============================================================================

Arg Arg::value (double number, Unit unit, std::uint8_t precision, Sign sign, Bound bound) noexcept
{
    Arg a;
    a.kind = ArgKind::Value;
    a.number = number;
    a.unit = unit;
    a.precision = precision;
    a.sign = sign;
    a.bound = bound;
    return a;
}

Arg Arg::count (std::int64_t n) noexcept
{
    Arg a;
    a.kind = ArgKind::Count;
    a.integer = n;
    return a;
}

Arg Arg::term (Term t) noexcept
{
    Arg a;
    a.kind = ArgKind::Term;
    a.termId = t;
    return a;
}

Arg Arg::midi (std::int64_t note) noexcept
{
    Arg a;
    a.kind = ArgKind::Midi;
    a.integer = note;
    return a;
}

Arg Arg::text (std::string_view userText) noexcept
{
    Arg a;
    a.kind = ArgKind::UserText;
    a.userText = userText;
    return a;
}

Fact Fact::of (FactId id) noexcept
{
    Fact f;
    f.id = id;
    return f;
}

Fact Fact::of (FactId id, const Arg& a) noexcept
{
    Fact f = of (id);
    f.args[0] = a;
    f.argCount = 1;
    return f;
}

Fact Fact::of (FactId id, const Arg& a, const Arg& b) noexcept
{
    Fact f = of (id, a);
    f.args[1] = b;
    f.argCount = 2;
    return f;
}

Fact Fact::of (FactId id, const Arg& a, const Arg& b, const Arg& c) noexcept
{
    Fact f = of (id, a, b);
    f.args[2] = c;
    f.argCount = 3;
    return f;
}

Fact Fact::of (FactId id, const Arg& a, const Arg& b, const Arg& c, const Arg& d) noexcept
{
    Fact f = of (id, a, b, c);
    f.args[3] = d;
    f.argCount = 4;
    return f;
}

Fact Fact::of (FactId id, const Arg& a, const Arg& b, const Arg& c, const Arg& d, const Arg& e) noexcept
{
    Fact f = of (id, a, b, c, d);
    f.args[4] = e;
    f.argCount = 5;
    return f;
}

std::size_t Text::size (const Fact& fact, Lang lang) noexcept
{
    Sink s;
    render (s, fact, lang);
    return s.n;
}

std::size_t Text::write (const Fact& fact, Lang lang, std::span<char> out) noexcept
{
    const std::size_t n = size (fact, lang);
    if (out.size() < n) return 0;
    Sink s { out.data(), n, 0 };
    render (s, fact, lang);
    return s.n;
}

std::string Text::text (const Fact& fact, Lang lang)
{
    const std::size_t n = size (fact, lang);
    std::string out (n, '\0');
    Sink s { out.data(), n, 0 };
    render (s, fact, lang);
    return out;
}

std::uint64_t Text::textBytes (const Fact& fact, Lang lang) noexcept
{
    // The length and the terminator, in the 16-byte steps libc++ and MSVC's STL allocate a string's storage in
    // (libstdc++ asks for exactly the two), and 64 bytes for what MSVC's STL asks beside them: 47 to align a block of
    // 4 KiB or more, and a 16-byte proxy in a build with iterator debugging. An upper bound on every standard library
    // this repository is built with.
    return ((std::uint64_t) size (fact, lang) + 1 + 15) / 16 * 16 + 64;
}

bool Text::complete (const Fact& fact) noexcept
{
    const detail::FactShape* shape = detail::shapeOf (fact.id);
    return shape == nullptr || whole (fact, *shape);
}

Plural Text::category (const Arg& number, Lang lang) noexcept
{
    return known (lang) ? categoryOf (number, lang) : Plural::Other;
}

std::optional<double> Text::parse (std::string_view typed, Lang lang) noexcept
{
    // The one division is correctly rounded only under IEEE-754's default environment: asked first, as every call that
    // computes asks it (docs/SESSION.md).
    if (! known (lang) || Session::checkFloatingPointEnvironment() != Status::Ok) return std::nullopt;
    const View row = numbersOf (lang);
    return detail::parseTyped (typed, textOf (row.find ("decimal")), textOf (row.find ("group")));
}

std::string_view Text::code (Lang lang) noexcept
{
    return known (lang) ? detail::kLangCodes[(std::size_t) lang] : std::string_view {};
}

std::optional<Lang> Text::langOf (std::string_view code) noexcept
{
    return detail::langOfCode (code);
}

bool Text::speaks (Lang lang) noexcept
{
    if (! known (lang)) return false;
    for (const View code : detail::catalogRoot().find ("languages"))
        if (textOf (code) == detail::kLangCodes[(std::size_t) lang]) return true;
    return false;
}

std::string_view Text::key (FactId id) noexcept
{
    const detail::FactShape* shape = detail::shapeOf (id);
    return shape != nullptr ? shape->key : std::string_view {};
}

std::optional<Fact> Text::rejected (const Answer& answer, const Request& request) noexcept
{
    const std::optional<FactId> id = detail::factOf (answer.rejection);
    if (! id) return std::nullopt;
    const detail::FactShape* shape = detail::shapeOf (*id);
    if (shape == nullptr || shape->argCount == 0) return Fact::of (*id);
    // The delivery format a target takes: its bit depth and rate, read off the answer.
    if (answer.rejection == Rejection::DeliveryFormat)
        return Fact::of (*id, Arg::count (answer.targetBits), Arg::value (double (answer.targetRate), Unit::Hz, 0));
    // A field's rejection: the field, read off the request the answer is for. None found — a master's own numbers, a
    // field the tables do not name — is said as the command's refusal (fact 131, no argument): never a fact short of
    // the field its message needs. Where the answer carries the refused number (and the domain it left), the field is
    // said with them, each at the places it has (Answer::value, low, high).
    std::optional<Term> field;
    if (std::holds_alternative<command::EditTarget> (request)) field = detail::targetFieldTerm (answer.field);
    else if (const auto* device = std::get_if<command::EditDevice> (&request))
        field = detail::deviceFieldTerm ((Device) device->fields.index(), answer.field);
    else if (const auto* revert = std::get_if<command::RevertEdits> (&request))
        field = detail::deviceFieldTerm ((Device) revert->fields.index(), answer.field);
    else if (std::holds_alternative<command::Load> (request)) field = Term::FieldAudio;
    else if (std::holds_alternative<command::ImportProject> (request))
        field = answer.device ? detail::deviceFieldTerm (*answer.device, answer.field) : detail::targetFieldTerm (answer.field);
    if (field && answer.value && answer.rejection == Rejection::OutOfDomain && answer.low && answer.high)
    {
        const Unit unit = detail::fieldUnit (*field);
        const auto exact = [unit] (double x) noexcept
        {
            // The fewest places that give the number back (Grid.h's decimalOf, trailing zeros dropped); nine at most.
            std::uint8_t places = 9;
            if (const auto d = session::detail::decimalOf (x))
            {
                auto mantissa = d->mantissa;
                places = d->scale;
                while (places > 0 && mantissa % 10 == 0) { mantissa /= 10; --places; }
            }
            return Arg::value (x, unit, places);
        };
        return Fact::of (FactId::RejectedOutOfDomainValue, Arg::term (*field), exact (*answer.value), exact (*answer.low),
                         exact (*answer.high));
    }
    if (field && answer.value && answer.rejection == Rejection::NotOneOf)
        return Fact::of (FactId::RejectedNotOneOfValue, Arg::term (*field), Arg::count (std::int64_t (*answer.value)));
    return field ? Fact::of (*id, Arg::term (*field)) : Fact::of (FactId::RejectedContract);
}

std::string detail::argText (const Arg& arg, Lang lang)
{
    if (! known (lang)) return {};
    Sink count;
    renderArg (count, arg, lang);
    std::string out (count.n, '\0');
    Sink s { out.data(), out.size(), 0 };
    renderArg (s, arg, lang);
    return out;
}

} // namespace felitronics::session::text
