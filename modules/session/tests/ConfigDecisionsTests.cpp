// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE OWNER'S DECISIONS IN THE CONFIG, pinned apart from the schema. The schema (src/ConfigSchema.cpp) says what a number may
// be — its type and its physical domain; this suite says what the owner decided it IS: every target row, field by field,
// and the engine's decided numbers. Changing a decision is therefore a deliberate edit of this file, never a side effect of
// a schema that happens to admit the new number. Run as `felitronics_session_config_decisions_tests <targets.toml>
// <engine.toml>`.
//
// AND THE VERSION OF THE SOUND, pinned to the name of the defaults (kGolden): a number that can change a master, changed
// without naming new defaults in engine.toml, turns this red — a project records the name, so two sets of numbers must
// never carry one.
//
// Its controls plant a departure a review found the schema admitting (the high-pass top above 50 Hz, another slope, another
// landing series, another target number or delivery rate, glue by default, a wider mono bass, another knob start) and
// require this suite to name it.

#include "ConfigTestSupport.h"

#include <felitronics_test.h>
#include <felitronics/session/Config.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace config = felitronics::session::config;
using config::testing::plant;
using config::testing::Plant;
using config::testing::same;
using felitronics::test::ok;

namespace
{
std::string g_targetsText, g_engineText;

// THE NAME OF THE DEFAULTS → THE VERSION OF THEIR SOUND. A new set of sound numbers is a new name in engine.toml
// (`defaults`) and a new line here; an old line stays, for the projects that name it.
struct Golden
{
    std::string_view defaults;
    std::uint64_t sound;
};
constexpr Golden kGolden[] = {
    { "2026-09", 0x40f23a08b4c3e613ull },
};

// One target row, every field (owner decisions): the loudness and ceiling, mono bass 120 Hz (vinyl 150), the high-pass
// floor 24 Hz (vinyl 32) and slope 24 dB/oct (vinyl 12), the loss at the lowest note 1 dB (club 0.3), the delivery;
// vinyl alone without a peak clipper, with the high-pass always and a +0.5 dB low shelf; cd and cdDynamic alone with a
// pass at the source's rate; TD1008's −14 LUFS album loudness, desktop only.
struct Row
{
    std::string_view key;
    config::Group group;
    double lufs, tp, monoBass, hpfFloor;
    std::int32_t slope;
    double noteLossDb;
    std::int32_t sampleRate, bitDepth;
    bool noClipper, hpfAlways, sourceRatePass;
    double lowShelfDb;           // 0: none
    double albumLufs;            // 0: none; every album is desktop only
};
constexpr auto S = config::Group::Streaming;
constexpr auto D = config::Group::Delivery;
constexpr auto A = config::Group::Aggregator;
constexpr Row kRows[] = {
    //  key            group lufs   tp    mono  floor slope loss  rate   bits  noClip always srcPass shelf album
    { "allStreaming",  S,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "cdDynamic",     D,   -12,  -1,   120,  24,  24,   1,    44100, 16,  false, false, true,   0,    0 },
    { "club",          D,   -8,   -1,   120,  24,  24,   0.3,  0,     24,  false, false, false,  0,    0 },
    { "lp",            D,   -14,  -3,   150,  32,  12,   1,    0,     24,  true,  true,  false,  0.5,  0 },
    { "spotify",       S,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "spotifyLoud",   S,   -11,  -2,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "appleMusic",    S,   -16,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "youtube",       S,   -14,  -1,   120,  24,  24,   1,    48000, 24,  false, false, false,  0,    0 },
    { "youtubeMusic",  S,   -7,   -1,   120,  24,  24,   1,    48000, 24,  false, false, false,  0,    0 },
    { "amazon",        S,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "tidal",         S,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "deezer",        S,   -15,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "soundcloud",    S,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "td1008",        S,   -16,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    -14 },
    { "ebu",           S,   -23,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "distrokid",     A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "cdbaby",        A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "tunecore",      A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "amuse",         A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "feiyr",         A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "routenote",     A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "horusmusic",    A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "dittomusic",    A,   -14,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
    { "cd",            D,   -9,   -0.3, 120,  24,  24,   1,    44100, 16,  false, false, true,   0,    0 },
    { "bandcamp",      D,   -10,  -1,   120,  24,  24,   1,    0,     24,  false, false, false,  0,    0 },
};

// Every decision the config departs from, by name; empty when it holds them all.
std::vector<std::string> departures (const config::Config& c)
{
    std::vector<std::string> out;
    const auto need = [&] (bool held, const std::string& decision) { if (! held) out.push_back (decision); };
    const config::Targets& t = c.targets;
    const config::Engine& e = c.engine;

    // THE TARGET TABLE — every row, in this order, every field.
    need (t.targets.size() == std::size (kRows), "the target table has " + std::to_string (std::size (kRows)) + " rows");
    for (std::size_t i = 0; i < std::size (kRows) && i < t.targets.size(); ++i)
    {
        const Row& r = kRows[i];
        const config::Target& x = t.targets[i];
        const std::string at = "targets." + std::string (r.key);
        need (x.key == r.key, "row " + std::to_string (i) + " is " + std::string (r.key));
        need (x.group == r.group, at + ".group");
        need (same (x.lufs, r.lufs), at + ".lufs");
        need (same (x.tp, r.tp), at + ".tp");
        need (same (x.monoBass, r.monoBass), at + ".monoBass");
        need (same (x.hpfFloor, r.hpfFloor), at + ".hpfFloor");
        need (x.hpfSlopeDbPerOct == r.slope, at + ".hpfSlopeDbPerOct");
        need (same (x.noteLossDb, r.noteLossDb), at + ".noteLossDb");
        need (x.sampleRate == r.sampleRate, at + ".sampleRate");
        need (x.bitDepth == r.bitDepth, at + ".bitDepth");
        need (x.noClipper == r.noClipper, at + ".noClipper");
        need (x.hpfAlways == r.hpfAlways, at + ".hpfAlways");
        need (x.sourceRatePass == r.sourceRatePass, at + ".sourceRatePass");
        need (same (r.lowShelfDb, 0.0) ? ! x.lowShelfDb.has_value() : x.lowShelfDb.has_value() && same (*x.lowShelfDb, r.lowShelfDb),
              at + ".lowShelfDb");
        need (same (r.albumLufs, 0.0) ? ! x.album.has_value()
                                      : x.album.has_value() && same (x.album->lufs, r.albumLufs) && x.album->desktopOnly,
              at + ".album");
    }
    need (t.defaultTarget == "allStreaming", "a session starts on allStreaming");
    need (t.main == std::vector<std::string> { "allStreaming", "lp", "cdDynamic", "cd", "bandcamp", "club" }, "the main targets");

    // THE ENGINE'S DECIDED NUMBERS.
    need (same (e.input.referenceLufs, -18.0), "the input is brought to −18 LUFS before the chain");
    need (same (e.input.quietWarningLufs, -40.0) && same (e.input.quietGainOnlyLufs, -55.0),
          "a quiet input: a warning below −40 LUFS, gain and ceiling only below −55");
    need (e.landing.passes == std::vector<std::int32_t> { 12, 24, 32 }, "the landing: series of 12, 24 and 32 passes");
    need (same (e.hpf.hzMax, 50.0), "the high-pass tops out at 50 Hz, the machine's and the knob's");
    need (same (e.hpf.hzMin, 15.0), "the high-pass knob starts at 15 Hz");
    need (e.hpf.slopes == std::vector<std::int32_t> { 12, 24, 48 }, "the high-pass slopes are 12, 24 and 48 dB/oct");
    need (same (e.hpf.comfort.lowHz, 24.0) && same (e.hpf.comfort.highHz, 42.0), "the high-pass comfort window is 24–42 Hz");
    need (same (e.hpf.comfort.warningLowHz, 20.0) && same (e.hpf.comfort.warningHighHz, 50.0),
          "the high-pass field warns towards 20 and 50 Hz");
    need (same (e.hpf.nothingBelowNoteInfraLowBelow, 0.01), "no high-pass when the infra-low share is under 1 % (and no DC)");
    need (same (e.observations.wideBassSideFractionAtLeast, 0.06) && e.observations.kinds.wideBass == config::Kind::Warning,
          "wide bass: one threshold, 6 % of side, and it is a warning");
    need (same (e.lowEnd.run.crossoverHz, 120.0), "the low end is measured at 120 Hz");
    need (e.compressor.thresholdFrom == config::ThresholdFrom::ShortTermP95, "the compressor's threshold is from the short-term P95");
    need (same (e.compressor.limitRelease.min, 50.0), "the compressor's release floor is 50 ms");
    need (e.glue.byTarget.size() == 1 && e.glue.byTarget[0].target == "cd" && same (e.glue.byTarget[0].position, 0.7),
          "the machine glues on cd alone, at 0.7");
    need (same (e.glue.knobMinDb, 0.0) && same (e.glue.knobMaxDb, 3.0) && same (e.glue.knobStepDb, 0.1),
          "the glue knob runs 0…3 dB in steps of 0.1");
    need (same (e.glue.defaultPosition, 0.0), "no glue by default: a target without its own takes the compressor out");
    const config::MonoBass& m = e.monoBass;
    need (same (m.lowWidth, 0.0), "mono bass is full mono below its crossover");
    need (same (m.lowWidthRange.min, 0.0) && same (m.lowWidthRange.max, 1.0) && same (m.lowWidthStep, 0.05),
          "the mono-bass width knob runs 0…1 in steps of 0.05");
    need (same (m.frequencyRange.min, 60.0) && same (m.frequencyRange.max, 300.0) && same (m.frequencyStep, 1.0),
          "the mono-bass crossover knob runs 60…300 Hz in steps of 1");
    const config::PeakClipper& p = e.limiter.peakClipper;
    need (same (p.littleNeedDb, 3.0), "needles are not measured at a need of 3 dB or less");
    need (same (p.shortOverDb, 3.0) && same (p.betweenOverDb, 1.5), "the peak clipper: 3 dB above the ceiling, 1.5 between");
    need (same (p.shortPlrDb, 10.0) && same (p.longPlrDb, 8.0), "the peak clipper's PLR bounds: 10 and 8 dB");
    need (e.stages.limiter, "the limiter is always on");
    need (e.dither.onUpToBits == 16, "dither at 16 bits only");
    need (! e.deEsser.offered && ! e.deEsser.automatic && same (e.deEsser.manualDepthDb, -6.0),
          "the de-esser: not offered, manual, off, −6 dB");
    need (e.blindTest.listenedMinSwitches == 2, "a blind pair is listened from two switches");
    return out;
}

std::string joined (const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : "; ") + x;
    return s;
}

void theConfigHoldsTheDecisions()
{
    felitronics::test::group ("the config holds the owner's decisions, every target field by field");
    const config::Loaded loaded = config::load();
    ok (loaded.ok(), "PRECONDITION: the embedded config binds");
    const auto away = departures (loaded.config);
    ok (away.empty(), "no departure" + (away.empty() ? std::string{} : ": " + joined (away)));
}

void theSoundIsPinnedToTheDefaults()
{
    felitronics::test::group ("the version of the sound is pinned to the name of the defaults");
    const config::Loaded loaded = config::load();
    const config::Versions v = config::versions();
    char hex[24];
    std::snprintf (hex, sizeof hex, "%016llx", (unsigned long long) v.sound);
    const Golden* golden = nullptr;
    for (const auto& g : kGolden)
        if (g.defaults == loaded.config.engine.defaults) golden = &g;
    ok (golden != nullptr, "defaults = \"" + loaded.config.engine.defaults + "\" has a line in kGolden");
    ok (golden != nullptr && golden->sound == v.sound,
        std::string ("its sound version is ") + hex + " — a sound number changed without new defaults turns this red");
}

void aDepartureIsNamed()
{
    felitronics::test::group ("control: a departure the schema admits is named here");
    ok (departures (config::load().config).empty(), "PRECONDITION: the embedded config departs from nothing");
    struct Departure { bool inTargets; std::string_view from, to, decision; };
    const Departure plants[] = {
        { false, "hzMax = 50", "hzMax = 51", "the high-pass tops out at 50 Hz, the machine's and the knob's" },
        { false, "hzMax = 50", "hzMax = 60", "the high-pass tops out at 50 Hz, the machine's and the knob's" },
        { false, "slopes = [12, 24, 48]", "slopes = [12, 24, 36]", "the high-pass slopes are 12, 24 and 48 dB/oct" },
        { false, "passes = [12, 24, 32]", "passes = [12, 24, 40]", "the landing: series of 12, 24 and 32 passes" },
        { true, "noteLossDb = 0.3", "noteLossDb = 0.5", "targets.club.noteLossDb" },
        { true, "lufs = -7,", "lufs = -8,", "targets.youtubeMusic.lufs" },
        { true, "sampleRate = 48000, bitDepth = 24 }\n# YouTube Music", "sampleRate = 22050, bitDepth = 24 }\n# YouTube Music",
          "targets.youtube.sampleRate" },
        { false, "default = 0\nwhenTicked", "default = 0.3\nwhenTicked", "no glue by default: a target without its own takes the compressor out" },
        { false, "lowWidth = 0\n", "lowWidth = 0.2\n", "mono bass is full mono below its crossover" },
        { false, "hzMin = 15", "hzMin = 16", "the high-pass knob starts at 15 Hz" },
    };
    for (const auto& d : plants)
    {
        const Plant p = plant (d.inTargets ? g_targetsText : g_engineText, d.from, d.to, d.to);
        if (! p.planted) { ok (false, std::string (d.to) + ": the control has rotted"); continue; }
        const config::Loaded loaded = d.inTargets ? config::bind (p.text, g_engineText) : config::bind (g_targetsText, p.text);
        const auto away = departures (loaded.config);
        bool named = false;
        for (const auto& a : away) named = named || a == d.decision;
        ok (loaded.ok() && named, std::string (d.to) + ": the schema admits it, and this suite names \"" + std::string (d.decision)
                                      + "\"" + (named ? std::string{} : " — got: " + joined (away)));
    }
}
} // namespace

int main (int argc, char** argv)
{
    std::printf ("felitronics session::config decisions tests\n");
    if (argc != 3 || ! config::testing::readFile (argv[1], g_targetsText) || ! config::testing::readFile (argv[2], g_engineText))
    {
        std::fprintf (stderr, "usage: %s <targets.toml> <engine.toml> — the two source documents, readable\n", argv[0]);
        return 2;
    }
    theConfigHoldsTheDecisions();
    theSoundIsPinnedToTheDefaults();
    aDepartureIsNamed();
    return felitronics::test::report();
}
