// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for felitronics::session::config (Config.h) — the config compiled into the library, its schema and
// its version. Run as `felitronics_session_config_tests <targets.toml> <engine.toml>`, the two source documents, which
// ctest passes. Pinned:
//   * the embedded config binds with no problem, and it IS the source documents: the library's canonical text of each
//     equals felitronics-toml's canonical text of the file — so a stale embedding cannot pass;
//   * the decisions the documents carry, number by number, where a number is a decision rather than a measurement;
//   * THE SCHEMA'S CONTROLS, each of which must go red: a planted unknown key, wrong type, value out of range (its own
//     domain, an array item, a range another key states), refusal across keys, missing key and syntax error, each
//     reported with its document, key path, line and column — and the source without the plant binds clean, so the plant
//     is the only difference;
//   * THE VERSION: the library's (a walk over its embedded data) is the source files' (the same walk over their parsed
//     text); every number, flag, string and key of both documents, changed one at a time, moves it — through the text
//     path and through the embedded-data path — and to a different value each time; comments and spacing do not.

#include <felitronics_test.h>
#include <felitronics/session/Config.h>
#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include "ConfigVersion.h"      // the walk config::version() is (modules/session/src)
#include "testcopy/engine.h"    // this suite's own embedding of the two documents, by the same tool from the same files
#include "testcopy/targets.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace config = felitronics::session::config;
namespace toml = felitronics::toml;
using felitronics::test::ok;

namespace
{
std::string g_targetsText, g_engineText;   // the two source documents, read once in main()

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

// Exact equality of two doubles without -Wfloat-equal's objection: every number here is a decimal of the document, read
// through felitronics-toml's correctly rounded conversion, so it is the literal's own double.
bool same (double a, double b) { return ! (a < b) && ! (b < a); }

std::string canonicalOf (const std::string& text)
{
    const auto parsed = toml::parse (text);
    const auto* t = std::get_if<toml::Table> (&parsed);
    return t != nullptr ? toml::write (*t) : std::string{};
}

void theEmbeddedConfigIsTheSource()
{
    felitronics::test::group ("the embedded config binds with no problem, and it is the source documents");
    const config::Loaded loaded = config::load();
    ok (loaded.ok(), "load(): no problem (" + std::to_string (loaded.problems.size()) + ")");
    for (const auto& p : loaded.problems)
        std::printf ("    %s %u:%u %s %s\n", config::name (p.document), (unsigned) p.line, (unsigned) p.column,
                     config::name (p.fault), p.path.c_str());
    const std::string targets = config::text (config::Document::Targets);
    const std::string engine = config::text (config::Document::Engine);
    ok (! targets.empty() && targets == canonicalOf (g_targetsText),
        "the embedded targets are targets.toml, byte for byte in canonical form (" + std::to_string (targets.size()) + " bytes)");
    ok (! engine.empty() && engine == canonicalOf (g_engineText),
        "the embedded engine is engine.toml, byte for byte in canonical form (" + std::to_string (engine.size()) + " bytes)");
    const config::Loaded fromFiles = config::bind (g_targetsText, g_engineText);
    ok (fromFiles.ok(), "and the source files bind clean through bind() too");
}

void itCarriesTheDecisions()
{
    felitronics::test::group ("the config carries the decisions, number by number");
    const config::Config c = config::load().config;
    const config::Engine& e = c.engine;

    ok (e.landing.passes == std::vector<std::int32_t> { 12, 24, 32 }, "landing: series of 12, 24 and 32 passes");
    ok (same (e.landing.toleranceLu, 0.1), "landing: landed within ±0.1 LU");
    ok (same (e.limiter.ceilingMarginDb, 0.15) && same (e.limiter.releaseMs, 50.0),
        "limiter: a 0.15 dB ceiling margin and a 50 ms release, no longer numbers inside a shell");
    ok (same (e.input.referenceLufs, -18.0), "input brought to −18 LUFS before the chain");
    ok (same (e.input.quietWarningLufs, -40.0) && same (e.input.quietGainOnlyLufs, -55.0),
        "quiet input: a warning below −40 LUFS, gain and ceiling only below −55");
    ok (same (e.hpf.hzMax, 50.0) && same (e.hpf.hzMin, 15.0), "high-pass: the knob and the machine top out at 50 Hz");
    ok (e.hpf.slopes == std::vector<std::int32_t> { 12, 24, 48 }, "high-pass slopes 12 / 24 / 48");
    ok (same (e.hpf.nothingToCut, 0.01), "no high-pass when the infra-low share is under 1 %");
    ok (same (e.observations.wideBassSideFractionAtLeast, 0.06) && e.observations.kinds.wideBass == config::Kind::Warning,
        "wide bass: ONE threshold, 6 % of side, and it is a warning");
    ok (e.compressor.thresholdFrom == config::ThresholdFrom::ShortTermP95,
        "the compressor's threshold is counted from the short-term P95");
    ok (e.glue.byTarget.size() == 1 && e.glue.byTarget[0].target == "cd" && same (e.glue.byTarget[0].position, 0.7),
        "the machine glues on cd only, at 0.7");
    ok (same (e.compressor.limitRelease.min, 50.0), "the compressor's release floor is 50 ms");
    ok (e.stages.limiter, "the limiter is always on");
    ok (e.dither.onUpToBits == 16, "dither at 16 bits only");
    ok (! e.deEsser.automatic && same (e.deEsser.manualDepthDb, -6.0), "the de-esser: manual, off, −6 dB");

    const config::Targets& t = c.targets;
    ok (t.defaultTarget == "allStreaming", "a session starts on allStreaming");
    ok (t.main == std::vector<std::string> { "allStreaming", "lp", "cdDynamic", "cd", "bandcamp", "club" }, "the main targets");
    ok (t.targets.size() == 25, "25 targets (" + std::to_string (t.targets.size()) + ")");
    bool monoBass = true, floor = true, loss = true, slope = true, shelf = true, clipper = true, always = true;
    bool pass = true, album = true;
    for (const config::Target& x : t.targets)
    {
        const bool lp = x.key == "lp", club = x.key == "club", cd = x.key == "cd" || x.key == "cdDynamic";
        monoBass = monoBass && same (x.monoBass, lp ? 150.0 : 120.0);
        floor = floor && same (x.hpfFloor, lp ? 32.0 : 24.0);
        loss = loss && same (x.noteLossDb, club ? 0.3 : 1.0);
        slope = slope && x.hpfSlopeDbPerOct == (lp ? 12 : 24);
        shelf = shelf && (lp ? x.lowShelfDb.has_value() && same (*x.lowShelfDb, 0.5) : ! x.lowShelfDb.has_value());
        clipper = clipper && x.noClipper == lp;
        always = always && x.hpfAlways == lp;
        pass = pass && x.sourceRatePass == cd;
        album = album && (x.key == "td1008" ? x.album.has_value() && same (x.album->lufs, -14.0) && x.album->desktopOnly
                                            : ! x.album.has_value());
    }
    ok (monoBass, "mono bass at 120 Hz on every target, 150 on vinyl");
    ok (floor, "the high-pass floor is 24 Hz on every target, 32 on vinyl");
    ok (loss, "the high-pass takes 1 dB at the lowest note, 0.3 on club");
    ok (slope, "the machine's high-pass slope is 24 dB/oct, 12 on vinyl");
    ok (shelf, "a +0.5 dB low shelf on vinyl, and on no other target");
    ok (clipper && always, "vinyl alone: no peak clipper, the high-pass always");
    ok (pass, "cd and cdDynamic alone take a pass at the source's rate");
    ok (album, "TD1008 carries the −14 LUFS album loudness, marked desktop only; no other target has one");
    const config::Target* td = config::find (t, "td1008");
    ok (td != nullptr && same (td->lufs, -16.0) && same (td->tp, -1.0), "TD1008's own target is the track, −16 / −1");
    ok (config::find (t, "no such target") == nullptr, "find() answers null for a key that is no row");
}

//==============================================================================
// THE SCHEMA'S CONTROLS

struct Plant
{
    std::string text;
    std::uint32_t line = 0, column = 0;   // where `at` begins, searched from where the plant went in
    bool planted = false;
};

// `text` with its one occurrence of `from` replaced by `to`, and the position of `at`, found from the plant onwards. A
// `from` that is not in the text exactly once is a control that has rotted, and fails as one.
Plant plant (const std::string& text, std::string_view from, std::string_view to, std::string_view at)
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

void mustRefuse (config::Document document, std::string_view from, std::string_view to, std::string_view at,
                 config::Fault fault, const std::string& path, config::Refusal refusal = config::Refusal::None)
{
    const bool inTargets = document == config::Document::Targets;
    const Plant p = plant (inTargets ? g_targetsText : g_engineText, from, to, at);
    const std::string what = std::string (config::name (fault)) + " " + path;
    if (! p.planted) { ok (false, what + ": the control has rotted — '" + std::string (from) + "' is not in the document once"); return; }
    const config::Loaded loaded = inTargets ? config::bind (p.text, g_engineText) : config::bind (g_targetsText, p.text);
    bool found = false;
    for (const auto& q : loaded.problems)
        found = found || (q.document == document && q.fault == fault && q.path == path && q.refusal == refusal
                          && q.line == p.line && q.column == p.column);
    std::string seen;
    for (const auto& q : loaded.problems)
        seen += std::string (" [") + config::name (q.document) + " " + std::to_string (q.line) + ":" + std::to_string (q.column)
              + " " + q.code + " " + q.path + "]";
    ok (found, what + " at " + config::name (document) + ".toml:" + std::to_string (p.line) + ":" + std::to_string (p.column)
                   + (found ? "" : " — got:" + seen));
}

void theSchemaRefuses()
{
    felitronics::test::group ("control: the schema refuses every planted mistake, at its line and column");
    ok (config::bind (g_targetsText, g_engineText).ok(), "PRECONDITION: the documents without a plant bind clean");
    using config::Document;
    using config::Fault;
    using config::Refusal;

    // Unknown keys: a typo is an error, never a setting silently ignored — in a table and inside an inline row.
    mustRefuse (Document::Engine, "releaseMs = 50", "releseMs = 50", "releseMs", Fault::UnknownKey, "limiter.releseMs");
    mustRefuse (Document::Targets, "hpfFloor = 32", "hpfFlor = 32", "hpfFlor", Fault::UnknownKey, "targets.lp.hpfFlor");
    // ...and the key the typo stood for is missing, pointed at the table that lacks it.
    mustRefuse (Document::Engine, "[limiter]\nceilingMarginDb = 0.15\n", "[limiter]\n", "[limiter]", Fault::Missing,
                "limiter.ceilingMarginDb");

    // Wrong types.
    mustRefuse (Document::Engine, "toleranceLu = 0.1", "toleranceLu = \"0.1\"", "\"0.1\"", Fault::WrongType, "landing.toleranceLu");
    mustRefuse (Document::Targets, "noClipper = true", "noClipper = 1", "1", Fault::WrongType, "targets.lp.noClipper");
    mustRefuse (Document::Targets, "default = \"allStreaming\"", "default = 14", "14", Fault::WrongType, "default");

    // Out of range: its own domain, an item of an array, and a range another key states (the edit travel, the knob).
    mustRefuse (Document::Engine, "hzMax = 50", "hzMax = 500", "500", Fault::OutOfRange, "hpf.hzMax");
    mustRefuse (Document::Engine, "passes = [12, 24, 32]", "passes = [12, 0, 32]", "0", Fault::OutOfRange, "landing.passes[1]");
    mustRefuse (Document::Targets, "lufs = -23", "lufs = -30", "-30", Fault::OutOfRange, "targets.ebu.lufs");
    mustRefuse (Document::Targets, "monoBass = 150", "monoBass = 400", "400", Fault::OutOfRange, "targets.lp.monoBass");
    mustRefuse (Document::Engine, "manualDefaultDb = 1.5", "manualDefaultDb = 4", "4", Fault::OutOfRange,
                "limiter.peakClipper.manualDefaultDb");

    // Refusals across keys.
    mustRefuse (Document::Engine, "byTarget = { cd = 0.7 }", "byTarget = { cdd = 0.7 }", "0.7", Fault::Refused,
                "glue.byTarget.cdd", Refusal::NotATarget);
    mustRefuse (Document::Targets, "default = \"allStreaming\"", "default = \"allStreamin\"", "\"allStreamin\"",
                Fault::Refused, "default", Refusal::NotATarget);
    mustRefuse (Document::Targets, "\"cd\", \"bandcamp\"", "\"cd\", \"cd\"", "\"cd\", \"club\"", Fault::Refused, "main[4]",
                Refusal::Duplicate);
    mustRefuse (Document::Targets, "hpfSlopeDbPerOct = 12", "hpfSlopeDbPerOct = 18", "18", Fault::Refused,
                "targets.lp.hpfSlopeDbPerOct", Refusal::NotOneOf);
    mustRefuse (Document::Engine, "detector = \"rms\"", "detector = \"rsm\"", "\"rsm\"", Fault::Refused,
                "compressor.detector", Refusal::NotOneOf);
    mustRefuse (Document::Engine, "limiter = true", "limiter = false", "false", Fault::Refused, "stages.limiter", Refusal::Fixed);
    mustRefuse (Document::Engine, "band = 2", "band = 1", "1", Fault::Refused, "lowShelf.band", Refusal::Duplicate);
    mustRefuse (Document::Engine, "hzNormal = [15, 42]", "hzNormal = [42, 15]", "[", Fault::Refused, "hpf.hzNormal",
                Refusal::OutOfOrder);

    // A document that is not TOML at all stops at the parser's error.
    mustRefuse (Document::Engine, "toleranceLu = 0.1", "toleranceLu = 0.1\ntoleranceLu = 0.2", "toleranceLu = 0.2",
                Fault::Syntax, "");
}

//==============================================================================
// THE VERSION

struct Leaves
{
    std::vector<std::uint64_t> versions;
    std::size_t numbers = 0, flags = 0, strings = 0;
};

// Changes leaf number `k` of the tree in place, in document order — an integer or a decimal by one unit of its last
// digit, a boolean flipped, a string given one more character — and answers true; or counts the leaves down and answers
// false when there are fewer than k + 1.
bool changeLeaf (toml::Value& v, std::size_t& k, Leaves& kinds);
bool changeLeaf (toml::Table& t, std::size_t& k, Leaves& kinds)
{
    for (const auto& e : t.entries())
        if (changeLeaf (*t.find (e.key), k, kinds)) return true;
    return false;
}
bool changeLeaf (toml::Value& v, std::size_t& k, Leaves& kinds)
{
    if (auto* table = std::get_if<toml::Table> (&v.data)) return changeLeaf (*table, k, kinds);
    if (auto* tables = std::get_if<toml::Tables> (&v.data))
    {
        for (auto& table : *tables)
            if (changeLeaf (table, k, kinds)) return true;
        return false;
    }
    if (auto* items = std::get_if<toml::Array> (&v.data))
    {
        for (auto& item : *items)
            if (changeLeaf (item, k, kinds)) return true;
        return false;
    }
    if (k != 0) { --k; return false; }
    if (auto* n = std::get_if<std::int64_t> (&v.data)) { *n += 1; ++kinds.numbers; }
    else if (auto* d = std::get_if<toml::Decimal> (&v.data)) { d->mantissa += 1; d->negativeZero = false; ++kinds.numbers; }
    else if (auto* b = std::get_if<bool> (&v.data)) { *b = ! *b; ++kinds.flags; }
    else if (auto* s = std::get_if<std::string> (&v.data)) { *s += 'x'; ++kinds.strings; }
    return true;
}

void theVersionMovesWithEveryValue()
{
    felitronics::test::group ("the config version: moved by every single value, through both paths, and by nothing else");
    const std::uint64_t v = config::version();
    const auto fromFiles = config::versionOf (g_targetsText, g_engineText);
    ok (fromFiles.has_value() && *fromFiles == v, "version(), the walk over the embedded data, is the source files' version");
    const auto& copyTargets = config::testcopy::targets;
    const auto& copyEngine = config::testcopy::engine;
    ok (config::detail::versionOf (copyTargets.root(), copyEngine.root()) == v,
        "...and this suite's own embedding hashes to it by the walk version() uses");
    ok (! config::versionOf ("[[", g_engineText).has_value(), "a document that does not parse has no version");
    char hex[20];
    std::snprintf (hex, sizeof hex, "%016llx", (unsigned long long) v);
    std::printf ("    config version %s\n", hex);

    // THROUGH THE TEXT: every leaf of the parsed sources changed in turn, the tree written and hashed as a document.
    const auto targets = std::get<toml::Table> (toml::parse (g_targetsText));
    const auto engine = std::get<toml::Table> (toml::parse (g_engineText));
    Leaves leaves;
    for (int doc = 0; doc < 2; ++doc)
        for (std::size_t k = 0;; ++k)
        {
            toml::Value root (doc == 0 ? targets : engine);
            std::size_t down = k;
            if (! changeLeaf (root, down, leaves)) break;
            const std::string changed = toml::write (std::get<toml::Table> (root.data));
            const auto w = doc == 0 ? config::versionOf (changed, g_engineText) : config::versionOf (g_targetsText, changed);
            leaves.versions.push_back (w ? *w : v);
        }
    std::size_t n = leaves.versions.size();
    std::printf ("    text: %zu values changed one at a time (%zu numbers, %zu flags, %zu strings)\n", n, leaves.numbers,
                 leaves.flags, leaves.strings);
    ok (n > 500, "text: " + std::to_string (n) + " values changed one at a time — " + std::to_string (leaves.numbers)
                     + " numbers, " + std::to_string (leaves.flags) + " flags, " + std::to_string (leaves.strings) + " strings");
    ok (std::none_of (leaves.versions.begin(), leaves.versions.end(), [v] (std::uint64_t x) { return x == v; }),
        "text: every one of them moves the version");
    std::vector<std::uint64_t> sorted = leaves.versions;
    std::sort (sorted.begin(), sorted.end());
    ok (std::adjacent_find (sorted.begin(), sorted.end()) == sorted.end(), "text: each to a version of its own");

    // THROUGH THE EMBEDDED DATA: the nodes of this suite's copy, one changed at a time — a number by one, a flag flipped,
    // a string or a key one byte shorter — and hashed by the walk version() uses.
    std::vector<std::uint64_t> fromNodes;
    std::size_t numbers = 0, flags = 0, texts = 0;
    for (int doc = 0; doc < 2; ++doc)
    {
        const felitronics::toml::embedded::Document& original = doc == 0 ? copyTargets : copyEngine;
        for (std::uint32_t i = 0; i < original.count; ++i)
            for (int what = 0; what < 2; ++what)   // 0: the value, 1: the key
            {
                std::vector<felitronics::toml::embedded::Node> nodes (original.nodes, original.nodes + original.count);
                auto& node = nodes[i];
                using felitronics::toml::embedded::Type;
                if (what == 1) { if (node.key.size == 0) continue; --node.key.size; ++texts; }
                else if (node.type == Type::Integer || node.type == Type::Decimal) { ++node.number; ++numbers; }
                else if (node.type == Type::Boolean) { node.number ^= 1; ++flags; }
                else if (node.type == Type::String && node.text.size != 0) { --node.text.size; ++texts; }
                else continue;
                const felitronics::toml::embedded::Document changed { nodes.data(), original.order, original.count,
                                                                       original.pools, original.poolCount };
                fromNodes.push_back (doc == 0 ? config::detail::versionOf (changed.root(), copyEngine.root())
                                              : config::detail::versionOf (copyTargets.root(), changed.root()));
            }
    }
    n = fromNodes.size();
    std::printf ("    data: %zu nodes changed one at a time (%zu numbers, %zu flags, %zu strings and keys)\n", n, numbers,
                 flags, texts);
    ok (n > 1000, "data: " + std::to_string (n) + " nodes changed one at a time — " + std::to_string (numbers) + " numbers, "
                      + std::to_string (flags) + " flags, " + std::to_string (texts) + " strings and keys");
    ok (std::none_of (fromNodes.begin(), fromNodes.end(), [v] (std::uint64_t x) { return x == v; }),
        "data: every one of them moves the version");
    std::sort (fromNodes.begin(), fromNodes.end());
    ok (std::adjacent_find (fromNodes.begin(), fromNodes.end()) == fromNodes.end(), "data: each to a version of its own");

    // What is not data does not move it: a comment added, and spacing changed.
    const Plant commented = plant (g_engineText, "[landing]\n", "[landing]   # a comment\n", "[landing]");
    const Plant spaced = plant (g_targetsText, "lufs = -23", "lufs    =    -23", "lufs");
    ok (commented.planted && spaced.planted, "PRECONDITION: a comment and some spacing planted");
    const auto same = config::versionOf (spaced.text, commented.text);
    ok (same.has_value() && *same == v, "a comment or spacing changes nothing: the version is of the data");
}
} // namespace

int main (int argc, char** argv)
{
    std::printf ("felitronics session::config tests\n");
    if (argc != 3 || ! readFile (argv[1], g_targetsText) || ! readFile (argv[2], g_engineText))
    {
        std::fprintf (stderr, "usage: %s <targets.toml> <engine.toml> — the two source documents, readable\n", argv[0]);
        return 2;
    }
    theEmbeddedConfigIsTheSource();
    itCarriesTheDecisions();
    theSchemaRefuses();
    theVersionMovesWithEveryValue();
    return felitronics::test::report();
}
