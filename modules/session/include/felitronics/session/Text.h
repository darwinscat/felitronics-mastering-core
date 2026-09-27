// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

//==============================================================================
// felitronics::session::text — FACTS, NOT TEXT. Nothing in the session prints. It states a FACT: an id and typed
// arguments — a number with its unit, precision, sign and bound; a count; a term (a word the catalog names); a note as a
// MIDI number; a text of the user's (a file name, a target's label), which is never translated. No fact carries a ready
// string. Text::text() renders a fact in a language, and that is the only place words are made: the same fact reads the
// same on the web and on the desktop, re-renders in another language without being computed again, and compares as
// data (docs/SESSION.md, "The text").
//
// TWO DOCUMENTS, COMPILED IN. modules/session/text/catalog.toml holds WHOLE messages — a message is never assembled from
// fragments — each with its placeholders named ({passes}), and with plural or select variants where the words depend on
// a number or a term; modules/session/text/format.toml is the ONE table of how every language writes a number: decimal
// sign, grouping, the minus, the bounds, the sign of an absent value, the units, the names of the notes. Both are
// compiled into the library as constexpr data (felitronics-toml's felitronics_toml_embed); nothing reads a file.
//
// THE BUILD HOLDS THE CATALOG. Every build of the library runs felitronics_session_text_check over both documents before
// the library is built: every language the catalog declares has every message and every term; every message's
// placeholders name its fact's arguments, and every language uses the same set of them (repeated or reordered freely);
// a plural message has exactly its language's CLDR categories, a select message exactly its term group's values; the
// formatting table has a row, and every unit, for every language of Lang. A missing key or a stray placeholder is a red
// build at `<file>:<line>:<column>`. So a declared language never lacks a message, and there is no fallback to English
// anywhere: a message the catalog does not have in a language — a language the catalog does not declare — renders as
// its id.
//
// NUMBERS ARE FORMATTED BY RULES OF ITS OWN, stated here and tested: no locale, no printf, no JavaScript semantics.
//   * Rounding: the EXACT value of the double is rounded to the decimal grid of `precision` fraction digits — to the
//     nearest multiple of 10^−precision, a value exactly halfway rounded away from zero. 0.125 → 0.13 and 2.5 → 3 are
//     exact halves; 1.005 → 1.00 is not, because the double nearest 1.005 is 1.00499999999999989…
//   * The sign follows the value, not its rounded digits: a negative value takes the minus (−0.04 at one digit is
//     "−0.0"), a positive one takes the plus under Sign::Always, and a zero — either zero — takes neither.
//   * Plural categories are CLDR's, selected on the number AS PRINTED: "1.0" is not "one" in English, "1" is. The
//     formatter and the selector are one component.
//   * A value that is not finite prints the language's absent sign ("—") alone: no sign, no bound, no unit.
//   * Parsing (Text::parse) reads what a person typed with std::from_chars, never strtod: no locale reaches it.
//
// As Session.h: this header carries no function body. The rendering is compiled with the library's own flags
// (docs/SESSION.md).
namespace felitronics::session::text
{

// The languages a shell may ask for: the twelve the site speaks. Which of them the catalog DECLARES is data
// (Text::speaks); the formatting table covers all twelve.
enum class Lang : std::uint8_t { En, De, Ru, Uk, Cs, Es, Fr, It, Pl, Pt, Ro, Tr };
inline constexpr std::size_t kLangCount = 12;

// A number's unit. Percent takes the number of percent (45 for 45 %), not a share of one. None is the number alone.
enum class Unit : std::uint8_t { None, Percent, Db, DbTp, DbFs, Lufs, Lu, Hz, KHz, Ms, S, Bpm };
inline constexpr std::size_t kUnitCount = 12;

// Which sign a number shows. Negative: the minus of a negative value. Always: the plus of a positive value too (a gain).
enum class Sign : std::uint8_t { Negative, Always };

// Whether a number is a bound: Exact prints the number, AtLeast "≥ 6 %", AtMost "≤ 6 %".
enum class Bound : std::uint8_t { Exact, AtLeast, AtMost };

// CLDR's plural categories.
enum class Plural : std::uint8_t { Zero, One, Two, Few, Many, Other };

// THE FACTS — one id per message of modules/session/text/catalog.toml, whose key Text::key() gives. The numbers are
// stable: a fact is stored and compared by its id, so an id is never reused for another message. The arguments each fact
// takes, by name and kind, are src/TextFacts.h's, and the build holds the catalog to them.
enum class FactId : std::uint16_t
{
    Value = 1,               // a reading alone: {value}
    LandingPass = 2,         // the landing's progress: {pass} of {passes}
    LandingConverged = 3,    // the landing converged in {passes} passes (plural on passes)
    PairsConsistent = 4,     // the blind test's repeats: {agreed} of {pairs} pairs (plural on pairs)
    LoudestLowNote = 5,      // the loudest note of the low end: {note}
    WideBass = 6,            // the bass is wide (side {side}) — the warning of phase 1
    RateAboveLimit = 7,      // the file's rate {rate} is above what this platform takes, {limit} (select on platform)
    // The session's own facts — its phases, its commands' refusals, its errors — take the next numbers.
};

// THE TERMS — words an argument of kind Term names: one value of a group of the catalog's [terms]. Printed as the
// catalog's word, or chosen on by a select message.
enum class Term : std::uint16_t
{
    PlatformWeb = 1,
    PlatformDesktop = 2,
};

enum class ArgKind : std::uint8_t { None, Value, Count, Term, Midi, UserText };

// One typed argument of a fact. Built by the static members, not by hand.
struct Arg
{
    ArgKind kind = ArgKind::None;
    Unit unit = Unit::None;              // Value
    std::uint8_t precision = 0;          // Value: fraction digits, 0 … 9
    Sign sign = Sign::Negative;          // Value
    Bound bound = Bound::Exact;          // Value
    Term termId {};                      // Term
    double number = 0.0;                 // Value
    std::int64_t integer = 0;            // Count; Midi: the MIDI note number, 0 … 127 (60 is C4)
    std::string_view userText;           // UserText: a VIEW — its bytes are the caller's, kept alive while it is rendered

    [[nodiscard]] static Arg value (double number, Unit unit, std::uint8_t precision, Sign sign = Sign::Negative,
                                    Bound bound = Bound::Exact) noexcept;
    [[nodiscard]] static Arg count (std::int64_t n) noexcept;
    [[nodiscard]] static Arg term (Term t) noexcept;
    [[nodiscard]] static Arg midi (std::int64_t note) noexcept;
    [[nodiscard]] static Arg text (std::string_view userText) noexcept;
};

// A fact: its id and its arguments, in the order src/TextFacts.h declares them for that id. A value: copying one
// allocates nothing. A fact whose arguments do not match its declaration renders each mismatched placeholder as
// `{name}`, visibly, rather than a guess.
struct Fact
{
    static constexpr std::size_t kMaxArgs = 6;
    FactId id = FactId::Value;
    std::array<Arg, kMaxArgs> args {};
    std::uint8_t argCount = 0;

    [[nodiscard]] static Fact of (FactId id) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b, const Arg& c) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b, const Arg& c, const Arg& d) noexcept;
};

//==============================================================================
// THE RENDERER — pure functions over the compiled-in documents: no state, no file, no locale, the same bytes on every
// row the library runs on, native and wasm. Static members, as Session's are (a public header declares no free
// function).
struct Text
{
    // THE FACT IN A LANGUAGE: its whole message, each placeholder replaced by its argument formatted by the table. A
    // message the catalog does not have in `lang` renders as its id (Text::key); nothing falls back to another language.
    // Allocates the result's bytes, and at most textBytes() of them.
    [[nodiscard]] static std::string text (const Fact& fact, Lang lang);

    // THE DEMAND OF text(), before it is made (law 11d): the bytes it will request from the heap, at most — the result's
    // length and its terminator, rounded up to the 16 bytes a standard library allocates a string's storage in.
    [[nodiscard]] static std::uint64_t textBytes (const Fact& fact, Lang lang) noexcept;

    // The same rendering without the heap. size(): its length in bytes. write(): the bytes into `out`, and how many; a
    // buffer shorter than size() gets nothing and the answer 0 (refused whole). Both allocate nothing.
    [[nodiscard]] static std::size_t size (const Fact& fact, Lang lang) noexcept;
    [[nodiscard]] static std::size_t write (const Fact& fact, Lang lang, std::span<char> out) noexcept;

    // The plural category of a number argument as `lang` prints it — the category a plural message selects on it with.
    // Other for an argument that is not a number (a Value or a Count), and for a value that prints as absent.
    [[nodiscard]] static Plural category (const Arg& number, Lang lang) noexcept;

    // WHAT A PERSON TYPED, read as a number: ASCII spaces around it ignored; an optional sign — "−" (U+2212), "-" or "+";
    // decimal digits, with at most one decimal sign: the language's own, or "." (every keyboard has one). A grouping
    // separator is refused rather than guessed ("1.234" is a thousand to a German reader, and nearly one to an English
    // one). At least one digit, at most 9 after the sign, and the digits as one integer no larger than 2^53. The
    // answer is that integer over 10^digits, one correctly rounded division: the double nearest the typed decimal, as a
    // correctly rounded strtod would give — without strtod, which follows the process's locale. "−0" is −0.0. Anything
    // else is nullopt.
    [[nodiscard]] static std::optional<double> parse (std::string_view typed, Lang lang) noexcept;

    // The language's code ("ru"), and the language of a code (nullopt for one that is not one of the twelve).
    [[nodiscard]] static std::string_view code (Lang lang) noexcept;
    [[nodiscard]] static std::optional<Lang> langOf (std::string_view code) noexcept;

    // Does the catalog declare `lang` — does every message exist in it?
    [[nodiscard]] static bool speaks (Lang lang) noexcept;

    // A fact's key in the catalog: the id a message renders as where the catalog does not have it.
    [[nodiscard]] static std::string_view key (FactId id) noexcept;
};

} // namespace felitronics::session::text
