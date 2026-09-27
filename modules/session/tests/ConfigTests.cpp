// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for felitronics::session::config (Config.h) — the config compiled into the library, its schema and
// its version. Run as `felitronics_session_config_tests <targets.toml> <engine.toml>`, the two source documents, which
// ctest passes. The owner's decisions are pinned by a suite of their own (tests/ConfigDecisionsTests.cpp). Pinned:
//   * the embedded config binds with no problem, and it IS the source documents: the library's canonical text of each
//     equals felitronics-toml's canonical text of the file — so a stale embedding cannot pass;
//   * THE SCHEMA'S CONTROLS, each of which must go red: a planted unknown key, wrong type, missing key, value out of its
//     domain or out of a range another key states, refusal across keys and syntax error — every input a review found the
//     schema accepting among them — each reported with its document, key path, line and column; and the documents
//     without a plant bind clean, so the plant is the only difference;
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
    const std::string what = std::string (config::name (fault)) + (refusal != config::Refusal::None
        ? std::string (" ") + config::name (refusal) : std::string{}) + " " + path;
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
    const auto E = Document::Engine;
    const auto T = Document::Targets;

    // Unknown keys — a typo is an error, never a setting silently ignored — in a table and inside an inline row, and the
    // key the typo stood for is missing, pointed at the table that lacks it.
    mustRefuse (E, "releaseMs = 50", "releseMs = 50", "releseMs", Fault::UnknownKey, "limiter.releseMs");
    mustRefuse (T, "hpfFloor = 32", "hpfFlor = 32", "hpfFlor", Fault::UnknownKey, "targets.lp.hpfFlor");
    mustRefuse (E, "[limiter]\nceilingMarginDb = 0.15\n", "[limiter]\n", "[limiter]", Fault::Missing, "limiter.ceilingMarginDb");

    // Wrong types.
    mustRefuse (E, "toleranceLu = 0.1", "toleranceLu = \"0.1\"", "\"0.1\"", Fault::WrongType, "landing.toleranceLu");
    mustRefuse (T, "noClipper = true", "noClipper = 1", "1", Fault::WrongType, "targets.lp.noClipper");
    mustRefuse (T, "default = \"allStreaming\"", "default = 14", "14", Fault::WrongType, "default");
    mustRefuse (E, "dualRelease = false", "dualRelease = 0", "0", Fault::WrongType, "limiter.dualRelease");

    // Out of a domain: its own, an item of an array, and a range another key states (the edit travel, the knob).
    mustRefuse (E, "hzMax = 50", "hzMax = 500", "500", Fault::OutOfRange, "hpf.hzMax");
    mustRefuse (E, "passes = [12, 24, 32]", "passes = [12, 0, 32]", "0", Fault::OutOfRange, "landing.passes[1]");
    mustRefuse (T, "lufs = -23", "lufs = -30", "-30", Fault::OutOfRange, "targets.ebu.lufs");
    mustRefuse (T, "monoBass = 150", "monoBass = 400", "400", Fault::OutOfRange, "targets.lp.monoBass");
    mustRefuse (E, "betweenOverDb = 1.5", "betweenOverDb = 4", "4", Fault::OutOfRange, "limiter.peakClipper.betweenOverDb");
    // analysis::BandCrest's domains: a hop of 10 ms or more, at most 64 hops to a block.
    mustRefuse (E, "hopMs = 100", "hopMs = 1", "1", Fault::OutOfRange, "crest.hopMs");
    mustRefuse (E, "blockHops = 4", "blockHops = 65", "65", Fault::OutOfRange, "crest.blockHops");
    // A bound the ranges cannot say: above zero, above the core's own number.
    mustRefuse (E, "enterDb = 6", "enterDb = 0", "0", Fault::OutOfRange, "stereoBursts.enterDb");
    mustRefuse (E, "clipping = { fullAtShareOfProgramme = 0.001 }", "clipping = { fullAtShareOfProgramme = 0 }", "0 }",
                Fault::OutOfRange, "observations.clipping.fullAtShareOfProgramme");
    mustRefuse (E, "fullAtDropDb = 60", "fullAtDropDb = 24", "24", Fault::OutOfRange, "observations.spectralWall.fullAtDropDb");

    // Refusals across keys: names.
    mustRefuse (E, "byTarget = { cd = 0.7 }", "byTarget = { cdd = 0.7 }", "0.7", Fault::Refused, "glue.byTarget.cdd", Refusal::NotATarget);
    mustRefuse (T, "default = \"allStreaming\"", "default = \"allStreamin\"", "\"allStreamin\"", Fault::Refused, "default",
                Refusal::NotATarget);
    mustRefuse (T, "hpfSlopeDbPerOct = 12", "hpfSlopeDbPerOct = 18", "18", Fault::Refused, "targets.lp.hpfSlopeDbPerOct",
                Refusal::NotOneOf);
    mustRefuse (E, "detector = \"rms\"", "detector = \"rsm\"", "\"rsm\"", Fault::Refused, "compressor.detector", Refusal::NotOneOf);
    mustRefuse (T, "sampleRate = 48000, bitDepth = 24 }\n# YouTube Music", "sampleRate = 22050, bitDepth = 24 }\n# YouTube Music",
                "22050", Fault::Refused, "targets.youtube.sampleRate", Refusal::NotOneOf);
    // ...duplicates, everywhere a name counts once.
    mustRefuse (T, "\"cd\", \"bandcamp\"", "\"cd\", \"cd\"", "\"cd\", \"club\"", Fault::Refused, "main[4]", Refusal::Duplicate);
    mustRefuse (E, "targets = [\"allStreaming\", \"cdDynamic\"]", "targets = [\"allStreaming\", \"allStreaming\"]",
                "\"allStreaming\"]", Fault::Refused, "blindTest.targets[1]", Refusal::Duplicate);
    mustRefuse (E, "{ key = \"bass5\", hz = 31 }", "{ key = \"bass4\", hz = 31 }", "\"bass4\"", Fault::Refused, "hpf.marks[2].key",
                Refusal::Duplicate);
    mustRefuse (E, "band = 2", "band = 1", "1", Fault::Refused, "lowShelf.band", Refusal::Duplicate);
    // ...order: ranges, series and the classes of the peak clipper.
    mustRefuse (E, "passes = [12, 24, 32]", "passes = [12, 24, 2]", "2]", Fault::Refused, "landing.passes[2]", Refusal::OutOfOrder);
    mustRefuse (E, "lowNoteHz = 20\nhighNoteHz = 300", "lowNoteHz = 20\nhighNoteHz = 20", "20\nfftOrder", Fault::Refused,
                "lowEnd.run.highNoteHz", Refusal::OutOfOrder);
    mustRefuse (E, "bandLowHz = 5000\nbandHighHz = 9000", "bandLowHz = 5000\nbandHighHz = 5000", "5000\nhop", Fault::Refused,
                "stereoBursts.bandHighHz", Refusal::OutOfOrder);
    mustRefuse (E, "baselineMs = 2000", "baselineMs = 5", "5", Fault::Refused, "stereoBursts.baselineMs", Refusal::OutOfOrder);
    mustRefuse (E, "infraLowCrossoverHz = 30", "infraLowCrossoverHz = 120", "120", Fault::Refused, "lowEnd.infraLowCrossoverHz",
                Refusal::OutOfOrder);
    mustRefuse (E, "dcOffset = { from = 0.001, fullAt = 0.01 }", "dcOffset = { from = 0.001, fullAt = 0.001 }", "0.001 }",
                Fault::Refused, "observations.dcOffset.fullAt", Refusal::OutOfOrder);
    mustRefuse (E, "bitsUnused = { fromBits = 1, fullAtBits = 8 }", "bitsUnused = { fromBits = 1, fullAtBits = 1 }", "1 }",
                Fault::Refused, "observations.bitsUnused.fullAtBits", Refusal::OutOfOrder);
    mustRefuse (E, "witnessQuantiles = [0.5, 0.9]", "witnessQuantiles = [0.9, 0.5]", "0.5]", Fault::Refused,
                "deEsser.witnessQuantiles[1]", Refusal::OutOfOrder);
    mustRefuse (E, "shortP90Ms = 2", "shortP90Ms = 8", "8\nlongBassShare", Fault::Refused, "limiter.peakClipper.longP90Ms",
                Refusal::OutOfOrder);   // points at the value of longP90Ms
    mustRefuse (E, "shapingUpToBits = 16", "shapingUpToBits = 24", "24", Fault::Refused, "dither.shapingUpToBits", Refusal::OutOfOrder);
    mustRefuse (E, "slowReleaseMs = 200", "slowReleaseMs = 20", "20", Fault::Refused, "limiter.slowReleaseMs", Refusal::OutOfOrder);
    mustRefuse (E, "warningLowHz = 20, warningHighHz = 50", "warningLowHz = 20, warningHighHz = 40", "40", Fault::Refused,
                "hpf.comfort.warningHighHz", Refusal::OutOfOrder);
    mustRefuse (E, "hzMin = 15", "hzMin = 50", "50\nhzDefault", Fault::Refused, "hpf.hzMax", Refusal::OutOfOrder);
    // ...a crest of exactly three corners, and the printed quantiles that the fields' names state.
    mustRefuse (E, "bandEdgesHz = [120, 2000, 6000]", "bandEdgesHz = []", "[]", Fault::Refused, "crest.bandEdgesHz", Refusal::NotOneOf);
    mustRefuse (E, "printedQuantiles = [0.5, 0.95]", "printedQuantiles = [0.1, 0.9]", "[0.1", Fault::Refused, "cost.printedQuantiles",
                Refusal::Fixed);
    mustRefuse (E, "printedQuantiles = [0.5, 0.95]", "printedQuantiles = []", "[]", Fault::Refused, "cost.printedQuantiles",
                Refusal::Fixed);
    mustRefuse (E, "limiter = true", "limiter = false", "false", Fault::Refused, "stages.limiter", Refusal::Fixed);
    // ...the laws of a ramp: byDepth is the ratio's alone, geometric needs both ends above zero.
    mustRefuse (E, "to = -9, law = \"linear\"", "to = -9, law = \"geometric\"", "\"geometric\"", Fault::Refused,
                "glue.threshOffset.law", Refusal::OutsideLaw);
    mustRefuse (E, "knee = { from = 8, to = 4, law = \"linear\" }", "knee = { from = 0, to = 4, law = \"byDepth\" }", "\"byDepth\"",
                Fault::Refused, "glue.knee.law", Refusal::OutsideLaw);
    // ...a filter on a trace at or above half its rate, and a top band above Nyquist at the lowest rate.
    mustRefuse (E, "lowPassHz = 8", "lowPassHz = 20", "20", Fault::Refused, "cost.pumping.lowPassHz", Refusal::AboveNyquist);
    mustRefuse (E, "highNoteHz = 300", "highNoteHz = 3900", "3900", Fault::Refused, "lowEnd.run.highNoteHz", Refusal::AboveNyquist);
    // ...a value off its knob's step.
    mustRefuse (T, "appleMusic   = { group = \"streaming\", lufs = -16, tp = -1,", "appleMusic   = { group = \"streaming\", lufs = -16, tp = -1.05,",
                "-1.05", Fault::Refused, "targets.appleMusic.tp", Refusal::NotOnStep);
    mustRefuse (T, "lowShelfDb = 0.5", "lowShelfDb = 0.55", "0.55", Fault::Refused, "targets.lp.lowShelfDb", Refusal::NotOnStep);
    mustRefuse (E, "betweenOverDb = 1.5", "betweenOverDb = 1.25", "1.25", Fault::Refused, "limiter.peakClipper.betweenOverDb",
                Refusal::NotOnStep);
    mustRefuse (E, "lowWidth = 0\n", "lowWidth = 0.03\n", "0.03", Fault::Refused, "monoBass.lowWidth", Refusal::NotOnStep);
    mustRefuse (E, "hopMs = 100", "hopMs = 15", "15", Fault::Refused, "crest.hopMs", Refusal::NotOnStep);
    // ...one threshold named in two places, refused apart.
    mustRefuse (E, "dcOffsetBelow = 0.001", "dcOffsetBelow = 0.002", "0.002", Fault::Refused, "hpf.nothingBelowNote.dcOffsetBelow",
                Refusal::Mismatch);
    // ...an optional flag written as its default, and a flag where it cannot apply.
    mustRefuse (T, "sampleRate = 0, bitDepth = 24 }\nappleMusic", "sampleRate = 0, bitDepth = 24, hpfAlways = false }\nappleMusic",
                "false", Fault::Refused, "targets.spotifyLoud.hpfAlways", Refusal::WrittenDefault);
    mustRefuse (T, "sampleRate = 0, bitDepth = 24 }\nspotifyLoud", "sampleRate = 0, bitDepth = 24, sourceRatePass = true }\nspotifyLoud",
                "true", Fault::Refused, "targets.spotify.sourceRatePass", Refusal::NotApplicable);

    // A document that is not TOML at all stops at the parser's error.
    mustRefuse (E, "toleranceLu = 0.1", "toleranceLu = 0.1\ntoleranceLu = 0.2", "toleranceLu = 0.2", Fault::Syntax, "");
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
    theSchemaRefuses();
    theVersionMovesWithEveryValue();
    return felitronics::test::report();
}
