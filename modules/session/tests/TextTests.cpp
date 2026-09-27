// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for felitronics::session::text (<felitronics/session/Text.h>) — facts, the catalog, the one
// formatting table. Run as `felitronics_session_text_tests <catalog.toml> <format.toml>`, the two source documents, which
// ctest passes. Pinned here:
//   * the embedded documents ARE the sources (canonical text, byte for byte), and the gate passes them;
//   * THE GATE'S CONTROLS, in-process: a missing message, term, category or unit pattern, a stray, lost or malformed
//     placeholder, another placeholder set in another language, a language nothing declares, a key nothing reads, a
//     selector that names no argument or one of the wrong kind, a wrong type, an empty text, a syntax error — each must be
//     reported with its fault, key path, rule, line and column;
//   * ROUNDING ON THE DECIMAL GRID: the exact value, ties away from zero — at exact halves, at doubles just off a half,
//     across a carry into a new digit, at the extremes of the double; and against an independent oracle (the binary
//     fraction multiplied out digit by digit in 64-bit integers) over twenty thousand random values;
//   * the formatting table in all twelve languages: separators, grouping and its minimum, the Unicode minus, the plus of
//     Sign::Always, the bounds, the absent value, units and the percent sign's side, the note names (de: H);
//   * CLDR PLURAL CATEGORIES on the printed number: all twelve languages against ICU 78's own answers on numbers of every
//     category, each language's set reached exactly; and through whole messages in ru and en;
//   * every message of the catalog rendered in ru and en; a language the catalog does not declare renders the id; an
//     argument that does not match its declaration renders `{name}`;
//   * Text::parse, from_chars under it: signs, separators, limits, and the correctly rounded double;
//   * MEMORY: size() and write() allocate nothing, text() allocates no more than textBytes() — and a declaration one step
//     short is caught; a short buffer gets nothing;
//   * THE SAME BYTES ON EVERY ROW: one FNV-1a hash of a corpus of renderings — every fact in every language, numbers from
//     a fixed generator in every language and unit, every note — pinned, so native rows and the wasm tier agree.

#include "DeclaredBudget.h"   // installs the allocation counter: EVERY form of `new`
#include "ConfigTestSupport.h"

#include <felitronics_test.h>
#include <felitronics/session/Text.h>
#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include "TextFacts.h"
#include "TextNumber.h"
#include "TextSchema.h"

#include <array>
#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace text = felitronics::session::text;
namespace detail = felitronics::session::text::detail;
namespace budget = felitronics::session::testing;
using felitronics::test::ok;
using text::Arg;
using text::Bound;
using text::Fact;
using text::FactId;
using text::Lang;
using text::Plural;
using text::Sign;
using text::Text;
using text::Unit;

namespace
{
std::string g_catalogText, g_formatText;   // the two source documents, read once in main()

constexpr std::array<Lang, 12> kAll = { Lang::En, Lang::De, Lang::Ru, Lang::Uk, Lang::Cs, Lang::Es,
                                        Lang::Fr, Lang::It, Lang::Pl, Lang::Pt, Lang::Ro, Lang::Tr };

std::string codeOf (Lang lang) { return std::string (Text::code (lang)); }

// Invisible characters, spelled out where a check prints a string.
std::string visible (std::string_view s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s.substr (i, 2) == "\xC2\xA0") { out += "<NBSP>"; ++i; }
        else if (s.substr (i, 3) == "\xE2\x80\xAF") { out += "<NNBSP>"; i += 2; }
        else out += s[i];
    }
    return out;
}

void same (const std::string& got, std::string_view want, const std::string& what)
{
    ok (got == want, what + ": \"" + visible (got) + "\"" + (got == want ? "" : " — want \"" + visible (want) + "\""));
}

//==============================================================================
// THE DOCUMENTS

void theEmbeddedTextIsTheSource()
{
    felitronics::test::group ("the embedded catalog and table are the source documents, and the gate passes them");
    namespace toml = felitronics::toml;
    const std::string catalog = toml::write (toml::embedded::toTable (detail::catalogRoot()));
    const std::string format = toml::write (toml::embedded::toTable (detail::formatRoot()));
    ok (! catalog.empty() && catalog == felitronics::session::config::testing::canonicalOf (g_catalogText),
        "the embedded catalog is catalog.toml, byte for byte in canonical form (" + std::to_string (catalog.size()) + " bytes)");
    ok (! format.empty() && format == felitronics::session::config::testing::canonicalOf (g_formatText),
        "the embedded table is format.toml, byte for byte in canonical form (" + std::to_string (format.size()) + " bytes)");
    const auto problems = detail::checkText (g_catalogText, g_formatText);
    ok (problems.empty(), "the gate passes the source documents (" + std::to_string (problems.size()) + " problems)");
    for (const auto& p : problems)
        std::printf ("    %s %u:%u %s %s %s\n", detail::Problem::name (p.document), (unsigned) p.line, (unsigned) p.column,
                     detail::Problem::name (p.fault), p.path.c_str(), p.code);
    ok (Text::speaks (Lang::Ru) && Text::speaks (Lang::En), "the catalog declares ru and en");
    bool others = false;
    for (const Lang l : kAll)
        if (l != Lang::Ru && l != Lang::En) others = others || Text::speaks (l);
    ok (! others, "and no other language");
    bool codes = true;
    for (const Lang l : kAll) codes = codes && Text::langOf (Text::code (l)) == l;
    ok (codes && ! Text::langOf ("xx") && ! Text::langOf ("") && Text::code ((Lang) 12).empty(),
        "every language's code reads back as the language; no other code is a language");
}

//==============================================================================
// THE GATE'S CONTROLS

struct Control
{
    detail::Document document;
    std::string_view from, to;     // the plant: the one occurrence of `from` becomes `to`
    std::string_view at;           // where the problem must point: once in the planted document
    detail::Fault fault;
    std::string_view path, code;
};

// The planted document's line and column of `at` (characters, not bytes), or 0 when `at` is not there exactly once.
std::pair<std::uint32_t, std::uint32_t> positionOf (const std::string& text, std::string_view at)
{
    const auto where = text.find (at);
    if (where == std::string::npos || text.find (at, where + 1) != std::string::npos) return { 0, 0 };
    std::uint32_t line = 1, column = 1;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < where; ++i)
        if (text[i] == '\n') { ++line; lineStart = i + 1; }
    for (std::size_t i = lineStart; i < where; ++i)
        if ((static_cast<unsigned char> (text[i]) & 0xC0u) != 0x80u) ++column;
    return { line, column };
}

void mustRefuse (const Control& c)
{
    const bool inCatalog = c.document == detail::Document::Catalog;
    const std::string& source = inCatalog ? g_catalogText : g_formatText;
    const std::string what = std::string (detail::Problem::name (c.fault)) + " " + std::string (c.path)
                           + (c.code.empty() ? "" : " — " + std::string (c.code));
    const auto first = source.find (c.from);
    if (first == std::string::npos || source.find (c.from, first + 1) != std::string::npos)
    {
        ok (false, "the control has rotted: its plant is not in the " + std::string (inCatalog ? "catalog" : "table")
                   + " exactly once (" + what + ")");
        return;
    }
    const std::string planted = source.substr (0, first) + std::string (c.to) + source.substr (first + c.from.size());
    const auto [line, column] = positionOf (planted, c.at);
    if (line == 0) { ok (false, "the control has rotted: '" + std::string (c.at) + "' is not in the planted text once"); return; }
    const auto problems = inCatalog ? detail::checkText (planted, g_formatText) : detail::checkText (g_catalogText, planted);
    bool found = false;
    for (const auto& p : problems)
        found = found || (p.document == c.document && p.fault == c.fault && p.path == c.path && std::string_view (p.code) == c.code
                          && p.line == line && p.column == column);
    ok (found, "refused: " + what + " at " + std::to_string (line) + ":" + std::to_string (column));
    if (! found)
        for (const auto& p : problems)
            std::printf ("      got %s %u:%u %s %s %s\n", detail::Problem::name (p.document), (unsigned) p.line,
                         (unsigned) p.column, detail::Problem::name (p.fault), p.path.c_str(), p.code);
}

void theGateRefusesEachMistake()
{
    felitronics::test::group ("the gate's controls: each planted mistake is reported with its rule, key path, line and column");
    using D = detail::Document;
    using F = detail::Fault;
    const Control controls[] = {
        // A message missing in a declared language — no fallback: the build is red.
        { D::Catalog, "en = \"{value}\"", "", "[messages.value]", F::Missing, "messages.value.en", "" },
        // A fact without any message.
        { D::Catalog, "[messages.loudestLowNote]\nru = \"Громчайшая нота низа: {note}\"\nen = \"Loudest bass note: {note}\"\n", "",
          "messages.value]", F::Missing, "messages.loudestLowNote", "" },
        // A message no fact has: a key nothing reads.
        { D::Catalog, "# ── messages ──\n", "# ── messages ──\n[messages.unused]\nru = \"x\"\nen = \"x\"\n", "unused]",
          F::UnknownKey, "messages.unused", "" },
        // A placeholder that names no argument of the fact.
        { D::Catalog, "en.other = \"landing: converged after {passes} passes\"", "en.other = \"landing: converged after {pass} passes\"",
          "\"landing: converged after {pass} passes\"", F::Refused, "messages.landingConverged.en.other.{pass}", "StrayPlaceholder" },
        // An argument the language never places.
        { D::Catalog, "en = \"Landing · pass {pass} of {passes}\"", "en = \"Landing · pass of {passes}\"", "\"Landing · pass of",
          F::Refused, "messages.landingPass.en.{pass}", "LostPlaceholder" },
        // A select's own argument placed in one language and not in the other: another set.
        { D::Catalog, "ru.web = \"Файл — {rate}.", "ru.web = \"{platform}: файл — {rate}.", "en.web", F::Refused,
          "messages.rateAboveLimit.en", "PlaceholderSet" },
        // Malformed braces.
        { D::Catalog, "ru = \"{value}\"", "ru = \"{value\"", "\"{value\"", F::Refused, "messages.value.ru", "BadPlaceholder" },
        { D::Catalog, "ru = \"{value}\"", "ru = \"{1}\"", "\"{1}\"", F::Refused, "messages.value.ru", "BadPlaceholder" },
        { D::Catalog, "ru = \"{value}\"", "ru = \"{value}}\"", "\"{value}}\"", F::Refused, "messages.value.ru", "BadPlaceholder" },
        // A plural message without one of its language's categories, and with one its language does not have.
        { D::Catalog, "ru.few = \"посадка: сошлось за {passes} прохода\"\n", "", "ru.one = \"посадка", F::Missing,
          "messages.landingConverged.ru.few", "" },
        { D::Catalog, "en.one = \"landing: converged after {passes} pass\"\n",
          "en.one = \"landing: converged after {passes} pass\"\nen.few = \"landing: converged after {passes} passes\"\n",
          "few = \"landing", F::UnknownKey, "messages.landingConverged.en.few", "" },
        // A language nothing declares, in a message.
        { D::Catalog, "en = \"Loudest bass note: {note}\"", "en = \"Loudest bass note: {note}\"\nde = \"Tiefster Ton: {note}\"", "de =",
          F::UnknownKey, "messages.loudestLowNote.de", "" },
        // The declared languages: one that is not one of the twelve, one twice, a new one without its messages.
        { D::Catalog, "languages = [\"ru\", \"en\"]", "languages = [\"ru\", \"en\", \"xx\"]", "\"xx\"", F::Refused, "languages",
          "NotALanguage" },
        { D::Catalog, "languages = [\"ru\", \"en\"]", "languages = [\"ru\", \"en\", \"ru\"]", "\"ru\"]", F::Refused, "languages",
          "Duplicate" },
        { D::Catalog, "languages = [\"ru\", \"en\"]", "languages = [\"ru\", \"en\", \"de\"]", "[messages.value]", F::Missing,
          "messages.value.de", "" },
        // Selectors: one that names no argument, one of the wrong kind, two at once.
        { D::Catalog, "plural = \"passes\"", "plural = \"pass\"", "\"pass\"", F::Refused, "messages.landingConverged.plural",
          "NotAnArgument" },
        { D::Catalog, "select = \"platform\"", "select = \"rate\"", "\"rate\"", F::Refused, "messages.rateAboveLimit.select",
          "SelectorKind" },
        { D::Catalog, "select = \"platform\"", "plural = \"limit\"\nselect = \"platform\"", "\"platform\"", F::Refused,
          "messages.rateAboveLimit.select", "TwoSelectors" },
        // A select message without one of its group's terms.
        { D::Catalog, "en.desktop = \"This file is {rate}. The engine takes nothing above {limit}.\"\n", "", "en.web", F::Missing,
          "messages.rateAboveLimit.en.desktop", "" },
        // Terms: one missing in a language, one with a brace, one no Term names.
        { D::Catalog, "desktop.en = \"desktop version\"\n", "", "desktop.ru", F::Missing, "terms.platform.desktop.en", "" },
        { D::Catalog, "web.en = \"web version\"", "web.en = \"web {version}\"", "\"web {version}\"", F::Refused,
          "terms.platform.web.en", "Brace" },
        { D::Catalog, "desktop.en = \"desktop version\"\n", "desktop.en = \"desktop version\"\nmobile.ru = \"x\"\nmobile.en = \"x\"\n",
          "mobile.ru", F::UnknownKey, "terms.platform.mobile", "" },
        // A wrong type, an empty text, a syntax error.
        { D::Catalog, "en = \"{value}\"", "en = 5", "5", F::WrongType, "messages.value.en", "" },
        { D::Catalog, "en = \"{value}\"", "en = \"\"", "\"\"", F::Refused, "messages.value.en", "Empty" },
        { D::Catalog, "languages = [\"ru\", \"en\"]", "languages = [\"ru\", \"en\"", "[terms.platform]", F::Syntax, "", "ExpectedArraySeparator" },
        // THE TABLE: a unit without a pattern, a pattern without its number, a row without a field, a field out of range,
        // eleven note names, a decimal sign that is the group separator, a digit as a separator, keys nothing reads.
        { D::Format, "tr = \"{n} dBTP\"\n", "", "[units.dbtp]", F::Missing, "units.dbtp.tr", "" },
        { D::Format, "en = \"{n} dB\"", "en = \"dB\"", "\"dB\"", F::Refused, "units.db.en", "Pattern" },
        { D::Format, "en = \"{n} dB\"", "en = \"{n} {n} dB\"", "\"{n} {n} dB\"", F::Refused, "units.db.en", "Pattern" },
        { D::Format, "[numbers.es]   # notes: letters\ndecimal = \",\"\n", "[numbers.es]   # notes: letters\n", "[numbers.es]",
          F::Missing, "numbers.es.decimal", "" },
        { D::Format, "[numbers.en]   # notes: letters\ndecimal = \".\"\ngroup = \",\"\nminimumGrouping = 1",
          "[numbers.en]   # notes: letters\ndecimal = \".\"\ngroup = \",\"\nminimumGrouping = 5", "5\nminus", F::OutOfRange,
          "numbers.en.minimumGrouping", "" },
        { D::Format, "notes = [\"C\", \"Cis\", \"D\", \"Dis\", \"E\", \"F\", \"Fis\", \"G\", \"Gis\", \"A\", \"Ais\", \"H\"]\n\n[numbers.ru]",
          "notes = [\"C\", \"Cis\", \"D\", \"Dis\", \"E\", \"F\", \"Fis\", \"G\", \"Gis\", \"A\", \"Ais\"]\n\n[numbers.ru]",
          "[\"C\", \"Cis\", \"D\", \"Dis\", \"E\", \"F\", \"Fis\", \"G\", \"Gis\", \"A\", \"Ais\"]", F::Refused, "numbers.de.notes",
          "NotTwelveNotes" },
        { D::Format, "decimal = \".\"\ngroup = \",\"", "decimal = \",\"\ngroup = \",\"", "\",\"\nminimumGrouping = 1",
          F::Refused, "numbers.en.group", "DecimalIsGroup" },
        { D::Format, "decimal = \".\"\ngroup = \",\"", "decimal = \"1\"\ngroup = \",\"", "\"1\"", F::Refused, "numbers.en.decimal",
          "NotASeparator" },
        { D::Format, "decimal = \".\"\ngroup = \",\"", "decimal = \".\"\ngroup = \",\"\nthousands = \",\"", "thousands", F::UnknownKey,
          "numbers.en.thousands", "" },
        { D::Format, "[units.percent]\n", "[units.parsec]\nen = \"{n} pc\"\n\n[units.percent]\n", "parsec]", F::UnknownKey,
          "units.parsec", "" },
    };
    for (const Control& c : controls) mustRefuse (c);
}

//==============================================================================
// ROUNDING ON THE DECIMAL GRID

std::string gridText (double value, unsigned precision)
{
    detail::Digits d;
    if (! detail::gridDigits (value, precision, d)) return "(none)";
    std::string s = d.negative ? "-" : "";
    s += d.integer();
    if (d.fractionDigits != 0) s += "." + std::string (d.fraction());
    return s;
}

// THE ORACLE, an independent algorithm: a double m·2^e with −60 ≤ e ≤ 10 is written out digit by digit — its integer
// part m >> −e, then each fraction digit as (f·10) >> −e with f the binary fraction, all in 64-bit integers (f < 2^60, so
// f·10 < 2^64) — to precision + 1 digits, which is exact; ties away from zero are then "digit precision + 1 is 5 or more".
std::string oracleText (double value, unsigned precision)
{
    const auto bits = std::bit_cast<std::uint64_t> (value);
    const int e = (int) ((bits >> 52) & 0x7FFu) - 1075;
    const std::uint64_t m = (bits & ((std::uint64_t (1) << 52) - 1)) | (std::uint64_t (1) << 52);
    std::string digits;
    std::size_t integerDigits = 0;
    if (e >= 0)
    {
        digits = std::to_string (m << e);
        integerDigits = digits.size();
        digits.append (precision + 1, '0');
    }
    else
    {
        const unsigned k = (unsigned) -e;
        const std::uint64_t mask = (std::uint64_t (1) << k) - 1;
        digits = std::to_string (m >> k);
        integerDigits = digits.size();
        std::uint64_t f = m & mask;
        for (unsigned i = 0; i <= precision; ++i)
        {
            f *= 10;
            digits += (char) ('0' + (f >> k));
            f &= mask;
        }
    }
    const bool up = digits.back() >= '5';
    digits.pop_back();
    if (up)
    {
        std::size_t i = digits.size();
        while (i > 0 && digits[i - 1] == '9') digits[--i] = '0';
        if (i == 0) { digits.insert (digits.begin(), '1'); ++integerDigits; }
        else ++digits[i - 1];
    }
    std::string integer = digits.substr (0, integerDigits);
    const std::size_t nonzero = integer.find_first_not_of ('0');
    integer = nonzero == std::string::npos ? "0" : integer.substr (nonzero);
    std::string s = value < 0.0 ? "-" : "";
    s += integer;
    if (precision != 0) s += "." + digits.substr (integerDigits);
    return s;
}

std::uint64_t splitmix (std::uint64_t& state)
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void roundingIsOnTheDecimalGrid()
{
    felitronics::test::group ("rounding: the exact value on the decimal grid, halves away from zero");
    struct Case { double value; unsigned precision; std::string_view want; std::string_view why; };
    const Case cases[] = {
        { 0.125, 2, "0.13", "an exact half rounds away from zero" },
        { -0.125, 2, "-0.13", "and so does a negative one" },
        { 2.5, 0, "3", "2.5 is 3 (round-half-even would say 2)" },
        { -2.5, 0, "-3", "-2.5 is -3" },
        { 0.5, 0, "1", "0.5 is 1" },
        { 1.5, 0, "2", "1.5 is 2" },
        { 3.5, 0, "4", "3.5 is 4" },
        { 0.25, 1, "0.3", "0.25 is an exact half: 0.3" },
        { 999999.5, 0, "1000000", "a half that carries into a new digit" },
        { 1.005, 2, "1.00", "1.005 is 1.00499999999999989…: below the half" },
        { 2.675, 2, "2.67", "2.675 is 2.67499999999999982…" },
        { 0.05, 1, "0.1", "0.05 is 0.05000000000000000277…: above the half" },
        { 0.15, 1, "0.1", "0.15 is 0.14999999999999999444…" },
        { 0.35, 1, "0.3", "0.35 is 0.34999999999999997779…" },
        { 0.45, 1, "0.5", "0.45 is 0.45000000000000001110…" },
        { 9.995, 2, "9.99", "9.995 is 9.99499999999999921840…" },
        { 99.95, 1, "100.0", "99.95 is 99.95000000000000284217…, and the carry makes a new digit" },
        { -0.04, 1, "-0.0", "a negative value that rounds to zero keeps its minus" },
        { -0.0, 1, "0.0", "negative zero is zero" },
        { 0.0, 3, "0.000", "zero with its fraction digits" },
        { 1234567.891, 0, "1234568", "no fraction digit" },
        { 0.1, 9, "0.100000000", "nine fraction digits" },
        { 5e-324, 9, "0.000000000", "the smallest subnormal" },
        { 1e300, 0, "1000000000000000052504760255204420248704468581108159154915854115511802457988908195786371375080447864043704443832883878176942523235360430575644792184786706982848387200926575803737830233794788090059368953234970799945081119038967640880074652742780142494579258788820056842838115669472196386865459400540160",
          "1e300, every exact digit" },
        { DBL_MAX, 0, "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368",
          "the largest double, every exact digit" },
    };
    for (const Case& c : cases) same (gridText (c.value, c.precision), c.want, std::string (c.why));
    same (gridText (std::numeric_limits<double>::quiet_NaN(), 1), "(none)", "NaN has no digits");
    same (gridText (std::numeric_limits<double>::infinity(), 1), "(none)", "an infinity has none");
    same (gridText (-std::numeric_limits<double>::infinity(), 1), "(none)", "nor has the other");
    same (gridText (1.0, 10), "(none)", "a precision above nine is refused");
    detail::Digits big;
    ok (detail::gridDigits (DBL_MAX, 9, big) && big.integer().size() == 309 && big.fraction() == "000000000",
        "the largest double at nine fraction digits: 309 integer digits and nine zeros");

    // THE ORACLE: random doubles with −60 ≤ e ≤ 10 (about 0.004 … 1.8e19), random precision and sign, and exact halves
    // built on purpose (an integer and a half, at every precision the half is exact at).
    std::uint64_t state = 20260927;
    int agreed = 0, tried = 0;
    std::string firstMiss;
    for (int n = 0; n < 20000; ++n)
    {
        const std::uint64_t r = splitmix (state);
        const int e = -60 + (int) (r % 71);
        const std::uint64_t mantissa = splitmix (state) & ((std::uint64_t (1) << 52) - 1);
        const std::uint64_t sign = (r >> 40) & 1u;
        const auto value = std::bit_cast<double> ((sign << 63) | ((std::uint64_t) (e + 1075) << 52) | mantissa);
        const unsigned precision = (unsigned) ((r >> 20) % 10);
        ++tried;
        const std::string got = gridText (value, precision), want = oracleText (value, precision);
        if (got == want) ++agreed;
        else if (firstMiss.empty()) firstMiss = got + " vs " + want;
    }
    for (int n = 0; n < 2000; ++n)
    {
        // k + 1/2 · 10^−p with k random: an exact binary fraction only when 10^−p · 1/2 is — p = 0 (k.5).
        const std::uint64_t r = splitmix (state);
        const double value = (double) (r % 1000000) + 0.5;
        ++tried;
        const std::string got = gridText ((r & 1u) != 0 ? -value : value, 0);
        const std::string want = oracleText ((r & 1u) != 0 ? -value : value, 0);
        if (got == want) ++agreed;
        else if (firstMiss.empty()) firstMiss = got + " vs " + want;
    }
    ok (agreed == tried, "the oracle agrees on " + std::to_string (agreed) + " of " + std::to_string (tried) + " values"
                         + (firstMiss.empty() ? "" : " (first miss: " + firstMiss + ")"));
    same (oracleText (0.125, 2), "0.13", "PRECONDITION: the oracle itself rounds an exact half away from zero");
    same (oracleText (2.675, 2), "2.67", "PRECONDITION: and reads the double below 2.675 as below the half");
}

//==============================================================================
// THE TABLE, IN EVERY LANGUAGE

std::string arg (const Arg& a, Lang lang) { return detail::argText (a, lang); }

void theTableFormatsEveryLanguage()
{
    felitronics::test::group ("the formatting table: separators, grouping, signs, bounds, units, notes — all twelve languages");
    const std::string NB = "\xC2\xA0", NN = "\xE2\x80\xAF", MINUS = "\xE2\x88\x92";
    struct Row { Lang lang; std::string big, small, percent, negPercent; };
    const Row rows[] = {
        { Lang::En, MINUS + "1,234,567.5", "1,234", "45%", MINUS + "45%" },
        { Lang::De, MINUS + "1.234.567,5", "1.234", "45" + NB + "%", MINUS + "45" + NB + "%" },
        { Lang::Ru, MINUS + "1" + NB + "234" + NB + "567,5", "1" + NB + "234", "45" + NB + "%", MINUS + "45" + NB + "%" },
        { Lang::Uk, MINUS + "1" + NB + "234" + NB + "567,5", "1" + NB + "234", "45%", MINUS + "45%" },
        { Lang::Cs, MINUS + "1" + NB + "234" + NB + "567,5", "1" + NB + "234", "45" + NB + "%", MINUS + "45" + NB + "%" },
        { Lang::Es, MINUS + "1.234.567,5", "1234", "45" + NB + "%", MINUS + "45" + NB + "%" },
        { Lang::Fr, MINUS + "1" + NN + "234" + NN + "567,5", "1" + NN + "234", "45" + NN + "%", MINUS + "45" + NN + "%" },
        { Lang::It, MINUS + "1.234.567,5", "1234", "45%", MINUS + "45%" },
        { Lang::Pl, MINUS + "1" + NB + "234" + NB + "567,5", "1234", "45%", MINUS + "45%" },
        { Lang::Pt, MINUS + "1.234.567,5", "1.234", "45%", MINUS + "45%" },
        { Lang::Ro, MINUS + "1.234.567,5", "1.234", "45" + NB + "%", MINUS + "45" + NB + "%" },
        { Lang::Tr, MINUS + "1.234.567,5", "1.234", "%45", MINUS + "%45" },
    };
    for (const Row& r : rows)
    {
        const std::string c = codeOf (r.lang);
        same (arg (Arg::value (-1234567.5, Unit::None, 1), r.lang), r.big, c + ": −1234567.5, grouped, its decimal sign, the minus");
        same (arg (Arg::value (1234.0, Unit::None, 0), r.lang), r.small, c + ": 1234 — grouped or not by the minimum");
        same (arg (Arg::value (45.0, Unit::Percent, 0), r.lang), r.percent, c + ": 45 %");
        same (arg (Arg::value (-45.0, Unit::Percent, 0), r.lang), r.negPercent, c + ": −45 %, the sign outside the pattern");
        same (arg (Arg::value (std::numeric_limits<double>::quiet_NaN(), Unit::Lufs, 1, Sign::Always, Bound::AtLeast), r.lang),
              "\xE2\x80\x94", c + ": a value that is not a number is the dash alone");
        same (arg (Arg::count (-12345), r.lang), MINUS + arg (Arg::count (12345), r.lang), c + ": a negative count takes the minus");
    }
    same (arg (Arg::value (12345.0, Unit::None, 0), Lang::Es), "12.345", "es: five digits are grouped");
    same (arg (Arg::value (12345.0, Unit::None, 0), Lang::Pl), "12" + NB + "345", "pl: so are they in Polish");
    same (arg (Arg::value (123.0, Unit::None, 0), Lang::En), "123", "three digits are never grouped");

    // Signs and bounds.
    same (arg (Arg::value (3.24, Unit::Db, 1, Sign::Always), Lang::En), "+3.2 dB", "en: Sign::Always shows the plus");
    same (arg (Arg::value (3.24, Unit::Db, 1, Sign::Always), Lang::Ru), "+3,2 дБ", "ru: the same gain");
    same (arg (Arg::value (0.0, Unit::Db, 1, Sign::Always), Lang::En), "0.0 dB", "zero takes no sign, even under Always");
    same (arg (Arg::value (0.02, Unit::Db, 1, Sign::Always), Lang::En), "+0.0 dB", "a positive value that rounds to zero keeps its plus");
    same (arg (Arg::value (-0.04, Unit::DbTp, 1), Lang::En), MINUS + "0.0 dBTP", "a negative one keeps its minus");
    same (arg (Arg::value (-0.0, Unit::DbTp, 1), Lang::En), "0.0 dBTP", "negative zero is zero");
    same (arg (Arg::value (6.0, Unit::Percent, 0, Sign::Negative, Bound::AtLeast), Lang::Ru), "≥" + NB + "6" + NB + "%", "ru: at least 6 %");
    same (arg (Arg::value (-14.0, Unit::Lufs, 0, Sign::Negative, Bound::AtMost), Lang::En), "≤" + NB + MINUS + "14 LUFS",
          "en: at most −14 LUFS — the bound before the sign");
    same (arg (Arg::value (6.0, Unit::Percent, 0, Sign::Negative, Bound::AtLeast), Lang::Fr), "≥" + NN + "6" + NN + "%", "fr: its narrow spaces");
    same (arg (Arg::value (5.0, Unit::Percent, 0, Sign::Negative, Bound::AtLeast), Lang::Tr), "≥" + NB + "%5", "tr: the bound before the percent sign");

    // Units, in en and ru (and the Latin ones everywhere).
    struct Unit1 { Unit unit; double value; std::uint8_t precision; std::string en, ru; };
    const Unit1 units[] = {
        { Unit::Db, -6.02, 2, MINUS + "6.02 dB", MINUS + "6,02 дБ" },
        { Unit::DbTp, -1.0, 1, MINUS + "1.0 dBTP", MINUS + "1,0 dBTP" },
        { Unit::DbFs, -60.0, 0, MINUS + "60 dBFS", MINUS + "60 dBFS" },
        { Unit::Lufs, -14.03, 1, MINUS + "14.0 LUFS", MINUS + "14,0 LUFS" },
        { Unit::Lu, 7.25, 1, "7.3 LU", "7,3 LU" },
        { Unit::Hz, 44100.0, 0, "44,100 Hz", "44" + NB + "100 Гц" },
        { Unit::KHz, 44.1, 1, "44.1 kHz", "44,1 кГц" },
        { Unit::Ms, 5.0, 0, "5 ms", "5 мс" },
        { Unit::S, 2.5, 1, "2.5 s", "2,5 с" },
        { Unit::Bpm, 128.0, 0, "128 BPM", "128 BPM" },
    };
    for (const Unit1& u : units)
    {
        same (arg (Arg::value (u.value, u.unit, u.precision), Lang::En), u.en, "en unit");
        same (arg (Arg::value (u.value, u.unit, u.precision), Lang::Ru), u.ru, "ru unit");
    }
    same (arg (Arg::value (5.0, Unit::S, 0), Lang::Tr), "5 sn", "tr: seconds are sn");
    same (arg (Arg::value (5.0, Unit::Ms, 1), Lang::Fr), "5,0" + NN + "ms", "fr: a unit after the narrow no-break space");

    // Notes: scientific pitch notation, each language's system.
    struct Note { std::int64_t midi; Lang lang; std::string_view want; };
    const Note notes[] = {
        { 60, Lang::En, "C4" }, { 61, Lang::En, "C♯4" }, { 71, Lang::En, "B4" }, { 28, Lang::En, "E1" },
        { 28, Lang::Ru, "E1" }, { 70, Lang::Ru, "A♯4" }, { 71, Lang::Fr, "B4" },
        { 71, Lang::De, "H4" }, { 70, Lang::De, "Ais4" }, { 66, Lang::De, "Fis4" }, { 71, Lang::Cs, "H4" }, { 71, Lang::Pl, "H4" },
        { 71, Lang::It, "Si4" }, { 61, Lang::It, "Do♯4" }, { 67, Lang::Ro, "Sol4" },
        { 21, Lang::En, "A0" }, { 127, Lang::En, "G9" },
    };
    for (const Note& n : notes)
        same (arg (Arg::midi (n.midi), n.lang), n.want, codeOf (n.lang) + ": MIDI " + std::to_string (n.midi));
    same (arg (Arg::midi (0), Lang::En), "C" + MINUS + "1", "MIDI 0 is C−1, with the minus");
    same (arg (Arg::midi (128), Lang::En), "\xE2\x80\x94", "MIDI 128 is no note: the dash");
    same (arg (Arg::midi (-1), Lang::De), "\xE2\x80\x94", "and neither is −1");

    // A term, and a user's text.
    same (arg (Arg::term (text::Term::PlatformDesktop), Lang::Ru), "десктоп-версия", "ru: a term's word");
    same (arg (Arg::term (text::Term::PlatformWeb), Lang::En), "web version", "en: a term's word");
    same (arg (Arg::term (text::Term::PlatformWeb), Lang::De), "platform.web", "de, undeclared: the term's id");
    same (arg (Arg::text ("mix {final}.wav"), Lang::Ru), "mix {final}.wav", "a user's text is never read: its braces stay");
}

//==============================================================================
// PLURALS

void pluralsAreCldrsOnThePrintedNumber()
{
    felitronics::test::group ("plural categories: CLDR's, on the number as printed — all twelve against ICU 78");
    // The samples as printed (with a point for the fraction), and ICU 78's category of each (Intl.PluralRules with the
    // fraction digits the sample shows): 1 one, f few, m many, o other.
    const std::string_view samples[] = { "0", "1", "2", "3", "4", "5", "7", "10", "11", "12", "14", "15", "19", "20", "21",
        "22", "25", "100", "101", "102", "111", "112", "119", "1000", "1001", "1000000", "2000000", "1000001", "0.0", "0.5",
        "1.0", "1.5", "2.0", "2.5", "5.0", "11.0", "21.0", "1.00", "0.01" };
    struct Expect { Lang lang; std::string_view icu; };
    const Expect expect[] = {
        { Lang::En, "o1ooooooooooooooooooooooooooooooooooooo" },
        { Lang::De, "o1ooooooooooooooooooooooooooooooooooooo" },
        { Lang::Ru, "m1fffmmmmmmmmm1fmm1fmmmm1mm1ooooooooooo" },
        { Lang::Uk, "m1fffmmmmmmmmm1fmm1fmmmm1mm1ooooooooooo" },
        { Lang::Cs, "o1fffooooooooooooooooooooooommmmmmmmmmm" },
        { Lang::Es, "o1ooooooooooooooooooooooommooo1oooooo1o" },
        { Lang::Fr, "11ooooooooooooooooooooooommo1111ooooo11" },
        { Lang::It, "o1ooooooooooooooooooooooommoooooooooooo" },
        { Lang::Pl, "m1fffmmmmmmmmmmfmmmfmmmmmmmmooooooooooo" },
        { Lang::Pt, "11ooooooooooooooooooooooommo1111ooooo11" },
        { Lang::Ro, "f1fffffffffffooooofffffofooffffffffffff" },
        { Lang::Tr, "o1oooooooooooooooooooooooooooo1oooooo1o" },
    };
    const auto letter = [] (Plural p) { return p == Plural::One ? '1' : p == Plural::Few ? 'f' : p == Plural::Many ? 'm'
                                             : p == Plural::Other ? 'o' : '?'; };
    for (const Expect& e : expect)
    {
        std::string got;
        std::uint8_t reached = 0;
        for (const std::string_view s : samples)
        {
            const auto point = s.find ('.');
            const Plural p = detail::pluralOf (e.lang, s.substr (0, point),
                                               point == std::string_view::npos ? std::string_view {} : s.substr (point + 1));
            got += letter (p);
            reached = (std::uint8_t) (reached | detail::bit (p));
        }
        ok (got == e.icu, codeOf (e.lang) + ": " + got + (got == e.icu ? "" : " — ICU says " + std::string (e.icu)));
        ok (reached == detail::pluralSet (e.lang), codeOf (e.lang) + ": the samples reach exactly the language's categories");
    }

    // Through Text::category: the formatter and the selector are one component.
    const auto cat = [] (const Arg& a, Lang l) { return Text::category (a, l); };
    ok (cat (Arg::count (1), Lang::En) == Plural::One, "en: 1 is one");
    ok (cat (Arg::value (1.0, Unit::None, 1), Lang::En) == Plural::Other, "en: 1.0 is other — the printed number decides");
    ok (cat (Arg::value (1.04, Unit::None, 0), Lang::En) == Plural::One, "en: 1.04 printed as 1 is one");
    ok (cat (Arg::value (0.96, Unit::None, 0), Lang::En) == Plural::One, "en: 0.96 printed as 1 is one");
    ok (cat (Arg::value (1.5, Unit::None, 1), Lang::Ru) == Plural::Other, "ru: 1,5 is other");
    ok (cat (Arg::count (21), Lang::Ru) == Plural::One && cat (Arg::count (11), Lang::Ru) == Plural::Many
            && cat (Arg::count (22), Lang::Ru) == Plural::Few && cat (Arg::count (0), Lang::Ru) == Plural::Many,
        "ru: 21 one, 11 many, 22 few, 0 many");
    ok (cat (Arg::count (-2), Lang::Ru) == Plural::Few, "a negative number selects as its magnitude");
    ok (cat (Arg::value (std::numeric_limits<double>::quiet_NaN(), Unit::None, 0), Lang::Ru) == Plural::Other,
        "an absent value is other");
    ok (cat (Arg::midi (61), Lang::Ru) == Plural::Other, "and so is an argument that is not a number");
}

//==============================================================================
// THE CATALOG'S MESSAGES

std::string render (const Fact& f, Lang lang) { return Text::text (f, lang); }

void everyMessageRenders()
{
    felitronics::test::group ("every message of the catalog, in ru and en; no fallback, no guess");
    const std::string NB = "\xC2\xA0", MINUS = "\xE2\x88\x92";
    same (render (Fact::of (FactId::Value, Arg::value (-14.03, Unit::Lufs, 1)), Lang::Ru), MINUS + "14,0 LUFS", "ru value");
    same (render (Fact::of (FactId::Value, Arg::value (-14.03, Unit::Lufs, 1)), Lang::En), MINUS + "14.0 LUFS", "en value");

    same (render (Fact::of (FactId::LandingPass, Arg::count (3), Arg::count (12)), Lang::Ru), "Посадка · проход 3 из 12", "ru landingPass");
    same (render (Fact::of (FactId::LandingPass, Arg::count (3), Arg::count (12)), Lang::En), "Landing · pass 3 of 12", "en landingPass");

    struct Passes { std::int64_t n; std::string_view ru, en; };
    const Passes passes[] = {
        { 1, "посадка: сошлось за 1 проход", "landing: converged after 1 pass" },
        { 2, "посадка: сошлось за 2 прохода", "landing: converged after 2 passes" },
        { 5, "посадка: сошлось за 5 проходов", "landing: converged after 5 passes" },
        { 11, "посадка: сошлось за 11 проходов", "landing: converged after 11 passes" },
        { 12, "посадка: сошлось за 12 проходов", "landing: converged after 12 passes" },
        { 21, "посадка: сошлось за 21 проход", "landing: converged after 21 passes" },
        { 24, "посадка: сошлось за 24 прохода", "landing: converged after 24 passes" },
        { 32, "посадка: сошлось за 32 прохода", "landing: converged after 32 passes" },
        { 0, "посадка: сошлось за 0 проходов", "landing: converged after 0 passes" },
    };
    for (const Passes& p : passes)
    {
        same (render (Fact::of (FactId::LandingConverged, Arg::count (p.n)), Lang::Ru), p.ru, "ru plural");
        same (render (Fact::of (FactId::LandingConverged, Arg::count (p.n)), Lang::En), p.en, "en plural");
    }

    struct Pairs { std::int64_t agreed, pairs; std::string_view ru, en; };
    const Pairs pairs[] = {
        { 5, 6, "согласованность повторов: 5 из 6 пар", "repeat consistency: 5 of 6 pairs" },
        { 1, 1, "согласованность повторов: 1 из 1 пары", "repeat consistency: 1 of 1 pair" },
        { 18, 21, "согласованность повторов: 18 из 21 пары", "repeat consistency: 18 of 21 pairs" },
        { 1, 3, "согласованность повторов: 1 из 3 пар", "repeat consistency: 1 of 3 pairs" },
    };
    for (const Pairs& p : pairs)
    {
        same (render (Fact::of (FactId::PairsConsistent, Arg::count (p.agreed), Arg::count (p.pairs)), Lang::Ru), p.ru,
              "ru: the noun agrees with the second number");
        same (render (Fact::of (FactId::PairsConsistent, Arg::count (p.agreed), Arg::count (p.pairs)), Lang::En), p.en, "en");
    }

    same (render (Fact::of (FactId::LoudestLowNote, Arg::midi (28)), Lang::Ru), "Громчайшая нота низа: E1", "ru note");
    same (render (Fact::of (FactId::LoudestLowNote, Arg::midi (61)), Lang::En), "Loudest bass note: C♯4", "en note");

    const Fact wide = Fact::of (FactId::WideBass, Arg::value (18.2, Unit::Percent, 0));
    same (render (wide, Lang::Ru),
          "бас широкий (бока 18" + NB + "%) — моно-бас соберёт его, на наушниках станет уже; если ширина нужна — выключите моно-бас в Настройках",
          "ru wideBass, the owner's words");
    same (render (wide, Lang::En),
          "the bass is wide (side 18%) — mono-bass will gather it, and on headphones it will sound narrower; if you need the width, switch mono-bass off in Settings",
          "en wideBass");

    const Fact web = Fact::of (FactId::RateAboveLimit, Arg::value (176.4, Unit::KHz, 1), Arg::value (96.0, Unit::KHz, 0),
                               Arg::term (text::Term::PlatformWeb));
    const Fact desktop = Fact::of (FactId::RateAboveLimit, Arg::value (384.0, Unit::KHz, 0), Arg::value (192.0, Unit::KHz, 0),
                                   Arg::term (text::Term::PlatformDesktop));
    same (render (web, Lang::Ru), "Файл — 176,4 кГц. Всё, что выше 96 кГц, — для десктоп-версии.", "ru select: web");
    same (render (web, Lang::En), "This file is 176.4 kHz. Above 96 kHz is for the desktop version.", "en select: web");
    same (render (desktop, Lang::Ru), "Файл — 384 кГц. Всё, что выше 192 кГц, движок не принимает.", "ru select: desktop");
    same (render (desktop, Lang::En), "This file is 384 kHz. The engine takes nothing above 192 kHz.", "en select: desktop");

    // No fallback: a language the catalog does not declare renders the message's id, in every language but ru and en.
    bool ids = true;
    for (const Lang l : kAll)
        if (l != Lang::Ru && l != Lang::En)
            for (const auto& shape : detail::kFacts)
                ids = ids && render (Fact::of (shape.id), l) == shape.key;
    ok (ids, "every message in every undeclared language is its id — never English");
    same (render (wide, Lang::De), "wideBass", "de: wideBass is its id");
    same (render (wide, (Lang) 40), "wideBass", "a language that is not one of the twelve: the id");
    same (render (Fact::of ((FactId) 999), Lang::En), "#999", "an id this library does not have: its number");
    ok (Text::key (FactId::LandingConverged) == "landingConverged" && Text::key ((FactId) 0).empty(), "Text::key");

    // No guess: an argument that does not match its declaration is shown as its placeholder.
    same (render (Fact::of (FactId::LandingPass, Arg::count (3)), Lang::Ru), "Посадка · проход 3 из {passes}", "a missing argument");
    same (render (Fact::of (FactId::Value, Arg::count (5)), Lang::En), "{value}", "an argument of another kind");
    same (render (Fact::of (FactId::Value, Arg::value (1.0, Unit::Db, 12)), Lang::En), "{value}", "a precision above nine");
    same (render (Fact::of (FactId::Value, Arg::value (1.0, (Unit) 77, 1)), Lang::En), "{value}", "a unit that does not exist");
    same (render (Fact::of (FactId::LandingConverged, Arg::value (2.0, Unit::None, 0)), Lang::En), "landingConverged",
          "a plural whose number is of another kind cannot choose: the id");
    same (render (Fact::of (FactId::RateAboveLimit, Arg::value (1.0, Unit::KHz, 0), Arg::value (1.0, Unit::KHz, 0),
                            Arg::term ((text::Term) 42)), Lang::En), "rateAboveLimit", "a select on a term that does not exist: the id");
}

//==============================================================================
// PARSING

bool bitsEqual (std::optional<double> got, double want)
{
    return got.has_value() && std::bit_cast<std::uint64_t> (*got) == std::bit_cast<std::uint64_t> (want);
}

void typedNumbersAreParsed()
{
    felitronics::test::group ("Text::parse: what a person typed, by from_chars, correctly rounded, no locale");
    ok (bitsEqual (Text::parse ("−14,5", Lang::Ru), -14.5), "ru: −14,5 (the Unicode minus, the comma)");
    ok (bitsEqual (Text::parse ("-14.5", Lang::Ru), -14.5), "ru: -14.5 (the hyphen, the point every keyboard has)");
    ok (bitsEqual (Text::parse ("+3", Lang::En), 3.0), "en: +3");
    ok (bitsEqual (Text::parse ("  7  ", Lang::En), 7.0), "spaces around are ignored");
    ok (bitsEqual (Text::parse ("0012,50", Lang::De), 12.5), "leading zeros change nothing");
    ok (bitsEqual (Text::parse (".5", Lang::En), 0.5) && bitsEqual (Text::parse ("5.", Lang::En), 5.0), ".5 and 5.");
    ok (bitsEqual (Text::parse ("2.675", Lang::En), 2.675) && bitsEqual (Text::parse ("1,005", Lang::Fr), 1.005)
            && bitsEqual (Text::parse ("0.1", Lang::En), 0.1) && bitsEqual (Text::parse ("123456.789", Lang::En), 123456.789),
        "the double nearest the typed decimal — the compiler's own literal");
    ok (bitsEqual (Text::parse ("0,123456789", Lang::Ru), 0.123456789), "nine fraction digits");
    ok (bitsEqual (Text::parse ("9007199254740992", Lang::En), 9007199254740992.0), "2^53 as digits");
    const auto negativeZero = Text::parse ("−0", Lang::En);
    ok (negativeZero && *negativeZero == 0.0 && std::bit_cast<std::uint64_t> (*negativeZero) == (std::uint64_t (1) << 63), "−0 is −0.0");
    const std::string_view refused[] = { "", " ", "−", "+", ".", "1e5", "0x10", "1,2,3", "1.2.3", "1 234", "12a", "--1",
        "0,1234567890", "9007199254740993", "123456789012345678901234567890", "\xC2\xA0" "7", "inf", "nan", "1,5,", "٣" };
    for (const std::string_view s : refused)
        ok (! Text::parse (s, Lang::Ru).has_value(), "refused: '" + std::string (s) + "'");
    ok (! Text::parse ("1,5", Lang::En).has_value(), "en: a comma is not a decimal sign (it is the grouping separator)");
    ok (! Text::parse ("1,234", Lang::En).has_value(), "en: and a grouped number is refused rather than guessed");
    ok (bitsEqual (Text::parse ("1,5", Lang::Tr), 1.5) && bitsEqual (Text::parse ("1,5", Lang::Pl), 1.5), "tr, pl: the comma");
    ok (! Text::parse ("1", (Lang) 30).has_value(), "a language that is not one of the twelve reads nothing");

    // What the renderer prints, read back: every value of a sweep on its own grid.
    int roundTrips = 0, tried = 0;
    for (int i = -20000; i <= 20000; i += 7)
        for (const Lang l : { Lang::En, Lang::Ru, Lang::De })
        {
            const double v = (double) i / 100.0;
            const std::string printed = arg (Arg::value (v, Unit::None, 2), l);
            std::string plain;                                                  // the minus back to a hyphen for the check
            if (printed.starts_with ("\xE2\x88\x92")) plain = "-" + printed.substr (3);
            else plain = printed;
            ++tried;
            if (bitsEqual (Text::parse (plain, l), v) && bitsEqual (Text::parse (printed, l), v)) ++roundTrips;
        }
    ok (roundTrips == tried, "what the table prints at two digits reads back as the same double (" + std::to_string (roundTrips)
                             + " of " + std::to_string (tried) + ")");
}

//==============================================================================
// MEMORY

void theDemandCoversWhatTextAsksFor()
{
    felitronics::test::group ("memory: size() and write() allocate nothing; text() no more than textBytes()");
    std::vector<Fact> facts = {
        Fact::of (FactId::Value, Arg::value (-14.03, Unit::Lufs, 1)),
        Fact::of (FactId::LandingConverged, Arg::count (21)),
        Fact::of (FactId::WideBass, Arg::value (18.0, Unit::Percent, 0)),
        Fact::of (FactId::RateAboveLimit, Arg::value (176.4, Unit::KHz, 1), Arg::value (96.0, Unit::KHz, 0),
                  Arg::term (text::Term::PlatformWeb)),
        Fact::of (FactId::Value, Arg::value (DBL_MAX, Unit::None, 9)),
    };
    bool quiet = true, covered = true, seen = false, caught = true;
    std::string worst;
    for (const Fact& f : facts)
        for (const Lang l : kAll)
        {
            std::size_t n = 0;
            const budget::Spent sized = budget::spend ([&] { n = Text::size (f, l); });
            std::array<char, 1024> buffer {};
            std::size_t written = 0;
            const budget::Spent wrote = budget::spend ([&] { written = Text::write (f, l, buffer); });
            quiet = quiet && sized.requests == 0 && wrote.requests == 0 && written == n;
            const std::uint64_t declared = Text::textBytes (f, l);
            std::string out;
            const budget::Spent spent = budget::spend ([&] { out = Text::text (f, l); });
            covered = covered && budget::covers (declared, spent) && out.size() == n && std::string_view (buffer.data(), n) == out;
            if (! budget::covers (declared, spent)) worst = budget::describe (declared, spent);
            if (spent.bytes > 0)
            {
                seen = true;
                caught = caught && ! budget::covers (declared - 16, spent);   // a declaration one step short is caught
            }
        }
    ok (quiet, "size() and write() asked the heap for nothing, and write() wrote size() bytes");
    ok (covered, "declared >= requested for every fact in every language, and text() is what write() wrote" + (worst.empty() ? "" : " — " + worst));
    ok (seen, "PRECONDITION: the counter saw text() ask the heap (a message longer than a string's own buffer)");
    ok (caught, "and a declaration 16 bytes short would have been caught: the demand is tight to the allocator's step");

    std::array<char, 8> small {};
    small.fill ('x');
    const std::size_t n = Text::write (facts[2], Lang::Ru, small);
    ok (n == 0 && std::string_view (small.data(), small.size()) == "xxxxxxxx", "a buffer shorter than size() gets nothing: refused whole");
}

//==============================================================================
// THE SAME BYTES EVERYWHERE

void theCorpusIsTheSameBytesOnEveryRow()
{
    felitronics::test::group ("one corpus of renderings, one hash: the same bytes on every row, native and wasm");
    std::uint64_t h = 1469598103934665603ull;
    std::uint64_t bytes = 0;
    const auto eat = [&] (std::string_view s)
    {
        for (const char c : s) { h ^= (unsigned char) c; h *= 1099511628211ull; }
        h ^= 0xFFu; h *= 1099511628211ull;            // a separator no rendering contains
        bytes += s.size();
    };
    const Unit units[] = { Unit::None, Unit::Percent, Unit::Db, Unit::DbTp, Unit::DbFs, Unit::Lufs, Unit::Lu, Unit::Hz,
                           Unit::KHz, Unit::Ms, Unit::S, Unit::Bpm };
    std::uint64_t state = 1;
    for (const Lang l : kAll)
    {
        for (const auto& shape : detail::kFacts)
        {
            Fact f = Fact::of (shape.id);
            for (std::size_t i = 0; i < shape.argCount; ++i)
            {
                const auto kind = shape.args[i].kind;
                f.args[i] = kind == text::ArgKind::Value ? Arg::value (-3.25, Unit::Db, 1, Sign::Always)
                          : kind == text::ArgKind::Count ? Arg::count (21)
                          : kind == text::ArgKind::Midi ? Arg::midi (40)
                          : kind == text::ArgKind::Term ? Arg::term (text::Term::PlatformDesktop)
                          : Arg::text ("take 3.wav");
            }
            f.argCount = (std::uint8_t) shape.argCount;
            eat (Text::text (f, l));
        }
        for (int n = 0; n < 1500; ++n)
        {
            const std::uint64_t r = splitmix (state);
            // Magnitudes from 1e-6 to 1e9, every sign, precision and bound; now and then a value that is not a number.
            const double magnitude = std::bit_cast<double> ((std::uint64_t) (1003 + r % 50) << 52 | (splitmix (state) >> 12));
            const double v = n % 97 == 0 ? std::numeric_limits<double>::quiet_NaN() : (r >> 63) != 0 ? -magnitude : magnitude;
            eat (arg (Arg::value (v, units[(r >> 8) % 12], (std::uint8_t) ((r >> 16) % 10), (r >> 24) % 2 != 0 ? Sign::Always : Sign::Negative,
                                  (Bound) ((r >> 32) % 3)), l));
            eat (arg (Arg::count ((std::int64_t) (splitmix (state) % 20000001) - 10000000), l));
        }
        for (std::int64_t m = -1; m <= 128; ++m) eat (arg (Arg::midi (m), l));
        eat (arg (Arg::term (text::Term::PlatformWeb), l));
    }
    constexpr std::uint64_t kPinned = 0x3032c37f22b2a86bull;
    char hex[32];
    std::snprintf (hex, sizeof hex, "%016llx", (unsigned long long) h);
    ok (h == kPinned, "the corpus hashes to " + std::string (hex) + " over " + std::to_string (bytes) + " bytes — pinned");
}
} // namespace

int main (int argc, char** argv)
{
    std::printf ("felitronics session::text tests\n");
    if (argc != 3 || ! felitronics::session::config::testing::readFile (argv[1], g_catalogText)
        || ! felitronics::session::config::testing::readFile (argv[2], g_formatText))
    {
        std::printf ("usage: %s <catalog.toml> <format.toml>\n", argv[0]);
        return 2;
    }
    theEmbeddedTextIsTheSource();
    theGateRefusesEachMistake();
    roundingIsOnTheDecimalGrid();
    theTableFormatsEveryLanguage();
    pluralsAreCldrsOnThePrintedNumber();
    everyMessageRenders();
    typedNumbersAreParsed();
    theDemandCoversWhatTextAsksFor();
    theCorpusIsTheSameBytesOnEveryRow();
    return felitronics::test::report();
}
