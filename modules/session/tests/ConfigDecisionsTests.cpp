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
    // glue engages moves (a target that glues, cd, or a person's glue); it was 7fb7cae3c8dc411b; updated in place, as above
    // ...and the unread [compressor] mix removed (owner, 05.10): the walk moves, no master does; it was
    // 02ed0efc37c02b67; updated in place, as above
    // ...and the manual limiter wall (owner, 05.10): slope 0.2, P95 spacing 0.5 dB; masters that meet the wall move.
    // It was a154dca562da8da3; updated in place, as above.
    // ...and every target's mastered-delivery class plus the two configured peak ceilings (owner, 05.10, v0.18.0):
    // already-mastered streaming/other deliveries may now bypass the normal chain; specification targets do not.
    // It was 3ca74d45e21253d9; updated in place before the release.
    // ...and the low end measured from 10 Hz, the lowest note sought from 25 Hz (owner, 06.10: [lowEnd.run] lowNoteHz 10,
    // [lowEnd] lowestNoteFromHz 25): a master whose lowest band on lay between 20 and 25 Hz takes its note from the bands
    // above; it was 086fda481af08315; updated in place, as above (no 2026-10 project is saved).
    // ...and every manual knob stepless (owner, 07.10: step 0, a typed value kept as typed — no machine value moves), the
    // low-end table to 500 Hz with the note's readings kept to 300 Hz (noteTopHz, 07.10: the drawings), and [lowEnd]
    // occupiedAboveBackgroundDb 6 (07.10: a mix with no bass is unsure); it was e16b73aead43a728; updated in place, as above.
    // ...and [lowEnd] noteRangeShareAtLeastDb −140 (07.10, the same decision: a float tone's rounding lines are no note); it
    // was 4fab20b29ed728bb; updated in place, as above.
    // ...and [lowEnd] occupiedAboveBackgroundDb removed (owner, 07.10: real mixes keep their notes): the lowest band is a
    // note only where the band under it was never on — a master whose lowest note the background's veto took has it again;
    // it was 702dd4d0cbf8d059; updated in place, as above.
    // ...and [hpf] note.aboveHz removed (owner, 07.10): it was 20 Hz, under the 25 Hz the note is sought from, so no note
    // and no master moves; it was 298d807f53e7f865; updated in place, as above.
    // ...and [lowEnd.run] fftOrderUpToHz 48000 (owner, 07.10: 96 kHz resolves as 48 kHz): the low-end order rises above
    // 48 kHz, so a master over 48 kHz whose bass was unresolved takes its note; nothing at 48 kHz and under moves; it was
    // 5c869a1028b4a6fd; updated in place, as above.
    // ...and [lowEnd] lowestNoteFromHz 30, was 25 (owner, 07.10: the bass's fifth string, B0 30.87 Hz, the limit for
    // everything): a master whose lowest band on lay between 25 and 30 Hz takes its note from the bands above; it was
    // 294f80d8221bb306; updated in place, as above.
    // ...and the two diodes by hand (owner, 07.10): [saturation] bias 0.2 and dcBlockHz 10, written for asym alone — no
    // existing master moves (every other type keeps bias 0 and no blocker); it was a38dce3eb12a1f77; updated in place, as above.
    // ...and the glue's five and the limiter's three by hand with their domains and comfort windows, the third max mode
    // (a target row with its high-pass floor at the machine's top, 50 Hz, its 3 dB budget, its expected passes) and maxClean
    // the target a new session starts on (owner, 07.10 and 08.10): numbers added, none of an existing target's changed —
    // measured, every one of the 30 targets' untouched master of Cold Gaze of Eternity and of Cat in Space is byte for byte
    // v0.20.0's (PCM and WAV, native), so a v0.20.0 project keeps its sound and its import; it was 06ea4bbff433f2e6; updated
    // in place, as above.
    // ...and a person's tick (owner, 08.10): [glue] whenTicked 2.6 with its ticked character (ratio 2, knee 6 dB, attack
    // 30 ms, release 300 ms) and [saturation] whenTicked 6 — a master a person ticked a glue or a saturation on for sounds
    // otherwise; no untouched master moves (a tick is a person's: no target's untouched master has one); it was
    // 3299e8a01bc682c9; updated in place, as above.
    // ...and the waterfall and the fourth max mode (09.10): [saturation] steerDriveMaxDb 10 (the drive the steering may
    // raise), [limiter.peakClipper] place both (the start clipper beside the limiter's), a max mode's cleaner (true when
    // absent, written by none) and Maximum · nuke (a target row appended, its 7 dB budget, its expected passes) — numbers
    // added, none of an existing target's changed: each moves a master only where a person asks a share or picks nuke,
    // so no untouched master moves (the WAV contract's recording holds); it was 2b0de0cc2cc8ac7b; updated in place, as above.
    { "2026-10", 0xbe07aaa73e1c26bfull },
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
    // the third (owner, 07.10): as loud as a limiter budget of 3 dB allows, the high-pass from 50 Hz (08.10), mono bass to 150 Hz
    { "maxExtreme",    S, -9,   -1,   150,  50,  24,  1,    0,     24,  false, false, false, 0,    0, LoudnessMode::MaxExtreme },
    // the fourth: extreme's row with a limiter budget of 5 dB; the manual mode starts at −7
    { "maxNuke",       S, -7,   -1,   150,  50,  24,  1,    0,     24,  false, false, false, 0,    0, LoudnessMode::MaxNuke },
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
        const auto expectedClass = r.key == "ebu" || r.key == "atsc" || r.key == "arib" || r.key == "op59"
            ? config::TargetClass::Specification
            : r.group == config::Group::Delivery ? config::TargetClass::Other : config::TargetClass::Streaming;
        need (x.targetClass == expectedClass, at + ".class");
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
    need (t.defaultTarget == "maxClean", "a session starts on Maximum · clean (owner, 07.10)");
    need (t.main == std::vector<std::string> { "allStreaming", "lp", "cdDynamic", "cd", "bandcamp", "club" }, "the main targets");

    // THE ENGINE'S DECIDED NUMBERS.
    need (same (e.input.referenceLufs, -18.0), "the input is brought to −18 LUFS before the chain");
    need (same (e.input.quietWarningLufs, -40.0) && same (e.input.quietGainOnlyLufs, -55.0),
          "a quiet input: a warning below −40 LUFS, gain and ceiling only below −55");
    need (e.landing.passes == 12, "the landing: one budget of 12 passes");
    need (e.progress.masterExpectedPasses == 3 && e.progress.masterExpectedPassesMaxClean == 7
          && e.progress.masterExpectedPassesMaxDense == 6,
          "the bar expects 3 renders of a manual master, 7 of max clean, 6 of max dense (owner, 06.10)");
    need (e.landing.truePeakAimDb == 0.05 && e.limiter.ceilingMarginDb == 0.15,
          "the true-peak aim and initial limiter margin are separate decisions");
    need (e.landing.onSourceGate, "the landing lands the level on the source's gate (owner, 04.10)");
    need (same (e.observations.masteredAboveLufs, -12) && same (e.observations.masteredPeakAboveDbTp, -1.5)
          && same (e.observations.masteredPlrBelowDb, 11),
          "already mastered requires louder than −12 LUFS, true peak above −1.5 dBTP and PLR below 11 (owner, 05.10)");
    need (same (e.masteredDelivery.loudAboveLufs, -14) && same (e.masteredDelivery.loudCeilingDbTp, -2)
          && same (e.masteredDelivery.regularCeilingDbTp, -1),
          "an already-mastered delivery uses −2 dBTP above −14 LUFS and −1 dBTP otherwise (owner, 05.10)");
    need (same (e.landing.limiterSlopeBelow, 0.2) && same (e.landing.limiterSlopeSpacingDb, 0.5),
          "manual landings stop below 0.2 LU per dB of P95 cut, spaced by at least 0.5 dB (owner, 05.10)");
    need (same (e.landing.cleanBudgetDb, 0.5),
          "max clean: the limiter's budget 0.5 dB, about −13 LUFS, a little above streaming (owner, 04.10, by ear)");
    need (same (e.landing.denseBudgetDb, 1.75), "max dense: the limiter's budget 1.75 dB, about −11 LUFS (owner, 04.10, by ear)");
    need (same (e.landing.maxCeilingLufs, -5.0), "the max modes search up to −5 LUFS");
    need (same (e.landing.maxFloorLufs, -14.0), "a max master never lands under −14 LUFS, whichever target (owner, 04.10)");
    need (same (e.landing.maxBudgetResolutionDb, 0.25),
          "the limiter-budget edge is proved to 0.25 dB unless a master asks for 0.05…1 dB");
    need (same (e.landing.quietBudgetDb, 4.0) && same (e.landing.middleBudgetDb, 7.0)
          && same (e.landing.loudBudgetDb, 7.5)
          && same (e.landing.middleLufs.min, -10.0) && same (e.landing.middleLufs.max, -8.0),
          "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)");
    need (same (e.hpf.machineTopHz, 50.0), "the machine's high-pass tops out at 50 Hz");
    need (same (e.hpf.hzMax, 80.0), "a person's high-pass knob travels to 80 Hz (owner, 01.10)");
    need (same (e.hpf.hzMin, 15.0), "the high-pass knob starts at 15 Hz");
    need (e.hpf.slopes == std::vector<std::int32_t> { 12, 24, 48 }, "the high-pass slopes are 12, 24 and 48 dB/oct");
    {
        const std::pair<std::string_view, double> marks[] = { { "kick", 50.0 }, { "bass4", 41.2 }, { "bass5", 30.87 },
                                                              { "piano", 27.5 }, { "guitar", 82.41 }, { "speech", 70.0 } };
        bool same6 = e.hpf.marks.size() == std::size (marks);
        for (std::size_t i = 0; same6 && i < std::size (marks); ++i)
            same6 = e.hpf.marks[i].key == marks[i].first && same (e.hpf.marks[i].hz, marks[i].second);
        need (same6, "the high-pass curve's marks: kick 50, E1 41.2, B0 30.87, the piano's A0 27.5, the guitar's E2 82.41 and "
                     "speech 70 Hz — no 808 (owner, 06.10)");
    }
    need (same (e.hpf.comfort.lowHz, 30.0) && same (e.hpf.comfort.highHz, 42.0), "the high-pass comfort window is 30–42 Hz (owner, 06.10)");
    need (same (e.hpf.comfort.warningLowHz, 26.0) && same (e.hpf.comfort.warningHighHz, 50.0),
          "the high-pass field warns towards 26 and 50 Hz (owner, 06.10: yellow from 30 down, the ramp as long as before)");
    need (same (e.hpf.noteSoundingAtLeastS, 3.0) && same (e.input.shortSeconds, 10.0)
          && same (e.lowEnd.occupiedFromDuty, 0.10) && same (e.lowEnd.occupiedMarginWhenOnDb, 2.0),
          "a sure lowest note: 2 dB over the occupancy line, 10 % of the frames, 3 s in all; not sought under 10 s");
    need (same (e.monoBass.lossWarnFromDb, 1.0) && same (e.monoBass.lossOffAboveDb, 3.0),
          "mono bass by its loss: placed under 1 dB, with a warning from 1 to 3 dB, left out above 3 dB");
    need (! e.stages.monoBass, "mono bass is placed by its weighed loss, never before it");
    need (same (e.observations.wideBassSideFractionAtLeast, 0.06) && e.observations.kinds.wideBass == config::Kind::Warning,
          "wide bass: one threshold, 6 % of side, and it is a warning");
    need (same (e.lowEnd.run.crossoverHz, 120.0), "the low end is measured at 120 Hz");
    need (same (e.lowEnd.run.lowNoteHz, 10.0) && same (e.lowEnd.run.highNoteHz, 500.0) && same (e.lowEnd.run.noteTopHz, 300.0)
          && same (e.lowEnd.lowestNoteFromHz, 30.0),
          "the low end's table measures from 10 Hz to 500 Hz, the note's readings to 300 Hz, and the lowest note is sought "
          "from 30 Hz, B0 30.87 Hz, a five-string's lowest (owner, 06.10 and 07.10)");
    need (e.lowEnd.run.fftOrder == 17 && same (e.lowEnd.run.fftOrderUpToHz, 48000.0),
          "the low end resolves at every rate as at 48 kHz: fftOrder 17 up to 48 kHz, one more per doubling of the rate above it (owner, 07.10)");
    need (same (e.saturation.bias, 0.2) && same (e.saturation.dcBlockHz, 10.0), "the asymmetric diode by hand: bias 0.2, its DC blocker at 10 Hz, written for it alone (owner, 07.10)");
    need (same (e.lowEnd.noteRangeShareAtLeastDb, -140.0),
          "a mix with no bass is unsure: the note range holds −140 dB of the programme, or there is no note (owner, 07.10)");
    need (e.compressor.thresholdFrom == config::ThresholdFrom::ShortTermP95, "the compressor's threshold is from the short-term P95");
    need (same (e.compressor.limitRelease.min, 50.0), "the compressor's release floor is 50 ms");
    need (e.glue.byTarget.size() == 1 && e.glue.byTarget[0].target == "cd" && same (e.glue.byTarget[0].upToDb, 2.6),
          "the machine glues on cd alone, up to 2.6 dB");
    need (same (e.glue.knobMinDb, 0.0) && same (e.glue.knobMaxDb, 3.0) && same (e.glue.knobStepDb, 0.0),
          "the glue knob runs 0…3 dB, stepless (owner, 07.10)");
    need (same (e.glue.defaultUpToDb, 0.0), "no glue by default: a target without its own takes the compressor out");
    need (same (e.glue.whenTickedUpToDb, 2.6), "ticked on untouched, the glue is up to 2.6 dB (owner, 08.10)");
    need (same (e.glue.ticked.ratio, 2.0) && same (e.glue.ticked.kneeDb, 6.0) && same (e.glue.ticked.attackMs, 30.0)
          && same (e.glue.ticked.releaseMs, 300.0),
          "a tick on an untouched glue gives it ratio 2, knee 6 dB, attack 30 ms and release 300 ms — glue, not a compressor (owner, 08.10)");
    need (same (e.saturation.whenTickedDb, 6.0), "ticked on untouched, the saturation drives 6 dB (owner, 08.10)");
    need (same (e.glue.mix, 0.4) && same (e.glue.mixStep, 0.0) && same (e.glue.mixRange.min, 0.0) && same (e.glue.mixRange.max, 1.0)
          && same (e.glue.mixDomain.min, 0.0) && same (e.glue.mixDomain.max, 1.0),
          "the glue in parallel by default: mix 40 %, a knob 0…100 %, stepless (owner, 05.10 and 07.10)");
    need (same (e.glue.detectorOverP95Db, 1.5), "the glue's threshold is calibrated 1.5 dB over the P95: the knob is the reduction the loud places really get");
    const config::MonoBass& m = e.monoBass;
    need (same (m.lowWidth, 0.0), "mono bass is full mono below its crossover");
    need (same (m.lowWidthRange.min, 0.0) && same (m.lowWidthRange.max, 1.0) && same (m.lowWidthStep, 0.05),
          "the mono-bass width knob runs 0…1 in steps of 0.05");
    need (same (m.frequencyRange.min, 60.0) && same (m.frequencyRange.max, 300.0) && same (m.frequencyStep, 0.0),
          "the mono-bass crossover knob runs 60…300 Hz, stepless (owner, 07.10)");
    // THE COMFORT WINDOWS (owner, 07.10): low, high, warningLow, warningHigh — where a knob's field is neutral, out to red.
    const auto window = [] (double low, double high, double warningLow, double warningHigh, double l, double h, double wl, double wh)
    { return same (low, l) && same (high, h) && same (warningLow, wl) && same (warningHigh, wh); };
    need (window (m.comfort.lowHz, m.comfort.highHz, m.comfort.warningLowHz, m.comfort.warningHighHz, 100, 180, 75, 250)
          && window (e.glue.comfort.low, e.glue.comfort.high, e.glue.comfort.warningLow, e.glue.comfort.warningHigh, 0, 1.5, 0, 6)
          && window (e.glue.mixComfort.low, e.glue.mixComfort.high, e.glue.mixComfort.warningLow, e.glue.mixComfort.warningHigh, 0.3, 1, 0, 1)
          && window (e.saturation.driveComfort.low, e.saturation.driveComfort.high, e.saturation.driveComfort.warningLow,
                     e.saturation.driveComfort.warningHigh, 0, 1.5, 0, 8),
          "the comfort windows: mono bass's crossover 100–180 Hz, red at 75 and 250; the glue 0–1.5 dB, red at 6; its mix "
          "30–100 %, red at 0; the saturation's drive 0–1.5 dB, red at 8 (owner, 07.10)");
    // EVERY OTHER MANUAL KNOB IS STEPLESS (owner, 06.10 and 07.10: «все ручки недискретные»): step 0 keeps a typed value.
    need (same (e.hpf.hzStep, 0.0) && same (e.tilt.step, 0.0) && same (e.low.step, 0.0) && same (e.bands.body.step, 0.0)
          && same (e.bands.mud.step, 0.0) && same (e.bands.forward.step, 0.0) && same (e.bands.brightness.step, 0.0)
          && same (e.bands.air.step, 0.0) && same (e.saturation.driveStep, 0.0) && same (c.targets.editLufs.step, 0.0)
          && same (c.targets.editTp.step, 0.0),
          "stepless knobs: the high-pass cutoff, tilt, low, the five EQ bands, the saturation's drive, the target's loudness "
          "and ceiling (owner, 06.10 and 07.10)");
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
    need (same (p.manualMinDb, 0.0) && same (p.manualMaxDb, 6.0) && same (p.manualStepDb, 0.0),
          "the manual cut off the peaks runs 0…6 dB, stepless (owner, 02.10 and 07.10)");
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
        { true, "allStreaming = { group = \"streaming\", class = \"streaming\"", "allStreaming = { group = \"streaming\", class = \"other\"", "targets.allStreaming.class" },
        { true, "class = \"specification\", lufs = -23", "class = \"streaming\", lufs = -23", "targets.ebu.class" },
        { false, "loudAboveLufs = -14", "loudAboveLufs = -13",
          "an already-mastered delivery uses −2 dBTP above −14 LUFS and −1 dBTP otherwise (owner, 05.10)" },
        { false, "loudCeilingDbTp = -2", "loudCeilingDbTp = -3",
          "an already-mastered delivery uses −2 dBTP above −14 LUFS and −1 dBTP otherwise (owner, 05.10)" },
        { false, "regularCeilingDbTp = -1", "regularCeilingDbTp = -2",
          "an already-mastered delivery uses −2 dBTP above −14 LUFS and −1 dBTP otherwise (owner, 05.10)" },
        { false, "machineTopHz = 50", "machineTopHz = 51", "the machine's high-pass tops out at 50 Hz" },
        { false, "{ key = \"piano\", hz = 27.5 }", "{ key = \"sub808\", hz = 28 }",
          "the high-pass curve's marks: kick 50, E1 41.2, B0 30.87, the piano's A0 27.5, the guitar's E2 82.41 and speech 70 Hz — no 808 (owner, 06.10)" },
        { false, "{ key = \"speech\", hz = 70 }", "{ key = \"speech\", hz = 85 }",
          "the high-pass curve's marks: kick 50, E1 41.2, B0 30.87, the piano's A0 27.5, the guitar's E2 82.41 and speech 70 Hz — no 808 (owner, 06.10)" },
        { false, "comfort = { lowHz = 30,", "comfort = { lowHz = 27,", "the high-pass comfort window is 30–42 Hz (owner, 06.10)" },
        { false, "warningLowHz = 26,", "warningLowHz = 23,",
          "the high-pass field warns towards 26 and 50 Hz (owner, 06.10: yellow from 30 down, the ramp as long as before)" },
        { false, "machineTopHz = 50", "machineTopHz = 80", "the machine's high-pass tops out at 50 Hz" },
        { false, "hzMax = 80", "hzMax = 50", "a person's high-pass knob travels to 80 Hz (owner, 01.10)" },
        { false, "hzMax = 80", "hzMax = 81", "a person's high-pass knob travels to 80 Hz (owner, 01.10)" },
        { false, "slopes = [12, 24, 48]", "slopes = [12, 24, 36]", "the high-pass slopes are 12, 24 and 48 dB/oct" },
        { false, "passes = 12", "passes = 11", "the landing: one budget of 12 passes" },
        { false, "expectedPasses = 3", "expectedPasses = 4", "the bar expects 3 renders of a manual master, 7 of max clean, 6 of max dense (owner, 06.10)" },
        { false, "expectedPassesMaxClean = 7", "expectedPassesMaxClean = 6",
          "the bar expects 3 renders of a manual master, 7 of max clean, 6 of max dense (owner, 06.10)" },
        { false, "expectedPassesMaxDense = 6", "expectedPassesMaxDense = 7",
          "the bar expects 3 renders of a manual master, 7 of max clean, 6 of max dense (owner, 06.10)" },
        { false, "onSourceGate = true", "onSourceGate = false", "the landing lands the level on the source's gate (owner, 04.10)" },
        { false, "clean = { budgetDb = 0.5 }", "clean = { budgetDb = 0.75 }",
          "max clean: the limiter's budget 0.5 dB, about −13 LUFS, a little above streaming (owner, 04.10, by ear)" },
        { false, "dense = { budgetDb = 1.75 }", "dense = { budgetDb = 3 }",
          "max dense: the limiter's budget 1.75 dB, about −11 LUFS (owner, 04.10, by ear)" },
        { false, "ceilingLufs = -5", "ceilingLufs = -6", "the max modes search up to −5 LUFS" },
        { false, "floorLufs = -14", "floorLufs = -13", "a max master never lands under −14 LUFS, whichever target (owner, 04.10)" },
        { false, "budgetResolutionDb = 0.25", "budgetResolutionDb = 0.5",
          "the limiter-budget edge is proved to 0.25 dB unless a master asks for 0.05…1 dB" },
        { true, "loudnessMode = \"maxDense\" }", "loudnessMode = \"maxClean\" }", "targets.maxDense.loudnessMode" },
        { false, "quietDb = 4,", "quietDb = 5,", "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { false, "loudDb = 7.5,", "loudDb = 10,", "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { false, "loudDb = 7.5,", "loudDb = 7.75,", "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { false, "middleLufs = [-10, -8]", "middleLufs = [-11, -8]",
          "the limiter's budget: P95 4 dB below −10 LUFS, 7 dB from −10 to −8, 7.5 dB louder (owner, 04.10; 7.5 in v0.14.1)" },
        { true, "noteLossDb = 0.3", "noteLossDb = 0.5", "targets.club.noteLossDb" },
        { true, "lufs = -7, tp = -1, monoBass = 120", "lufs = -8, tp = -1, monoBass = 120", "targets.youtubeMusic.lufs" },
        { true, "hpfFloor = 32, hpfSlopeDbPerOct = 24, noteLossDb = 0.3", "hpfFloor = 24, hpfSlopeDbPerOct = 24, noteLossDb = 0.3",
          "targets.club.hpfFloor" },
        { false, "note = { soundingAtLeastS = 3 }", "note = { soundingAtLeastS = 2 }",
          "a sure lowest note: 2 dB over the occupancy line, 10 % of the frames, 3 s in all; not sought under 10 s" },
        { false, "lowNoteHz = 10", "lowNoteHz = 20",
          "the low end's table measures from 10 Hz to 500 Hz, the note's readings to 300 Hz, and the lowest note is sought "
          "from 30 Hz, B0 30.87 Hz, a five-string's lowest (owner, 06.10 and 07.10)" },
        { false, "highNoteHz = 500", "highNoteHz = 300",
          "the low end's table measures from 10 Hz to 500 Hz, the note's readings to 300 Hz, and the lowest note is sought "
          "from 30 Hz, B0 30.87 Hz, a five-string's lowest (owner, 06.10 and 07.10)" },
        { false, "noteTopHz = 300", "noteTopHz = 0",
          "the low end's table measures from 10 Hz to 500 Hz, the note's readings to 300 Hz, and the lowest note is sought "
          "from 30 Hz, B0 30.87 Hz, a five-string's lowest (owner, 06.10 and 07.10)" },
        { false, "fftOrderUpToHz = 48000", "fftOrderUpToHz = 44100",
          "the low end resolves at every rate as at 48 kHz: fftOrder 17 up to 48 kHz, one more per doubling of the rate above it (owner, 07.10)" },
        { false, "bias = 0.2\n", "bias = 0.3\n", "the asymmetric diode by hand: bias 0.2, its DC blocker at 10 Hz, written for it alone (owner, 07.10)" },
        { false, "dcBlockHz = 10\n", "dcBlockHz = 5\n", "the asymmetric diode by hand: bias 0.2, its DC blocker at 10 Hz, written for it alone (owner, 07.10)" },
        { false, "noteRangeShareAtLeastDb = -140", "noteRangeShareAtLeastDb = -160",
          "a mix with no bass is unsure: the note range holds −140 dB of the programme, or there is no note (owner, 07.10)" },
        { false, "comfort = { lowHz = 100,", "comfort = { lowHz = 90,",
          "the comfort windows: mono bass's crossover 100–180 Hz, red at 75 and 250; the glue 0–1.5 dB, red at 6; its mix "
          "30–100 %, red at 0; the saturation's drive 0–1.5 dB, red at 8 (owner, 07.10)" },
        { false, "driveComfort = { low = 0, high = 1.5, warningLow = 0, warningHigh = 8 }",
          "driveComfort = { low = 0, high = 1.5, warningLow = 0, warningHigh = 6 }",
          "the comfort windows: mono bass's crossover 100–180 Hz, red at 75 and 250; the glue 0–1.5 dB, red at 6; its mix "
          "30–100 %, red at 0; the saturation's drive 0–1.5 dB, red at 8 (owner, 07.10)" },
        { false, "hzStep = 0", "hzStep = 1",
          "stepless knobs: the high-pass cutoff, tilt, low, the five EQ bands, the saturation's drive, the target's loudness "
          "and ceiling (owner, 06.10 and 07.10)" },
        { true, "green = [-15, -13], step = 0 }", "green = [-15, -13], step = 0.1 }",
          "stepless knobs: the high-pass cutoff, tilt, low, the five EQ bands, the saturation's drive, the target's loudness "
          "and ceiling (owner, 06.10 and 07.10)" },
        { false, "lowestNoteFromHz = 30", "lowestNoteFromHz = 25",
          "the low end's table measures from 10 Hz to 500 Hz, the note's readings to 300 Hz, and the lowest note is sought "
          "from 30 Hz, B0 30.87 Hz, a five-string's lowest (owner, 06.10 and 07.10)" },
        { false, "offAboveDb = 3,", "offAboveDb = 4,", "mono bass by its loss: placed under 1 dB, with a warning from 1 to 3 dB, left out above 3 dB" },
        { false, "eq = true\nmonoBass = false", "eq = true\nmonoBass = true", "mono bass is placed by its weighed loss, never before it" },
        { true, "sampleRate = 48000, bitDepth = 24 }\n# YouTube Music", "sampleRate = 22050, bitDepth = 24 }\n# YouTube Music",
          "targets.youtube.sampleRate" },
        { false, "default = 0\nwhenTicked", "default = 0.3\nwhenTicked", "no glue by default: a target without its own takes the compressor out" },
        { false, "whenTicked = 2.6", "whenTicked = 2.7", "ticked on untouched, the glue is up to 2.6 dB (owner, 08.10)" },
        { false, "ticked = { ratio = 2,", "ticked = { ratio = 2.5,",
          "a tick on an untouched glue gives it ratio 2, knee 6 dB, attack 30 ms and release 300 ms — glue, not a compressor (owner, 08.10)" },
        { false, "whenTicked = 6", "whenTicked = 5", "ticked on untouched, the saturation drives 6 dB (owner, 08.10)" },
        { false, "aboveLufs = -12", "aboveLufs = -13",
          "already mastered requires louder than −12 LUFS, true peak above −1.5 dBTP and PLR below 11 (owner, 05.10)" },
        { false, "peakAboveDbTp = -1.5", "peakAboveDbTp = -2",
          "already mastered requires louder than −12 LUFS, true peak above −1.5 dBTP and PLR below 11 (owner, 05.10)" },
        { false, "plrBelowDb = 11", "plrBelowDb = 12",
          "already mastered requires louder than −12 LUFS, true peak above −1.5 dBTP and PLR below 11 (owner, 05.10)" },
        { false, "limiterSlopeBelow = 0.2", "limiterSlopeBelow = 0.3",
          "manual landings stop below 0.2 LU per dB of P95 cut, spaced by at least 0.5 dB (owner, 05.10)" },
        { false, "limiterSlopeSpacingDb = 0.5", "limiterSlopeSpacingDb = 0.4",
          "manual landings stop below 0.2 LU per dB of P95 cut, spaced by at least 0.5 dB (owner, 05.10)" },
        { false, "mix = 0.4\nmixRange", "mix = 1\nmixRange", "the glue in parallel by default: mix 40 %, a knob 0…100 %, stepless (owner, 05.10 and 07.10)" },
        { false, "mixStep = 0\n", "mixStep = 0.2\n", "the glue in parallel by default: mix 40 %, a knob 0…100 %, stepless (owner, 05.10 and 07.10)" },
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
