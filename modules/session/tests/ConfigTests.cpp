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

#include "ConfigVersion.h"      // the walk Config::versions() is (modules/session/src)
#include "testcopy/bands.h"     // this suite's own embedding of the three documents, by the same tool from the same files
#include "testcopy/engine.h"
#include "testcopy/targets.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace config = felitronics::session::config;
using config::Config;
using config::Problem;
namespace toml = felitronics::toml;
using config::testing::canonicalOf;
using config::testing::plant;
using config::testing::Plant;
using felitronics::test::ok;

namespace
{
std::string g_targetsText, g_engineText, g_bandsText;   // the source documents (bands.toml: felitronics-bands'), read in main()

template<class T> constexpr bool hasMix = requires (T value) { value.mix; };

void theCompressorHasNoUnusedMix()
{
    felitronics::test::group ("the glue owns the mix; the compressor has no unused setting");
    ok (! hasMix<config::Compressor>, "config::Compressor has no mix member");
    auto engine = g_engineText;
    const auto start = engine.find ("[compressor]\n");
    const auto mix = engine.find ("\nmix = 1\n", start);
    if (mix != std::string::npos && mix < engine.find ("[compressor.limits]", start))
        engine.erase (mix + 1, std::string_view ("mix = 1\n").size());
    ok (Config::bind (g_targetsText, engine).ok(), "the compressor binds without a mix");
    engine.insert (start + std::string_view ("[compressor]\n").size(), "mix = 1\n");
    const auto loaded = Config::bind (g_targetsText, engine);
    bool unknown = false;
    for (const auto& problem : loaded.problems)
        unknown = unknown || (problem.fault == config::Fault::UnknownKey && problem.path == "compressor.mix");
    ok (unknown, "the retired compressor.mix is refused as an unknown key");
}

void mustAccept (config::Document doc, std::string_view from, std::string_view to)
{
    const auto changed = plant (doc == config::Document::Engine ? g_engineText : g_targetsText, from, to, to);
    const auto loaded = Config::bind (doc == config::Document::Targets ? changed.text : g_targetsText,
                                      doc == config::Document::Engine ? changed.text : g_engineText);
    ok (changed.planted && loaded.ok(), "slider hint does not reject " + std::string (to));
}

void theEmbeddedConfigIsTheSource()
{
    felitronics::test::group ("the embedded config binds with no problem, and it is the source documents");
    const config::Loaded loaded = Config::load();
    ok (loaded.ok(), "load(): no problem (" + std::to_string (loaded.problems.size()) + ")");
    for (const auto& p : loaded.problems)
        std::printf ("    %s %u:%u %s %s %s\n", Problem::name (p.document), (unsigned) p.line, (unsigned) p.column,
                     Problem::name (p.fault), p.path.c_str(), p.code);
    const std::string targets = Config::text (config::Document::Targets);
    const std::string engine = Config::text (config::Document::Engine);
    ok (! targets.empty() && targets == canonicalOf (g_targetsText),
        "the embedded targets are targets.toml, byte for byte in canonical form (" + std::to_string (targets.size()) + " bytes)");
    ok (! engine.empty() && engine == canonicalOf (g_engineText),
        "the embedded engine is engine.toml, byte for byte in canonical form (" + std::to_string (engine.size()) + " bytes)");
    ok (Config::bind (g_targetsText, g_engineText).ok(), "and the source files bind clean through bind() too");
    const std::string bands = Config::text (config::Document::Bands);
    ok (! bands.empty() && bands == canonicalOf (g_bandsText),
        "the embedded bands are felitronics-bands' bands.toml, byte for byte in canonical form (" + std::to_string (bands.size()) + " bytes)");
    ok (Config::bind (g_targetsText, g_engineText, g_bandsText).ok(), "...and the three source files bind clean");
}

//==============================================================================
// THE SCHEMA'S CONTROLS

// `at` is searched from the plant onwards — or, when it names a table's header ("[crest]"), from the top: a block the
// analyzer refuses is refused at its header.
void mustRefuse (config::Document document, std::string_view from, std::string_view to, std::string_view at,
                 config::Fault fault, const std::string& path, config::Refusal refusal = config::Refusal::None)
{
    const bool inTargets = document == config::Document::Targets, inBands = document == config::Document::Bands;
    const bool header = at.size() > 2 && at.front() == '[' && at.back() == ']' && at[1] != ']';
    Plant p = plant (inTargets ? g_targetsText : inBands ? g_bandsText : g_engineText, from, to, header ? to : at);
    if (p.planted && header)
    {
        // The header's own line: the bracketed name also occurs in comments.
        const std::string line = "\n" + std::string (at) + "\n";
        p = plant (p.text, line, line, at);
    }
    const std::string what = std::string (Problem::name (fault)) + (refusal != config::Refusal::None
        ? std::string (" ") + Problem::name (refusal) : std::string{}) + " " + path;
    if (! p.planted) { ok (false, what + ": the control has rotted — '" + std::string (from) + "' is not in the document once"); return; }
    const config::Loaded loaded = inTargets ? Config::bind (p.text, g_engineText, g_bandsText)
                                : inBands   ? Config::bind (g_targetsText, g_engineText, p.text)
                                            : Config::bind (g_targetsText, p.text, g_bandsText);
    bool found = false;
    for (const auto& q : loaded.problems)
        found = found || (q.document == document && q.fault == fault && q.path == path && q.refusal == refusal
                          && q.line == p.line && q.column == p.column);
    std::string seen;
    for (const auto& q : loaded.problems)
        seen += std::string (" [") + Problem::name (q.document) + " " + std::to_string (q.line) + ":" + std::to_string (q.column)
              + " " + q.code + " " + q.path + "]";
    ok (found, what + " at " + Problem::name (document) + ".toml:" + std::to_string (p.line) + ":" + std::to_string (p.column)
                   + (found ? "" : " — got:" + seen));
}

void theSchemaRefuses()
{
    felitronics::test::group ("control: the schema refuses every planted mistake, at its line and column");
    ok (Config::bind (g_targetsText, g_engineText).ok(), "PRECONDITION: the documents without a plant bind clean");
    using config::Document;
    using config::Fault;
    using config::Refusal;
    const auto E = Document::Engine;
    const auto T = Document::Targets;
    mustRefuse (E, "limiterSlopeBelow = 0.2", "limiterSlopeBelow = 0", "0", Fault::OutOfRange, "landing.limiterSlopeBelow");
    mustRefuse (E, "limiterSlopeSpacingDb = 0.5", "limiterSlopeSpacingDb = 0", "0", Fault::OutOfRange, "landing.limiterSlopeSpacingDb");
    mustRefuse (E, "budgetResolutionDb = 0.25", "budgetResolutionDb = 0.01", "0.01", Fault::OutOfRange,
                "landing.max.budgetResolutionDb");
    // A max mode's `cleaner` (MVP): true when absent, so it is written only as false; written as false it is read so.
    mustRefuse (E, "dense = { budgetDb = 1.75 }", "dense = { budgetDb = 1.75, cleaner = true }", "true", Fault::Refused,
                "landing.max.dense.cleaner", Refusal::WrittenDefault);
    {
        const auto changed = plant (g_engineText, "dense = { budgetDb = 1.75 }", "dense = { budgetDb = 1.75, cleaner = false }",
                                    "cleaner");
        const auto loaded = Config::bind (g_targetsText, changed.text);
        const auto& l = loaded.config.engine.landing;
        ok (changed.planted && loaded.ok() && ! l.denseCleaner && l.cleanCleaner && l.extremeCleaner,
            "[landing.max] dense = { cleaner = false } is read as false, the modes without the key as true");
    }

    // Unknown keys — a typo is an error, never a setting silently ignored — in a table and inside an inline row, and the
    // key the typo stood for is missing, pointed at the table that lacks it.
    mustRefuse (E, "releaseMs = 50", "releseMs = 50", "releseMs", Fault::UnknownKey, "limiter.releseMs");
    mustRefuse (T, "monoBass = 150, hpfFloor = 32", "monoBass = 150, hpfFlor = 32", "hpfFlor", Fault::UnknownKey, "targets.lp.hpfFlor");
    mustRefuse (E, "[limiter]\nceilingMarginDb = 0.15\n", "[limiter]\n", "[limiter]", Fault::Missing, "limiter.ceilingMarginDb");

    mustRefuse (E, "driveRange = [0, 12]", "driveRange = [0, 13]", "13", Fault::OutOfRange, "saturation.driveRange[1]");
    mustRefuse (E, "lowWidth = 0\n", "lowWidth = 1.01\n", "1.01", Fault::OutOfRange, "monoBass.lowWidth");
    mustRefuse (E, "releaseMs = 50", "releaseMs = 0.99", "0.99", Fault::OutOfRange, "limiter.releaseMs");
    mustRefuse (E, "slowReleaseMs = 200", "slowReleaseMs = 0.99", "0.99", Fault::OutOfRange, "limiter.slowReleaseMs");
    mustAccept (E, "releaseMs = 50", "releaseMs = 1");
    mustRefuse (E, "[tilt]\ndomain = [-6, 6]\nband = 1\nnormal = [-1.5, 1.5]\nhard = [-3, 3]",
                "[tilt]\ndomain = [1, 6]\nband = 1\nnormal = [1, 2]\nhard = [1, 3]",
                "[1, 6", Fault::OutOfRange, "tilt.domain");
    // [hpf] note.aboveHz is gone (owner, 07.10): the note is sought from [lowEnd] lowestNoteFromHz alone, and the key is unknown.
    mustRefuse (E, "note = { ", "note = { aboveHz = 20, ", "aboveHz", Fault::UnknownKey, "hpf.note.aboveHz");
    // Where the lowest note is sought from (owner, 06.10): a band centre in hertz, 0 the whole table, under the table's top.
    mustRefuse (E, "lowestNoteFromHz = 30\n", "", "[lowEnd]", Fault::Missing, "lowEnd.lowestNoteFromHz");
    mustRefuse (E, "lowestNoteFromHz = 30", "lowestNoteFromHz = 200.5", "200.5", Fault::OutOfRange, "lowEnd.lowestNoteFromHz");
    mustRefuse (E, "lowestNoteFromHz = 30", "lowestNoteFromHz = -1", "-1", Fault::OutOfRange, "lowEnd.lowestNoteFromHz");
    mustAccept (E, "lowestNoteFromHz = 30", "lowestNoteFromHz = 0");
    // A knob's step is 0 or more (owner, 07.10): 0, no step, is what every manual knob takes now; under 0 is no step at all.
    mustRefuse (E, "hzStep = 0", "hzStep = -1", "-1", Fault::OutOfRange, "hpf.hzStep");
    mustRefuse (E, "frequencyStep = 0", "frequencyStep = -1", "-1", Fault::OutOfRange, "monoBass.frequencyStep");
    mustRefuse (E, "knobStepDb = 0", "knobStepDb = -0.1", "-0.1", Fault::OutOfRange, "glue.knobStepDb");
    mustRefuse (E, "mixStep = 0\n", "mixStep = -0.1\n", "-0.1", Fault::OutOfRange, "glue.mixStep");
    mustRefuse (E, "driveStep = 0", "driveStep = -0.5", "-0.5", Fault::OutOfRange, "saturation.driveStep");
    mustRefuse (E, "manualStepDb = 0", "manualStepDb = -0.1", "-0.1", Fault::OutOfRange, "limiter.peakClipper.manualStepDb");
    mustRefuse (T, "green = [-15, -13], step = 0 }", "green = [-15, -13], step = -0.1 }", "-0.1", Fault::OutOfRange, "edit.lufs.step");
    // ...and the two knobs that keep their step refuse 0: mono bass's width and the saturation's mix.
    mustRefuse (E, "lowWidthStep = 0.05", "lowWidthStep = 0", "0", Fault::OutOfRange, "monoBass.lowWidthStep");
    mustRefuse (E, "mixStep = 0.05", "mixStep = 0", "0", Fault::OutOfRange, "saturation.mixStep");
    // A comfort window lies inside its knob's domain, in order, and is required (owner, 07.10).
    mustRefuse (E, "warningHighHz = 250 }", "warningHighHz = 170 }", "170", Fault::Refused, "monoBass.comfort.warningHighHz",
                Refusal::OutOfOrder);
    mustRefuse (E, "comfort = { low = 0, high = 1.5, warningLow = 0, warningHigh = 6 }",
                "comfort = { low = 0, high = 1.5, warningLow = 0, warningHigh = 7 }", "7", Fault::OutOfRange, "glue.comfort.warningHigh");
    mustRefuse (E, "mixComfort = { low = 0.3, high = 1, warningLow = 0, warningHigh = 1 }\n", "", "[glue]", Fault::Missing, "glue.mixComfort");
    mustRefuse (E, "driveComfort = { low = 0, high = 1.5, warningLow = 0, warningHigh = 8 }",
                "driveComfort = { low = 2, high = 1.5, warningLow = 0, warningHigh = 8 }", "1.5", Fault::Refused,
                "saturation.driveComfort.high", Refusal::OutOfOrder);
    mustAccept (E, "byTarget = { cd = 2.6 }", "byTarget = { cd = 2.75 }");
    // The machine's glue stays on the slider's travel (owner decision 3.8): above knobMaxDb is a person's alone.
    mustRefuse (E, "byTarget = { cd = 2.6 }", "byTarget = { cd = 3.25 }", "3.25", Fault::OutOfRange, "glue.byTarget.cd");
    mustRefuse (E, "whenTicked = 2.6", "whenTicked = 3.5", "3.5", Fault::OutOfRange, "glue.whenTicked");
    // The tick's character lies in each field's own domain (ratio 1…10, attack within attackMsDomain), and the saturation's
    // tick within the drive's domain.
    mustRefuse (E, "ticked = { ratio = 2,", "ticked = { ratio = 11,", "11", Fault::OutOfRange, "glue.ticked.ratio");
    mustRefuse (E, "attackMs = 30, releaseMs = 300 }", "attackMs = 0, releaseMs = 300 }", "0", Fault::OutOfRange, "glue.ticked.attackMs");
    mustRefuse (E, "whenTicked = 6", "whenTicked = 13", "13", Fault::OutOfRange, "saturation.whenTicked");
    mustAccept (E, "byTarget = { cd = 2.6 }", "byTarget = { cd = 3 }");
    mustAccept (T, "lowDb = 0.5", "lowDb = 5.25");
    mustAccept (T, "monoBass = 150, hpfFloor = 32", "monoBass = 150, hpfFloor = 100.25");
    // Wrong types.
    mustRefuse (E, "toleranceLu = 0.1", "toleranceLu = \"0.1\"", "\"0.1\"", Fault::WrongType, "landing.toleranceLu");
    mustRefuse (T, "noClipper = true", "noClipper = 1", "1", Fault::WrongType, "targets.lp.noClipper");
    mustRefuse (T, "default = \"maxClean\"", "default = 14", "14", Fault::WrongType, "default");
    mustRefuse (E, "dualRelease = false", "dualRelease = 0", "0", Fault::WrongType, "limiter.dualRelease");
    // The oversampling's bounds are integers — the session reads them so (Rules::oversampling): a bound written with a
    // decimal point stops the config here, at its line and column, not the session when a factor is checked.
    mustRefuse (E, "oversamplingDomain = [2, 16]", "oversamplingDomain = [2.0, 16.0]", "2.0", Fault::WrongType, "limiter.oversamplingDomain[0]");
    mustRefuse (E, "oversamplingDomain = [2, 16]", "oversamplingDomain = [2.5, 16.5]", "2.5", Fault::WrongType, "limiter.oversamplingDomain[0]");
    mustAccept (E, "oversamplingDomain = [2, 16]", "oversamplingDomain = [4, 8]");

    // Out of a domain: its own, an item of an array, and a range another key states (the edit travel, the knob).
    mustAccept (E, "hzMax = 80", "hzMax = 500");
    mustRefuse (E, "passes = 12", "passes = 0", "0", Fault::OutOfRange, "landing.passes");
    mustAccept (T, "lufs = -23", "lufs = -30");
    mustRefuse (T, "monoBass = 150, hpfFloor = 32", "monoBass = 400, hpfFloor = 32", "400", Fault::OutOfRange, "targets.lp.monoBass");
    mustRefuse (E, "betweenCutDb = 1.5", "betweenCutDb = 7", "7", Fault::OutOfRange, "limiter.peakClipper.betweenCutDb");
    // A bound the ranges cannot say: above zero, above the core's own number, a hole in a range.
    mustRefuse (T, "sampleRate = 48000, bitDepth = 24 }\n# YouTube Music", "sampleRate = 4000, bitDepth = 24 }\n# YouTube Music",
                "4000", Fault::OutOfRange, "targets.youtube.sampleRate");
    mustRefuse (E, "clipping = { fullAtShareOfProgramme = 0.001 }", "clipping = { fullAtShareOfProgramme = 0 }", "0 }",
                Fault::OutOfRange, "observations.clipping.fullAtShareOfProgramme");
    mustRefuse (E, "fullAtDropDb = 40", "fullAtDropDb = 24", "24", Fault::OutOfRange, "observations.spectralWall.fullAtDropDb");

    // Fractional cutoff and slider bounds are valid independently of the slider step.
    mustAccept (E, "hzMax = 80", "hzMax = 80.5");
    mustAccept (E, "machineTopHz = 50", "machineTopHz = 50.5");
    mustAccept (E, "hzMin = 15", "hzMin = 15.5");

    // Refusals across keys: names — the one a row goes by among them, which an empty key cannot be.
    mustRefuse (T, "ebu          = {", "\"\"           = {", "{ group = \"streaming\", class = \"specification\", lufs = -23", Fault::Refused, "targets.\"\"",
                Refusal::EmptyKey);
    mustRefuse (E, "byTarget = { cd = 2.6 }", "byTarget = { cdd = 2.6 }", "2.6", Fault::Refused, "glue.byTarget.cdd", Refusal::NotATarget);
    mustRefuse (T, "default = \"maxClean\"", "default = \"maxClea\"", "\"maxClea\"", Fault::Refused, "default",
                Refusal::NotATarget);
    mustAccept (T, "hpfSlopeDbPerOct = 12", "hpfSlopeDbPerOct = 18");
    mustRefuse (E, "detector = \"rms\"", "detector = \"rsm\"", "\"rsm\"", Fault::Refused, "compressor.detector", Refusal::NotOneOf);
    // ...duplicates, everywhere a name counts once.
    mustRefuse (T, "\"cd\", \"bandcamp\"", "\"cd\", \"cd\"", "\"cd\", \"club\"", Fault::Refused, "main[4]", Refusal::Duplicate);
    mustRefuse (E, "targets = [\"allStreaming\", \"cdDynamic\"]", "targets = [\"allStreaming\", \"allStreaming\"]",
                "\"allStreaming\"]", Fault::Refused, "blindTest.targets[1]", Refusal::Duplicate);
    mustRefuse (E, "{ key = \"bass5\", hz = 30.87 }", "{ key = \"bass4\", hz = 30.87 }", "\"bass4\"", Fault::Refused, "hpf.marks[2].key",
                Refusal::Duplicate);
    mustRefuse (E, "band = 2", "band = 1", "1", Fault::Refused, "low.band", Refusal::Duplicate);
    // ...order: ranges and the classes of the peak clipper.
    mustRefuse (E, "infraLowCrossoverHz = 30", "infraLowCrossoverHz = 120", "120", Fault::Refused, "lowEnd.infraLowCrossoverHz",
                Refusal::OutOfOrder);
    mustRefuse (E, "from = 0.001, fullAt = 0.01,", "from = 0.001, fullAt = 0.001,", "0.001, warningFrom",
                Fault::Refused, "observations.dcOffset.fullAt", Refusal::OutOfOrder);
    mustRefuse (E, "warningFrom = 0.01, errorFrom = 0.1 }", "warningFrom = 0.01, errorFrom = 0.01 }", "0.01 }",
                Fault::Refused, "observations.dcOffset.errorFrom", Refusal::OutOfOrder);
    mustRefuse (E, "fromBitsShort = 1, fullAtBitsShort = 8,", "fromBitsShort = 1, fullAtBitsShort = 1,", "1, errorFromBitsShort",
                Fault::Refused, "observations.bitsUnused.fullAtBitsShort", Refusal::OutOfOrder);
    mustRefuse (E, "plrBelowDb = 10.5, fullAtPlrDb = 7 }", "plrBelowDb = 10.5, fullAtPlrDb = 11 }", "11 }",
                Fault::Refused, "observations.alreadyLimited.fullAtPlrDb", Refusal::OutOfOrder);
    mustRefuse (E, "witnessQuantiles = [0.5, 0.9]", "witnessQuantiles = [0.9, 0.5]", "0.5]", Fault::Refused,
                "deEsser.witnessQuantiles[1]", Refusal::OutOfOrder);
    mustRefuse (E, "shortP90Ms = 2", "shortP90Ms = 8", "8\nlongBassShare", Fault::Refused, "limiter.peakClipper.longP90Ms",
                Refusal::OutOfOrder);   // points at the value of longP90Ms
    mustRefuse (E, "shapingUpToBits = 16", "shapingUpToBits = 24", "24", Fault::Refused, "dither.shapingUpToBits", Refusal::OutOfOrder);
    mustRefuse (E, "slowReleaseMs = 200", "slowReleaseMs = 20", "20", Fault::Refused, "limiter.slowReleaseMs", Refusal::OutOfOrder);
    mustRefuse (E, "warningLowHz = 26, warningHighHz = 50", "warningLowHz = 26, warningHighHz = 40", "40", Fault::Refused,
                "hpf.comfort.warningHighHz", Refusal::OutOfOrder);
    mustRefuse (E, "hzMin = 15", "hzMin = 80", "80\nmachineTopHz", Fault::Refused, "hpf.hzMax", Refusal::OutOfOrder);
    // The machine's top lies on the knob's travel: above its start, at most its end, a finite number, and there.
    mustAccept (E, "machineTopHz = 50", "machineTopHz = 80");
    mustRefuse (E, "machineTopHz = 50", "machineTopHz = 81", "81", Fault::Refused, "hpf.machineTopHz", Refusal::OutOfOrder);
    mustRefuse (E, "machineTopHz = 50", "machineTopHz = 15", "15", Fault::Refused, "hpf.machineTopHz", Refusal::OutOfOrder);
    mustRefuse (E, "hzMin = 15", "hzMin = 60", "50\nslopes", Fault::Refused, "hpf.machineTopHz", Refusal::OutOfOrder);
    mustRefuse (E, "hzMax = 80", "hzMax = 49", "50\nslopes", Fault::Refused, "hpf.machineTopHz", Refusal::OutOfOrder);
    mustRefuse (E, "machineTopHz = 50", "machineTopHz = nan", "nan", Fault::Syntax, "");   // UnsupportedValue: never a number
    mustRefuse (E, "machineTopHz = 50", "machineTopHz = inf", "inf", Fault::Syntax, "");
    mustRefuse (E, "machineTopHz = 50", "machineTopHz = \"50\"", "\"50\"", Fault::WrongType, "hpf.machineTopHz");
    mustRefuse (E, "hzMax = 80\nmachineTopHz = 50\n", "hzMax = 80\n", "[hpf]", Fault::Missing, "hpf.machineTopHz");
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
    // ...a filter on a trace at or above half its rate.
    mustRefuse (E, "lowPassHz = 8", "lowPassHz = 20", "20", Fault::Refused, "cost.pumping.lowPassHz", Refusal::AboveNyquist);
    // ...and a block its analyzer refuses at a source rate the product accepts — asked of the analyzer itself
    // (storageFor), refused at the block: a crest hop under its 10 ms quantum or more than 64 hops to a block; a note
    // range of one note, or whose top band is above Nyquist at 8 kHz; a burst band of no width, a baseline under a hop or
    // over the analyzer's 65 536 hops, no enter level, a band whose corners are one float.
    mustRefuse (E, "hopMs = 100", "hopMs = 1", "[crest]", Fault::Refused, "crest", Refusal::AnalyzerRefuses);
    mustRefuse (E, "blockHops = 4", "blockHops = 65", "[crest]", Fault::Refused, "crest", Refusal::AnalyzerRefuses);
    mustRefuse (E, "lowNoteHz = 10\nhighNoteHz = 500", "lowNoteHz = 10\nhighNoteHz = 10", "[lowEnd.run]", Fault::Refused,
                "lowEnd.run", Refusal::AnalyzerRefuses);
    mustRefuse (E, "highNoteHz = 500", "highNoteHz = 3900", "[lowEnd.run]", Fault::Refused, "lowEnd.run", Refusal::AnalyzerRefuses);
    // The note's top (07.10): a band centre in hertz, 0 the whole table; the analyzer refuses a negative one.
    mustRefuse (E, "noteTopHz = 300\n", "", "[lowEnd.run]", Fault::Missing, "lowEnd.run.noteTopHz");
    mustRefuse (E, "noteTopHz = 300", "noteTopHz = -1", "-1", Fault::OutOfRange, "lowEnd.run.noteTopHz");
    mustAccept (E, "noteTopHz = 300", "noteTopHz = 0");
    // The rate up to which fftOrder holds (owner, 07.10: 96 kHz resolves as 48 kHz): required and positive; one that asks an
    // order the analyzer cannot take at a source rate (1 Hz: 13 more at 8 kHz) refuses the block.
    mustRefuse (E, "fftOrderUpToHz = 48000\n", "", "[lowEnd.run]", Fault::Missing, "lowEnd.run.fftOrderUpToHz");
    mustRefuse (E, "fftOrderUpToHz = 48000", "fftOrderUpToHz = 0", "0", Fault::OutOfRange, "lowEnd.run.fftOrderUpToHz");
    mustRefuse (E, "fftOrderUpToHz = 48000", "fftOrderUpToHz = 1", "[lowEnd.run]", Fault::Refused, "lowEnd.run", Refusal::AnalyzerRefuses);
    mustAccept (E, "fftOrderUpToHz = 48000", "fftOrderUpToHz = 96000");
    // The background's veto is gone (owner, 07.10: real mixes keep their notes): [lowEnd] occupiedAboveBackgroundDb is unknown.
    mustRefuse (E, "occupiedMarginWhenOnDb = 2\n", "occupiedMarginWhenOnDb = 2\noccupiedAboveBackgroundDb = 6\n", "occupiedAboveBackgroundDb",
                Fault::UnknownKey, "lowEnd.occupiedAboveBackgroundDb");
    // The note range's share of the programme (owner, 07.10): −240…0 dB, required.
    mustRefuse (E, "noteRangeShareAtLeastDb = -140\n", "", "[lowEnd]", Fault::Missing, "lowEnd.noteRangeShareAtLeastDb");
    mustRefuse (E, "noteRangeShareAtLeastDb = -140", "noteRangeShareAtLeastDb = 1", "1", Fault::OutOfRange, "lowEnd.noteRangeShareAtLeastDb");
    mustRefuse (E, "noteRangeShareAtLeastDb = -140", "noteRangeShareAtLeastDb = -241", "-241", Fault::OutOfRange,
                "lowEnd.noteRangeShareAtLeastDb");
    mustRefuse (E, "bandLowHz = 5000\nbandHighHz = 9000", "bandLowHz = 5000\nbandHighHz = 5000", "[stereoBursts]", Fault::Refused,
                "stereoBursts", Refusal::AnalyzerRefuses);
    mustRefuse (E, "bandLowHz = 5000\nbandHighHz = 9000", "bandLowHz = 5000\nbandHighHz = 5000.0001", "[stereoBursts]",
                Fault::Refused, "stereoBursts", Refusal::AnalyzerRefuses);
    mustRefuse (E, "baselineMs = 2000", "baselineMs = 5", "[stereoBursts]", Fault::Refused, "stereoBursts", Refusal::AnalyzerRefuses);
    mustRefuse (E, "baselineMs = 2000", "baselineMs = 1000000", "[stereoBursts]", Fault::Refused, "stereoBursts",
                Refusal::AnalyzerRefuses);
    mustRefuse (E, "enterDb = 6", "enterDb = 0", "[stereoBursts]", Fault::Refused, "stereoBursts", Refusal::AnalyzerRefuses);
    // ...a value off its knob's step.
    mustAccept (T, "appleMusic   = { group = \"streaming\", class = \"streaming\", lufs = -16, tp = -1,", "appleMusic   = { group = \"streaming\", class = \"streaming\", lufs = -16, tp = -1.05,");
    mustAccept (T, "lowDb = 0.5", "lowDb = 0.55");
    mustAccept (E, "betweenCutDb = 1.5", "betweenCutDb = 1.25");
    mustAccept (E, "lowWidth = 0\n", "lowWidth = 0.03\n");
    mustRefuse (E, "hopMs = 100", "hopMs = 15", "15", Fault::Refused, "crest.hopMs", Refusal::NotOnStep);
    // ...a warning above the loss that takes mono bass out.
    mustRefuse (E, "warnFromDb = 1, offAboveDb = 3", "warnFromDb = 4, offAboveDb = 3", "3", Fault::Refused, "monoBass.loss.offAboveDb",
                Refusal::OutOfOrder);
    // ...an optional flag written as its default, and a flag where it cannot apply.
    mustRefuse (T, "sampleRate = 0, bitDepth = 24 }\nappleMusic", "sampleRate = 0, bitDepth = 24, noClipper = false }\nappleMusic",
                "false", Fault::Refused, "targets.spotifyLoud.noClipper", Refusal::WrittenDefault);
    mustRefuse (T, "sampleRate = 0, bitDepth = 24 }\nspotifyLoud", "sampleRate = 0, bitDepth = 24, sourceRatePass = true }\nspotifyLoud",
                "true", Fault::Refused, "targets.spotify.sourceRatePass", Refusal::NotApplicable);

    // A document that is not TOML at all stops at the parser's error.
    mustRefuse (E, "toleranceLu = 0.1", "toleranceLu = 0.1\ntoleranceLu = 0.2", "toleranceLu = 0.2", Fault::Syntax, "");
}

// Binds the documents with edits planted in either, or reports that a plant rotted.
struct Edit
{
    config::Document document;
    std::string_view from, to;
};
std::optional<config::Loaded> bindWith (std::initializer_list<Edit> edits, std::string* targets = nullptr)
{
    std::string t = g_targetsText, e = g_engineText;
    for (const Edit& x : edits)
    {
        std::string& text = x.document == config::Document::Targets ? t : e;
        const Plant p = plant (text, x.from, x.to, x.to);
        if (! p.planted) return std::nullopt;
        text = p.text;
    }
    if (targets != nullptr) *targets = t;
    return Config::bind (t, e);
}

bool hasProblem (const config::Loaded& l, config::Document d, const std::string& path, config::Refusal r, const std::string& text,
                 std::string_view at)
{
    const Plant where = plant (text, at, at, at);   // the position of `at`, which must be in the text once
    for (const auto& q : l.problems)
        if (q.document == d && q.path == path && q.refusal == r && q.line == where.line && q.column == where.column) return true;
    return false;
}

void theSchemaAdmitsWhatTheAnalyzersAdmit()
{
    felitronics::test::group ("control: analyzer domains and slider hints are independent");
    using config::Document;
    using config::Refusal;
    // A two-note range just two hertz wide: LowEnd::storageFor admits it (MIDI 16 and 17 fall inside 20…22 Hz), and a
    // hand-written semitone rule refused it.
    const auto twoNotes = bindWith ({ { Document::Engine, "lowNoteHz = 10\nhighNoteHz = 500", "lowNoteHz = 20\nhighNoteHz = 22" } });
    ok (twoNotes && twoNotes->ok(), "lowEnd.run 20…22 Hz: two notes, admitted by the analyzer, passes the schema");
    // The grid is counted from the travel's start: a mono-bass travel from 60.5 Hz puts 120 Hz half a step off.
    std::string targets;
    const auto offset = bindWith ({ { Document::Engine, "frequencyRange = [60, 300]", "frequencyRange = [60.5, 300.0]" } }, &targets);
    ok (offset && offset->ok(), "shifted slider travel does not reject defaults");
    // Across the documents the engine's step is its own decimal, all nine places: on a low shelf of step 0.100000001 from
    // −3, 0.500000035 is on the grid and 0.5 is not.
    const auto fine = bindWith ({ { Document::Engine, "band = 2\nnormal = [-1.5, 1.5]\nhard = [-3, 3]\nstep = 0",
                                    "band = 2\nnormal = [-1.5, 1.5]\nhard = [-3, 3]\nstep = 0.100000001" },
                                  { Document::Targets, "lowDb = 0.5", "lowDb = 0.500000035" } });
    ok (fine && fine->ok(), "lowDb 0.500000035 on a step of 0.100000001 from −3: on the grid");
    const auto coarse = bindWith ({ { Document::Engine, "band = 2\nnormal = [-1.5, 1.5]\nhard = [-3, 3]\nstep = 0",
                                      "band = 2\nnormal = [-1.5, 1.5]\nhard = [-3, 3]\nstep = 0.100000001" } }, &targets);
    ok (coarse && coarse->ok(), "defaults need not be on slider steps");
}

//==============================================================================
// THE VERSIONS

// Which key paths cannot change a master — this suite's own statement of the rule, held against the library's walk: a
// path listed, or under one, moves `all` and never `sound`; every other moves both. The rule: what is shown, what prints a
// finding or a warning without switching a device (every observation threshold, polarity included), what is
// measured after the master, development, the name of the defaults — and, while no shell offers the de-esser, its block
// and the bursts only it reads (but not deEsser.offered itself: offering it brings them in).
constexpr std::string_view kTargetsPresentation[] = {
    "main", "notes", "edit.lufs.from", "edit.lufs.to", "edit.lufs.green", "edit.tp.from", "edit.tp.to", "edit.tp.green",
};
constexpr std::string_view kEnginePresentation[] = {
    "defaults", "limiter.peakClipper.densityMinusDb", "limiter.peakClipper.densityWithinDb", "hpf.slopesNormal",
    "hpf.comfort", "hpf.curveTopDb", "hpf.curveBottomDb", "hpf.curveStepDb", "hpf.curveHeadroomDb", "hpf.marks",
    "monoBass.zones", "monoBass.comfort", "glue.comfort", "glue.mixComfort", "glue.thresholdDbComfort", "glue.ratioComfort",
    "glue.kneeDbComfort", "glue.attackMsComfort", "glue.releaseMsComfort", "limiter.releaseMsComfort", "limiter.lookaheadMsComfort", "saturation.driveComfort", "saturation.cut",
    "tilt.normal", "low.normal",
    "bands.body.normal", "bands.mud.normal", "bands.forward.normal", "bands.brightness.normal", "bands.air.normal", "eq", "crest", "cost", "progress", "blindTest",
};
constexpr std::string_view kWhileNoDeEsser[] = { "deEsser", "stereoBursts" };

bool under (const std::string& path, std::string_view p)
{
    return path == p || (path.size() > p.size() && path.compare (0, p.size(), p) == 0 && path[p.size()] == '.');
}

bool presentation (int doc, const std::string& path)
{
    const auto covers = [&] (std::string_view p) { return under (path, p); };
    if (doc == 0) return std::any_of (std::begin (kTargetsPresentation), std::end (kTargetsPresentation), covers);
    if (under (path, "observations")) return true;
    if (path == "deEsser.offered") return false;   // the documents' de-esser is not offered
    return std::any_of (std::begin (kEnginePresentation), std::end (kEnginePresentation), covers)
        || std::any_of (std::begin (kWhileNoDeEsser), std::end (kWhileNoDeEsser), covers);
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
    const config::Versions v = Config::versions();
    char hex[48];
    std::snprintf (hex, sizeof hex, "%016llx / %016llx", (unsigned long long) v.all, (unsigned long long) v.sound);
    std::printf ("    config versions (all / sound) %s\n", hex);
    ok (sameVersions (Config::versionsOf (g_targetsText, g_engineText, g_bandsText), v)
            && sameVersions (Config::versionsOf (g_targetsText, g_engineText), v),
        "versions(), the walk over the embedded data, are the source files' versions");
    const auto& tc = config::testcopy::targets;
    const auto& ec = config::testcopy::engine;
    const auto& bc = config::testcopy::bands;
    ok (config::detail::version (tc.root(), ec.root(), bc.root(), false) == v.all
            && config::detail::version (tc.root(), ec.root(), bc.root(), true) == v.sound,
        "...and this suite's own embedding hashes to them by the walk versions() uses");
    ok (v.all != v.sound, "all and sound are two versions");
    ok (! Config::versionsOf ("[[", g_engineText).has_value(), "a document that does not parse has no version");

    // Each is a meaning-preserving respelling a review found moving the old version.
    struct Respelling { config::Document document; std::string_view from, to, what; };
    const Respelling respellings[] = {
        { config::Document::Engine, "toleranceLu = 0.1", "toleranceLu = 0.10", "a decimal's scale: 0.1 as 0.10" },
        { config::Document::Engine, "releaseMs = 50\n", "releaseMs = 50.0\n", "an integer written as a decimal: 50 as 50.0" },
        { config::Document::Targets, "lowDb = 0.5", "lowDb = 0.50", "a target's number respelled: 0.5 as 0.50" },
        { config::Document::Engine, "makeupDb = 0\n", "makeupDb = -0.0\n", "a zero written as −0.0" },
        { config::Document::Engine, "ceilingMarginDb = 0.15\nreleaseMs = 50\n", "releaseMs = 50\nceilingMarginDb = 0.15\n",
          "two keys of a table swapped" },
        { config::Document::Engine, "referenceLufs = -18\nquiet = { warningLufs = -40, gainOnlyLufs = -55 }\n",
          "quiet = { warningLufs = -40, gainOnlyLufs = -55 }\nreferenceLufs = -18\n", "a key moved after an inline table" },
        { config::Document::Engine, "quiet = { warningLufs = -40, gainOnlyLufs = -55 }\nshortSeconds = 10\n",
          "shortSeconds = 10\n\n[input.quiet]\nwarningLufs = -40\ngainOnlyLufs = -55\n",
          "an inline table written under a header of its own" },
    };
    for (const auto& r : respellings)
    {
        const bool inTargets = r.document == config::Document::Targets;
        const Plant p = plant (inTargets ? g_targetsText : g_engineText, r.from, r.to, r.to.substr (0, 3));
        if (! p.planted) { ok (false, std::string (r.what) + ": the respelling has rotted"); continue; }
        const auto w = inTargets ? Config::versionsOf (p.text, g_engineText) : Config::versionsOf (g_targetsText, p.text);
        const auto bound = inTargets ? Config::bind (p.text, g_engineText) : Config::bind (g_targetsText, p.text);
        ok (bound.ok() && sameVersions (w, v), std::string (r.what) + ": still valid, and neither version moves");
    }
    // A comment and spacing are not data either.
    const Plant commented = plant (g_engineText, "[landing]\n", "[landing]   # a comment\n", "[landing]");
    const Plant spaced = plant (g_targetsText, "lufs = -23", "lufs    =    -23", "lufs");
    ok (commented.planted && spaced.planted && sameVersions (Config::versionsOf (spaced.text, commented.text), v),
        "a comment or spacing changes nothing");
    // Order counts where it means something: the main list, in presentation — so `all` moves and `sound` does not.
    const Plant reordered = plant (g_targetsText, "\"allStreaming\", \"lp\"", "\"lp\", \"allStreaming\"", "\"lp\"");
    const auto r = reordered.planted ? Config::versionsOf (reordered.text, g_engineText) : std::nullopt;
    ok (r && r->all != v.all && r->sound == v.sound, "the main list reordered moves all, and not sound");
}

void theSoundIsWhatCanChangeAMaster()
{
    felitronics::test::group ("sound: what can change a master; what only shows or prints moves all alone");
    const config::Versions v = Config::versions();
    struct Case { config::Document document; std::string_view from, to, what; bool soundMoves; };
    const std::string spotifyRow = "spotify      = { group = \"streaming\", class = \"streaming\", lufs = -14, tp = -1, monoBass = 120, hpfFloor = 32, "
                                   "hpfSlopeDbPerOct = 24, noteLossDb = 1, sampleRate = 0, bitDepth = 24 }\n";
    const std::string loudRow = "spotifyLoud  = { group = \"streaming\", class = \"streaming\", lufs = -11, tp = -2, monoBass = 120, hpfFloor = 32, "
                                "hpfSlopeDbPerOct = 24, noteLossDb = 1, sampleRate = 0, bitDepth = 24 }\n";
    const std::string rows = spotifyRow + loudRow, swapped = loudRow + spotifyRow;
    const Case cases[] = {
        { config::Document::Targets, rows, swapped, "two target rows swapped: the order a shell lists them in", false },
        { config::Document::Targets, "default = \"maxClean\"", "default = \"spotify\"",
          "the default target: a project that omits an unchanged target reopens on it", true },
        { config::Document::Targets, "lufs = { domain = \"finite\", from = -25,", "lufs = { domain = \"finite\", from = -26,", "the hand edit's travel", false },
        { config::Document::Targets, "green = [-15, -13], step = 0 }", "green = [-15, -13], step = 0.05 }",
          "the hand edit's step, which the targets' numbers sit on", true },
        { config::Document::Engine, "clipping = { fullAtShareOfProgramme = 0.001 }", "clipping = { fullAtShareOfProgramme = 0.002 }",
          "a report-only observation threshold (clipping's full weight)", false },
        { config::Document::Engine, "wideBass = { sideFractionAtLeast = 0.06, fullAt = 0.3 }", "wideBass = { sideFractionAtLeast = 0.07, fullAt = 0.3 }",
          "the wide-bass warning: mono bass is placed whatever it says", false },
        { config::Document::Engine, "polarity = { correlationBelow = 0,", "polarity = { correlationBelow = 0.1,",
          "observations.polarity: a finding — mono bass is decided by the loss of its own band", false },
        { config::Document::Engine, "normal = [-1.5, 1.5]\nhard = [-3, 3]\nstep = 0\n\n# LOW",
          "normal = [-1.2, 1.5]\nhard = [-3, 3]\nstep = 0\n\n# LOW", "tilt's red zone", false },
        { config::Document::Engine, "slopesNormal = [12, 24]", "slopesNormal = [12]", "which slopes warn", false },
        { config::Document::Engine, "densityMinusDb = 3", "densityMinusDb = 4", "the peak clipper's printed density", false },
        { config::Document::Engine, "band = 8", "band = 9", "the de-esser's block, while no shell offers it", false },
        { config::Document::Engine, "witnessQuantiles = [0.5, 0.9]", "witnessQuantiles = [0.5, 0.95]",
          "the de-esser's witness quantiles, while it is not offered", false },
        { config::Document::Engine, "eventCapacity = 16384", "eventCapacity = 8192", "the bursts only the de-esser reads", false },
        { config::Document::Engine, "[deEsser]\noffered = false", "[deEsser]\noffered = true",
          "offering the de-esser brings its block in", true },
        { config::Document::Engine, "toleranceLu = 0.1", "toleranceLu = 0.2", "the landing's tolerance", true },
    };
    for (const auto& c : cases)
    {
        const bool inTargets = c.document == config::Document::Targets;
        const Plant p = plant (inTargets ? g_targetsText : g_engineText, c.from, c.to, c.to);
        if (! p.planted) { ok (false, std::string (c.what) + ": the case has rotted"); continue; }
        const auto w = inTargets ? Config::versionsOf (p.text, g_engineText) : Config::versionsOf (g_targetsText, p.text);
        const bool held = w && w->all != v.all && (w->sound != v.sound) == c.soundMoves;
        ok (held, std::string (c.what) + (c.soundMoves ? ": moves all and sound" : ": moves all, and not sound"));
    }
}

void theVersionsMoveWithEveryValue()
{
    felitronics::test::group ("the versions: moved by every single value, sound by what can change a master, through both paths");
    const config::Versions v = Config::versions();

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
            const auto w = doc == 0 ? Config::versionsOf (changed, g_engineText) : Config::versionsOf (g_targetsText, changed);
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
    const auto& copyBands = config::testcopy::bands;
    std::vector<std::uint64_t> fromNodes;
    std::size_t numbers = 0, flags = 0, texts = 0, bandsKeptSound = 0;
    for (int doc = 0; doc < 3; ++doc)
    {
        const toml::embedded::Document& original = doc == 0 ? copyTargets : doc == 1 ? copyEngine : copyBands;
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
                const auto t = doc == 0 ? changed.root() : copyTargets.root();
                const auto e = doc == 1 ? changed.root() : copyEngine.root();
                const auto b = doc == 2 ? changed.root() : copyBands.root();
                fromNodes.push_back (config::detail::version (t, e, b, false));
                // felitronics-bands' geometry is sound, every node of it.
                if (doc == 2 && config::detail::version (t, e, b, true) == v.sound) ++bandsKeptSound;
            }
    }
    std::printf ("    data: %zu nodes changed one at a time (%zu numbers, %zu flags, %zu strings and keys)\n", fromNodes.size(),
                 numbers, flags, texts);
    ok (fromNodes.size() > 1000, "data: " + std::to_string (fromNodes.size()) + " nodes changed one at a time");
    ok (std::none_of (fromNodes.begin(), fromNodes.end(), [&] (std::uint64_t x) { return x == v.all; }),
        "data: every one of them moves all");
    std::sort (fromNodes.begin(), fromNodes.end());
    ok (std::adjacent_find (fromNodes.begin(), fromNodes.end()) == fromNodes.end(), "data: each to a version of its own");
    ok (bandsKeptSound == 0, "data: every node of bands.toml moves sound (" + std::to_string (bandsKeptSound) + " did not)");
}

// THE NAMED BANDS ARE FELITRONICS-BANDS': the filters of tilt, low and the five bands are bands.toml's alone — a pivot, a Q
// or a type written back into engine.toml is a key nothing reads — and the two documents name the same seven bands.
void theBandsAreFelitronicsBands()
{
    felitronics::test::group ("the EQ devices' filters are felitronics-bands' bands.toml, never a copy in engine.toml");
    using config::Document;
    using config::Fault;
    const config::Loaded loaded = Config::load();
    const config::Engine& e = loaded.config.engine;
    const auto same = [] (double a, double b) { return ! (a < b) && ! (b < a); };
    ok (loaded.ok() && same (e.tilt.freqHz, 1000) && same (e.low.freqHz, 80) && same (e.low.q, 0.6) && same (e.bands.body.freqHz, 160)
            && e.bands.air.type == config::BandType::HighShelf,
        "the bound engine carries bands.toml's numbers (tilt 1000 Hz, low 80 Hz q 0.6, body 160 Hz, air a high shelf)");
    mustRefuse (Document::Engine, "[tilt]\n", "[tilt]\nfreqHz = 1000\n", "freqHz", Fault::UnknownKey, "tilt.freqHz");
    mustRefuse (Document::Engine, "[low]\n", "[low]\nq = 0.6\n", "q = 0.6", Fault::UnknownKey, "low.q");
    mustRefuse (Document::Engine, "[bands.body]\n", "[bands.body]\nq = 0.7\n", "q = 0.7", Fault::UnknownKey, "bands.body.q");
    mustRefuse (Document::Engine, "[bands.air]\n", "[bands.air]\ntype = \"highShelf\"\n", "type", Fault::UnknownKey,
                "bands.air.type");
    mustRefuse (Document::Engine, "[bands.air]", "[bands.treble]", "treble", Fault::UnknownKey, "bands.treble");
    mustRefuse (Document::Bands, "[bands.air]", "[bands.treble]", "treble", Fault::UnknownKey, "bands.treble");
    mustRefuse (Document::Bands, "type = \"lowShelf\"", "type = \"bell\"", "\"bell\"", Fault::Refused, "bands.low.type",
                config::Refusal::NotOneOf);
}
} // namespace

int main (int argc, char** argv)
{
    std::printf ("felitronics session::config tests\n");
    if (argc != 4 || ! config::testing::readFile (argv[1], g_targetsText) || ! config::testing::readFile (argv[2], g_engineText)
        || ! config::testing::readFile (argv[3], g_bandsText))
    {
        std::fprintf (stderr, "usage: %s <targets.toml> <engine.toml> <bands.toml> — the source documents, readable\n", argv[0]);
        return 2;
    }
    theEmbeddedConfigIsTheSource();
    theCompressorHasNoUnusedMix();
    theBandsAreFelitronicsBands();
    theSchemaRefuses();
    theSchemaAdmitsWhatTheAnalyzersAdmit();
    theVersionsAreNormalised();
    theSoundIsWhatCanChangeAMaster();
    theVersionsMoveWithEveryValue();
    return felitronics::test::report();
}
