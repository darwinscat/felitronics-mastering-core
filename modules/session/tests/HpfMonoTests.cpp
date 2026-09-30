// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE HIGH-PASS AND MONO BASS (owner decisions 3.2–3.5): the sure lowest note at each of its four boundaries and the short
// programme; the cutoff on the chain's own response — the loss the target allows at the note, or the floor or the top it
// stops at; always on, a quiet input included; the loss of the low end folded to mono at 1 and 3 dB exactly, above them,
// where the bass does not sound and where it is too little; a person's mono bass against the machine; the knobs' domains
// (18 and 36 dB/oct, a cutoff above 50 Hz, not rounded); and, through the real pump on synthetic mixes, a detector that
// never errs upward and a loss that tells normal, partial and inverted bass apart — once for every target.

#include "DeclaredBudget.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Grid.h"
#include "Planner.h"
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
namespace budget = felitronics::session::testing;
namespace eq = felitronics::eq;

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return detail::same (a, b); }
double midiHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

// A LOW-END READING as the first phase publishes it — its band table (MIDI 16…62, the run's geometry) and its 10 ms blocks
// — owned here, with the loudness reading beside it, laid out by Analyzer for the planner.
struct Readings
{
    std::uint32_t rate = 48000;
    double hop = 65536;
    std::vector<double> bands = std::vector<double> (47 * 16, 0.0);
    std::vector<double> blocks;
    MeasurementValue lowNumbers[4] {};
    MeasurementArray lowArrays[2] {};
    MeasurementValue loudness[2] {};
    MeasurementResult results[kAnalyzers] {};
    Readings()
    {
        for (int b = 0; b < 47; ++b)
        {
            double* row = bands.data() + b * 16;
            row[0] = 16 + b; row[1] = midiHz (16 + b); row[15] = 1;
        }
    }
    // A band occupied `duty` of the frames, `count` frames, `margin` dB above the duty line.
    void occupy (int midi, double duty, double count, double margin)
    {
        double* row = bands.data() + (midi - 16) * 16;
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
        lowArrays[0] = { "blocks", { 0, 480, std::uint64_t (seconds * rate), rate }, 8, blocks.size() / 8, blocks.size() / 8, true, blocks };
        lowArrays[1] = { "bands", { 0, 0, std::uint64_t (seconds * rate), rate }, 16, 47, 47, true, bands };
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
    felitronics::test::group ("the sure lowest note: 2 dB over the line, 10 % of the frames, above 20 Hz, 3 s in all; not sought under 10 s");
    const auto cut = [] (Readings& r, double seconds = 60) { return plan (r.inputs ("allStreaming", seconds)).found.hpf; };
    {
        Readings r; r.occupy (33, 0.10, 20, 2.0);
        const auto f = cut (r);
        ok (f.cut == HpfCut::Note && f.noteMidi == 33 && same (*f.noteHz, midiHz (33)), "2 dB over the line and 10 % of the frames: sure");
        Readings under; under.occupy (33, 0.10, 20, std::nextafter (2.0, 0.0));
        ok (cut (under).cut == HpfCut::Unsure, "a hair under 2 dB: not sure — the floor");
        Readings rare; rare.occupy (33, std::nextafter (0.10, 0.0), 20, 6); rare.occupy (45, 0.5, 60, 6);
        const auto g = cut (rare);
        ok (g.noteMidi == 45, "a hair under 10 % of the frames is not occupied: the next occupied band is the lowest note");
        Readings unsureLowest; unsureLowest.occupy (33, 0.2, 20, 1.0); unsureLowest.occupy (45, 0.5, 60, 6);
        ok (cut (unsureLowest).cut == HpfCut::Unsure, "the lowest occupied band not sure: the floor — never a higher band, which would cut music");
    }
    {
        Readings r; r.occupy (16, 0.5, 40, 6);
        r.bands[1] = 20.0;
        ok (cut (r).cut == HpfCut::Unsure, "a band at 20 Hz exactly is not above 20 Hz: the floor");
        r.bands[1] = std::nextafter (20.0, 30.0);
        ok (cut (r).cut != HpfCut::Unsure, "a hair above 20 Hz is");
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

void theCutoffOnTheChainsResponse()
{
    felitronics::test::group ("the cutoff: the loss the target allows at the note on the chain's own response, the 32 Hz floor, the 50 Hz top");
    const auto r = detail::rules();
    bool exact = true, stops = true, floors = true, unrounded = true;
    std::string worst;
    for (std::uint16_t row = 0; row < r.rows; ++row)
    {
        const auto target = r.row (row);
        const double loss = target.noteLossDb.toDouble(), floor = target.hpfFloor.toDouble();
        const int slope = target.hpfSlope;
        floors = floors && same (floor, 32.0);
        for (const std::uint32_t rate : { 44100u, 48000u, 96000u })
            for (int midi = 16; midi <= 62; ++midi)
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
            }
    }
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
    ok (text::Text::text (PlanText::hpf (top), text::Lang::Ru).find ("упёрся") != std::string::npos, "and the report says it stopped there");
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
    const auto spent = budget::spend ([&] { (void) plan (loud.inputs ("allStreaming", 60)); });
    ok (spent.requests == 0, "the planner asks the heap for nothing");
}

//==============================================================================
// THROUGH THE PUMP — synthetic mixes, measured as a source is.

struct Mix
{
    static constexpr std::uint32_t rate = 48000;
    std::vector<float> left, right;
    explicit Mix (double seconds) : left (std::size_t (seconds * rate)), right (left.size()) {}
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
Measured measure (const Mix& m, const char* target = "allStreaming")
{
    Measured out { Session::create().session, {} };
    auto& s = *out.s;
    const float* planes[] { m.left.data(), m.right.data() };
    (void) s.apply (command::SetTarget { 1, target });
    ok (s.apply (command::Load { 2, { planes, 2, m.left.size(), Mix::rate }, {} }).rejection == Rejection::None, "PRECONDITION: the mix loads");
    for (unsigned i = 0; i < 2000000 && s.state() == State::Loaded; ++i) (void) s.step (16);
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
    HpfFields<Touched> edge; edge.fq = std::nextafter (24000.0, 0.0); edge.slope = 24;
    ok (s.apply (command::EditDevice { 7, edge }).rejection == Rejection::None, "a hair under half the rate: taken");
    // The band written is the hand's cutoff and slope, exactly, and the curve is drawn from it.
    detail::EqStage stage; detail::writeEq (s.project().devices, detail::rules(), stage);
    ok (same (stage.bands[detail::eqBand (Device::Hpf)].lanes[0].freq, edge.fq.value_or (0))
        && stage.bands[detail::eqBand (Device::Hpf)].lanes[0].slope == 24, "the high-pass band is what the knob says, bit for bit");
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

int main()
{
    theSureLowestNote();
    theCutoffOnTheChainsResponse();
    quietAndUnmeasured();
    theLossOfTheLowEnd();
    whereTheBassSounds();
    throughThePump();
    aPersonsKnobs();
    everyTargetFromOneMeasurement();
    return felitronics::test::report();
}
