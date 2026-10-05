// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE WHOLE PLAN SOUNDING (owner decisions 3.6, 3.7, 3.12, 3.13; technical decision 3О11). The limiter with its needles,
// the dither and the chain a master the session decides is rendered with:
//   * the needles' classes on every boundary of the owner's table, a need of 3 dB, needles that were not measured apart
//     from needles that are not there, a clipped source from ten confirmed clips a minute, and a person's manual
//     threshold sounding over every refusal of the machine's, with the machine's reason beside it;
//   * the dither by the delivery's format — 16 bits only, a person's "off" allowed, a tick above 16 bits kept without
//     effect — its noise a stated version (weighted TPDF, the fixed seed, blanked after 4096 zero samples);
//   * vinyl: the machine never above the medium's ceiling and never cutting needles, a person's hand obeyed and warned,
//     the report's "ready for cutting", what the file shows and what it cannot, the constant note about the top;
//   * a quiet input: strictly under −55 LUFS the machine sets the gain and the ceiling, the format's dither and the
//     high-pass at its floor, and nothing else;
//   * THE RENDER: a master the session decides is, sample for sample, the master the PREVIOUS path makes — a version-1
//     master whose ready chain this suite composes itself from the rules as the owner states them — on every target kind
//     and under a person's edits; its chain follows the devices as they sound (the tick rule), never [stages]; a master
//     that waited for its measurements is the master asked for after them; and its memory is declared before the work,
//     on the waiting path too.

#include "../../../tests/DeclaredBudget.h"
#include "Chain.h"
#include "Devices.h"
#include "Driver.h"
#include "Dynamics.h"
#include "EqCurve.h"
#include "Grid.h"
#include "Limiter.h"
#include "MasterJob.h"
#include "Observations.h"
#include "Planner.h"
#include "embedded/no-vinyl.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Text.h>
#include <felitronics/session/Wire.h>
#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace declared = felitronics::declared;
namespace mastering = felitronics::mastering;

// The needles' identity as the session keeps it: the ceiling they were measured at, set by hand to a value no pump
// produces, to hold the two rules that read it — requestNeedles and planInputs — to one answer.
struct felitronics::session::detail::Inspector
{
    static void measuredAt (Session& s, double ceiling)
    {
        s.needlesCeilingDb_ = ceiling;
        s.needlesKey_ ^= 1u;   // a result of another ceiling: another key
    }
    static void loudness (Session& s, std::span<const MeasurementValue> numbers)
    {
        auto& r = s.measurementResults_[std::size_t (Analyzer::Loudness)];
        r.numbers = numbers;
    }
    static void deliveryFixture (Session& s, const MasterPlan& plan)
    {
        s.masterJob_.reset();
        s.startMaster (plan);
    }
    static void request (Session& s) { s.requestNeedles(); s.replan(); }
    static bool current (const Session& s) { return s.planInputs (s.project_).needlesCurrent; }
    static std::optional<double> ceiling (const Session& s) { return s.needlesCeilingDb_; }
};

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return detail::same (a, b); }
bool near (double a, double b, double by) { return std::fabs (a - b) <= by; }
bool sameBits (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
std::string ru (const text::Fact& f) { return text::Text::text (f, text::Lang::Ru); }
std::string en (const text::Fact& f) { return text::Text::text (f, text::Lang::En); }
bool whole (const text::Fact& f)
{
    const auto a = ru (f), b = en (f);
    return a.size() > 10 && b.size() > 10 && a.find ('{') == std::string::npos && b.find ('{') == std::string::npos
        && a != std::string (text::Text::key (f.id)) && b != std::string (text::Text::key (f.id));
}

//==============================================================================
// THE PLANNER'S INPUTS ON FAKED READINGS: the loudness and the true peak, the needles' result and the clip detector's.

enum class Needle { Ready, Pending, Cancelled, Unavailable, Stale };
struct Faked
{
    MeasurementValue loudness[2], needles[3], clipping[1];
    MeasurementResult results[kAnalyzers] {};
    detail::PlanInputs in;
    Faked (std::string_view target, double lufs, double peak)
    {
        loudness[0] = { "integratedLufs", lufs, MeasurementReason::None, 0 };
        loudness[1] = { "truePeakDb", peak, MeasurementReason::None, 0 };
        for (std::size_t i = 0; i < kAnalyzers; ++i)
        { results[i].analyzer = Analyzer (i); results[i].status = MeasurementStatus::Unavailable; results[i].reason = MeasurementReason::NotImplemented; }
        ready (Analyzer::Loudness, loudness);
        in.rules = detail::rules(); in.row = *in.rules.find (target); in.channels = 2; in.sampleRate = 48000; in.frames = 48000ull * 60ull;
        in.measurements = results;
        measured (Needle::Ready, 100.0, 1.0, 0.1);
        clips (0.0);
    }
    Faked (const Faked&) = delete;
    void ready (Analyzer a, std::span<const MeasurementValue> numbers)
    {
        auto& r = results[std::size_t (a)];
        r.status = MeasurementStatus::Ready; r.reason = MeasurementReason::None; r.numbers = numbers;
    }
    // The needles' result: `runs` excursions, 90 % of them no longer than `p90Ms`, `bass` of their dose under 200 Hz.
    Faked& measured (Needle how, double runs = 100.0, double p90Ms = 1.0, double bass = 0.1)
    {
        needles[0] = { "runCount", runs, MeasurementReason::None, 0 };
        needles[1] = { "p90Ms", p90Ms, MeasurementReason::None, 0 };
        needles[2] = { "bassDoseShare", bass, MeasurementReason::None, 0 };
        auto& r = results[std::size_t (Analyzer::Excursions)];
        r.numbers = {};
        in.needlesCurrent = how != Needle::Stale;
        switch (how)
        {
            case Needle::Ready:
            case Needle::Stale:       ready (Analyzer::Excursions, needles); break;
            case Needle::Pending:     r.status = MeasurementStatus::Pending; r.reason = MeasurementReason::Pending; break;
            case Needle::Cancelled:   r.status = MeasurementStatus::Cancelled; r.reason = MeasurementReason::Cancelled; break;
            case Needle::Unavailable: r.status = MeasurementStatus::Unavailable; r.reason = MeasurementReason::Memory; break;
        }
        return *this;
    }
    // The clip detector's confirmed clips, of a programme a minute long: `perMinute` of them.
    Faked& clips (double perMinute)
    {
        clipping[0] = { "runCount", perMinute, MeasurementReason::None, 0 };
        ready (Analyzer::Clipping, clipping);
        return *this;
    }
    // A target whose headroom — its ceiling over its loudness — is `headroom` dB: the need is the input's PLR less it.
    Faked& headroom (double db)
    {
        in.targetEdit.tp = -1.0;
        in.targetEdit.lufs = -1.0 - db;
        return *this;
    }
    Devices machine() const
    {
        Devices d; DevicePlans plans; detail::PlanFindings found;
        detail::propose (in, d, plans, found);
        return d;
    }
};

// THE OWNER'S TABLE (decisions 3.6, 3.7), written here in its own numbers, never read from the config: what the machine
// does with the needles of an input whose need is above 3 dB.
struct Want { NeedlesClass proposed; NeedlesWhy why; double overDb; };
Want ownersTable (double plr, double p90Ms, double bass, double clipsPerMinute)
{
    if (clipsPerMinute >= 10.0) return { NeedlesClass::None, NeedlesWhy::Clipped, 0.0 };
    if (plr < 8.0) return { NeedlesClass::None, NeedlesWhy::LowPlr, 0.0 };
    if (bass >= 0.5) return { NeedlesClass::None, NeedlesWhy::Bass, 0.0 };
    if (p90Ms >= 8.0) return { NeedlesClass::None, NeedlesWhy::Long, 0.0 };
    if (p90Ms <= 2.0 && bass <= 0.25 && plr >= 10.0) return { NeedlesClass::Short, NeedlesWhy::Cuts, 3.0 };
    return { NeedlesClass::Between, NeedlesWhy::Cuts, 1.5 };
}

void theClasses()
{
    felitronics::test::group ("the needles' classes: every boundary of the owner's table, all together");
    // The boundaries themselves, a value just inside and one just outside each.
    const double plrs[] { 7.0, 7.99, 8.0, 9.99, 10.0, 14.0 };
    const double p90s[] { 0.5, 2.0, 2.01, 7.99, 8.0, 30.0 };
    const double basses[] { 0.0, 0.25, 0.26, 0.49, 0.5, 0.9 };
    const double clipRates[] { 0.0, 9.99, 10.0, 400.0 };
    unsigned cases = 0, right = 0, shorts = 0, betweens = 0, offs = 0;
    std::string firstWrong;
    for (const double plr : plrs) for (const double p90 : p90s) for (const double bass : basses) for (const double rate : clipRates)
    {
        // A loudness of −20 LUFS and a peak `plr` above it; a target with 1 dB of headroom: the need is the PLR less 1.
        Faked f ("allStreaming", -20.0, -20.0 + plr);
        f.headroom (1.0).measured (Needle::Ready, 50.0, p90, bass).clips (rate);
        const auto got = detail::needlesAnswer (f.in);
        const auto want = ownersTable (plr, p90, bass, rate);
        ++cases;
        const bool good = got.proposed == want.proposed && got.why == want.why
            && (want.proposed == NeedlesClass::None ? ! got.overDb.has_value() : got.overDb && sameBits (*got.overDb, want.overDb));
        right += good ? 1u : 0u;
        shorts += want.proposed == NeedlesClass::Short ? 1u : 0u;
        betweens += want.proposed == NeedlesClass::Between ? 1u : 0u;
        offs += want.proposed == NeedlesClass::None ? 1u : 0u;
        if (! good && firstWrong.empty())
            firstWrong = " — first at PLR " + std::to_string (plr) + ", p90 " + std::to_string (p90) + " ms, bass " + std::to_string (bass)
                       + ", " + std::to_string (rate) + " clips a minute";
    }
    ok (right == cases && cases == 864, std::to_string (right) + " of " + std::to_string (cases)
        + " combinations of PLR, p90, bass share and clips a minute are classed as the owner's table says" + firstWrong);
    ok (shorts > 0 && betweens > 0 && offs > 0, "and the table has all three answers in it: " + std::to_string (shorts) + " short, "
        + std::to_string (betweens) + " between, " + std::to_string (offs) + " not cut");

    // Each boundary by itself, by name.
    const auto at = [] (double plr, double p90, double bass, double rate = 0.0)
    {
        Faked f ("allStreaming", -20.0, -20.0 + plr);
        f.headroom (1.0).measured (Needle::Ready, 50.0, p90, bass).clips (rate);
        return detail::needlesAnswer (f.in);
    };
    ok (at (10.0, 2.0, 0.25).proposed == NeedlesClass::Short && same (*at (10.0, 2.0, 0.25).overDb, 3.0),
        "p90 2 ms, bass 0.25, PLR 10 — each exactly on its bound — is short: up to 3 dB off the peaks");
    ok (at (9.99, 2.0, 0.25).proposed == NeedlesClass::Between && at (10.0, 2.01, 0.25).proposed == NeedlesClass::Between
        && at (10.0, 2.0, 0.26).proposed == NeedlesClass::Between && same (*at (10.0, 2.0, 0.26).overDb, 1.5),
        "one step past any of the three is between: up to 1.5 dB off the peaks");
    ok (at (8.0, 7.99, 0.49).proposed == NeedlesClass::Between, "PLR exactly 8, p90 just under 8 ms, bass just under 0.5: still cut, with care");
    ok (at (7.99, 1.0, 0.1).why == NeedlesWhy::LowPlr && at (12.0, 8.0, 0.1).why == NeedlesWhy::Long && at (12.0, 1.0, 0.5).why == NeedlesWhy::Bass,
        "PLR under 8, p90 exactly 8 ms, bass exactly 0.5: each alone rules the clipper out, with its own reason");
    ok (at (12.0, 1.0, 0.1, 10.0).why == NeedlesWhy::Clipped && at (12.0, 1.0, 0.1, 9.99).proposed == NeedlesClass::Short,
        "ten confirmed clips a minute is a clipped source — no clipper, whatever the needles; under ten it is allowed");
    ok (at (7.0, 30.0, 0.9, 400.0).why == NeedlesWhy::Clipped && at (7.0, 30.0, 0.9).why == NeedlesWhy::LowPlr
        && at (12.0, 30.0, 0.9).why == NeedlesWhy::Bass,
        "where several hold, the first in the stated order is the reason: clipped, then the PLR, then the bass, then the length");
}

void theNeedAndTheMeasurement()
{
    felitronics::test::group ("a need of 3 dB is not measured; needles that were not measured are not needles that are not there");
    // PLR 12 (−18 LUFS, −6 dBTP); a headroom of 9 dB makes the need exactly 3.
    Faked exact ("allStreaming", -18.0, -6.0);
    exact.headroom (9.0);
    const auto little = detail::needlesAnswer (exact.in);
    ok (little.needDb && same (*little.needDb, 3.0) && little.why == NeedlesWhy::LittleNeed && little.proposed == NeedlesClass::None,
        "a need of exactly 3 dB: the machine does not cut, and says the need is little");
    ok ((detail::needs (exact.in, exact.machine(), true, *std::make_unique<DevicePlans>()) & detail::bitOf (Analyzer::Excursions)) == 0
        && detail::waiting (exact.in, detail::bitOf (Analyzer::Excursions)) == 0,
        "its plan reads no needles: its answer stands without them");
    Faked above ("allStreaming", -18.0, -6.0);
    above.headroom (8.75);
    ok (same (*detail::needlesAnswer (above.in).needDb, 3.25) && detail::needlesAnswer (above.in).proposed == NeedlesClass::Short,
        "a quarter of a decibel more need, and the measured needles are classed");

    Faked f ("allStreaming", -18.0, -6.0);
    f.headroom (1.0);
    ok (detail::needlesAnswer (f.measured (Needle::Ready, 0.0).in).why == NeedlesWhy::NoExcursions,
        "measured, and no peak stands above the ceiling: nothing to cut");
    const auto failed = detail::needlesAnswer (f.measured (Needle::Unavailable).in);
    const auto stopped = detail::needlesAnswer (f.measured (Needle::Cancelled).in);
    ok (failed.why == NeedlesWhy::Unmeasured && failed.reason == MeasurementReason::Memory
        && stopped.why == NeedlesWhy::Unmeasured && stopped.reason == MeasurementReason::Cancelled,
        "a measurement that ended without a value — refused its memory, or cancelled — is not measured, with how it ended");
    ok (failed.why != NeedlesWhy::NoExcursions, "and the two are different reasons");
    ok (detail::needlesAnswer (f.measured (Needle::Pending).in).why == NeedlesWhy::Pending
        && detail::needlesAnswer (f.measured (Needle::Stale).in).why == NeedlesWhy::Pending,
        "a running measurement, or a result measured at another target's ceiling, is not an answer yet");
    // A clipped source is ruled out whatever its needles are: while they are measured its answer is already there.
    Faked clippedEarly ("allStreaming", -18.0, -6.0);
    clippedEarly.headroom (1.0).clips (50.0).measured (Needle::Pending);
    const auto early = detail::needlesAnswer (clippedEarly.in);
    ok (early.why == NeedlesWhy::Clipped && early.proposed == NeedlesClass::None && early.clipsPerMinute && same (*early.clipsPerMinute, 50.0)
        && (detail::needs (clippedEarly.in, clippedEarly.machine(), true, *std::make_unique<DevicePlans>()) & detail::bitOf (Analyzer::Excursions)) == 0,
        "a source clipped 50 times a minute, its needles still measured: ruled out as clipped, and its plan reads no needles");
    DevicePlans plans;
    f.measured (Needle::Stale);
    const auto needs = detail::needs (f.in, f.machine(), true, plans);
    ok (detail::waiting (f.in, needs) == detail::bitOf (Analyzer::Excursions), "and the plan waits for the needles at its own ceiling");

    // The reasons as the plan holds the device back.
    const auto held = [] (Faked& x)
    {
        Devices d; DevicePlans p; detail::PlanFindings found;
        detail::propose (x.in, d, p, found);
        return p.limiter.heldBack;
    };
    Faked clipped ("allStreaming", -18.0, -6.0); clipped.headroom (1.0).clips (50.0);
    Faked unmeasured ("allStreaming", -18.0, -6.0); unmeasured.headroom (1.0).measured (Needle::Unavailable);
    Faked cut ("allStreaming", -18.0, -6.0); cut.headroom (1.0);
    ok (held (clipped) == HeldBack::Measured && held (unmeasured) == HeldBack::Unmeasured && held (cut) == HeldBack::None && held (exact) == HeldBack::None,
        "the plan says what holds the clipper back: a measurement that ruled against it, one that did not end with a value — or nothing");
}

void thePersonsThreshold()
{
    felitronics::test::group ("a person's manual threshold sounds over every refusal of the machine's, with the machine's reason beside it");
    struct Case { const char* name; NeedlesWhy why; text::Term term; };
    const auto manual = [] (Faked& f, double x)
    {
        auto d = f.machine();
        d.limiter.hand.needles = Needles::Manual;
        d.limiter.hand.needlesDb = x;
        return detail::limiterFinding (f.in, d);
    };
    const auto warned = [] (const LimiterFinding& f, NeedlesWhy why, const char* russian)
    {
        const auto said = PlanText::needlesAgainstMachine (f);
        return f.cutting && f.mode == Needles::Manual && f.againstMachine && f.why == why && f.sounding == Sounding::Hand
            && said && whole (*said) && ru (*said).find ("Машина бы не резала") == 0 && ru (*said).find (russian) != std::string::npos;
    };
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (9.0);
        const auto got = manual (f, 2.0);
        const auto against = PlanText::needlesAgainstMachine (got);
        ok (warned (got, NeedlesWhy::LittleNeed, "нужда") && same (got.overDb, 2.0), "over a need of 3 dB: it cuts its 2 dB off the peaks — " + (against ? ru (*against) : std::string()));
    }
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0).measured (Needle::Unavailable);
        ok (warned (manual (f, 0.5), NeedlesWhy::Unmeasured, "не измерены"), "over needles that were not measured");
    }
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0).measured (Needle::Ready, 0.0);
        ok (warned (manual (f, 0.5), NeedlesWhy::NoExcursions, "пиков выше потолка нет"), "over needles that are not there");
    }
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0).clips (10.0);
        const auto got = manual (f, 6.0);
        ok (warned (got, NeedlesWhy::Clipped, "источник клипован") && same (got.overDb, 6.0) && got.clipsPerMinute && same (*got.clipsPerMinute, 10.0),
            "over a clipped source — ten clips a minute — at the far end of its domain, 6 dB");
    }
    {
        Faked f ("allStreaming", -20.0, -13.0); f.headroom (1.0);
        ok (warned (manual (f, 1.0), NeedlesWhy::LowPlr, "уже ограничен"), "over an input already limited");
    }
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0).measured (Needle::Ready, 50.0, 1.0, 0.7);
        ok (warned (manual (f, 1.0), NeedlesWhy::Bass, "баса"), "over bassy excursions");
        f.measured (Needle::Ready, 50.0, 12.0, 0.1);
        ok (warned (manual (f, 1.0), NeedlesWhy::Long, "длинные"), "over long ones");
    }
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0).measured (Needle::Pending);
        const auto got = manual (f, 1.0);
        ok (got.cutting && ! got.againstMachine && ! PlanText::needlesAgainstMachine (got),
            "while the needles are still measured it cuts all the same, and no reason is invented for the machine");
        DevicePlans plans;
        auto d = f.machine(); d.limiter.hand.needles = Needles::Manual;
        ok ((detail::needs (f.in, d, true, plans) & detail::bitOf (Analyzer::Excursions)) == 0,
            "and the master does not wait for them: a manual threshold reads no measurement");
    }
    {
        // Where the machine cuts too, there is nothing to warn of.
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0);
        const auto got = manual (f, 2.0);
        ok (got.cutting && ! got.againstMachine && got.proposed == NeedlesClass::Short && got.sounding == Sounding::Hand && same (got.overDb, 2.0),
            "where the machine cuts too — short needles, up to 3 dB — the person's 2 dB sounds, without a warning");
        ok (ru (PlanText::limiter (got)).find ("по ручной настройке") != std::string::npos && whole (PlanText::limiter (got)),
            "and the limiter's line names it as manual: " + ru (PlanText::limiter (got)));
    }
    {
        // A knob turned is a device wanted: the threshold alone, with no mode set, is manual.
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (9.0);
        auto d = f.machine();
        d.limiter.hand.needlesDb = 2.5;
        const auto got = detail::limiterFinding (f.in, d);
        ok (got.mode == Needles::Manual && got.cutting && same (got.overDb, 2.5) && got.againstMachine,
            "a threshold turned by hand, with no mode set, is a manual threshold — and sounds");
        d.limiter.hand.needles = Needles::Auto;
        const auto automatic = detail::limiterFinding (f.in, d);
        ok (automatic.mode == Needles::Auto && ! automatic.cutting && automatic.sounding == Sounding::Proposal,
            "set back to auto by hand it is the machine's answer again, the person's threshold kept and silent");
        d.limiter.hand.needles = Needles::Off;
        Faked cuts ("allStreaming", -18.0, -6.0); cuts.headroom (1.0);
        auto off = cuts.machine(); off.limiter.hand.needles = Needles::Off;
        const auto silenced = detail::limiterFinding (cuts.in, off);
        ok (! silenced.cutting && silenced.mode == Needles::Off && silenced.sounding == Sounding::Hand && silenced.proposed == NeedlesClass::Short
            && PlanText::limiter (silenced).id == text::FactId::LimiterNeedlesOff,
            "switched off by hand where the machine would cut: no clipper, and the line says it is off — " + ru (PlanText::limiter (silenced)));
    }
    // The write: the limiter's stage as the chain gets it.
    {
        Faked f ("allStreaming", -18.0, -6.0); f.headroom (1.0);
        mastering::MasteringChainParams p;
        detail::writeLimiter (f.in, f.machine(), p);
        ok (p.limiter.peakClip && same (p.limiter.overCeilingDb, 8.0) && same (p.limiter.kneeDb, 0.0) && same (p.limiter.releaseMs, 50.0)
            && ! p.limiter.dualRelease && same (p.limiter.slowReleaseMs, 200.0) && same (p.limiter.ceilingDbTp, -1.0) && ! p.bypassLimiter,
            "short needles on a need of 11 dB: the peak clipper on, hard, 3 dB off the peaks — its threshold 8 dB above the ceiling — "
            "inside the limiter, its release 50 ms, never bypassed");
        auto d = f.machine(); d.limiter.hand.needles = Needles::Manual; d.limiter.hand.needlesDb = 0.0;
        detail::writeLimiter (f.in, d, p);
        ok (p.limiter.peakClip && same (p.limiter.overCeilingDb, 11.0), "a manual cut of 0 dB stands the threshold at the need, 11 dB: nothing off the peaks");
        f.clips (30.0);
        detail::writeLimiter (f.in, f.machine(), p);
        ok (! p.limiter.peakClip && ! p.bypassLimiter, "a clipped source: the clipper's flag off, the limiter in the chain all the same");
    }
    // The lines: every reason the machine has is a whole sentence in both languages, and none claims a cut.
    {
        bool sentences = true;
        std::string lines;
        const auto line = [&] (Faked& f)
        {
            const auto finding = detail::limiterFinding (f.in, f.machine());
            const auto fact = PlanText::limiter (finding);
            sentences = sentences && whole (fact) && ! finding.cutting && ru (fact).find ("не режутся") != std::string::npos;
            lines += "\n        " + ru (fact);
        };
        Faked little ("allStreaming", -18.0, -6.0); little.headroom (9.0); line (little);
        Faked none ("allStreaming", -18.0, -6.0); none.headroom (1.0).measured (Needle::Ready, 0.0); line (none);
        Faked unmeasured ("allStreaming", -18.0, -6.0); unmeasured.headroom (1.0).measured (Needle::Cancelled); line (unmeasured);
        Faked clipped ("allStreaming", -18.0, -6.0); clipped.headroom (1.0).clips (42.0); line (clipped);
        Faked dense ("allStreaming", -20.0, -13.0); dense.headroom (1.0); line (dense);
        Faked bassy ("allStreaming", -18.0, -6.0); bassy.headroom (1.0).measured (Needle::Ready, 9.0, 1.0, 0.75); line (bassy);
        Faked longer ("allStreaming", -18.0, -6.0); longer.headroom (1.0).measured (Needle::Ready, 9.0, 11.0, 0.1); line (longer);
        Faked vinyl ("lp", -18.0, -6.0); line (vinyl);
        Faked quiet ("allStreaming", -60.0, -40.0); quiet.headroom (1.0); line (quiet);
        ok (sentences, "every reason the machine does not cut for is a whole sentence, ru and en:" + lines);
        Faked cut ("allStreaming", -18.0, -6.0); cut.headroom (1.0);
        const auto shortLine = PlanText::limiter (detail::limiterFinding (cut.in, cut.machine()));
        cut.measured (Needle::Ready, 50.0, 5.0, 0.3);
        const auto betweenLine = PlanText::limiter (detail::limiterFinding (cut.in, cut.machine()));
        ok (shortLine.id == text::FactId::LimiterShort && betweenLine.id == text::FactId::LimiterBetween && whole (shortLine) && whole (betweenLine),
            "and the two classes that cut:\n        " + ru (shortLine) + "\n        " + ru (betweenLine));
        // The cut is a cap the landing spends as it needs, never the amount it takes: the message says so in its own
        // words ("не больше" / "no more than"), so {cut} is Bound::Exact — the line never reads "no more than ≤ …".
        const auto en = [] (const text::Fact& f) { return text::Text::text (f, text::Lang::En); };
        const auto capped = [&] (const text::Fact& f)
        {
            return f.args[1].bound == text::Bound::Exact && ru (f).find ("не больше ") != std::string::npos
                && en (f).find ("no more than ") != std::string::npos && ru (f).find ("\xE2\x89\xA4") == std::string::npos
                && en (f).find ("\xE2\x89\xA4") == std::string::npos;
        };
        ok (capped (shortLine) && capped (betweenLine),
            "the clipper's cut is a cap, said in words, the number exact:\n        " + ru (shortLine) + "\n        " + ru (betweenLine));
    }
}

// THE CUT IS AN AMOUNT (owner, 30.09): the clipper takes at most its class's decibels off the peaks — 3 short, 1.5
// between — and the limiter does the rest; the threshold stands max(0, need − cut) above the ceiling. Before, the two
// numbers were thresholds, and the cautious class cut deeper than the bold one.
void theCutIsAnAmount()
{
    felitronics::test::group ("the peak clipper cuts an amount off the peaks: at most 3 dB short, 1.5 between, the limiter the rest");
    // PLR 10.9 (−20 LUFS, −9.1 dBTP) and 3 dB of headroom: a need of 7.9 dB, the needles short, then between.
    Faked f ("allStreaming", -20.0, -9.1);
    f.headroom (3.0).measured (Needle::Ready, 50.0, 1.0, 0.1);
    mastering::MasteringChainParams shortChain, betweenChain;
    const auto shortOne = detail::limiterFinding (f.in, f.machine());
    detail::writeLimiter (f.in, f.machine(), shortChain);
    f.measured (Needle::Ready, 50.0, 5.0, 0.3);
    const auto betweenOne = detail::limiterFinding (f.in, f.machine());
    detail::writeLimiter (f.in, f.machine(), betweenChain);
    ok (shortOne.needDb && betweenOne.needDb && std::fabs (*shortOne.needDb - 7.9) < 1e-9 && shortOne.proposed == NeedlesClass::Short
        && betweenOne.proposed == NeedlesClass::Between && shortChain.limiter.peakClip && betweenChain.limiter.peakClip,
        "PRECONDITION: a need of 7.9 dB, short needles and then between, both cut");
    const double shortCut = *shortOne.needDb - shortChain.limiter.overCeilingDb;
    const double betweenCut = *betweenOne.needDb - betweenChain.limiter.overCeilingDb;
    ok (std::fabs (shortCut - 3.0) < 1e-9 && std::fabs (betweenCut - 1.5) < 1e-9 && betweenCut < shortCut,
        "on one need of 7.9 dB the cautious class cuts less than the bold one: " + std::to_string (betweenCut) + " dB between, "
        + std::to_string (shortCut) + " dB short (thresholds " + std::to_string (betweenChain.limiter.overCeilingDb) + " and "
        + std::to_string (shortChain.limiter.overCeilingDb) + " dB above the ceiling)");
    ok (same (*shortOne.proposedOverDb, 3.0) && same (shortOne.overDb, 3.0) && same (*betweenOne.proposedOverDb, 1.5),
        "the finding carries the amount, 3 and 1.5 dB, as the plan's line says it");
    // A need the cut cannot close: a threshold of need − cut beyond the limiter's working range would cut more.
    Faked wide ("allStreaming", -30.0, -2.0);
    wide.headroom (1.0).measured (Needle::Ready, 50.0, 1.0, 0.1);
    mastering::MasteringChainParams wideChain;
    detail::writeLimiter (wide.in, wide.machine(), wideChain);
    ok (detail::limiterFinding (wide.in, wide.machine()).cutting && ! wideChain.limiter.peakClip && ! wideChain.bypassLimiter,
        "a need of 27 dB puts the threshold past the limiter's 12 dB range: the clipper stays off rather than cut more than 3 dB");
    // A person's manual X is X dB off the peaks, whatever the need.
    auto d = f.machine(); d.limiter.hand.needles = Needles::Manual; d.limiter.hand.needlesDb = 2.5;
    mastering::MasteringChainParams manualChain;
    detail::writeLimiter (f.in, d, manualChain);
    ok (manualChain.limiter.peakClip && same (manualChain.limiter.overCeilingDb, *betweenOne.needDb - 2.5),
        "a manual 2.5 dB on the need of 7.9: the threshold 5.4 dB above the ceiling");
    Faked little ("allStreaming", -18.0, -6.0); little.headroom (9.0);
    auto all = little.machine(); all.limiter.hand.needles = Needles::Manual; all.limiter.hand.needlesDb = 6.0;
    detail::writeLimiter (little.in, all, manualChain);
    ok (manualChain.limiter.peakClip && same (manualChain.limiter.overCeilingDb, 0.0),
        "a manual 6 dB over a need of 3: the threshold at the ceiling, never under it");
}

//==============================================================================
// THE DITHER

void theDither()
{
    felitronics::test::group ("the dither: by the delivery's format — 16 bits only, a person's off allowed, a tick above 16 bits kept without effect");
    const auto chain = [] (Faked& f, const Devices& d)
    {
        command::MasterReady ready;
        detail::writeChain (f.in, d, ready);
        return ready;
    };
    Faked cd ("cd", -18.0, -6.0);
    const auto on = detail::ditherFinding (cd.in, cd.machine());
    const auto written = chain (cd, cd.machine());
    ok (on.bits == 16 && on.applies && on.on && ! on.offByHand && ! on.keptWithoutEffect && written.topology.dither,
        "cd, 16 bits: the dither is in the chain");
    ok (written.params.dither.bits == 16 && written.params.dither.shaping == felitronics::dither::NoiseShaping::Weighted
        && written.params.dither.seed == 0x853c49e6748fea9bull && written.params.dither.autoBlank
        && written.params.dither.autoBlankSamples == 4096 && ! written.params.bypassDither,
        "its noise is the stated version: weighted TPDF, seed 0x853c49e6748fea9b, blanked after 4096 zero samples");
    auto off = cd.machine(); off.dither.hand.on = false;
    const auto byHand = detail::ditherFinding (cd.in, off);
    ok (byHand.applies && ! byHand.on && byHand.offByHand && ! chain (cd, off).topology.dither
        && PlanText::dither (byHand).id == text::FactId::DitherOffByHand && whole (PlanText::dither (byHand)),
        "a person's off at 16 bits is obeyed: no dither stage — " + ru (PlanText::dither (byHand)));
    Faked streaming ("allStreaming", -18.0, -6.0);
    const auto none = detail::ditherFinding (streaming.in, streaming.machine());
    const auto plain = chain (streaming, streaming.machine());
    ok (none.bits == 24 && ! none.applies && ! none.on && ! none.keptWithoutEffect && ! plain.topology.dither
        && plain.params.dither.shaping == felitronics::dither::NoiseShaping::None && plain.params.dither.bits == 24
        && PlanText::dither (none).id == text::FactId::DitherNotApplied,
        "allStreaming, 24 bits: no dither stage at all — " + ru (PlanText::dither (none)));
    auto kept = streaming.machine(); kept.dither.hand.on = true;
    const auto silent = detail::ditherFinding (streaming.in, kept);
    DevicePlans plans;
    (void) detail::needs (streaming.in, kept, true, plans);
    ok (silent.keptWithoutEffect && ! silent.on && ! chain (streaming, kept).topology.dither && ! plans.dither.on
        && plans.dither.tick == TickFrom::Hand,
        "a person's tick at 24 bits is kept and does nothing: no stage, and the plan says the device is out while the tick is a person's");
    ok (PlanText::dither (silent).id == text::FactId::DitherKept && whole (PlanText::dither (silent))
        && ru (PlanText::dither (silent)).find ("не действует") != std::string::npos,
        "with its reason: " + ru (PlanText::dither (silent)));
    ok (whole (PlanText::dither (on)) && ru (PlanText::dither (on)).find ("16") != std::string::npos, "and where it sounds: " + ru (PlanText::dither (on)));

    // Through the session: a tick above 16 bits arrives by a project file, is kept, exported again, and the device is out.
    auto s = Session::create().session;
    const float samples[4] { 0, .25f, -.25f, 0 }; const float* planes[2] { samples, samples };
    (void) s->apply (command::Load { 1, { planes, 2, 4, 48000 }, { "short.wav", 48000, true, 24 } });
    (void) detail::Driver::measured1 (*s, s->measurementJob(), s->source().hash);
    const auto saved = s->exportProject();
    const std::string text = std::string (saved.view()) + "\n[dither]\non.hand = true\n";
    const auto imported = s->importProject (2, text);
    const auto view = s->snapshot();
    ok (imported.rejection == Rejection::None && s->project().devices.dither.hand.on && *s->project().devices.dither.hand.on
        && view.view().plan.dither.keptWithoutEffect && ! view.view().plan.devices.dither.on
        && view.view().plan.devices.dither.heldBack == HeldBack::Target,
        "an imported project's dither tick on a 24-bit target: kept in the project, out of the chain, held back by the target");
    const auto again = s->exportProject();
    ok (std::string (again.view()).find ("on.hand = true") != std::string::npos, "and written again when the project is exported");
    DitherFields<Touched> tick; tick.on = true;
    ok (s->apply (command::EditDevice { 3, tick }).rejection == Rejection::NotOffered,
        "a new edit of it is refused as before: the device is not offered at 24 bits");
}

//==============================================================================
// VINYL AND A QUIET INPUT, ON THE PLANNER

void vinylOnThePlanner()
{
    felitronics::test::group ("vinyl: the machine never above the medium's ceiling and never cutting needles; a person's hand is obeyed and warned");
    // An input whose need on lp is far above 3 dB, with short needles: on any other target the clipper would cut.
    Faked lp ("lp", -30.0, -6.0);
    const auto machine = detail::limiterFinding (lp.in, lp.machine());
    ok (machine.vinyl && same (machine.ceilingDbTp, -3.0) && same (machine.mediumCeilingDbTp, -3.0) && ! machine.cutting
        && machine.why == NeedlesWhy::Target && machine.mode == Needles::Off && ! machine.ceilingAboveMedium && ! machine.needlesAgainstMedium
        && *machine.needDb > 3.0,
        "lp: the ceiling −3 dBTP and no peak clipper, with a need of " + std::to_string (*machine.needDb) + " dB");
    ok (! PlanText::vinylCeiling (machine) && ! PlanText::vinylNeedles (machine), "with nothing to warn of");
    const auto top = PlanText::vinylTop (machine);
    ok (top && whole (*top) && ru (*top).find ("16") != std::string::npos && ru (*top).find ("резчик") != std::string::npos,
        "and the constant note about the top, with no threshold behind it: " + (top ? ru (*top) : std::string()));
    Faked streaming ("allStreaming", -30.0, -6.0);
    ok (! PlanText::vinylTop (detail::limiterFinding (streaming.in, streaming.machine())), "no other target carries it");
    DevicePlans plans;
    ok ((detail::needs (lp.in, lp.machine(), true, plans) & detail::bitOf (Analyzer::Excursions)) == 0,
        "the machine reads no needles on vinyl: nothing to wait for");
    {
        // A target without the clipper that is no vinyl: a person's needles there are not against a medium.
        Faked plain ("lp", -30.0, -6.0);
        plain.in.rules = detail::readRules (felitronics::session::test::embedded::noVinyl.root(), plain.in.rules.engine, plain.in.rules.geometry);
        plain.in.row = *plain.in.rules.find ("lp");
        auto hand = plain.machine(), onVinyl = lp.machine();
        hand.limiter.hand.needles = onVinyl.limiter.hand.needles = Needles::Manual;
        hand.limiter.hand.needlesDb = onVinyl.limiter.hand.needlesDb = 1.0;
        const auto noMedium = detail::limiterFinding (plain.in, hand), medium = detail::limiterFinding (lp.in, onVinyl);
        ok (plain.in.rules.row (plain.in.row).noClipper && ! plain.in.rules.row (plain.in.row).vinyl && noMedium.cutting && ! noMedium.vinyl
            && ! noMedium.needlesAgainstMedium && ! PlanText::vinylNeedles (noMedium) && medium.cutting && medium.needlesAgainstMedium,
            "needles cut by hand on a target without the clipper warn of the medium only where it is vinyl");
    }

    // A person's ceiling above the medium's.
    lp.in.targetEdit.tp = -1.0;
    const auto raised = detail::limiterFinding (lp.in, lp.machine());
    const auto warning = PlanText::vinylCeiling (raised);
    ok (same (raised.ceilingDbTp, -1.0) && raised.ceilingAboveMedium && warning && whole (*warning)
        && ru (*warning).find ("резчик может вернуть лак") != std::string::npos,
        "a person's ceiling of −1 dBTP sounds, with the medium's warning: " + (warning ? ru (*warning) : std::string()));
    mastering::MasteringChainParams p;
    detail::writeLimiter (lp.in, lp.machine(), p);
    ok (same (p.limiter.ceilingDbTp, -1.0) && ! p.limiter.peakClip, "and it is the ceiling the limiter is given — no refusal by the profile");
    lp.in.targetEdit.tp = -3.0;
    ok (! detail::limiterFinding (lp.in, lp.machine()).ceilingAboveMedium, "a person's ceiling AT the medium's is no departure");
    lp.in.targetEdit.tp.reset();

    // A person's needles on vinyl.
    auto d = lp.machine();
    d.limiter.hand.needles = Needles::Manual; d.limiter.hand.needlesDb = 1.0;
    const auto cutting = detail::limiterFinding (lp.in, d);
    const auto needlesWarning = PlanText::vinylNeedles (cutting);
    const auto against = PlanText::needlesAgainstMachine (cutting);
    ok (cutting.cutting && cutting.needlesAgainstMedium && cutting.againstMachine && cutting.why == NeedlesWhy::Target
        && needlesWarning && whole (*needlesWarning) && against && ru (*against).find ("у этой цели клиппера игл нет") != std::string::npos,
        "a person's manual threshold cuts on vinyl, with both warnings: " + (needlesWarning ? ru (*needlesWarning) : std::string())
        + " / " + (against ? ru (*against) : std::string()));
    detail::writeLimiter (lp.in, d, p);
    ok (p.limiter.peakClip && cutting.needDb && same (p.limiter.overCeilingDb, std::max (0.0, *cutting.needDb - 1.0)),
        "and the clipper is written: 1 dB off the peaks");
    d.limiter.hand.needles = Needles::Auto; d.limiter.hand.needlesDb.reset();
    const auto automatic = detail::limiterFinding (lp.in, d);
    ok (! automatic.cutting && ! automatic.needlesAgainstMedium, "auto by hand on vinyl is the machine's answer: it does not cut");

    // The rest of the medium's row: the high-pass from 32 Hz at 12 dB/oct, mono bass at 150 Hz, the low shelf +0.5 dB.
    const auto row = lp.machine();
    ok (row.hpf.machine.on && same (row.hpf.machine.fq, 32.0) && row.hpf.machine.slope == 12 && same (row.monoBass.machine.fq, 150.0)
        && row.low.machine.on && same (row.low.machine.db, 0.5) && row.limiter.machine.needles == Needles::Off
        && lp.in.rules.row (lp.in.row).sampleRate == 0,
        "the machine's layer on lp: high-pass 32 Hz at 12 dB/oct, mono bass 150 Hz, low +0.5 dB, no needles, the source's rate kept");
}

void aQuietInput()
{
    felitronics::test::group ("a quiet input: strictly under −55 LUFS the machine sets the gain, the ceiling, the format's dither and the high-pass at its floor");
    const double under = std::nextafter (-55.0, -100.0);
    Faked boundary ("cd", -55.0, -40.0), quiet ("cd", under, -40.0);
    ok (! detail::quietInput (boundary.in) && detail::quietInput (quiet.in), "exactly −55 LUFS is still the ordinary mode; the next double under it is quiet");
    const auto ordinary = boundary.machine(), bare = quiet.machine();
    ok (ordinary.limiter.machine.needles == Needles::Auto && ordinary.dither.machine.on && ordinary.hpf.machine.on,
        "at the boundary the limiter's needles are the machine's to class, as on any input");
    ok (bare.hpf.machine.on && same (bare.hpf.machine.fq, 32.0) && ! bare.monoBass.machine.on && ! bare.glue.machine.on
        && bare.limiter.machine.needles == Needles::Off && bare.dither.machine.on && ! bare.low.machine.on && ! bare.tilt.machine.on
        && ! bare.saturation.machine.on,
        "under it, on cd: the high-pass at 32 Hz and the 16-bit dither stand; no mono bass, no glue, no needles, no other device");
    Faked quietLp ("lp", under, -40.0);
    ok (! quietLp.machine().low.machine.on && same (quietLp.machine().low.machine.db, 0.5),
        "on lp the low shelf is off too, its +0.5 dB left on the knob for a person's tick");
    DevicePlans plans; detail::PlanFindings found; Devices d;
    detail::propose (quiet.in, d, plans, found);
    ok (plans.limiter.heldBack == HeldBack::Quiet && plans.monoBass.heldBack == HeldBack::Quiet && plans.glue.heldBack == HeldBack::Quiet
        && plans.hpf.heldBack == HeldBack::None && plans.dither.heldBack == HeldBack::None,
        "each device the machine left out says why — the quiet input — and the two that stand are not held back");
    const auto finding = detail::limiterFinding (quiet.in, bare);
    ok (finding.why == NeedlesWhy::Quiet && ! finding.cutting && PlanText::limiter (finding).id == text::FactId::LimiterQuiet
        && (detail::needs (quiet.in, bare, true, plans) & detail::bitOf (Analyzer::Excursions)) == 0,
        "the limiter holds the ceiling without a clipper and waits for no needles: " + ru (PlanText::limiter (finding)));
    // The chain: the limiter, the dither, the EQ stage for the high-pass alone.
    command::MasterReady ready;
    detail::writeChain (quiet.in, bare, ready);
    const auto& t = ready.topology;
    ok (t.eq && ! t.monoBass && ! t.compressor && ! t.clipper && t.limiter && t.dither && ! ready.params.limiter.peakClip
        && ready.params.eqBands[0].on && ! ready.params.eqBands[1].on && ! ready.params.eqBands[2].on
        && same (ready.params.inputGainDb, 0.0) && same (ready.params.preLimiterGainDb, 0.0),
        "its chain: the high-pass, the limiter and the dither — the gain is the landing search's, once");
    // A person's hand is open all the same.
    auto hand = bare;
    hand.monoBass.hand.on = true; hand.limiter.hand.needles = Needles::Manual; hand.limiter.hand.needlesDb = 2.0; hand.tilt.hand.db = 1.0;
    detail::writeChain (quiet.in, hand, ready);
    ok (ready.topology.monoBass && ready.params.limiter.peakClip && ready.params.eqBands[1].on,
        "and a person may still switch a device on: mono bass, a manual threshold and a tilt all sound");
}

//==============================================================================
// THE RENDER

// A MIX THE SESSION CAN MEASURE AND MASTER: a kick every half second (120 BPM) with a click on its attack — needles —
// and a pad that swells in the second half, the right channel a little apart. `scale` moves its level, `clicks` the
// height of the needles.
struct Mix
{
    static constexpr unsigned rate = 48000;
    unsigned frames;
    std::vector<float> left, right;
    const float* planes[2] { nullptr, nullptr };
    explicit Mix (float scale = 1.0f, unsigned seconds = 12, double clicks = 0.35) : frames (rate * seconds), left (frames), right (frames)
    {
        namespace det = felitronics::core::det;
        for (unsigned i = 0; i < frames; ++i)
        {
            const double t = double (i) / rate, beat = double (i % (rate / 2)) / rate;
            const double kick = 0.22 * det::exp2 (-beat / 0.04) * det::sin (2 * kPi * 60.0 * beat);
            // A needle: half a millisecond of a 3 kHz burst on the attack.
            const double needle = beat < 0.0005 ? clicks * det::sin (2 * kPi * 3000.0 * beat + 0.5 * kPi) : 0.0;
            const double swell = t < 6.0 ? 0.6 : 1.0;
            const double pad = swell * (0.05 * det::sin (2 * kPi * 220.0 * t) + 0.04 * det::sin (2 * kPi * 331.0 * t));
            left[i] = scale * float (kick + needle + pad);
            right[i] = scale * float (kick + needle + swell * (0.05 * det::sin (2 * kPi * 220.0 * t + 0.4) + 0.04 * det::sin (2 * kPi * 331.0 * t + 1.1)));
        }
        planes[0] = left.data(); planes[1] = right.data();
    }
    Mix (const Mix&) = delete;
    command::Load load (CommandId id) const { return { id, { planes, 2, frames, rate }, { "mix.wav", rate, true, 24 } }; }
};

void drive (Session& s)
{
    for (unsigned i = 0; i < 4000000 && (s.measurementJob() || s.needlesJob()); ++i) (void) s.step (16);
}
// A session with the mix measured to the end, on `target`.
std::unique_ptr<Session> measured (const Mix& mix, const char* target)
{
    auto s = Session::create().session;
    (void) s->apply (command::SetTarget { 1, target });
    ok (s->apply (mix.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads");
    drive (*s);
    ok (s->state() == State::Measured2 && s->snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: measured, the plan ready");
    return s;
}
struct Done
{
    std::vector<text::Fact> facts;
    std::vector<float> audio;
    MasterAudioShape shape {};
    Kept kept {};
    bool made = false;
    bool said (text::FactId id) const { return std::any_of (facts.begin(), facts.end(), [&] (const text::Fact& f) { return f.id == id; }); }
    std::string line (text::FactId id) const
    {
        for (const auto& f : facts) if (f.id == id) return ru (f);
        return {};
    }
};
// The master's job to its end: the facts it published, its audio, and the kept master. The audio is then released.
Done finish (Session& s)
{
    Done out;
    for (unsigned i = 0; i < 4000000 && s.job() != 0; ++i)
    {
        (void) s.step (16);
        for (const auto& e : s.events()) if (e.kind == EventKind::Fact) out.facts.push_back (e.payload.fact.view());
    }
    if (s.job() != 0 || s.masters().empty()) return out;
    out.kept = s.masters().back();
    const auto token = s.pendingMaster();
    out.shape = s.masterAudioShape (token);
    out.audio.resize (std::size_t (out.shape.frames * out.shape.channels));
    out.made = token.master == out.kept.id && ! out.audio.empty() && s.copyMaster (token, out.audio) == MasterTransferStatus::Ok;
    (void) s.releaseMaster (token);
    return out;
}
bool sameAudio (const Done& a, const Done& b)
{
    if (! a.made || ! b.made || a.audio.size() != b.audio.size() || a.shape.sampleRate != b.shape.sampleRate) return false;
    for (std::size_t i = 0; i < a.audio.size(); ++i)
        if (std::bit_cast<std::uint32_t> (a.audio[i]) != std::bit_cast<std::uint32_t> (b.audio[i])) return false;
    return true;
}
detail::PlanInputs inputsOf (const Session& s, const Snapshot& snapshot)
{
    detail::PlanInputs in;
    in.rules = detail::rules(); in.row = s.project().target; in.targetEdit = s.project().targetEdit;
    in.channels = s.source().channels; in.sampleRate = s.source().sampleRate; in.frames = s.source().frames;
    in.measurements = snapshot.view().measurements;
    return in;
}
std::optional<double> numberOf (const Snapshot& snapshot, Analyzer analyzer, std::string_view name)
{
    const auto& r = snapshot.view().measurements[std::size_t (analyzer)];
    if (r.status == MeasurementStatus::Ready)
        for (const auto& v : r.numbers) if (v.name == name && v.value) return v.value;
    return {};
}

// THE CHAIN OF A MASTER OF THE SESSION'S PROJECT, COMPOSED HERE — the suite's own statement of the rules, in the owner's
// numbers: the fixed geometry, the three EQ devices' bands (the one EQ stage of tasks 02–03, pinned by their suites),
// mono bass by its tick on a stereo source, the glue and the saturation (task 04's write, pinned by its suite), the
// limiter with the clipper the owner's table gives this input, the dither at 16 bits. `version`: 1 to hand it to the
// previous path as a ready chain; 0 to fingerprint it as the session's own.
command::MasterReady composed (const Session& s, const Snapshot& snapshot, std::uint32_t version)
{
    const auto rules = detail::rules();
    const auto& project = s.project();
    const auto target = rules.row (project.target);
    command::MasterReady ready;
    ready.version = version;
    auto& t = ready.topology;
    t.internalBlock = 256; t.oversampleFactor = 4; t.tapsPerPhase = 64;
    t.compressorLookaheadMs = 1.0; t.limiterLookaheadMs = 1.0; t.sidechainHpfHz = 0.0;
    auto& p = ready.params;
    p.inputGainDb = 0.0; p.preLimiterGainDb = 0.0;
    detail::EqStage stage;
    detail::writeEq (project.devices, rules, stage);
    std::copy (std::begin (stage.bands), std::end (stage.bands), std::begin (p.eqBands));
    t.eq = stage.bands[0].on || stage.bands[1].on || stage.bands[2].on;
    const auto mono = detail::settingsOf (rules, project.devices.monoBass);
    t.monoBass = mono.on && s.source().channels == 2;
    p.monoBass.enabled = t.monoBass; p.monoBass.frequencyHz = float (mono.fq); p.monoBass.lowWidth = float (mono.width);
    t.stereoAir = false;
    p.stereoAir.enabled = false; p.stereoAir.frequencyHz = 6000.0f; p.stereoAir.gainDb = 0.0f;
    detail::writeDynamics (inputsOf (s, snapshot), project.devices, p);
    t.compressor = ! p.bypassCompressor;
    t.clipper = ! p.bypassClipper;
    // The limiter: the target's ceiling (a person's edit of it included), and the clipper by the owner's table.
    const double ceiling = project.targetEdit.tp.value_or (target.tp.toDouble());
    const double targetLufs = project.targetEdit.lufs.value_or (target.lufs.toDouble());
    const auto lufs = numberOf (snapshot, Analyzer::Loudness, "integratedLufs"), peak = numberOf (snapshot, Analyzer::Loudness, "truePeakDb");
    const double plr = *peak - *lufs, need = plr - (ceiling - targetLufs);
    const auto hand = project.devices.limiter.hand;
    const Needles mode = hand.needles ? *hand.needles : hand.needlesDb ? Needles::Manual : project.devices.limiter.machine.needles;
    bool cuts = false;
    double cut = 0.0;
    if (mode == Needles::Manual) { cuts = true; cut = hand.needlesDb.value_or (project.devices.limiter.machine.needlesDb); }
    else if (mode == Needles::Auto && ! target.noClipper && *lufs >= -55.0 && need > 3.0)
    {
        const auto runs = numberOf (snapshot, Analyzer::Excursions, "runCount");
        if (runs && *runs > 0.0)
        {
            const auto clips = numberOf (snapshot, Analyzer::Clipping, "runCount").value_or (0.0);
            const double minutes = double (s.source().frames) / double (s.source().sampleRate) / 60.0;
            const auto want = ownersTable (plr, *numberOf (snapshot, Analyzer::Excursions, "p90Ms"),
                                           *numberOf (snapshot, Analyzer::Excursions, "bassDoseShare"), clips / minutes);
            cuts = want.proposed != NeedlesClass::None;
            if (cuts) cut = want.overDb;
        }
    }
    // At most the cut off the peaks, which the need stands that far above the ceiling; off where no threshold can keep to it.
    // The chain is given the cut and the forecast peak at the limiter's input — the input's PLR over engine.toml [input]
    // referenceLufs, −18 — and works the threshold out from the peak the landing measures.
    const double over = cuts ? std::max (0.0, need - cut) : 1.5;
    cuts = cuts && over <= 12.0;
    p.limiter.ceilingDbTp = ceiling;
    p.limiter.releaseMs = 50.0; p.limiter.dualRelease = false; p.limiter.slowReleaseMs = 200.0;
    p.limiter.peakClip = cuts; p.limiter.overCeilingDb = cuts ? over : 1.5; p.limiter.kneeDb = 0.0;
    p.peakClipCutDb = cuts ? cut : std::numeric_limits<double>::quiet_NaN();
    p.peakClipPeakDb = cuts ? plr - 18.0 : std::numeric_limits<double>::quiet_NaN();
    t.limiter = true;
    // The dither: at 16 bits, unless a person switched it off.
    const bool dithered = target.bitDepth <= 16 && detail::settingsOf (rules, project.devices.dither).on;
    t.dither = dithered;
    p.dither.bits = target.bitDepth;
    p.dither.shaping = target.bitDepth <= 16 ? felitronics::dither::NoiseShaping::Weighted : felitronics::dither::NoiseShaping::None;
    p.dither.seed = 0x853c49e6748fea9bull; p.dither.autoBlank = true; p.dither.autoBlankSamples = 4096;
    return ready;
}
// The fingerprint the session records for a version-0 master of that chain: the job resolves the delivery (the target's
// rate — the source's where it keeps it — and depth) and stands the limiter's first ceiling a margin of 0.15 dB under.
std::uint64_t fingerprintOf (const Session& s, const Snapshot& snapshot)
{
    auto ready = composed (s, snapshot, 0);
    const auto target = detail::rules().row (s.project().target);
    ready.deliveryRateHz = target.sampleRate == 0 ? s.source().sampleRate : std::uint32_t (target.sampleRate);
    ready.deliveryBits = std::uint8_t (target.bitDepth);
    ready.params.limiter.ceilingDbTp -= 0.15;
    return detail::MasterJob::fingerprint (ready);
}

// A master the session decides, against the previous path given this suite's composition of the same project: two
// sessions brought to the same state by `edit`, one asked for a version-0 master, the other for a version-1 master with
// the composed chain.
struct Pair { Done decided, ready; std::uint64_t recipeHash = 0, composedHash = 0; bool taken = false; };
template <class Edit> Pair bothPaths (const Mix& mix, const char* target, Edit&& edit)
{
    Pair out;
    auto a = measured (mix, target), b = measured (mix, target);
    edit (*a); edit (*b);
    drive (*a); drive (*b);
    const auto snapshot = b->snapshot();
    command::Master previous { 90 };
    previous.ready = composed (*b, snapshot, 1);
    out.composedHash = fingerprintOf (*a, a->snapshot());
    const auto first = a->apply (command::Master { 90 });
    out.recipeHash = a->jobRecipe().readyHash;
    const auto second = b->apply (previous);
    out.taken = first.rejection == Rejection::None && second.rejection == Rejection::None;
    out.decided = finish (*a);
    out.ready = finish (*b);
    return out;
}
std::string landed (const Done& d)
{
    if (! d.made || ! d.kept.report || ! d.kept.report->achievedLufs || ! d.kept.report->truePeakDbTp) return "no master";
    return std::to_string (*d.kept.report->achievedLufs) + " LUFS, " + std::to_string (*d.kept.report->truePeakDbTp) + " dBTP, "
         + std::to_string (d.shape.sampleRate) + " Hz";
}
bool sameReports (const Done& a, const Done& b)
{
    const auto& x = a.kept.report; const auto& y = b.kept.report;
    return x && y && x->achievedLufs && y->achievedLufs && sameBits (*x->achievedLufs, *y->achievedLufs)
        && x->truePeakDbTp && y->truePeakDbTp && sameBits (*x->truePeakDbTp, *y->truePeakDbTp)
        && a.kept.landing && b.kept.landing && a.kept.landing->passes == b.kept.landing->passes
        && x->deliverable == y->deliverable && x->targetMet == y->targetMet;
}

void theRenderIsThePreviousPaths()
{
    felitronics::test::group ("the render: a master the session decides is, sample for sample, the previous path's master of the chain composed here");
    const Mix mix;
    const auto nothing = [] (Session&) {};
    {
        {
            const auto probe = measured (mix, "allStreaming");
            const auto seen = probe->snapshot();
            std::printf ("    the mix: %.2f LUFS, %.2f dBTP; needles p90 %.2f ms, bass share %.3f, %g runs; class %u (why %u), need %.2f dB\n",
                numberOf (seen, Analyzer::Loudness, "integratedLufs").value_or (0.0), numberOf (seen, Analyzer::Loudness, "truePeakDb").value_or (0.0),
                numberOf (seen, Analyzer::Excursions, "p90Ms").value_or (-1.0), numberOf (seen, Analyzer::Excursions, "bassDoseShare").value_or (-1.0),
                numberOf (seen, Analyzer::Excursions, "runCount").value_or (-1.0), unsigned (seen.view().plan.limiter.proposed),
                unsigned (seen.view().plan.limiter.why), seen.view().plan.limiter.needDb.value_or (0.0));
        }
        const auto pair = bothPaths (mix, "allStreaming", nothing);
        ok (pair.taken && pair.decided.made && pair.ready.made, "PRECONDITION: allStreaming — both masters are made");
        ok (sameAudio (pair.decided, pair.ready) && sameReports (pair.decided, pair.ready),
            "allStreaming, the machine's plan: every sample's bits and the landing agree — " + landed (pair.decided));
        ok (pair.recipeHash == pair.composedHash && pair.decided.kept.recipe.readyHash == pair.recipeHash && pair.decided.kept.recipe.readyVersion == 0
            && pair.decided.kept.recipe.deliveryRateHz == 48000,
            "and its recipe records that chain's fingerprint, as version 0");
        ok (pair.decided.kept.report && pair.decided.kept.report->medium && ! pair.decided.kept.report->medium->vinyl
            && ! pair.ready.kept.report->medium, "its report says whose chain it was: the session's carries the medium, a shell's ready chain none");
    }
    {
        // cd: 44.1 kHz and 16 bits with dither, the machine's glue on the measured tempo, the pass at the source's rate.
        const auto pair = bothPaths (mix, "cd", nothing);
        ok (pair.taken && pair.decided.made && pair.ready.made && pair.decided.shape.sampleRate == 44100, "PRECONDITION: cd — both masters are made, at 44.1 kHz");
        ok (sameAudio (pair.decided, pair.ready) && sameReports (pair.decided, pair.ready) && pair.recipeHash == pair.composedHash,
            "cd, delivered at another rate with the glue and the dither: the same bits — " + landed (pair.decided));
        bool grid = true;
        for (const float x : pair.decided.audio) grid = grid && same (double (x) * 32768.0, std::nearbyint (double (x) * 32768.0));
        ok (grid && pair.decided.said (text::FactId::MasterGlue), "every sample of it on the 16-bit grid, and the glue's line published");
    }
    {
        const auto pair = bothPaths (mix, "lp", nothing);
        ok (pair.taken && pair.decided.made && pair.ready.made, "PRECONDITION: lp — both masters are made");
        ok (sameAudio (pair.decided, pair.ready) && sameReports (pair.decided, pair.ready) && pair.recipeHash == pair.composedHash,
            "lp, with its low shelf, 150 Hz mono bass and no needles: the same bits — " + landed (pair.decided));
    }
    {
        // A person's hand over the machine, every kind of it, with the panel hidden.
        const auto hands = [] (Session& s)
        {
            TiltFields<Touched> tilt; tilt.db = 2.0;                        // a knob turned, no tick written
            SaturationFields<Touched> sat; sat.on = true; sat.drive = 6.0;
            MonoBassFields<Touched> mono; mono.on = false;                  // a tick switched off
            HpfFields<Touched> hpf; hpf.fq = 45.0; hpf.slope = 48;
            LimiterFields<Touched> limiter; limiter.needles = Needles::Manual; limiter.needlesDb = 2.0;
            GlueFields<Touched> glue; glue.on = true; glue.upToDb = 1.5;
            command::EditTarget target { 40, {} }; target.fields.lufs = -11.0; target.fields.tp = -0.8;
            bool taken = s.apply (command::SetManual { 30, true }).rejection == Rejection::None;
            for (const DeviceEdit& edit : { DeviceEdit (tilt), DeviceEdit (sat), DeviceEdit (mono), DeviceEdit (hpf), DeviceEdit (limiter), DeviceEdit (glue) })
                taken = taken && s.apply (command::EditDevice { 31, edit }).rejection == Rejection::None;
            taken = taken && s.apply (target).rejection == Rejection::None && s.apply (command::SetManual { 41, false }).rejection == Rejection::None;
            ok (taken, "PRECONDITION: a person's edits are taken, and the panel hidden again");
        };
        const auto pair = bothPaths (mix, "allStreaming", hands);
        ok (pair.taken && pair.decided.made && pair.ready.made, "PRECONDITION: by hand — both masters are made");
        ok (sameAudio (pair.decided, pair.ready) && sameReports (pair.decided, pair.ready) && pair.recipeHash == pair.composedHash,
            "a person's edits — a tilt by its knob alone, a saturation, mono bass off, another high-pass, a manual threshold, a glue, the target's numbers — "
            "all sound with the panel hidden: the same bits — " + landed (pair.decided));
        ok (pair.decided.said (text::FactId::MasterSaturation) && pair.decided.said (text::FactId::MasterGlue),
            "and the stages a person brought in report what they did");
    }
}

// A RENDERED MASTER'S CLIPPER TAKES ITS AMOUNT, WHATEVER GAIN THE LANDING SETTLES ON: on the mix, the machine's short
// class (3 dB) and a manual 1 dB at the streaming target (a need of about 4.5 dB), and a manual 2 dB at -9 LUFS / -1 dBTP,
// where the limiter takes dB off the loudness and the landing adds gain well past the plain one; each mastered to the
// end. The peak clipper's own reduction trace, over the whole delivery, stays within the amount — the threshold is
// worked out from the peak the landing measured at the limiter's input, for the gain it settles on. Before, the
// threshold stood where the input's forecast put it at the plain gain, and every dB the landing added on top went into
// the clipper too: the 2 dB took 5.7 at -9 LUFS (as the page saw); riding the forecast alone still took 2.36.
void theClipperCutsItsAmount()
{
    felitronics::test::group ("a rendered master: the peak clipper takes no more than its amount off the peaks, at any landing gain");
    const Mix mix;
    // The threshold is worked out from the peak the first pass measured at the limiter's input, and every later render
    // scales that input by its own gain in float: what is left is float rounding of the gain node and the oversampler's
    // sums, about 1e-5 dB. A thousandth is two orders above it and two below the tenth the card prints.
    const double tolerance = 0.001;
    struct Case { double manualCut; std::optional<double> lufs, tp; };
    for (const Case& c : { Case { 0.0, {}, {} }, Case { 1.0, {}, {} }, Case { 2.0, -9.0, -1.0 } })
    {
        auto s = measured (mix, "allStreaming");
        double amount = 3.0;
        if (c.manualCut > 0.0 || c.lufs)
        {
            bool taken = s->apply (command::SetManual { 30, true }).rejection == Rejection::None;
            if (c.manualCut > 0.0)
            {
                LimiterFields<Touched> limiter; limiter.needles = Needles::Manual; limiter.needlesDb = c.manualCut;
                taken = taken && s->apply (command::EditDevice { 31, DeviceEdit (limiter) }).rejection == Rejection::None;
                amount = c.manualCut;
            }
            if (c.lufs)
            {
                command::EditTarget target { 32, {} }; target.fields.lufs = *c.lufs; target.fields.tp = *c.tp;
                taken = taken && s->apply (target).rejection == Rejection::None;
            }
            ok (taken && s->apply (command::SetManual { 33, false }).rejection == Rejection::None, "PRECONDITION: the edits are taken");
        }
        const auto plan = s->snapshot().view().plan.limiter;
        const double need = plan.needDb.value_or (0.0);
        ok (plan.cutting && same (plan.overDb, amount) && need > amount + 1.0,
            "PRECONDITION: the clipper cuts " + std::to_string (amount) + " dB on a need of " + std::to_string (need) + " dB");
        ok (s->apply (command::Master { 90 }).rejection == Rejection::None, "PRECONDITION: the master starts");
        const auto done = finish (*s);
        const auto& trace = done.kept.landing ? done.kept.landing->peakClipTrace : std::optional<LandingTrace> {};
        double deepest = 0.0;
        if (trace) for (const auto& row : trace->rows) deepest = std::max (deepest, row.maxDb);
        // How far the landing's drive moved past the plain one: the gain it added, and the pass ceiling it lowered under
        // the target's. Where it moved a dB or more, a threshold that stood still would have cut that much more.
        const auto& log = done.kept.landing ? done.kept.landing->log : std::span<const LandingPass> {};
        const double drift = log.empty() ? 0.0 : (log.back().gainDb - log.front().gainDb) + (plan.ceilingDbTp - log.back().ceilingDbTp);
        if (c.lufs) ok (drift > 1.0, "PRECONDITION: at " + std::to_string (*c.lufs) + " LUFS the landing drives "
                        + std::to_string (drift) + " dB past the plain gain");
        ok (done.made && trace && trace->valid && ! trace->rows.empty() && ! log.empty() && deepest > 0.0 && deepest <= amount + tolerance,
            std::string (c.manualCut > 0.0 ? "a manual " : "the machine's short class, ") + std::to_string (amount) + " dB: the clipper took at most "
            + std::to_string (deepest) + " dB off the peaks, the landing's drift of " + std::to_string (drift) + " dB notwithstanding, in "
            + std::to_string (log.size()) + " passes — " + landed (done));
    }
}

// The chain of the project as it stands, by its recipe's fingerprint: a master is asked for and cancelled at once.
std::uint64_t recipeOf (Session& s, CommandId id)
{
    const auto asked = s.apply (command::Master { id });
    const auto hash = asked.rejection == Rejection::None ? s.jobRecipe().readyHash : 0u;
    if (asked.rejection == Rejection::None) (void) s.apply (command::Cancel { id + 1, asked.job });
    return hash;
}
template <class Fields> bool edit (Session& s, const Fields& fields, CommandId id = 50)
{
    return s.apply (command::EditDevice { id, DeviceEdit (fields) }).rejection == Rejection::None;
}
command::MasterReady chainOf (const Session& s)
{
    const auto snapshot = s.snapshot();
    auto in = inputsOf (s, snapshot);
    in.needlesCurrent = true;   // the fixture's needles are measured at the project's ceiling
    command::MasterReady ready;
    detail::writeChain (in, s.project().devices, ready);
    return ready;
}

void theTopologyFollowsTheTicks()
{
    felitronics::test::group ("the chain's topology follows the devices as they sound — the tick rule — never [stages]");
    const Mix mix;
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    ok (s.apply (command::SetManual { 3, true }).rejection == Rejection::None, "PRECONDITION: the panel open");
    const auto rules = detail::rules();
    ok (! rules.monoBass && ! rules.clipper && rules.compressor, "PRECONDITION: [stages] says no mono bass, no clipper, a compressor");
    {
        const auto t = chainOf (s).topology;
        ok (t.eq && t.monoBass && ! t.compressor && ! t.clipper && t.limiter && ! t.dither,
            "the machine's plan on allStreaming: the EQ stage (the high-pass), mono bass, the limiter — mono bass in whatever "
            "[stages] says, no compressor though [stages] has one");
        ok (recipeOf (s, 100) == fingerprintOf (s, s.snapshot()), "and that is the chain a master of it records");
    }
    // Each device's tick, by hand, against the machine's — and the knob alone.
    {
        HpfFields<Touched> hpf; hpf.on = false;
        ok (edit (s, hpf) && ! chainOf (s).topology.eq && recipeOf (s, 110) == fingerprintOf (s, s.snapshot()),
            "the high-pass switched off by hand, with no other EQ device: no EQ stage");
        TiltFields<Touched> tilt; tilt.db = 3.0;
        ok (edit (s, tilt) && chainOf (s).topology.eq && chainOf (s).params.eqBands[1].on && ! chainOf (s).params.eqBands[0].on
            && s.snapshot().view().plan.devices.tilt.tick == TickFrom::Touched && recipeOf (s, 120) == fingerprintOf (s, s.snapshot()),
            "a tilt turned by its knob, with no tick written, sounds: the EQ stage is back, for its band alone");
        TiltFields<Touched> flat; flat.on = true; flat.db = 0.0;
        ok (edit (s, flat) && ! chainOf (s).topology.eq, "ticked at 0 dB it writes nothing: a shelf that does nothing is no stage");
        TiltFields<Mark> both; both.on = true; both.db = true;
        HpfFields<Mark> tick; tick.on = true;
        ok (s.apply (command::RevertEdits { 60, DeviceMask (both) }).rejection == Rejection::None
            && s.apply (command::RevertEdits { 61, DeviceMask (tick) }).rejection == Rejection::None && chainOf (s).topology.eq,
            "PRECONDITION: reverted, the high-pass is back");
    }
    {
        MonoBassFields<Touched> mono; mono.on = false;
        ok (edit (s, mono) && ! chainOf (s).topology.monoBass && recipeOf (s, 130) == fingerprintOf (s, s.snapshot()),
            "mono bass switched off by hand: out of the chain");
        MonoBassFields<Touched> wide; wide.on = true; wide.fq = 90.0; wide.width = 0.5;
        const bool taken = edit (s, wide);
        const auto ready = chainOf (s);
        ok (taken && ready.topology.monoBass && same (double (ready.params.monoBass.frequencyHz), 90.0) && same (double (ready.params.monoBass.lowWidth), 0.5),
            "back on at a person's crossover and width: written as set");
    }
    {
        GlueFields<Touched> glue; glue.on = true;
        ok (edit (s, glue) && chainOf (s).topology.compressor && ! chainOf (s).params.bypassCompressor,
            "the glue ticked by hand on a target the machine does not glue: the compressor is in, at the knob a tick gives");
        glue.upToDb = 0.0;
        ok (edit (s, glue) && ! chainOf (s).topology.compressor, "at 0 dB the compressor is out whatever its tick");
        SaturationFields<Touched> sat; sat.drive = 4.0;
        ok (edit (s, sat) && chainOf (s).topology.clipper && recipeOf (s, 140) == fingerprintOf (s, s.snapshot()),
            "a saturation drive turned, no tick written: the clipper stage is in");
        LowFields<Touched> low; low.on = true; low.db = -1.0;
        ok (edit (s, low) && chainOf (s).params.eqBands[2].on && same (chainOf (s).params.eqBands[2].lanes[0].gainDb, -1.0),
            "the low shelf by hand on a target the machine leaves it off: its band written");
    }
    // A mono source has no side to fold; hiding the panel changes nothing of what sounds.
    {
        const auto before = recipeOf (s, 150);
        ok (s.apply (command::SetManual { 160, false }).rejection == Rejection::None && recipeOf (s, 161) == before && before != 0,
            "with the panel hidden every edit still sounds: the same chain");
        std::vector<float> one (mix.left);
        const float* plane[1] { one.data() };
        auto m = Session::create().session;
        ok (m->apply (command::Load { 1, { plane, 1, mix.frames, Mix::rate }, { "mono.wav", Mix::rate, true, 24 } }).rejection == Rejection::None,
            "PRECONDITION: a mono source loads");
        drive (*m);
        ok (! chainOf (*m).topology.monoBass && ! m->snapshot().view().plan.devices.monoBass.on && recipeOf (*m, 170) == fingerprintOf (*m, m->snapshot()),
            "a mono source: mono bass is out of the chain, and the plan says so");
    }
}

void aMasterThatWaited()
{
    felitronics::test::group ("a master taken before its measurements ended is the master asked for after them");
    const Mix mix;
    for (const char* target : { "cd", "club" })
    {
        // Asked for while what its devices read still runs: cd's glue reads the tempo, which the second measurement has
        // not reached when the first ends; club's clipper reads the needles at club's own ceiling, which a change of
        // target has only just asked for.
        const bool needles = std::string_view (target) == "club";
        auto early = Session::create().session;
        (void) early->apply (command::SetTarget { 1, needles ? "allStreaming" : target });
        ok (early->apply (mix.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads");
        for (unsigned i = 0; i < 4000000 && early->state() == State::Loaded; ++i) (void) early->step (1);
        if (needles) ok (early->apply (command::SetTarget { 9, target }).rejection == Rejection::None, "PRECONDITION: the target changes to club");
        const auto waitingFor = early->snapshot().view().plan.waiting;
        ok ((waitingFor & detail::bitOf (needles ? Analyzer::Excursions : Analyzer::Tempo)) != 0,
            std::string (target) + ": PRECONDITION: the plan waits for " + (needles ? "the needles at its ceiling" : "the tempo"));
        const auto declared = early->check (command::Master { 3 });
        Answer asked;
        const auto atOnce = declared::spend ([&] { asked = early->apply (command::Master { 3 }); });
        ok (early->state() == State::Measured1 && waitingFor != 0 && asked.rejection == Rejection::None
            && early->snapshot().view().masterProgress.name == PhaseName::Analyzers && early->jobRecipe().readyHash == 0,
            std::string (target) + ": taken with the panel hidden while its devices' measurements run — it waits, its chain not fixed yet");
        // Its memory: declared by the command, asked for when the wait ends, never past the declaration.
        std::uint64_t largest = 0, startedAt = 0;
        std::vector<text::Fact> facts;
        for (unsigned i = 0; i < 4000000 && early->job() != 0; ++i)
        {
            const bool wasWaiting = early->jobRecipe().readyHash == 0;
            const auto work = declared::spend ([&] { (void) early->step (1); });
            if (wasWaiting && early->job() != 0 && early->jobRecipe().readyHash != 0) startedAt = std::uint64_t (work.bytes);
            else if (! wasWaiting) largest = std::max (largest, std::uint64_t (work.bytes));
            for (const auto& e : early->events()) if (e.kind == EventKind::Fact) facts.push_back (e.payload.fact.view());
        }
        ok (declared.rejection == Rejection::None && declared::covers (declared.bytes, atOnce) && startedAt > 0
            && std::uint64_t (atOnce.bytes) + startedAt <= declared.bytes && largest <= declared.bytes,
            "  its memory is declared before the work: " + std::to_string (declared.bytes) + " B declared, " + std::to_string (atOnce.bytes)
            + " asked by the command and " + std::to_string (startedAt) + " when the wait ended");
        Done waited;
        waited.facts = facts;
        ok (early->masters().size() == 1, "  PRECONDITION: the waiting master is made");
        if (early->masters().empty()) continue;
        waited.kept = early->masters().back();
        const auto token = early->pendingMaster();
        waited.shape = early->masterAudioShape (token);
        waited.audio.resize (std::size_t (waited.shape.frames * waited.shape.channels));
        waited.made = early->copyMaster (token, waited.audio) == MasterTransferStatus::Ok;
        // Asked for after everything ended.
        auto late = measured (mix, target);
        ok (late->apply (command::Master { 3 }).rejection == Rejection::None && late->jobRecipe().readyHash != 0, "  PRECONDITION: the late master is taken, its chain fixed at once");
        const auto after = finish (*late);
        ok (waited.made && after.made && sameAudio (waited, after) && waited.kept.recipe.readyHash == after.kept.recipe.readyHash
            && sameReports (waited, after),
            "  the same chain and the same bits, however early it was asked for — " + landed (after));
    }
}

void theMemoryOfAMaster()
{
    felitronics::test::group ("a decided master's memory is declared before the work — a large one, cancelled, then a small one; a refusal leaves nothing behind");
    const Mix large (1.0f, 12), small (1.0f, 3);
    auto sp = measured (large, "cd"); auto& s = *sp;
    const auto declaredLarge = s.check (command::Master { 3 });
    Answer asked;
    const auto spentLarge = declared::spend ([&] { asked = s.apply (command::Master { 3 }); });
    std::uint64_t largest = 0;
    for (unsigned i = 0; i < 2000 && s.job() != 0; ++i)
        largest = std::max (largest, std::uint64_t (declared::spend ([&] { (void) s.step (1); }).bytes));
    ok (declaredLarge.rejection == Rejection::None && asked.rejection == Rejection::None && declared::covers (declaredLarge.bytes, spentLarge)
        && largest <= declaredLarge.bytes && spentLarge.requests > 0,
        "a twelve-second master on cd: " + declared::describe (declaredLarge.bytes, spentLarge) + ", and no step asks for more");
    const double liveWhileRunning = s.liveBytes();
    ok (s.apply (command::Cancel { 4, asked.job }).rejection == Rejection::None && s.job() == 0 && s.liveBytes() < liveWhileRunning,
        "cancelled mid-render: the job's memory is given back");
    // A smaller source in the same session: its master declares its own, smaller, demand and stays inside it.
    ok (s.apply (small.load (5)).rejection == Rejection::None, "PRECONDITION: a three-second source replaces it");
    drive (s);
    const auto declaredSmall = s.check (command::Master { 6 });
    const auto spentSmall = declared::spend ([&] { asked = s.apply (command::Master { 6 }); });
    largest = 0;
    for (unsigned i = 0; i < 4000000 && s.job() != 0; ++i)
        largest = std::max (largest, std::uint64_t (declared::spend ([&] { (void) s.step (1); }).bytes));
    ok (declaredSmall.rejection == Rejection::None && asked.rejection == Rejection::None && declaredSmall.bytes < declaredLarge.bytes
        && declared::covers (declaredSmall.bytes, spentSmall) && largest <= declaredSmall.bytes && s.masters().size() == 1,
        "then a three-second one: " + declared::describe (declaredSmall.bytes, spentSmall) + " — smaller, covered to its end");
    // The output is pending: the next master is refused whole — nothing allocated, nothing changed — until it is taken.
    const auto before = s.revision();
    Answer refused;
    const auto spentRefused = declared::spend ([&] { refused = s.apply (command::Master { 7 }); });
    ok (refused.rejection == Rejection::OutputPending && spentRefused.requests == 0 && s.revision() == before && s.job() == 0
        && s.masters().size() == 1 && s.pendingMaster().master == s.masters()[0].id,
        "with its audio not yet taken the next master is refused whole: no allocation, no revision, the finished one intact");
    ok (s.releaseMaster (s.pendingMaster()) == MasterTransferStatus::Ok && s.check (command::Master { 8 }).rejection == Rejection::None,
        "and taken again once it is released");
    // A heap that cannot hold the job refuses it before anything is asked for. The last master's damage ends first: a
    // master asked while one runs is priced with its bytes freed (MasterJobTests' masterAtTheCeilingWithADamage).
    for (unsigned i = 0; i < 4000000 && s.damageJob() != 0; ++i) (void) s.step (16);
    const auto need = s.check (command::Master { 8 });
    ok (s.setCapacity ({ s.liveBytes() + double (need.bytes) - 1.0, 9007199254740991.0 }) == Status::Ok, "PRECONDITION: a heap one byte short");
    const auto spentShort = declared::spend ([&] { refused = s.apply (command::Master { 8 }); });
    ok (refused.rejection == Rejection::Memory && spentShort.requests == 0 && s.job() == 0 && s.masters().size() == 1,
        "one byte short of its demand: refused with Memory before the first allocation");

    // THE WAITING PATH: taken while a heap can hold it, its wait ends on a heap that cannot — the job is dropped with its
    // reason, nothing half-made stays, and the session goes on.
    auto wp = Session::create().session; auto& w = *wp;
    (void) w.apply (command::SetTarget { 1, "cd" });
    ok (w.apply (large.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads, on cd");
    for (unsigned i = 0; i < 4000000 && w.state() == State::Loaded; ++i) (void) w.step (1);
    const auto waitingNeed = w.check (command::Master { 3 });
    const auto waitingMaster = w.apply (command::Master { 3 });
    ok (waitingMaster.rejection == Rejection::None && w.jobRecipe().readyHash == 0, "PRECONDITION: a master that waits for cd's tempo");
    ok (w.setCapacity ({ w.liveBytes() + double (waitingNeed.bytes) / 4.0, 9007199254740991.0 }) == Status::Ok, "PRECONDITION: the heap shrinks while it waits");
    bool memoryError = false, doneEvent = false;
    for (unsigned i = 0; i < 4000000 && w.job() != 0; ++i)
    {
        (void) w.step (1);
        for (const auto& e : w.events())
        {
            memoryError = memoryError || (e.kind == EventKind::Error && e.payload.error.code == ErrorCode::Memory && e.jobId == waitingMaster.job
                                          && e.payload.error.needBytes > 0.0);
            doneEvent = doneEvent || e.kind == EventKind::Done;
        }
    }
    ok (memoryError && ! doneEvent && w.job() == 0 && w.masters().empty() && ! w.mastering() && w.pendingMaster().master == 0,
        "its wait ends on a heap too small: a memory error under the master's own job, no master, no audio");
    ok (w.setCapacity ({ 9007199254740991.0, 9007199254740991.0 }) == Status::Ok, "PRECONDITION: the heap is given back");
    drive (w);
    ok (w.apply (command::Master { 4 }).rejection == Rejection::None, "and with room again the next master is taken");
    const auto remade = finish (w);
    ok (remade.made && w.masters().size() == 1, "and made");
}

void vinylAndQuietMastered()
{
    felitronics::test::group ("vinyl and a quiet input, mastered: the report's lines say what holds, and only that");
    const Mix mix;
    {
        auto sp = measured (mix, "lp"); auto& s = *sp;
        const auto plan = s.snapshot();
        ok (s.apply (command::Master { 3 }).rejection == Rejection::None, "PRECONDITION: an lp master is taken");
        const auto done = finish (s);
        const auto& report = *done.kept.report;
        ok (done.made && report.medium && report.medium->vinyl && report.medium->ready && same (report.medium->crossoverHz, 150.0)
            && same (report.medium->cutoffHz, 32.0) && *report.truePeakDbTp <= -3.0 && same (report.ceilingDbTp, -3.0),
            "lp by the machine: the bass folded below 150 Hz, the high-pass from 32 Hz, the true peak at "
            + std::to_string (*report.truePeakDbTp) + " dBTP under the −3 ceiling");
        ok (done.said (text::FactId::MasterVinylReady) && done.line (text::FactId::MasterVinylReady).find ("Мастер готов к нарезке") == 0
            && done.line (text::FactId::MasterVinylReady).find ("RIAA и уровень по длине стороны — за резчиком") != std::string::npos,
            "the report: " + done.line (text::FactId::MasterVinylReady));
        ok (done.said (text::FactId::MasterVinylChecked) && done.said (text::FactId::MasterVinylUncheckable) && ! done.said (text::FactId::MasterVinylDeparts)
            && done.line (text::FactId::MasterVinylUncheckable).find ("длина стороны") != std::string::npos,
            "what the file shows apart from what it cannot: " + done.line (text::FactId::MasterVinylChecked) + " / " + done.line (text::FactId::MasterVinylUncheckable));
        ok (PlanText::vinylTop (plan.view().plan.limiter).has_value(), "and the plan carries the note about the top");
    }
    {
        // A person's hand against the medium: the ceiling raised and the needles cut. No refusal — and no "ready".
        auto sp = measured (mix, "lp"); auto& s = *sp;
        command::EditTarget raised { 3, {} }; raised.fields.tp = -1.0;
        LimiterFields<Touched> limiter; limiter.needles = Needles::Manual; limiter.needlesDb = 1.0;
        ok (s.apply (command::SetManual { 2, true }).rejection == Rejection::None && s.apply (raised).rejection == Rejection::None && edit (s, limiter),
            "PRECONDITION: a ceiling of −1 dBTP and a manual threshold, by hand, on lp — both taken");
        drive (s);
        const auto plan = s.snapshot();
        const auto& finding = plan.view().plan.limiter;
        ok (finding.ceilingAboveMedium && finding.needlesAgainstMedium && finding.againstMachine && PlanText::vinylCeiling (finding)
            && PlanText::vinylNeedles (finding) && PlanText::needlesAgainstMachine (finding),
            "the plan warns of both beside the knobs");
        ok (s.apply (command::Master { 5 }).rejection == Rejection::None, "and the master is taken all the same");
        const auto done = finish (s);
        ok (done.made && done.kept.report->medium && ! done.kept.report->medium->ready && done.said (text::FactId::MasterVinylDeparts)
            && ! done.said (text::FactId::MasterVinylReady) && ! done.said (text::FactId::MasterVinylChecked)
            && done.said (text::FactId::MasterVinylUncheckable) && *done.kept.report->truePeakDbTp > -3.0,
            "made, above −3 dBTP as asked — and the report does not call it ready: " + done.line (text::FactId::MasterVinylDeparts));
        ok (done.said (text::FactId::MasterVinylCeilingDeparts) && done.said (text::FactId::MasterVinylNeedlesDeparts)
            && ! done.said (text::FactId::MasterVinylNoFold) && ! done.said (text::FactId::MasterVinylFoldDeparts)
            && ! done.said (text::FactId::MasterVinylNoHighPass) && ! done.said (text::FactId::MasterVinylHighPassDeparts),
            "and says what departs, with its numbers: " + done.line (text::FactId::MasterVinylCeilingDeparts) + " / "
            + done.line (text::FactId::MasterVinylNeedlesDeparts));
        // Each rule of the medium alone, on the plan of the master the project would get: the fold at the medium's
        // crossover or above at the machine's width, the high-pass from the floor at its slope or steeper, the ceiling
        // no higher than the medium's, no needles.
        auto vp = measured (mix, "lp"); auto& v = *vp;
        const auto readyWith = [&] (auto&& change)
        {
            Project project = v.project();
            change (project);
            const auto planned = detail::MasterJob::plan (v, command::Master {}, project);
            return planned.rejection == Rejection::None && planned.medium && planned.medium->vinyl && planned.medium->ready;
        };
        const auto asIs = [] (Project&) {};
        ok (readyWith (asIs), "the machine's plan on lp: ready for cutting");
        ok (! readyWith ([] (Project& p) { p.targetEdit.tp = -2.9; }) && readyWith ([] (Project& p) { p.targetEdit.tp = -3.0; })
            && readyWith ([] (Project& p) { p.targetEdit.tp = -4.0; }),
            "a ceiling of −2.9 dBTP alone takes it away; −3 and −4 keep it");
        ok (! readyWith ([] (Project& p) { p.devices.limiter.hand.needles = Needles::Manual; }),
            "needles cut by hand alone take it away");
        ok (! readyWith ([] (Project& p) { p.devices.monoBass.hand.on = false; }) && ! readyWith ([] (Project& p) { p.devices.monoBass.hand.fq = 120.0; })
            && ! readyWith ([] (Project& p) { p.devices.monoBass.hand.width = 0.5; }) && readyWith ([] (Project& p) { p.devices.monoBass.hand.fq = 180.0; }),
            "mono bass off, at 120 Hz or half wide takes it away; folded from 180 Hz keeps it");
        ok (! readyWith ([] (Project& p) { p.devices.hpf.hand.on = false; }) && ! readyWith ([] (Project& p) { p.devices.hpf.hand.fq = 25.0; })
            && ! readyWith ([] (Project& p) { p.devices.hpf.hand.slope = 6; }) && readyWith ([] (Project& p) { p.devices.hpf.hand.slope = 24; })
            && readyWith ([] (Project& p) { p.devices.hpf.hand.fq = 40.0; }),
            "the high-pass off, from 25 Hz or at 6 dB/oct takes it away; steeper or higher keeps it");
        // What departs, each rule alone: its own line, with the chain's number and the rule's — and none where ready.
        const auto departures = [&] (auto&& change)
        {
            Project project = v.project();
            change (project);
            const auto planned = detail::MasterJob::plan (v, command::Master {}, project);
            MasterReport report;
            report.medium = planned.medium;
            report.deliverable = planned.rejection == Rejection::None;
            return MasterReportText::vinylDepartures (report);
        };
        // The one line said, its id and its numbers (a count as its integer).
        const auto only = [] (const std::array<std::optional<text::Fact>, 4>& said, std::size_t at, text::FactId id, std::vector<double> numbers)
        {
            bool fine = said[at] && said[at]->id == id && said[at]->argCount == numbers.size() && whole (*said[at]);
            for (std::size_t i = 0; i < said.size(); ++i) fine = fine && (i == at) == said[i].has_value();
            for (std::size_t i = 0; fine && i < numbers.size(); ++i)
            {
                const auto& a = said[at]->args[i];
                fine = a.kind == text::ArgKind::Count ? double (a.integer) == numbers[i] : same (a.number, numbers[i]);
            }
            return fine;
        };
        const auto lineOf = [] (const std::array<std::optional<text::Fact>, 4>& said)
        {
            std::string all;
            for (const auto& f : said) if (f) all += (all.empty() ? "" : " / ") + ru (*f);
            return all;
        };
        const auto none = departures (asIs);
        ok (! none[0] && ! none[1] && ! none[2] && ! none[3], "ready: no rule departs, no line");
        const auto ceiling = departures ([] (Project& p) { p.targetEdit.tp = -2.9; });
        ok (only (ceiling, 2, text::FactId::MasterVinylCeilingDeparts, { -2.9, -3.0 }), "the ceiling alone: " + lineOf (ceiling));
        const auto needles = departures ([] (Project& p) { p.devices.limiter.hand.needles = Needles::Manual; p.devices.limiter.hand.needlesDb = 2.0; });
        ok (only (needles, 3, text::FactId::MasterVinylNeedlesDeparts, { 2.0 }), "the needles alone: " + lineOf (needles));
        ok (needles[3] && needles[3]->args[0].bound == text::Bound::Exact && ru (*needles[3]).find ("не больше ") != std::string::npos
            && ru (*needles[3]).find ("\xE2\x89\xA4") == std::string::npos,
            "and the clipper's setting is named as a cap, in words: " + lineOf (needles));
        const auto noFold = departures ([] (Project& p) { p.devices.monoBass.hand.on = false; });
        const auto lowFold = departures ([] (Project& p) { p.devices.monoBass.hand.fq = 120.0; });
        const auto wideFold = departures ([] (Project& p) { p.devices.monoBass.hand.width = 0.5; });
        ok (only (noFold, 0, text::FactId::MasterVinylNoFold, { 150.0 })
            && only (lowFold, 0, text::FactId::MasterVinylFoldDeparts, { 120.0, 0.0, 150.0, 0.0 })
            && only (wideFold, 0, text::FactId::MasterVinylFoldDeparts, { 150.0, 50.0, 150.0, 0.0 }),
            "the fold alone — off, too low, too wide: " + lineOf (noFold) + " / " + lineOf (lowFold) + " / " + lineOf (wideFold));
        const auto noCut = departures ([] (Project& p) { p.devices.hpf.hand.on = false; });
        const auto lowCut = departures ([] (Project& p) { p.devices.hpf.hand.fq = 25.0; });
        const auto softCut = departures ([] (Project& p) { p.devices.hpf.hand.slope = 6; });
        ok (only (noCut, 1, text::FactId::MasterVinylNoHighPass, { 32.0, 12.0 })
            && only (lowCut, 1, text::FactId::MasterVinylHighPassDeparts, { 25.0, 12.0, 32.0, 12.0 })
            && only (softCut, 1, text::FactId::MasterVinylHighPassDeparts, { 32.0, 6.0, 32.0, 12.0 }),
            "the high-pass alone — off, too low, too gentle: " + lineOf (noCut) + " / " + lineOf (lowCut) + " / " + lineOf (softCut));
        // The same hand arriving by a project file.
        const auto saved = s.exportProject();
        auto ip = measured (mix, "allStreaming"); auto& imported = *ip;
        ok (imported.importProject (9, saved.view()).rejection == Rejection::None, "PRECONDITION: the project is opened in another session");
        drive (imported);
        const auto opened = imported.snapshot();
        ok (opened.view().plan.limiter.ceilingAboveMedium && opened.view().plan.limiter.needlesAgainstMedium && opened.view().plan.limiter.cutting
            && recipeOf (imported, 10) == fingerprintOf (imported, imported.snapshot()),
            "opened from its file the hand sounds the same, with the same warnings");
    }
    {
        // A very quiet copy of the mix, 38 dB down: about −62 LUFS.
        const Mix quiet (0.0125f);
        auto sp = measured (quiet, "cd"); auto& s = *sp;
        const auto plan = s.snapshot();
        const auto lufs = numberOf (plan, Analyzer::Loudness, "integratedLufs").value_or (0.0);
        const auto& devices = plan.view().plan.devices;
        ok (lufs < -55.0 && devices.hpf.on && ! devices.monoBass.on && ! devices.glue.on && devices.dither.on && ! devices.low.on
            && plan.view().plan.limiter.why == NeedlesWhy::Quiet && plan.view().plan.hpf.cut == HpfCut::Quiet,
            "a copy at " + std::to_string (lufs) + " LUFS on cd: the high-pass at its floor and the dither stand, nothing else");
        ok (s.apply (command::Master { 3 }).rejection == Rejection::None, "PRECONDITION: its master is taken");
        const auto done = finish (s);
        ok (done.made && done.kept.report->medium && done.kept.report->medium->quietInput && done.said (text::FactId::MasterQuietInput)
            && ! done.said (text::FactId::MasterGlue) && *done.kept.report->gainFromSourceDb > 40.0,
            "mastered by gain and ceiling alone — " + done.line (text::FactId::MasterQuietInput));
        ok (recipeOf (s, 10) == fingerprintOf (s, s.snapshot()), "its chain the one composed from the rules: no clipper, no glue, no mono bass");
    }
    {
        const Mix mix16;
        auto sp = measured (mix16, "allStreaming"); auto& s = *sp;
        ok (s.apply (command::Master { 3 }).rejection == Rejection::None, "PRECONDITION: an ordinary master is taken");
        const auto done = finish (s);
        ok (done.made && ! done.said (text::FactId::MasterVinylReady) && ! done.said (text::FactId::MasterVinylDeparts)
            && ! done.said (text::FactId::MasterVinylUncheckable) && ! done.said (text::FactId::MasterQuietInput),
            "an ordinary input on another target says none of it");
    }
}

void theDitherSounding()
{
    felitronics::test::group ("the dither sounding: the same bytes every time, a person's off rounds without noise, the file adds no second noise");
    const Mix mix (1.0f, 4);
    const auto wav = [] (Session& s)
    {
        const auto token = s.pendingMaster();
        const auto plan = s.masterWavPlan (token);
        std::vector<std::uint8_t> bytes (std::size_t (plan.bytes));
        bool copied = bool (plan);
        // In the slices a shell takes it in: 64 KiB at most.
        for (std::size_t at = 0; copied && at < bytes.size(); at += 65536u)
            copied = s.copyMasterWav (token, at, { bytes.data() + at, std::min<std::size_t> (65536u, bytes.size() - at) }) == MasterTransferStatus::Ok;
        return copied ? bytes : std::vector<std::uint8_t> {};
    };
    const auto master = [&] (bool ditherOff, std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& fileAgain)
    {
        auto sp = measured (mix, "cd"); auto& s = *sp;
        if (ditherOff)
        {
            DitherFields<Touched> off; off.on = false;
            ok (s.apply (command::SetManual { 3, true }).rejection == Rejection::None && edit (s, off), "PRECONDITION: the dither switched off by hand");
        }
        ok (s.apply (command::Master { 5 }).rejection == Rejection::None, "PRECONDITION: a cd master is taken");
        Done out;
        for (unsigned i = 0; i < 4000000 && s.job() != 0; ++i) (void) s.step (16);
        if (s.masters().empty()) return out;
        out.kept = s.masters().back();
        const auto token = s.pendingMaster();
        out.shape = s.masterAudioShape (token);
        out.audio.resize (std::size_t (out.shape.frames * out.shape.channels));
        out.made = s.copyMaster (token, out.audio) == MasterTransferStatus::Ok;
        file = wav (s);
        fileAgain = wav (s);
        return out;
    };
    std::vector<std::uint8_t> first, firstAgain, second, secondAgain, plain, plainAgain;
    const auto a = master (false, first, firstAgain), b = master (false, second, secondAgain), rounded = master (true, plain, plainAgain);
    ok (a.made && b.made && sameAudio (a, b) && ! first.empty() && first == second,
        "two cd masters of one project: the same samples and the same " + std::to_string (first.size()) + " WAV bytes — the seed is fixed");
    ok (first == firstAgain && plain == plainAgain, "the file written twice from one master is the same bytes: writing it adds no noise");
    const auto onGrid = [] (const Done& d)
    {
        bool grid = true;
        for (const float x : d.audio) grid = grid && same (double (x) * 32768.0, std::nearbyint (double (x) * 32768.0));
        return grid;
    };
    // The file holds exactly the master's samples as 16-bit words: quantised once, in the chain.
    bool words = first.size() >= 44 + a.audio.size() * 2;
    for (std::size_t frame = 0; words && frame < std::size_t (a.shape.frames); ++frame)
        for (std::size_t c = 0; c < 2; ++c)
        {
            const std::size_t at = first.size() - a.audio.size() * 2 + (frame * 2 + c) * 2;
            const auto word = std::int16_t (std::uint16_t (first[at]) | std::uint16_t (first[at + 1]) << 8);
            words = double (word) == double (a.audio[c * std::size_t (a.shape.frames) + frame]) * 32768.0;
        }
    ok (onGrid (a) && words, "every sample sits on the 16-bit grid, and the file's words are those samples: no second quantisation");
    ok (rounded.made && onGrid (rounded) && ! sameAudio (a, rounded) && rounded.kept.recipe.readyHash != a.kept.recipe.readyHash,
        "with the dither off by hand the delivery is still 16 bits, rounded without noise — another master");
    {
        // 24 bits: no dither stage; the samples on the 24-bit grid.
        auto sp = measured (mix, "allStreaming"); auto& s = *sp;
        ok (s.apply (command::Master { 5 }).rejection == Rejection::None, "PRECONDITION: a 24-bit master is taken");
        const auto done = finish (s);
        bool grid = done.made;
        for (const float x : done.audio) grid = grid && same (double (x) * 8388608.0, std::nearbyint (double (x) * 8388608.0));
        ok (grid && ! chainOf (s).topology.dither, "a 24-bit delivery: no dither stage, every sample on the 24-bit grid");
    }
}
//==============================================================================
// THE OBSERVATIONS

struct Seen
{
    MeasurementResult results[kAnalyzers] {};
    MeasurementValue loudness[2], clipping[4], forensics[8], programme[6], stereo[2], lowEnd[6], infra[2], bursts[10], hum[8];
    MeasurementArray clipArray {}, events {}, candidates {};
    std::vector<double> clipRows, eventRows, candidateRows;
    detail::ObservationInputs in;
    Seen()
    {
        for (std::size_t i = 0; i < kAnalyzers; ++i)
        { results[i].analyzer = Analyzer (i); results[i].status = MeasurementStatus::Pending; results[i].reason = MeasurementReason::Pending; }
        in.rules = detail::rules(); in.measurements = results; in.channels = 2; in.sampleRate = 48000; in.frames = 48000ull * 60ull;
        in.bitDepth = 24;
    }
    Seen (const Seen&) = delete;
    void ready (Analyzer a, std::span<const MeasurementValue> numbers, std::span<const MeasurementArray> arrays = {})
    {
        auto& r = results[std::size_t (a)];
        r.status = MeasurementStatus::Ready; r.reason = MeasurementReason::None; r.numbers = numbers; r.arrays = arrays;
    }
    void ended (Analyzer a, MeasurementReason why)
    {
        auto& r = results[std::size_t (a)];
        r.status = MeasurementStatus::Unavailable; r.reason = why; r.numbers = {}; r.arrays = {};
    }
    Observations seen() const { Observations o; detail::observe (in, o); return o; }
};

void theSourceReport()
{
    const auto report = [] (double lufs, double peak, double runs, double samples)
    {
        Seen x;
        x.loudness[0] = { "integratedLufs", lufs, MeasurementReason::None, 0 };
        x.loudness[1] = { "truePeakDb", peak, MeasurementReason::None, 0 };
        x.clipping[0] = { "runCount", runs, MeasurementReason::None, 0 };
        x.clipping[1] = { "samplePeak", samples, MeasurementReason::None, 0 };
        x.ready (Analyzer::Loudness, x.loudness);
        x.ready (Analyzer::Clipping, { x.clipping, 2 });
        return detail::sourceReport (x.in);
    };
    const auto mastered = report (-9, -.5, 0, .9);
    ok (mastered.alreadyMastered.value_or (false) && mastered.facts.count == 2
        && mastered.facts.items[0].id == text::FactId::SourceNoClipping
        && mastered.facts.items[1].id == text::FactId::SourceAlreadyMastered,
        "an already-mastered file says so after the clipping verdict, with loudness and true peak");
    bool boundaries = true;
    for (const auto pair : { std::pair { -14.0, -.5 }, { -13.0, -.5 }, { -12.0, -.5 }, { -9.0, -1.5 }, { -9.0, 2.0 } })
        boundaries = boundaries && report (pair.first, pair.second, 0, .9).alreadyMastered == false;
    ok (boundaries && report (-11.999, -1.499, 0, .8).alreadyMastered == true,
        "all three strict boundaries hold; loud mixes at −15…−13 LUFS are not called mastered");
    const auto clipped = report (-9, .2, 1, 1), overs = report (-9, .2, 0, .99);
    ok (clipped.clipping == SourceClipStatus::Clipped && clipped.facts.count == 2
        && clipped.facts.items[0].id == text::FactId::SourceRealClipping
        && overs.clipping == SourceClipStatus::InterSampleOvers && overs.facts.count == 2
        && overs.facts.items[0].id == text::FactId::SourceInterSampleOvers,
        "confirmed flat tops are real clipping; true-peak overs below full-scale samples are gain-fixable");
    Seen missing;
    const auto unknown = detail::sourceReport (missing.in);
    ok (! unknown.alreadyMastered && unknown.clipping == SourceClipStatus::NotMeasured && unknown.facts.count == 1,
        "missing readings are not a clean clipping verdict or a mastered verdict");
    bool words = true;
    for (const auto& row : { mastered, clipped, overs, unknown })
        for (unsigned i = 0; i < row.facts.count; ++i) words = words && whole (row.facts.items[i]);
    ok (words, "every source-report line renders complete Russian and English");

    const auto rules = detail::rules();
    const auto delivery = [&] (std::string_view target, double lufs, double peak, bool already = true)
    {
        SourceReport r; r.alreadyMastered = already; r.loudnessLufs = lufs; r.truePeakDbTp = peak;
        Project p; p.target = rules.find (target).value();
        detail::masteredDelivery (r, rules, p);
        return r;
    };
    const auto asIs = delivery ("allStreaming", -9.0, -2.5);
    const auto peaks = delivery ("spotify", -9.0, -0.5);
    const auto regular = delivery ("appleMusic", -14.0, -0.5);
    const auto spec = delivery ("ebu", -9.0, -0.5);
    const auto other = delivery ("cd", -9.0, -0.5);
    const auto louder = delivery ("maxDense", -13.0, -0.5);
    const auto ordinary = delivery ("spotify", -9.0, -0.5, false);
    ok (asIs.deliveryMode == DeliveryMode::AsIs && same (asIs.deliveryGainDb.value_or (1.0), 0.0)
        && same (asIs.deliveryCeilingDbTp.value_or (0.0), -2.0) && asIs.facts.items[0].id == text::FactId::SourceDeliveryAsIs,
        "the policy's reachable fixture passes an already-mastered source within the −2 dBTP ceiling bit-exact");
    ok (peaks.deliveryMode == DeliveryMode::PeaksOnly && same (peaks.deliveryGainDb.value_or (0.0), -1.5)
        && same (peaks.deliveryCeilingDbTp.value_or (0.0), -2.0) && other.deliveryMode == DeliveryMode::PeaksOnly
        && same (regular.deliveryCeilingDbTp.value_or (0.0), -1.0),
        "streaming and other quieter targets use only the plain peak gain and configured loud ceiling");
    ok (spec.deliveryMode == DeliveryMode::Mastered && ! spec.deliveryGainDb
        && louder.deliveryMode == DeliveryMode::Mastered && louder.facts.count == 1
        && louder.facts.items[0].id == text::FactId::SourceMasterLouder
        && ordinary.deliveryMode == DeliveryMode::Mastered,
        "specification and louder/max targets master normally, the louder one warns, and a normal mix is unaffected");
    {
        constexpr std::uint32_t hotRate = 44100;
        std::vector<float> hot (hotRate);
        for (std::size_t i = 0; i < hot.size(); ++i)
            hot[i] = float (.9 * felitronics::core::det::sin (
                6.283185307179586 * 18000.0 * double (i) / hotRate));
        const float* hotPlanes[] { hot.data() };
        auto hs = Session::create().session;
        (void) hs->apply (command::SetTarget { 1, "youtube" });
        const bool loaded = hs->apply (command::Load { 2, { hotPlanes, 1, hot.size(), hotRate },
            { "18k.wav", hotRate, true, 24 } }).rejection == Rejection::None;
        drive (*hs);
        const auto initial = detail::MasterJob::plan (*hs, command::Master { 3 }, hs->project());
        const bool started = loaded && initial.deliveryMode == DeliveryMode::PeaksOnly
            && hs->apply (command::Master { 3 }).rejection == Rejection::None;
        const Done deliveredHot = started ? finish (*hs) : Done {};
        felitronics::analysis::ReferenceTruePeakMeter meter;
        bool measured = deliveredHot.made && meter.prepare (deliveredHot.shape.sampleRate, 1024,
                                                            int (deliveredHot.shape.channels));
        for (std::uint64_t at = 0; measured && at < deliveredHot.shape.frames; at += 1024u)
        {
            const int n = int (std::min<std::uint64_t> (1024u, deliveredHot.shape.frames - at));
            const float* block[] { deliveredHot.audio.data() + at };
            measured = meter.process (block, 1, n);
        }
        if (measured) meter.drain();
        const auto* hotReport = deliveredHot.kept.report ? &*deliveredHot.kept.report : nullptr;
        ok (measured && hotReport && hotReport->deliveryMode == DeliveryMode::PeaksOnly
            && hotReport->truePeakDbTp
            && std::bit_cast<std::uint64_t> (*hotReport->truePeakDbTp)
                == std::bit_cast<std::uint64_t> (meter.truePeakDb())
            && meter.truePeakDb() <= hotReport->ceilingDbTp && hotReport->peakSafe
            && hotReport->deliveryGainDb < initial.deliveryGainDb,
            "peaks-only measures the resampled 18 kHz delivery and corrects it under the true-peak ceiling");
    }
    {
        Seen x;
        x.loudness[0] = { "integratedLufs", -9.0, MeasurementReason::None, 0 };
        x.loudness[1] = { "truePeakDb", -0.5, MeasurementReason::None, 0 };
        x.clipping[0] = { "runCount", 0.0, MeasurementReason::None, 0 };
        x.clipping[1] = { "samplePeak", 0.9, MeasurementReason::None, 0 };
        x.forensics[0] = { "wall.valid", 1.0, MeasurementReason::None, 0 };
        x.forensics[1] = { "wall.cutoffHz", 16000.0, MeasurementReason::None, 0 };
        x.forensics[2] = { "wall.dropDb", 40.0, MeasurementReason::None, 0 };
        x.forensics[3] = { "wall.cutoffFractionOfNyquist", 2.0 / 3.0, MeasurementReason::None, 0 };
        x.ready (Analyzer::Loudness, x.loudness);
        x.ready (Analyzer::Clipping, { x.clipping, 2 });
        x.ready (Analyzer::Forensics, { x.forensics, 4 });
        const auto lossy = detail::sourceReport (x.in);
        ok (lossy.facts.count == 2 && lossy.facts.items[0].id == text::FactId::SourceNoClipping
            && lossy.facts.items[1].id == text::FactId::SourceFormerLossy
            && lossy.advice.count == 1 && lossy.advice.items[0].id == text::FactId::SourceAlreadyMastered,
            "a former-lossy source asks for the lossless original immediately after the clipping verdict");
    }

    std::vector<float> pcm (48000);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = float (std::clamp (2 * felitronics::core::det::sin (.03 * double (i)), -1.0, 1.0));
    const float* planes[] { pcm.data() };
    auto made = Session::create(); auto& s = *made.session;
    (void) s.apply (command::Load { 1, { planes, 1, pcm.size(), 48000 }, {} });
    while (s.step (16).state == StepState::More) {}
    const auto snapshot = s.snapshot();
    ok (snapshot.view().sourceReport && snapshot.view().sourceReport->clipping == SourceClipStatus::Clipped,
        "the clipping analyzer's actual verdict reaches the source report");
    detail::MasterPlan asIsPlan;
    asIsPlan.deliveryMode = DeliveryMode::AsIs;
    asIsPlan.deliveryRate = s.source().sampleRate;
    asIsPlan.frames = int (s.source().frames);
    asIsPlan.ready.deliveryBits = s.source().bitDepth;
    asIsPlan.request.targetLufs = snapshot.view().sourceReport->loudnessLufs.value_or (0.0);
    asIsPlan.sourceLufs = asIsPlan.request.targetLufs;
    asIsPlan.deliveryCeilingDbTp = -2.0;
    detail::MasterJob asIsJob;
    bool copied = asIsJob.begin (s, asIsPlan);
    while (copied && asIsJob.step (1024) == mastering::StepResult::More) {}
    const auto exact = asIsJob.takeOutput();
    copied = copied && exact && asIsJob.reportResult().deliveryMode == DeliveryMode::AsIs;
    for (std::size_t i = 0; copied && i < pcm.size(); ++i)
        copied = std::bit_cast<std::uint32_t> (exact[i]) == std::bit_cast<std::uint32_t> (pcm[i]);
    ok (copied, "the reachable as-is delivery fixture copies every source float bit for bit, with no resample or dither");
    std::string json (std::size_t (Codec::encodedBytes (snapshot.view()).bytes), '\0');
    Snapshot restored;
    ok (Codec::encode (snapshot.view(), json) == CodecStatus::Ok && Codec::decode (json, restored) == CodecStatus::Ok
        && restored.view().sourceReport && restored.view().sourceReport->facts.items[0].id == text::FactId::SourceRealClipping,
        "the source report survives the snapshot codec with clipping first");
    command::Master safe { 2 };
    const auto peaksPlan = detail::MasterJob::plan (s, safe, s.project());
    auto forced = safe; forced.masterAnyway = true;
    const auto forcedPlan = detail::MasterJob::plan (s, forced, s.project());
    auto sixteen = s.project(); sixteen.target = detail::rules().find ("cdDynamic").value();
    const auto sixteenPlan = detail::MasterJob::plan (s, safe, sixteen);
    ok (snapshot.view().sourceReport->alreadyMastered.value_or (false)
        && s.check (safe).rejection == Rejection::None
        && peaksPlan.deliveryMode == DeliveryMode::PeaksOnly && ! peaksPlan.ready.topology.eq
        && ! peaksPlan.ready.topology.compressor && ! peaksPlan.ready.topology.clipper && ! peaksPlan.ready.topology.limiter
        && forcedPlan.deliveryMode == DeliveryMode::Mastered
        && sixteenPlan.deliveryMode == DeliveryMode::PeaksOnly && sixteenPlan.deliveryDithered
        && sixteenPlan.ready.topology.dither && sixteenPlan.ready.deliveryBits == 16,
        "peaks-only uses gain and format dither with no mastering chain or clipped-gain choice; masterAnyway restores the chain");
    const auto deliver = [&] (command::Master command, const detail::MasterPlan* fixture = nullptr)
    {
        const auto checked = s.check (command);
        Answer answer;
        const auto spent = declared::spend ([&] { answer = s.apply (command); });
        bool valid = answer.rejection == Rejection::None && declared::covers (checked.bytes, spent);
        if (valid && fixture) detail::Inspector::deliveryFixture (s, *fixture);
        for (unsigned i = 0; valid && s.job() != 0 && i < 100000; ++i) (void) s.step (16);
        return valid && s.job() == 0 && ! s.masters().empty();
    };
    const auto wavMatches = [&] (const MasterToken& identity, const std::vector<float>& audio,
                                 unsigned expectedBits)
    {
        const auto wavPlan = s.masterWavPlan (identity);
        const auto audioShape = s.masterAudioShape (identity);
        std::vector<std::uint8_t> bytes (std::size_t (wavPlan.bytes));
        bool copied = bool (wavPlan) && wavPlan.bits == expectedBits;
        for (std::size_t at = 0; copied && at < bytes.size(); at += 65536u)
            copied = s.copyMasterWav (identity, at,
                { bytes.data() + at, std::min<std::size_t> (65536u, bytes.size() - at) }) == MasterTransferStatus::Ok;
        const std::size_t bytesPerSample = expectedBits / 8u;
        const std::size_t payload = audio.size() * bytesPerSample;
        if (! copied || bytes.size() < payload || audioShape.frames * audioShape.channels != audio.size()) return false;
        const std::size_t data = bytes.size() - payload;
        for (std::size_t frame = 0; frame < std::size_t (audioShape.frames); ++frame)
            for (std::size_t channel = 0; channel < audioShape.channels; ++channel)
            {
                const std::size_t sample = frame * audioShape.channels + channel;
                const std::size_t at = data + sample * bytesPerSample;
                std::int32_t code = std::int32_t (std::uint32_t (bytes[at]) | (std::uint32_t (bytes[at + 1]) << 8));
                if (expectedBits == 24)
                {
                    code |= std::int32_t (std::uint32_t (bytes[at + 2]) << 16);
                    if ((code & 0x800000) != 0) code |= ~0xffffff;
                }
                else code = std::int16_t (code);
                const float decoded = float (double (code) / double (std::uint32_t (1) << (expectedBits - 1u)));
                if (std::bit_cast<std::uint32_t> (decoded)
                    != std::bit_cast<std::uint32_t> (audio[channel * std::size_t (audioShape.frames) + frame])) return false;
            }
        return true;
    };
    bool peaksRendered = deliver (safe);
    auto token = s.pendingMaster();
    auto shape = s.masterAudioShape (token);
    peaksRendered = peaksRendered && shape.frames != 0;
    std::vector<float> delivered (std::size_t (shape.frames * shape.channels));
    peaksRendered = peaksRendered && s.copyMaster (token, delivered) == MasterTransferStatus::Ok;
    const double gain = felitronics::core::det::pow (10.0,
        s.masters().back().report->deliveryGainDb / 20.0);
    for (std::size_t i = 0; peaksRendered && i < pcm.size(); ++i)
        peaksRendered = near (delivered[i], double (pcm[i]) * gain, 0.000001);
    const auto peaksText = s.exportWorked (token.master);
    bool peaksGrid = peaksRendered;
    for (const float sample : delivered)
        peaksGrid = peaksGrid && same (double (sample) * 8388608.0,
                                      std::nearbyint (double (sample) * 8388608.0));
    ok (peaksRendered && s.masters().back().report->deliveryMode == DeliveryMode::PeaksOnly
        && peaksText.view().find ("mode = \"peaksOnly\"") != std::string_view::npos
        && peaksText.view().find ("chain = false") != std::string_view::npos
        && peaksText.view().find ("dither = false") != std::string_view::npos,
        "the real peaks-only job delivers only the scalar gain, with its own immutable no-chain report");
    ok (peaksGrid && wavMatches (token, delivered, 24),
        "peaks-only 24-bit session PCM is on its delivery grid and equals the exported WAV samples");
    (void) s.releaseMaster (token);
    const auto codecRoundTrip = [&]
    {
        const auto captured = s.snapshot();
        const auto size = Codec::encodedBytes (captured.view());
        std::string encoded (std::size_t (size.bytes), '\0');
        Snapshot decoded;
        return size.status == CodecStatus::Ok && Codec::encode (captured.view(), encoded) == CodecStatus::Ok
            && Codec::decode (encoded, decoded) == CodecStatus::Ok;
    };
    ok (codecRoundTrip(), "a peaks-only master survives the snapshot codec without a fictitious limiter landing");
    MeasurementValue asIsLoudness[] { { "integratedLufs", -9.0, MeasurementReason::None, 0 },
                                      { "truePeakDb", -2.5, MeasurementReason::None, 0 } };
    detail::Inspector::loudness (s, asIsLoudness);
    asIsPlan.sourceLufs = -9.0;
    command::Master exactRequest { 20 };
    bool exactRendered = deliver (exactRequest, &asIsPlan);
    token = s.pendingMaster();
    shape = s.masterAudioShape (token);
    exactRendered = exactRendered && shape.frames != 0;
    delivered.resize (std::size_t (shape.frames * shape.channels));
    exactRendered = exactRendered && s.copyMaster (token, delivered) == MasterTransferStatus::Ok;
    for (std::size_t i = 0; exactRendered && i < pcm.size(); ++i)
        exactRendered = std::bit_cast<std::uint32_t> (delivered[i]) == std::bit_cast<std::uint32_t> (pcm[i]);
    const auto exactText = s.exportWorked (token.master);
    ok (exactRendered && exactText.view().find ("mode = \"asIs\"") != std::string_view::npos
        && exactText.view().find ("fileFormat = \"original\"") != std::string_view::npos
        && exactText.view().find ("dither = false") != std::string_view::npos,
        "the reachable as-is fixture completes the session transfer and reports the original without dither");
    (void) s.releaseMaster (token);
    ok (codecRoundTrip(), "an as-is master survives the snapshot codec without a fictitious limiter landing");
    detail::Inspector::loudness (s, snapshot.view().measurements[std::size_t (Analyzer::Loudness)].numbers);
    (void) s.apply (command::SetTarget { 21, "cdDynamic" });
    bool dithered = deliver (command::Master { 22 });
    token = s.pendingMaster();
    const auto ditherText = s.exportWorked (token.master);
    shape = s.masterAudioShape (token);
    dithered = dithered && shape.frames != 0;
    delivered.resize (std::size_t (shape.frames * shape.channels));
    dithered = dithered && s.copyMaster (token, delivered) == MasterTransferStatus::Ok;
    bool quantized = ! delivered.empty();
    for (float sample : delivered)
        quantized = quantized && same (double (sample) * 32768.0, std::floor (double (sample) * 32768.0));
    ok (dithered && quantized && wavMatches (token, delivered, 16) && shape.sampleRate == 44100
        && s.masters().back().report->deliveryDithered
        && ditherText.view().find ("bitDepth = 16") != std::string_view::npos
        && ditherText.view().find ("dither = true") != std::string_view::npos,
        "the peaks-only CD delivery converts rate and applies the actual 16-bit dither");
    (void) s.releaseMaster (token);
    (void) s.apply (command::SetTarget { 23, "ebu" });
    bool specified = deliver (command::Master { 24 });
    token = s.pendingMaster();
    ok (specified && s.masters().back().report->deliveryMode == DeliveryMode::Mastered
        && s.masters().back().report->achievedLufs.value_or (0.0) < -22.0,
        "an already-mastered source is actually turned down to the EBU requirement");
    (void) s.releaseMaster (token);
    (void) s.apply (command::SetTarget { 25, "allStreaming" });
    forced.id = 26;
    const bool masteredAnyway = deliver (forced);
    token = s.pendingMaster();
    ok (masteredAnyway && s.masters().back().report->deliveryMode == DeliveryMode::Mastered
        && s.masters().back().landing && s.masters().back().landing->passes > 0,
        "masterAnyway actually runs the normal chain and landing");
    (void) s.releaseMaster (token);
    (void) s.apply (command::EditTarget { 2, { 2.0, -1.0 } });
    command::Master request { 3 };
    ok (s.check (request).rejection == Rejection::ClippedGain,
        "a clipped source cannot silently receive more loudness");
    request.allowClippedGain = true;
    ok (s.check (request).rejection == Rejection::None, "mastering a clipped source anyway remains an explicit choice");
    ok (Wire::commandStorage (s, R"({"kind":"master","commandId":"4","allowClippedGain":true,"masterAnyway":true})").rejection == Rejection::None
        && Wire::commandStorage (s, R"({"kind":"master","commandId":"4","allowClippedGain":false})").rejection == Rejection::ClippedGain
        && Wire::commandStorage (s, R"({"kind":"master","commandId":"4","masterAnyway":"true"})").rejection == Rejection::Contract
        && Wire::commandStorage (s, R"({"kind":"master","commandId":"4","allowClippedGain":"true"})").rejection == Rejection::Contract,
        "the wire accepts both explicit boolean choices and rejects accidental strings");
    request.id = 30;
    const bool louderRendered = deliver (request);
    ok (louderRendered && s.masters().back().report->deliveryMode == DeliveryMode::Mastered
        && s.masters().back().landing && s.masters().back().landing->passes > 0,
        "the louder target still renders normally after explicit clipped-gain consent");
    (void) s.releaseMaster (s.pendingMaster());
}

void theObservations()
{
    felitronics::test::group ("the observations: facts with their numbers — found, not found and not measured apart; thresholds strict where the owner said so");
    {
        Seen x;
        ok (x.seen().tooQuiet.status == ObservationStatus::NotMeasured && x.seen().tooQuiet.reason == MeasurementReason::Pending
            && x.seen().clipping.status == ObservationStatus::NotMeasured,
            "nothing ended yet: every kind is not measured — never \"not found\"");
        x.ended (Analyzer::Loudness, MeasurementReason::NoSignal);
        ok (x.seen().tooQuiet.status == ObservationStatus::NotMeasured && x.seen().tooQuiet.reason == MeasurementReason::NoSignal,
            "a loudness that ended without a value: not measured, with its reason");
    }
    {
        // −40 and −55 LUFS, exactly and one double under each.
        Seen x;
        const auto quiet = [&] (double lufs)
        {
            x.loudness[0] = { "integratedLufs", lufs, MeasurementReason::None, 0 };
            x.loudness[1] = { "truePeakDb", lufs + 12.0, MeasurementReason::None, 0 };
            x.ready (Analyzer::Loudness, { x.loudness, 2 });
            return x.seen().tooQuiet;
        };
        const auto at40 = quiet (-40.0), under40 = quiet (std::nextafter (-40.0, -100.0)), at55 = quiet (-55.0), under55 = quiet (std::nextafter (-55.0, -100.0));
        ok (at40.status == ObservationStatus::NotFound && under40.status == ObservationStatus::Found && under40.second < 0.5
            && at55.status == ObservationStatus::Found && at55.second < 0.5 && under55.second > 0.5 && under55.style == ObservationStyle::Warning,
            "−40 LUFS exactly is not quiet, a hair under is a warning; −55 exactly is still the ordinary mode, a hair under is gain only");
        const auto warn = ObservationText::fact (ObservationKind::TooQuiet, under40), gain = ObservationText::fact (ObservationKind::TooQuiet, under55);
        ok (warn && gain && warn->id == text::FactId::SourceQuiet && gain->id == text::FactId::SourceGainOnly && whole (*warn) && whole (*gain)
            && ! ObservationText::fact (ObservationKind::TooQuiet, at40),
            "the lines: " + (warn ? ru (*warn) : std::string()) + " / " + (gain ? ru (*gain) : std::string()) + " — and none where nothing was found");
    }
    {
        // Clips: none, a few (named by place), regular (the clipped source).
        Seen x;
        const auto clips = [&] (std::vector<double> starts)
        {
            x.clipRows.clear();
            for (const double at : starts) x.clipRows.insert (x.clipRows.end(), { at * 48000.0, 3.0, 0.0, 1.0, 0.99, 1.0 });
            x.clipArray = { "clips", { 0, 0, x.in.frames, 48000 }, 6, starts.size(), starts.size(), true, x.clipRows };
            x.clipping[0] = { "runCount", double (starts.size()), MeasurementReason::None, 0 };
            x.clipping[1] = { "clippedSamples[0]", 3.0 * double (starts.size()), MeasurementReason::None, 0 };
            x.clipping[2] = { "dcOffset[0]", 0.0, MeasurementReason::None, 0 };
            x.clipping[3] = { "dcOffset[1]", 0.0, MeasurementReason::None, 0 };
            x.ready (Analyzer::Clipping, { x.clipping, 4 }, { &x.clipArray, 1 });
            return x.seen().clipping;
        };
        const auto none = clips ({}), two = clips ({ 12.0, 12.4, 107.0 / 1.0 - 60.0 }), many = clips (std::vector<double> (10, 1.0));
        ok (none.status == ObservationStatus::NotFound, "no confirmed clip: not found");
        const auto said = ObservationText::fact (ObservationKind::Clipping, two);
        ok (two.status == ObservationStatus::Found && same (two.value, 3.0) && two.places == 2 && same (two.at1, 12.0) && same (two.at2, 47.0)
            && two.style == ObservationStyle::Error && said && said->id == text::FactId::SourceClipsAt2 && whole (*said),
            "three clips, two of them 0.4 s apart: two places — " + (said ? ru (*said) : std::string()));
        const auto clipped = ObservationText::fact (ObservationKind::Clipping, many);
        ok (same (many.second, 10.0) && clipped && clipped->id == text::FactId::SourceClipped && whole (*clipped)
            && ru (*clipped).find ("клипована") != std::string::npos,
            "ten a minute: the clipped source — " + (clipped ? ru (*clipped) : std::string()));
    }
    {
        // The DC offset from its threshold, handled by the high-pass where it is in the chain.
        Seen x;
        const auto dc = [&] (double offset, bool hpf)
        {
            x.clipping[0] = { "runCount", 0.0, MeasurementReason::None, 0 };
            x.clipping[1] = { "dcOffset[0]", offset, MeasurementReason::None, 0 };
            x.clipping[2] = { "dcOffset[1]", -offset / 2.0, MeasurementReason::None, 0 };
            x.ready (Analyzer::Clipping, { x.clipping, 3 });
            x.in.hpfOn = hpf;
            return x.seen().dcOffset;
        };
        const auto under = dc (0.000999, true), at = dc (0.001, true), unhandled = dc (0.005, false);
        ok (under.status == ObservationStatus::NotFound && at.status == ObservationStatus::Found && at.handledBy == HandledBy::Hpf && at.handled
            && same (at.severity, 0.0) && unhandled.handledBy == HandledBy::Hpf && ! unhandled.handled && near (unhandled.severity, 4.0 / 9.0, 1e-12),
            "a DC offset from 0.001 is found, weighed to full at 0.01, and handled by the high-pass only where the high-pass is in the chain");
    }
    {
        // The spectral wall: found from 0.2 below Nyquist; a file too short to look is not measured.
        Seen x;
        const auto wall = [&] (double valid, double fraction, MeasurementReason cutoffReason = MeasurementReason::None)
        {
            x.forensics[0] = { "wall.valid", valid, MeasurementReason::None, 0 };
            x.forensics[1] = { "wall.cutoffHz", cutoffReason == MeasurementReason::None ? std::optional<double> (fraction * 24000.0) : std::nullopt, cutoffReason, 0 };
            x.forensics[2] = { "wall.dropDb", 32.0, MeasurementReason::None, 0 };
            x.forensics[3] = { "wall.cutoffFractionOfNyquist", fraction, MeasurementReason::None, 0 };
            x.ready (Analyzer::Forensics, { x.forensics, 4 });
            return x.seen().spectralWall;
        };
        const auto lossy = wall (1.0, 0.667), high = wall (1.0, 0.89), none = wall (0.0, 1.0, MeasurementReason::NoSignal), shortFile = wall (0.0, 1.0, MeasurementReason::TooShort);
        const auto said = ObservationText::fact (ObservationKind::SpectralWall, lossy);
        ok (lossy.status == ObservationStatus::Found && near (lossy.confidence, (32.0 - 24.0) / (40.0 - 24.0), 1e-12) && high.status == ObservationStatus::NotFound
            && none.status == ObservationStatus::NotFound && shortFile.status == ObservationStatus::NotMeasured && said && whole (*said),
            "a wall at 16 kHz is found, one near Nyquist is not, no wall is not found, a file too short is not measured — " + (said ? ru (*said) : std::string()));
    }
    {
        // Sibilance from the bursts: shown whatever the de-esser; blind is not measured, not "clean".
        Seen x;
        const double dome = 0.9;
        const auto bursts = [&] (double sideUnderMidDb, double burstShare, unsigned count)
        {
            x.eventRows.clear();
            for (unsigned i = 0; i < count; ++i)
            {
                const double peak = 1e-4;
                const double row[16] { double (i) * 96000.0, 3840.0, double (i) * 96000.0 + 960.0, peak, peak / 16.0, 12.0 + double (i % 5),
                    peak / dome, peak * 8.0, 8.0, 0.0, 0.0, 0.0, peak * felitronics::core::det::pow10 (-sideUnderMidDb / 10.0), 0.0, 1.0, 0.0 };
                x.eventRows.insert (x.eventRows.end(), row, row + 16);
            }
            x.events = { "midEvents", { 0, 480, x.in.frames, 48000 }, 16, count, count, true, x.eventRows };
            x.bursts[0] = { "sampleRate", 48000.0, MeasurementReason::None, 0 };
            x.bursts[1] = { "hopSamples[0]", 480.0, MeasurementReason::None, 0 };
            x.bursts[2] = { "eligibleHops[0]", 6000.0, MeasurementReason::None, 0 };
            x.bursts[3] = { "burstHops[0]", 6000.0 * burstShare, MeasurementReason::None, 0 };
            x.bursts[4] = { "domeShare", dome, MeasurementReason::None, 0 };
            x.bursts[5] = { "sideAbsent", 0.0, MeasurementReason::None, 0 };
            x.ready (Analyzer::StereoBursts, { x.bursts, 6 }, { &x.events, 1 });
            return x.seen().sibilance;
        };
        const auto spoken = bursts (10.0, 0.1, 30), wide = bursts (1.0, 0.1, 30), blind = bursts (10.0, 0.5, 30);
        const auto said = ObservationText::fact (ObservationKind::Sibilance, spoken);
        ok (spoken.status == ObservationStatus::Found && near (spoken.confidence, 1.0, 1e-9) && spoken.hypothesis && spoken.places == 3
            && near (spoken.second, 30.0, 1e-9) && said && said->id == text::FactId::SourceSibilanceAt && whole (*said),
            "thirty centred bursts a minute in the band: shown, a hypothesis, with its loudest places — " + (said ? ru (*said) : std::string()));
        ok (wide.status == ObservationStatus::NotFound && blind.status == ObservationStatus::NotMeasured && blind.reason == MeasurementReason::Capacity,
            "bursts the side carries as loud as the mid are not sibilance; bursts in half the time are a blind detector, not a finding");
    }
    {
        // The hum by the detector's own verdict per channel — even where it refused to call the result valid.
        Seen x;
        const auto hum = [&] (MeasurementStatus status, double reason, double prominence, bool wandering)
        {
            x.hum[0] = { "reason[0]", reason, MeasurementReason::None, 0 };
            x.hum[1] = { "reason[1]", reason, MeasurementReason::None, 0 };
            x.hum[2] = { "prominenceDb[0]", reason < 0.5 ? std::optional<double> (prominence) : std::nullopt,
                         reason < 0.5 ? MeasurementReason::None : MeasurementReason::NoSignal, 0 };
            x.hum[3] = { "fundamentalHz[0]", reason < 0.5 ? std::optional<double> (50.0) : std::nullopt,
                         reason < 0.5 ? MeasurementReason::None : MeasurementReason::NoSignal, 0 };
            x.hum[4] = { "tonePower[0]", reason < 0.5 ? std::optional<double> (1e-6) : std::nullopt,
                         reason < 0.5 ? MeasurementReason::None : MeasurementReason::NoSignal, 0 };
            x.candidateRows.assign (36 * 4, 0.0);
            if (wandering)
            {
                double* v = x.candidateRows.data();
                v[18] = 1.0; v[19] = 1.0; v[16] = 0.0; v[17] = 0.0; v[22] = 60.3; v[23] = 1e-6; v[26] = 25.0;
            }
            x.candidates = { "candidates", { 0, 0, x.in.frames, 48000 }, 36, 4, 4, true, x.candidateRows };
            auto& r = x.results[std::size_t (Analyzer::Hum)];
            r.status = status; r.reason = status == MeasurementStatus::Ready ? MeasurementReason::None : MeasurementReason::NoSignal;
            r.numbers = { x.hum, 5 }; r.arrays = { &x.candidates, 1 };
            return x.seen();
        };
        const auto line = hum (MeasurementStatus::Ready, 0.0, 22.0, false), clean = hum (MeasurementStatus::Ready, 0.0, 0.0, false);
        const auto wanders = hum (MeasurementStatus::Unavailable, 9.0, 0.0, true), deaf = hum (MeasurementStatus::Unavailable, 6.0, 0.0, false);
        ok (line.hum.status == ObservationStatus::Found && same (line.hum.value, 50.0) && same (line.hum.second, 22.0)
            && clean.hum.status == ObservationStatus::NotFound && clean.humWandered.status == ObservationStatus::NotFound,
            "a stationary line 22 dB over its background is hum; a judged programme without one has none");
        const auto said = ObservationText::fact (ObservationKind::HumWandered, wanders.humWandered);
        ok (wanders.hum.status == ObservationStatus::NotFound && wanders.humWandered.status == ObservationStatus::Found
            && same (wanders.humWandered.value, 60.3) && wanders.humWandered.doubtful && said && whole (*said),
            "a line the detector measured and would not call stationary — its result refused as a whole — is a wandering hum, doubtful: " + (said ? ru (*said) : std::string()));
        // Owner decision 01.10: the detector listened through a programme that is never quiet — no steady line stood out.
        ok (deaf.hum.status == ObservationStatus::NotFound && deaf.humWandered.status == ObservationStatus::NotFound,
            "no quiet stretch: the detector listened and no line stood out — not found, not \"not measured\"");
        // Owner decision 01.10: a steady line that stops where the whole programme is quiet is music — the detector
        // listened and judged it: no hum, no wandering one, not "not measured".
        const auto music = hum (MeasurementStatus::Unavailable, 11.0, 0.0, false);
        ok (music.hum.status == ObservationStatus::NotFound && music.humWandered.status == ObservationStatus::NotFound,
            "a line only under the music: the detector judged it music — the hum not found");
        // Refused before any work (no price for this programme, or memory the session may not take): the result carries
        // no number at all, so the hum is not measured with the result's own reason — never "too short" for a long file.
        for (const auto reason : { MeasurementReason::Memory, MeasurementReason::Unsupported })
        {
            auto& r = x.results[std::size_t (Analyzer::Hum)];
            r.status = MeasurementStatus::Unavailable; r.reason = reason; r.numbers = {}; r.arrays = {};
            const auto o = x.seen();
            ok (o.hum.status == ObservationStatus::NotMeasured && o.hum.reason == reason
                && o.humWandered.status == ObservationStatus::NotMeasured && o.humWandered.reason == reason,
                "the hum analyzer refused before work: not measured with the refusal's reason, not \"too short\"");
        }
        x.in.channels = 1;
        x.ended (Analyzer::LowEnd, MeasurementReason::NoSignal);
        x.ended (Analyzer::Stereo, MeasurementReason::NoSignal);
        ok (x.seen().wideBass.status == ObservationStatus::NotFound && x.seen().polarity.status == ObservationStatus::NotFound,
            "a mono file has no wide bass and no opposite polarity, whatever its low end's status");
    }
    {
        // A rule one of whose inputs was not measured is not measured: never "not found", never a 0 in its line.
        Seen x;
        const auto none = MeasurementReason::None;
        // Polarity: the correlation read, the low end's side share not — then both.
        x.stereo[0] = { "correlation", -0.9, none, 0 };
        x.ready (Analyzer::Stereo, { x.stereo, 1 });
        x.ended (Analyzer::LowEnd, MeasurementReason::TooShort);
        const auto halfOpposite = x.seen().polarity;
        x.stereo[0] = { "correlation", 0.9, none, 0 };
        const auto halfClean = x.seen().polarity;
        x.lowEnd[0] = { "rawSideFraction", 0.1, none, 0 };
        x.ready (Analyzer::LowEnd, { x.lowEnd, 1 });
        const auto wholeClean = x.seen().polarity;
        ok (halfOpposite.status == ObservationStatus::NotMeasured && halfClean.status == ObservationStatus::NotMeasured
            && halfClean.reason == MeasurementReason::TooShort && wholeClean.status == ObservationStatus::NotFound,
            "polarity with the side share not measured is not measured, whatever the correlation; with both read, judged");
        // The edges: the tail invalid (a non-finite sample in it), the head read — a long head, then a short one.
        const auto edges = [&] (double headSeconds, double tailValid)
        {
            x.programme[0] = { "sampleRate", 48000.0, none, 0 };
            x.programme[1] = { "leadingSilenceSamples", headSeconds * 48000.0, none, 0 };
            x.programme[2] = { "leadingSilenceValid", 1.0, none, 0 };
            x.programme[3] = { "trailingSilenceSamples", 0.0, none, 0 };
            x.programme[4] = { "trailingSilenceValid", tailValid, none, 0 };
            x.ready (Analyzer::Programme, { x.programme, 5 });
            return x.seen().edgeSilence;
        };
        const auto longHead = edges (3.0, 0.0), shortHead = edges (0.1, 0.0), measuredLong = edges (3.0, 1.0);
        const auto said = ObservationText::fact (ObservationKind::EdgeSilence, measuredLong);
        const auto unsaid = ObservationText::fact (ObservationKind::EdgeSilence, longHead);
        ok (longHead.status == ObservationStatus::NotMeasured && longHead.reason == MeasurementReason::NonFinite
            && shortHead.status == ObservationStatus::NotMeasured && unsaid && unsaid->id == text::FactId::ObservationUnmeasured
            && measuredLong.status == ObservationStatus::Found && same (measuredLong.value, 3.0) && same (measuredLong.second, 0.0) && said && whole (*said),
            "an edge not measured is not \"0.0 s\": the edges are not measured, a short head is not \"not found\"; both read — "
            + (said ? ru (*said) : std::string()));
        // The DC offset: one channel read, the other not.
        x.clipping[0] = { "runCount", 0.0, none, 0 };
        x.clipping[1] = { "dcOffset[0]", 0.0, none, 0 };
        x.ready (Analyzer::Clipping, { x.clipping, 2 });
        const auto halfDc = x.seen().dcOffset;
        x.clipping[2] = { "dcOffset[1]", 0.0, none, 0 };
        x.ready (Analyzer::Clipping, { x.clipping, 3 });
        ok (halfDc.status == ObservationStatus::NotMeasured && halfDc.reason == MeasurementReason::Unsupported
            && x.seen().dcOffset.status == ObservationStatus::NotFound,
            "a DC offset read on one channel of two is not measured; read on both, judged");
        // The unused low bits: one channel read, the other not.
        x.forensics[0] = { "grid.alwaysZeroLowBits[0]", 8.0, none, 0 };
        x.ready (Analyzer::Forensics, { x.forensics, 1 });
        ok (x.seen().bitsUnused.status == ObservationStatus::NotMeasured, "the unused low bits read on one channel of two: not measured");
        // Already limited: a PLR that is not dense, the clips not counted — then counted.
        Seen y;
        y.loudness[0] = { "integratedLufs", -14.0, none, 0 };
        y.loudness[1] = { "truePeakDb", -2.0, none, 0 };
        y.ready (Analyzer::Loudness, { y.loudness, 2 });
        const auto uncounted = y.seen().alreadyLimited;
        y.clipping[0] = { "runCount", 0.0, none, 0 };
        y.ready (Analyzer::Clipping, { y.clipping, 1 });
        const auto counted = y.seen().alreadyLimited;
        y.loudness[1] = { "truePeakDb", -8.0, none, 0 };
        y.ended (Analyzer::Clipping, MeasurementReason::Memory);
        const auto dense = y.seen().alreadyLimited;
        ok (uncounted.status == ObservationStatus::NotMeasured && uncounted.reason == MeasurementReason::Pending
            && counted.status == ObservationStatus::NotFound && dense.status == ObservationStatus::Found && same (dense.value, 6.0),
            "a PLR of 12 dB with the clips not counted is not measured, counted and none it is not found; a PLR of 6 dB is found without them");
    }
    {
        // The order of the analysis, and nothing that switches a device.
        ok (unsigned (ObservationKind::Clipping) == 0 && unsigned (ObservationKind::AlreadyLimited) < unsigned (ObservationKind::SpectralWall)
            && unsigned (ObservationKind::Sibilance) < unsigned (ObservationKind::Hum) && kObservationKinds == 17,
            "the kinds in the order of the analysis: the file, then the spectrum, then the hum");
    }
    {
        // On a real session: its observations in the snapshot, with the plan.
        const Mix mix (1.0f, 4);
        auto sp = measured (mix, "allStreaming");
        const auto o = sp->snapshot().view().observations;
        bool ended = true;
        for (unsigned k = 0; k < kObservationKinds; ++k)
            ended = ended && ObservationText::of (o, ObservationKind (k)).reason != MeasurementReason::Pending;
        ok (o.tooQuiet.status == ObservationStatus::NotFound && o.tooShort.status == ObservationStatus::Found && same (o.tooShort.value, 4.0)
            && o.clipping.status == ObservationStatus::NotFound && ended && o.wideBass.handledBy == HandledBy::MonoBass
            && (o.hum.status != ObservationStatus::NotMeasured || o.hum.reason == MeasurementReason::NoSignal),
            "a measured four-second mix: its observations published with the snapshot — too short to search, not quiet, no clip, every "
            "kind judged or not measured with its reason (the hum: no quiet part to listen in)");
    }
}

// THE OWNER'S TABLE (01.10): the styles, the styles by size and the severities, each threshold from both sides.
void theOwnersTable()
{
    felitronics::test::group ("the owner's observation table: a style by nature, by size where approved; severities along their ramps");
    const auto none = MeasurementReason::None;
    const auto lineOf = [] (ObservationKind kind, const Observation& o)
    { const auto f = ObservationText::fact (kind, o); return f ? std::pair { f->id, ru (*f) } : std::pair { text::FactId::Value, std::string() }; };
    {
        // The DC offset: a note under 1 % of full scale, a warning from 1 %, an error from 10 %.
        Seen x;
        const auto dc = [&] (double offset)
        {
            x.clipping[0] = { "runCount", 0.0, none, 0 };
            x.clipping[1] = { "dcOffset[0]", offset, none, 0 };
            x.clipping[2] = { "dcOffset[1]", 0.0, none, 0 };
            x.ready (Analyzer::Clipping, { x.clipping, 3 });
            return x.seen().dcOffset;
        };
        const auto small = dc (0.0099), one = dc (0.01), large = dc (0.0999), ten = dc (-0.1);
        const auto note = lineOf (ObservationKind::DcOffset, small), warn = lineOf (ObservationKind::DcOffset, one);
        ok (small.style == ObservationStyle::Note && one.style == ObservationStyle::Warning && large.style == ObservationStyle::Warning
            && ten.style == ObservationStyle::Error && note.first == text::FactId::SourceDcNoteStereo && warn.first == text::FactId::SourceDcStereo
            && note.second.find ("ФВЧ мастера её уберёт") != std::string::npos,
            "DC under 1 % a note, from 1 % a warning, from 10 % (either sign) an error; the note says — " + note.second);
        // Both channels, signed, as the readings print them: the line names the left's and the right's offset.
        const auto both = [&] (double left, double right)
        {
            x.clipping[1] = { "dcOffset[0]", left, none, 0 };
            x.clipping[2] = { "dcOffset[1]", right, none, 0 };
            x.ready (Analyzer::Clipping, { x.clipping, 3 });
            return x.seen().dcOffset;
        };
        const auto pair = both (-0.0042, 0.0071), loud = both (0.002, -0.15);
        const auto pairLine = lineOf (ObservationKind::DcOffset, pair), loudLine = lineOf (ObservationKind::DcOffset, loud);
        const auto pairFact = ObservationText::fact (ObservationKind::DcOffset, pair);
        ok (pairLine.first == text::FactId::SourceDcNoteStereo && pairFact && pairFact->argCount == 2
            && same (pairFact->args[0].number, -0.0042) && same (pairFact->args[1].number, 0.0071)
            && pairLine.second.find ("L \xE2\x88\x92" "0,0042, R 0,0071") != std::string::npos
            && loudLine.first == text::FactId::SourceDcStereo && loudLine.second.find ("R \xE2\x88\x92" "0,1500") != std::string::npos
            && same (loud.value, 0.15) && loud.style == ObservationStyle::Error,
            "a stereo offset names both channels, signed; the larger sizes it — " + pairLine.second + " / " + loudLine.second);
        // A mono source keeps its one-number line.
        x.in.channels = 1;
        const auto mono = both (0.005, 0.0);
        const auto monoLine = lineOf (ObservationKind::DcOffset, mono);
        x.in.channels = 2;
        ok (monoLine.first == text::FactId::SourceDcNote && mono.places == 1, "a mono offset: its one number — " + monoLine.second);
    }
    {
        // The effective depth: 24 nothing, 23…17 a warning, 16 and less an error; a 32-bit container carrying 24 nothing.
        Seen x;
        const auto bits = [&] (std::uint32_t container, double unused)
        {
            x.in.bitDepth = container;
            x.forensics[0] = { "grid.alwaysZeroLowBits[0]", unused, none, 0 };
            x.forensics[1] = { "grid.alwaysZeroLowBits[1]", unused + 1.0, none, 0 };
            x.ready (Analyzer::Forensics, { x.forensics, 2 });
            return x.seen().bitsUnused;
        };
        const auto full = bits (24, 0), d23 = bits (24, 1), d17 = bits (24, 7), d16 = bits (24, 8), cd = bits (16, 0), wide = bits (32, 8), lossy = bits (0, 0);
        const auto truncated = lineOf (ObservationKind::BitsUnused, d23), shallow = lineOf (ObservationKind::BitsUnused, cd);
        ok (full.status == ObservationStatus::NotFound && d23.status == ObservationStatus::Found && same (d23.value, 23.0)
            && d23.style == ObservationStyle::Warning && d17.style == ObservationStyle::Warning && d16.style == ObservationStyle::Error
            && cd.style == ObservationStyle::Error && same (cd.value, 16.0) && near (d16.severity, 1.0, 0.0) && same (d23.severity, 0.0)
            && wide.status == ObservationStatus::NotFound && lossy.status == ObservationStatus::NotMeasured
            && truncated.first == text::FactId::SourceTruncatedBits && truncated.second.find ("до 23 бит") != std::string::npos
            && shallow.first == text::FactId::SourceShallowMix && shallow.second.find ("16-битный микс — сведите в 24 бита") != std::string::npos,
            "depth 24 nothing; 23 and 17 a warning; 16 — of a 24-bit or a 16-bit file — an error; 32 carrying 24 nothing; no container not "
            "measured — " + truncated.second + " / " + shallow.second);
    }
    {
        // The infra-low share: under 2 % nothing, 2…5 % a note, from 5 % a warning.
        Seen x;
        const auto infra = [&] (double share)
        {
            x.infra[0] = { "infraLowShare", share, none, 0 };
            x.infra[1] = { "crossoverHz", 30.0, none, 0 };
            x.ready (Analyzer::InfraLow, { x.infra, 2 });
            return x.seen().infraLow;
        };
        const auto under = infra (0.0199), from = infra (0.02), below5 = infra (0.0499), at5 = infra (0.05);
        const auto note = lineOf (ObservationKind::InfraLow, from), warn = lineOf (ObservationKind::InfraLow, at5);
        ok (under.status == ObservationStatus::NotFound && from.style == ObservationStyle::Note && below5.style == ObservationStyle::Note
            && at5.style == ObservationStyle::Warning && note.first == text::FactId::SourceInfraLowNote
            && warn.first == text::FactId::SourceInfraLowWarning && warn.second.find ("(5,0") != std::string::npos,
            "infra-low from 2 % a note, from 5 % a warning — " + note.second + " / " + warn.second);
    }
    {
        // Already limited: found under a PLR of 10.5 dB, a warning, the severity full at 7 dB.
        Seen x;
        const auto limited = [&] (double plr)
        {
            x.loudness[0] = { "integratedLufs", -10.0, none, 0 };
            x.loudness[1] = { "truePeakDb", -10.0 + plr, none, 0 };
            x.ready (Analyzer::Loudness, { x.loudness, 2 });
            x.clipping[0] = { "runCount", 0.0, none, 0 };
            x.ready (Analyzer::Clipping, { x.clipping, 1 });
            return x.seen().alreadyLimited;
        };
        const auto at = limited (10.5), under = limited (10.25), half = limited (8.75), dense = limited (6.0);
        const auto said = lineOf (ObservationKind::AlreadyLimited, half);
        ok (at.status == ObservationStatus::NotFound && under.status == ObservationStatus::Found && under.style == ObservationStyle::Warning
            && under.hypothesis && near (under.severity, 0.25 / 3.5, 1e-12) && near (half.severity, 0.5, 1e-12) && same (dense.severity, 1.0)
            && said.first == text::FactId::SourceLimitedBus && said.second.find ("без лимитера на шине") != std::string::npos,
            "a PLR of 10.5 dB is not limited, a hair under is a warning; the severity half at 8.75 dB and full at 7 — " + said.second);
    }
    {
        // The spectral wall: a warning from 12 % under Nyquist, confidence full at a 40 dB drop.
        Seen x;
        const auto wall = [&] (double fraction, double drop)
        {
            x.forensics[0] = { "wall.valid", 1.0, none, 0 };
            x.forensics[1] = { "wall.cutoffHz", fraction * 24000.0, none, 0 };
            x.forensics[2] = { "wall.dropDb", drop, none, 0 };
            x.forensics[3] = { "wall.cutoffFractionOfNyquist", fraction, none, 0 };
            x.ready (Analyzer::Forensics, { x.forensics, 4 });
            return x.seen().spectralWall;
        };
        const auto at87 = wall (0.87, 40.0), at89 = wall (0.89, 40.0), deep = wall (0.667, 60.0);
        const auto said = lineOf (ObservationKind::SpectralWall, deep);
        ok (at87.status == ObservationStatus::Found && at87.style == ObservationStyle::Warning && same (at87.confidence, 1.0)
            && at89.status == ObservationStatus::NotFound && said.first == text::FactId::SourceLossy && said.second.find ("16,0") != std::string::npos,
            "a wall at 87 % of Nyquist is a warning, at 89 % nothing; a 40 dB drop is fully confident — " + said.second);
    }
    {
        // Wide bass, polarity and a dual-mono file: their styles and severities.
        Seen x;
        const auto low = [&] (double lowSide, double correlation, double rawSide)
        {
            x.lowEnd[0] = { "lowSideFraction", lowSide, none, 0 };
            x.lowEnd[1] = { "crossoverHz", 120.0, none, 0 };
            x.lowEnd[2] = { "rawSideFraction", rawSide, none, 0 };
            x.ready (Analyzer::LowEnd, { x.lowEnd, 3 });
            x.stereo[0] = { "correlation", correlation, none, 0 };
            x.stereo[1] = { "dualMono", 0.0, none, 0 };
            x.ready (Analyzer::Stereo, { x.stereo, 2 });
            return x.seen();
        };
        const auto at6 = low (0.06, 0.5, 0.1), at18 = low (0.18, 0.5, 0.1), at30 = low (0.3, 0.5, 0.1);
        ok (at6.wideBass.style == ObservationStyle::Warning && same (at6.wideBass.severity, 0.0) && near (at18.wideBass.severity, 0.5, 1e-12)
            && same (at30.wideBass.severity, 1.0), "wide bass a warning, its severity 0 at 6 %, half at 18 %, full at 30 %");
        const auto edge = low (0.5, -0.1, 0.1), quarter = low (0.625, -0.1, 0.1), opposite = low (0.8, -0.1, 0.1);
        ok (edge.polarity.status == ObservationStatus::Found && edge.polarity.style == ObservationStyle::Error
            && same (edge.polarity.severity, 0.0) && near (quarter.polarity.severity, 0.5, 1e-12) && same (opposite.polarity.severity, 1.0),
            "opposite polarity an error, its severity by the low band's correlation: 0 at 0, half at −0.25, full at −0.5 and below");
        x.stereo[1] = { "dualMono", 1.0, none, 0 };
        const auto dual = x.seen().dualMono;
        const auto said = lineOf (ObservationKind::DualMono, dual);
        ok (dual.style == ObservationStyle::Warning && same (dual.severity, 0.0) && said.second.find ("проверьте экспорт") != std::string::npos,
            "a dual-mono file a warning, severity 0 — " + said.second);
    }
    {
        // The hum: the severity in dB from −60 to −40; a note, a warning from half its severity when confident. And the
        // detector's verdicts: listened and found nothing is not found; could not listen is not measured, with why.
        Seen x;
        x.programme[0] = { "programmeMeanSquare", 1.0, none, 0 };
        x.ready (Analyzer::Programme, { x.programme, 1 });
        const auto hum = [&] (double reason, double prominence, double power, bool baseHeard = false)
        {
            const bool ok0 = reason < 0.5;
            x.hum[0] = { "reason[0]", reason, none, 0 };
            x.hum[1] = { "reason[1]", reason, none, 0 };
            x.hum[2] = { "prominenceDb[0]", ok0 ? std::optional<double> (prominence) : std::nullopt, ok0 ? none : MeasurementReason::NoSignal, 0 };
            x.hum[3] = { "fundamentalHz[0]", ok0 ? std::optional<double> (50.0) : std::nullopt, ok0 ? none : MeasurementReason::NoSignal, 0 };
            x.hum[4] = { "tonePower[0]", ok0 ? std::optional<double> (power) : std::nullopt, ok0 ? none : MeasurementReason::NoSignal, 0 };
            x.candidateRows.assign (36 * 4, 0.0);
            for (std::size_t row = 0; row < 4; ++row) x.candidateRows[row * 36] = double (row / 2);
            if (baseHeard) x.candidateRows[2] = 1.0;
            x.candidates = { "candidates", { 0, 0, x.in.frames, 48000 }, 36, 4, 4, true, x.candidateRows };
            auto& r = x.results[std::size_t (Analyzer::Hum)];
            r.status = ok0 ? MeasurementStatus::Ready : MeasurementStatus::Unavailable; r.reason = ok0 ? none : MeasurementReason::NoSignal;
            r.numbers = { x.hum, 5 }; r.arrays = { &x.candidates, 1 };
            return x.seen().hum;
        };
        const auto quietLine = hum (0.0, 40.0, 1e-6), loud = hum (0.0, 40.0, 1e-4), over = hum (0.0, 40.0, 1e-3);
        const auto under50 = hum (0.0, 40.0, 0.5e-5), over50 = hum (0.0, 40.0, 2e-5), unsure = hum (0.0, 22.0, 2e-5);
        ok (same (quietLine.severity, 0.0) && near (loud.severity, 1.0, 1e-12) && same (over.severity, 1.0)
            && near (over50.severity, 0.5 + 10.0 * felitronics::core::det::log10 (2.0) / 20.0, 1e-12)
            && under50.style == ObservationStyle::Note && over50.style == ObservationStyle::Warning
            && unsure.doubtful && unsure.style == ObservationStyle::Note,
            "hum severity 0 at −60 dB, full at −40 dB; a note under −50 dB, a warning over it — but not when doubtful");
        const auto noQuiet = hum (6.0, 0.0, 0.0), comb = hum (10.0, 0.0, 0.0), single = hum (7.0, 0.0, 0.0), singleHeard = hum (7.0, 0.0, 0.0, true);
        const auto tooFew = hum (8.0, 0.0, 0.0, true), shortFile = hum (4.0, 0.0, 0.0), holed = hum (5.0, 0.0, 0.0), coarse = hum (3.0, 0.0, 0.0);
        ok (noQuiet.status == ObservationStatus::NotFound && comb.status == ObservationStatus::NotFound && single.status == ObservationStatus::NotFound
            && singleHeard.status == ObservationStatus::NotMeasured && singleHeard.reason == MeasurementReason::NoSignal
            && tooFew.status == ObservationStatus::NotMeasured && tooFew.reason == MeasurementReason::NoSignal
            && shortFile.reason == MeasurementReason::TooShort && holed.reason == MeasurementReason::NonFinite
            && coarse.reason == MeasurementReason::Unsupported && coarse.status == ObservationStatus::NotMeasured,
            "never quiet, a comb without its base, one quiet stretch with no base line: not found; a base line heard in too little "
            "quiet: not measured (no signal); too short, all frames holed, too coarse: not measured, each with its reason");
    }
}

// Two facts are one line: the same id, and the same words in both declared languages.
bool sameLine (const text::Fact& a, const text::Fact& b) { return a.id == b.id && ru (a) == ru (b) && en (a) == en (b); }
// The snapshot's lines are ObservationText's, kind by kind in the order of ObservationKind: every kind that has a line,
// and nothing else. `lines`: how many kinds have one.
bool linesAreTheText (const SnapshotView& v, std::size_t& lines)
{
    std::size_t i = 0;
    bool same = true;
    for (unsigned k = 0; k < kObservationKinds; ++k)
        if (const auto said = ObservationText::fact (ObservationKind (k), ObservationText::of (v.observations, ObservationKind (k))))
        {
            same = same && i < v.observationFacts.count && v.observationFacts.items[i].kind == ObservationKind (k)
                && sameLine (v.observationFacts.items[i].fact, *said);
            ++i;
        }
    lines = i;
    return same && i == v.observationFacts.count;
}
bool hasLine (const SnapshotView& v, ObservationKind kind)
{
    for (std::size_t i = 0; i < v.observationFacts.count; ++i)
        if (v.observationFacts.items[i].kind == kind) return true;
    return false;
}

void theObservationsSpeakForThemselves()
{
    felitronics::test::group ("the observations speak for themselves: the snapshot carries each one's line as ObservationText states it");
    {
        // Not measured: its name and why — for every kind and every reason, in both declared languages.
        Seen x;
        x.ended (Analyzer::Loudness, MeasurementReason::NoSignal);
        const auto quiet = ObservationText::fact (ObservationKind::TooQuiet, x.seen().tooQuiet);
        ok (quiet && quiet->id == text::FactId::ObservationUnmeasured && quiet->argCount == 2 && quiet->args[0].termId == text::Term::ObservationTooQuiet
            && quiet->args[1].termId == text::Term::ReasonNoSignal && whole (*quiet),
            "a loudness that ended without a value: " + (quiet ? ru (*quiet) + " / " + en (*quiet) : std::string()));
        bool every = true;
        std::vector<std::string> names;
        for (unsigned k = 0; k < kObservationKinds; ++k)
            for (unsigned r = unsigned (MeasurementReason::Pending); r <= unsigned (MeasurementReason::Memory); ++r)
            {
                Observation o; o.status = ObservationStatus::NotMeasured; o.reason = MeasurementReason (r);
                const auto f = ObservationText::fact (ObservationKind (k), o);
                every = every && f && whole (*f) && f->args[0].termId == text::Term (unsigned (text::Term::ObservationClipping) + k);
                if (f && r == unsigned (MeasurementReason::Pending)) names.push_back (ru (*f) + en (*f));
            }
        std::sort (names.begin(), names.end());
        ok (every && names.size() == kObservationKinds && std::adjacent_find (names.begin(), names.end()) == names.end(),
            "every kind not measured, for every reason, is a whole line in Russian and English — and each kind by its own name");
        Observation none; none.status = ObservationStatus::NotFound; none.reason = MeasurementReason::None;
        ok (! ObservationText::fact (ObservationKind::Clipping, none), "measured and not found: no line");
    }
    {
        auto s = Session::create().session;
        ok (s->snapshot().view().observationFacts.count == 0, "before a source the snapshot carries no observation line");
        const Mix mix (1.0f, 4);
        (void) s->apply (command::SetTarget { 1, "allStreaming" });
        ok (s->apply (mix.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads");
        std::size_t lines = 0;
        const auto loaded = s->snapshot();
        const auto& first = loaded.view().observationFacts;
        const auto quietLine = std::find_if (first.items.begin(), first.items.begin() + first.count,
                                             [] (const ObservationFact& f) { return f.kind == ObservationKind::TooQuiet; });
        ok (linesAreTheText (loaded.view(), lines) && lines == kObservationKinds && quietLine != first.items.begin() + first.count
            && quietLine->fact.id == text::FactId::ObservationUnmeasured && quietLine->fact.args[1].termId == text::Term::ReasonPending,
            "loaded, the loudness not ended: every kind has its line, and \"too quiet\" says it is not measured yet — " + (quietLine != first.items.begin() + first.count ? ru (quietLine->fact) : std::string()));
        drive (*s);
        const auto done = s->snapshot();
        const auto& v = done.view();
        ok (linesAreTheText (v, lines) && lines > 0 && lines < kObservationKinds && v.observations.tooQuiet.status == ObservationStatus::NotFound
            && ! hasLine (v, ObservationKind::TooQuiet) && hasLine (v, ObservationKind::TooShort),
            "measured: the lines are ObservationText's, kind by kind — " + std::to_string (lines) + " of 17 have one; not quiet, so no line of it");
        ok (v.observations.loudestLowNote.style == ObservationStyle::Reading && v.observations.wideBass.style == ObservationStyle::Warning
            && v.observations.clipping.style == ObservationStyle::Error && v.observations.lowestLowBand.style == ObservationStyle::Reading,
            "the loudest low note and the lowest band are readings; wide bass a warning, clipping an error");
        // A new source: the lines follow its measurement.
        const Mix quiet (0.03f, 4);
        ok (s->apply (quiet.load (3)).rejection == Rejection::None, "PRECONDITION: a quieter mix loads");
        drive (*s);
        const auto again = s->snapshot();
        std::size_t linesAgain = 0;
        ok (linesAreTheText (again.view(), linesAgain) && again.view().observations.tooQuiet.status == ObservationStatus::Found
            && hasLine (again.view(), ObservationKind::TooQuiet),
            "a new measurement: the lines follow it — the quieter mix's own line among them");
        // Across the wire: kind and fact, whole; an unknown fact id or kind is refused.
        std::string json (std::size_t (Codec::encodedBytes (again.view()).bytes), '\0');
        Snapshot restored;
        ok (Codec::encode (again.view(), json) == CodecStatus::Ok && Codec::decode (json, restored) == CodecStatus::Ok, "PRECONDITION: encoded and decoded");
        bool carried = restored.view().observationFacts.count == again.view().observationFacts.count && linesAgain != 0;
        for (std::size_t i = 0; carried && i < linesAgain; ++i)
            carried = restored.view().observationFacts.items[i].kind == again.view().observationFacts.items[i].kind
                   && sameLine (restored.view().observationFacts.items[i].fact, again.view().observationFacts.items[i].fact);
        const auto at = json.find ("\"observationFacts\":[{\"fact\":{\"FactId\":");
        ok (carried && at != std::string::npos, "the lines cross the wire whole, kind and fact");
        if (at != std::string::npos)
        {
            const auto id = json.find (':', json.find ("FactId", at)) + 1, idEnd = json.find (',', id);
            auto badFact = json; badFact.replace (id, idEnd - id, "999");
            const auto kindAt = json.find ("]},\"kind\":", at) + 10, kindEnd = json.find ('}', kindAt);
            auto badKind = json; badKind.replace (kindAt, kindEnd - kindAt, "17");
            auto goodKind = json; goodKind.replace (kindAt, kindEnd - kindAt, "16");
            Snapshot refused;
            ok (Codec::decode (badFact, refused) == CodecStatus::Invalid && Codec::decode (badKind, refused) == CodecStatus::Invalid
                && Codec::decode (goodKind, refused) == CodecStatus::Ok,
                "an unknown fact id (999) or kind (17) is refused; the last kind (16) is read");
        }
    }
}

// The reading of `kind` in a list, or nothing; a number of a measurement result, by name.
template <std::size_t N> const ReadingFact* readingOf (const BoundedList<ReadingFact, N>& list, ReadingKind kind)
{
    for (std::size_t i = 0; i < list.count; ++i)
        if (list.items[i].kind == kind) return &list.items[i];
    return nullptr;
}
std::optional<double> numberOf (const MeasurementResult& r, std::string_view name)
{
    for (const auto& v : r.numbers)
        if (v.name == name) return v.value;
    return std::nullopt;
}
// A reading as the table declares it: FactId::Value with one number of `unit` at `precision`.
bool isValue (const ReadingFact* r, text::Unit unit, std::uint8_t precision)
{
    return r && r->fact.id == text::FactId::Value && r->fact.argCount == 1 && r->fact.args[0].kind == text::ArgKind::Value
        && r->fact.args[0].unit == unit && r->fact.args[0].precision == precision
        && ! ru (r->fact).empty() && ru (r->fact).find ('{') == std::string::npos && en (r->fact).find ('{') == std::string::npos
        && ru (r->fact) != std::string (text::Text::key (r->fact.id));
}

void theReadingsAreFacts()
{
    felitronics::test::group ("the readings are facts: the source's numbers in the snapshot, each with the core's unit and precision");
    {
        // A reading renders as the catalogue says: the number, its unit and the language's decimal sign; its name is
        // the catalogue's term of its kind.
        auto s = Session::create().session;
        ok (s->snapshot().view().readings.count == 0, "before a source the snapshot carries no reading");
        const Mix mix (1.0f, 4);
        (void) s->apply (command::SetTarget { 1, "allStreaming" });
        ok (s->apply (mix.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads");
        ok (! readingOf (s->snapshot().view().readings, ReadingKind::Integrated), "loaded, the loudness not ended: no integrated reading");
        drive (*s);
        const auto done = s->snapshot();
        const auto& v = done.view();
        const auto& loudness = v.measurements[std::size_t (Analyzer::Loudness)];
        const auto& clipping = v.measurements[std::size_t (Analyzer::Clipping)];
        const auto* lufs = readingOf (v.readings, ReadingKind::Integrated);
        const auto* peak = readingOf (v.readings, ReadingKind::TruePeak);
        const auto* left = readingOf (v.readings, ReadingKind::DcOffsetLeft);
        const auto* runs = readingOf (v.readings, ReadingKind::ClipRuns);
        const auto* samplePeak = readingOf (v.readings, ReadingKind::SamplePeak);
        const auto measuredLufs = numberOf (loudness, "integratedLufs");
        ok (isValue (lufs, text::Unit::Lufs, 1) && isValue (peak, text::Unit::DbTp, 1) && measuredLufs
            && sameBits (lufs->fact.args[0].number, *measuredLufs)
            && sameBits (peak->fact.args[0].number, numberOf (loudness, "truePeakDb").value_or (0.0)),
            "the integrated loudness and the true peak are the loudness measurement's numbers, LUFS and dBTP to a tenth — " + ru (lufs ? lufs->fact : text::Fact {}));
        ok (isValue (left, text::Unit::None, 4) && isValue (readingOf (v.readings, ReadingKind::DcOffsetRight), text::Unit::None, 4)
            && ! readingOf (v.readings, ReadingKind::DcOffset)
            && sameBits (left->fact.args[0].number, numberOf (clipping, "dcOffset[0]").value_or (1.0))
            && isValue (runs, text::Unit::None, 0) && isValue (samplePeak, text::Unit::DbFs, 1)
            && isValue (readingOf (v.readings, ReadingKind::Correlation), text::Unit::None, 2)
            && isValue (readingOf (v.readings, ReadingKind::LongestRun), text::Unit::None, 0)
            && isValue (readingOf (v.readings, ReadingKind::ClippedSamples), text::Unit::None, 0)
            && isValue (readingOf (v.readings, ReadingKind::StereoWindows), text::Unit::None, 0),
            "a stereo source: the DC offset of each channel (no one-channel line), the clipping's numbers, the correlation and the stereo windows");
        bool ascending = v.readings.count > 0, master = false;
        for (std::size_t i = 0; i < v.readings.count; ++i)
        {
            ascending = ascending && (i == 0 || v.readings.items[i - 1].kind < v.readings.items[i].kind);
            master = master || v.readings.items[i].kind >= ReadingKind::Target;
        }
        ok (ascending && ! master, "one list, in the order of ReadingKind, each kind once — and no master's reading in the source's");
        const auto lufsRu = lufs ? ru (lufs->fact) : std::string(), lufsEn = lufs ? en (lufs->fact) : std::string();
        ok (lufsRu.find (',') != std::string::npos && lufsRu.find ('.') == std::string::npos && lufsEn.find ('.') != std::string::npos
            && lufsRu.ends_with (" LUFS") && lufsEn.ends_with (" LUFS")
            && ReadingText::name (ReadingKind::Integrated) == text::Term::ReadingIntegrated
            && ReadingText::name (ReadingKind::CheckPasses) == text::Term::ReadingCheckPasses,
            "Russian writes the decimal comma, English the point, both the unit the table gives: " + lufsRu + " / " + lufsEn);
        // A new source: the readings follow its measurement.
        const Mix quiet (0.03f, 4);
        ok (s->apply (quiet.load (3)).rejection == Rejection::None, "PRECONDITION: a quieter mix loads");
        ok (! readingOf (s->snapshot().view().readings, ReadingKind::Integrated), "the new source loaded: the old source's readings are gone");
        drive (*s);
        const auto again = s->snapshot();
        const auto* quieter = readingOf (again.view().readings, ReadingKind::Integrated);
        const auto quieterLufs = numberOf (again.view().measurements[std::size_t (Analyzer::Loudness)], "integratedLufs");
        ok (quieter && lufs && quieterLufs && sameBits (quieter->fact.args[0].number, *quieterLufs)
            && quieter->fact.args[0].number < lufs->fact.args[0].number - 20.0,
            "a new measurement: the readings follow it — the quieter mix's own loudness");
        // Across the wire: kind and fact, whole; an unknown kind is refused.
        std::string json (std::size_t (Codec::encodedBytes (again.view()).bytes), '\0');
        Snapshot restored;
        ok (Codec::encode (again.view(), json) == CodecStatus::Ok && Codec::decode (json, restored) == CodecStatus::Ok, "PRECONDITION: encoded and decoded");
        bool carried = restored.view().readings.count == again.view().readings.count && again.view().readings.count != 0;
        for (std::size_t i = 0; carried && i < again.view().readings.count; ++i)
            carried = restored.view().readings.items[i].kind == again.view().readings.items[i].kind
                   && sameLine (restored.view().readings.items[i].fact, again.view().readings.items[i].fact)
                   && sameBits (restored.view().readings.items[i].fact.args[0].number, again.view().readings.items[i].fact.args[0].number);
        const auto at = json.find ("\"readings\":[{\"fact\":{\"FactId\":");
        ok (carried && at != std::string::npos, "the readings cross the wire whole, kind and fact");
        if (at != std::string::npos)
        {
            const auto kindAt = json.find ("]},\"kind\":", at) + 10, kindEnd = json.find ('}', kindAt);
            auto badKind = json; badKind.replace (kindAt, kindEnd - kindAt, "31");
            auto goodKind = json; goodKind.replace (kindAt, kindEnd - kindAt, "30");
            Snapshot refused;
            ok (Codec::decode (badKind, refused) == CodecStatus::Invalid && Codec::decode (goodKind, refused) == CodecStatus::Ok,
                "an unknown reading kind (31) is refused; the last kind (30) is read");
        }
    }
}

void theNeedlesAreThePlansNeedles()
{
    felitronics::test::group ("\"the same ceiling\" is decided by the bits in both rules: requestNeedles and the plan agree");
    const Mix mix (1.0f, 4);
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    // An input whose ceiling on the target comes out exactly +0.0: loudness −10 LUFS, true peak +4 dBTP, a target of −1 dBTP
    // at −11 LUFS — a need of 4 dB, above the bound, and a ceiling of 4 − 4.
    static constexpr MeasurementValue numbers[] { { "integratedLufs", -10.0, MeasurementReason::None, 0 }, { "truePeakDb", 4.0, MeasurementReason::None, 0 } };
    detail::Inspector::loudness (s, numbers);
    command::EditTarget edit { 3, {} }; edit.fields.lufs = -11.0; edit.fields.tp = -1.0;
    ok (s.apply (edit).rejection == Rejection::None && detail::Inspector::ceiling (s) && sameBits (*detail::Inspector::ceiling (s), 0.0),
        "PRECONDITION: the needles are asked for at a ceiling of +0.0");
    drive (s);
    ok (detail::Inspector::current (s) && s.snapshot().view().plan.status == PlanStatus::Ready, "PRECONDITION: measured there, the plan ready");
    // The kept result says it was measured at −0.0: equal by value, another ceiling by its bits.
    detail::Inspector::measuredAt (s, -0.0);
    ok (! detail::Inspector::current (s), "the plan does not take a result of −0.0 for one of +0.0");
    detail::Inspector::request (s);
    ok (s.needlesJob() != 0 && sameBits (*detail::Inspector::ceiling (s), 0.0),
        "and requestNeedles does not either: it measures again at +0.0, so the plan's wait ends");
    drive (s);
    ok (detail::Inspector::current (s) && s.snapshot().view().plan.waiting == 0, "the needles are the plan's needles again");
}
} // namespace

void theAdviceIsAFact()
{
    felitronics::test::group ("the advice beside a knob is a fact of the plan — the value as it sounds against the norm; the target's note");
    const Mix mix;
    auto sp = measured (mix, "allStreaming"); auto& s = *sp;
    ok (s.apply (command::SetManual { 3, true }).rejection == Rejection::None && s.snapshot().view().plan.status == PlanStatus::Ready,
        "PRECONDITION: the panel open, the plan ready");
    const auto advice = [&] (std::uint16_t id) -> std::optional<PlanFact>
    {
        const auto snap = s.snapshot();
        const auto& facts = snap.view().plan.facts;
        for (std::size_t i = 0; i < facts.count; ++i)
            if (std::uint16_t (facts.items[i].fact.id) == id) return facts.items[i];
        return std::nullopt;
    };
    const auto noAdvice = [&]
    {
        for (std::uint16_t id = 500; id <= 509; ++id) if (advice (id)) return false;
        return true;
    };
    const auto at = [] (const std::optional<PlanFact>& f, std::size_t i) { return f ? f->fact.args[i].number : -1.0; };
    const auto hpfAt = [&] (double fq, std::int32_t slope, bool on = true)
    {
        HpfFields<Touched> hpf; hpf.on = on; hpf.fq = fq; hpf.slope = slope;
        return edit (s, hpf);
    };
    const auto machineHz = s.snapshot().view().plan.hpf.soundingHz;
    ok (noAdvice(), "the machine's own plan raises no advice, wherever its cutoff sits (owner, 01.10) — "
        + std::to_string (machineHz) + " Hz at its slope");
    // THE RULE ITSELF, on the finding: a machine value — proposed now, or kept from a file — is never advised against,
    // however far outside the window; the same value set by a person is.
    {
        HpfFinding hpf; hpf.soundingHz = 50.0; hpf.soundingSlope = 6;
        MonoBassFinding mono; mono.soundingHz = 250.0;
        bool machineSilent = true, handSpeaks = true;
        for (const auto by : { Sounding::Proposal, Sounding::File, Sounding::Off })
        {
            hpf.sounding = by; mono.sounding = by;
            machineSilent = machineSilent && ! PlanText::hpfCutoffAdvice (hpf) && ! PlanText::hpfSlopeAdvice (hpf)
                && ! PlanText::monoBassAdvice (mono);
        }
        hpf.sounding = Sounding::Hand; mono.sounding = Sounding::Hand;
        const auto cut = PlanText::hpfCutoffAdvice (hpf), slope = PlanText::hpfSlopeAdvice (hpf);
        const auto zone = PlanText::monoBassAdvice (mono);
        handSpeaks = cut && cut->id == text::FactId::HpfAboveComfort && slope && slope->id == text::FactId::HpfSlopeGentle
            && zone && zone->id == text::FactId::MonoBassOutsideZones;
        ok (machineSilent, "a machine value outside the window (a cutoff at 50 Hz, 6 dB/oct, mono bass at 250 Hz): no advice");
        ok (handSpeaks, "the same values set by a person: 501, 502 and 505");
    }
    // THE HIGH-PASS: the cutoff against the comfort window (strictly), the slope against the normal ones.
    ok (hpfAt (20.0, 24), "PRECONDITION: a hand high-pass at 20 Hz");
    const auto below = advice (500);
    ok (below && below->device == Device::Hpf && at (below, 0) == 20.0 && at (below, 1) == 24.0 && at (below, 2) == 42.0
        && ru (below->fact).find ("24–42") != std::string::npos && ! advice (501) && ! advice (502) && ! advice (503),
        "below the window: its why, with the window — " + (below ? ru (below->fact) + " / " + en (below->fact) : std::string ("none")));
    ok (hpfAt (45.0, 24) && advice (501) && at (advice (501), 0) == 45.0 && ! advice (500),
        "above the window: its why — " + (advice (501) ? ru (advice (501)->fact) : std::string ("none")));
    ok (hpfAt (24.0, 24) && noAdvice() && hpfAt (42.0, 24) && noAdvice() && hpfAt (30.0, 12) && noAdvice(),
        "inside the window, both edges included, at a normal slope: none");
    const auto gentle = (hpfAt (30.0, 6), advice (502));
    ok (gentle && gentle->fact.args[0].integer == 6 && gentle->fact.args[1].integer == 12 && ! advice (500) && ! advice (501),
        "a slope gentler than the gentlest normal one: its why — " + (gentle ? ru (gentle->fact) : std::string ("none")));
    const auto steep = (hpfAt (30.0, 48), advice (503));
    ok (steep && steep->fact.args[0].integer == 48 && steep->fact.args[1].integer == 24,
        "a slope steeper than the steepest normal one: its why — " + (steep ? ru (steep->fact) : std::string ("none")));
    ok (hpfAt (30.0, 18) && noAdvice(), "a slope between two normal ones is inside the norm");
    ok (hpfAt (20.0, 48) && advice (500) && advice (503), "a cutoff and a slope both outside: both whys");
    ok (hpfAt (20.0, 48, false) && noAdvice(), "a high-pass out of the chain: no advice, whatever its knob");
    ok (hpfAt (32.0, 24), "PRECONDITION: the high-pass back in the norm");
    // Per value: a person's slope says nothing of the cutoff beside it, and back. The cutoff's hand goes (revertEdits on
    // fq alone) while the slope's stays: no cutoff advice, whatever the machine's cutoff.
    {
        HpfFields<Touched> hand; hand.on = true; hand.fq = 20.0; hand.slope = 6;
        HpfFields<Mark> fq; fq.fq = true;
        const bool both = edit (s, hand) && advice (500) && advice (502);
        const bool reverted = s.apply (command::RevertEdits { 900, fq }).rejection == Rejection::None;
        ok (both && reverted && ! advice (500) && ! advice (501) && advice (502),
            "a slope by hand with the machine's cutoff: the slope's advice alone");
        HpfFields<Mark> slope; slope.slope = true;
        ok (hpfAt (20.0, 6) && s.apply (command::RevertEdits { 901, slope }).rejection == Rejection::None
            && advice (500) && ! advice (502) && ! advice (503), "a cutoff by hand with the machine's slope: the cutoff's alone");
        ok (hpfAt (32.0, 24), "PRECONDITION: the high-pass back in the norm");
    }
    // MONO BASS: the crossover outside every destination's zone, both ends of a zone inside.
    const auto monoAt = [&] (double fq) { MonoBassFields<Touched> mono; mono.on = true; mono.fq = fq; return edit (s, mono); };
    const auto outside = (monoAt (70.0), advice (505));
    const auto belowAll = (monoAt (70.0), advice (509));
    ok (belowAll && ! advice (505) && belowAll->device == Device::MonoBass && at (belowAll, 0) == 70.0 && at (belowAll, 1) == 80.0
        && at (belowAll, 2) == 120.0,
        "a crossover below every zone: lower than a club or vinyl asks for — " + (belowAll ? ru (belowAll->fact) : std::string ("none")));
    const auto aboveAll = (monoAt (250.0), advice (505));
    ok (aboveAll && ! advice (509) && at (aboveAll, 0) == 250.0 && at (aboveAll, 1) == 120.0 && at (aboveAll, 2) == 200.0
        && ru (aboveAll->fact).find ("выше, чем нужно и клубу") != std::string::npos,
        "above every zone: higher than a club and vinyl need — " + (aboveAll ? ru (aboveAll->fact) : std::string ("none")));
    ok (monoAt (100.0) && ! advice (505) && ! advice (509) && monoAt (80.0) && ! advice (509) && monoAt (200.0) && ! advice (505)
        && monoAt (160.0) && ! advice (505), "inside a zone, its ends included: none");
    ok (monoAt (120.0), "PRECONDITION: mono bass back at a zone");
    // THE EQ CURVE: the shelves as they sound, against [eq] curve.warnDb, at the point of the largest |dB|.
    HpfFields<Touched> off; off.on = false;
    TiltFields<Touched> tilt; tilt.on = true; tilt.db = 3.0;
    ok (edit (s, off) && edit (s, tilt), "PRECONDITION: the high-pass off, tilt by hand at +3 dB");
    const auto snap = s.snapshot();
    EqPoint peak {};
    for (const auto& p : snap.view().eqCurve) if (std::abs (p.db) > std::abs (peak.db)) peak = p;
    const auto over = advice (504);
    ok (over && over->device == Device::Tilt && sameBits (at (over, 0), peak.db) && sameBits (at (over, 1), peak.hz) && std::abs (peak.db) > 2.0,
        "the summed shelves leave ±2 dB: the overshoot, with the curve's own point — " + (over ? ru (over->fact) + " / " + en (over->fact) : std::string ("none")));
    TiltFields<Touched> mild; mild.on = true; mild.db = 1.5;
    ok (edit (s, mild) && ! advice (504), "tilt at 1.5 dB stays inside: none");
    TiltFields<Touched> flat; flat.on = true; flat.db = 0.0;
    LowFields<Touched> low; low.on = true; low.db = 2.5;
    const auto lowOver = (edit (s, flat), edit (s, low), advice (504));
    ok (lowOver && lowOver->device == Device::Low && at (lowOver, 0) > 2.0, "the low shelf alone past it: said of low — "
        + (lowOver ? ru (lowOver->fact) : std::string ("none")));
    // THE TARGET'S NOTE, from targets.toml [notes], beside the snapshot's target.
    const auto note = [] (std::string_view key) { return SnapshotText::targetNote (key); };
    ok (note ("youtubeMusic") && note ("youtubeMusic")->id == text::FactId::TargetMeasured && note ("youtubeMusic")->args[0].number == -7.0
        && note ("club") && note ("club")->id == text::FactId::TargetPractice && note ("club")->args[0].number == -8.0
        && note ("cdDynamic") && note ("cdDynamic")->id == text::FactId::TargetPractice
        && note ("bandcamp") && note ("bandcamp")->id == text::FactId::TargetNoNormalisation
        && ! note ("allStreaming") && ! note ("spotify") && ! note ("td1008") && ! note ("nowhere"),
        "measured, practice, no normalisation — and nothing for a platform's published number or a standard: "
            + ru (*note ("youtubeMusic")) + " / " + ru (*note ("club")) + " / " + ru (*note ("bandcamp")));
    ok (! s.snapshot().view().targetNote, "allStreaming's snapshot carries no note");
    auto tp = measured (mix, "youtubeMusic"); auto& t = *tp;
    HpfFields<Touched> hand; hand.fq = 20.0; hand.slope = 48;
    ok (t.apply (command::SetManual { 3, true }).rejection == Rejection::None && edit (t, hand), "PRECONDITION: youtubeMusic, a hand high-pass");
    // Across the wire: the advice, the sounding slope and the note, whole.
    const auto again = t.snapshot();
    std::string json (std::size_t (Codec::encodedBytes (again.view()).bytes), '\0');
    Snapshot restored;
    ok (Codec::encode (again.view(), json) == CodecStatus::Ok && Codec::decode (json, restored) == CodecStatus::Ok, "PRECONDITION: encoded and decoded");
    bool carried = restored.view().plan.facts.count == again.view().plan.facts.count && again.view().targetNote && restored.view().targetNote
        && sameLine (*restored.view().targetNote, *again.view().targetNote) && restored.view().plan.hpf.soundingSlope == 48;
    for (std::size_t i = 0; carried && i < again.view().plan.facts.count; ++i)
        carried = restored.view().plan.facts.items[i].device == again.view().plan.facts.items[i].device
               && sameLine (restored.view().plan.facts.items[i].fact, again.view().plan.facts.items[i].fact);
    ok (carried && again.view().plan.facts.count > 2, "the codec carries the advice, the sounding slope and the target's note whole");
}

// THE MACHINE KEEPS ITS OWN NORM: the advice "beyond the norm" (a high-pass slope outside slopesNormal, an EQ curve past
// its warning, mono bass outside every destination's zone) is said of a person's value — the machine's own plan never
// raises it. Every target of targets.toml, on every input the suite measures: the contract's fixture inputs
// (tools/contract/fixtures/inputs.json, "measure" and "measure-stereo", built here by the same formula) and the synthetic
// mixes of this file. The comfort window of the high-pass (500, 501) is in the list too: the machine's floor may sit
// outside it by an owner decision, and its advice is still a person's alone (owner, 01.10).
struct Input
{
    std::string name;
    std::uint32_t channels = 0, frames = 0, rate = 0;
    std::vector<float> samples;
    const float* planes[2] { nullptr, nullptr };
    void point() { for (std::uint32_t c = 0; c < channels; ++c) planes[c] = samples.data() + std::size_t (c) * frames; }
};
Input contractInput (const char* name, std::uint32_t rate, std::uint32_t channels, std::uint32_t frames, std::uint32_t period, double scale)
{
    // fixtures.mjs: sample i of the planar block is ((i % frames + floor (i / frames) * 7) % period - period / 2) * scale,
    // in 16-bit units.
    Input in { name, channels, frames, rate, std::vector<float> (std::size_t (channels) * frames) };
    for (std::size_t i = 0; i < in.samples.size(); ++i)
    {
        const double v = (double ((i % frames + (i / frames) * 7) % period) - double (period) / 2.0) * scale;
        in.samples[i] = float (v / 32768.0);
    }
    in.point();
    return in;
}
// `wide`: a 70 Hz bass under the mix, the right channel's an eighth of a cycle late — a wide low end the machine folds.
Input mixInput (const char* name, float scale, double clicks, std::uint32_t channels, bool wide = false)
{
    namespace det = felitronics::core::det;
    const Mix mix (scale, 8, clicks);
    Input in { name, channels, mix.frames, Mix::rate, std::vector<float> (std::size_t (channels) * mix.frames) };
    std::copy (mix.left.begin(), mix.left.end(), in.samples.begin());
    if (channels == 2) std::copy (mix.right.begin(), mix.right.end(), in.samples.begin() + mix.frames);
    for (std::uint32_t i = 0; wide && channels == 2 && i < mix.frames; ++i)
    {
        const double t = double (i) / Mix::rate;
        in.samples[i] += float (0.2 * det::sin (2 * kPi * 70.0 * t));
        in.samples[std::size_t (mix.frames) + i] += float (0.2 * det::sin (2 * kPi * 70.0 * t + 0.25 * kPi));
    }
    in.point();
    return in;
}

void theMachineKeepsItsOwnNorm()
{
    felitronics::test::group ("the machine's own plan raises no advice beyond the norm — every target, every input the suite has");
    constexpr std::uint16_t kBeyondNorm[] = { std::uint16_t (text::FactId::HpfBelowComfort), std::uint16_t (text::FactId::HpfAboveComfort),
        std::uint16_t (text::FactId::HpfSlopeGentle), std::uint16_t (text::FactId::HpfSlopeSteep),
        std::uint16_t (text::FactId::EqOvershoot), std::uint16_t (text::FactId::MonoBassOutsideZones),
        std::uint16_t (text::FactId::MonoBassBelowZones) };
    std::vector<Input> inputs;
    inputs.push_back (contractInput ("contract measure", 48000, 1, 384000, 128, 128));
    inputs.push_back (contractInput ("contract measure-stereo", 48000, 2, 192000, 128, 128));
    inputs.push_back (mixInput ("mix", 1.0f, 0.35, 2));
    inputs.push_back (mixInput ("mix quiet", 0.05f, 0.35, 2));
    inputs.push_back (mixInput ("mix hot", 2.0f, 0.35, 2));
    inputs.push_back (mixInput ("mix without needles", 1.0f, 0.0, 2));
    inputs.push_back (mixInput ("mix mono", 1.0f, 0.35, 1));
    inputs.push_back (mixInput ("mix with a wide bass", 1.0f, 0.35, 2, true));
    const auto& rules = detail::rules();
    unsigned plans = 0, hpfSounds = 0, monoSounds = 0, monoBeyondVinyl = 0;
    std::string raised;
    bool ready = rules.rows > 20;
    for (const auto& in : inputs)
    {
        auto s = Session::create().session;
        CommandId id = 1;
        (void) s->apply (command::SetTarget { id++, rules.row (0).key });
        const command::Load load { id++, { in.planes, in.channels, in.frames, in.rate }, { "input.wav", in.rate, true, 24 } };
        if (s->apply (load).rejection != Rejection::None) { ready = false; raised += "\n        " + in.name + ": not loaded"; continue; }
        for (std::uint16_t row = 0; row < rules.rows; ++row)
        {
            const auto key = rules.row (row).key;
            (void) s->apply (command::SetTarget { id++, key });
            drive (*s);
            const auto snap = s->snapshot();
            const auto& plan = snap.view().plan;
            if (plan.status != PlanStatus::Ready) { ready = false; raised += "\n        " + in.name + " / " + std::string (key) + ": no plan"; continue; }
            ++plans;
            hpfSounds += plan.hpf.sounding != Sounding::Off ? 1u : 0u;
            monoSounds += plan.monoBass.sounding != Sounding::Off ? 1u : 0u;
            monoBeyondVinyl += plan.monoBass.sounding != Sounding::Off && ! rules.row (row).vinyl ? 1u : 0u;
            for (std::size_t i = 0; i < plan.facts.count; ++i)
                for (const auto beyond : kBeyondNorm)
                    if (std::uint16_t (plan.facts.items[i].fact.id) == beyond)
                        raised += "\n        " + in.name + " / " + std::string (key) + ": " + ru (plan.facts.items[i].fact);
        }
    }
    // Not vacuous: the high-pass sounds on every plan, mono bass on some beyond vinyl's — the advice has a machine value
    // to judge on every target.
    ok (ready && plans == inputs.size() * rules.rows && hpfSounds == plans && monoBeyondVinyl >= rules.rows - 1u,
        "PRECONDITION: every input planned on every target (" + std::to_string (plans) + " plans; the high-pass sounds on "
        + std::to_string (hpfSounds) + ", mono bass on " + std::to_string (monoSounds) + ")" + (ready ? std::string {} : raised));
    ok (raised.empty(), "no plan of the machine's own says its choice is beyond the norm" + raised);
}

// THE LIMITER'S BUDGET BY THE TARGET'S LOUDNESS ([landing] limiterBudget): below −10 LUFS 4 dB, from −10 to −8 (both
// ends) 7 dB, louder 7.5 dB — for every target row by its own loudness and for any loudness a person types.
void theLimiterBudgetRule()
{
    felitronics::test::group ("the limiter's budget follows the target's loudness, a person's edit included");
    const auto engine = detail::rules().engine;
    const auto at = [&] (double lufs) { return detail::limiterBudgetDb (engine, lufs); };
    const auto is = [&] (double lufs, double budget) { return felitronics::core::exactlyEqual (at (lufs), budget); };
    ok (is (-10.01, 4.0) && is (-10.0, 7.0) && is (-8.0, 7.0) && is (-7.99, 7.5),
        "the borders: −10.01 LUFS 4 dB, −10 and −8 7 dB, −7.99 7.5 dB");
    ok (is (-24.0, 4.0) && is (-23.0, 4.0) && is (-14.0, 4.0) && is (-11.0, 4.0) && is (-9.0, 7.0) && is (-5.0, 7.5),
        "broadcast −24 and −23, streaming −14 and −11: 4 dB; −9: 7 dB; −5: 7.5 dB");
    const auto rules = detail::rules();
    bool rows = true;
    for (std::uint16_t i = 0; i < rules.rows; ++i)
    {
        const double lufs = rules.row (i).lufs.toDouble();
        rows = rows && is (lufs, lufs < -10.0 ? 4.0 : lufs <= -8.0 ? 7.0 : 7.5);
    }
    ok (rows, "every target row by its own loudness");
    ok (std::isnan (at (std::numeric_limits<double>::quiet_NaN())), "no loudness, no budget");
}

int main (int argc, char** argv)
{
    // The guard over every target and input is its own ctest (felitronics_session_machine_norm_tests): a long enumeration
    // the slow checked build may leave out.
    if (argc > 1 && std::string_view (argv[1]) == "machine-norm")
    {
        std::printf ("felitronics::session — the machine's own plan keeps its own norm\n");
        theMachineKeepsItsOwnNorm();
        return felitronics::test::report();
    }
    std::printf ("felitronics::session — the limiter, the dither and the whole plan sounding\n");
    theClasses();
    theCutIsAnAmount();
    theNeedAndTheMeasurement();
    thePersonsThreshold();
    theDither();
    vinylOnThePlanner();
    aQuietInput();
    theRenderIsThePreviousPaths();
    theClipperCutsItsAmount();
    theTopologyFollowsTheTicks();
    aMasterThatWaited();
    theMemoryOfAMaster();
    theLimiterBudgetRule();
    vinylAndQuietMastered();
    theDitherSounding();
    theObservations();
    theSourceReport();
    theOwnersTable();
    theObservationsSpeakForThemselves();
    theReadingsAreFacts();
    theNeedlesAreThePlansNeedles();
    theAdviceIsAFact();
    return felitronics::test::report();
}
