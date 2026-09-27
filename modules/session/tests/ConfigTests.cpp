// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// JUCE-free self-tests for felitronics::session::config (Config.h) — the config compiled into the library, its schema and
// its versions. Run as `felitronics_session_config_tests <targets.toml> <engine.toml>`, the two source documents, which
// ctest passes. The owner's decisions are pinned by a suite of their own (tests/ConfigDecisionsTests.cpp). Pinned here:
//   * the embedded config binds with no problem, and it IS the source documents: the library's canonical text of each
//     equals felitronics-toml's canonical text of the file — so a stale embedding cannot pass;
//   * THE SCHEMA'S CONTROLS, each of which must go red: a planted unknown key, wrong type, missing key, value out of its
//     domain or out of a range another key states, refusal across keys and syntax error — every input a review found the
//     schema accepting among them — each reported with its document, key path, line and column; and the documents
//     without a plant bind clean, so the plant is the only difference;
//   * THE VERSIONS: normalised — a number's spelling, the order of keys, inline-or-not and −0 move nothing; every value of
//     both documents changed one at a time moves `all`, each to a value of its own, and moves `sound` exactly when the
//     value can change a master — through the documents' text and through the embedded data.

#include "ConfigTestSupport.h"

#include <felitronics_test.h>
#include <felitronics/session/Config.h>
#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include "ConfigVersion.h"      // the walk config::versions() is (modules/session/src)
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
using config::testing::canonicalOf;
using config::testing::plant;
using config::testing::Plant;
using felitronics::test::ok;

namespace
{
std::string g_targetsText, g_engineText;   // the two source documents, read once in main()

void theEmbeddedConfigIsTheSource()
{
    felitronics::test::group ("the embedded config binds with no problem, and it is the source documents");
    const config::Loaded loaded = config::load();
    ok (loaded.ok(), "load(): no problem (" + std::to_string (loaded.problems.size()) + ")");
    for (const auto& p : loaded.problems)
        std::printf ("    %s %u:%u %s %s %s\n", config::name (p.document), (unsigned) p.line, (unsigned) p.column,
                     config::name (p.fault), p.path.c_str(), p.code);
    const std::string targets = config::text (config::Document::Targets);
    const std::string engine = config::text (config::Document::Engine);
    ok (! targets.empty() && targets == canonicalOf (g_targetsText),
        "the embedded targets are targets.toml, byte for byte in canonical form (" + std::to_string (targets.size()) + " bytes)");
    ok (! engine.empty() && engine == canonicalOf (g_engineText),
        "the embedded engine is engine.toml, byte for byte in canonical form (" + std::to_string (engine.size()) + " bytes)");
    ok (config::bind (g_targetsText, g_engineText).ok(), "and the source files bind clean through bind() too");
}

//==============================================================================
// THE SCHEMA'S CONTROLS

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
// THE VERSIONS

// Which key paths cannot change a master — this suite's own statement, held against the library's walk: a path of the
// list, or under one, moves `all` and never `sound`; every other moves both.
constexpr std::string_view kTargetsPresentation[] = { "default", "main", "edit.lufs.green", "edit.tp.green" };
constexpr std::string_view kEnginePresentation[] = {
    "defaults", "hpf.comfort", "hpf.curveTopDb", "hpf.curveBottomDb", "hpf.curveStepDb", "hpf.curveHeadroomDb", "hpf.marks",
    "monoBass.zones", "eq", "observations.kinds", "crest", "cost", "progress", "blindTest",
};

bool presentation (int doc, const std::string& path)
{
    const auto covers = [&] (std::string_view p) { return path == p || (path.size() > p.size() && path.compare (0, p.size(), p) == 0
                                                                        && path[p.size()] == '.'); };
    if (doc == 0) return std::any_of (std::begin (kTargetsPresentation), std::end (kTargetsPresentation), covers);
    return std::any_of (std::begin (kEnginePresentation), std::end (kEnginePresentation), covers);
}

struct Leaves
{
    std::size_t numbers = 0, flags = 0, strings = 0;
};

// Changes leaf number `k` of the tree in place, in document order — a number by one unit of its last digit, a boolean
// flipped, a string given one more character — records its key path and answers true; or counts the leaves down and
// answers false when there are fewer than k + 1.
bool changeLeaf (toml::Value& v, std::size_t& k, Leaves& kinds, const std::string& path, std::string& changed);
bool changeLeaf (toml::Table& t, std::size_t& k, Leaves& kinds, const std::string& path, std::string& changed)
{
    for (const auto& e : t.entries())
        if (changeLeaf (*t.find (e.key), k, kinds, path.empty() ? e.key : path + "." + e.key, changed)) return true;
    return false;
}
bool changeLeaf (toml::Value& v, std::size_t& k, Leaves& kinds, const std::string& path, std::string& changed)
{
    if (auto* table = std::get_if<toml::Table> (&v.data)) return changeLeaf (*table, k, kinds, path, changed);
    if (auto* tables = std::get_if<toml::Tables> (&v.data))
    {
        for (auto& table : *tables)
            if (changeLeaf (table, k, kinds, path, changed)) return true;
        return false;
    }
    if (auto* items = std::get_if<toml::Array> (&v.data))
    {
        for (auto& item : *items)
            if (changeLeaf (item, k, kinds, path, changed)) return true;
        return false;
    }
    if (k != 0) { --k; return false; }
    if (auto* n = std::get_if<std::int64_t> (&v.data)) { *n += 1; ++kinds.numbers; }
    else if (auto* d = std::get_if<toml::Decimal> (&v.data)) { d->mantissa += 1; d->negativeZero = false; ++kinds.numbers; }
    else if (auto* b = std::get_if<bool> (&v.data)) { *b = ! *b; ++kinds.flags; }
    else if (auto* s = std::get_if<std::string> (&v.data)) { *s += 'x'; ++kinds.strings; }
    changed = path;
    return true;
}

bool sameVersions (const std::optional<config::Versions>& a, const config::Versions& b)
{
    return a && a->all == b.all && a->sound == b.sound;
}

void theVersionsAreNormalised()
{
    felitronics::test::group ("the versions are of the data: spelling, key order, inline-or-not and −0 move nothing");
    const config::Versions v = config::versions();
    char hex[48];
    std::snprintf (hex, sizeof hex, "%016llx / %016llx", (unsigned long long) v.all, (unsigned long long) v.sound);
    std::printf ("    config versions (all / sound) %s\n", hex);
    ok (sameVersions (config::versionsOf (g_targetsText, g_engineText), v),
        "versions(), the walk over the embedded data, are the source files' versions");
    ok (config::detail::version (config::testcopy::targets.root(), config::testcopy::engine.root(), false) == v.all
            && config::detail::version (config::testcopy::targets.root(), config::testcopy::engine.root(), true) == v.sound,
        "...and this suite's own embedding hashes to them by the walk versions() uses");
    ok (v.all != v.sound, "all and sound are two versions");
    ok (! config::versionsOf ("[[", g_engineText).has_value(), "a document that does not parse has no version");

    // Each is a meaning-preserving respelling a review found moving the old version.
    struct Respelling { config::Document document; std::string_view from, to, what; };
    const Respelling respellings[] = {
        { config::Document::Engine, "toleranceLu = 0.1", "toleranceLu = 0.10", "a decimal's scale: 0.1 as 0.10" },
        { config::Document::Engine, "releaseMs = 50\n", "releaseMs = 50.0\n", "an integer written as a decimal: 50 as 50.0" },
        { config::Document::Targets, "lowShelfDb = 0.5", "lowShelfDb = 0.50", "a target's number respelled: 0.5 as 0.50" },
        { config::Document::Engine, "makeupDb = 0\n", "makeupDb = -0.0\n", "a zero written as −0.0" },
        { config::Document::Engine, "ceilingMarginDb = 0.15\nreleaseMs = 50\n", "releaseMs = 50\nceilingMarginDb = 0.15\n",
          "two keys of a table swapped" },
        { config::Document::Engine, "referenceLufs = -18\nquiet = { warningLufs = -40, gainOnlyLufs = -55 }\n",
          "quiet = { warningLufs = -40, gainOnlyLufs = -55 }\nreferenceLufs = -18\n", "a key moved after an inline table" },
        { config::Document::Engine, "quiet = { warningLufs = -40, gainOnlyLufs = -55 }\nshortSeconds = 10\nshortConfidence = 0.5\n",
          "shortSeconds = 10\nshortConfidence = 0.5\n\n[input.quiet]\nwarningLufs = -40\ngainOnlyLufs = -55\n",
          "an inline table written under a header of its own" },
    };
    for (const auto& r : respellings)
    {
        const bool inTargets = r.document == config::Document::Targets;
        const Plant p = plant (inTargets ? g_targetsText : g_engineText, r.from, r.to, r.to.substr (0, 3));
        if (! p.planted) { ok (false, std::string (r.what) + ": the respelling has rotted"); continue; }
        const auto w = inTargets ? config::versionsOf (p.text, g_engineText) : config::versionsOf (g_targetsText, p.text);
        const auto bound = inTargets ? config::bind (p.text, g_engineText) : config::bind (g_targetsText, p.text);
        ok (bound.ok() && sameVersions (w, v), std::string (r.what) + ": still valid, and neither version moves");
    }
    // A comment and spacing are not data either.
    const Plant commented = plant (g_engineText, "[landing]\n", "[landing]   # a comment\n", "[landing]");
    const Plant spaced = plant (g_targetsText, "lufs = -23", "lufs    =    -23", "lufs");
    ok (commented.planted && spaced.planted && sameVersions (config::versionsOf (spaced.text, commented.text), v),
        "a comment or spacing changes nothing");
    // Order counts where it means something: the main list, in presentation — so `all` moves and `sound` does not.
    const Plant reordered = plant (g_targetsText, "\"allStreaming\", \"lp\"", "\"lp\", \"allStreaming\"", "\"lp\"");
    const auto r = reordered.planted ? config::versionsOf (reordered.text, g_engineText) : std::nullopt;
    ok (r && r->all != v.all && r->sound == v.sound, "the main list reordered moves all, and not sound");
}

void theVersionsMoveWithEveryValue()
{
    felitronics::test::group ("the versions: moved by every single value, sound by what can change a master, through both paths");
    const config::Versions v = config::versions();

    // THROUGH THE TEXT: every leaf of the parsed sources changed in turn, the tree written and hashed as a document.
    const auto targets = std::get<toml::Table> (toml::parse (g_targetsText));
    const auto engine = std::get<toml::Table> (toml::parse (g_engineText));
    Leaves leaves;
    std::vector<std::uint64_t> all;
    std::size_t soundMoved = 0, soundKept = 0, wrongSound = 0;
    std::string firstWrong;
    for (int doc = 0; doc < 2; ++doc)
        for (std::size_t k = 0;; ++k)
        {
            toml::Value root (doc == 0 ? targets : engine);
            std::size_t down = k;
            std::string path;
            if (! changeLeaf (root, down, leaves, "", path)) break;
            const std::string changed = toml::write (std::get<toml::Table> (root.data));
            const auto w = doc == 0 ? config::versionsOf (changed, g_engineText) : config::versionsOf (g_targetsText, changed);
            if (! w) { all.push_back (v.all); continue; }
            all.push_back (w->all);
            const bool shown = presentation (doc, path);
            const bool moved = w->sound != v.sound;
            (moved ? soundMoved : soundKept) += 1;
            if (moved == shown) { ++wrongSound; if (firstWrong.empty()) firstWrong = path; }
        }
    std::printf ("    text: %zu values changed one at a time (%zu numbers, %zu flags, %zu strings); sound moved by %zu, kept by %zu\n",
                 all.size(), leaves.numbers, leaves.flags, leaves.strings, soundMoved, soundKept);
    ok (all.size() > 500, "text: " + std::to_string (all.size()) + " values changed one at a time");
    ok (std::none_of (all.begin(), all.end(), [&] (std::uint64_t x) { return x == v.all; }), "text: every one of them moves all");
    std::sort (all.begin(), all.end());
    ok (std::adjacent_find (all.begin(), all.end()) == all.end(), "text: each to a version of its own");
    ok (wrongSound == 0 && soundMoved > 0 && soundKept > 0,
        "text: sound moves exactly for the values that can change a master"
            + (firstWrong.empty() ? std::string{} : " — not for " + firstWrong));

    // THROUGH THE EMBEDDED DATA: the nodes of this suite's copy, one changed at a time — a number by one, a flag flipped,
    // a string or a key one byte shorter — and hashed by the walk versions() uses.
    const auto& copyTargets = config::testcopy::targets;
    const auto& copyEngine = config::testcopy::engine;
    std::vector<std::uint64_t> fromNodes;
    std::size_t numbers = 0, flags = 0, texts = 0;
    for (int doc = 0; doc < 2; ++doc)
    {
        const toml::embedded::Document& original = doc == 0 ? copyTargets : copyEngine;
        for (std::uint32_t i = 0; i < original.count; ++i)
            for (int what = 0; what < 2; ++what)   // 0: the value, 1: the key
            {
                std::vector<toml::embedded::Node> nodes (original.nodes, original.nodes + original.count);
                auto& node = nodes[i];
                using toml::embedded::Type;
                if (what == 1) { if (node.key.size == 0) continue; --node.key.size; ++texts; }
                else if (node.type == Type::Integer || node.type == Type::Decimal) { ++node.number; ++numbers; }
                else if (node.type == Type::Boolean) { node.number ^= 1; ++flags; }
                else if (node.type == Type::String && node.text.size != 0) { --node.text.size; ++texts; }
                else continue;
                const toml::embedded::Document changed { nodes.data(), original.order, original.count, original.pools,
                                                         original.poolCount };
                fromNodes.push_back (doc == 0 ? config::detail::version (changed.root(), copyEngine.root(), false)
                                              : config::detail::version (copyTargets.root(), changed.root(), false));
            }
    }
    std::printf ("    data: %zu nodes changed one at a time (%zu numbers, %zu flags, %zu strings and keys)\n", fromNodes.size(),
                 numbers, flags, texts);
    ok (fromNodes.size() > 1000, "data: " + std::to_string (fromNodes.size()) + " nodes changed one at a time");
    ok (std::none_of (fromNodes.begin(), fromNodes.end(), [&] (std::uint64_t x) { return x == v.all; }),
        "data: every one of them moves all");
    std::sort (fromNodes.begin(), fromNodes.end());
    ok (std::adjacent_find (fromNodes.begin(), fromNodes.end()) == fromNodes.end(), "data: each to a version of its own");
}
} // namespace

int main (int argc, char** argv)
{
    std::printf ("felitronics session::config tests\n");
    if (argc != 3 || ! config::testing::readFile (argv[1], g_targetsText) || ! config::testing::readFile (argv[2], g_engineText))
    {
        std::fprintf (stderr, "usage: %s <targets.toml> <engine.toml> — the two source documents, readable\n", argv[0]);
        return 2;
    }
    theEmbeddedConfigIsTheSource();
    theSchemaRefuses();
    theVersionsAreNormalised();
    theVersionsMoveWithEveryValue();
    return felitronics::test::report();
}
