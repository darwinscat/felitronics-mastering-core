// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE EQ BANDS — five static bands of the one EQ stage, a person's only (the owner's decision of 01.10): body, mud,
// forward, brightness and air, each a bell or a high shelf from [bands]. The machine leaves them at 0 dB on every target
// and input; a band at 0 dB is no band, so the master is the one made before the device was there (the stage held to a
// copy of the previous writeEq, the master to a master without the device); +3 dB moves the curve and the engine's
// rendered sound at the band's frequency by what the filter states; the commands refuse a gain outside its domain on its
// field and a revert gives the machine's 0 back; the kit draws the same curve; the norm of the curve stays the shelves'.

#include "BuildContract.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Grid.h"
#include "Planner.h"
#include "TextFacts.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Kit.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/eq/EqEngine.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace eq = felitronics::eq;

namespace previous
{
using namespace felitronics::session::detail;
// THE PREVIOUS writeEq — src/EqCurve.cpp at 3abfa7c, before the EQ bands, copied unchanged but for its namespace: the
// oracle a band at 0 dB is held to.
bool sameNumber (double a, double b) noexcept { return a <= b && a >= b; }
double number (felitronics::toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto n = v.integer()) return double (*n);
    storageOverflow();
}
eq::BandParams band (eq::FilterType type, bool on, double hz) noexcept
{
    eq::BandParams b;
    b.on = on;
    b.type = type;
    b.lanes[0].freq = hz;
    return b;
}
void writeEq (const HpfFields<Value>& hpf, const TiltFields<Value>& tilt, const LowFields<Value>& low, const Rules& rules,
              EqStage& stage) noexcept
{
    auto& h = stage.bands[eqBand (Device::Hpf)];
    h = band (eq::FilterType::HighPass, hpf.on, hpf.fq);
    h.lanes[0].slope = hpf.slope;

    auto& t = stage.bands[eqBand (Device::Tilt)];
    t = band (eq::FilterType::Tilt, tilt.on && ! sameNumber (tilt.db, 0), number (rules.engine.find ("tilt").find ("freqHz")));
    t.lanes[0].gainDb = tilt.db;

    const auto config = rules.engine.find ("low");
    auto& l = stage.bands[eqBand (Device::Low)];
    l = band (eq::FilterType::LowShelf, low.on && ! sameNumber (low.db, 0), number (config.find ("freqHz")));
    l.lanes[0].Q = number (config.find ("q"));
    l.lanes[0].gainDb = low.db;
}
} // namespace previous

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return detail::same (a, b); }
bool sameBits (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }

// A band's gain in a BandsFields of any form, by its place (the order Project.h writes them).
template <class F> auto& gainAt (F& fields, std::size_t i)
{
    switch (i)
    {
        case 0: return fields.body;
        case 1: return fields.mud;
        case 2: return fields.forward;
        case 3: return fields.brightness;
        default: return fields.air;
    }
}

// The planner on a loudness reading alone.
struct Planned { Devices devices; DevicePlans plans; detail::PlanFindings found; };
Planned plan (std::string_view target, double lufs, std::uint32_t channels)
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

// A placed session: a short mix through the real pump, every device offered unless `offered` says otherwise.
std::unique_ptr<Session> placed (std::uint32_t offered = kAllDevices)
{
    constexpr unsigned rate = 48000, frames = rate * 6;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i)
        pcm[i] = float (0.3 * felitronics::core::det::sin (2 * kPi * 55.0 * i / rate) + 0.1 * felitronics::core::det::sin (2 * kPi * 440.0 * i / rate)
                        + 0.05 * felitronics::core::det::sin (2 * kPi * 3000.0 * i / rate));
    const float* planes[] { pcm.data(), pcm.data() };
    Capabilities caps;
    caps.offeredDevices = offered;
    auto s = Session::create (caps, config::Config::versions().all).session;
    ok (s->apply (command::Load { 2, { planes, 2, frames, rate }, {} }).rejection == Rejection::None, "PRECONDITION: the mix loads");
    for (unsigned i = 0; i < 2000000 && (s->measurementJob() || s->needlesJob()); ++i) (void) s->step (16);
    ok (s->snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: placed, the plan ready");
    return s;
}

// The band a gain of `db` on band i writes, as the engine designs it.
eq::BandParams bandOf (std::size_t i, double db)
{
    BandsFields<Value> fields;
    fields.on = true;
    gainAt (fields, i) = db;
    detail::EqStage stage;
    detail::writeEq (HpfFields<Value> {}, TiltFields<Value> {}, LowFields<Value> {}, fields, detail::rules(), stage);
    return stage.bands[detail::bandsSlot (detail::rules(), i)];
}
double coreDb (const eq::BandParams& band, double rate, double hz)
{
    return 20 * std::log10 (std::abs (eq::bandResponse (band, rate, 2 * kPi * hz / rate)));
}
const config::EqMove& moveOf (const config::Engine& e, std::size_t i)
{
    const config::EqMove* moves[] { &e.bands.body, &e.bands.mud, &e.bands.forward, &e.bands.brightness, &e.bands.air };
    return *moves[i];
}

//==============================================================================

void theConfig()
{
    felitronics::test::group ("the config: five bands in free slots of the stage, as the old page had them");
    const auto cfg = config::Config::load();
    ok (cfg.ok(), "PRECONDITION: the embedded config binds");
    const auto& e = cfg.config.engine;
    struct Row { int band; config::BandType type; double hz, q, from, to; };
    const Row rows[] { { 3, config::BandType::Bell, 160, 0.7, -3, 3 }, { 4, config::BandType::Bell, 300, 0.8, -3, 0 },
                       { 5, config::BandType::Bell, 3000, 0.7, -3, 3 }, { 6, config::BandType::HighShelf, 8000, 0.6, -3, 3 },
                       { 7, config::BandType::HighShelf, 12000, 0.6, -3, 3 } };
    bool as = true;
    for (std::size_t i = 0; i < 5; ++i)
    {
        const auto& m = moveOf (e, i);
        as = as && m.band == rows[i].band && m.type == rows[i].type && same (m.freqHz, rows[i].hz) && same (m.q, rows[i].q)
            && same (m.hard.min, rows[i].from) && same (m.hard.max, rows[i].to) && same (m.step, 0.1)
            && detail::bandsSlot (detail::rules(), i) == rows[i].band;
    }
    ok (as, "body 160 Hz bell Q 0.7, mud 300 Hz bell Q 0.8 (−3…0), forward 3 kHz bell Q 0.7, brightness 8 kHz and air 12 kHz high "
            "shelves Q 0.6; ±3 dB by 0.1 dB, in bands 3–7");
    ok (e.hpf.band == 0 && e.tilt.band == 1 && e.low.band == 2 && e.deEsser.band == 8, "the other devices keep their bands");
}

void theMachine()
{
    felitronics::test::group ("the machine never sets them: 0 dB on every target, source and input, and nothing read for them");
    const auto r = detail::rules();
    bool zero = true, offered = true;
    for (std::uint16_t row = 0; row < r.rows; ++row)
        for (const double lufs : { -14.0, -60.0 })
            for (const std::uint32_t channels : { 1u, 2u })
            {
                const auto p = plan (r.row (row).key, lufs, channels);
                for (std::size_t i = 0; i < 5; ++i) zero = zero && sameBits (gainAt (p.devices.bands.machine, i), 0.0);
                zero = zero && p.plans.bands.heldBack == HeldBack::None && p.plans.bands.target == 0 && p.plans.bands.measured == 0
                    && p.plans.bands.needs == 0;
                offered = offered && detail::offered (r, row, channels, Device::Bands);
            }
    ok (zero, "every band at 0 dB, no target or measurement behind it, nothing awaited");
    ok (offered, "offered on every target and source");
    auto s = placed();
    const auto v = s->snapshot();
    bool placedZero = ! v.view().plan.devices.bands.on;
    for (std::size_t i = 0; i < 5; ++i) placedZero = placedZero && sameBits (gainAt (s->project().devices.bands.machine, i), 0.0);
    ok (placedZero, "a placed session's machine layer holds 0 dB on every band, and the plan says the device does not sound");
}

void theCommands()
{
    felitronics::test::group ("the commands: a gain in its domain kept as written, one outside refused on its field, a revert gives 0 back");
    auto sp = placed(); auto& s = *sp;
    const auto r = detail::rules();
    const auto edit = [&] (std::size_t i, double db, CommandId id) { BandsFields<Touched> f; gainAt (f, i) = db; return s.apply (command::EditDevice { id, f }); };
    bool kept = true, refused = true, named = true;
    CommandId id = 10;
    for (std::size_t i = 0; i < 5; ++i)
    {
        const double top = i == 1 ? 0.0 : 6.0;
        for (const double db : { -6.0, top, 1.25, -0.123456789 })
        {
            if (i == 1 && db > 0.0) continue;
            kept = kept && edit (i, db, ++id).rejection == Rejection::None && sameBits (*gainAt (s.project().devices.bands.hand, i), detail::kept (db));
        }
        for (const double db : { std::nextafter (top, 7.0), std::nextafter (-6.0, -7.0), 30.0 })
        {
            const auto before = s.revision();
            const auto a = edit (i, db, ++id);
            refused = refused && a.rejection == Rejection::OutOfDomain && a.field == i && s.revision() == before;
            named = named && text::detail::deviceFieldTerm (Device::Bands, std::uint8_t (a.field)) == text::Term (unsigned (text::Term::FieldBandsBody) + i);
        }
        refused = refused && edit (i, std::numeric_limits<double>::quiet_NaN(), ++id).rejection == Rejection::NotFinite;
    }
    ok (kept, "every value of a band's domain is kept bit for bit — no rounding to the step, no clamp to the travel");
    ok (refused, "past ±6 dB (mud: above 0 dB) or not a number: refused whole, on the band's own field, the revision unmoved");
    ok (named, "and the refusal names the band: FieldBandsBody … FieldBandsAir");
    ok (s.apply (command::EditDevice { ++id, [] { BandsFields<Touched> f; f.mud = 0.1; return f; }() }).rejection == Rejection::OutOfDomain,
        "mud is a cut only: +0.1 dB is refused");
    BandsFields<Mark> back; back.body = true; back.air = true;
    ok (s.apply (command::RevertEdits { ++id, back }).rejection == Rejection::None && ! s.project().devices.bands.hand.body
        && ! s.project().devices.bands.hand.air && s.project().devices.bands.hand.mud, "a revert takes back the bands it names");
    BandsFields<Mark> all; all.body = all.mud = all.forward = all.brightness = all.air = true;
    ok (s.apply (command::RevertEdits { ++id, all }).rejection == Rejection::None, "PRECONDITION: everything reverted");
    const auto sounding = detail::settingsOf (r, s.project().devices.bands);
    bool zero = true;
    for (std::size_t i = 0; i < 5; ++i) zero = zero && sameBits (gainAt (sounding, i), 0.0);
    detail::EqStage stage; detail::writeEq (s.project().devices, r, stage);
    for (std::size_t i = 0; i < 5; ++i) zero = zero && ! stage.bands[detail::bandsSlot (r, i)].on;
    ok (zero && ! s.snapshot().view().plan.devices.bands.on, "and gives the machine's 0 dB back: no band sounds");
    auto shell = placed (kAllDevices & ~(1u << unsigned (Device::Bands)));
    BandsFields<Touched> one; one.body = 1.0;
    ok (shell->apply (command::EditDevice { 3, one }).rejection == Rejection::NotOffered
        && shell->snapshot().view().plan.devices.bands.heldBack == HeldBack::Shell,
        "a shell that does not offer them is refused an edit, and the plan says why");
}

void theGeometry()
{
    felitronics::test::group ("+3 dB on each band: the curve moves at the band's frequency by what the filter states, core's response point by point");
    const auto cfg = config::Config::load();
    const auto& e = cfg.config.engine;
    const auto r = detail::rules();
    for (std::size_t i = 0; i < 5; ++i)
    {
        const auto& m = moveOf (e, i);
        const double db = i == 1 ? -3.0 : 3.0;   // mud is a cut
        bool pointwise = true, mirror = true;
        double atFrequency = 0.0;
        for (const double rate : { 48000.0, 96000.0 })
        {
            Project p;
            gainAt (p.devices.bands.hand, i) = db;
            EqPoint curve[kEqCurvePoints];
            detail::eqCurve (p, r, rate, curve);
            const auto band = bandOf (i, db);
            for (const auto& point : curve) pointwise = pointwise && std::abs (point.db - coreDb (band, rate, point.hz)) < 1e-6;
            const auto cut = bandOf (i, -db);
            for (const auto& point : curve) mirror = mirror && std::abs (coreDb (cut, rate, point.hz) + point.db) < 1e-9 + 1e-6 * std::abs (point.db);
            if (rate < 50000.0) atFrequency = coreDb (band, rate, m.freqHz);
        }
        // A bell: the whole gain at its centre. A high shelf at its corner: the power Q²(A − 1)² + A of a boost by A.
        const double boost = std::pow (10.0, std::abs (db) / 20);
        const double expected = m.type == config::BandType::Bell ? db : 10 * std::log10 (m.q * m.q * (boost - 1) * (boost - 1) + boost);
        const std::string name (detail::kBandNames[i]);
        ok (pointwise, name + ": the curve is core's response of the band written, within 1e-6 dB on every point, at 48 and 96 kHz");
        ok (std::abs (atFrequency - expected) < 0.01, name + ": " + std::to_string (atFrequency) + " dB at " + std::to_string (m.freqHz)
                                                       + " Hz, as stated (" + std::to_string (expected) + " ± 0.01)");
        ok (mirror, name + ": a cut is the boost's mirror");
    }
}

void theRenderedSound()
{
    felitronics::test::group ("the engine fed the bands written renders a sine at each band's frequency moved by the curve's amount");
    const auto cfg = config::Config::load();
    const auto r = detail::rules();
    constexpr double rate = 48000;
    constexpr int block = 240, settle = 200, measure = 200;   // 1 s to settle, 1 s measured: whole periods of every frequency
    for (std::size_t i = 0; i < 5; ++i)
    {
        const double hz = moveOf (cfg.config.engine, i).freqHz, db = i == 1 ? -3.0 : 3.0;
        Devices devices;
        gainAt (devices.bands.hand, i) = db;
        detail::EqStage stage; detail::writeEq (devices, r, stage);
        auto engine = std::make_unique<eq::EqEngine>();
        ok (engine->prepare (rate, block, 2), "PRECONDITION: the engine prepares");
        for (int b = 0; b < eq::EqEngine::kMaxBands; ++b) engine->setBand (b, stage.bands[b]);
        std::vector<float> left (block), right (block);
        double in = 0, out = 0;
        std::uint64_t n = 0;
        for (int k = 0; k < settle + measure; ++k)
        {
            for (int j = 0; j < block; ++j, ++n)
                left[std::size_t (j)] = right[std::size_t (j)] = float (0.25 * felitronics::core::det::sin (2 * kPi * hz * double (n) / rate));
            if (k >= settle) for (const float x : left) in += double (x) * x;
            float* planes[] { left.data(), right.data() };
            ok (k > 0 || engine->process (planes, 2, block), "PRECONDITION: the engine processes");
            if (k > 0) (void) engine->process (planes, 2, block);
            if (k >= settle) for (const float x : left) out += double (x) * x;
        }
        const double moved = 10 * std::log10 (out / in), expected = coreDb (bandOf (i, db), rate, hz);
        ok (std::abs (moved - expected) < 0.02, std::string (detail::kBandNames[i]) + ": the rendered sine moved " + std::to_string (moved)
                                                    + " dB, the curve says " + std::to_string (expected) + " (± 0.02 dB)");
    }
}

void aBandAtZeroIsNoBand()
{
    felitronics::test::group ("a band at 0 dB is no band: the stage the previous writeEq wrote, and the master made without the device");
    const auto r = detail::rules();
    // THE STAGE against the previous code: every band of the 24, field by field, for devices with and without hands — and
    // for the bands ticked off by hand with gains that would sound.
    bool asBefore = true;
    for (const int hands : { 0, 1, 2 })
    {
        Devices devices;
        devices.hpf.machine = { true, 31.5, 24 };
        devices.low.machine = { true, 0.5 };
        devices.bands.machine.on = true;
        if (hands > 0)
        {
            devices.tilt.hand.on = true; devices.tilt.hand.db = 1.25;
            for (std::size_t i = 0; i < 5; ++i) gainAt (devices.bands.hand, i) = hands == 2 ? (i == 1 ? -1.5 : 2.5) : i % 2 ? -0.0 : 0.0;
            if (hands == 2) devices.bands.hand.on = false;
        }
        detail::EqStage now, before;
        detail::writeEq (devices, r, now);
        previous::writeEq (detail::settingsOf (r, devices.hpf), detail::settingsOf (r, devices.tilt), detail::settingsOf (r, devices.low), r, before);
        for (int b = 0; b < eq::EqEngine::kMaxBands; ++b)
        {
            const auto& x = now.bands[b];
            const auto& y = before.bands[b];
            asBefore = asBefore && x.on == y.on && x.type == y.type && x.swept == y.swept && x.bypass == y.bypass && x.dyn.on == y.dyn.on
                && sameBits (x.dyn.rangeDb, y.dyn.rangeDb) && sameBits (x.dyn.thrDb, y.dyn.thrDb) && x.dyn.thrAuto == y.dyn.thrAuto
                && sameBits (x.dyn.atk, y.dyn.atk) && sameBits (x.dyn.rel, y.dyn.rel);
            for (std::size_t l = 0; l < std::size (x.lanes); ++l)
                asBefore = asBefore && x.lanes[l].on == y.lanes[l].on && sameBits (x.lanes[l].freq, y.lanes[l].freq)
                    && sameBits (x.lanes[l].Q, y.lanes[l].Q) && sameBits (x.lanes[l].gainDb, y.lanes[l].gainDb)
                    && x.lanes[l].slope == y.lanes[l].slope && x.lanes[l].bypass == y.lanes[l].bypass;
        }
    }
    ok (asBefore, "every band of the stage, every field, bit for bit what the previous writeEq wrote — bands at +0 and −0 dB "
                  "included, and the bands ticked off with gains");
    // THE MASTER: a session with every band set to 0 dB by hand renders the master of a session without the device.
    const auto master = [] (bool bandsByHand, double body)
    {
        auto s = placed();
        if (bandsByHand)
        {
            BandsFields<Touched> f; f.body = body; f.mud = 0.0; f.forward = -0.0; f.brightness = 0.0; f.air = 0.0;
            ok (s->apply (command::EditDevice { 20, f }).rejection == Rejection::None, "PRECONDITION: the bands are set");
        }
        ok (s->apply (command::Master { 21 }).rejection == Rejection::None, "PRECONDITION: a master is asked");
        for (unsigned i = 0; i < 4000000 && s->job(); ++i) (void) s->step (16);
        const auto token = s->pendingMaster();
        const auto pcm = s->viewMaster (token);
        return std::vector<float> (pcm.begin(), pcm.end());
    };
    const auto without = master (false, 0.0), zero = master (true, 0.0), body = master (true, 3.0);
    ok (! without.empty() && without.size() == zero.size()
        && std::memcmp (without.data(), zero.data(), without.size() * sizeof (float)) == 0,
        "the master with every band at 0 dB is, sample for sample, the master without them");
    ok (body.size() == without.size() && std::memcmp (without.data(), body.data(), without.size() * sizeof (float)) != 0,
        "CONTROL: +3 dB of body does reach the master");
}

void theKitAndTheNorm()
{
    felitronics::test::group ("the kit draws the same curve and knows the knobs; the curve's norm stays the shelves'");
    const auto r = detail::rules();
    Project p;
    p.devices.hpf.hand = { true, 30.0, 24 };
    p.devices.tilt.hand = { true, 1.0 };
    p.devices.bands.hand.body = 2.5; p.devices.bands.hand.mud = -1.5; p.devices.bands.hand.forward = 3.0;
    p.devices.bands.hand.brightness = -2.0; p.devices.bands.hand.air = 3.0;
    EqPoint snapshotCurve[kEqCurvePoints], kitCurve[kEqCurvePoints];
    detail::eqCurve (p, r, 48000, snapshotCurve);
    KitEq k;
    k.hpf = detail::settingsOf (r, p.devices.hpf); k.tilt = detail::settingsOf (r, p.devices.tilt); k.low = detail::settingsOf (r, p.devices.low);
    k.bands = detail::settingsOf (r, p.devices.bands);
    const auto preview = Kit::eqCurve (k, 48000, kitCurve);
    bool sameCurve = preview.status == CodecStatus::Ok;
    for (std::size_t i = 0; i < kEqCurvePoints; ++i) sameCurve = sameCurve && sameBits (kitCurve[i].db, snapshotCurve[i].db) && sameBits (kitCurve[i].hz, snapshotCurve[i].hz);
    ok (sameCurve, "the kit's curve with the bands is the snapshot's, bit for bit");
    KitEq bad = k; bad.bands.mud = 0.5;
    ok (Kit::eqCurve (bad, 48000, kitCurve).status == CodecStatus::Invalid, "a gain outside its domain is no preview");
    const auto travel = Kit::travel (text::Term::FieldBandsMud), body = Kit::travel (text::Term::FieldBandsAir);
    ok (travel.status == CodecStatus::Ok && same (travel.from, -3.0) && same (travel.to, 0.0) && same (travel.step, 0.1)
        && same (body.from, -3.0) && same (body.to, 3.0), "the knobs' travel: mud −3…0, the others ±3, by 0.1 dB");
    const auto typed = Kit::parse ("1,26", text::Lang::Ru, text::Term::FieldBandsBody, 0);
    ok (typed.status == CodecStatus::Ok && typed.refusal == KitRefusal::None && same (typed.value, 1.3), "a typed gain lands on the 0.1 dB grid");
    ok (! Kit::heat (text::Term::FieldBandsForward, 3.0).window, "no norm window this release: no heat");
    // THE NORM: [eq] curve.warnDb judges the shelves (tilt's and low's) only — the bands at their ends say nothing.
    Devices loud;
    for (std::size_t i = 0; i < 5; ++i) gainAt (loud.bands.hand, i) = i == 1 ? -6.0 : 6.0;
    const auto finding = detail::eqFinding (loud, r, 48000);
    ok (! finding.over && same (finding.db, 0.0), "the bands at ±6 dB move no beyond-the-norm finding");
}

void theProjectFile()
{
    felitronics::test::group ("the project file: [bands] with each gain a person set, back by an import; a file without them is all 0 dB");
    auto sp = placed(); auto& s = *sp;
    BandsFields<Touched> f; f.body = 1.5; f.air = -2.0;
    ok (s.apply (command::EditDevice { 3, f }).rejection == Rejection::None, "PRECONDITION: two bands set");
    const auto saved = s.exportProject();
    ok (saved.view().find ("[bands]\nbody.hand = 1.5\nair.hand = -2\n") != std::string_view::npos, "the file writes the two");
    auto copy = placed();
    ok (copy->apply (command::ImportProject { 4, saved.view() }).rejection == Rejection::None
        && sameBits (*copy->project().devices.bands.hand.body, 1.5) && sameBits (*copy->project().devices.bands.hand.air, -2.0)
        && ! copy->project().devices.bands.hand.mud, "an import keeps them exactly");
    BandsFields<Touched> off; off.on = false;
    ok (s.apply (command::EditDevice { 6, off }).rejection == Rejection::None, "PRECONDITION: the bands ticked off");
    const auto unticked = s.exportProject();
    ok (unticked.view().find ("[bands]\nbody.hand = 1.5\nair.hand = -2\non.hand = false\n") != std::string_view::npos,
        "the file writes the tick after the gains");
    auto again = placed();
    ok (again->apply (command::ImportProject { 7, unticked.view() }).rejection == Rejection::None
        && again->project().devices.bands.hand.on == std::optional<bool> (false) && sameBits (*again->project().devices.bands.hand.body, 1.5)
        && ! again->snapshot().view().plan.devices.bands.on, "an import keeps the tick off and the gains under it");
    ok (! copy->project().devices.bands.hand.on && copy->project().devices.bands.machine.on && copy->snapshot().view().plan.devices.bands.on,
        "a file with gains and no tick by hand imports with the machine's on: its gains sound");
}

// The snapshot's JSON after `edit` on a placed session.
std::string gainsOffSnapshot (Session& s, const BandsFields<Touched>& edit)
{
    ok (s.apply (command::EditDevice { 30, edit }).rejection == Rejection::None, "PRECONDITION: the edit lands");
    const auto v = s.snapshot();
    const auto need = Codec::encodedBytes (v.view());
    std::string out (std::size_t (need.bytes), '\0');
    ok (Codec::encode (v.view(), out) == CodecStatus::Ok, "PRECONDITION: the snapshot encodes");
    return out;
}


void theTick()
{
    felitronics::test::group ("the tick: off takes the whole device out with its gains kept, on again sounds them, a revert gives the machine's on");
    const auto r = detail::rules();
    // THE CURVE: the bands off with gains are the curve without them; on, the gains move it.
    Project plain;
    plain.devices.hpf.hand = { true, 30.0, 24 }; plain.devices.tilt.hand = { true, 1.0 };
    plain.devices.bands.machine.on = true;
    Project with = plain;
    for (std::size_t i = 0; i < 5; ++i) gainAt (with.devices.bands.hand, i) = i == 1 ? -1.5 : 2.5;
    with.devices.bands.hand.on = false;
    EqPoint a[kEqCurvePoints], b[kEqCurvePoints];
    detail::eqCurve (with, r, 48000, a); detail::eqCurve (plain, r, 48000, b);
    bool sameCurve = true, moved = false;
    for (std::size_t i = 0; i < kEqCurvePoints; ++i) sameCurve = sameCurve && sameBits (a[i].db, b[i].db) && sameBits (a[i].hz, b[i].hz);
    with.devices.bands.hand.on = true;
    detail::eqCurve (with, r, 48000, a);
    for (std::size_t i = 0; i < kEqCurvePoints; ++i) moved = moved || ! sameBits (a[i].db, b[i].db);
    ok (sameCurve && moved, "off with gains: the curve without the bands, bit for bit; on again: the gains move it");
    // THE KIT: off with gains draws what gains of 0 draw; a gain outside its domain is refused whatever the tick.
    KitEq off;
    off.hpf = detail::settingsOf (r, plain.devices.hpf); off.tilt = detail::settingsOf (r, plain.devices.tilt);
    off.bands = detail::settingsOf (r, with.devices.bands); off.bands.on = false;
    KitEq none = off;
    for (std::size_t i = 0; i < 5; ++i) gainAt (none.bands, i) = 0.0;
    EqPoint kitOff[kEqCurvePoints], kitNone[kEqCurvePoints];
    bool kitSame = Kit::eqCurve (off, 48000, kitOff).status == CodecStatus::Ok && Kit::eqCurve (none, 48000, kitNone).status == CodecStatus::Ok;
    for (std::size_t i = 0; i < kEqCurvePoints; ++i) kitSame = kitSame && sameBits (kitOff[i].db, kitNone[i].db) && sameBits (kitOff[i].db, b[i].db);
    KitEq bad = off; bad.bands.mud = 0.5;
    ok (kitSame && Kit::eqCurve (bad, 48000, kitOff).status == CodecStatus::Invalid,
        "the kit: off with gains draws no band, and a gain outside its domain is still no preview");
    // THE MASTER: off with gains is, sample for sample, the master without the bands; on again, the gains sound as set.
    const auto master = [] (Session& s, CommandId id)
    {
        ok (s.apply (command::Master { id }).rejection == Rejection::None, "PRECONDITION: a master is asked");
        for (unsigned i = 0; i < 4000000 && s.job(); ++i) (void) s.step (16);
        const auto pcm = s.viewMaster (s.pendingMaster());
        return std::vector<float> (pcm.begin(), pcm.end());
    };
    BandsFields<Touched> gains; gains.body = 3.0; gains.mud = -1.5; gains.air = 2.0;
    BandsFields<Touched> gainsOff = gains; gainsOff.on = false;
    BandsFields<Touched> on; on.on = true;
    auto bare = placed(), set = placed(), toggled = placed();
    const auto without = master (*bare, 20);
    ok (set->apply (command::EditDevice { 20, gains }).rejection == Rejection::None, "PRECONDITION: the gains set");
    const auto sounding = master (*set, 21);
    ok (toggled->apply (command::EditDevice { 20, gainsOff }).rejection == Rejection::None, "PRECONDITION: the gains set, the tick off");
    const auto bandsPlan = toggled->snapshot().view().plan.devices.bands;
    const auto silent = master (*toggled, 21);
    ok (! bandsPlan.on && bandsPlan.tick == TickFrom::Hand && ! without.empty() && silent.size() == without.size()
        && std::memcmp (silent.data(), without.data(), without.size() * sizeof (float)) == 0,
        "off with gains: the plan says it does not sound, and the master is the one without the bands, sample for sample");
    auto back = placed();
    ok (back->apply (command::EditDevice { 20, gainsOff }).rejection == Rejection::None && ! back->snapshot().view().plan.devices.bands.on
        && back->apply (command::EditDevice { 21, on }).rejection == Rejection::None && sameBits (*back->project().devices.bands.hand.body, 3.0)
        && back->snapshot().view().plan.devices.bands.on, "PRECONDITION: off, then on again, the gains kept");
    const auto again = master (*back, 22);
    ok (again.size() == sounding.size() && std::memcmp (again.data(), sounding.data(), sounding.size() * sizeof (float)) == 0
        && std::memcmp (again.data(), without.data(), without.size() * sizeof (float)) != 0,
        "on again: the master is the one the gains make without a tick ever written");
    // THE REVERT: the tick goes back to the machine's, which is on.
    BandsFields<Mark> tick; tick.on = true;
    BandsFields<Touched> justOff; justOff.on = false;
    auto reverted = placed();
    ok (reverted->apply (command::EditDevice { 20, justOff }).rejection == Rejection::None
        && reverted->snapshot().view().plan.devices.bands.tick == TickFrom::Hand, "PRECONDITION: the tick off by hand");
    ok (reverted->apply (command::RevertEdits { 21, tick }).rejection == Rejection::None && ! reverted->project().devices.bands.hand.on
        && reverted->snapshot().view().plan.devices.bands.tick == TickFrom::Machine
        && detail::settingsOf (r, reverted->project().devices.bands).on, "a revert of the tick gives the machine's: on");
    ok (toggled->apply (command::RevertEdits { 24, tick }).rejection == Rejection::None && ! toggled->project().devices.bands.hand.on
        && toggled->snapshot().view().plan.devices.bands.on, "...and with gains by hand, they sound");
    const auto view = gainsOffSnapshot (*placed(), gainsOff);
    Snapshot current;
    ok (Codec::decode (view, current) == CodecStatus::Ok && current.view().project.devices.bands.hand.on == std::optional<bool> (false),
        "the snapshot carries the tick: it decodes off");
}
} // namespace

int main()
{
    theConfig();
    theMachine();
    theCommands();
    theGeometry();
    theRenderedSound();
    aBandAtZeroIsNoBand();
    theKitAndTheNorm();
    theProjectFile();
    theTick();
    return felitronics::test::report();
}
