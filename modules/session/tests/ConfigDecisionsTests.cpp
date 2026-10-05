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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace config = felitronics::session::config;
using config::Config;
using config::testing::plant;
using config::testing::Plant;
using config::testing::same;
using felitronics::test::ok;

namespace
{
std::string g_targetsText, g_engineText;

// THE NAME OF THE DEFAULTS → THE VERSION OF THEIR SOUND. A new set of sound numbers is a new name in engine.toml
// (`defaults`) and a new line here; the line of defaults a release carries stays, for the projects that name it (before
// the first release that carries them, a line may be updated in place).
struct Golden
{
    std::string_view defaults;
    std::uint64_t sound;
};
constexpr Golden kGolden[] = {
    { "2026-09", 0xf49360664b45a789ull },
    // the high-pass always, from 32 Hz and the sure lowest note; mono bass by its loss; the glue calibrated on the P95; the
    // chain's geometry, the dither's noise and the clipped-source bound stated; the lp row marked as cut to vinyl; the peak
    // clipper's numbers an amount off the peaks (3 dB short, 1.5 between), no longer a threshold above the ceiling
    // ...and the EQ bands' numbers ([bands], 01.10): numbers added, none changed — a person's only, at 0 dB no band, so
    // every 2026-10 project sounds as it did
    // ...and the saturation's type tape (owner, 01.10), and its output knob gone (v0.6.0; it was 0 dB, neutral), updated in
    // place: no 2026-10 project had been saved
    // ...and the high-pass's machine top its own key (machineTopHz = 50, owner, 01.10) apart from the knob's travel, now to
    // 80 Hz: no machine cutoff moves; updated in place, as above
    // ...and two knobs' steps (owner, 02.10): the mono-bass crossover by 5 Hz, the cut off the peaks by 0.1 dB. A step is
    // in the sound because a typed value is placed on its grid; no machine value moves; updated in place, as above
    // ...and the cut off the peaks' travel to 6 dB, the whole of its domain (owner, 02.10): a person's knob only, no machine
    // value moves; updated in place, as above
    // ...and felitronics-bands' bands.toml a document of the walk (v0.13.0): the filters of tilt, low and the five bands
    // (type, hz, q) left engine.toml for it with the same numbers — no master moves, the walk does; it was
    // 71b944c40d573c58; updated in place, as above
    // ...and three broadcast targets beside ebu (v0.13.0): atsc, arib, op59 — new rows, no existing target's master
    // moves; it was 485f7b6eeac75749; updated in place, as above
    // ...and loudness as a request (owner, 04.10, v0.14.0): the level landed on the source's gate and the limiter's
    // budget by the target's loudness ([landing] onSourceGate, limiterBudget) — the masters of a dynamic mix and of a
    // target the limiter cannot reach within its budget move; it was fb0cedc4f4311049; updated in place, as above
    // ...and the loud target's limiter budget 7.5 dB, was 10 (owner, 04.10, v0.14.1): a master louder than −8 LUFS that
    // its limiter would cut past 7.5 dB moves; it was 14193babae5aa94c; updated in place, as above
    // ...and the max modes (owner, 04.10, v0.15.0): two target rows appended and [landing.max] — numbers added, none
    // changed, so every target of manual loudness sounds as before; it was 406759a3436aec72; updated in place, as above
    // ...and the max modes by ear (owner, 04.10, v0.16.0): budgets 0.5 and 1.75 dB, the floor at −14 LUFS, the guard out
    // of the config, the rows' manual starts −13 and −11 — only a max master moves, every target of manual loudness sounds
    // as before; it was 15b627f7db3ad5dc; updated in place, as above
    // ...and the glue in parallel by default (owner, 05.10, v0.17.0: [glue] mix 0.4 on a knob by 0.2) — every master whose
    // glue engages moves (a target that glues, cd, or a person's glue), and [compressor] mix, which wrote the old 1, goes;
    // it was 7fb7cae3c8dc411b; updated in place, as above
    { "2026-10", 0xa154dca562da8da3ull },
};

// One target row, every field (owner decisions): the loudness and ceiling, mono bass 120 Hz (vinyl 150), the high-pass
// floor 32 Hz on every target (decision 3.3; the high-pass stands always) and slope 24 dB/oct (vinyl 12), the loss at the
// lowest note 1 dB (club 0.3), the delivery; vinyl alone without a peak clipper, marked as cut to a lathe (its tp the
// medium's ceiling) and with a +0.5 dB low shelf; cd and
// cdDynamic alone with a pass at the source's rate; TD1008's −14 LUFS album loudness, desktop only.
using felitronics::session::LoudnessMode;

struct Row
{
    std::string_view key;
    config::Group group;
    double lufs, tp, monoBass, hpfFloor;
    std::int32_t slope;
    double noteLossDb;
    std::int32_t sampleRate, bitDepth;
    bool noClipper, sourceRatePass;
    bool vinyl;             // cut to a lathe: the row's tp is the medium's ceiling
    double lowDb;           // 0: none
    double albumLufs;            // 0: none; every album is desktop only
    LoudnessMode mode = LoudnessMode::Manual;   // a max row's loudness mode
};
constexpr auto S = config::Group::Streaming;
constexpr auto D = config::Group::Delivery;
constexpr auto A = config::Group::Aggregator;
constexpr Row kRows[] = {
    //  key           group lufs  tp    mono  floor slope loss rate   bits noClip srcPass vinyl shelf album
    { "allStreaming",  S, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "cdDynamic",     D, -12,  -1,   120,  32,  24,  1,    44100, 16,  false, true,  false, 0,    0 },
    { "club",          D, -8,   -1,   120,  32,  24,  0.3,  0,     24,  false, false, false, 0,    0 },
    { "lp",            D, -14,  -3,   150,  32,  12,  1,    0,     24,  true,  false, true,  0.5,  0 },
    { "spotify",       S, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "spotifyLoud",   S, -11,  -2,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "appleMusic",    S, -16,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "youtube",       S, -14,  -1,   120,  32,  24,  1,    48000, 24,  false, false, false, 0,    0 },
    { "youtubeMusic",  S, -7,   -1,   120,  32,  24,  1,    48000, 24,  false, false, false, 0,    0 },
    { "amazon",        S, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "tidal",         S, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "deezer",        S, -15,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "soundcloud",    S, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "td1008",        S, -16,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    -14 },
    { "ebu",           S, -23,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "distrokid",     A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "cdbaby",        A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "tunecore",      A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "amuse",         A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "feiyr",         A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "routenote",     A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "horusmusic",    A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "dittomusic",    A, -14,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "cd",            D, -9,   -0.3, 120,  32,  24,  1,    44100, 16,  false, true,  false, 0,    0 },
    { "bandcamp",      D, -10,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "atsc",          S, -24,  -2,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "arib",          S, -24,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    { "op59",          S, -24,  -2,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0 },
    // the max modes as targets (owner, 04.10, v0.15.0): allStreaming's medium; lufs is where the manual mode starts
    { "maxClean",      S, -13,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0, LoudnessMode::MaxClean },
    { "maxDense",      S, -11,  -1,   120,  32,  24,  1,    0,     24,  false, false, false, 0,    0, LoudnessMode::MaxDense },
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
        need (x.sourceRatePass == r.sourceRatePass, at + ".sourceRatePass");
        need (x.vinyl == r.vinyl, at + ".vinyl");
        need (x.loudnessMode == r.mode, at + ".loudnessMode");
        need (same (r.lowDb, 0.0) ? ! x.lowDb.has_value() : x.lowDb.has_value() && same (*x.lowDb, r.lowDb),
              at + ".lowDb");
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
    need (e.landing.passes == 12, "the landing: one budget of 12 passes");
    need (e.landing.truePeakAimDb == 0.05 && e.limiter.ceilingMarginDb == 0.15,
          "the true-peak aim and initial limiter margin are separate decisions");
    need (e.landing.onSourceGate, "the landing lands the level on the source's gate (owner, 04.10)");
    need (same (e.landing.cleanBudgetDb, 0.5),
          "max clean: the limiter's budget 0.5 dB, about −13 LUFS, a little above streaming (owner, 04.10, by ear)");
    need (same (e.landing.denseBudgetDb, 1.75), "max dense: the limiter's budget 1.75 dB, about −11 LUFS (owner, 04.10, by ear)");
    need (same (e.landing.maxCeilingLufs, -5.0), "the max modes search up to −5 LUFS");
    need (same (e.landing.maxFloorLufs, -14.0), "a max master never lands under −14 LUFS, whichever target (owner, 04.10)");
    need (same (e.landing.quietBudgetDb, 4.0) && same (e.landing.middleBudgetDb, 7.0)
          && same (e.landing.loudBudgetDb, 7.5)
          && same (e.landing.middleLufs.min, -10.0) && same (e.landing.middleLufs.max, -8.0),
          "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)");
    need (same (e.hpf.machineTopHz, 50.0), "the machine's high-pass tops out at 50 Hz");
    need (same (e.hpf.hzMax, 80.0), "a person's high-pass knob travels to 80 Hz (owner, 01.10)");
    need (same (e.hpf.hzMin, 15.0), "the high-pass knob starts at 15 Hz");
    need (e.hpf.slopes == std::vector<std::int32_t> { 12, 24, 48 }, "the high-pass slopes are 12, 24 and 48 dB/oct");
    need (same (e.hpf.comfort.lowHz, 24.0) && same (e.hpf.comfort.highHz, 42.0), "the high-pass comfort window is 24–42 Hz");
    need (same (e.hpf.comfort.warningLowHz, 20.0) && same (e.hpf.comfort.warningHighHz, 50.0),
          "the high-pass field warns towards 20 and 50 Hz");
    need (same (e.hpf.noteAboveHz, 20.0) && same (e.hpf.noteSoundingAtLeastS, 3.0) && same (e.input.shortSeconds, 10.0)
          && same (e.lowEnd.occupiedFromDuty, 0.10) && same (e.lowEnd.occupiedMarginWhenOnDb, 2.0),
          "a sure lowest note: 2 dB over the occupancy line, 10 % of the frames, above 20 Hz, 3 s in all; not sought under 10 s");
    need (same (e.monoBass.lossWarnFromDb, 1.0) && same (e.monoBass.lossOffAboveDb, 3.0),
          "mono bass by its loss: placed under 1 dB, with a warning from 1 to 3 dB, left out above 3 dB");
    need (! e.stages.monoBass, "mono bass is placed by its weighed loss, never before it");
    need (same (e.observations.wideBassSideFractionAtLeast, 0.06) && e.observations.kinds.wideBass == config::Kind::Warning,
          "wide bass: one threshold, 6 % of side, and it is a warning");
    need (same (e.lowEnd.run.crossoverHz, 120.0), "the low end is measured at 120 Hz");
    need (e.compressor.thresholdFrom == config::ThresholdFrom::ShortTermP95, "the compressor's threshold is from the short-term P95");
    need (same (e.compressor.limitRelease.min, 50.0), "the compressor's release floor is 50 ms");
    need (e.glue.byTarget.size() == 1 && e.glue.byTarget[0].target == "cd" && same (e.glue.byTarget[0].upToDb, 2.6),
          "the machine glues on cd alone, up to 2.6 dB");
    need (same (e.glue.knobMinDb, 0.0) && same (e.glue.knobMaxDb, 3.0) && same (e.glue.knobStepDb, 0.1),
          "the glue knob runs 0…3 dB in steps of 0.1");
    need (same (e.glue.defaultUpToDb, 0.0), "no glue by default: a target without its own takes the compressor out");
    need (same (e.glue.whenTickedUpToDb, 0.5), "ticked on untouched, the glue is up to 0.5 dB");
    need (same (e.glue.mix, 0.4) && same (e.glue.mixStep, 0.2) && same (e.glue.mixRange.min, 0.0) && same (e.glue.mixRange.max, 1.0)
          && same (e.glue.mixDomain.min, 0.0) && same (e.glue.mixDomain.max, 1.0),
          "the glue in parallel by default: mix 40 %, a knob 0…100 % by 20 % (owner, 05.10)");
    need (same (e.glue.detectorOverP95Db, 1.5), "the glue's threshold is calibrated 1.5 dB over the P95: the knob is the reduction the loud places really get");
    const config::MonoBass& m = e.monoBass;
    need (same (m.lowWidth, 0.0), "mono bass is full mono below its crossover");
    need (same (m.lowWidthRange.min, 0.0) && same (m.lowWidthRange.max, 1.0) && same (m.lowWidthStep, 0.05),
          "the mono-bass width knob runs 0…1 in steps of 0.05");
    need (same (m.frequencyRange.min, 60.0) && same (m.frequencyRange.max, 300.0) && same (m.frequencyStep, 5.0),
          "the mono-bass crossover knob runs 60…300 Hz in steps of 5 (owner, 02.10)");
    const auto& b = e.bands;
    need (same (b.body.normal.min, -1.5) && same (b.body.normal.max, 1.5) && same (b.forward.normal.min, -1.5) && same (b.forward.normal.max, 1.5)
          && same (b.brightness.normal.min, -1.5) && same (b.brightness.normal.max, 1.5) && same (b.air.normal.min, -1.5) && same (b.air.normal.max, 1.5)
          && same (b.mud.normal.min, -1.5) && same (b.mud.normal.max, 0.0),
          "the EQ bands turn red past ±1.5 dB, as tilt and low (owner, 02.10); the mud band, a cut alone, below −1.5");
    const config::PeakClipper& p = e.limiter.peakClipper;
    need (same (p.littleNeedDb, 3.0), "needles are not measured at a need of 3 dB or less");
    need (same (p.shortCutDb, 3.0) && same (p.betweenCutDb, 1.5), "the peak clipper: up to 3 dB off the peaks short, 1.5 between");
    need (same (p.shortPlrDb, 10.0) && same (p.longPlrDb, 8.0), "the peak clipper's PLR bounds: 10 and 8 dB");
    need (same (p.shortP90Ms, 2.0) && same (p.longP90Ms, 8.0) && same (p.shortBassShare, 0.25) && same (p.longBassShare, 0.5),
          "the needles' classes: short to 2 ms and a bass share of 0.25, long from 8 ms or 0.5");
    need (same (p.clippedPerMinute, 10.0), "a source is clipped from 10 confirmed clips a minute");
    need (same (p.kneeDb, 0.0), "the peak clipper is a hard clip");
    need (same (p.manualMinDb, 0.0) && same (p.manualMaxDb, 6.0) && same (p.manualStepDb, 0.1),
          "the manual cut off the peaks runs 0…6 dB in steps of 0.1 (owner, 02.10)");
    need (e.dither.seed == 0x853c49e6748fea9bull && e.dither.autoBlank && e.dither.autoBlankSamples == 4096
          && e.dither.shaping == config::NoiseShaping::Weighted,
          "the dither: weighted TPDF from the fixed seed, blanked after 4096 zero samples");
    need (same (e.observations.vinylTopAboveHz, 16000.0), "the vinyl note about the top: above 16 kHz");
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
    const config::Loaded loaded = Config::load();
    ok (loaded.ok(), "PRECONDITION: the embedded config binds");
    const auto away = departures (loaded.config);
    ok (away.empty(), "no departure" + (away.empty() ? std::string{} : ": " + joined (away)));
}

// THE LIMITER'S BUDGET IN dB WITH A FRACTION: the schema takes 1 to 60 dB, a whole number or not, and refuses the rest.
void theBudgetIsFractional()
{
    felitronics::test::group ("the limiter's budget is a number of dB on a quarter-dB step, from 1 to 60");
    const auto bound = [] (std::string_view from, std::string_view to)
    {
        const Plant p = plant (g_engineText, from, to, to);
        return p.planted ? std::optional<bool> (Config::bind (g_targetsText, p.text).ok()) : std::nullopt;
    };
    const auto shipped = Config::load();
    ok (shipped.ok() && same (shipped.config.engine.landing.loudBudgetDb, 7.5), "the shipped 7.5 dB is taken");
    ok (bound ("middleDb = 7,", "middleDb = 6.25,") == std::optional<bool> (true)
        && bound ("loudDb = 7.5,", "loudDb = 7.25,") == std::optional<bool> (true), "6.25 and 7.25 dB are taken");
    ok (bound ("loudDb = 7.5,", "loudDb = 7.3,") == std::optional<bool> (false)
        && bound ("middleDb = 7,", "middleDb = 7.125,") == std::optional<bool> (false),
        "7.3 and 7.125 dB are refused: the budget is a whole number of quarter dB, the landing's own resolution");
    ok (bound ("quietDb = 4,", "quietDb = 0.5,") == std::optional<bool> (false), "0.5 dB is refused: under 1");
    ok (bound ("loudDb = 7.5,", "loudDb = 61,") == std::optional<bool> (false), "61 dB is refused: over 60");
    ok (bound ("middleDb = 7,", "middleDb = 3.5,") == std::optional<bool> (false), "a middle budget under the quiet one is refused");
}

void theSoundIsPinnedToTheDefaults()
{
    felitronics::test::group ("the version of the sound is pinned to the name of the defaults");
    const config::Loaded loaded = Config::load();
    const config::Versions v = Config::versions();
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
    ok (departures (Config::load().config).empty(), "PRECONDITION: the embedded config departs from nothing");
    struct Departure { bool inTargets; std::string_view from, to, decision; };
    const Departure plants[] = {
        { false, "machineTopHz = 50", "machineTopHz = 51", "the machine's high-pass tops out at 50 Hz" },
        { false, "machineTopHz = 50", "machineTopHz = 80", "the machine's high-pass tops out at 50 Hz" },
        { false, "hzMax = 80", "hzMax = 50", "a person's high-pass knob travels to 80 Hz (owner, 01.10)" },
        { false, "hzMax = 80", "hzMax = 81", "a person's high-pass knob travels to 80 Hz (owner, 01.10)" },
        { false, "slopes = [12, 24, 48]", "slopes = [12, 24, 36]", "the high-pass slopes are 12, 24 and 48 dB/oct" },
        { false, "passes = 12", "passes = 11", "the landing: one budget of 12 passes" },
        { false, "onSourceGate = true", "onSourceGate = false", "the landing lands the level on the source's gate (owner, 04.10)" },
        { false, "clean = { budgetDb = 0.5 }", "clean = { budgetDb = 0.75 }",
          "max clean: the limiter's budget 0.5 dB, about −13 LUFS, a little above streaming (owner, 04.10, by ear)" },
        { false, "dense = { budgetDb = 1.75 }", "dense = { budgetDb = 3 }",
          "max dense: the limiter's budget 1.75 dB, about −11 LUFS (owner, 04.10, by ear)" },
        { false, "ceilingLufs = -5", "ceilingLufs = -6", "the max modes search up to −5 LUFS" },
        { false, "floorLufs = -14", "floorLufs = -13", "a max master never lands under −14 LUFS, whichever target (owner, 04.10)" },
        { true, "loudnessMode = \"maxDense\" }", "loudnessMode = \"maxClean\" }", "targets.maxDense.loudnessMode" },
        { false, "quietDb = 4,", "quietDb = 5,", "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { false, "loudDb = 7.5,", "loudDb = 10,", "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { false, "loudDb = 7.5,", "loudDb = 7.75,", "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { false, "middleLufs = [-10, -8]", "middleLufs = [-11, -8]",
          "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { true, "noteLossDb = 0.3", "noteLossDb = 0.5", "targets.club.noteLossDb" },
        { true, "lufs = -7,", "lufs = -8,", "targets.youtubeMusic.lufs" },
        { true, "hpfFloor = 32, hpfSlopeDbPerOct = 24, noteLossDb = 0.3", "hpfFloor = 24, hpfSlopeDbPerOct = 24, noteLossDb = 0.3",
          "targets.club.hpfFloor" },
        { false, "note = { aboveHz = 20, soundingAtLeastS = 3 }", "note = { aboveHz = 20, soundingAtLeastS = 2 }",
          "a sure lowest note: 2 dB over the occupancy line, 10 % of the frames, above 20 Hz, 3 s in all; not sought under 10 s" },
        { false, "offAboveDb = 3,", "offAboveDb = 4,", "mono bass by its loss: placed under 1 dB, with a warning from 1 to 3 dB, left out above 3 dB" },
        { false, "eq = true\nmonoBass = false", "eq = true\nmonoBass = true", "mono bass is placed by its weighed loss, never before it" },
        { true, "sampleRate = 48000, bitDepth = 24 }\n# YouTube Music", "sampleRate = 22050, bitDepth = 24 }\n# YouTube Music",
          "targets.youtube.sampleRate" },
        { false, "default = 0\nwhenTicked", "default = 0.3\nwhenTicked", "no glue by default: a target without its own takes the compressor out" },
        { false, "whenTicked = 0.5", "whenTicked = 0.6", "ticked on untouched, the glue is up to 0.5 dB" },
        { false, "mix = 0.4\nmixRange", "mix = 1\nmixRange", "the glue in parallel by default: mix 40 %, a knob 0…100 % by 20 % (owner, 05.10)" },
        { false, "mixStep = 0.2", "mixStep = 0.05", "the glue in parallel by default: mix 40 %, a knob 0…100 % by 20 % (owner, 05.10)" },
        { false, "byTarget = { cd = 2.6 }", "byTarget = { cd = 2.5 }", "the machine glues on cd alone, up to 2.6 dB" },
        { false, "lowWidth = 0\n", "lowWidth = 0.2\n", "mono bass is full mono below its crossover" },
        { false, "hzMin = 15", "hzMin = 16", "the high-pass knob starts at 15 Hz" },
    };
    for (const auto& d : plants)
    {
        const Plant p = plant (d.inTargets ? g_targetsText : g_engineText, d.from, d.to, d.to);
        if (! p.planted) { ok (false, std::string (d.to) + ": the control has rotted"); continue; }
        const config::Loaded loaded = d.inTargets ? Config::bind (p.text, g_engineText) : Config::bind (g_targetsText, p.text);
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
    theBudgetIsFractional();
    theSoundIsPinnedToTheDefaults();
    aDepartureIsNamed();
    return felitronics::test::report();
}
