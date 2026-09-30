// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE PLAN OF THE DEVICES (src/Planner.h): every device planned, and planned as the previous path placed it; the one EQ
// stage its three devices share, drawn from the bands they write and measured through the engine that runs them; the
// plan's key and cache; what a master waits for with the panel open and hidden; the recipe captured when it is asked
// for; the machine layer an import keeps and adoptMachine replaces; a change of target; and no plan that waits for ever.

#include "DeclaredBudget.h"
#include "Advance.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Grid.h"
#include "Planner.h"
#include "PreviousPaths.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Config.h>
#include <felitronics/eq/EqEngine.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;

struct felitronics::session::detail::Inspector
{
    static std::uint64_t planRuns (const Session& s) { return s.planRuns_; }
    static bool jobWaiting (const Session& s) { return s.jobWaiting_; }
};

namespace
{
bool sameBits (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
bool same (double a, double b) { return detail::same (a, b); }
bool sameCeiling (const std::optional<double>& a, const std::optional<double>& b)
{
    return a.has_value() == b.has_value() && (! a || sameBits (*a, *b));
}

template <class F> bool sameFields (const detail::Rules& r, const F& a, const F& b)
{
    bool eq = true;
    detail::DeviceOf<F>::each (r, [&] (std::uint8_t, const detail::FieldRule&, const auto& x, const auto& y)
    {
        using T = std::remove_cvref_t<decltype (x)>;
        if constexpr (std::is_same_v<T, double>) eq = eq && sameBits (x, y);
        else if constexpr (std::is_same_v<T, std::optional<double>>) eq = eq && x.has_value() == y.has_value() && (! x || sameBits (*x, *y));
        else eq = eq && x == y;
    }, a, b);
    return eq;
}
bool sameDevices (const Devices& a, const Devices& b)
{
    const auto r = detail::rules();
    bool eq = true;
    detail::eachDevice (a, [&] (Device d, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        const auto& other = Of::layers (b);
        eq = eq && sameFields (r, layers.machine, other.machine) && sameFields (r, layers.hand, other.hand);
        (void) d;
    });
    return eq;
}
std::size_t fieldCount (Device d)
{
    std::size_t n = 0;
    Devices devices;
    detail::eachDevice (devices, [&] (Device device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        if (device == d) n = std::size (Of::fields);
    });
    return n;
}

// A synthetic mix a tempo can be measured on: a click every half second, twelve seconds long.
struct Clicks
{
    static constexpr unsigned rate = 48000, frames = rate * 12;
    std::vector<float> pcm = std::vector<float> (frames);
    const float* planes[2] { nullptr, nullptr };
    Clicks()
    {
        for (unsigned i = 0; i < frames; ++i) pcm[i] = i % 24000 < 300 ? .2f : 0.0f;
        planes[0] = planes[1] = pcm.data();
    }
    command::Load load (CommandId id = 1) const { return { id, { planes, 2, frames, rate }, { "clicks.wav", rate, true, 24 } }; }
};

template <class P> bool stepUntil (Session& s, P&& done, unsigned limit = 400000)
{
    for (unsigned i = 0; i < limit; ++i)
    {
        if (done()) return true;
        if (s.step (1).state == StepState::Done && ! done()) return false;
    }
    return done();
}

std::unique_ptr<Session> fresh()
{
    auto made = Session::create();
    ok (made.status == Status::Ok, "PRECONDITION: a session");
    return std::move (made.session);
}

// A short source whose measurements a fixture's seam ends (Advance.h): placed at once on allStreaming.
struct Short
{
    float samples[4] { 0, .25f, -.25f, 0 };
    const float* planes[2] { samples, samples };
    command::Load load() const { return { 1, { planes, 2, 4, 48000 }, { "short.wav", 48000, true, 24 } }; }
};
std::unique_ptr<Session> placedShort()
{
    Short audio;
    auto s = fresh();
    ok (s->apply (audio.load()).rejection == Rejection::None, "PRECONDITION: the short source loads");
    budget::measure (*s);
    ok (s->column() == Column::Measured2 && s->snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: placed, the plan ready");
    return s;
}
std::string version (Version v) { return std::to_string (v.major) + '.' + std::to_string (v.minor) + '.' + std::to_string (v.patch); }
std::string projectText (std::string_view core, std::string_view sections)
{
    return "defaults = \"" + std::string (*detail::rules().engine.find ("defaults").string()) + "\"\ncore = \"" + std::string (core)
         + "\"\nmanual = false\n\n[target]\nname = \"allStreaming\"\n" + std::string (sections);
}
bool factIn (const Session& s, text::FactId id, std::int64_t count)
{
    for (const auto& e : s.events())
        if (e.kind == EventKind::Fact && e.payload.fact.view().id == id && e.payload.fact.view().args[0].integer == count) return true;
    return false;
}

//==============================================================================

void everyDeviceIsPlannedAsBefore()
{
    felitronics::test::group ("every device planned: without a measurement, the previous path's layers — but for the decided high-pass and mono bass");
    const auto r = detail::rules();
    int cases = 0; bool asBefore = true, facts = true;
    for (std::uint16_t row = 0; row < r.rows; ++row)
        for (const std::uint32_t channels : { 1u, 2u })
            for (std::uint32_t offered = 0; offered <= 255u; ++offered)
            {
                // Hands in every device, so the shell's refusal is seen taking them away.
                Devices now, before;
                detail::eachDevice (now, [] (Device, auto& layers)
                {
                    if constexpr (requires { layers.hand.on; }) layers.hand.on = true;
                    else layers.hand.needles = Needles::Manual;
                });
                before = now;
                detail::PlanInputs in;
                in.rules = r; in.row = row; in.channels = channels; in.sampleRate = 48000; in.offered = offered;
                detail::placeMachine (in, now);
                previous::previousPlaceMachine (r, row, channels, before, offered);
                const auto target = r.row (row);
                // Owner decisions 3.3 and 3.5, without a low-end reading: the high-pass at the target's floor, mono bass
                // not placed until its loss is weighed. Every other field, and every person's layer, as before.
                Devices expected = before;
                expected.hpf.machine.fq = target.hpfFloor.toDouble();
                expected.monoBass.machine.on = false;
                asBefore = asBefore && sameDevices (now, expected);
                Devices proposed; DevicePlans plans; detail::PlanFindings found;
                detail::propose (in, proposed, plans, found);
                for (unsigned d = 0; d <= unsigned (Device::Low); ++d)
                {
                    const auto device = Device (d);
                    const auto& plan = detail::planOf (plans, device);
                    const auto mask = std::uint8_t ((1u << fieldCount (device)) - 1u);
                    const bool shell = (offered & (1u << d)) == 0;
                    const HeldBack want = shell ? HeldBack::Shell
                        : device == Device::MonoBass && channels == 1 ? HeldBack::Source
                        : device == Device::Hpf || device == Device::MonoBass ? HeldBack::Unmeasured
                        : device == Device::Limiter && target.noClipper ? HeldBack::Target
                        : device == Device::Dither && target.bitDepth > r.ditherUpToBits ? HeldBack::Target : HeldBack::None;
                    facts = facts && plan.heldBack == want && (plan.target & ~mask) == 0 && plan.measured == 0 && plan.needs == 0;
                }
                facts = facts && plans.hpf.target == 6u && (plans.monoBass.target & 2u) != 0
                     && found.hpf.cut == HpfCut::Unmeasured && same (found.hpf.cutoffHz, target.hpfFloor.toDouble())
                     && found.monoBass.verdict == (channels == 1 ? MonoBassVerdict::MonoSource : MonoBassVerdict::Unmeasured)
                     && ((plans.low.target != 0) == target.lowDb.has_value()) && ((plans.glue.target != 0) == target.glue.has_value());
                ++cases;
            }
    ok (asBefore, std::to_string (cases) + " placements equal the previous path's machine and person layers, bit for bit, "
              "the decided high-pass floor and unplaced mono bass apart");
    ok (facts, "each device's plan says what held it back (shell, source, target) and which fields its target decided");
}

void theEqStage()
{
    felitronics::test::group ("the one EQ stage: each device its own band; the curve drawn from the bands written is the previous curve and what the engine does");
    namespace eq = felitronics::eq;
    const auto r = detail::rules();
    // 1. The curve from the written bands is the previous path's curve, bit for bit.
    bool identical = true; int curves = 0;
    for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int slope = 6; slope <= 96; slope += 6)
            for (const double fq : { 10.0, 36.25, 50.0, 1000.0, 3990.0 })
                for (const double tilt : { -6.0, -1.25, 0.0, 3.0 })
                    for (const double low : { -6.0, 0.0, 0.5, 6.0 })
                    {
                        Project p;
                        detail::PlanInputs in; in.rules = r; in.row = r.defaultRow; in.channels = 2; in.sampleRate = std::uint32_t (rate);
                        detail::placeMachine (in, p.devices);
                        p.devices.hpf.hand.fq = fq; p.devices.hpf.hand.slope = slope;
                        p.devices.hpf.hand.on = (slope / 6) % 5 != 0;
                        p.devices.tilt.hand.db = tilt; p.devices.tilt.hand.on = (slope / 6) % 3 != 0;
                        p.devices.low.machine.db = low; p.devices.low.machine.on = slope % 12 == 0;
                        EqPoint now[kEqCurvePoints], before[kEqCurvePoints];
                        detail::eqCurve (p, r, rate, now);
                        previous::previousEqCurve (p, r, rate, before);
                        for (std::size_t i = 0; i < kEqCurvePoints; ++i)
                            identical = identical && sameBits (now[i].hz, before[i].hz) && sameBits (now[i].db, before[i].db);
                        ++curves;
                    }
    ok (identical, std::to_string (curves) + " curves from the written bands equal the previous path's, bit for bit");

    // 2. Each device writes its own band and no other.
    Project p;
    detail::PlanInputs in; in.rules = r; in.row = r.defaultRow; in.channels = 2; in.sampleRate = 48000;
    detail::placeMachine (in, p.devices);
    p.devices.tilt.hand = { true, 1.25 }; p.devices.low.hand = { true, -2.5 };
    detail::EqStage base; detail::writeEq (p.devices, r, base);
    const auto changedBands = [&] (const detail::EqStage& other)
    {
        std::uint32_t mask = 0;
        for (int i = 0; i < eq::EqEngine::kMaxBands; ++i) if (! (other.bands[i] == base.bands[i])) mask |= 1u << i;
        return mask;
    };
    bool owned = true;
    for (const Device device : { Device::Hpf, Device::Tilt, Device::Low })
    {
        Project q = p;
        if (device == Device::Hpf) q.devices.hpf.hand.fq = 41.0;
        if (device == Device::Tilt) q.devices.tilt.hand.db = -3.0;
        if (device == Device::Low) q.devices.low.hand.db = 4.0;
        detail::EqStage stage; detail::writeEq (q.devices, r, stage);
        owned = owned && changedBands (stage) == (1u << detail::eqBand (device));
    }
    for (const Device device : { Device::MonoBass, Device::Glue, Device::Saturation, Device::Limiter, Device::Dither })
        owned = owned && detail::eqBand (device) == -1;
    bool untouched = true;
    for (int i = 3; i < eq::EqEngine::kMaxBands; ++i) untouched = untouched && base.bands[i] == eq::BandParams {};
    ok (owned && untouched, "a device's change moves its own band alone; the bands of no EQ device stay as the engine's defaults");
    // Each band is exactly its own device's: the type, the frequency, the gain or slope its settings give, and nothing
    // another device wrote.
    const auto cfg = config::Config::load();
    eq::BandParams hpfBand, tiltBand, lowBand;
    hpfBand.on = p.devices.hpf.machine.on; hpfBand.type = eq::FilterType::HighPass;
    hpfBand.lanes[0].freq = p.devices.hpf.machine.fq; hpfBand.lanes[0].slope = p.devices.hpf.machine.slope;
    tiltBand.on = true; tiltBand.type = eq::FilterType::Tilt; tiltBand.lanes[0].freq = cfg.config.engine.tilt.freqHz; tiltBand.lanes[0].gainDb = 1.25;
    lowBand.on = true; lowBand.type = eq::FilterType::LowShelf; lowBand.lanes[0].freq = cfg.config.engine.low.freqHz;
    lowBand.lanes[0].Q = cfg.config.engine.low.q; lowBand.lanes[0].gainDb = -2.5;
    ok (base.bands[detail::eqBand (Device::Hpf)] == hpfBand && base.bands[detail::eqBand (Device::Tilt)] == tiltBand
        && base.bands[detail::eqBand (Device::Low)] == lowBand, "each band holds exactly its own device's filter");
    Project off = p; off.devices.tilt.hand.on = false;
    detail::EqStage bypassed; detail::writeEq (off.devices, r, bypassed);
    ok (! bypassed.bands[detail::eqBand (Device::Tilt)].on && changedBands (bypassed) == (1u << detail::eqBand (Device::Tilt)),
        "a tick off bypasses its own band and nothing else");

    // 3. The curve is what sounds: core's response of the written bands, and the engine's own output on sines.
    bool response = true, sound = true;
    double worst = 0;
    for (const double rate : { 44100.0, 96000.0 })
    {
        Project q = p;
        q.devices.hpf.hand = { true, 38.5, 36 };
        q.devices.low.hand = { true, 3.75 };
        detail::EqStage stage; detail::writeEq (q.devices, r, stage);
        EqPoint curve[kEqCurvePoints];
        detail::eqCurve (stage.bands, rate, curve);
        for (const auto& point : curve)
        {
            const double w = 2 * eq::kPi * point.hz / rate;
            double expected = 0;
            for (const auto& band : stage.bands) expected += 20 * std::log10 (std::abs (eq::bandResponse (band, rate, w)));
            response = response && std::abs (expected - point.db) < 1e-6;
        }
        auto engine = std::make_unique<eq::EqEngine>();
        constexpr int block = 512;
        ok (engine->prepare (rate, block, 1), "PRECONDITION: the engine prepares");
        for (int i = 0; i < eq::EqEngine::kMaxBands; ++i) engine->setBand (i, stage.bands[i]);
        for (std::size_t k = 8; k < kEqCurvePoints; k += 12)
        {
            engine->reset();
            const double hz = curve[k].hz;
            const int settle = int (rate), measure = int (rate / 2);
            std::vector<float> x (std::size_t (settle + measure));
            for (std::size_t i = 0; i < x.size(); ++i) x[i] = float (0.25 * std::sin (2 * eq::kPi * hz * double (i) / rate));
            std::vector<float> y = x;
            for (std::size_t at = 0; at < y.size(); at += block)
            {
                float* channel[] { y.data() + at };
                ok (engine->process (channel, 1, int (std::min<std::size_t> (block, y.size() - at))), "PRECONDITION: the engine processes");
            }
            // The output's amplitude at the input's frequency, over whole cycles after the filters have settled.
            double c = 0, sn = 0, cx = 0, sx = 0;
            const int cycles = int (std::floor (double (measure) * hz / rate));
            const int n = cycles > 0 ? int (std::floor (double (cycles) * rate / hz)) : measure;
            for (int i = 0; i < n; ++i)
            {
                const double phase = 2 * eq::kPi * hz * double (settle + i) / rate;
                c += y[std::size_t (settle + i)] * std::cos (phase); sn += y[std::size_t (settle + i)] * std::sin (phase);
                cx += x[std::size_t (settle + i)] * std::cos (phase); sx += x[std::size_t (settle + i)] * std::sin (phase);
            }
            const double gain = 10 * std::log10 ((c * c + sn * sn) / (cx * cx + sx * sx));
            worst = std::max (worst, std::abs (gain - curve[k].db));
            sound = sound && std::abs (gain - curve[k].db) < 0.05;
        }
    }
    ok (response, "the curve is core's response of the bands written, point by point");
    ok (sound, "and the engine running those bands makes it: sines through the engine land on the curve (worst "
               + std::to_string (worst) + " dB)");
}

void thePlansKey()
{
    felitronics::test::group ("the plan's key: equal inputs, one plan; one input changed, the planner runs again");
    Clicks audio;
    auto sp = fresh(); auto& s = *sp;
    (void) s.apply (audio.load (1));
    ok (stepUntil (s, [&] { return s.state() == State::Measured2 && s.needlesJob() == 0; }), "PRECONDITION: measured");
    const auto runs = [&] { return detail::Inspector::planRuns (s); };
    const auto key = [&] { return s.snapshot().view().plan.key; };
    const auto r0 = runs(); const auto k0 = key();
    ok (s.apply (command::SetManual { 2, true }).rejection == Rejection::None && runs() == r0 && key() == k0,
        "showing the panel changes no input: the cached plan answers");
    ok (s.apply (command::SetManual { 3, false }).rejection == Rejection::None && runs() == r0 && key() == k0,
        "hiding it neither");
    ok (s.apply (command::SetTarget { 4, "allStreaming" }).rejection == Rejection::None && runs() == r0 && key() == k0,
        "the same target placed again is the same plan");
    TiltFields<Touched> tilt; tilt.db = 1.0;
    ok (s.apply (command::EditDevice { 5, tilt }).rejection == Rejection::None && runs() == r0 + 1 && key() != k0,
        "a person's edit is an input");
    TiltFields<Mark> back; back.db = true;
    ok (s.apply (command::RevertEdits { 6, back }).rejection == Rejection::None && runs() == r0 + 2 && key() == k0,
        "taken back, the inputs and so the key are the first plan's");
    const auto needles = s.needlesJob();
    command::EditTarget lufs { 7, {} }; lufs.fields.lufs = -13.0;
    ok (s.apply (lufs).rejection == Rejection::None && runs() == r0 + 3 && key() != k0 && s.needlesJob() != needles,
        "the target's numbers are inputs, and so is the ceiling's needles job they start");
    ok (s.apply (command::SetTarget { 8, "lp" }).rejection == Rejection::None && runs() == r0 + 4, "so is the target");
}

void theOpenPanelWaits()
{
    felitronics::test::group ("with the panel open a master waits for what its devices read, and says what and how far");
    Clicks audio;
    auto sp = fresh(); auto& s = *sp;
    (void) s.apply (command::SetTarget { 1, "cd" });
    (void) s.apply (audio.load (2));
    (void) s.apply (command::SetManual { 3, true });
    ok (stepUntil (s, [&] { return s.state() != State::Loaded; }) && s.state() == State::Measured1, "PRECONDITION: the first measurement ends");
    auto v = s.snapshot();
    ok (v.view().plan.status == PlanStatus::Pending && v.view().plan.awaited == Analyzer::Tempo && v.view().plan.awaitedBy == Device::Glue
        && v.view().plan.readOnly && ! v.view().canMaster && v.view().devicesPlaced == false
        && same (v.view().project.devices.glue.machine.upToDb, 2.6),
        "cd's glue reads the tempo: the machine layer is shown read-only, and the master button waits for the tempo the glue reads");
    const auto revision = s.revision();
    ok (s.check (command::Master { 4 }).rejection == Rejection::PlanPending && s.apply (command::Master { 4 }).rejection == Rejection::PlanPending
        && s.revision() == revision && s.job() == 0, "a master asked for anyway is refused whole: PlanPending");
    bool rejectedEvent = false;
    for (const auto& e : s.events()) rejectedEvent = rejectedEvent || (e.kind == EventKind::Rejected && e.payload.rejected.code == Rejection::PlanPending);
    ok (rejectedEvent, "and the refusal is published");
    Answer refused; refused.rejection = Rejection::PlanPending;
    const auto fact = text::Text::rejected (refused, command::Master { 4 });
    const auto shown = fact ? text::Text::text (*fact, text::Lang::Ru) : std::string {};
    ok (shown.find ("Панель") != std::string::npos, "the refusal speaks: " + shown);
    double furthest = 0; bool hum = false;
    ok (stepUntil (s, [&]
    {
        const auto view = s.snapshot();
        furthest = std::max (furthest, view.view().plan.awaitedFraction);
        if (view.view().plan.waiting == 0)
        {
            hum = view.view().measurements[std::size_t (Analyzer::Hum)].status == MeasurementStatus::Pending;
            return true;
        }
        return false;
    }), "the tempo ends");
    ok (furthest > 0 && furthest <= 1, "its progress was published while it ran (" + std::to_string (furthest) + ")");
    ok (hum, "the tempo the plan reads went ahead of the optional findings: hum is still to come");
    v = s.snapshot();
    ok (v.view().plan.status == PlanStatus::Ready && ! v.view().plan.readOnly && v.view().devicesPlaced && v.view().canMaster
        && ! v.view().plan.awaited, "then the devices are placed and the master is available with the panel open");
    ok (s.apply (command::Master { 5 }).rejection == Rejection::None, "and a master is taken");
    (void) s.step (1);
    ok (s.snapshot().view().masterProgress.name == PhaseName::Pass, "without waiting: it has what it reads");
    const auto text = text::Text::text (text::Fact::of (text::FactId::PlanWaiting, text::Arg::term (text::Term::DeviceGlue),
        text::Arg::term (text::Term::AnalyzerTempo), text::Arg::value (40, text::Unit::Percent, 0)), text::Lang::Ru);
    ok (text.find ("Темп") != std::string::npos && text.find ("Клей") != std::string::npos && text.find ('{') == std::string::npos,
        "the waiting button's fact renders: " + text);
}

void aTargetWithoutGlueDoesNotWaitForTempo()
{
    felitronics::test::group ("the tempo is waited for only where a glue compresses — cdDynamic's is off");
    Clicks audio;
    auto sp = fresh(); auto& s = *sp;
    (void) s.apply (command::SetTarget { 1, "cdDynamic" });
    (void) s.apply (audio.load (2));
    ok (stepUntil (s, [&] { return s.state() != State::Loaded; }) && s.state() == State::Measured1, "PRECONDITION: the first measurement ends");
    const auto v = s.snapshot();
    ok (! v.view().tempoChoice.ready && v.view().plan.status == PlanStatus::Ready && v.view().plan.waiting == 0,
        "with the tempo still to measure, cdDynamic's plan is ready: its glue does not compress");
    (void) s.apply (command::SetManual { 2, true });
    ok (s.apply (command::Master { 3 }).rejection == Rejection::None, "a master is taken with the panel open");
    (void) s.step (1);
    ok (s.snapshot().view().masterProgress.name == PhaseName::Pass && ! detail::Inspector::jobWaiting (s),
        "and renders without waiting for the tempo");
}

void theNeedlesAreWaitedForAtTheirCeiling()
{
    felitronics::test::group ("a new ceiling measures the needles again; with the panel open the master waits for them");
    Clicks audio;
    auto sp = fresh(); auto& s = *sp;
    (void) s.apply (audio.load (1));
    ok (stepUntil (s, [&] { return s.state() == State::Measured2 && s.needlesJob() == 0; }), "PRECONDITION: measured");
    const auto before = s.snapshot();
    ok (before.view().plan.status == PlanStatus::Ready && before.view().measurements[std::size_t (Analyzer::Excursions)].status == MeasurementStatus::Ready,
        "PRECONDITION: the needles measured at allStreaming's ceiling");
    (void) s.apply (command::SetManual { 2, true });
    command::EditTarget tp { 3, {} }; tp.fields.tp = -3.0;
    ok (s.apply (tp).rejection == Rejection::None && s.needlesJob() != 0, "a person's ceiling measures the needles again");
    auto v = s.snapshot();
    ok (v.view().plan.status == PlanStatus::Pending && v.view().plan.awaited == Analyzer::Excursions && v.view().plan.awaitedBy == Device::Limiter,
        "the plan waits for the needles the limiter's peak clipper reads");
    ok (s.apply (command::Master { 4 }).rejection == Rejection::PlanPending, "with the panel open the master waits for them");
    double furthest = 0;
    ok (stepUntil (s, [&] { furthest = std::max (furthest, s.snapshot().view().plan.awaitedFraction); return s.snapshot().view().plan.waiting == 0; }),
        "the needles end");
    ok (furthest > 0 && s.apply (command::Master { 5 }).rejection == Rejection::None, "with their progress shown, and then the master is taken");
}

void aHiddenMasterKeepsWhatWasAsked()
{
    felitronics::test::group ("with the panel hidden a master is taken at once; its recipe is the project when it was asked for");
    Clicks audio;
    auto sp = fresh(); auto& s = *sp;
    (void) s.apply (audio.load (1));
    ok (stepUntil (s, [&] { return s.state() == State::Measured1 && s.needlesJob() == 0; }), "PRECONDITION: the first measurement ends");
    ok (s.snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: placed on allStreaming");
    GlueFields<Touched> glue; glue.on = true; glue.upToDb = 1.0;
    ok (s.apply (command::EditDevice { 2, glue }).rejection == Rejection::None, "a person's glue");
    auto v = s.snapshot();
    ok (v.view().plan.status == PlanStatus::Ready && ! v.view().plan.readOnly && v.view().plan.waiting == detail::bitOf (Analyzer::Tempo),
        "the panel stays open to edits; what the project reads now waits for the tempo");
    const auto master = s.apply (command::Master { 3 });
    ok (master.rejection == Rejection::None && detail::Inspector::jobWaiting (s), "the hidden panel's master is taken and waits");
    const auto asked = s.project().target;
    TiltFields<Touched> tilt; tilt.on = true; tilt.db = 2.0;
    ok (s.apply (command::EditDevice { 4, tilt }).rejection == Rejection::None
        && s.apply (command::SetTarget { 5, "lp" }).rejection == Rejection::None, "a person edits and changes the target meanwhile");
    ok (s.jobRecipe().project.target == asked && s.jobRecipe().project.devices.glue.hand.upToDb == 1.0
        && ! s.jobRecipe().project.devices.tilt.hand.db, "the waiting master's recipe is the project it was asked for");
    ok (stepUntil (s, [&] { return s.job() == 0; }) && s.masters().size() == 1, "the master ends after the tempo");
    const auto& recipe = s.masters()[0].recipe;
    ok (recipe.project.target == asked && recipe.project.devices.glue.hand.upToDb == 1.0 && ! recipe.project.devices.tilt.hand.db
        && s.masters()[0].id == master.job, "and the master kept is made from it — not from the target and edits that came after");
}

void noPlanWaitsForEver()
{
    felitronics::test::group ("no plan waits for ever: a stopped needles job ends; a waiting master keeps its own ceiling");
    Clicks audio;
    {
        auto sp = fresh(); auto& s = *sp;
        (void) s.apply (audio.load (1));
        ok (stepUntil (s, [&] { return s.state() == State::Measured2 && s.needlesJob() == 0; }), "PRECONDITION: measured");
        command::EditTarget tp { 2, {} }; tp.fields.tp = -3.0;
        (void) s.apply (tp);
        const auto master = s.apply (command::Master { 3 });
        ok (master.rejection == Rejection::None && detail::Inspector::jobWaiting (s), "a hidden master waits for the new needles");
        ok (s.apply (command::Cancel { 4, s.needlesJob() }).rejection == Rejection::None, "the needles job is stopped");
        ok (stepUntil (s, [&] { return s.job() == 0; }) && s.masters().size() == 1,
            "the master goes on without them: a stopped needles job has ended, with its reason");
        ok (s.snapshot().view().plan.status == PlanStatus::Ready
            && s.snapshot().view().measurements[std::size_t (Analyzer::Excursions)].status == MeasurementStatus::Cancelled,
            "and the plan is ready, the needles' reason kept");
    }
    {
        auto sp = fresh(); auto& s = *sp;
        (void) s.apply (audio.load (1));
        ok (stepUntil (s, [&] { return s.state() == State::Measured2 && s.needlesJob() == 0; }), "PRECONDITION: measured");
        command::EditTarget tp { 2, {} }; tp.fields.tp = -3.0;
        (void) s.apply (tp);
        const auto asked = s.snapshot().view().needlesCeilingDb;
        ok (s.apply (command::Master { 3 }).rejection == Rejection::None, "a hidden master waits for the needles at its ceiling");
        ok (s.apply (command::SetTarget { 4, "club" }).rejection == Rejection::None && sameCeiling (s.snapshot().view().needlesCeilingDb, asked)
            && s.needlesJob() != 0, "a new target leaves the master's needles running at the ceiling it asked for");
        ok (stepUntil (s, [&] { return s.job() == 0; }) && s.masters().size() == 1, "the master ends");
        ok (stepUntil (s, [&] { return s.snapshot().view().plan.status == PlanStatus::Ready; })
            && ! sameCeiling (s.snapshot().view().needlesCeilingDb, asked), "then the needles are measured at the new target's ceiling");
    }
}

void oneNeedOneMeasurement()
{
    felitronics::test::group ("one need, one measurement: a target with the same ceiling measures nothing; the source is measured once");
    Clicks audio;
    auto sp = fresh(); auto& s = *sp;
    (void) s.apply (audio.load (1));
    ok (stepUntil (s, [&] { return s.state() == State::Measured2 && s.needlesJob() == 0; }), "PRECONDITION: measured");
    const auto before = s.snapshot();
    ok (s.apply (command::SetTarget { 2, "spotify" }).rejection == Rejection::None && s.needlesJob() == 0 && s.measurementJob() == 0
        && s.snapshot().view().plan.status == PlanStatus::Ready,
        "spotify's numbers are allStreaming's: the needles already measured serve it, nothing runs");
    ok (s.apply (command::SetTarget { 3, "club" }).rejection == Rejection::None && s.needlesJob() != 0 && s.measurementJob() == 0,
        "club's ceiling is another: its needles run, and the source is not measured again");
    ok (stepUntil (s, [&] { return s.needlesJob() == 0; }), "they end");
    const auto after = s.snapshot();
    bool once = true;
    for (unsigned i = 0; i < kAnalyzers; ++i)
        if (Analyzer (i) != Analyzer::Excursions)
            once = once && after.view().measurements[i].status == before.view().measurements[i].status
                && after.view().measurements[i].framesRead == before.view().measurements[i].framesRead
                && after.view().measurements[i].key == before.view().measurements[i].key;
    ok (once, "every other measurement is the one made once, for every target");
}

void anImportKeepsTheFilesMachine()
{
    felitronics::test::group ("an import keeps the file's machine layer, same core or another; adoptMachine takes the planner's; a person's layer stays");
    for (const bool foreign : { false, true })
    {
        auto sp = placedShort(); auto& s = *sp;
        const auto core = foreign ? std::string ("0.0.1") : version (Session::version());
        const auto file = projectText (core, "\n[hpf]\nfq.machine = 36\n\n[tilt]\ndb.hand = 1.25\n");
        const auto revision = s.revision();
        ok (s.apply (command::ImportProject { 2, file }).rejection == Rejection::None && s.revision() == revision + 1, "the file imports");
        const auto v = s.snapshot();
        ok (same (s.project().devices.hpf.machine.fq, 36.0) && v.view().plan.fromFile, "its machine layer is kept as the file wrote it");
        ok (v.view().machineDifferences.size() == 1 && v.view().machineDifferences[0].device == Device::Hpf
            && v.view().machineDifferences[0].field == 1 && same (v.view().machineDifferences[0].fileValue, 36.0)
            && same (v.view().machineDifferences[0].coreValue, 32.0), "the planner's decision beside it: one difference, the high-pass at its 32 Hz floor");
        ok (factIn (s, foreign ? text::FactId::MachineDifferences : text::FactId::SameCoreMachineDifferences, 1),
            foreign ? "another core: the difference is announced" : "the same core: announced as a hand-edited file");
        Answer adopt;
        const auto spent = budget::spend ([&] { adopt = s.apply (command::AdoptMachine { 3 }); });
        ok (adopt.rejection == Rejection::None && s.revision() == revision + 2, "adoptMachine is taken");
        ok (spent.requests == 0, "and asks the heap for nothing");
        const auto adopted = s.snapshot();
        ok (same (s.project().devices.hpf.machine.fq, 32.0) && adopted.view().machineDifferences.empty() && ! adopted.view().plan.fromFile
            && s.project().devices.tilt.hand.db && same (*s.project().devices.tilt.hand.db, 1.25),
            "the planner's decision taken, the person's 1.25 dB kept");
        ok (s.apply (command::AdoptMachine { 4 }).rejection == Rejection::None && s.revision() == revision + 2,
            "with nothing left to take it is accepted without a revision");
    }
}

void aChangeOfTargetResetsEdits()
{
    felitronics::test::group ("a change of target: the count a warning shows, the edits kept until it is sent, reset when it is");
    auto sp = placedShort(); auto& s = *sp;
    TiltFields<Touched> tilt; tilt.db = 1.25;
    LowFields<Touched> low; low.db = 0.75;
    HpfFields<Touched> hpf; hpf.fq = 36.0;
    (void) s.apply (command::EditDevice { 2, tilt }); (void) s.apply (command::EditDevice { 3, low }); (void) s.apply (command::EditDevice { 4, hpf });
    ok (s.snapshot().view().handFieldCount == 3, "three edits: the warning's count");
    ok (s.apply (command::SetTarget { 5, "nowhere" }).rejection == Rejection::UnknownTarget && s.snapshot().view().handFieldCount == 3,
        "a change that does not happen keeps every edit");
    const auto saved = s.exportProject();
    Short audio;
    auto copy = fresh(); (void) copy->apply (audio.load()); budget::measure (*copy);
    ok (copy->apply (command::ImportProject { 6, saved.view() }).rejection == Rejection::None
        && copy->project().devices.tilt.hand.db && sameBits (*copy->project().devices.tilt.hand.db, 1.25)
        && copy->project().devices.low.hand.db && sameBits (*copy->project().devices.low.hand.db, 0.75),
        "the project saved before the change keeps 1.25 and 0.75 dB exactly");
    TiltFields<Mark> back; back.db = true;
    ok (s.apply (command::RevertEdits { 7, back }).rejection == Rejection::None && ! s.project().devices.tilt.hand.db
        && s.project().devices.low.hand.db && s.project().devices.hpf.hand.fq, "a revert takes back the one edit it names");
    ok (s.apply (command::SetTarget { 8, "lp" }).rejection == Rejection::None && s.snapshot().view().handFieldCount == 0
        && same (s.project().devices.low.machine.db, 0.5), "the change sent: every device edit reset, lp's own low shelf placed");
}
// THE OWNER'S RULE: a person's edit always sounds. A device's tick is the person's when they set one; otherwise on when
// any of its fields carries their value; otherwise the machine's.
void aTouchedDeviceSounds()
{
    felitronics::test::group ("a person's edit always sounds: a touched knob ticks its device; their own un-tick wins; untouched, the machine's tick");
    const auto r = detail::rules();
    // Every device with a tick and a knob, walked by the one list: machine off, one knob touched.
    bool touched = true, unticked = true, machine = true;
    Devices devices;
    detail::eachDevice (devices, [&] (Device, auto& layers)
    {
        using Fields = std::remove_cvref_t<decltype (layers.machine)>;
        if constexpr (requires { layers.machine.on; })
        {
            for (const bool machineOn : { false, true })
            {
                layers.machine.on = machineOn; layers.hand = {};
                machine = machine && detail::settingsOf (r, layers).on == machineOn && detail::tickFrom (r, layers) == TickFrom::Machine;
                std::size_t fields = 0;
                detail::DeviceOf<Fields>::each (r, [&] (std::uint8_t, const detail::FieldRule&, const auto&) { ++fields; }, layers.machine);
                for (std::size_t touch = 1; touch < fields; ++touch)
                {
                    layers.hand = {};
                    detail::DeviceOf<Fields>::each (r, [&] (std::uint8_t i, const detail::FieldRule&, auto& hand, const auto& value)
                    { if (i == touch) hand = value; }, layers.hand, layers.machine);
                    touched = touched && detail::settingsOf (r, layers).on && detail::tickFrom (r, layers) == TickFrom::Touched
                           && detail::settingsOf (r, layers, false).on == machineOn;
                    layers.hand.on = false;
                    unticked = unticked && ! detail::settingsOf (r, layers).on && detail::tickFrom (r, layers) == TickFrom::Hand;
                    layers.hand.on = true;
                    unticked = unticked && detail::settingsOf (r, layers).on && detail::tickFrom (r, layers) == TickFrom::Hand;
                }
            }
        }
    });
    ok (touched, "every device: any knob a person touched ticks it on, whatever the machine's tick (equal to the machine's value too)");
    ok (unticked, "a person's own tick — off or on — wins over their knobs");
    ok (machine, "with nothing touched the tick is the machine's");

    // THE PLAN'S EXAMPLE: `[tilt] db.hand = 3`, no tick written, sounds.
    auto sp = placedShort(); auto& s = *sp;
    const auto flat = s.snapshot();
    const auto example = projectText (version (Session::version()), "\n[tilt]\ndb.hand = 3\n");
    ok (s.apply (command::ImportProject { 2, example }).rejection == Rejection::None && ! s.project().devices.tilt.machine.on
        && ! s.project().devices.tilt.hand.on, "PRECONDITION: the project carries the knob alone; the machine's tilt is off");
    const auto v = s.snapshot();
    const auto top = [] (const Snapshot& snap) { return snap.view().eqCurve[kEqCurvePoints - 1].db; };
    ok (std::abs (top (v) - top (flat) - 3.0) < 0.2, "it sounds: the curve's top is up by the 3 dB");
    ok (v.view().plan.devices.tilt.on && v.view().plan.devices.tilt.tick == TickFrom::Touched,
        "and the snapshot says the tick is on, and that a touched knob made it so");
    TiltFields<Touched> off; off.on = false;
    ok (s.apply (command::EditDevice { 3, off }).rejection == Rejection::None, "the person un-ticks it");
    const auto silent = s.snapshot();
    ok (std::abs (top (silent) - top (flat)) < 1e-9 && ! silent.view().plan.devices.tilt.on && silent.view().plan.devices.tilt.tick == TickFrom::Hand
        && same (*s.project().devices.tilt.hand.db, 3.0), "silent, the 3 dB kept on the knob: the person's own tick");
    TiltFields<Mark> tick; tick.on = true;
    ok (s.apply (command::RevertEdits { 4, tick }).rejection == Rejection::None && s.snapshot().view().plan.devices.tilt.on,
        "the tick reverted, the knob sounds again");
    // A person's glue knob alone makes the glue compress — and so reads the tempo, like a ticked one.
    GlueFields<Touched> glue; glue.upToDb = 1.0;
    ok (s.apply (command::EditDevice { 5, glue }).rejection == Rejection::None
        && (s.snapshot().view().plan.needs & detail::bitOf (Analyzer::Tempo)) != 0 && s.snapshot().view().plan.devices.glue.tick == TickFrom::Touched,
        "a touched glue knob compresses, and the plan reads the tempo for it");
    const auto all = s.snapshot();
    ok (all.view().plan.devices.limiter.on && all.view().plan.devices.limiter.tick == TickFrom::Machine
        && all.view().plan.devices.hpf.on && all.view().plan.devices.hpf.tick == TickFrom::Machine
        && ! all.view().plan.devices.saturation.on, "untouched devices show the machine's tick; the limiter is always on");
    ok (s.apply (command::SetTarget { 6, "lp" }).rejection == Rejection::None && ! s.snapshot().view().plan.devices.tilt.on
        && s.snapshot().view().plan.devices.tilt.tick == TickFrom::Machine && s.snapshot().view().plan.devices.low.on,
        "a change of target resets the edits and the ticks they gave; vinyl's low is the machine's");
}
} // namespace

int main()
{
    aTouchedDeviceSounds();
    everyDeviceIsPlannedAsBefore();
    theEqStage();
    thePlansKey();
    theOpenPanelWaits();
    aTargetWithoutGlueDoesNotWaitForTempo();
    theNeedlesAreWaitedForAtTheirCeiling();
    aHiddenMasterKeepsWhatWasAsked();
    noPlanWaitsForEver();
    oneNeedOneMeasurement();
    anImportKeepsTheFilesMachine();
    aChangeOfTargetResetsEdits();
    return felitronics::test::report();
}
