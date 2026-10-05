// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// GLUE AND SATURATION FROM THE NORMALISED INPUT (owner decisions 3.8, 3.8а, 3.9). The glue's knob, "up to N dB", is the
// loss the core's own static curve takes at the input's short-term P95 — one smooth formula over 0…6, no step and no
// clamp inside the domain; the machine sets it only on cd, 2.6, never above 3; without a P95 it is unavailable to the
// machine and to a person alike, with its reason, the person's value kept and the master not blocked; its release
// follows a tempo measured with confidence, 120 BPM otherwise, inside 50…500 ms. The saturation is the chain's tanh
// stage, never the machine's, its drive counted from the input's true peak. Both read the input brought to the reference
// loudness by ONE gain the landing search adds: one mix exported at several levels gets the same compressor and the same
// shaper, and sounds the same. What each did is measured on its own stage and reported — never the chain's true peak.

#include "Devices.h"
#include "Chain.h"
#include "Dynamics.h"
#include "Limiter.h"
#include "Grid.h"
#include "Planner.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Text.h>
#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/tempo/TempoDetector.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace mastering = felitronics::mastering;
namespace dynamics = felitronics::dynamics;

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return detail::same (a, b); }
bool sameF (float a, float b) { return detail::same (double (a), double (b)); }
bool near (double a, double b, double by) { return std::fabs (a - b) <= by; }

// The tempo as a fixture hands it to the planner.
enum class Tempo { Pending, Cancelled, Unavailable, High, Medium };

// The planner's inputs on faked readings: the loudness and the true peak, the short-term P95 where there is one, and
// the tempo.
struct Faked
{
    MeasurementValue loudness[2], programme[1], tempo[2];
    MeasurementResult results[kAnalyzers] {};
    detail::PlanInputs in;
    Faked (std::string_view target, double lufs, double peak, std::optional<double> p95, Tempo t = Tempo::High, double bpm = 120.0)
    {
        loudness[0] = { "integratedLufs", lufs, MeasurementReason::None, 0 };
        loudness[1] = { "truePeakDb", peak, MeasurementReason::None, 0 };
        programme[0] = { "shortTermP95", p95, p95 ? MeasurementReason::None : MeasurementReason::TooShort, 0 };
        tempo[0] = { "headlineBpm", bpm, MeasurementReason::None, 0 };
        tempo[1] = { "headlineLabel", double (t == Tempo::High ? felitronics::tempo::ConfidenceLabel::High : felitronics::tempo::ConfidenceLabel::Medium),
                     MeasurementReason::None, 0 };
        for (std::size_t i = 0; i < kAnalyzers; ++i)
        { results[i].analyzer = Analyzer (i); results[i].status = MeasurementStatus::Unavailable; results[i].reason = MeasurementReason::NotImplemented; }
        auto ready = [&] (Analyzer a, std::span<const MeasurementValue> numbers)
        {
            auto& r = results[std::size_t (a)];
            r.status = MeasurementStatus::Ready; r.reason = MeasurementReason::None; r.numbers = numbers;
        };
        ready (Analyzer::Loudness, loudness);
        if (p95) ready (Analyzer::Programme, programme);
        auto& tr = results[std::size_t (Analyzer::Tempo)];
        switch (t)
        {
            case Tempo::High:
            case Tempo::Medium:      ready (Analyzer::Tempo, tempo); break;
            case Tempo::Pending:     tr.status = MeasurementStatus::Pending; tr.reason = MeasurementReason::Pending; break;
            case Tempo::Cancelled:   tr.status = MeasurementStatus::Cancelled; tr.reason = MeasurementReason::Cancelled; break;
            case Tempo::Unavailable: tr.status = MeasurementStatus::Unavailable; tr.reason = MeasurementReason::TooShort; break;
        }
        // A target the rules do not hold is the fixture's own error, said here — not an empty optional read as a row
        // (gcc 14 on arm64 read one as a row past the table, and the config read trapped).
        in.rules = detail::rules();
        const auto row = in.rules.find (target);
        felitronics::test::ok (row.has_value(), "PRECONDITION: the rules hold the target " + std::string (target));
        in.row = row.value_or (std::uint16_t (0)); in.channels = 2; in.sampleRate = 48000; in.frames = 48000 * 60;
        in.measurements = results;
    }
    Faked (const Faked&) = delete;
    // The machine's layer on these inputs, and a person's glue or saturation over it.
    Devices machine() const
    {
        Devices d; DevicePlans plans; detail::PlanFindings found;
        detail::propose (in, d, plans, found);
        return d;
    }
    Devices glued (double upToDb) const { auto d = machine(); d.glue.hand.on = true; d.glue.hand.upToDb = upToDb; return d; }
    Devices shaped (double drive, double mix = 1.0) const
    {
        auto d = machine();
        d.saturation.hand.on = true; d.saturation.hand.drive = drive; d.saturation.hand.mix = mix;
        return d;
    }
};

// What the core's own static curve takes at the P95 with a glue's values: the gain computer a Compressor runs, the
// threshold where the finding put it.
double coreLossAt (const GlueFinding& f, double p95Db)
{
    dynamics::GainComputer computer;
    computer.setMode (dynamics::Mode::DownCompress);
    computer.setThresholdDb (*f.thresholdDb);
    computer.setRatio (*f.ratio);
    computer.setKneeDb (*f.kneeDb);
    computer.setRangeDb (60.0);
    return -computer.deltaDb (p95Db);
}

// A MIX A TEMPO AND A P95 CAN BE MEASURED ON: a kick every half second (120 BPM), a pad that swells in the second half,
// the right channel a little apart. Twelve seconds unless told; its peak under 0.45, so a copy twice as loud stays under
// full scale.
struct Mix
{
    static constexpr unsigned rate = 48000;
    unsigned frames;
    std::vector<float> left, right;
    const float* planes[2] { nullptr, nullptr };
    explicit Mix (float scale = 1.0f, unsigned seconds = 12) : frames (rate * seconds), left (frames), right (frames)
    {
        namespace det = felitronics::core::det;
        for (unsigned i = 0; i < frames; ++i)
        {
            const double t = double (i) / rate, beat = double (i % (rate / 2)) / rate;
            const double kick = 0.30 * det::exp2 (-beat / 0.04) * det::sin (2 * kPi * 60.0 * beat)
                              + (beat < 0.004 ? 0.10 * (double ((i * 2654435761u) >> 16 & 0xffffu) / 32768.0 - 1.0) : 0.0);
            const double swell = t < 6.0 ? 0.6 : 1.0;
            const double pad = swell * (0.05 * det::sin (2 * kPi * 220.0 * t) + 0.04 * det::sin (2 * kPi * 331.0 * t));
            left[i] = scale * float (kick + pad);
            right[i] = scale * float (kick + swell * (0.05 * det::sin (2 * kPi * 220.0 * t + 0.4) + 0.04 * det::sin (2 * kPi * 331.0 * t + 1.1)));
        }
        planes[0] = left.data(); planes[1] = right.data();
    }
    Mix (const Mix&) = delete;
    command::Load load (CommandId id) const { return { id, { planes, 2, frames, rate }, { "mix.wav", rate, true, 24 } }; }
};

// A session with the mix measured to the end, on `target`.
std::unique_ptr<Session> measured (const Mix& mix, const char* target)
{
    auto s = Session::create().session;
    (void) s->apply (command::SetTarget { 1, target });
    ok (s->apply (mix.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads");
    for (unsigned i = 0; i < 4000000 && (s->measurementJob() || s->needlesJob()); ++i) (void) s->step (16);
    ok (s->state() == State::Measured2 && s->snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: measured, the plan ready");
    return s;
}
// The master's job to its end; the facts it published.
std::vector<text::Fact> finish (Session& s)
{
    std::vector<text::Fact> facts;
    for (unsigned i = 0; i < 4000000 && s.job() != 0; ++i)
    {
        (void) s.step (16);
        for (const auto& e : s.events()) if (e.kind == EventKind::Fact) facts.push_back (e.payload.fact.view());
    }
    return facts;
}
// The planner's inputs as the session holds them: its own measurements, read through a snapshot the caller keeps.
detail::PlanInputs inputsOf (const Session& s, const Snapshot& snapshot)
{
    detail::PlanInputs in;
    in.rules = detail::rules(); in.row = s.project().target; in.targetEdit = s.project().targetEdit;
    in.channels = s.source().channels; in.sampleRate = s.source().sampleRate; in.frames = s.source().frames;
    in.measurements = snapshot.view().measurements;
    return in;
}

// THE TWO STAGES ALONE, on the mix: the compressor and the soft clipper of a chain with nothing else, the input brought
// to the reference by `gainDb` — the one gain the landing search adds. Its output, the compressor's gain reduction frame
// by frame, and the clipper's peaks.
struct Staged
{
    std::vector<float> left, right, gr;
    bool counted = false;
    mastering::ClipperPeaks peaks {};
    double grMax = 0.0, grP95 = 0.0;
};
mastering::MasteringChainConfig twoStages (bool limiter = false)
{
    mastering::MasteringChainConfig topology;
    topology.eq = topology.dither = false;
    topology.compressor = topology.clipper = true;
    topology.limiter = limiter;
    topology.compressorLookaheadMs = 0.0;
    return topology;
}
Staged stage (const Mix& mix, mastering::MasteringChainParams params, double gainDb, double loudShare)
{
    const auto topology = twoStages();
    params.inputGainDb += gainDb;
    mastering::MasteringChain chain;
    chain.setParams (params);
    felitronics::test::run (chain.prepare (double (Mix::rate), 2, topology));
    const int latency = chain.latencySamples(), block = 1024;
    const int total = int (mix.frames) + latency;
    Staged out;
    out.left.resize (std::size_t (total)); out.right.resize (std::size_t (total)); out.gr.resize (std::size_t (total + block));
    int written = 0;
    for (int at = 0; at < total; at += block)
    {
        const int n = std::min (block, total - at);
        for (int i = 0; i < n; ++i)
        {
            const bool inside = at + i < int (mix.frames);
            out.left[std::size_t (at + i)] = inside ? mix.left[std::size_t (at + i)] : 0.0f;
            out.right[std::size_t (at + i)] = inside ? mix.right[std::size_t (at + i)] : 0.0f;
        }
        float* io[] { out.left.data() + at, out.right.data() + at };
        mastering::MasteringChainTaps taps;
        taps.compressorGrDb = out.gr.data() + written; taps.frameCapacity = int (out.gr.size()) - written;
        felitronics::test::run (chain.process (io, 2, n, taps));
        written += taps.framesWritten;
    }
    out.gr.resize (std::size_t (written));
    out.counted = chain.clipperPeaks (loudShare, out.peaks);
    // The gain reduction as the report states it: the largest sample, and the P95 of the 4 ms windows' means.
    std::vector<double> windows;
    const std::size_t window = 192;
    for (std::size_t at = 0; at < out.gr.size(); at += window)
    {
        double sum = 0; const std::size_t end = std::min (out.gr.size(), at + window);
        for (std::size_t i = at; i < end; ++i) { sum += std::fabs (double (out.gr[i])); out.grMax = std::max (out.grMax, std::fabs (double (out.gr[i]))); }
        windows.push_back (sum / double (end - at));
    }
    std::sort (windows.begin(), windows.end());
    if (! windows.empty()) out.grP95 = windows[std::min (windows.size() - 1, std::size_t (std::ceil (0.95 * double (windows.size()))) - 1)];
    return out;
}
double cutDb (double quietGain, double ratio) { return 20.0 * felitronics::core::det::log10 (quietGain / ratio); }
double loudShare() { return config::Config::load().config.engine.saturation.cutLoudShare; }

//==============================================================================

void theCurve()
{
    felitronics::test::group ("the glue's knob: the loss at the P95 on the core's own curve, one smooth formula over 0…6");
    const auto r = detail::rules();
    const Faked f ("allStreaming", -18.0, -3.0, -12.0);
    // The loud places on the detector's scale: the P95 and the calibration over it.
    const double over = config::Config::load().config.engine.glue.detectorOverP95Db, loud = -12.0 + over;
    bool exact = true, placed = true, dot = true;
    for (const double n : { 0.5, 1.25, 2.6, 3.0, 6.0 })
    {
        const auto found = detail::glueFinding (f.in, f.glued (n));
        // The transfer curve's point the page draws: the P95 on the detector's scale, where the curve takes the knob.
        dot = dot && found.p95DetectorDb && same (*found.p95DetectorDb, loud) && near (coreLossAt (found, *found.p95DetectorDb), n, 1e-9)
            && same (*found.thresholdDb, *found.p95DetectorDb + detail::glueFor (r, n).threshOffsetDb);
        const double loss = found.state == GlueState::Active ? coreLossAt (found, loud) : -1.0;
        exact = exact && found.state == GlueState::Active && same (found.upToDb, n) && near (loss, n, 1e-9);
        placed = placed && found.thresholdDb && same (*found.thresholdDb, -12.0 + over + detail::glueFor (r, n).threshOffsetDb);
        std::printf ("    up to %.2f dB: ratio %.4f, threshold %.3f dB under the loud places, knee %.3f dB, attack %.3f ms, release %.2f ms at 120 BPM; the core's curve takes %.9f dB\n",
            n, *found.ratio, loud - *found.thresholdDb, *found.kneeDb, *found.attackMs, found.releaseMs.value_or (0.0), loss);
    }
    ok (exact, "up to 0.5 / 1.25 / 2.6 / 3 / 6 dB: the core's gain computer takes exactly that at the loud places, within 1e-9 dB");
    ok (placed && same (over, 1.5), "the loud places stand the calibrated 1.5 dB over the short-term P95: the threshold is P95 + 1.5 + the travel's offset, to the bit");
    ok (dot, "p95DetectorDb is that level, P95 + 1.5, the threshold's own base: the core's static curve takes upToDb there, within 1e-9 dB");

    const auto out = detail::glueFinding (f.in, f.glued (0.0));
    auto unticked = f.glued (2.0); unticked.glue.hand.on = false;
    ok (out.state == GlueState::Out && detail::glueFinding (f.in, unticked).state == GlueState::Out
        && detail::glueFinding (f.in, f.machine()).state == GlueState::Out,
        "at 0 dB, unticked, and as the machine leaves it off cd, the compressor is out of the chain");

    // SMOOTH: every value moves one way along the knob, in steps that never jump, and nothing changes at 3 — the
    // slider's right end is a hint, not a seam. No value is rounded: a hair on the knob moves every one of them.
    const auto limits = config::Config::load().config.engine.compressor;
    constexpr double h = 0.01;
    bool oneWay = true, even = true, inside = true;
    double worstKink = 0.0;
    detail::GlueCurve before = detail::glueFor (r, h), at = detail::glueFor (r, 2 * h);
    const auto field = [] (const detail::GlueCurve& c, int i)
    { return i == 0 ? c.ratio : i == 1 ? c.threshOffsetDb : i == 2 ? c.kneeDb : i == 3 ? c.attackMs : i == 4 ? c.divisor : c.travel; };
    for (int step = 3; step <= 600; ++step)
    {
        const auto after = detail::glueFor (r, step * h);
        for (int i = 0; i < 6; ++i)
        {
            const double d1 = field (at, i) - field (before, i), d2 = field (after, i) - field (at, i);
            oneWay = oneWay && d1 * d2 > 0.0;
            // From a quarter of a dB on: towards 0 the travel runs as the root of the knob — steeper, and still one line.
            if (step > 25) even = even && std::fabs (d2 - d1) <= 0.05 * std::fabs (d1);
            if (step - 1 >= 290 && step - 1 <= 310) worstKink = std::max (worstKink, std::fabs (d2 - d1) / std::fabs (d1));
        }
        inside = inside && after.threshOffsetDb > limits.limitThreshOffset.min && after.threshOffsetDb < limits.limitThreshOffset.max
            && after.kneeDb > limits.limitKnee.min && after.kneeDb < limits.limitKnee.max
            && after.attackMs > limits.limitAttack.min && after.attackMs < limits.limitAttack.max;
        before = at; at = after;
    }
    ok (oneWay, "ratio, threshold, knee, attack, release divisor and the travel each move one way over the whole 0…6");
    ok (even, "in even steps from 0.25 dB on: no step differs from its neighbour by more than a twentieth — no stair, no quantising");
    ok (worstKink < 0.02, "nothing happens at 3 dB: the steps either side of it differ by " + std::to_string (100 * worstKink) + " %");
    ok (inside, "and no value reaches a clamp of [compressor.limits] inside the domain");
    const auto a = detail::glueFor (r, 2.6), b = detail::glueFor (r, 2.6001);
    ok (b.ratio > a.ratio && b.threshOffsetDb < a.threshOffsetDb && b.kneeDb < a.kneeDb && b.attackMs < a.attackMs && b.divisor > a.divisor,
        "a ten-thousandth of a dB on the knob moves every value: nothing is rounded");
}

void theMachine()
{
    felitronics::test::group ("the machine: glue on cd alone, 2.6 dB, never above 3; saturation never");
    const auto r = detail::rules();
    const auto engine = config::Config::load().config.engine;
    bool glue = true, saturation = true;
    for (std::uint16_t row = 0; row < r.rows; ++row)
    {
        const auto target = r.row (row);
        const Faked f (target.key, -14.0, -1.0, -10.0);
        Devices d; DevicePlans plans; detail::PlanFindings found;
        detail::propose (f.in, d, plans, found);
        const bool cd = target.key == "cd";
        glue = glue && d.glue.machine.on == cd && same (d.glue.machine.upToDb, cd ? 2.6 : 0.0) && d.glue.machine.upToDb <= engine.glue.knobMaxDb
            && plans.glue.heldBack == HeldBack::None && (plans.glue.target != 0) == cd
            && (detail::glueFinding (f.in, d).state == GlueState::Active) == cd;
        saturation = saturation && ! d.saturation.machine.on && same (d.saturation.machine.drive, 0.0)
            && plans.saturation.target == 0 && plans.saturation.measured == 0 && ! detail::saturationFinding (f.in, d).active;
    }
    ok (glue, "glue: ticked at 2.6 dB on cd, off at 0 everywhere else — and nowhere above the slider's 3 dB");
    ok (saturation, "saturation: off at 0 dB on every target — a taste of the manual mode");
    ok (same (engine.glue.knobMaxDb, 3.0) && same (engine.glue.domain.max, 6.0) && same (engine.glue.whenTickedUpToDb, 0.5),
        "the slider runs to 3, the core takes 6, a tick starts at 0.5");
    const Faked quiet ("cd", -56.0, -40.0, -50.0);
    Devices d; DevicePlans plans; detail::PlanFindings found;
    detail::propose (quiet.in, d, plans, found);
    ok (! d.glue.machine.on && plans.glue.heldBack == HeldBack::Quiet, "an input too quiet to measure: cd's glue is left off, and says why");
}

void aPersonsKnob()
{
    felitronics::test::group ("a person's glue: a tick starts at 0.5 dB; 0…6 as written; kept with the panel hidden, saved and imported");
    const Mix mix;
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    ok (! s.project().manual && s.snapshot().view().plan.glue.state == GlueState::Out, "PRECONDITION: the panel hidden, the glue out");
    GlueFields<Touched> tick; tick.on = true;
    ok (s.apply (command::EditDevice { 3, tick }).rejection == Rejection::None, "the glue is ticked with the panel hidden");
    auto v = s.snapshot();
    ok (v.view().plan.glue.state == GlueState::Active && same (v.view().plan.glue.upToDb, 0.5) && ! s.project().devices.glue.hand.upToDb
        && v.view().plan.devices.glue.on && v.view().plan.devices.glue.tick == TickFrom::Hand,
        "a tick without a knob compresses at [glue] whenTicked, 0.5 dB — and the project keeps the knob untouched");
    bool taken = true;
    for (const double n : { 0.0, 0.5, 1.25, 2.6, 3.0, 1.2345, 4.5, 6.0 })
    {
        GlueFields<Touched> knob; knob.upToDb = n;
        taken = taken && s.apply (command::EditDevice { 4, knob }).rejection == Rejection::None && same (*s.project().devices.glue.hand.upToDb, n);
        const auto now = s.snapshot();
        taken = taken && same (now.view().plan.glue.upToDb, n) && now.view().plan.glue.state == (n > 0 ? GlueState::Active : GlueState::Out);
    }
    ok (taken, "0, 0.5, 1.25, 2.6, 3, 1.2345, 4.5 and 6 dB are each taken as written — past the slider's travel too — and 0 takes the compressor out");
    GlueFields<Touched> over; over.upToDb = 6.01;
    GlueFields<Touched> under; under.upToDb = -0.01;
    ok (s.apply (command::EditDevice { 5, over }).rejection == Rejection::OutOfDomain && s.apply (command::EditDevice { 6, under }).rejection == Rejection::OutOfDomain
        && same (*s.project().devices.glue.hand.upToDb, 6.0), "6.01 and −0.01 dB are refused, the value kept");

    GlueFields<Touched> glue; glue.upToDb = 4.5;
    SaturationFields<Touched> sat; sat.on = true; sat.drive = 6.0; sat.mix = 0.5;
    ok (s.apply (command::EditDevice { 7, glue }).rejection == Rejection::None && s.apply (command::EditDevice { 8, sat }).rejection == Rejection::None,
        "PRECONDITION: glue 4.5 dB, saturation 6 dB at mix 0.5");
    const auto hidden = s.snapshot();
    ok (hidden.view().plan.glue.state == GlueState::Active && same (hidden.view().plan.glue.upToDb, 4.5) && hidden.view().plan.glue.releaseMs
        && hidden.view().plan.saturation.active && same (hidden.view().plan.saturation.knobDb, 6.0),
        "with the panel hidden both sound as the person set them");
    (void) s.apply (command::SetManual { 9, true });
    const auto shown = s.snapshot();
    ok (shown.view().plan.key == hidden.view().plan.key && same (*shown.view().plan.glue.ratio, *hidden.view().plan.glue.ratio)
        && same (*shown.view().plan.saturation.driveDb, *hidden.view().plan.saturation.driveDb), "and showing the panel changes nothing");
    const auto saved = s.exportProject();
    auto copy = measured (mix, "allStreaming");
    ok (copy->apply (command::ImportProject { 10, saved.view() }).rejection == Rejection::None, "PRECONDITION: the saved project imports");
    const auto& d = copy->project().devices;
    ok (d.glue.hand.on && *d.glue.hand.on && same (*d.glue.hand.upToDb, 4.5) && *d.saturation.hand.on && same (*d.saturation.hand.drive, 6.0)
        && same (*d.saturation.hand.mix, 0.5)
        && same (*copy->snapshot().view().plan.glue.ratio, *hidden.view().plan.glue.ratio), "a saved project keeps them, and they sound the same");
    ok (s.apply (command::SetTarget { 11, "cd" }).rejection == Rejection::None && ! s.project().devices.glue.hand.upToDb
        && ! s.project().devices.saturation.hand.on && same (s.snapshot().view().plan.glue.upToDb, 2.6),
        "a change of target resets them: cd's own 2.6 dB");
}

void withoutAP95()
{
    felitronics::test::group ("without a P95 the glue is unavailable — to the machine and to a person — with its reason; the master is not blocked");
    const Faked f ("cd", -18.0, -3.0, std::nullopt);
    Devices machine; DevicePlans plans; detail::PlanFindings found;
    detail::propose (f.in, machine, plans, found);
    ok (! machine.glue.machine.on && same (machine.glue.machine.upToDb, 2.6) && plans.glue.heldBack == HeldBack::Unmeasured
        && detail::glueFinding (f.in, machine).state == GlueState::Out,
        "the machine: cd's glue is left off — no P95 to stand its threshold on — and says so (Unmeasured)");
    const auto hand = f.glued (2.0);
    const auto finding = detail::glueFinding (f.in, hand);
    ok (finding.state == GlueState::Unavailable && same (finding.upToDb, 2.0) && ! finding.ratio && ! finding.thresholdDb && ! finding.releaseMs,
        "a person's glue at 2 dB: unavailable — no threshold is invented");
    mastering::MasteringChainParams params;
    detail::writeDynamics (f.in, hand, params);
    ok (params.bypassCompressor, "and the chain gets no compressor");
    const auto fact = PlanText::glue (finding);
    const auto ru = fact ? text::Text::text (*fact, text::Lang::Ru) : std::string {};
    const auto en = fact ? text::Text::text (*fact, text::Lang::En) : std::string {};
    ok (fact && fact->id == text::FactId::GlueUnavailable && ru.find ("недоступен") != std::string::npos && ru.find ("P95") != std::string::npos
        && ru.find ("2") != std::string::npos && ru.find ('{') == std::string::npos && en.find ("unavailable") != std::string::npos,
        "the reason is a fact: " + ru + " / " + en);
    ok (! PlanText::glue (detail::glueFinding (Faked ("cd", -18.0, -3.0, -12.0).in, hand)), "with a P95 there is nothing to say");

    // The real thing: two seconds have an integrated loudness and a true peak, and no short-term loudness at all.
    const Mix scrap (1.0f, 2);
    auto sp = Session::create().session; auto& s = *sp;
    (void) s.apply (command::SetTarget { 1, "cd" });
    ok (s.apply (scrap.load (2)).rejection == Rejection::None, "PRECONDITION: a two-second source loads");
    for (unsigned i = 0; i < 4000000 && (s.measurementJob() || s.needlesJob()); ++i) (void) s.step (16);
    (void) s.apply (command::SetManual { 3, true });
    auto v = s.snapshot();
    ok (v.view().mandatoryMeasurementsReady && v.view().measurements[std::size_t (Analyzer::Programme)].status != MeasurementStatus::Pending
        && ! detail::inputLevels (inputsOf (s, v)).p95Db && detail::inputLevels (inputsOf (s, v)).truePeakDb,
        "PRECONDITION: its loudness and peak are measured, its short-term P95 is not");
    ok (v.view().plan.status == PlanStatus::Ready && ! v.view().project.devices.glue.machine.on && v.view().plan.devices.glue.heldBack == HeldBack::Unmeasured
        && v.view().plan.waiting == 0, "on cd it is placed: the glue off and saying why, nothing waited for");
    GlueFields<Touched> glue; glue.on = true; glue.upToDb = 2.0;
    ok (s.apply (command::EditDevice { 4, glue }).rejection == Rejection::None, "a person ticks the glue at 2 dB: the edit is taken");
    v = s.snapshot();
    ok (v.view().plan.glue.state == GlueState::Unavailable && same (*s.project().devices.glue.hand.upToDb, 2.0) && *s.project().devices.glue.hand.on
        && v.view().plan.devices.glue.on && v.view().plan.waiting == 0 && (v.view().plan.needs & detail::bitOf (Analyzer::Tempo)) == 0,
        "the value is kept and the tick shown; the glue is unavailable and reads nothing — no tempo is waited for");
    ok (v.view().canMaster && s.apply (command::Master { 5 }).rejection == Rejection::None, "and a master is taken, with the panel open");
}

void theTempo()
{
    felitronics::test::group ("the release follows a tempo measured with confidence — 0.5 and up — and 120 BPM otherwise; 50…500 ms");
    const auto r = detail::rules();
    const double divisor = detail::glueFor (r, 2.6).divisor;
    const auto release = [&] (Tempo t, double bpm, double n = 2.6)
    {
        const Faked f ("allStreaming", -18.0, -3.0, -12.0, t, bpm);
        return detail::glueFinding (f.in, f.glued (n));
    };
    const auto needs = [&] (Tempo t, std::optional<double> p95, double n)
    {
        const Faked f ("allStreaming", -18.0, -3.0, p95, t);
        DevicePlans plans;
        return detail::needs (f.in, f.glued (n), true, plans) & detail::bitOf (Analyzer::Tempo);
    };
    const auto sure = release (Tempo::High, 100.0);
    ok (sure.releaseMs && near (*sure.releaseMs, 60000.0 / 100.0 / divisor, 1e-9) && sure.tempoMeasured && same (*sure.bpm, 100.0)
        && ! sure.releaseClamped && ! PlanText::glueTempo (sure) && ! PlanText::glueRelease (sure),
        "a sure 100 BPM: the release is a beat over the travel's divisor, " + std::to_string (*sure.releaseMs) + " ms");
    const auto guess = release (Tempo::Medium, 100.0);
    const auto none = release (Tempo::Unavailable, 100.0);
    ok (guess.releaseMs && near (*guess.releaseMs, 60000.0 / 120.0 / divisor, 1e-9) && ! guess.tempoMeasured && same (*guess.bpm, 120.0)
        && none.releaseMs && same (*none.releaseMs, *guess.releaseMs) && ! none.tempoMeasured,
        "a tempo under the high label, and one that could not be measured at all: 120 BPM");
    ok (same (felitronics::tempo::TempoDetector::kHighConfidence, 0.5)
        && config::Config::load().config.engine.compressor.tempoTrustedConfidence == config::ConfidenceLabel::High
        && same (config::Config::load().config.engine.compressor.tempoBpmWhenUnsure, 120.0),
        "the high label is a confidence of 0.5 and up — the detector's own threshold — and the fallback is the config's 120");
    const auto fallback = PlanText::glueTempo (none);
    const auto said = fallback ? text::Text::text (*fallback, text::Lang::Ru) : std::string {};
    ok (fallback && fallback->id == text::FactId::GlueTempoFallback && said.find ("120") != std::string::npos && said.find ('{') == std::string::npos,
        "and the fallback is said: " + said);
    // A tempo heard under the trusted label (owner, 02.10 — Cold Gaze of Eternity: 140.3 BPM at medium): the release stays
    // at 120, the line names the tempo heard and its low confidence, and the choice names no failure — the result is whole.
    {
        const Faked f ("allStreaming", -18.0, -3.0, -12.0, Tempo::Medium, 140.3);
        const auto heard = detail::glueFinding (f.in, f.glued (2.6));
        const auto choice = detail::tempoChoice (r, f.results[std::size_t (Analyzer::Tempo)]);
        const auto line = PlanText::glueTempo (heard);
        const auto en = line ? text::Text::text (*line, text::Lang::En) : std::string {};
        const auto ru = line ? text::Text::text (*line, text::Lang::Ru) : std::string {};
        ok (heard.tempoUnsureBpm && same (*heard.tempoUnsureBpm, 140.3) && heard.bpm && same (*heard.bpm, 120.0) && ! heard.tempoMeasured
            && choice.ready && ! choice.measured && same (choice.bpm, 120.0) && choice.reason == MeasurementReason::None
            && line && line->id == text::FactId::GlueTempoUnsure && en.find ("140.3") != std::string::npos
            && en.find ("120") != std::string::npos && ru.find ("140,3") != std::string::npos && ru.find ("120") != std::string::npos
            && en.find ('{') == std::string::npos && ru.find ('{') == std::string::npos,
            "a tempo heard at medium: the release at 120, the reason no failure, and said — " + ru + " / " + en);
        ok (! sure.tempoUnsureBpm && ! none.tempoUnsureBpm && none.bpm && fallback && fallback->id == text::FactId::GlueTempoFallback
            && detail::tempoChoice (r, Faked ("allStreaming", -18.0, -3.0, -12.0, Tempo::Unavailable).results[std::size_t (Analyzer::Tempo)]).reason
                   == MeasurementReason::TooShort,
            "a tempo followed, and none heard: no number to name — the fallback line and the measurement's own reason");
    }
    const auto pending = release (Tempo::Pending, 100.0), cancelled = release (Tempo::Cancelled, 100.0);
    ok (pending.state == GlueState::Active && pending.ratio && ! pending.releaseMs && ! pending.bpm && cancelled.state == GlueState::Active && ! cancelled.releaseMs,
        "while the tempo runs, or is stopped, the glue has its curve and no release yet");
    ok (needs (Tempo::Pending, -12.0, 2.6) == detail::bitOf (Analyzer::Tempo) && needs (Tempo::Pending, -12.0, 0.0) == 0
        && needs (Tempo::Pending, std::nullopt, 2.6) == 0,
        "a glue that compresses reads the tempo — a master waits for it; at 0 dB, or unavailable, it reads nothing");
    {
        const Faked f ("allStreaming", -18.0, -3.0, -12.0, Tempo::Pending);
        mastering::MasteringChainParams params;
        detail::writeDynamics (f.in, f.glued (2.6), params);
        ok (params.bypassCompressor, "and before the tempo ends no half-set compressor is written");
    }
    const auto fast = release (Tempo::High, 300.0, 6.0), slow = release (Tempo::High, 30.0, 0.1);
    ok (fast.releaseClamped && same (*fast.releaseMs, 50.0) && *fast.releaseAskedMs < 50.0 && slow.releaseClamped && same (*slow.releaseMs, 500.0)
        && *slow.releaseAskedMs > 500.0, "300 BPM at 6 dB asks for " + std::to_string (*fast.releaseAskedMs) + " ms and gets the floor, 50; 30 BPM at 0.1 dB asks for "
        + std::to_string (*slow.releaseAskedMs) + " and gets the top, 500");
    const auto held = PlanText::glueRelease (fast);
    const auto heldRu = held ? text::Text::text (*held, text::Lang::Ru) : std::string {};
    ok (held && held->id == text::FactId::GlueReleaseHeld && heldRu.find ("50") != std::string::npos && heldRu.find ('{') == std::string::npos,
        "a clamp has its reason: " + heldRu);
    bool inside = true;
    for (int step = 1; step <= 600; ++step)
    {
        const auto at = release (Tempo::Medium, 0.0, step * 0.01);
        inside = inside && at.releaseMs && ! at.releaseClamped && *at.releaseMs >= 50.0 && *at.releaseMs <= 500.0;
    }
    ok (inside, "at the fallback 120 BPM the release stays inside 50…500 ms over the whole knob, unclamped");
}

void theSaturation()
{
    felitronics::test::group ("the saturation: the chain's tanh stage, its drive the knob's at the input's true peak; mix as set, the output neutral");
    namespace det = felitronics::core::det;
    bool aligned = true, written = true;
    for (const double peak : { -12.0, -6.0, -1.0, 0.0, 2.0 })
        for (const double knob : { 3.0, 6.0, 12.0 })
        {
            // The input at −18 LUFS already, so its true peak is the normalised one.
            const Faked f ("allStreaming", -18.0, peak, -12.0);
            const auto d = f.shaped (knob);
            const auto found = detail::saturationFinding (f.in, d);
            const double k = det::pow10 (*found.driveDb / 20) - 1, want = det::pow10 (knob / 20) - 1;
            aligned = aligned && found.active && same (found.knobDb, knob) && same (*found.peakDbTp, peak)
                && near (k * det::pow10 (peak / 20), want, 1e-12 * want) && (peak < 0 ? *found.driveDb > knob : peak > 0 ? *found.driveDb < knob : near (*found.driveDb, knob, 1e-12));
            mastering::MasteringChainParams params;
            params.inputGainDb = 1.25; params.preLimiterGainDb = -2.5;
            detail::writeDynamics (f.in, d, params);
            written = written && ! params.bypassClipper && params.bypassCompressor && params.clipper.shape == felitronics::saturation::WaveShaper::Shape::Tape
                && sameF (params.clipper.driveDb, float (*found.driveDb)) && sameF (params.clipper.autoComp, 0.0f) && sameF (params.clipper.mix, 1.0f)
                && sameF (params.clipper.outputDb, 0.0f) && sameF (params.clipper.bias, 0.0f) && same (params.inputGainDb, 1.25) && same (params.preLimiterGainDb, -2.5);
        }
    ok (aligned, "drive 3 / 6 / 12 dB at peaks from −12 to +2 dBTP: the shaper's gain at the peak is the knob's, k·peak = 10^(knob/20) − 1");
    ok (written, "the clipper stage gets it — tape, the machine's type, the compensation 0 — and neither gain of the chain is touched: the normalising gain is the search's, once");

    const Faked f ("allStreaming", -18.0, -6.0, -12.0);
    mastering::MasteringChainParams params;
    detail::writeDynamics (f.in, f.shaped (6.0, 0.0), params);
    const bool dry = ! params.bypassClipper && sameF (params.clipper.mix, 0.0f) && sameF (params.clipper.outputDb, 0.0f);
    detail::writeDynamics (f.in, f.shaped (6.0, 1.0), params);
    ok (dry && sameF (params.clipper.mix, 1.0f) && sameF (params.clipper.outputDb, 0.0f), "mix 0 and 1 are written as set, the shaper's output at 0 dB");
    detail::writeDynamics (f.in, f.shaped (0.0), params);
    auto unticked = f.shaped (6.0); unticked.saturation.hand.on = false;
    const bool zero = params.bypassClipper && ! detail::saturationFinding (f.in, f.shaped (0.0)).active;
    detail::writeDynamics (f.in, unticked, params);
    ok (zero && params.bypassClipper, "at 0 dB, and unticked, the stage is bypassed");
    detail::writeDynamics (f.in, f.machine(), params);
    ok (params.bypassClipper && params.bypassCompressor, "the machine's devices off cd: neither stage in the chain");

    // Every field of both stages is the config's, none the core's default.
    const Faked cd ("cd", -18.0, -3.0, -12.0, Tempo::High, 96.0);
    mastering::MasteringChainParams full;
    full.compressor.lookaheadMs = 7.0; full.compressor.makeupDb = 3.0; full.compressor.autoMakeup = true; full.compressorMix = 0.25;
    full.clipper.autoComp = 0.5f; full.clipper.dcBlockHz = 10.0f;
    detail::writeDynamics (cd.in, cd.machine(), full);
    const auto glue = detail::glueFinding (cd.in, cd.machine());
    ok (! full.bypassCompressor && full.bypassClipper && full.compressor.detector == dynamics::Detector::Rms && full.compressor.link == dynamics::LinkMode::Max
        && full.compressor.mode == dynamics::Mode::DownCompress && same (full.compressor.rmsWindowMs, 5.0) && same (full.compressor.rangeDb, 60.0)
        && same (full.compressor.makeupDb, 0.0) && ! full.compressor.autoMakeup && same (full.compressor.lookaheadMs, 0.0) && same (full.compressorMix, 0.4) && same (glue.mix, 0.4)
        && same (full.compressor.thresholdDb, *glue.thresholdDb) && same (full.compressor.ratio, *glue.ratio) && same (full.compressor.kneeDb, *glue.kneeDb)
        && same (full.compressor.attackMs, *glue.attackMs) && same (full.compressor.releaseMs, *glue.releaseMs)
        && sameF (full.clipper.autoComp, 0.0f) && sameF (full.clipper.dcBlockHz, 0.0f),
        "cd's machine glue: every compressor field written from the config and the finding, over whatever stood there");
}

void oneSystemOfLevels()
{
    felitronics::test::group ("one mix at several levels: the same compressor, the same shaper, the same sound");
    // The readings of one input exported at four levels, exactly: every finding is the same.
    const Faked base ("cd", -14.0, -1.5, -9.25, Tempo::High, 96.0);
    const auto glue0 = detail::glueFinding (base.in, base.shaped (6.0));
    const auto shaper0 = detail::saturationFinding (base.in, base.shaped (6.0));
    bool exact = glue0.state == GlueState::Active && shaper0.active;
    for (const double shift : { -12.0, -3.3, 6.0, 9.75 })
    {
        const Faked f ("cd", -14.0 + shift, -1.5 + shift, -9.25 + shift, Tempo::High, 96.0);
        const auto g = detail::glueFinding (f.in, f.shaped (6.0));
        const auto sat = detail::saturationFinding (f.in, f.shaped (6.0));
        exact = exact && g.state == GlueState::Active && same (*g.ratio, *glue0.ratio) && same (*g.kneeDb, *glue0.kneeDb) && same (*g.attackMs, *glue0.attackMs)
            && same (*g.releaseMs, *glue0.releaseMs) && near (*g.thresholdDb, *glue0.thresholdDb, 1e-9) && sat.active
            && near (*sat.driveDb, *shaper0.driveDb, 1e-9) && near (*sat.peakDbTp, *shaper0.peakDbTp, 1e-9)
            && near (*detail::inputLevels (f.in).gainDb, *detail::inputLevels (base.in).gainDb - shift, 1e-9);
    }
    ok (exact, "readings shifted by −12, −3.3, +6 and +9.75 dB: one gain apart, and the same threshold, ratio, knee, attack, release and drive");

    // The real thing, measured: the mix, a quarter of it and twice it.
    struct Level { float scale = 1.0f; std::unique_ptr<Mix> mix; std::unique_ptr<Session> s; PlanView plan; Staged out; double gainDb = 0; };
    const double share = loudShare();
    const auto largest = [] (const Staged& o) { return cutDb (o.peaks.quietGain, o.peaks.loudLeastRatio); };
    const auto usual = [] (const Staged& o) { return cutDb (o.peaks.quietGain, o.peaks.loudUsualRatio); };
    const auto run = [&] (const char* target, Level* levels)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& level = levels[i];
            level.scale = i == 0 ? 1.0f : i == 1 ? 0.25f : 2.0f;
            level.mix = std::make_unique<Mix> (level.scale);
            level.s = measured (*level.mix, target);
            SaturationFields<Touched> sat; sat.on = true; sat.drive = 6.0;
            felitronics::test::run (level.s->apply (command::EditDevice { 3, sat }).rejection == Rejection::None);
            const auto snapshot = level.s->snapshot();
            level.plan = snapshot.view().plan;
            mastering::MasteringChainParams params;
            detail::writeDynamics (inputsOf (*level.s, snapshot), level.s->project().devices, params);
            level.gainDb = *level.plan.inputGainDb;
            level.out = stage (*level.mix, params, level.gainDb, share);
        }
    };
    const auto apart = [] (const Staged& a, const Staged& b)
    {
        double worst = 0.0;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            worst = std::max ({ worst, std::fabs (double (a.left[i]) - double (b.left[i])), std::fabs (double (a.right[i]) - double (b.right[i])) });
        return worst;
    };

    // THE SATURATION ALONE (allStreaming has no glue): the true peak scales with the level exactly.
    Level shaped[3];
    run ("allStreaming", shaped);
    bool gains = true, shaper = true, cut = true;
    double worst = 0.0;
    for (const auto& level : shaped)
    {
        const double shift = 20 * std::log10 (double (level.scale));
        gains = gains && near (level.gainDb, shaped[0].gainDb - shift, 1e-6);
        shaper = shaper && level.plan.saturation.active && level.plan.glue.state == GlueState::Out
            && near (*level.plan.saturation.driveDb, *shaped[0].plan.saturation.driveDb, 1e-6) && near (*level.plan.saturation.peakDbTp, *shaped[0].plan.saturation.peakDbTp, 1e-6);
        worst = std::max (worst, apart (level.out, shaped[0].out));
        cut = cut && level.out.counted && near (largest (level.out), largest (shaped[0].out), 1e-3) && near (usual (level.out), usual (shaped[0].out), 1e-3);
    }
    std::printf ("    saturation at 6 dB: drive %.4f dB at a normalised peak of %.3f dBTP; peaks cut by up to %.3f dB, usually %.3f dB — at x1, x0.25 and x2\n",
        *shaped[0].plan.saturation.driveDb, *shaped[0].plan.saturation.peakDbTp, largest (shaped[0].out), usual (shaped[0].out));
    ok (gains, "the one gain differs by exactly the level: 12.04 dB up for the quarter, 6.02 dB down for the double");
    ok (shaper, "the saturation: one drive, from one normalised true peak");
    ok (worst < 1e-5, "and the stage puts out the same signal: the largest difference " + std::to_string (worst) + " of full scale");
    ok (cut, "with the same cut of peaks, the largest and the usual");

    // WITH cd's GLUE: its threshold stands on the programme report's P95, which that report reads off 0.1 LU bins of the
    // file's own level — so between levels the threshold agrees within one bin, and everything else exactly.
    Level glued[3];
    run ("cd", glued);
    bool curve = true, threshold = true, did = true;
    worst = 0.0;
    for (const auto& level : glued)
    {
        curve = curve && level.plan.glue.state == GlueState::Active && same (level.plan.glue.upToDb, 2.6) && same (*level.plan.glue.ratio, *glued[0].plan.glue.ratio)
            && same (*level.plan.glue.kneeDb, *glued[0].plan.glue.kneeDb) && same (*level.plan.glue.attackMs, *glued[0].plan.glue.attackMs)
            && near (*level.plan.glue.releaseMs, *glued[0].plan.glue.releaseMs, 1e-9) && level.plan.glue.tempoMeasured == glued[0].plan.glue.tempoMeasured;
        threshold = threshold && near (*level.plan.glue.thresholdDb, *glued[0].plan.glue.thresholdDb, 0.1);
        worst = std::max (worst, apart (level.out, glued[0].out));
        did = did && near (level.out.grP95, glued[0].out.grP95, 0.06) && near (level.out.grMax, glued[0].out.grMax, 0.06)
            && near (usual (level.out), usual (glued[0].out), 0.1);
        std::printf ("    x%.2f on cd: gain %+.3f dB; glue up to %.1f dB — ratio %.4f, threshold %.3f dB, attack %.2f ms, release %.2f ms (%s %.1f BPM)\n"
                     "           it took %.2f dB at its P95, %.2f dB at most; the saturation cut peaks by up to %.2f dB, usually %.2f dB\n",
            double (level.scale), level.gainDb, level.plan.glue.upToDb, *level.plan.glue.ratio, *level.plan.glue.thresholdDb, *level.plan.glue.attackMs,
            *level.plan.glue.releaseMs, level.plan.glue.tempoMeasured ? "measured" : "fallback", *level.plan.glue.bpm,
            level.out.grP95, level.out.grMax, largest (level.out), usual (level.out));
    }
    ok (curve, "cd's glue: one ratio, knee, attack and release at every level");
    ok (threshold, "one threshold, within the P95's own 0.1 LU bin");
    ok (worst < 5e-3 && did, "and the same action within that bin: the largest difference " + std::to_string (worst)
        + " of full scale, the gain reduction within 0.06 dB");
    ok (glued[0].out.grP95 > 0.3 && glued[0].out.grMax >= glued[0].out.grP95, "the glue really compressed: " + std::to_string (glued[0].out.grP95)
        + " dB at its P95, " + std::to_string (glued[0].out.grMax) + " dB at most, for a knob of 2.6 dB");
}

// THE CALIBRATION REACHES THE CHAIN. A steady tone has one detector level, so what the compressor takes is the curve's
// own value there: the real gain reduction of the real stage is the core's static curve at the tone's RMS, with the
// threshold where the finding put it — the P95, the calibration over it, the travel's offset. (That the knob then is
// the reduction MUSIC gets on its loud places is measured on the owner's mixes; its numbers are in [glue].)
void theCalibration()
{
    felitronics::test::group ("the calibrated threshold reaches the chain: on a steady tone the stage takes the curve's own value at the detector's level");
    struct Tone : Mix
    {
        Tone() : Mix (1.0f, 12)
        {
            for (unsigned i = 0; i < frames; ++i)
                left[i] = right[i] = float (0.25 * felitronics::core::det::sin (2 * kPi * 1000.0 * double (i) / rate));
        }
    };
    const Tone tone;
    auto sp = measured (tone, "allStreaming"); auto& s = *sp;
    const double over = config::Config::load().config.engine.glue.detectorOverP95Db;
    bool held = true;
    for (const double n : { 1.25, 2.6, 3.0, 6.0 })
    {
        GlueFields<Touched> glue; glue.on = true; glue.upToDb = n;
        felitronics::test::run (s.apply (command::EditDevice { 3, glue }).rejection == Rejection::None);
        const auto snapshot = s.snapshot();
        const auto& plan = snapshot.view().plan;
        const auto in = inputsOf (s, snapshot);
        const auto levels = detail::inputLevels (in);
        mastering::MasteringChainParams params;
        detail::writeDynamics (in, s.project().devices, params);
        const auto out = stage (tone, params, *plan.inputGainDb, loudShare());
        // The detector's level: the tone's RMS after the one gain.
        const double detector = 20 * std::log10 (0.25 / std::sqrt (2.0)) + *plan.inputGainDb;
        const double curve = coreLossAt (plan.glue, detector);
        held = held && plan.glue.state == GlueState::Active && same (*plan.glue.thresholdDb, *levels.p95Db + over + detail::glueFor (in.rules, n).threshOffsetDb)
            && same (params.compressor.thresholdDb, *plan.glue.thresholdDb) && near (out.grP95, curve, 0.03) && near (out.grMax, curve, 0.05);
        std::printf ("    up to %.2f dB: the loud places at %.3f dB (P95 %.3f + %.1f), the tone's RMS at %.3f dB; the curve takes %.3f dB there, the stage took %.3f dB\n",
            n, *levels.p95Db + over, *levels.p95Db, over, detector, curve, out.grP95);
    }
    ok (held, "up to 1.25 / 2.6 / 3 / 6 dB: the stage's gain reduction is the curve's at the detector's level, within 0.03 dB, the threshold the finding's");
}

void theCut()
{
    felitronics::test::group ("what the saturation cut: measured on the stage — the largest and the usual — growing evenly with the drive");
    const Mix mix;
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    const double share = loudShare();
    const auto run = [&] (double drive, double mixed)
    {
        SaturationFields<Touched> sat; sat.on = true; sat.drive = drive; sat.mix = mixed;
        felitronics::test::run (s.apply (command::EditDevice { 3, sat }).rejection == Rejection::None);
        const auto snapshot = s.snapshot();
        mastering::MasteringChainParams params;
        detail::writeDynamics (inputsOf (s, snapshot), s.project().devices, params);
        return stage (mix, params, *snapshot.view().plan.inputGainDb, share);
    };
    const auto largest = [] (const Staged& o) { return cutDb (o.peaks.quietGain, o.peaks.loudLeastRatio); };
    const auto usual = [] (const Staged& o) { return cutDb (o.peaks.quietGain, o.peaks.loudUsualRatio); };
    const auto off = run (0.0, 1.0);
    ok (! off.counted, "drive 0: the stage is bypassed and nothing is counted");
    double previousLargest = 0.0, previousUsual = 0.0, stepLargest = 0.0, stepUsual = 0.0, worstTurn = 0.0;
    bool grows = true, even = true;
    for (int drive = 1; drive <= 12; ++drive)
    {
        const auto at = run (drive, 1.0);
        const double n = largest (at), m = usual (at);
        grows = grows && at.counted && n > previousLargest && m > previousUsual && n >= m;
        // A dB of drive moves each number by under two, and by nearly what the dB before moved it: no jump.
        const double dn = n - previousLargest, dm = m - previousUsual;
        even = even && dn < 2.0 && dm < 2.0;
        // (The first step off zero brings the stage in — its oversampling round trip takes the mix's noise clicks down
        // by half a dB whatever the drive; from there on the knob is all that moves.)
        if (drive > 2) worstTurn = std::max ({ worstTurn, std::fabs (dn - stepLargest), std::fabs (dm - stepUsual) });
        if (drive == 3 || drive == 6 || drive == 12)
            std::printf ("    drive %2d dB: peaks cut by up to %.3f dB, usually %.3f dB (%llu loud quanta of %llu)\n", drive, n, m,
                (unsigned long long) at.peaks.loudQuanta, (unsigned long long) at.peaks.quanta);
        previousLargest = n; previousUsual = m; stepLargest = dn; stepUsual = dm;
    }
    ok (grows, "drive 1…12 dB: the largest and the usual cut both grow at every dB, the largest never under the usual");
    ok (even && worstTurn < 0.35, "and neither jumps: a dB of the knob moves each by under two, and by within " + std::to_string (worstTurn)
        + " dB of what the dB before moved it");
    const auto full = run (6.0, 1.0), dry = run (6.0, 0.0), half = run (6.0, 0.5);
    ok (dry.counted && near (largest (dry), 0.0, 1e-3) && usual (half) > 0.05 && usual (half) < usual (full),
        "mix 0 cuts nothing; mix 0.5 less than mix 1 (" + std::to_string (usual (half)) + " of " + std::to_string (usual (full)) + " dB)");
}

void theReport()
{
    felitronics::test::group ("the report: the glue's P95 and maximum, the saturation's largest and usual cut — each stage's own, as facts");
    const Mix mix;
    // allStreaming delivers at the source's rate, so the master's chain is the one a test can run beside it.
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    GlueFields<Touched> knob; knob.on = true; knob.upToDb = 2.6;
    SaturationFields<Touched> sat; sat.on = true; sat.drive = 6.0;
    ok (s.apply (command::EditDevice { 3, knob }).rejection == Rejection::None && s.apply (command::EditDevice { 3, sat }).rejection == Rejection::None,
        "PRECONDITION: glue up to 2.6 dB, saturation at 6 dB");
    command::Master request { 4 };
    request.ready.version = 1;
    request.ready.topology = twoStages (true);
    double gainDb = 0.0, sourcePeak = 0.0;
    {
        const auto snapshot = s.snapshot();
        detail::writeDynamics (inputsOf (s, snapshot), s.project().devices, request.ready.params);
        gainDb = *snapshot.view().plan.inputGainDb;
        sourcePeak = *snapshot.view().plan.saturation.peakDbTp;
    }
    ok (same (request.ready.params.inputGainDb, 0.0) && ! request.ready.params.bypassCompressor && ! request.ready.params.bypassClipper,
        "PRECONDITION: both stages written, the chain's input gain left to the search");
    request.source = s.source().hash; request.revision = s.revision();
    ok (s.apply (request).rejection == Rejection::None, "PRECONDITION: the master is taken");
    const auto facts = finish (s);
    ok (s.job() == 0 && s.masters().size() == 1 && s.masters()[0].report && s.masters()[0].report->cost, "PRECONDITION: mastered, with its cost");
    const auto& report = *s.masters()[0].report;
    const auto& cost = *report.cost;
    // The same two stages alone, the input brought up by the one gain: what the master's own stages did, to the bit.
    auto alone = request.ready.params;
    const auto staged = stage (mix, alone, gainDb, loudShare());
    ok (cost.glueP95Db.value && cost.glueMaxDb.value && cost.glueP95Db.reason == MeasurementReason::None
        && same (*cost.glueMaxDb.value, staged.grMax) && near (*cost.glueP95Db.value, staged.grP95, 0.1) && *cost.glueMaxDb.value >= *cost.glueP95Db.value,
        "the glue: " + std::to_string (*cost.glueP95Db.value) + " dB at its P95, " + std::to_string (*cost.glueMaxDb.value)
        + " dB at most — the compressor's own trace (alone: " + std::to_string (staged.grP95) + " / " + std::to_string (staged.grMax) + ")");
    ok (cost.saturationCutMaxDb.value && cost.saturationCutUsualDb.value
        && same (*cost.saturationCutMaxDb.value, cutDb (staged.peaks.quietGain, staged.peaks.loudLeastRatio))
        && same (*cost.saturationCutUsualDb.value, cutDb (staged.peaks.quietGain, staged.peaks.loudUsualRatio))
        && cost.saturationCutMaxDb.compared == staged.peaks.loudQuanta,
        "the saturation: peaks cut by up to " + std::to_string (*cost.saturationCutMaxDb.value) + " dB, usually " + std::to_string (*cost.saturationCutUsualDb.value)
        + " dB — the stage's own count, the input normalised once");
    // The chain's true peak fell by another number altogether: the limiter's and the landing's work.
    const double chainFall = sourcePeak - *report.truePeakDbTp;
    ok (std::fabs (chainFall - *cost.saturationCutMaxDb.value) > 0.5,
        "and it is not the fall of the chain's true peak (" + std::to_string (chainFall) + " dB from the normalised input's peak to the master's)");
    const auto said = [&] (text::FactId id) -> std::string
    {
        for (const auto& f : facts) if (f.id == id) return text::Text::text (f, text::Lang::Ru) + " / " + text::Text::text (f, text::Lang::En);
        return {};
    };
    const auto glue = said (text::FactId::MasterGlue), shaped = said (text::FactId::MasterSaturation);
    ok (glue.find ("Клей снял") != std::string::npos && glue.find ('{') == std::string::npos, "the glue's line is published: " + glue);
    ok (shaped.find ("срезала пики до") != std::string::npos && shaped.find ("обычно") != std::string::npos && shaped.find ('{') == std::string::npos,
        "the saturation's line is published: " + shaped);

    // A master without either stage says nothing about them.
    auto bare = measured (mix, "allStreaming");
    command::Master plain { 3 };
    plain.ready.version = 1;
    plain.ready.topology = twoStages (true);
    {
        const auto snapshot = bare->snapshot();
        detail::writeDynamics (inputsOf (*bare, snapshot), bare->project().devices, plain.ready.params);
    }
    ok (plain.ready.params.bypassCompressor && plain.ready.params.bypassClipper && bare->apply (plain).rejection == Rejection::None,
        "PRECONDITION: a master with both stages bypassed is taken");
    bool silent = true;
    for (const auto& f : finish (*bare)) silent = silent && f.id != text::FactId::MasterGlue && f.id != text::FactId::MasterSaturation;
    const auto& bareCost = bare->masters()[0].report->cost;
    ok (bareCost && ! bareCost->glueP95Db.value && bareCost->glueP95Db.reason == MeasurementReason::NoSignal && ! bareCost->saturationCutMaxDb.value
        && bareCost->saturationCutUsualDb.reason == MeasurementReason::NoSignal && silent,
        "with both stages out of the chain the report has no number for them — NoSignal — and no line");
}
void theType()
{
    felitronics::test::group ("the saturation's type: a person's pick among five over the machine's tape — the chain, the report, "
                              "the project file; a refused type changes nothing, a change of target takes it back");
    const Mix mix;
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    const auto& sat = s.project().devices.saturation;
    ok (sat.machine.type == SaturationType::Tape && ! sat.hand.type, "the machine's type is the config's, tape; nothing by hand");
    SaturationFields<Touched> knob; knob.on = true; knob.drive = 6.0;
    ok (s.apply (command::EditDevice { 3, knob }).rejection == Rejection::None, "PRECONDITION: saturation at 6 dB");
    // A hand drive with no type picked runs the machine's type (owner, 01.10: tape).
    mastering::MasteringChainParams unpicked;
    {
        const auto snapshot = s.snapshot();
        detail::writeDynamics (inputsOf (s, snapshot), s.project().devices, unpicked);
    }

    // REFUSED: atan, cubic and asym are the config's only (research), and past tape is no type — the command is refused
    // whole, at the type's place, and the drive it carried is not taken either.
    bool refused = true;
    std::string said;
    for (const auto t : { SaturationType::Atan, SaturationType::Cubic, SaturationType::Asym, SaturationType (8) })
    {
        const auto revision = s.revision();
        SaturationFields<Touched> pick; pick.type = t; pick.drive = 3.0;
        const command::EditDevice request { 4, pick };
        const auto answer = s.apply (request);
        refused = refused && answer.rejection == Rejection::NotOneOf && answer.field == 4 && s.revision() == revision
               && ! sat.hand.type && sat.hand.drive == 6.0;
        if (const auto fact = text::Text::rejected (answer, request)) said = text::Text::text (*fact, text::Lang::Ru);
    }
    ok (refused && said.find ("Тип сатурации") != std::string::npos,
        "atan, cubic, asym and one past tape: NotOneOf at field 4, the revision and the drive unmoved — " + said);

    // PICKED: the type is the stage the chain runs, and what the report says it cut is measured on THAT stage.
    const auto pick = [&] (SaturationType t)
    {
        SaturationFields<Touched> edit; edit.type = t;
        felitronics::test::run (s.apply (command::EditDevice { 5, edit }).rejection == Rejection::None);
        const auto snapshot = s.snapshot();
        mastering::MasteringChainParams params;
        detail::writeDynamics (inputsOf (s, snapshot), s.project().devices, params);
        return params;
    };
    const auto share = loudShare();
    const auto gainOf = [&] { return *s.snapshot().view().plan.inputGainDb; };
    const auto tanh = pick (SaturationType::Tanh);
    const auto tanhStage = stage (mix, tanh, gainOf(), share);
    bool each = tanh.clipper.shape == felitronics::saturation::WaveShaper::Shape::Tanh;
    for (const auto t : { SaturationType::Tube, SaturationType::Transistor, SaturationType::Transformer })
        each = each && int (pick (t).clipper.shape) == int (t) && sat.hand.type == t;
    const auto tape = pick (SaturationType::Tape);
    const auto tapeStage = stage (mix, tape, gainOf(), share);
    const auto unpickedStage = stage (mix, unpicked, gainOf(), share);
    const auto bits = [] (const std::vector<float>& a, const std::vector<float>& b)
    { return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0; };
    ok (unpicked.clipper.shape == felitronics::saturation::WaveShaper::Shape::Tape && ! unpicked.bypassClipper
        && bits (unpickedStage.left, tapeStage.left) && bits (unpickedStage.right, tapeStage.right)
        && ! bits (unpickedStage.left, tanhStage.left),
        "a hand drive with no type picked sounds tape: its stage is bit for bit the tape pick's, and not the tanh's");
    const double tanhCut = cutDb (tanhStage.peaks.quietGain, tanhStage.peaks.loudLeastRatio);
    const double tapeCut = cutDb (tapeStage.peaks.quietGain, tapeStage.peaks.loudLeastRatio);
    ok (each && tape.clipper.shape == felitronics::saturation::WaveShaper::Shape::Tape && ! tape.bypassClipper
        && tanhStage.counted && tapeStage.counted && std::fabs (tapeCut - tanhCut) > 1.0e-3,
        "tanh, tube, transistor, transformer, tape: each is the clipper's shape; tape's stage cuts " + std::to_string (tapeCut)
        + " dB at most where tanh's cuts " + std::to_string (tanhCut));

    command::Master request { 6 };
    request.ready.version = 1;
    request.ready.topology = twoStages (true);
    request.ready.params = tape;
    request.source = s.source().hash; request.revision = s.revision();
    ok (s.apply (request).rejection == Rejection::None, "PRECONDITION: a master with the tape type is taken");
    const auto facts = finish (s);
    const auto* cost = s.masters().size() == 1 && s.masters()[0].report && s.masters()[0].report->cost ? &*s.masters()[0].report->cost : nullptr;
    std::string line;
    for (const auto& f : facts) if (f.id == text::FactId::MasterSaturation) line = text::Text::text (f, text::Lang::Ru);
    ok (cost && cost->saturationCutMaxDb.value && same (*cost->saturationCutMaxDb.value, tapeCut)
        && cost->saturationCutUsualDb.value
        && same (*cost->saturationCutUsualDb.value, cutDb (tapeStage.peaks.quietGain, tapeStage.peaks.loudUsualRatio))
        && line.find ("срезала пики до") != std::string::npos,
        "the master's report measures the tape stage, the same words for every type: " + line);

    // THE FILE: the pick is written as the person's, the machine's tape is not written; another session reads it back.
    (void) pick (SaturationType::Tanh);
    const auto file = s.exportProject();
    const std::string written (file.view());
    auto other = measured (mix, "allStreaming");
    ok (file.rejection == Rejection::None && written.find ("type.hand = \"tanh\"") != std::string::npos
        && written.find ("type.machine") == std::string::npos
        && other->importProject (7, written).rejection == Rejection::None
        && other->project().devices.saturation.hand.type == SaturationType::Tanh
        && other->project().devices.saturation.machine.type == SaturationType::Tape,
        "the project file carries type.hand = \"tanh\" and reads back as the person's tanh over the machine's tape");
    const auto at = written.find ("type.hand = \"tanh\"");
    auto atan = written; atan.replace (at, 18, "type.hand = \"atan\"");
    auto third = measured (mix, "allStreaming");
    const auto answer = third->importProject (8, atan);
    ok (answer.rejection == Rejection::NotOneOf && answer.device == Device::Saturation && answer.field == 4
        && ! third->project().devices.saturation.hand.type, "a file with atan by hand is refused, at the saturation's type");

    // A CHANGE OF TARGET takes the pick back with every other edit, and the warning before it counts it.
    const auto view = s.snapshot();
    const auto warned = SnapshotText::targetChange (view.view());
    ok (view.view().handFieldCount == 3 && warned && warned->args[0].integer == 3,
        "the warning before a change of target counts the type among the three edits (on, drive, type)");
    ok (s.apply (command::SetTarget { 9, "cd" }).rejection == Rejection::None && ! sat.hand.type && sat.machine.type == SaturationType::Tape,
        "after the change the type is the machine's tape again");
}
} // namespace

void limiterAndDitherAsTheChainGetsThem()
{
    felitronics::test::group ("the plan carries the limiter's release, slow release, lookahead and oversampling, and the dither's shaping, as the chain gets them");
    const auto& e = config::Config::load().config.engine;
    bool same16 = true;
    for (const char* target : { "cd", "allStreaming", "lp" })
    {
        const Faked f (target, -18.0, -3.0, -12.0);
        const auto d = f.machine();
        const auto lim = detail::limiterFinding (f.in, d);
        const auto dit = detail::ditherFinding (f.in, d);
        command::MasterReady ready;
        detail::writeChain (f.in, d, ready);
        const auto& l = ready.params.limiter;
        same16 = same16 && same (lim.releaseMs, l.releaseMs) && lim.dualRelease == l.dualRelease && same (lim.slowReleaseMs, l.slowReleaseMs)
            && same (lim.lookaheadMs, ready.topology.limiterLookaheadMs) && same (lim.releaseMs, e.limiter.releaseMs)
            && same (lim.lookaheadMs, e.limiter.lookaheadMs)
            && lim.oversampling == mastering::MasteringChain::tapOversampleFactorFor (ready.topology) && lim.oversampling >= 2
            && int (dit.shaping) == int (ready.params.dither.shaping);
        std::printf ("    %s: release %.0f ms, slow %.0f ms (%s), lookahead %.2f ms, oversampling %dx; dither at %d bits, shaping %u\n", target,
            lim.releaseMs, lim.slowReleaseMs, lim.dualRelease ? "on" : "off", lim.lookaheadMs, int (lim.oversampling), int (dit.bits), unsigned (dit.shaping));
    }
    ok (same16, "cd, all streaming, lp (vinyl): the plan's numbers are the ones the chain is written with, and the effective oversampling");
    const Faked cd ("cd", -18.0, -3.0, -12.0), stream ("allStreaming", -18.0, -3.0, -12.0);
    const auto cdDither = detail::ditherFinding (cd.in, cd.machine()), streamDither = detail::ditherFinding (stream.in, stream.machine());
    ok (cdDither.bits <= 16 && cdDither.shaping == DitherShaping::Weighted && (streamDither.bits <= 16 || streamDither.shaping == DitherShaping::None),
        "16 bits take the config's weighted shaping; above shapingUpToBits there is none");
}

void outStatesItsNumbers()
{
    felitronics::test::group ("a glue out of the chain states its numbers all the same — off, at 0 dB — and stays out (slice 5)");
    const auto r = detail::rules();
    const double fallback = config::Config::load().config.engine.compressor.tempoBpmWhenUnsure;
    const auto check = [&] (const detail::PlanInputs& in, const Devices& d, double knob, std::optional<double> p95, std::optional<double> bpm, bool measured)
    {
        const auto f = detail::glueFinding (in, d);
        const auto curve = detail::glueFor (r, knob);
        const double over = config::Config::load().config.engine.glue.detectorOverP95Db;
        bool good = f.state == GlueState::Out && same (f.upToDb, knob) && f.ratio && same (*f.ratio, curve.ratio)
            && f.kneeDb && same (*f.kneeDb, curve.kneeDb) && f.attackMs && same (*f.attackMs, curve.attackMs);
        good = good && (p95 ? f.p95DetectorDb && same (*f.p95DetectorDb, *p95 + over) && f.thresholdDb
                                && same (*f.thresholdDb, *p95 + over + curve.threshOffsetDb)
                            : ! f.p95DetectorDb && ! f.thresholdDb);
        const double asked = bpm ? 60000.0 / *bpm / curve.divisor : 0.0;
        good = good && bpm && f.bpm && same (*f.bpm, *bpm) && f.tempoMeasured == measured && f.releaseAskedMs && same (*f.releaseAskedMs, asked)
            && f.releaseMs && *f.releaseMs > 0.0;
        mastering::MasteringChainParams params;
        detail::writeDynamics (in, d, params);
        return good && params.bypassCompressor && ! PlanText::glue (f);
    };
    const Faked measured ("allStreaming", -18.0, -3.0, -12.0, Tempo::High, 128.0);
    const Faked pending ("allStreaming", -18.0, -3.0, -12.0, Tempo::Pending);
    const Faked noP95 ("cd", -18.0, -3.0, std::nullopt, Tempo::Pending);
    auto unticked = pending.glued (2.0); unticked.glue.hand.on = false;
    // The P95 on the normalised input: the gain to [input] referenceLufs from the faked −18 LUFS, added.
    const double p95 = -12.0 + config::Config::load().config.engine.input.referenceLufs + 18.0;
    ok (check (measured.in, measured.glued (0.0), 0.0, p95, 128.0, true),
        "ticked at 0 dB: the travel's start — its ratio, knee and attack, the threshold on the P95, the release from the measured tempo");
    ok (check (pending.in, unticked, 2.0, p95, fallback, false),
        "unticked at 2 dB, its tempo never measured: the knob's numbers, and the release at the fallback tempo");
    ok (check (noP95.in, noP95.machine(), 2.6, std::nullopt, fallback, false),
        "the machine's cd glue left off for want of a P95: ratio, knee, attack and release, and no threshold — none is invented");
    const auto active = detail::glueFinding (pending.in, pending.glued (2.0));
    ok (active.state == GlueState::Active && ! active.releaseMs && ! active.bpm,
        "a glue in the chain whose tempo is pending still waits for it: no fallback release");
}

// THE GLUE'S MIX (v0.17.0): the glue is a parallel compressor, 40 % of the compressed signal under 60 % of the dry one,
// wherever it is on — the machine's cd glue and a person's tick alike. A person moves the share 0 … 1 (the page's slider
// by 0.2, the core takes any share in its domain); the project keeps it.
void theMix()
{
    felitronics::test::group ("the glue's mix: 40 % by default wherever the glue is on; 0 … 1 by hand, kept");
    const auto r = detail::rules();
    const auto engine = config::Config::load().config.engine;
    ok (same (engine.glue.mix, 0.4) && same (engine.glue.mixStep, 0.2) && same (engine.glue.mixRange.min, 0.0) && same (engine.glue.mixRange.max, 1.0)
        && same (engine.glue.mixDomain.min, 0.0) && same (engine.glue.mixDomain.max, 1.0), "[glue] mix 0.4, the slider 0 … 1 by 0.2, the domain 0 … 1");
    bool machine = true;
    for (std::uint16_t row = 0; row < r.rows; ++row)
    {
        const auto target = r.row (row);
        const Faked f (target.key, -14.0, -1.0, -10.0, Tempo::High, 96.0);
        Devices d; DevicePlans plans; detail::PlanFindings found;
        detail::propose (f.in, d, plans, found);
        mastering::MasteringChainParams params;
        params.compressorMix = 0.25;
        detail::writeDynamics (f.in, d, params);
        const auto glue = detail::glueFinding (f.in, d);
        const bool cd = target.key == "cd";
        machine = machine && same (d.glue.machine.mix, 0.4) && same (glue.mix, 0.4) && (glue.state == GlueState::Active) == cd
            && params.bypassCompressor == ! cd && same (params.compressorMix, cd ? 0.4 : 1.0);
    }
    ok (machine, "every target's machine sets the mix at 0.4 — cd's glue, the one the auto plan turns on, compresses at 40 %, and a "
        "glue out of the chain leaves the stage at 1, as before");

    const Mix mix;
    {
        auto sp = measured (mix, "allStreaming"); auto& s = *sp;
        (void) s.apply (command::SetManual { 3, true });
        GlueFields<Touched> tick; tick.on = true;
        ok (s.apply (command::EditDevice { 4, tick }).rejection == Rejection::None && s.snapshot().view().plan.glue.state == GlueState::Active
            && same (s.snapshot().view().plan.glue.mix, 0.4) && ! s.project().devices.glue.hand.mix, "a tick in the manual mode: the glue in at 40 %, the project's mix untouched");
        bool taken = true;
        for (const double n : { 0.6, 0.0, 1.0, 0.5, 0.2 })
        {
            GlueFields<Touched> knob; knob.mix = n;
            taken = taken && s.apply (command::EditDevice { 5, knob }).rejection == Rejection::None && same (*s.project().devices.glue.hand.mix, n)
                && same (s.snapshot().view().plan.glue.mix, n);
        }
        ok (taken, "0.6, 0, 1, 0.5 and 0.2 are each taken as written, and sound so");
        GlueFields<Touched> over; over.mix = 1.01;
        GlueFields<Touched> under; under.mix = -0.01;
        ok (s.apply (command::EditDevice { 6, over }).rejection == Rejection::OutOfDomain && s.apply (command::EditDevice { 7, under }).rejection == Rejection::OutOfDomain
            && same (*s.project().devices.glue.hand.mix, 0.2), "1.01 and −0.01 are refused, the value kept");
        const auto saved = s.exportProject();
        auto copy = measured (mix, "allStreaming");
        ok (copy->apply (command::ImportProject { 8, saved.view() }).rejection == Rejection::None && copy->project().devices.glue.hand.mix
            && same (*copy->project().devices.glue.hand.mix, 0.2) && same (copy->snapshot().view().plan.glue.mix, 0.2), "a saved project keeps the mix, and sounds at it");
        ok (s.apply (command::SetTarget { 9, "cd" }).rejection == Rejection::None && ! s.project().devices.glue.hand.mix
            && same (s.snapshot().view().plan.glue.mix, 0.4), "a change of target resets it: 40 % again");
    }
    // At mix 1 the glue is v0.16.0's downward glue to the bit: felitronics_session_master_job_tests pins it, natively and
    // as wasm.
}

int main()
{
    std::printf ("felitronics::session — glue and saturation from the normalised input\n");
    theCurve();
    theMachine();
    aPersonsKnob();
    withoutAP95();
    theTempo();
    theSaturation();
    oneSystemOfLevels();
    theCalibration();
    theCut();
    theReport();
    theType();
    outStatesItsNumbers();
    limiterAndDitherAsTheChainGetsThem();
    theMix();
    return felitronics::test::report();
}
