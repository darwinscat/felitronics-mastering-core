// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// TILT AND LOW — two devices, one EQ stage (technical decision 3О10): the machine's layer on every target and on a quiet
// input (tilt never, low only as vinyl's +0.5 dB); the knobs' ±6 dB domain with 0 and 1.25 dB exact and values past the
// slider's travel taken as written; the geometry — tilt about 1 kHz, −dB below and +dB above, the ends 2·dB apart; low a
// static shelf at 80 Hz, Q 0.6; the three EQ devices together, each moving its own contribution, the curve what the
// engine runs from the bands written; a person's layer kept hidden, saved and imported, reset by a change of target.

#include "../../../tests/DeclaredBudget.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Grid.h"
#include "Planner.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/eq/EqEngine.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace eq = felitronics::eq;

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return detail::same (a, b); }
bool sameBits (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }

// The planner on a loudness reading alone: the target, and an input at `lufs`.
struct Planned { Devices devices; DevicePlans plans; detail::PlanFindings found; };
Planned plan (std::string_view target, double lufs, std::uint32_t channels = 2)
{
    MeasurementValue loudness[] { { "integratedLufs", lufs, MeasurementReason::None, 0 }, { "truePeakDb", -1.0, MeasurementReason::None, 0 } };
    MeasurementResult results[kAnalyzers] {};
    for (std::size_t i = 0; i < kAnalyzers; ++i)
    { results[i].analyzer = Analyzer (i); results[i].status = MeasurementStatus::Unavailable; results[i].reason = MeasurementReason::NotImplemented; }
    results[0].status = MeasurementStatus::Ready; results[0].reason = MeasurementReason::None; results[0].numbers = loudness;
    detail::PlanInputs in;
    in.rules = detail::rules(); in.row = *in.rules.find (target); in.channels = channels; in.sampleRate = 48000; in.frames = 48000 * 60;
    in.measurements = results;
    Planned p;
    detail::propose (in, p.devices, p.plans, p.found);
    return p;
}

// A placed session: a 12 s mix through the real pump.
std::unique_ptr<Session> placed (const char* target = "allStreaming")
{
    constexpr unsigned rate = 48000, frames = rate * 12;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i)
        pcm[i] = float (0.3 * felitronics::core::det::sin (2 * kPi * 55.0 * i / rate) + 0.1 * felitronics::core::det::sin (2 * kPi * 440.0 * i / rate));
    const float* planes[] { pcm.data(), pcm.data() };
    auto s = Session::create().session;
    (void) s->apply (command::SetTarget { 1, target });
    ok (s->apply (command::Load { 2, { planes, 2, frames, rate }, {} }).rejection == Rejection::None, "PRECONDITION: the mix loads");
    for (unsigned i = 0; i < 2000000 && (s->measurementJob() || s->needlesJob()); ++i) (void) s->step (16);
    ok (s->snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: placed, the plan ready");
    return s;
}
double coreDb (const eq::BandParams& band, double rate, double hz)
{
    return 20 * std::log10 (std::abs (eq::bandResponse (band, rate, 2 * kPi * hz / rate)));
}
eq::BandParams tiltBand (double db, const config::Engine& e)
{
    eq::BandParams b; b.on = true; b.type = eq::FilterType::Tilt; b.lanes[0].freq = e.tilt.freqHz; b.lanes[0].gainDb = db; return b;
}
eq::BandParams lowBand (double db, const config::Engine& e)
{
    eq::BandParams b; b.on = true; b.type = eq::FilterType::LowShelf; b.lanes[0].freq = e.low.freqHz; b.lanes[0].Q = e.low.q; b.lanes[0].gainDb = db; return b;
}

//==============================================================================

void theMachine()
{
    felitronics::test::group ("the machine: tilt never; low on every target, ticked only as vinyl's +0.5 dB, and not on a quiet input");
    const auto r = detail::rules();
    bool tilt = true, low = true, offered = true;
    for (std::uint16_t row = 0; row < r.rows; ++row)
        for (const double lufs : { -14.0, -55.0, std::nextafter (-55.0, -60.0) })
            for (const std::uint32_t channels : { 1u, 2u })
            {
                const auto target = r.row (row);
                const auto p = plan (target.key, lufs, channels);
                const bool quiet = lufs < -55.0;
                tilt = tilt && ! p.devices.tilt.machine.on && same (p.devices.tilt.machine.db, 0.0) && p.plans.tilt.heldBack == HeldBack::None
                    && p.plans.tilt.target == 0 && p.plans.tilt.measured == 0 && p.plans.tilt.needs == 0;
                const bool medium = target.lowDb.has_value();
                low = low && p.devices.low.machine.on == (medium && ! quiet)
                    && same (p.devices.low.machine.db, medium ? target.lowDb->toDouble() : 0.0)
                    && p.plans.low.heldBack == (medium && quiet ? HeldBack::Quiet : HeldBack::None)
                    && p.plans.low.target == (medium ? 3u : 0u) && p.plans.low.measured == 0 && p.plans.low.needs == 0;
                offered = offered && detail::offered (r, row, channels, Device::Low) && detail::offered (r, row, channels, Device::Tilt);
            }
    ok (tilt, "tilt: off and flat on every target, source and input — the machine does not touch timbre, and reads nothing for it");
    ok (low, "low: off at 0 dB everywhere but vinyl; on vinyl +0.5 dB from its target, ticked — and unticked below −55 LUFS, strictly");
    ok (offered, "both are offered on every target and source");
    const auto lp = plan ("lp", -14.0);
    ok (lp.devices.low.machine.on && same (lp.devices.low.machine.db, 0.5), "vinyl: low on at +0.5 dB");
}

void theKnobs()
{
    felitronics::test::group ("the knobs: ±6 dB, 0 and 1.25 dB exact, values between steps and past the slider's travel as written");
    auto sp = placed(); auto& s = *sp;
    const auto tiltOf = [&] (double db) { TiltFields<Touched> f; f.on = true; f.db = db; return s.apply (command::EditDevice { 3, f }); };
    const auto lowOf = [&] (double db) { LowFields<Touched> f; f.on = true; f.db = db; return s.apply (command::EditDevice { 4, f }); };
    bool taken = true;
    for (const double db : { -6.0, 6.0, 0.0, 1.25, -0.0, 5.75, 3.0000001, 0.123456789 })
    {
        taken = taken && tiltOf (db).rejection == Rejection::None && sameBits (*s.project().devices.tilt.hand.db, detail::kept (db))
             && lowOf (db).rejection == Rejection::None && sameBits (*s.project().devices.low.hand.db, detail::kept (db));
        detail::EqStage stage; detail::writeEq (s.project().devices, detail::rules(), stage);
        taken = taken && sameBits (stage.bands[detail::eqBand (Device::Tilt)].lanes[0].gainDb, detail::kept (db))
             && sameBits (stage.bands[detail::eqBand (Device::Low)].lanes[0].gainDb, detail::kept (db));
    }
    ok (taken, "every value of the domain is kept and written into its band bit for bit — no rounding to the step, no clamp to the travel");
    for (const double db : { std::nextafter (6.0, 7.0), std::nextafter (-6.0, -7.0), 30.0 })
    {
        const auto t = tiltOf (db), l = lowOf (db);
        ok (t.rejection == Rejection::OutOfDomain && t.field == 1 && l.rejection == Rejection::OutOfDomain && l.field == 1,
            "past ±6 dB: refused whole, on the knob's field (" + std::to_string (db) + ")");
    }
    ok (tiltOf (std::numeric_limits<double>::quiet_NaN()).rejection == Rejection::NotFinite
        && lowOf (std::numeric_limits<double>::infinity()).rejection == Rejection::NotFinite, "not a number: refused");
    // Inside the domain nothing in the engine clamps: core's design takes ±30 dB, the domain is ±6.
    const auto cfg = config::Config::load();
    ok (std::abs (coreDb (tiltBand (6.0, cfg.config.engine), 96000, 20.0) + 6.0) < 0.05
        && std::abs (coreDb (lowBand (-6.0, cfg.config.engine), 96000, 5.0) + 6.0) < 0.05,
        "at the domain's ends the engine gives the whole 6 dB: no clamp hides behind the knob");
}

void theGeometry()
{
    felitronics::test::group ("the geometry: tilt about 1 kHz, −dB below and +dB above, the ends 2·dB apart; low a static shelf at 80 Hz, Q 0.6");
    const auto cfg = config::Config::load();
    const auto& e = cfg.config.engine;
    ok (same (e.tilt.freqHz, 1000.0) && same (e.low.freqHz, 80.0) && same (e.low.q, 0.6), "the config's pivot, corner and Q");
    const auto r = detail::rules();
    bool ends = true, pivot = true, shelf = true;
    for (const double db : { -6.0, -1.25, 1.25, 3.0, 6.0 })
        for (const double rate : { 48000.0, 96000.0 })
        {
            Project p;
            p.devices.tilt.hand = { true, db };
            EqPoint curve[kEqCurvePoints];
            detail::eqCurve (p, r, rate, curve);
            const double bottom = curve[0].db, top = curve[kEqCurvePoints - 1].db;
            double atPivot = 1e9;
            for (const auto& point : curve) if (std::abs (point.hz - 1000.0) < 30.0) atPivot = point.db;
            ends = ends && std::abs (bottom + db) < 0.05 && std::abs (top - db) < 0.12 && std::abs ((top - bottom) - 2 * db) < 0.15;
            pivot = pivot && std::abs (atPivot) < 0.05 * std::abs (db) + 1e-9;
            Project q;
            q.devices.low.hand = { true, db };
            detail::eqCurve (q, r, rate, curve);
            // A shelf: the whole gain far below the corner and nothing at the top; at the corner what its Q states — the
            // power Q²(A − 1)² + A of a boost by A, and its reciprocal for a cut.
            const auto band = lowBand (db, e);
            const double boost = std::pow (10.0, std::abs (db) / 20), corner = 10 * std::log10 (0.36 * (boost - 1) * (boost - 1) + boost);
            shelf = shelf && std::abs (coreDb (band, rate, 2.0) - db) < 0.02 && std::abs (curve[kEqCurvePoints - 1].db) < 0.01
                 && std::abs (coreDb (band, rate, 80.0) - (db < 0 ? -corner : corner)) < 0.01;
            for (const auto& point : curve) shelf = shelf && std::abs (point.db - coreDb (band, rate, point.hz)) < 1e-6;
        }
    ok (ends, "tilt: the low end down by the knob, the top up by it, 2·dB between them");
    ok (pivot, "and the pivot at 1 kHz unmoved");
    ok (shelf, "low: the whole gain below 80 Hz, the corner's gain what Q 0.6 states, none at the top — core's shelf, point by point");
}

void threeDevicesOneStage()
{
    felitronics::test::group ("the high-pass, tilt and low together: each its own contribution; the curve and the engine's sound from the bands written");
    const auto cfg = config::Config::load();
    const auto& e = cfg.config.engine;
    const auto r = detail::rules();
    const double rate = 48000;
    Project p;
    p.devices.hpf.hand = { true, 38.5, 24 };
    p.devices.tilt.hand = { true, 1.25 };
    p.devices.low.hand = { true, -2.5 };
    const auto curveOf = [&] (const Project& project) { std::vector<EqPoint> c (kEqCurvePoints); detail::eqCurve (project, r, rate, c); return c; };
    const auto all = curveOf (p);
    eq::BandParams hp; hp.on = true; hp.type = eq::FilterType::HighPass; hp.lanes[0].freq = 38.5; hp.lanes[0].slope = 24;
    bool summed = true, own = true;
    for (std::size_t i = 0; i < kEqCurvePoints; ++i)
        summed = summed && std::abs (all[i].db - (coreDb (hp, rate, all[i].hz) + coreDb (tiltBand (1.25, e), rate, all[i].hz)
                                                  + coreDb (lowBand (-2.5, e), rate, all[i].hz))) < 1e-6;
    ok (summed, "the summed curve is the three decided filters, point by point");
    // Take one device out: the curve loses exactly that device's contribution.
    for (const Device device : { Device::Hpf, Device::Tilt, Device::Low })
    {
        Project q = p;
        if (device == Device::Hpf) q.devices.hpf.hand.on = false;
        if (device == Device::Tilt) q.devices.tilt.hand.on = false;
        if (device == Device::Low) q.devices.low.hand.on = false;
        const auto without = curveOf (q);
        const auto band = device == Device::Hpf ? hp : device == Device::Tilt ? tiltBand (1.25, e) : lowBand (-2.5, e);
        for (std::size_t i = 0; i < kEqCurvePoints; ++i)
            own = own && std::abs ((all[i].db - without[i].db) - coreDb (band, rate, all[i].hz)) < 1e-6;
    }
    ok (own, "a device's tick takes out its own contribution and nothing of another's");
    // THE SOUND: the engine with the bands written is the engine with the decided filters — and nothing dynamic.
    detail::EqStage stage; detail::writeEq (p.devices, r, stage);
    bool staticBands = true;
    for (const auto& band : stage.bands)
        staticBands = staticBands && ! band.dyn.on && ! band.swept && ! band.bypass && ! band.lanes[1].on && ! band.lanes[2].on && ! band.lanes[3].on && ! band.lanes[4].on;
    ok (staticBands, "no band is dynamic, swept or split: three static stereo filters");
    auto written = std::make_unique<eq::EqEngine>(), decided = std::make_unique<eq::EqEngine>();
    constexpr int block = 256, blocks = 64;
    ok (written->prepare (rate, block, 2) && decided->prepare (rate, block, 2), "PRECONDITION: the engines prepare");
    for (int i = 0; i < eq::EqEngine::kMaxBands; ++i) written->setBand (i, stage.bands[i]);
    decided->setBand (0, hp); decided->setBand (1, tiltBand (1.25, e)); decided->setBand (2, lowBand (-2.5, e));
    std::vector<float> a (2 * block * blocks), b;
    std::uint64_t seed = 0x2545F4914F6CDD1Dull;
    for (auto& x : a) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; x = float (double (std::int64_t (seed >> 11) % 2000001 - 1000000) / 4000000.0); }
    b = a;
    bool identical = true, moved = false;
    for (int k = 0; k < blocks; ++k)
    {
        float* wa[] { a.data() + 2 * k * block, a.data() + (2 * k + 1) * block };
        float* wb[] { b.data() + 2 * k * block, b.data() + (2 * k + 1) * block };
        const float before = wa[0][block / 2];
        identical = identical && written->process (wa, 2, block) && decided->process (wb, 2, block);
        moved = moved || std::bit_cast<std::uint32_t> (before) != std::bit_cast<std::uint32_t> (wa[0][block / 2]);
    }
    for (std::size_t i = 0; i < a.size(); ++i) identical = identical && std::bit_cast<std::uint32_t> (a[i]) == std::bit_cast<std::uint32_t> (b[i]);
    ok (identical && moved, "the engine fed the bands written gives, sample for sample, the sound of the decided filters");
}

void aPersonsLayer()
{
    felitronics::test::group ("a person's tilt and low: sounding with the panel hidden, kept by a saved project, reset by a change of target");
    auto sp = placed(); auto& s = *sp;
    ok (! s.project().manual, "PRECONDITION: the panel is hidden");
    TiltFields<Touched> tilt; tilt.on = true; tilt.db = 1.25;
    LowFields<Touched> low; low.on = true; low.db = -0.75;
    ok (s.apply (command::EditDevice { 3, tilt }).rejection == Rejection::None && s.apply (command::EditDevice { 4, low }).rejection == Rejection::None,
        "both are edited with the panel hidden");
    const auto hidden = s.snapshot();
    bool sounding = false;
    for (const auto& point : hidden.view().eqCurve) sounding = sounding || (point.hz > 5000 && std::abs (point.db - 1.25) < 0.2);
    ok (sounding && hidden.view().handFieldCount == 4, "and sound: the curve carries them, four touched fields counted");
    (void) s.apply (command::SetManual { 5, true });
    const auto shown = s.snapshot();
    bool unchanged = shown.view().plan.key == hidden.view().plan.key;
    for (std::size_t i = 0; i < kEqCurvePoints; ++i) unchanged = unchanged && sameBits (shown.view().eqCurve[i].db, hidden.view().eqCurve[i].db);
    ok (unchanged, "showing the panel changes neither the curve nor the plan");
    const auto master = s.apply (command::Master { 6 });
    ok (master.rejection == Rejection::None && sameBits (*s.jobRecipe().project.devices.tilt.hand.db, 1.25)
        && sameBits (*s.jobRecipe().project.devices.low.hand.db, -0.75), "a master's recipe carries both");
    (void) s.apply (command::Cancel { 7, master.job });
    const auto saved = s.exportProject();
    ok (saved.view().find ("[tilt]\non.hand = true\ndb.hand = 1.25\n") != std::string_view::npos
        && saved.view().find ("[low]\non.hand = true\ndb.hand = -0.75\n") != std::string_view::npos, "the project file writes them as separate devices");
    auto copy = placed();
    ok (copy->apply (command::ImportProject { 8, saved.view() }).rejection == Rejection::None
        && sameBits (*copy->project().devices.tilt.hand.db, 1.25) && sameBits (*copy->project().devices.low.hand.db, -0.75)
        && *copy->project().devices.tilt.hand.on && *copy->project().devices.low.hand.on, "and an import keeps them exactly");
    TiltFields<Mark> back; back.db = true;
    ok (s.apply (command::RevertEdits { 9, back }).rejection == Rejection::None && ! s.project().devices.tilt.hand.db
        && s.project().devices.tilt.hand.on && s.project().devices.low.hand.db, "a revert takes back the one field it names");
    ok (s.apply (command::SetTarget { 10, "nowhere" }).rejection == Rejection::UnknownTarget && s.project().devices.low.hand.db,
        "a change of target that does not happen keeps them");
    ok (s.apply (command::SetTarget { 11, "lp" }).rejection == Rejection::None && ! s.project().devices.low.hand.db
        && ! s.project().devices.tilt.hand.on && s.snapshot().view().handFieldCount == 0
        && s.project().devices.low.machine.on && same (s.project().devices.low.machine.db, 0.5),
        "the change sent resets both, and vinyl's own +0.5 dB stands");
}

void theFilesMachineStays()
{
    felitronics::test::group ("an imported machine layer stays: a file's low on streaming is kept, the planner's off shown beside it");
    auto sp = placed(); auto& s = *sp;
    const auto own = s.exportProject();
    std::string file (own.view());
    file += "\n[low]\non.machine = true\ndb.machine = 1.5\n";
    ok (s.apply (command::ImportProject { 3, file }).rejection == Rejection::None, "PRECONDITION: the file imports");
    const auto v = s.snapshot();
    ok (s.project().devices.low.machine.on && same (s.project().devices.low.machine.db, 1.5) && v.view().plan.fromFile,
        "the file's low — on at 1.5 dB — is the machine's layer");
    bool on = false, db = false;
    for (const auto& d : v.view().machineDifferences)
        if (d.device == Device::Low)
        {
            on = on || (d.field == 0 && same (d.fileValue, 1.0) && same (d.coreValue, 0.0));
            db = db || (d.field == 1 && same (d.fileValue, 1.5) && same (d.coreValue, 0.0));
        }
    ok (on && db, "and the planner's — off at 0 dB — is shown beside it, field by field");
    ok (s.apply (command::AdoptMachine { 4 }).rejection == Rejection::None && ! s.project().devices.low.machine.on
        && same (s.project().devices.low.machine.db, 0.0), "taken only by adoptMachine");
}

void aQuietInput()
{
    felitronics::test::group ("a quiet input: vinyl's correction is not placed; a person's low sounds all the same");
    const auto quiet = plan ("lp", -60.0);
    ok (! quiet.devices.low.machine.on && same (quiet.devices.low.machine.db, 0.5) && quiet.plans.low.heldBack == HeldBack::Quiet,
        "the machine's low is off, its +0.5 dB left on the knob");
    Devices devices = quiet.devices;
    devices.low.hand.on = true;
    detail::EqStage stage; detail::writeEq (devices, detail::rules(), stage);
    ok (stage.bands[detail::eqBand (Device::Low)].on && same (stage.bands[detail::eqBand (Device::Low)].lanes[0].gainDb, 0.5),
        "a person's tick writes the band");
}
} // namespace

int main()
{
    theMachine();
    theKnobs();
    theGeometry();
    threeDevicesOneStage();
    aPersonsLayer();
    theFilesMachineStays();
    aQuietInput();
    return felitronics::test::report();
}
