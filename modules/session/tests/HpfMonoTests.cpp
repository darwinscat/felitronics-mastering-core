// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE HIGH-PASS AND MONO BASS (owner decisions 3.2–3.5): the sure lowest note at each of its four boundaries and the short
// programme; the cutoff on the chain's own response — the loss the target allows at the note, or the floor or the top it
// stops at; always on, a quiet input included; the loss of the low end folded to mono at 1 and 3 dB exactly, above them,
// where the bass does not sound and where it is too little; a person's mono bass against the machine; the knobs' domains
// (18 and 36 dB/oct, a cutoff above 50 Hz, not rounded); and, through the real pump on synthetic mixes, a detector that
// never errs upward and a loss that tells normal, partial and inverted bass apart — once for every target.

#include "../../../tests/DeclaredBudget.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Grid.h"
#include "Planner.h"
#include "RealMixLowEnd.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/eq/EqBand.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <limits>
#include <bit>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace declared = felitronics::declared;
namespace eq = felitronics::eq;

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return detail::same (a, b); }
double midiHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

// A LOW-END READING as the first phase publishes it — its band table (MIDI 4…71, 10.30…493.88 Hz, the run's geometry; the note
// reads the bands up to 293.66 Hz, MIDI 62) and its 10 ms blocks — owned here, with the loudness reading beside it, laid out by
// Analyzer for the planner.
constexpr int kFirstMidi = 4, kBandCount = 68, kNoteTopMidi = 62;
struct Readings
{
    std::uint32_t rate = 48000;
    double hop = 65536;
    std::vector<double> bands = std::vector<double> (kBandCount * 16, 0.0);
    std::vector<double> blocks;
    std::uint64_t blocksNotKept = 0;           // blocks of the piece past what the run holds: a reading of a first part
    double background = 0;                     // the run's backgroundDensity: the median density of the note range
    double rangeShare = 0.5;                   // the run's bandRangeShare: the note range's share of the programme
    MeasurementValue lowNumbers[6] {};
    MeasurementArray lowArrays[2] {};
    MeasurementValue loudness[2] {};
    MeasurementResult results[kAnalyzers] {};
    Readings()
    {
        for (int b = 0; b < kBandCount; ++b)
        {
            double* row = bands.data() + b * 16;
            row[0] = kFirstMidi + b; row[1] = midiHz (kFirstMidi + b); row[15] = 1;
        }
    }
    // A band occupied `duty` of the frames, `count` frames, `margin` dB above the duty line.
    void occupy (int midi, double duty, double count, double margin)
    {
        double* row = bands.data() + (midi - kFirstMidi) * 16;
        row[11] = count; row[12] = duty; row[14] = margin; row[13] = margin - 20;
    }
    void block (double mid, double side, double samples = 480, bool valid = true)
    {
        const double row[] { double (blocks.size() / 8) * 480, samples, samples, 0, mid, side, valid ? 1.0 : 0.0, double (blocks.size() / 8) };
        blocks.insert (blocks.end(), row, row + 8);
    }
    detail::PlanInputs inputs (const char* target, double seconds, double lufs = -14, double crossover = 120)
    {
        for (std::size_t i = 0; i < kAnalyzers; ++i)
        {
            results[i] = {}; results[i].analyzer = Analyzer (i);
            results[i].status = MeasurementStatus::Unavailable; results[i].reason = MeasurementReason::NotImplemented;
        }
        loudness[0] = { "integratedLufs", lufs, MeasurementReason::None, 0 };
        loudness[1] = { "truePeakDb", -1.0, MeasurementReason::None, 0 };
        auto& l = results[std::size_t (Analyzer::Loudness)];
        l.status = MeasurementStatus::Ready; l.reason = MeasurementReason::None; l.numbers = loudness;
        lowNumbers[0] = { "hopSamples", hop, MeasurementReason::None, 0 };
        lowNumbers[1] = { "sampleRate", double (rate), MeasurementReason::None, 0 };
        lowNumbers[2] = { "peakMidi", 40.0, MeasurementReason::None, 0 };
        lowNumbers[3] = { "crossoverHz", crossover, MeasurementReason::None, 0 };
        lowNumbers[4] = { "backgroundDensity", background, MeasurementReason::None, 0 };
        lowNumbers[5] = { "bandRangeShare", rangeShare, MeasurementReason::None, 0 };
        lowArrays[0] = { "blocks", { 0, 480, std::uint64_t (seconds * rate), rate }, 8, blocks.size() / 8 + blocksNotKept, blocks.size() / 8,
                         blocksNotKept == 0, blocks };
        lowArrays[1] = { "bands", { 0, 0, std::uint64_t (seconds * rate), rate }, 16, kBandCount, kBandCount, true, bands };
        auto& r = results[std::size_t (crossover == 150 ? Analyzer::LowEnd150 : Analyzer::LowEnd)];
        r.status = MeasurementStatus::Ready; r.reason = MeasurementReason::None;
        r.numbers = lowNumbers; r.arrays = lowArrays;
        detail::PlanInputs in;
        in.rules = detail::rules();
        in.row = *in.rules.find (target);
        in.channels = 2; in.sampleRate = rate; in.frames = std::uint64_t (seconds * rate);
        in.measurements = results;
        return in;
    }
};
struct Planned { Devices devices; DevicePlans plans; detail::PlanFindings found; };
Planned plan (const detail::PlanInputs& in)
{
    Planned p;
    detail::propose (in, p.devices, p.plans, p.found);
    return p;
}
// The loss core's own design of the high-pass takes at `hz`, dB, positive.
double coreLoss (double fc, int slope, double rate, double hz)
{
    eq::BandParams b; b.on = true; b.type = eq::FilterType::HighPass; b.lanes[0].freq = fc; b.lanes[0].slope = slope;
    return -20 * std::log10 (std::abs (eq::bandResponse (b, rate, 2 * kPi * hz / rate)));
}

//==============================================================================

void theSureLowestNote()
{
    felitronics::test::group ("the sure lowest note: 2 dB over the line, 10 % of the frames, from 25 Hz, 3 s in all; not sought under 10 s");
    const auto cut = [] (Readings& r, double seconds = 60) { return plan (r.inputs ("allStreaming", seconds)).found.hpf; };
    {
        Readings r; r.occupy (33, 0.10, 20, 2.0);
        const auto f = cut (r);
        ok (f.cut == HpfCut::Note && f.noteMidi == 33 && same (*f.noteHz, midiHz (33)), "2 dB over the line and 10 % of the frames: sure");
        Readings under; under.occupy (33, 0.10, 20, std::nextafter (2.0, 0.0));
        ok (cut (under).cut == HpfCut::Unsure, "a hair under 2 dB: not sure — the floor");
        Readings rare; rare.occupy (33, std::nextafter (0.10, 0.0), 20, 6); rare.occupy (45, 0.5, 60, 6);
        const auto g = cut (rare);
        ok (g.cut == HpfCut::Unsure && ! g.noteMidi && same (g.cutoffHz, 32.0),
            "a hair under 10 % of the frames: the lowest band is not sure — the floor, never the next band as the note");
        Readings once; once.occupy (20, 0.004, 1, 6); once.occupy (45, 0.5, 60, 6);
        ok (cut (once).cut == HpfCut::Unsure && same (cut (once).cutoffHz, 32.0), "a lowest band on in a single frame: the floor all the same");
        Readings unsureLowest; unsureLowest.occupy (33, 0.2, 20, 1.0); unsureLowest.occupy (45, 0.5, 60, 6);
        ok (cut (unsureLowest).cut == HpfCut::Unsure, "the lowest occupied band not sure: the floor — never a higher band, which would cut music");
    }
    {
        // The search starts at [lowEnd] lowestNoteFromHz, 25 Hz included (owner, 06.10), which [hpf] note.aboveHz (20 Hz) lies under.
        Readings r; r.occupy (kFirstMidi, 0.5, 40, 6);
        r.bands[1] = 25.0;
        ok (cut (r).cut != HpfCut::Unsure && r.bands[1] > 20.0, "a band at 25 Hz exactly is sought — and above 20 Hz");
        r.bands[1] = std::nextafter (25.0, 0.0);
        ok (cut (r).cut == HpfCut::Unsure, "a hair under 25 Hz is not: the floor");
    }
    {
        // The bottom of the low end (owner, 07.10): the lowest band on at all from 25 Hz is a note only where the band under
        // it — under 25 Hz included — was never on; on in a single frame, it is no note: the floor, never the band above it.
        Readings r; r.occupy (20, 0.5, 60, 6); r.occupy (28, 0.5, 60, 6);
        ok (cut (r).noteMidi == 20, "a lowest band at 25.96 Hz with nothing on under it: the note");
        r.occupy (19, 0.004, 1, 0.5);
        ok (cut (r).cut == HpfCut::Unsure && ! cut (r).noteMidi && same (cut (r).cutoffHz, 32.0),
            "the band under it, 24.50 Hz, on in one frame: no note, the floor — never the band above it");
        Readings lower; lower.occupy (20, 0.5, 60, 6); lower.occupy (18, 0.5, 60, 6);
        ok (cut (lower).noteMidi == 20, "a band on further under 25 Hz, 23.12 Hz, with 24.50 Hz silent: the note all the same");
        Readings first; first.occupy (kFirstMidi, 0.5, 40, 6); first.bands[1] = 25.0;
        ok (cut (first).noteMidi == kFirstMidi, "the table's first band, with no band under it: the note");
    }
    {
        // The range's share of the programme, [lowEnd] noteRangeShareAtLeastDb (owner, 07.10): −140 dB is a note, a hair
        // less is none.
        Readings r; r.occupy (28, 0.5, 60, 6);
        r.rangeShare = felitronics::core::det::pow10 (-14.0);
        ok (cut (r).noteMidi == 28, "a range holding −140 dB of the programme: the note");
        r.rangeShare = std::nextafter (r.rangeShare, 0.0);
        ok (cut (r).cut == HpfCut::Unsure && ! cut (r).noteMidi, "a hair under −140 dB: no note, the floor");
    }
    {
        Readings r; r.hop = 48000; r.occupy (33, 0.5, 3, 6);
        ok (cut (r).cut == HpfCut::Note, "3 frames of a 1 s hop: 3 s in all, sure");
        r.hop = 47999;
        ok (cut (r).cut == HpfCut::Unsure, "a hair under 3 s: not sure");
    }
    {
        Readings r; r.occupy (33, 0.5, 20, 6);
        ok (cut (r, 10.0).cut == HpfCut::Note, "a programme of 10 s exactly is searched");
        ok (cut (r, 10.0 - 1.0 / 48000).cut == HpfCut::Short && same (cut (r, 9.99).cutoffHz, 32.0),
            "one frame shorter is not: the floor, whatever its note");
    }
    {
        Readings r;
        ok (cut (r).cut == HpfCut::Unsure && same (cut (r).cutoffHz, 32.0), "no occupied band: the floor");
    }
}

// THE LOWEST NOTE FROM 25 Hz (owner, 06.10): the table measures from 10 Hz, and the note is sought from [lowEnd]
// lowestNoteFromHz up — a band under it is the spectrum's, skipped, never the note and never the floor in the note's place.
void theLowestNoteFromTwentyFiveHz()
{
    felitronics::test::group ("the lowest note is sought from 25 Hz up: a band under it is skipped, never the note, never the floor");
    const auto cut = [] (Readings& r) { return plan (r.inputs ("allStreaming", 60)).found.hpf; };
    {
        Readings r; r.occupy (19, 0.5, 60, 6);
        const auto f = cut (r);
        ok (f.cut == HpfCut::Unsure && ! f.noteMidi && same (f.cutoffHz, 32.0), "a band at 24.50 Hz, sure by every other test, alone: no note — the floor");
    }
    {
        Readings r; r.occupy (20, 0.5, 60, 6);
        const auto f = cut (r);
        ok (f.cut == HpfCut::BelowFloor && f.noteMidi == 20 && same (*f.noteHz, midiHz (20)),
            "the first band from 25 Hz, 25.96 Hz: the note (under the floor, which stands)");
    }
    {
        Readings r; r.occupy (11, 0.05, 3, 0.5); r.occupy (28, 0.5, 60, 6);
        const auto f = cut (r);
        ok (f.cut != HpfCut::Unsure && f.noteMidi == 28,
            "an unsure band at 15.43 Hz under a sure 41.2 Hz: the note is 41.2 Hz — the rumble neither is the note nor makes it the floor");
    }
    {
        Readings r; r.occupy (11, 0.9, 100, 10);
        ok (cut (r).cut == HpfCut::Unsure && same (cut (r).cutoffHz, 32.0), "a steady 15.43 Hz and nothing from 25 Hz up: no note, the floor");
    }
}

void theCutoffOnTheChainsResponse()
{
    felitronics::test::group ("the cutoff: the loss the target allows at the note on the chain's own response, the 32 Hz floor, the 50 Hz top");
    const auto r = detail::rules();
    // The machine's top is 50 Hz, never the knob's travel, which goes to 80 (owner, 01.10).
    ok (same (r.hpfTop.toDouble(), 50.0) && same (r.hpfFq.to.toDouble(), 80.0), "PRECONDITION: the machine's top 50 Hz, the knob's travel 80");
    bool exact = true, stops = true, floors = true, unrounded = true, underTop = true;
    std::string worst;
    for (std::uint16_t row = 0; row < r.rows; ++row)
    {
        const auto target = r.row (row);
        const double loss = target.noteLossDb.toDouble(), floor = target.hpfFloor.toDouble();
        const int slope = target.hpfSlope;
        floors = floors && same (floor, 32.0);
        for (const std::uint32_t rate : { 44100u, 48000u, 96000u })
            for (int midi = 20; midi <= 62; ++midi)   // every band the note is sought in: from 25 Hz (25.96 Hz) to the table's top
            {
                Readings in; in.rate = rate; in.occupy (midi, 0.5, 60, 6);
                const auto p = plan (in.inputs (std::string (target.key).c_str(), 60));
                const auto& f = p.found.hpf;
                const double atNote = coreLoss (p.devices.hpf.machine.fq, slope, rate, midiHz (midi));
                if (f.cut == HpfCut::Note)
                {
                    exact = exact && std::abs (atNote - loss) < 1e-6 && atNote <= loss + 1e-9 && same (*f.noteLossDb, detail::highPassLossDb (p.devices.hpf.machine.fq, slope, rate, midiHz (midi)));
                    unrounded = unrounded && ! same (p.devices.hpf.machine.fq, std::round (p.devices.hpf.machine.fq));
                    if (std::abs (atNote - loss) >= 1e-6) worst = std::string (target.key) + " " + std::to_string (midi);
                }
                else if (f.cut == HpfCut::Floor || f.cut == HpfCut::BelowFloor)
                    stops = stops && same (p.devices.hpf.machine.fq, floor) && atNote > loss && std::abs (*f.noteLossDb - atNote) < 1e-6
                        && (f.cut == HpfCut::BelowFloor) == (midiHz (midi) < floor);
                else if (f.cut == HpfCut::Top)
                    stops = stops && same (p.devices.hpf.machine.fq, 50.0) && atNote < loss;
                else stops = false;
                stops = stops && p.devices.hpf.machine.on && p.devices.hpf.machine.slope == slope;
                underTop = underTop && p.devices.hpf.machine.fq <= 50.0 && f.cutoffHz <= 50.0;
            }
    }
    ok (underTop, "the machine's cutoff is never above 50 Hz, on every target, note and rate — the knob's 80 is a person's");
    ok (floors, "the floor is 32 Hz on every target");
    ok (exact, "a note that decides: core's own high-pass takes exactly the target's loss there (1 dB, club 0.3), on every target, slope and rate" + (worst.empty() ? "" : " — not " + worst));
    ok (unrounded, "the cutoff is not rounded to the hertz");
    ok (stops, "a floor above the note's cutoff takes more of it, and says how much; a top below it takes less");
    Readings low; low.occupy (21, 0.5, 60, 6);                // 27.5 Hz: an 808 under the floor
    const auto f = plan (low.inputs ("allStreaming", 60)).found.hpf;
    ok (f.cut == HpfCut::BelowFloor && same (f.cutoffHz, 32.0) && *f.noteLossDb > 3, "a note under the floor: 32 Hz, and the note cut by "
        + std::to_string (*f.noteLossDb) + " dB");
    const auto text = text::Text::text (PlanText::hpf (f), text::Lang::Ru);
    ok (text.find ("ниже пола") != std::string::npos && text.find ('{') == std::string::npos, "the report says so: " + text);
    Readings over; over.occupy (26, 0.5, 60, 6);              // 36.7 Hz: above the floor, but its cutoff is below it
    const auto g = plan (over.inputs ("allStreaming", 60)).found.hpf;
    const auto overText = text::Text::text (PlanText::hpf (g), text::Lang::Ru);
    ok (g.cut == HpfCut::Floor && same (g.cutoffHz, 32.0) && *g.noteLossDb > 1 && overText.find ("ниже пола") == std::string::npos
        && overText.find ("пол цели") != std::string::npos, "a note above the floor that the floor takes more of: the floor, and its cost — " + overText);
    Readings high; high.occupy (43, 0.5, 60, 6);              // 98 Hz: the cutoff it allows is above the top
    const auto top = plan (high.inputs ("allStreaming", 60)).found.hpf;
    ok (top.cut == HpfCut::Top && same (top.cutoffHz, 50.0), "a note whose cutoff is above 50 Hz: the top");
    const auto topText = text::Text::text (PlanText::hpf (top), text::Lang::Ru);
    ok (topText.find ("упёрся в 50,0") != std::string::npos, "and the report says it stopped there, at the machine's top: " + topText);
}

void quietAndUnmeasured()
{
    felitronics::test::group ("the high-pass stands on a quiet input too, at its floor; mono bass does not; below −55 LUFS strictly");
    Readings r; r.occupy (33, 0.5, 60, 6);
    for (int i = 0; i < 1000; ++i) r.block (1, 0);
    auto p = plan (r.inputs ("allStreaming", 60, std::nextafter (-55.0, -60.0)));
    ok (p.devices.hpf.machine.on && p.found.hpf.cut == HpfCut::Quiet && same (p.devices.hpf.machine.fq, 32.0)
        && ! p.devices.monoBass.machine.on && p.found.monoBass.verdict == MonoBassVerdict::Quiet && p.plans.monoBass.heldBack == HeldBack::Quiet,
        "below −55 LUFS: the high-pass at its floor, mono bass not placed");
    p = plan (r.inputs ("allStreaming", 60, -55.0));
    ok (p.found.hpf.cut == HpfCut::Note && p.devices.monoBass.machine.on, "at −55 LUFS exactly: the ordinary machine");
    Readings none;
    detail::PlanInputs in = none.inputs ("allStreaming", 60);
    none.results[std::size_t (Analyzer::LowEnd)].status = MeasurementStatus::Unavailable;
    const auto u = plan (in);
    ok (u.devices.hpf.machine.on && u.found.hpf.cut == HpfCut::Unmeasured && same (u.found.hpf.cutoffHz, 32.0)
        && u.found.monoBass.verdict == MonoBassVerdict::Unmeasured && ! u.devices.monoBass.machine.on,
        "no low-end reading: the high-pass at its floor with its reason; mono bass not weighed, not placed — no false safety");
}

// Blocks whose low band loses `lossDb` folded to mono: mid 1 and side 10^(lossDb/10) − 1; 512 of them, 5.12 s.
MonoBassFinding weigh (double lossDb, double crossover = 120)
{
    Readings r;
    const double side = std::pow (10.0, lossDb / 10) - 1.0;
    for (int i = 0; i < 512; ++i) r.block (1.0, side);
    return plan (r.inputs (crossover == 150 ? "lp" : "allStreaming", 60, -14, crossover)).found.monoBass;
}

void theLossOfTheLowEnd()
{
    felitronics::test::group ("mono bass by the loss folding takes: under 1 dB placed; 1 to 3 dB placed with the number; above 3 dB left out");
    const auto r0 = detail::rules();
    const auto verdict = [&] (double loss) { return detail::monoBassVerdictFor (r0, loss); };
    ok (verdict (std::nextafter (1.0, 0.0)) == MonoBassVerdict::On && verdict (1.0) == MonoBassVerdict::Partial
        && verdict (3.0) == MonoBassVerdict::Partial && verdict (std::nextafter (3.0, 4.0)) == MonoBassVerdict::AntiPhase
        && verdict (std::numeric_limits<double>::infinity()) == MonoBassVerdict::AntiPhase,
        "the boundaries, to the bit: under 1 dB placed; 1 and 3 dB exactly placed with the number; a hair above 3 dB left out");
    const auto below = weigh (0.5), partly = weigh (2.0), above = weigh (6.0);
    ok (below.verdict == MonoBassVerdict::On && std::abs (*below.lossDb - 0.5) < 1e-9, "0.5 dB: placed, nothing to say");
    ok (partly.verdict == MonoBassVerdict::Partial && std::abs (*partly.lossDb - 2.0) < 1e-9, "2 dB: placed, with the number");
    ok (above.verdict == MonoBassVerdict::AntiPhase && std::abs (*above.lossDb - 6.0) < 1e-9, "6 dB: left out");
    Readings r;
    for (int i = 0; i < 512; ++i) r.block (1.0, 3.0);
    const auto p = plan (r.inputs ("allStreaming", 60));
    ok (! p.devices.monoBass.machine.on && p.plans.monoBass.heldBack == HeldBack::Measured && (p.plans.monoBass.measured & 1u) != 0,
        "the machine leaves it out, held back by what it measured");
    const auto say = text::Text::text (*PlanText::monoBass (partly), text::Lang::Ru);
    const auto warn = text::Text::text (*PlanText::monoBass (above), text::Lang::Ru);
    ok (say.find ("частично") != std::string::npos && warn.find ("полярность") != std::string::npos && ! PlanText::monoBass (below),
        "the report: partly — " + say + " / inverted — " + warn);
    Readings wide;
    for (int i = 0; i < 512; ++i) wide.block (0.94, 0.06);
    const auto sixPercent = plan (wide.inputs ("allStreaming", 60)).found.monoBass;
    ok (sixPercent.verdict == MonoBassVerdict::On, "wide bass at 6 % of side folds with " + std::to_string (*sixPercent.lossDb)
        + " dB: placed — the width warns, it does not decide");
    Readings silent;
    for (int i = 0; i < 512; ++i) silent.block (0.0, 1.0);
    const auto gone = plan (silent.inputs ("allStreaming", 60)).found.monoBass;
    ok (gone.verdict == MonoBassVerdict::AntiPhase && gone.lossDb && std::isinf (*gone.lossDb), "a low end with no mid at all: everything folds away");
    ok (weigh (2.0, 150).verdict == MonoBassVerdict::Partial && same (weigh (2.0, 150).crossoverHz, 150.0),
        "on vinyl the loss is weighed at 150 Hz, its crossover");
    ok (weigh (2.0, 120).verdict == MonoBassVerdict::Partial, "and at 120 Hz elsewhere");
}

void whereTheBassSounds()
{
    felitronics::test::group ("weighed where the bass sounds: a quiet inverted stretch does not count; too little bass is its own reason");
    Readings r;
    for (int i = 0; i < 512; ++i) r.block (1.0, 0.0);
    for (int i = 0; i < 4096; ++i) r.block (1e-4, 1e-3);          // 40 dB down, inverted: not where the bass sounds
    const auto quietTail = plan (r.inputs ("allStreaming", 60)).found.monoBass;
    ok (quietTail.verdict == MonoBassVerdict::On && same (*quietTail.lossDb, 0.0) && same (quietTail.soundingSeconds, 5.12),
        "a long quiet inverted stretch 40 dB under the bass is not weighed");
    Readings loud;
    for (int i = 0; i < 512; ++i) loud.block (1.0, 0.0);
    for (int i = 0; i < 512; ++i) loud.block (0.05, 0.05);        // 10 dB down, inverted half: it sounds, and counts
    ok (plan (loud.inputs ("allStreaming", 60)).found.monoBass.lossDb > 0.0, "a stretch 10 dB under the bass sounds, and counts");
    Readings little;
    for (int i = 0; i < 299; ++i) little.block (1.0, 0.0);        // 2.99 s
    const auto p = plan (little.inputs ("allStreaming", 60));
    ok (p.found.monoBass.verdict == MonoBassVerdict::Unmeasured && ! p.devices.monoBass.machine.on && p.plans.monoBass.heldBack == HeldBack::Unmeasured,
        "under 3 s of bass: not weighed, and not placed, with its reason");
    Readings enough;
    for (int i = 0; i < 300; ++i) enough.block (1.0, 0.0);
    ok (plan (enough.inputs ("allStreaming", 60)).found.monoBass.verdict == MonoBassVerdict::On, "3 s exactly: weighed");
    Readings holes;
    for (int i = 0; i < 512; ++i) holes.block (1.0, 0.0, 480, false);
    ok (plan (holes.inputs ("allStreaming", 60)).found.monoBass.verdict == MonoBassVerdict::Unmeasured, "blocks with holes are not weighed");
    const auto spent = declared::spend ([&] { (void) plan (loud.inputs ("allStreaming", 60)); });
    ok (spent.requests == 0, "the planner asks the heap for nothing");
}

//==============================================================================
// THROUGH THE PUMP — synthetic mixes, measured as a source is.

struct Mix
{
    static constexpr std::uint32_t rate = 48000;
    std::vector<float> left, right;
    explicit Mix (double seconds) : left (std::size_t (seconds * rate)), right (left.size()) {}
    // A passage of a sine from `from` to `to` seconds, fading over 50 ms at each end (a gated sine's click is broadband).
    Mix& passage (double hz, double level, double from, double to)
    {
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const double t = double (i) / rate;
            const double gate = std::clamp ((t - from) / 0.05, 0.0, 1.0) * std::clamp ((to - t) / 0.05, 0.0, 1.0);
            if (! (gate > 0)) continue;
            const double x = level * gate * felitronics::core::det::sin (2 * kPi * hz * t);
            left[i] += float (x); right[i] += float (x);
        }
        return *this;
    }
    // TPDF dither of ±1 LSB at `bits`, white and independent per channel, from a fixed seed: the noise a delivery adds.
    Mix& dither (int bits, std::uint32_t seed = 1)
    {
        const double lsb = 1.0 / double (std::uint32_t (1) << (bits - 1));
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const double l = uniform (seed) - uniform (seed), r = uniform (seed) - uniform (seed);
            left[i] += float (lsb * l); right[i] += float (lsb * r);
        }
        return *this;
    }
    // A rumble: pink noise (1/f power — Kellet's filter on a fixed-seed white noise) at `rms` into both channels alike.
    Mix& pink (double rms, std::uint32_t seed = 7)
    {
        std::vector<double> x (left.size());
        double b[7] {}, sum = 0;
        for (auto& v : x)
        {
            const double w = 2 * uniform (seed) - 1;
            b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.96900 * b[2] + w * 0.1538520;
            b[3] = 0.86650 * b[3] + w * 0.3104856; b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
            v = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362; b[6] = w * 0.115926;
            sum += v * v;
        }
        const double gain = rms / std::sqrt (sum / double (x.size()));
        for (std::size_t i = 0; i < x.size(); ++i) { left[i] += float (gain * x[i]); right[i] += float (gain * x[i]); }
        return *this;
    }
    static double uniform (std::uint32_t& seed) { seed = seed * 1664525u + 1013904223u; return double (seed >> 8) / 16777216.0; }
    // `level` of a sine at `hz` into both channels, the right one scaled by `rightGain` (−1: inverted).
    Mix& tone (double hz, double level, double rightGain = 1.0, double from = 0, double to = 1e9)
    {
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const double t = double (i) / rate;
            if (t < from || t >= to) continue;
            const double x = level * felitronics::core::det::sin (2 * kPi * hz * t);
            left[i] += float (x); right[i] += float (x * rightGain);
        }
        return *this;
    }
};
struct Measured { std::unique_ptr<Session> s; PlanView plan; };
// Until the low-end runs the devices read have ended: the devices are placed at the loudness and the true peak, and their
// low-end fields are "not measured yet" until then.
void untilTheLowEnd (Session& s, unsigned limit)
{
    unsigned i = 0;
    for (; i < limit && s.state() == State::Loaded; ++i) (void) s.step (16);
    bool low = false, low150 = false;
    for (; i < limit && ! (low && low150); ++i)
    {
        if (s.step (16).state == StepState::Done) break;
        for (const auto& n : s.events())
            if (n.kind == EventKind::Measurement && n.payload.measurement.status != MeasurementStatus::Pending)
            {
                low = low || n.payload.measurement.analyzer == Analyzer::LowEnd;
                low150 = low150 || n.payload.measurement.analyzer == Analyzer::LowEnd150;
            }
    }
}
Measured measure (const Mix& m, const char* target = "allStreaming")
{
    Measured out { Session::create().session, {} };
    auto& s = *out.s;
    const float* planes[] { m.left.data(), m.right.data() };
    (void) s.apply (command::SetTarget { 1, target });
    ok (s.apply (command::Load { 2, { planes, 2, m.left.size(), Mix::rate }, {} }).rejection == Rejection::None, "PRECONDITION: the mix loads");
    untilTheLowEnd (s, 2000000);
    out.plan = s.snapshot().view().plan;
    return out;
}

void throughThePump()
{
    felitronics::test::group ("through the pump: the detector never errs upward; the loss tells normal, partial and inverted bass apart");
    const auto note = [] (double hz) { return std::round (12 * std::log2 (hz / 440.0) + 69); };
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3).tone (440.0, 0.1));
        ok (m.plan.hpf.cut == HpfCut::Note && m.plan.hpf.noteMidi && *m.plan.hpf.noteMidi <= note (55.0)
            && m.plan.hpf.cutoffHz > 32 && m.plan.hpf.cutoffHz < 55,
            "a bass at 55 Hz: its note, or one below it — never above — and a cutoff under it (" + std::to_string (m.plan.hpf.cutoffHz) + " Hz)");
        ok (m.plan.monoBass.verdict == MonoBassVerdict::On && *m.plan.monoBass.lossDb < 0.5, "centred bass folds with nothing lost ("
            + std::to_string (*m.plan.monoBass.lossDb) + " dB)");
    }
    {
        const auto m = measure (Mix (20).tone (1000.0, 0.3));
        ok (m.plan.hpf.cut == HpfCut::Unsure && same (m.plan.hpf.cutoffHz, 32.0), "no bass: the floor");
    }
    {
        const auto m = measure (Mix (20).tone (45.0, 0.3, 1.0, 5.0, 7.0).tone (330.0, 0.1));
        ok (! m.plan.hpf.noteMidi || *m.plan.hpf.noteMidi <= note (45.0), "a rare 808 — 2 s of 20: never a note above it");
        ok (m.plan.hpf.cutoffHz <= 45.0, "and no cutoff above it");
    }
    {
        const auto m = measure (Mix (8).tone (60.0, 0.3));
        ok (m.plan.hpf.cut == HpfCut::Short && same (m.plan.hpf.cutoffHz, 32.0), "a punk song of 8 s: not searched, the floor");
    }
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3, -1.0).tone (440.0, 0.1));
        ok (m.plan.monoBass.verdict == MonoBassVerdict::AntiPhase && *m.plan.monoBass.lossDb > 6, "bass inverted in one channel: left out ("
            + std::to_string (*m.plan.monoBass.lossDb) + " dB)");
    }
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3, 0.2).tone (440.0, 0.1));
        ok (m.plan.monoBass.verdict == MonoBassVerdict::Partial, "bass a fifth as loud in one channel: partly, with the number ("
            + std::to_string (*m.plan.monoBass.lossDb) + " dB)");
    }
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3).tone (2000.0, 0.2, -1.0));
        ok (m.plan.monoBass.verdict == MonoBassVerdict::On && *m.plan.monoBass.lossDb < 0.5,
            "opposite polarity above the crossover alone: the bass folds whole");
    }
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3, -1.0), "lp");
        ok (m.plan.monoBass.verdict == MonoBassVerdict::AntiPhase && same (m.plan.monoBass.crossoverHz, 150.0), "vinyl weighs it at 150 Hz");
    }
}

void aRareLowNoteIsNotSkipped()
{
    felitronics::test::group ("a rare low note is not skipped: the lowest band that was on decides, and unsure means the floor");
    // A steady bass at 55 Hz allows a cutoff of 46 Hz — were it the lowest thing in the mix.
    const auto bassy = [] { Mix m (120); m.tone (55.0, 0.3).tone (440.0, 0.05); return m; };
    {
        const auto m = measure (bassy());
        ok (m.plan.hpf.cut == HpfCut::Note && m.plan.hpf.cutoffHz > 40.0, "PRECONDITION: the bass alone gives a cutoff from its note ("
            + std::to_string (m.plan.hpf.cutoffHz) + " Hz)");
    }
    {
        // An 808 at 29.1 Hz in one 5-second passage of two minutes: under 10 % of the frames.
        auto mix = bassy(); mix.passage (29.1, 0.3, 50.0, 55.0);
        const auto m = measure (mix);
        ok (m.plan.hpf.cut == HpfCut::Unsure && same (m.plan.hpf.cutoffHz, 32.0) && ! m.plan.hpf.noteMidi,
            "a rare 808 under the bass: the floor — the bass is not taken for the lowest note (" + std::to_string (m.plan.hpf.cutoffHz) + " Hz)");
        const auto said = text::Text::text (PlanText::hpf (m.plan.hpf), text::Lang::Ru) + " / " + text::Text::text (PlanText::hpf (m.plan.hpf), text::Lang::En);
        ok (PlanText::hpf (m.plan.hpf).id == text::FactId::HpfUnsure && said.find ("неуверенно") != std::string::npos && said.find ('{') == std::string::npos,
            "and the floor says the lowest band was not sure: " + said);
    }
    {
        // One 0.2 s thump at 25 Hz.
        auto mix = bassy(); mix.passage (25.0, 0.4, 40.0, 40.2);
        const auto m = measure (mix);
        ok (m.plan.hpf.cut == HpfCut::Unsure && same (m.plan.hpf.cutoffHz, 32.0), "one thump under the bass: the floor as well ("
            + std::to_string (m.plan.hpf.cutoffHz) + " Hz)");
    }
}

void aPersonsKnobs()
{
    felitronics::test::group ("a person's knobs: mono bass against the machine, 18 and 36 dB/oct, a cutoff above 50 Hz — as written");
    auto m = measure (Mix (20).tone (55.0, 0.3, -1.0));
    auto& s = *m.s;
    ok (! s.project().devices.monoBass.machine.on && ! s.snapshot().view().plan.monoBass.againstMachine, "PRECONDITION: left out");
    MonoBassFields<Touched> mono; mono.on = true;
    ok (s.apply (command::EditDevice { 3, mono }).rejection == Rejection::None && s.snapshot().view().plan.monoBass.againstMachine,
        "a person switches it on anyway: taken, and the warning stands beside the tick");
    for (const std::int32_t slope : { 18, 36, 6, 96 })
    {
        HpfFields<Touched> hpf; hpf.fq = 57.3; hpf.slope = slope;
        ok (s.apply (command::EditDevice { 4, hpf }).rejection == Rejection::None && s.project().devices.hpf.hand.slope == slope
            && same (*s.project().devices.hpf.hand.fq, 57.3), std::to_string (slope) + " dB/oct and 57.3 Hz: as written, not the menu's");
    }
    const auto text = s.exportProject();
    auto copy = measure (Mix (20).tone (55.0, 0.3, -1.0));
    ok (copy.s->apply (command::ImportProject { 5, text.view() }).rejection == Rejection::None
        && copy.s->project().devices.hpf.hand.slope == 96 && same (*copy.s->project().devices.hpf.hand.fq, 57.3)
        && *copy.s->project().devices.monoBass.hand.on, "and a saved project keeps them");
    for (const auto& [fq, slope, why] : { std::tuple { 24000.0, 24, "at half the rate" }, std::tuple { 0.0, 24, "at 0 Hz" },
                                          std::tuple { 40.0, 7, "7 dB/oct" }, std::tuple { 40.0, 102, "102 dB/oct" } })
    {
        HpfFields<Touched> hpf; hpf.fq = fq; hpf.slope = slope;
        ok (s.apply (command::EditDevice { 6, hpf }).rejection != Rejection::None, std::string ("refused: ") + why);
    }
    // The knob travels to 80 Hz (owner, 01.10): a person's 70 Hz, past the machine's top, is taken and is what sounds.
    HpfFields<Touched> seventy; seventy.fq = 70.0; seventy.slope = 24;
    ok (s.apply (command::EditDevice { 6, seventy }).rejection == Rejection::None && same (*s.project().devices.hpf.hand.fq, 70.0),
        "70 Hz by hand: taken");
    {
        detail::EqStage at70; detail::writeEq (s.project().devices, detail::rules(), at70);
        ok (same (at70.bands[detail::eqBand (Device::Hpf)].lanes[0].freq, 70.0) && s.project().devices.hpf.machine.fq <= 50.0,
            "and the band sounds at 70 Hz; the machine's own cutoff stays at or under its 50 Hz top");
    }
    HpfFields<Touched> edge; edge.fq = std::nextafter (24000.0, 0.0); edge.slope = 24;
    ok (s.apply (command::EditDevice { 7, edge }).rejection == Rejection::None, "a hair under half the rate: taken");
    // The band written is the hand's cutoff and slope, exactly, and the curve is drawn from it.
    detail::EqStage stage; detail::writeEq (s.project().devices, detail::rules(), stage);
    ok (same (stage.bands[detail::eqBand (Device::Hpf)].lanes[0].freq, edge.fq.value_or (0))
        && stage.bands[detail::eqBand (Device::Hpf)].lanes[0].slope == 24, "the high-pass band is what the knob says, bit for bit");
}

void aPieceLongerThanTheReading()
{
    felitronics::test::group ("a piece longer than the low-end reading holds: mono bass is weighed over the part it holds, and says so");
    // Mono bass stands unless a MEASURED loss rules against it: the part the run holds is weighed, the device placed by
    // it, and the finding says how much of the piece that was.
    Readings r;
    for (int i = 0; i < 2000; ++i) r.block (1.0, 0.001);
    auto p = plan (r.inputs ("allStreaming", 30));
    ok (p.found.monoBass.verdict == MonoBassVerdict::On && p.devices.monoBass.machine.on && ! p.found.monoBass.coveredSeconds
        && ! PlanText::monoBassCoverage (p.found.monoBass), "PRECONDITION: the whole piece in the reading — weighed, placed, nothing to add");
    r.blocksNotKept = 1000;
    p = plan (r.inputs ("allStreaming", 30));
    ok (p.found.monoBass.verdict == MonoBassVerdict::On && p.devices.monoBass.machine.on && p.plans.monoBass.heldBack == HeldBack::None
        && p.found.monoBass.lossDb && p.found.monoBass.coveredSeconds && same (*p.found.monoBass.coveredSeconds, 20.0) && same (p.found.monoBass.pieceSeconds, 30.0),
        "a third of the piece past the reading: the first 20 s of 30 are weighed, and mono bass stands");
    const auto fact = PlanText::monoBassCoverage (p.found.monoBass);
    const auto said = fact ? text::Text::text (*fact, text::Lang::Ru) + " / " + text::Text::text (*fact, text::Lang::En) : std::string {};
    ok (fact && fact->id == text::FactId::MonoBassPartWeighed && fact->argCount == 2 && same (fact->args[0].number, 20.0) && same (fact->args[1].number, 30.0)
        && said.find ("20") != std::string::npos && said.find ("30") != std::string::npos && said.find ('{') == std::string::npos,
        "and the finding says what the weighing covered: " + said);
    // A measured loss above 3 dB rules against it on a part as on a whole.
    Readings inverted;
    for (int i = 0; i < 2000; ++i) inverted.block (0.001, 1.0);
    inverted.blocksNotKept = 100;
    const auto q = plan (inverted.inputs ("allStreaming", 21));
    ok (q.found.monoBass.verdict == MonoBassVerdict::AntiPhase && ! q.devices.monoBass.machine.on && q.found.monoBass.coveredSeconds,
        "a part with the bass in opposite polarity: left out by the measured loss, the coverage stated beside it");

    // Through the pump, at 8 kHz so that twelve minutes are light: the run keeps 65536 blocks of 10 ms — 10.9 minutes.
    constexpr unsigned rate = 8000, seconds = 700;
    std::vector<float> pcm (std::size_t (rate) * seconds);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = float (0.3 * felitronics::core::det::sin (2 * kPi * 55.0 * double (i % (rate * 4u)) / rate));
    const float* planes[] { pcm.data(), pcm.data() };
    auto s = Session::create().session;
    ok (s->apply (command::Load { 1, { planes, 2, pcm.size(), rate }, {} }).rejection == Rejection::None, "PRECONDITION: the long piece loads");
    untilTheLowEnd (*s, 40000000);
    const auto b = s->snapshot();
    const auto& found = b.view().plan.monoBass;
    const auto covered = PlanText::monoBassCoverage (found);
    ok (found.verdict == MonoBassVerdict::On && b.view().project.devices.monoBass.machine.on && found.coveredSeconds
        && std::fabs (*found.coveredSeconds - 655.36) < 0.01 && same (found.pieceSeconds, 700.0) && covered && covered->id == text::FactId::MonoBassPartWeighed,
        "eleven minutes forty of centred bass: weighed over the first " + std::to_string (found.coveredSeconds.value_or (0.0)) + " s, and mono bass stands — "
        + (covered ? text::Text::text (*covered, text::Lang::Ru) : std::string {}));
}

void aSentenceStatesWhatSounds()
{
    felitronics::test::group ("a sentence states what sounds: the planner's reasons only where its proposal sounds");
    const auto said = [] (const text::Fact& f) { return text::Text::text (f, text::Lang::Ru) + " / " + text::Text::text (f, text::Lang::En); };
    // A sure 55 Hz note, the bass partly in opposite polarity: the machine cuts from the note and folds the bass.
    auto m = measure (Mix (20).tone (55.0, 0.3, 0.2).tone (440.0, 0.1));
    auto& s = *m.s;
    auto v = s.snapshot();
    const double proposed = v.view().plan.hpf.cutoffHz;
    ok (v.view().plan.hpf.cut == HpfCut::Note && v.view().plan.hpf.sounding == Sounding::Proposal && same (v.view().plan.hpf.soundingHz, proposed)
        && PlanText::hpf (v.view().plan.hpf).id == text::FactId::HpfNote
        && v.view().plan.monoBass.verdict == MonoBassVerdict::Partial && v.view().plan.monoBass.sounding == Sounding::Proposal
        && PlanText::monoBass (v.view().plan.monoBass)->id == text::FactId::MonoBassPartial,
        "PRECONDITION: the machine's own devices sound, and its sentences are said: " + said (PlanText::hpf (v.view().plan.hpf)));

    HpfFields<Touched> low; low.fq = 20.0;
    ok (s.apply (command::EditDevice { 3, low }).rejection == Rejection::None, "a person sets the cutoff to 20 Hz");
    v = s.snapshot();
    auto fact = PlanText::hpf (v.view().plan.hpf);
    ok (v.view().plan.hpf.sounding == Sounding::Hand && same (v.view().plan.hpf.soundingHz, 20.0) && same (v.view().plan.hpf.cutoffHz, proposed)
        && v.view().plan.hpf.cut == HpfCut::Note && fact.id == text::FactId::HpfByHand && fact.argCount == 1 && same (fact.args[0].number, 20.0)
        && said (fact).find ("20") != std::string::npos && said (fact).find ('{') == std::string::npos,
        "the finding still holds the proposal (" + std::to_string (proposed) + " Hz from the note) and says a person's 20 Hz sounds; the sentence names 20 Hz and claims nothing of the note: " + said (fact));
    HpfFields<Touched> same20; same20.fq = proposed;
    (void) s.apply (command::EditDevice { 4, same20 });
    v = s.snapshot();
    ok (v.view().plan.hpf.sounding == Sounding::Proposal && PlanText::hpf (v.view().plan.hpf).id == text::FactId::HpfNote,
        "a person's value equal to the proposal: the proposal sounds, and its reasons are true of the sound");
    HpfFields<Touched> steep; steep.slope = 48;
    (void) s.apply (command::EditDevice { 5, steep });
    v = s.snapshot();
    ok (v.view().plan.hpf.sounding == Sounding::Hand && PlanText::hpf (v.view().plan.hpf).id == text::FactId::HpfByHand,
        "another slope at the same cutoff takes another share of the note: a person's");
    HpfFields<Touched> off; off.on = false;
    (void) s.apply (command::EditDevice { 6, off });
    v = s.snapshot();
    fact = PlanText::hpf (v.view().plan.hpf);
    ok (v.view().plan.hpf.sounding == Sounding::Off && fact.id == text::FactId::HpfOff && fact.argCount == 0, "unticked: " + said (fact));

    MonoBassFields<Touched> wide; wide.fq = 200.0;
    ok (s.apply (command::EditDevice { 7, wide }).rejection == Rejection::None, "a person moves the mono-bass crossover to 200 Hz");
    v = s.snapshot();
    auto mono = PlanText::monoBass (v.view().plan.monoBass);
    ok (v.view().plan.monoBass.sounding == Sounding::Hand && same (v.view().plan.monoBass.soundingHz, 200.0) && same (v.view().plan.monoBass.crossoverHz, 120.0)
        && mono && mono->id == text::FactId::MonoBassByHand && same (mono->args[0].number, 200.0),
        "the loss was weighed at 120 Hz: the sentence names the 200 Hz that sounds and no loss — " + (mono ? said (*mono) : std::string {}));
    ok (! PlanText::monoBassPolarity (v.view().plan.monoBass), "and no polarity warning: the bass is not in opposite polarity");
    MonoBassFields<Touched> out; out.on = false;
    (void) s.apply (command::EditDevice { 8, out });
    v = s.snapshot();
    ok (v.view().plan.monoBass.sounding == Sounding::Off && ! PlanText::monoBass (v.view().plan.monoBass),
        "unticked by a person where the machine folds: nothing is said of a loss that does not happen");

    // Opposite polarity: left out by the machine, with its warning — which stays true of the fold a person switches on.
    auto a = measure (Mix (20).tone (55.0, 0.3, -1.0));
    auto before = a.s->snapshot();
    ok (before.view().plan.monoBass.sounding == Sounding::Off && PlanText::monoBass (before.view().plan.monoBass)->id == text::FactId::MonoBassAntiPhase,
        "left out for opposite polarity: the reason is said");
    MonoBassFields<Touched> on; on.on = true;
    (void) a.s->apply (command::EditDevice { 3, on });
    const auto after = a.s->snapshot();
    ok (after.view().plan.monoBass.sounding == Sounding::Proposal && after.view().plan.monoBass.againstMachine
        && PlanText::monoBass (after.view().plan.monoBass)->id == text::FactId::MonoBassAntiPhase && ! PlanText::monoBassPolarity (after.view().plan.monoBass),
        "switched on against the machine at its own crossover: the warning stands, once");
    // ...and at a crossover of the person's: the sentence names it, and the polarity warning stands beside it.
    MonoBassFields<Touched> moved; moved.fq = 200.0;
    (void) a.s->apply (command::EditDevice { 4, moved });
    const auto byHand = a.s->snapshot();
    const auto main = PlanText::monoBass (byHand.view().plan.monoBass), warning = PlanText::monoBassPolarity (byHand.view().plan.monoBass);
    ok (byHand.view().plan.monoBass.sounding == Sounding::Hand && main && main->id == text::FactId::MonoBassByHand
        && warning && warning->id == text::FactId::MonoBassAntiPhase,
        "at a person's 200 Hz against the machine: the by-hand sentence, and the polarity warning beside it — "
        + (warning ? text::Text::text (*warning, text::Lang::Ru) : std::string {}));
    MonoBassFields<Touched> silenced; silenced.on = false;
    (void) a.s->apply (command::EditDevice { 5, silenced });
    ok (! PlanText::monoBassPolarity (a.s->snapshot().view().plan.monoBass)
        && PlanText::monoBass (a.s->snapshot().view().plan.monoBass)->id == text::FactId::MonoBassAntiPhase, "unticked again: the one warning, as the reason it is out");

    // A machine layer kept from a project file that the planner would place otherwise.
    auto f = measure (Mix (20).tone (55.0, 0.3, 0.2).tone (440.0, 0.1));
    std::string project (f.s->exportProject().view());
    const auto section = project.find ("[hpf]"), line = project.find ("fq.machine = ", section);
    if (section != std::string::npos && line != std::string::npos) project.replace (line, project.find ('\n', line) - line, "fq.machine = 47.5");
    ok (f.s->apply (command::ImportProject { 3, project }).rejection == Rejection::None, "PRECONDITION: a project whose machine cutoff is 47.5 Hz imports");
    const auto kept = f.s->snapshot();
    fact = PlanText::hpf (kept.view().plan.hpf);
    ok (kept.view().plan.fromFile && kept.view().plan.hpf.sounding == Sounding::File && same (kept.view().plan.hpf.soundingHz, 47.5)
        && fact.id == text::FactId::HpfKept && same (fact.args[0].number, 47.5), "the file's cutoff sounds, and is named as the file's: " + said (fact));
    (void) f.s->apply (command::AdoptMachine { 4 });
    ok (f.s->snapshot().view().plan.hpf.sounding == Sounding::Proposal, "adopting the planner's layer makes its proposal the sound again");
}

void everyTargetFromOneMeasurement()
{
    felitronics::test::group ("one measured source serves every target: no measurement again, each its own floor, slope, loss and crossover");
    auto m = measure (Mix (20).tone (41.2, 0.3).tone (330.0, 0.1));
    auto& s = *m.s;
    for (unsigned i = 0; i < 2000000 && (s.measurementJob() || s.needlesJob()); ++i) (void) s.step (16);
    const auto before = s.snapshot();
    const auto r = detail::rules();
    bool each = true, again = false;
    for (std::uint16_t row = 0; row < r.rows; ++row)
    {
        const auto target = r.row (row);
        TiltFields<Touched> tilt; tilt.db = 1.0;
        (void) s.apply (command::EditDevice { 10, tilt });
        ok (s.apply (command::SetTarget { 11, target.key }).rejection == Rejection::None, "PRECONDITION: target");
        again = again || s.measurementJob() != 0;
        const auto v = s.snapshot();
        const auto& d = s.project().devices;
        each = each && ! d.tilt.hand.db && d.hpf.machine.slope == target.hpfSlope && d.hpf.machine.on
            && same (v.view().plan.monoBass.crossoverHz, target.monoBass.toDouble()) && same (d.monoBass.machine.fq, target.monoBass.toDouble())
            && (v.view().plan.hpf.cut == HpfCut::Note || v.view().plan.hpf.cut == HpfCut::Floor)
            && v.view().measurements[std::size_t (Analyzer::LowEnd)].key == before.view().measurements[std::size_t (Analyzer::LowEnd)].key;
        if (v.view().plan.hpf.cut == HpfCut::Note)
            each = each && std::abs (coreLoss (d.hpf.machine.fq, target.hpfSlope, Mix::rate, *v.view().plan.hpf.noteHz) - target.noteLossDb.toDouble()) < 1e-6;
    }
    ok (each, "every target: its slope, its crossover, its loss at the note; the person's edit reset by the change");
    ok (! again, "and no measurement ran again");
}
} // namespace

// THE LOWEST OCCUPIED BAND, AS THE LOW END'S READING — published with its sureness, never withheld for it: the shell shows
// "35 Hz, unsure" where the reading is under the margin, and the planner's rule (an unsure band: the floor) is its own.
std::optional<double> lowNumber (const Measured& m, std::string_view name, MeasurementReason* reason = nullptr)
{
    const auto snapshot = m.s->snapshot();
    const auto& r = snapshot.view().measurements[std::size_t (Analyzer::LowEnd)];
    for (const auto& v : r.numbers)
        if (v.name == name)
        {
            if (reason) *reason = v.reason;
            return v.value;
        }
    if (reason) *reason = MeasurementReason::Unsupported;
    return std::nullopt;
}
void theLowestBandWithItsSureness()
{
    felitronics::test::group ("the lowest occupied band is published with its sureness — an unsure one is shown as unsure, not withheld");
    // A steady bass at 55 Hz and, 19 dB under it, a steady D1 (36.7 Hz): on in every frame, but just over the duty line.
    Mix faint (20); faint.tone (55.0, 0.3).tone (36.71, 0.3 * felitronics::core::det::pow10 (-19.0 / 20.0));
    Mix clear (20); clear.tone (55.0, 0.3).tone (36.71, 0.3 * felitronics::core::det::pow10 (-8.0 / 20.0));
    const auto unsure = measure (faint), sure = measure (clear);
    MeasurementReason why {};
    const auto margin = lowNumber (unsure, "lowestOccupiedMarginDb", &why);
    const auto hz = lowNumber (unsure, "lowestOccupiedHz"), flag = lowNumber (unsure, "lowestOccupiedSure");
    ok (margin && *margin < 2.0 && *margin > 0.0 && hz && *hz > 35.0 && *hz < 38.0 && why == MeasurementReason::None,
        "D1 19 dB under the bass: its band is published — " + std::to_string (hz.value_or (0.0)) + " Hz, " + std::to_string (margin.value_or (-1.0))
        + " dB over the duty line (under the 2 dB a sure note stands)");
    ok (flag && same (*flag, 0.0) && lowNumber (unsure, "lowestOccupiedResolved") && same (*lowNumber (unsure, "lowestOccupiedResolved"), 1.0),
        "and says it is unsure, resolved");
    ok (unsure.plan.hpf.cut == HpfCut::Unsure && same (unsure.plan.hpf.cutoffHz, 32.0),
        "the planner's rule is its own: an unsure lowest band gives the floor");
    const auto seen = unsure.s->snapshot().view().observations.lowestLowBand;
    const auto said = ObservationText::fact (ObservationKind::LowestLowBand, seen);
    ok (seen.status == ObservationStatus::Found && seen.doubtful && said && said->id == text::FactId::SourceLowestBandUnsure
        && text::Text::text (*said, text::Lang::Ru).find ("неуверенно") != std::string::npos,
        "the observation shows it, unsure: " + (said ? text::Text::text (*said, text::Lang::Ru) : std::string()));
    const auto clearly = sure.s->snapshot().view().observations.lowestLowBand;
    ok (clearly.status == ObservationStatus::Found && ! clearly.doubtful
        && ObservationText::fact (ObservationKind::LowestLowBand, clearly)->id == text::FactId::SourceLowestBand,
        "and a sure one plainly");
    const auto sureFlag = lowNumber (sure, "lowestOccupiedSure"), sureMargin = lowNumber (sure, "lowestOccupiedMarginDb");
    ok (sureFlag && same (*sureFlag, 1.0) && sureMargin && *sureMargin >= 2.0 && lowNumber (sure, "lowestOccupiedHz"),
        "D1 8 dB under: published and sure (" + std::to_string (sureMargin.value_or (-1.0)) + " dB)");
}

// UNDER 25 Hz (owner, 06.10): «the spectrum from 10 Hz, the lowest note from 25 Hz» — what sounds under 25 Hz is measured
// in the table, and is never the lowest note the high-pass and the report read; a bass above it is found as before.
void underTwentyFiveHzIsTheSpectrumsNotTheNotes()
{
    felitronics::test::group ("under 25 Hz: the table measures from 10 Hz, and what sounds there is never the lowest note");
    const auto note = [] (double hz) { return std::round (12 * std::log2 (hz / 440.0) + 69); };
    const auto bandsOf = [] (const Measured& m)
    {
        std::vector<double> out;
        const auto snapshot = m.s->snapshot();
        for (const auto& a : snapshot.view().measurements[std::size_t (Analyzer::LowEnd)].arrays)
            if (a.name == "bands" && a.columns == 16) out.assign (a.values.begin(), a.values.end());
        return out;
    };
    {
        const auto m = measure (Mix (20).tone (15.43, 0.3));
        const auto bands = bandsOf (m);
        bool noteTop = bands.size() == std::size_t (kBandCount) * 16;
        for (std::size_t b = 0; noteTop && b + 16 <= bands.size(); b += 16)
            noteTop = bands[b] <= kNoteTopMidi || (bands[b + 11] == 0 && bands[b + 12] == 0);
        ok (bands.size() == std::size_t (kBandCount) * 16 && same (bands[0], double (kFirstMidi)) && std::abs (bands[1] - 10.30) < 0.005
            && same (bands[bands.size() - 16], 71.0) && std::abs (bands[bands.size() - 15] - 493.88) < 0.005 && noteTop,
            "the table starts at 10.30 Hz (MIDI 4) and ends at 493.88 Hz (MIDI 71); the bands above 293.66 Hz (MIDI 62) carry no "
            "occupancy: " + std::to_string (bands.size() / 16) + " bands");
        double total = 0, at = 0;
        for (std::size_t b = 0; b + 16 <= bands.size(); b += 16) { total += bands[b + 6]; if (same (bands[b], 11.0)) at = bands[b + 6]; }
        const auto peak = lowNumber (m, "peakMidi");
        ok (total > 0 && at / total > 0.9 && peak && same (*peak, 11.0),
            "a 15.43 Hz tone shows in its own band: " + std::to_string (total > 0 ? at / total : 0.0) + " of the table's energy, the loudest band");
        MeasurementReason why {};
        const auto lowest = lowNumber (m, "lowestOccupiedHz", &why);
        ok (! lowest && why == MeasurementReason::NoSignal,
            "and is no lowest occupied band: nothing from 25 Hz up (" + std::to_string (lowest.value_or (0.0)) + " Hz)");
        ok (m.s->snapshot().view().observations.lowestLowBand.status == ObservationStatus::NotFound, "the report names no lowest band");
        ok (m.plan.hpf.cut == HpfCut::Unsure && ! m.plan.hpf.noteMidi && same (m.plan.hpf.cutoffHz, 32.0), "and the high-pass stands at the floor");
    }
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3).tone (15.43, 0.15).tone (440.0, 0.1));
        ok (m.plan.hpf.cut == HpfCut::Note && m.plan.hpf.noteMidi && same (double (*m.plan.hpf.noteMidi), note (55.0))
            && m.plan.hpf.cutoffHz > 32 && m.plan.hpf.cutoffHz < 55,
            "a bass at 55 Hz over a 15.43 Hz rumble 6 dB under it: its note, as before (" + std::to_string (m.plan.hpf.cutoffHz) + " Hz)");
        const auto lowest = lowNumber (m, "lowestOccupiedMidi");
        ok (lowest && same (*lowest, note (55.0)), "and the lowest occupied band is the bass's");
    }
    {
        const auto m = measure (Mix (20).tone (55.0, 0.3).tone (23.12, 0.15).tone (440.0, 0.1));
        ok (m.plan.hpf.cut == HpfCut::Note && m.plan.hpf.noteMidi && same (double (*m.plan.hpf.noteMidi), note (55.0))
            && m.plan.hpf.cutoffHz > 32 && m.plan.hpf.cutoffHz < 55,
            "a 23.12 Hz rumble under it — under 25 Hz, not 20: the bass's note all the same (" + std::to_string (m.plan.hpf.cutoffHz) + " Hz)");
    }
}

// A MIX WITH NO BASS SAYS "UNSURE" (owner, 07.10): a high tone's leakage, a dither, a noise or a rumble lights the bands on
// both sides of 25 Hz alike, so the lowest band from 25 Hz up is not where the low end starts — the band under it was on —
// and it is no note, never a skip to the band above it. A played bass has nothing on under it and is found as before,
// also 40 dB under a loud 1 kHz tone.
void aMixWithNoBassIsUnsure()
{
    felitronics::test::group ("a mix with no bass is unsure: a band lit by leakage, dither or rumble is not where the low end starts");
    const auto said = [] (const Measured& m)
    {
        const auto sure = lowNumber (m, "lowestOccupiedSure"), midi = lowNumber (m, "lowestOccupiedMidi");
        return " (cut " + std::to_string (int (m.plan.hpf.cut)) + ", note " + std::to_string (m.plan.hpf.noteMidi.value_or (-1))
             + ", lowest band " + std::to_string (midi.value_or (-1)) + " sure " + std::to_string (sure.value_or (-1)) + ")";
    };
    const auto unsure = [&] (const Measured& m, const std::string& what)
    {
        const auto sure = lowNumber (m, "lowestOccupiedSure");
        ok (m.plan.hpf.cut == HpfCut::Unsure && ! m.plan.hpf.noteMidi && same (m.plan.hpf.cutoffHz, 32.0) && (! sure || same (*sure, 0.0)),
            what + ": unsure, the floor" + said (m));
    };
    const auto found = [&] (const Measured& m, int midi, const std::string& what)
    {
        const auto sure = lowNumber (m, "lowestOccupiedSure");
        ok (m.plan.hpf.noteMidi == midi && m.plan.hpf.cut != HpfCut::Unsure && sure && same (*sure, 1.0), what + said (m));
    };
    unsure (measure (Mix (20).tone (1000.0, 0.3)), "a 1 kHz tone alone");
    unsure (measure (Mix (20).tone (1000.0, 0.3).dither (16)), "a 1 kHz tone over a 16-bit dither");
    unsure (measure (Mix (20).pink (0.1)), "pink noise alone at −20 dBFS, a rumble that is all there is");
    // A float tone that is not periodic in the sample grid: its rounding draws real lines into the empty range, 6.3 dB over
    // the background, in a range holding −172 dB of the programme — the range's share vetoes it.
    unsure (measure (Mix (20).tone (997.0, 0.25)), "a 997 Hz tone alone, its float rounding lines in the range");
    {
        const auto m = measure (Mix (4).tone (997.0, 0.25));
        const auto sure = lowNumber (m, "lowestOccupiedSure");
        ok (sure && same (*sure, 0.0), "and 4 s of it: the report's lowest band is not sure, as v0.18.0 read it");
    }
    unsure (measure (Mix (20).tone (1000.0, 0.3).pink (0.001)), "a 1 kHz tone over a rumble at −60 dBFS");
    found (measure (Mix (20).tone (41.2, 0.3)), 28, "E1 41.2 Hz alone: its note");
    found (measure (Mix (20).tone (30.87, 0.3)), 23, "B0 30.87 Hz alone: its note");
    found (measure (Mix (20).tone (1000.0, 0.3).tone (41.2, 0.003)), 28, "E1 40 dB under a 1 kHz tone: its note");
    found (measure (Mix (20).tone (1000.0, 0.3).tone (30.87, 0.003)), 23, "B0 40 dB under a 1 kHz tone: its note");
    found (measure (Mix (20).tone (30.87, 0.03).tone (61.74, 0.19).tone (1000.0, 0.1)), 23,
           "a five-string B0 16 dB under its second harmonic: B0, never the harmonic");
    found (measure (Mix (20).tone (41.2, 0.3).dither (16).pink (0.001)), 28, "E1 over a dither and a rumble: its note");
}

// REAL MIXES KEEP THEIR NOTES (owner, 07.10): v0.19.0's background veto took the lowest note from real mixes — a dense mix's
// lowest note stands −6 to +0.4 dB over the note range's median density — so the planner reads the recorded low end of
// the two demo songs (RealMixLowEnd.h) and names v0.18.0's note on each: D♯1 in Cold Gaze of Eternity (the owner's
// answer of 07.10: kept for now, though the song never goes under E), unsure in Cat in Space.
void realMixesKeepTheirNotes()
{
    felitronics::test::group ("real mixes keep their notes: the two demo songs name v0.18.0's lowest note");
    int same18 = 0, notes = 0;
    std::string wrong;
    for (const auto& mix : kRealMixes)
    {
        Readings r; r.rate = mix.rate; r.hop = mix.hop; r.rangeShare = mix.rangeShare; r.background = mix.background;
        for (auto& v : r.bands) v = 0;
        for (int b = 0; b < kBandCount; ++b) { double* row = r.bands.data() + b * 16; row[0] = kFirstMidi + b; row[1] = midiHz (kFirstMidi + b); row[15] = 1; }
        for (int i = 0; i < mix.bandCount; ++i)
        {
            const auto& band = mix.bands[i];
            double* row = r.bands.data() + (band.midi - kFirstMidi) * 16;
            row[11] = band.count; row[12] = band.duty; row[14] = band.marginDb; row[7] = band.density; row[15] = band.resolved;
        }
        const auto f = plan (r.inputs ("allStreaming", double (mix.frames) / mix.rate)).found.hpf;
        const int named = f.noteMidi.value_or (-1);
        const int expected = mix.v018Note;
        if (named == expected) ++same18;
        else if (wrong.size() < 300) wrong += std::string (" ") + mix.name + ": " + std::to_string (named) + " not " + std::to_string (expected) + ";";
        notes += named >= 0 ? 1 : 0;
    }
    ok (same18 == int (std::size (kRealMixes)), "every real mix names v0.18.0's note (" + std::to_string (same18) + " of "
        + std::to_string (std::size (kRealMixes)) + ", " + std::to_string (notes) + " notes)" + wrong);
    const auto one = [] (std::string_view name)
    {
        for (const auto& mix : kRealMixes) if (std::string_view (mix.name) == name) return &mix;
        return static_cast<const RealMix*> (nullptr);
    };
    const auto* gaze = one ("cold-gaze-of-eternity");
    ok (gaze && gaze->v018Note == 27, "PRECONDITION: Cold Gaze of Eternity, the demo, had D♯1 at v0.18.0");
}

//==============================================================================
// MASTER AS SOON AS THE LOUDNESS AND THE TRUE PEAK ARE KNOWN (owner, 02.10): the devices are placed then; a field whose
// measurement has not ended is the machine's "not measured yet"; a person may edit it; a hidden panel's master waits in
// its job for what its devices read and sounds as one asked after everything ended.

struct Early { std::unique_ptr<Session> s; };
Early loadMix (const Mix& m, const char* target)
{
    Early out { Session::create().session };
    const float* planes[] { m.left.data(), m.right.data() };
    (void) out.s->apply (command::SetTarget { 1, target });
    ok (out.s->apply (command::Load { 2, { planes, 2, m.left.size(), Mix::rate }, {} }).rejection == Rejection::None, "PRECONDITION: the mix loads");
    return out;
}
template <class P> bool pumpUntil (Session& s, P&& done, unsigned limit = 4000000)
{
    for (unsigned i = 0; i < limit; ++i)
    {
        if (done()) return true;
        if (s.step (1).state == StepState::Done && ! done()) return false;
    }
    return done();
}
std::vector<std::uint8_t> wavOf (Session& s)
{
    const auto token = s.pendingMaster();
    const auto plan = s.masterWavPlan (token);
    std::vector<std::uint8_t> bytes (std::size_t (plan.bytes));
    for (std::size_t at = 0; at < bytes.size(); at += 65536u)
    {
        const auto n = std::min<std::size_t> (65536u, bytes.size() - at);
        if (s.copyMasterWav (token, at, std::span<std::uint8_t> (bytes.data() + at, n)) != MasterTransferStatus::Ok) return {};
    }
    return bytes;
}
bool hasFact (const PlanView& plan, Device device, text::FactId id)
{
    for (std::size_t i = 0; i < plan.facts.count; ++i)
        if (plan.facts.items[i].device == device && plan.facts.items[i].fact.id == id) return true;
    return false;
}

void masterAtLoudnessAndPeak()
{
    felitronics::test::group ("the devices are placed once the loudness and the true peak are known; what they still read is \"not measured yet\"");
    const auto mix = Mix (20).tone (55.0, 0.3).tone (440.0, 0.1);
    auto e = loadMix (mix, "allStreaming"); auto& s = *e.s;
    ok (pumpUntil (s, [&] { return s.state() != State::Loaded; }), "PRECONDITION: the first measurement ends");
    auto v = s.snapshot();
    const auto& lowEnd = v.view().measurements[std::size_t (Analyzer::LowEnd)];
    ok (v.view().state == State::Measured1 && v.view().mandatoryMeasurementsReady && lowEnd.status == MeasurementStatus::Pending,
        "Measured1 comes with the loudness and the true peak, before the low end is measured");
    const auto& plan = v.view().plan;
    constexpr std::uint8_t hpfFq = 1u << 1, monoBassOn = 1u << 0;       // fields in the order Project.h writes them
    ok (plan.status == PlanStatus::Pending && ! plan.readOnly && plan.devices.hpf.heldBack == HeldBack::Pending
        && plan.devices.hpf.pending == hpfFq && plan.devices.monoBass.heldBack == HeldBack::Pending
        && plan.devices.monoBass.pending == monoBassOn
        && (plan.devices.hpf.needs & detail::bitOf (Analyzer::LowEnd)) != 0,
        "the high-pass's cutoff and mono bass's tick wait for the low end: not measured yet, and the panel takes edits");
    ok (hasFact (plan, Device::Hpf, text::FactId::DeviceUnmeasured) && hasFact (plan, Device::MonoBass, text::FactId::DeviceUnmeasured),
        "the card's words are the core's fact");
    ok (s.apply (command::SetManual { 3, true }).rejection == Rejection::None
        && s.apply (command::Master { 4 }).rejection == Rejection::PlanPending, "with the panel open the master waits for what the devices read");
    ok (s.apply (command::SetManual { 5, false }).rejection == Rejection::None, "PRECONDITION: the panel hidden again");
    MonoBassFields<Touched> width; width.width = 0.25;
    HpfFields<Touched> slope; slope.slope = 12;
    ok (s.apply (command::EditDevice { 6, width }).rejection == Rejection::None
        && s.apply (command::EditDevice { 7, slope }).rejection == Rejection::None, "a person edits before the machine has measured");
    HpfFields<Mark> back; back.slope = true;
    ok (s.apply (command::RevertEdits { 8, back }).rejection == Rejection::None
        && s.apply (command::EditDevice { 9, slope }).rejection == Rejection::None, "and reverts and edits again");
    ok (pumpUntil (s, [&] { return s.snapshot().view().plan.status == PlanStatus::Ready; }), "the plan ends");
    v = s.snapshot();
    const auto& d = v.view().project.devices;
    ok (v.view().plan.hpf.cut == HpfCut::Note && ! same (d.hpf.machine.fq, 32.0) && d.monoBass.machine.on
        && v.view().plan.devices.hpf.pending == 0 && v.view().plan.devices.monoBass.pending == 0
        && d.monoBass.hand.width && same (*d.monoBass.hand.width, 0.25) && d.hpf.hand.slope && *d.hpf.hand.slope == 12,
        "the machine fills its untouched fields when its measurement ends, and never moves a touched one");
}

void aHiddenMasterAtLoudnessAndPeakSoundsAsLate()
{
    felitronics::test::group ("a hidden panel's master asked at the loudness and the true peak sounds as one asked after everything ended");
    const auto mix = Mix (20).tone (55.0, 0.3).tone (440.0, 0.1);
    auto early = loadMix (mix, "allStreaming"); auto& a = *early.s;
    ok (pumpUntil (a, [&] { return a.state() != State::Loaded; }), "PRECONDITION: the first measurement ends");
    ok (a.snapshot().view().measurements[std::size_t (Analyzer::LowEnd)].status == MeasurementStatus::Pending
        && a.apply (command::Master { 3 }).rejection == Rejection::None, "the master is taken before the low end is measured");
    ok (pumpUntil (a, [&] { return a.job() == 0; }) && a.masters().size() == 1, "and ends");
    auto late = loadMix (mix, "allStreaming"); auto& b = *late.s;
    ok (pumpUntil (b, [&] { return b.state() == State::Measured2 && b.needlesJob() == 0; }), "PRECONDITION: everything measured");
    ok (b.apply (command::Master { 3 }).rejection == Rejection::None && pumpUntil (b, [&] { return b.job() == 0; }), "the late master ends");
    const auto x = a.masters().size() == 1 ? wavOf (a) : std::vector<std::uint8_t> {}, y = wavOf (b);
    ok (! x.empty() && x == y, "the same WAV, byte for byte (" + std::to_string (x.size()) + " and " + std::to_string (y.size())
        + " bytes; pending " + std::to_string (a.pendingMaster().master) + ", " + std::to_string (b.pendingMaster().master) + ")");
    ok (a.masters().size() == 1 && b.masters().size() == 1
        && a.masters()[0].recipe.project.devices.hpf.machine.fq == b.masters()[0].recipe.project.devices.hpf.machine.fq
        && a.masters()[0].recipe.project.devices.monoBass.machine.on, "its recipe carries the machine's measured fields");
}

void theDeviceRunsGoFirst()
{
    felitronics::test::group ("the low-end runs the target's devices read go first; nothing is measured twice");
    const auto mix = Mix (20).tone (55.0, 0.3).tone (440.0, 0.1);
    for (const char* target : { "allStreaming", "lp", "cd" })
    {
        auto e = loadMix (mix, target); auto& s = *e.s;
        std::vector<Analyzer> order;
        std::vector<double> share (kAnalyzers, 0.0);
        double before = 0, fraction = 0;
        bool twice = false;
        for (unsigned i = 0; i < 4000000 && ! (s.state() == State::Measured2 && s.needlesJob() == 0); ++i)
        {
            if (s.step (1).state == StepState::Done) break;
            for (const auto& n : s.events())
            {
                if (n.kind == EventKind::Phase && n.payload.phase.name == PhaseName::Analyzers && n.jobId == s.snapshot().view().measurementJob)
                    fraction = n.payload.phase.fraction;
                if (n.kind != EventKind::Measurement || n.payload.measurement.status == MeasurementStatus::Pending
                    || n.payload.measurement.analyzer == Analyzer::Excursions || n.payload.measurement.analyzer == Analyzer::Loudness
                    || n.payload.measurement.analyzer == Analyzer::Clipping || n.payload.measurement.analyzer == Analyzer::Programme
                    || n.payload.measurement.analyzer == Analyzer::Waveform) continue;
                twice = twice || std::find (order.begin(), order.end(), n.payload.measurement.analyzer) != order.end();
                order.push_back (n.payload.measurement.analyzer);
            }
            if (! order.empty() && share[std::size_t (order.back())] == 0.0 && fraction > before)
            { share[std::size_t (order.back())] = fraction - before; before = fraction; }
        }
        const auto at = [&] (Analyzer a) { return std::size_t (std::find (order.begin(), order.end(), a) - order.begin()); };
        const bool lp = std::string (target) == "lp";
        const auto deviceRuns = lp ? std::max (at (Analyzer::LowEnd), at (Analyzer::LowEnd150)) : at (Analyzer::LowEnd);
        bool first = true;
        for (const auto a : { Analyzer::InfraLow, Analyzer::Forensics, Analyzer::Stereo, Analyzer::Crest, Analyzer::Hum, Analyzer::StereoBursts })
            first = first && deviceRuns < at (a);
        if (! lp) first = first && deviceRuns < at (Analyzer::LowEnd150);
        ok (order.size() == 9 && ! twice && first, std::string (target) + ": the devices' runs first, each analyzer once");
        if (std::string (target) == "cd")
            ok (at (Analyzer::Tempo) < at (Analyzer::LowEnd150) && at (Analyzer::LowEnd) < at (Analyzer::Tempo),
                "cd: the tempo its glue reads goes ahead of the findings, never ahead of the low end its devices read");
        if (! lp && std::string (target) != "cd")
            ok (share[std::size_t (Analyzer::Stereo)] > 0 && share[std::size_t (Analyzer::Crest)] > 10 * share[std::size_t (Analyzer::Stereo)],
                std::string (target) + ": the bar moves by the configured weights (stereo " + std::to_string (share[std::size_t (Analyzer::Stereo)])
                + ", crest " + std::to_string (share[std::size_t (Analyzer::Crest)]) + ")");
    }
    // A change of target while the first low-end run measures: the new target's run follows it, once.
    auto e = loadMix (mix, "allStreaming"); auto& s = *e.s;
    ok (pumpUntil (s, [&] { return s.state() != State::Loaded; }), "PRECONDITION: placed");
    ok (s.apply (command::SetTarget { 3, "lp" }).rejection == Rejection::None, "the target changes to lp while the low end measures");
    ok (pumpUntil (s, [&] { return s.snapshot().view().plan.status == PlanStatus::Ready; })
        && s.snapshot().view().measurements[std::size_t (Analyzer::InfraLow)].status == MeasurementStatus::Pending
        && s.snapshot().view().measurements[std::size_t (Analyzer::LowEnd150)].status == MeasurementStatus::Ready,
        "its crossover's run ends before any finding is measured");
}

void noFileCarriesAPlaceholder()
{
    felitronics::test::group ("a file never carries a field not measured yet, and an import never meets an incomplete machine");
    const auto mix = Mix (20).tone (55.0, 0.3).tone (440.0, 0.1);
    auto e = loadMix (mix, "allStreaming"); auto& s = *e.s;
    ok (pumpUntil (s, [&] { return s.state() != State::Loaded; }) && s.snapshot().view().plan.devices.hpf.pending != 0,
        "PRECONDITION: placed, the high-pass's cutoff not measured yet");
    ok (s.exportProjectBytes().rejection == Rejection::PlanPending && s.exportProject().rejection == Rejection::PlanPending,
        "an export waits while a device field is not measured yet: no placeholder is written as the machine's opinion");
    ok (pumpUntil (s, [&] { return s.snapshot().view().plan.status == PlanStatus::Ready; })
        && s.snapshot().view().measurements[std::size_t (Analyzer::LowEnd150)].status == MeasurementStatus::Pending,
        "PRECONDITION: allStreaming's plan ready, the 150 Hz run lp's mono bass reads still to come");
    const auto saved = s.exportProject();
    ok (saved.rejection == Rejection::None, "with nothing pending the project exports");
    std::string text (saved.view());
    const auto at = text.find ("\"allStreaming\"");
    ok (at != std::string::npos, "PRECONDITION: the file names its target");
    text.replace (at, std::string ("\"allStreaming\"").size(), "\"lp\"");
    const auto revision = s.revision();
    ok (s.importProject (3, text).rejection == Rejection::PlanPending && s.revision() == revision && s.project().target != detail::rules().find ("lp").value_or (0),
        "an lp file while lp's run is still to come: refused whole, nothing changed");
    ok (pumpUntil (s, [&] { return s.snapshot().view().measurements[std::size_t (Analyzer::LowEnd150)].status != MeasurementStatus::Pending; })
        && s.importProject (4, text).rejection == Rejection::None, "once it has ended the same file opens");
}

int main()
{
    theSureLowestNote();
    theLowestNoteFromTwentyFiveHz();
    theCutoffOnTheChainsResponse();
    quietAndUnmeasured();
    theLossOfTheLowEnd();
    whereTheBassSounds();
    throughThePump();
    aRareLowNoteIsNotSkipped();
    aPersonsKnobs();
    aPieceLongerThanTheReading();
    aSentenceStatesWhatSounds();
    everyTargetFromOneMeasurement();
    theLowestBandWithItsSureness();
    underTwentyFiveHzIsTheSpectrumsNotTheNotes();
    aMixWithNoBassIsUnsure();
    realMixesKeepTheirNotes();
    masterAtLoudnessAndPeak();
    aHiddenMasterAtLoudnessAndPeakSoundsAsLate();
    theDeviceRunsGoFirst();
    noFileCarriesAPlaceholder();
    return felitronics::test::report();
}
