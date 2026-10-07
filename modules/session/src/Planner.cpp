// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE PLANNER (src/Planner.h): one Planned<> per device, walked in the order of Device. Every proposal starts from the
// config's defaults for the target and the source (src/Devices.h) and says where each field came from; what a device
// reads follows from the settings the project gives it.

#include "BuildGuards.h"

#include "Planner.h"
#include <felitronics/session/Kit.h>
#include "Devices.h"
#include "Grid.h"
#include "Rules.h"
#include "BuildContract.h"
#include "Dynamics.h"
#include "EqCurve.h"
#include "Limiter.h"
#include "MeasurementPlan.h"
#include "Needles.h"
#include "Observations.h"
#include "SourceMeasurements.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

namespace felitronics::session::detail
{
namespace
{
// A field's bit in DevicePlan::target / measured: its place among the device's fields, in the order Project.h writes them.
template <class Fields, class M> std::uint8_t fieldBit (const Rules& rules, const Fields& fields, const M& member) noexcept
{
    std::uint8_t bit = 0;
    DeviceOf<Fields>::each (rules, [&] (std::uint8_t i, const FieldRule&, const auto& f)
    {
        if (static_cast<const void*> (&f) == static_cast<const void*> (&member)) bit = std::uint8_t (1u << i);
    }, fields);
    return bit;
}

bool offeredByShell (const PlanInputs& in, Device device) noexcept { return (in.offered & (1u << unsigned (device))) != 0; }
// The source's low-end run measured at `hz` — the main run's crossover (lowEnd.run.crossoverHz) or 150 — if there is one.
std::optional<Analyzer> lowEndRunAt (const PlanInputs& in, double hz) noexcept
{
    const Pcm pcm { nullptr, in.channels, in.frames, in.sampleRate };
    const auto params = MeasurementPlan::parametersFor (pcm);
    if (same (params.lowEnd.crossoverHz, hz)) return Analyzer::LowEnd;
    if (same (params.lowEnd150.crossoverHz, hz)) return Analyzer::LowEnd150;
    return std::nullopt;
}
// Has a result a device reads not ended yet — the source's run still to come, or stopped?
bool pendingResult (const MeasurementResult* r) noexcept { return r && ! ended (*r); }

double configured (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    storageOverflow();
}
bool shortInput (const PlanInputs& in) noexcept
{
    return double (in.frames) < configured (in.rules.engine.find ("input").find ("shortSeconds")) * double (in.sampleRate);
}
// Is the input long enough to hold the bass mono bass weighs ([monoBass] loss.soundingAtLeastS)?
bool longEnoughToWeigh (const PlanInputs& in) noexcept
{
    return double (in.frames) >= configured (in.rules.engine.find ("monoBass").find ("loss").find ("soundingAtLeastS")) * double (in.sampleRate);
}
const MeasurementResult* resultOf (const PlanInputs& in, Analyzer analyzer) noexcept
{
    const auto i = std::size_t (analyzer);
    return i < in.measurements.size() ? &in.measurements[i] : nullptr;
}
std::optional<double> scalar (const MeasurementResult& r, std::string_view name) noexcept
{
    if (r.status == MeasurementStatus::Ready)
        for (const auto& v : r.numbers)
            if (v.name == name && v.value && std::isfinite (*v.value)) return v.value;
    return {};
}
const MeasurementArray* arrayOf (const MeasurementResult& r, std::string_view name) noexcept
{
    if (r.status == MeasurementStatus::Ready)
        for (const auto& a : r.arrays)
            if (a.name == name) return &a;
    return nullptr;
}
bool quiet (const PlanInputs& in) noexcept { return quietInput (in); }

// THE SURE LOWEST NOTE (owner decision 3.2, as written), from the first phase's low-end run: the LOWEST BAND THAT WAS ON
// AT ALL from [lowEnd] lowestNoteFromHz up decides, and that band alone — a band under it, which the table measures from
// 10 Hz for the spectrum, is skipped (owner, 06.10). It is a sure note when it is on in at least [lowEnd] occupiedFromDuty of the
// frames, is resolved, carries a valid note reading, stands [lowEnd] occupiedMarginWhenOnDb above the duty line and is where
// the low end starts — the band under it, under lowestNoteFromHz included, never on — while the range holds [lowEnd]
// noteRangeShareAtLeastDb of the programme (owner, 07.10, both: a mix with no bass is unsure; real mixes keep their notes), lies
// above [hpf] note.aboveHz and sounds [hpf] note.soundingAtLeastS in all (its frames times the hop). A lowest band that
// fails any of it — a rare 808, one thump — is no note, and the cutoff is the target's floor: the detector never takes
// a higher band as "the note", because a note above the true one cuts music.
struct Note { std::int32_t midi = 0; double hz = 0.0; };
std::optional<Note> sureLowestNote (const PlanInputs& in, const MeasurementResult& lowEnd) noexcept
{
    const auto* bands = arrayOf (lowEnd, "bands");
    const auto hop = scalar (lowEnd, "hopSamples"), rate = scalar (lowEnd, "sampleRate");
    const auto share = scalar (lowEnd, "bandRangeShare");
    if (! bands || bands->columns != 16 || ! hop || ! rate || ! (*rate > 0) || ! scalar (lowEnd, "peakMidi") || ! share) return {};
    const auto low = in.rules.engine.find ("lowEnd");
    const auto note = in.rules.engine.find ("hpf").find ("note");
    const double dutyFrom = configured (low.find ("occupiedFromDuty")), margin = configured (low.find ("occupiedMarginWhenOnDb"));
    const double fromHz = configured (low.find ("lowestNoteFromHz"));
    if (! (*share >= core::det::pow10 (configured (low.find ("noteRangeShareAtLeastDb")) / 10.0))) return {};   // a veto, as above
    const double aboveHz = configured (note.find ("aboveHz")), sounding = configured (note.find ("soundingAtLeastS"));
    const auto rows = std::size_t (bands->stored);
    for (std::size_t b = 0; b < rows && (b + 1) * 16 <= bands->values.size(); ++b)
    {
        const double* row = bands->values.data() + b * 16;
        const double count = row[11], duty = row[12];
        if (! (row[1] >= fromHz) || ! (count > 0)) continue;
        const bool bottom = b == 0 || ! (bands->values[(b - 1) * 16 + 11] > 0);   // the band under it never on
        const bool sure = duty >= dutyFrom && row[15] > 0.5 && row[14] >= margin && bottom && row[1] > aboveHz
                       && count * *hop / *rate >= sounding;
        if (! sure) return {};
        return Note { std::int32_t (row[0]), row[1] };
    }
    return {};
}

// THE LOSS OF THE LOW END FOLDED TO MONO (owner decision 3.5): the side's share of the low band where the bass sounds —
// 10·log10((mid + side) / mid) over the 10 ms blocks of the low-end run at the target's crossover whose low-band level is
// within [monoBass] loss.soundingWithinDb of the level the loudest 5 % of the blocks reach (by duration), when those
// blocks last [monoBass] loss.soundingAtLeastS in all. Nothing that sounds, or too little of it: no reading.
struct Weighed { double lossDb = 0.0, seconds = 0.0; };
// The run keeps a bounded number of blocks: a piece longer than they cover has only its first part in them. The loss is
// weighed over that part — mono bass stands unless a measured loss rules against it — and the finding says how many
// seconds of the piece the weighing covered.
std::optional<double> coveredPart (const MeasurementResult& lowEnd) noexcept
{
    const auto* blocks = arrayOf (lowEnd, "blocks");
    const auto rate = scalar (lowEnd, "sampleRate");
    if (! blocks || blocks->columns != 8 || ! rate || ! (*rate > 0) || lowEnd.status != MeasurementStatus::Ready) return {};
    const auto rows = std::min<std::size_t> (std::size_t (blocks->stored), blocks->values.size() / 8);
    if ((blocks->complete && blocks->stored >= blocks->total) || rows == 0) return {};
    const double* last = blocks->values.data() + (rows - 1) * 8;
    return (last[0] + last[1]) / *rate;
}
std::optional<Weighed> weighMonoLoss (const PlanInputs& in, const MeasurementResult& lowEnd) noexcept
{
    const auto* blocks = arrayOf (lowEnd, "blocks");
    const auto rate = scalar (lowEnd, "sampleRate");
    if (! blocks || blocks->columns != 8 || ! rate || ! (*rate > 0)) return {};
    const auto loss = in.rules.engine.find ("monoBass").find ("loss");
    const double within = configured (loss.find ("soundingWithinDb")), atLeast = configured (loss.find ("soundingAtLeastS"));
    const auto rows = std::min<std::size_t> (std::size_t (blocks->stored), blocks->values.size() / 8);
    const auto level = [&] (const double* row) noexcept -> std::optional<double>
    {
        const double samples = row[1], energy = row[4] + row[5];
        if (! (row[6] > 0.5) || ! (samples > 0) || ! (energy > 0) || ! std::isfinite (energy)) return {};
        return 10 * core::det::log10 (energy / samples);
    };
    // The loud reference: the level the loudest 5 % of the sounding duration reaches, on a half-decibel grid.
    constexpr double lowest = -240.0, step = 0.5;
    constexpr std::size_t bins = 560;
    double histogram[bins] {};
    double duration = 0.0;
    for (std::size_t i = 0; i < rows; ++i)
        if (const auto l = level (blocks->values.data() + i * 8))
        {
            const double at = std::clamp ((*l - lowest) / step, 0.0, double (bins - 1));
            histogram[std::size_t (at)] += blocks->values[i * 8 + 1];
            duration += blocks->values[i * 8 + 1];
        }
    if (! (duration > 0)) return {};
    double above = 0.0, reference = lowest;
    for (std::size_t b = bins; b-- > 0;)
    {
        above += histogram[b];
        if (above >= 0.05 * duration) { reference = lowest + double (b) * step; break; }
    }
    double mid = 0.0, side = 0.0, samples = 0.0;
    for (std::size_t i = 0; i < rows; ++i)
    {
        const double* row = blocks->values.data() + i * 8;
        if (const auto l = level (row); l && *l >= reference - within)
        { mid += row[4]; side += row[5]; samples += row[1]; }
    }
    const double seconds = samples / *rate;
    if (! (seconds >= atLeast) || ! (mid + side > 0)) return {};
    return Weighed { mid > 0 ? 10 * core::det::log10 ((mid + side) / mid) : std::numeric_limits<double>::infinity(), seconds };
}

} // namespace

bool quietInput (const PlanInputs& in) noexcept
{
    const auto i = std::size_t (Analyzer::Loudness);
    if (i >= in.measurements.size() || in.measurements[i].status != MeasurementStatus::Ready) return false;
    const auto gainOnly = in.rules.engine.find ("input").find ("quiet").find ("gainOnlyLufs");
    const double below = gainOnly.decimal() ? gainOnly.decimal()->toDouble() : double (gainOnly.integer().value_or (0));
    for (const auto& v : in.measurements[i].numbers)
        if (v.name == "integratedLufs" && v.value && std::isfinite (*v.value)) return *v.value < below;
    return false;
}

MonoBassVerdict monoBassVerdictFor (const Rules& rules, double lossDb) noexcept
{
    const auto config = rules.engine.find ("monoBass").find ("loss");
    return lossDb < configured (config.find ("warnFromDb")) ? MonoBassVerdict::On
         : lossDb <= configured (config.find ("offAboveDb")) ? MonoBassVerdict::Partial : MonoBassVerdict::AntiPhase;
}

namespace
{
template <class Fields> struct Planned;

// [hpf] (owner decisions 3.2–3.4): on always; the cutoff max(what the sure lowest note allows, the target's floor), never
// above the machine's top ([hpf] machineTopHz — not the knob's travel, which goes higher); the slope the target's.
template <> struct Planned<HpfFields<Value>>
{
    static void propose (const PlanInputs& in, HpfFields<Value>& m, DevicePlan& plan, PlanFindings& found) noexcept
    {
        const TargetRow target = in.rules.row (in.row);
        const double floor = target.hpfFloor.toDouble(), top = in.rules.hpfTop.toDouble();
        plan.target |= fieldBit (in.rules, m, m.slope);
        HpfFinding& f = found.hpf;
        f = {};
        f.cutoffHz = floor;
        const auto* lowEnd = resultOf (in, Analyzer::LowEnd);
        const double rate = double (in.sampleRate);
        if (! lowEnd || ! (rate > 0)) f.cut = HpfCut::Unmeasured;
        else if (quiet (in)) f.cut = HpfCut::Quiet;
        else if (shortInput (in)) f.cut = HpfCut::Short;
        else if (pendingResult (lowEnd))
        {
            // Not measured yet: the floor holds the place, no bit says where it came from — the field is pending.
            f.cut = HpfCut::Unmeasured;
            m.fq = kept (f.cutoffHz);
            plan.pending |= fieldBit (in.rules, m, m.fq);
            plan.heldBack = HeldBack::Pending;
            return;
        }
        else if (lowEnd->status != MeasurementStatus::Ready) f.cut = HpfCut::Unmeasured;
        else if (const auto note = sureLowestNote (in, *lowEnd); ! note) f.cut = HpfCut::Unsure;
        else
        {
            const double allowed = highPassCutoffFor (note->hz, target.noteLossDb.toDouble(), m.slope, rate);
            f.cut = allowed < floor ? (note->hz < floor ? HpfCut::BelowFloor : HpfCut::Floor) : allowed > top ? HpfCut::Top : HpfCut::Note;
            f.cutoffHz = f.cut == HpfCut::Floor || f.cut == HpfCut::BelowFloor ? floor : f.cut == HpfCut::Top ? top : allowed;
            f.noteMidi = note->midi;
            f.noteHz = note->hz;
            f.noteLossDb = highPassLossDb (f.cutoffHz, m.slope, rate, note->hz);
        }
        m.fq = kept (f.cutoffHz);
        plan.measured |= f.cut == HpfCut::Note || f.cut == HpfCut::Top ? fieldBit (in.rules, m, m.fq) : std::uint8_t (0);
        plan.target |= f.cut == HpfCut::Note || f.cut == HpfCut::Top ? std::uint8_t (0) : fieldBit (in.rules, m, m.fq);
        if (f.cut == HpfCut::Unmeasured) plan.heldBack = HeldBack::Unmeasured;
    }
    // The machine's cutoff reads the low end — where it searches at all: a ticked device the shell offers, on an input
    // neither too quiet nor too short.
    static std::uint32_t needs (const PlanInputs& in, const HpfFields<Value>& hpf) noexcept
    {
        return hpf.on && offeredByShell (in, Device::Hpf) && in.sampleRate != 0 && ! quiet (in) && ! shortInput (in)
            ? bitOf (Analyzer::LowEnd) : 0u;
    }
};

// [monoBass] (owner decision 3.5): at the target's crossover, placed by the loss the low end takes folded to mono.
template <> struct Planned<MonoBassFields<Value>>
{
    static void propose (const PlanInputs& in, MonoBassFields<Value>& m, DevicePlan& plan, PlanFindings& found) noexcept
    {
        plan.target |= fieldBit (in.rules, m, m.fq);
        MonoBassFinding& f = found.monoBass;
        f = {};
        f.crossoverHz = m.fq;
        // The low-end run whose crossover is the target's: the one measured at 120 Hz, or the one at 150.
        const MeasurementResult* lowEnd = nullptr;
        for (const auto analyzer : { Analyzer::LowEnd, Analyzer::LowEnd150 })
            if (const auto* r = resultOf (in, analyzer); r)
                if (const auto at = scalar (*r, "crossoverHz"); at && same (*at, m.fq)) lowEnd = r;
        std::optional<Weighed> weighed;
        const auto run = lowEndRunAt (in, m.fq);
        if (! offered (in.rules, in.row, in.channels, Device::MonoBass)) f.verdict = MonoBassVerdict::MonoSource;
        else if (in.measurements.empty()) f.verdict = MonoBassVerdict::Unmeasured;
        else if (quiet (in)) f.verdict = MonoBassVerdict::Quiet;
        else if (run && longEnoughToWeigh (in) && pendingResult (resultOf (in, *run)))
        {
            // Not measured yet: left out until the run at the target's crossover ends, the tick pending.
            f.verdict = MonoBassVerdict::Unmeasured;
            m.on = false;
            plan.pending |= fieldBit (in.rules, m, m.on);
            plan.heldBack = HeldBack::Pending;
            return;
        }
        else if (weighed = lowEnd ? weighMonoLoss (in, *lowEnd) : std::nullopt; ! weighed) f.verdict = MonoBassVerdict::Unmeasured;
        else
        {
            f.lossDb = weighed->lossDb;
            f.soundingSeconds = weighed->seconds;
            f.verdict = monoBassVerdictFor (in.rules, weighed->lossDb);
            if (in.sampleRate != 0)
                if (f.coveredSeconds = coveredPart (*lowEnd); f.coveredSeconds) f.pieceSeconds = double (in.frames) / double (in.sampleRate);
        }
        switch (f.verdict)
        {
            case MonoBassVerdict::On:
            case MonoBassVerdict::Partial:    m.on = true; plan.measured |= fieldBit (in.rules, m, m.on); break;
            case MonoBassVerdict::AntiPhase:  m.on = false; plan.measured |= fieldBit (in.rules, m, m.on); plan.heldBack = HeldBack::Measured; break;
            case MonoBassVerdict::Unmeasured: m.on = false; plan.heldBack = HeldBack::Unmeasured; break;
            case MonoBassVerdict::Quiet:      m.on = false; plan.heldBack = HeldBack::Quiet; break;
            case MonoBassVerdict::MonoSource: m.on = false; plan.heldBack = HeldBack::Source; break;
        }
    }
    // The machine's tick is weighed on the low-end run at the target's crossover — whatever the tick is now, which that
    // run decides: a device offered for the source and by the shell, on an input not too quiet and long enough to hold
    // the bass it weighs ([monoBass] loss.soundingAtLeastS).
    static std::uint32_t needs (const PlanInputs& in, const MonoBassFields<Value>&) noexcept
    {
        if (! offeredByShell (in, Device::MonoBass) || ! offered (in.rules, in.row, in.channels, Device::MonoBass) || quiet (in)
            || ! longEnoughToWeigh (in)) return 0u;
        const auto run = lowEndRunAt (in, in.rules.row (in.row).monoBass.toDouble());
        return run ? bitOf (*run) : 0u;
    }
};

// [glue] (owner decisions 3.8, 3.8а): ticked, "up to N dB", only where [glue] byTarget names the target (cd) — and not
// on an input too quiet to measure, nor on one without a short-term P95, which the threshold stands on. A glue that
// compresses reads the tempo — its release follows it — so a master waits for it; one that cannot compress reads nothing.
template <> struct Planned<GlueFields<Value>>
{
    static void propose (const PlanInputs& in, GlueFields<Value>& m, DevicePlan& plan, PlanFindings&) noexcept
    {
        if (! in.rules.row (in.row).glue) return;
        plan.target |= std::uint8_t (fieldBit (in.rules, m, m.on) | fieldBit (in.rules, m, m.upToDb));
        if (quiet (in)) { m.on = false; plan.heldBack = HeldBack::Quiet; }
        else if (! inputLevels (in).p95Db) { m.on = false; plan.heldBack = HeldBack::Unmeasured; }
    }
    static std::uint32_t needs (const PlanInputs& in, const GlueFields<Value>& glue) noexcept
    {
        return glue.on && glue.upToDb > 0.0 && inputLevels (in).p95Db ? bitOf (Analyzer::Tempo) : 0u;
    }
};

// [saturation]: the config's defaults — a taste of the manual mode; the machine does not set it.
template <> struct Planned<SaturationFields<Value>>
{
    static void propose (const PlanInputs&, SaturationFields<Value>&, DevicePlan&, PlanFindings&) noexcept {}
    static std::uint32_t needs (const PlanInputs&, const SaturationFields<Value>&) noexcept { return 0; }
};

// [tilt]: off and flat — the machine does not touch timbre (a tilt taken from a measurement is a genre trap). A person's
// tick and knob are the whole device.
template <> struct Planned<TiltFields<Value>>
{
    static void propose (const PlanInputs&, TiltFields<Value>&, DevicePlan&, PlanFindings&) noexcept {}
    static std::uint32_t needs (const PlanInputs&, const TiltFields<Value>&) noexcept { return 0; }
};

// [limiter] (owner decisions 3.6, 3.7, 3.12): always in the chain. Its knob — the needles decided by the peak clipper
// (auto), or none where the target has no peak clipper and on an input too quiet to measure. Deciding by itself, the
// clipper reads the needles at the ceiling the target's numbers give and classes them (src/Limiter.h): the class is the
// machine's answer at the time of the master, and what holds the clipper back is said here.
template <> struct Planned<LimiterFields<Value>>
{
    static void propose (const PlanInputs& in, LimiterFields<Value>& m, DevicePlan& plan, PlanFindings&) noexcept
    {
        const auto answer = needlesAnswer (in);
        switch (answer.why)
        {
            case NeedlesWhy::Target:
                plan.target |= fieldBit (in.rules, m, m.needles);
                plan.heldBack = HeldBack::Target;
                break;
            case NeedlesWhy::Quiet:
                m.needles = Needles::Off;
                plan.heldBack = HeldBack::Quiet;
                break;
            case NeedlesWhy::Unmeasured:   plan.heldBack = HeldBack::Unmeasured; break;
            case NeedlesWhy::Clipped:
            case NeedlesWhy::LowPlr:
            case NeedlesWhy::Bass:
            case NeedlesWhy::Long:         plan.heldBack = HeldBack::Measured; break;
            case NeedlesWhy::Cuts:
            case NeedlesWhy::Shell:        // the planner marks a device the shell does not offer
            case NeedlesWhy::NoReadings:   // no loudness, no plan at all
            case NeedlesWhy::LittleNeed:   // nothing to cut: nothing is held back
            case NeedlesWhy::Pending:
            case NeedlesWhy::NoExcursions: break;
        }
    }
    // The clipper deciding by itself reads the needles — unless its answer stands without them: a shell or a target
    // without it, a quiet input, a need too little to cut, a clipped source.
    static std::uint32_t needs (const PlanInputs& in, const LimiterFields<Value>& limiter) noexcept
    {
        if (limiter.needles != Needles::Auto) return 0u;
        switch (needlesAnswer (in).why)
        {
            case NeedlesWhy::Shell:
            case NeedlesWhy::Target:
            case NeedlesWhy::Quiet:
            case NeedlesWhy::LittleNeed:
            case NeedlesWhy::Clipped:      return 0u;
            case NeedlesWhy::NoReadings:
            case NeedlesWhy::Pending:
            case NeedlesWhy::Unmeasured:
            case NeedlesWhy::NoExcursions:
            case NeedlesWhy::LowPlr:
            case NeedlesWhy::Bass:
            case NeedlesWhy::Long:
            case NeedlesWhy::Cuts:         break;
        }
        return bitOf (Analyzer::Excursions);
    }
};

// [dither]: on a delivery of the bit depths it serves.
template <> struct Planned<DitherFields<Value>>
{
    static void propose (const PlanInputs& in, DitherFields<Value>& m, DevicePlan& plan, PlanFindings&) noexcept
    {
        plan.target |= fieldBit (in.rules, m, m.on);
        if (! offered (in.rules, in.row, in.channels, Device::Dither)) plan.heldBack = HeldBack::Target;
    }
    static std::uint32_t needs (const PlanInputs&, const DitherFields<Value>&) noexcept { return 0; }
};

// [low]: a static shelf on every target. The machine ticks it only for the target's correction for its medium (lowDb:
// vinyl's +0.5 dB) — a number of the target, not a decision taken from a measurement — and not on an input too quiet
// to measure, where it places no device; the correction's number stays on the knob for a person's tick.
template <> struct Planned<LowFields<Value>>
{
    static void propose (const PlanInputs& in, LowFields<Value>& m, DevicePlan& plan, PlanFindings&) noexcept
    {
        if (! in.rules.row (in.row).lowDb) return;
        plan.target |= std::uint8_t (fieldBit (in.rules, m, m.on) | fieldBit (in.rules, m, m.db));
        if (quiet (in))
        {
            m.on = false;
            plan.heldBack = HeldBack::Quiet;
        }
    }
    static std::uint32_t needs (const PlanInputs&, const LowFields<Value>&) noexcept { return 0; }
};

// [bands]: a person's only — the machine leaves every band at 0 dB (it does not touch timbre in this release).
template <> struct Planned<BandsFields<Value>>
{
    static void propose (const PlanInputs&, BandsFields<Value>&, DevicePlan&, PlanFindings&) noexcept {}
    static std::uint32_t needs (const PlanInputs&, const BandsFields<Value>&) noexcept { return 0; }
};

// Every device's plan and fields, in the order of Device: v(device, layers, plan).
template <class D, class P, class V> void eachPlan (D& devices, P& plans, V&& v)
{
    v (Device::Hpf, devices.hpf, plans.hpf);
    v (Device::MonoBass, devices.monoBass, plans.monoBass);
    v (Device::Glue, devices.glue, plans.glue);
    v (Device::Saturation, devices.saturation, plans.saturation);
    v (Device::Tilt, devices.tilt, plans.tilt);
    v (Device::Limiter, devices.limiter, plans.limiter);
    v (Device::Dither, devices.dither, plans.dither);
    v (Device::Low, devices.low, plans.low);
    v (Device::Bands, devices.bands, plans.bands);
}
} // namespace

bool ended (const MeasurementResult& result) noexcept
{
    switch (result.status)
    {
        case MeasurementStatus::Ready:
        case MeasurementStatus::Unavailable: return true;
        case MeasurementStatus::Pending:     return false;
        case MeasurementStatus::Cancelled:   return result.analyzer == Analyzer::Excursions;
    }
    storageOverflow();
}

void propose (const PlanInputs& in, Devices& machine, DevicePlans& plans, PlanFindings& found) noexcept
{
    Devices defaults;
    placeDefaults (in.rules, in.row, in.channels, defaults);
    plans = {};
    found = {};
    eachPlan (machine, plans, [&] (Device device, auto& layers, DevicePlan& plan)
    {
        using Fields = std::remove_cvref_t<decltype (layers.machine)>;
        layers.machine = DeviceOf<Fields>::layers (defaults).machine;
        Planned<Fields>::propose (in, layers.machine, plan, found);
        if (! offeredByShell (in, device))
        {
            plan.heldBack = HeldBack::Shell;
            // The EQ bands' machine tick stays on (the machine never bypasses them; at 0 dB they write nothing), so a
            // project file's [bands] defaults agree with it: the plan says they do not sound.
            if constexpr (requires { layers.machine.on; } && ! requires { layers.machine.body; }) layers.machine.on = false;
            if constexpr (requires { layers.machine.needles; }) layers.machine.needles = Needles::Off;
        }
    });
}

std::uint32_t needs (const PlanInputs& in, const Devices& devices, bool withHand, DevicePlans& plans) noexcept
{
    std::uint32_t all = 0;
    eachPlan (devices, plans, [&] (Device device, const auto& layers, DevicePlan& plan)
    {
        using Fields = std::remove_cvref_t<decltype (layers.machine)>;
        auto settings = settingsOf (in.rules, layers, withHand);
        // The glue's knob as it sounds: [glue] whenTicked for a person's tick on a knob the machine left at 0.
        if constexpr (requires { settings.upToDb; }) if (withHand) settings.upToDb = glueKnob (in.rules, layers);
        // The needles' mode as it sounds: a threshold a person turned is manual.
        if constexpr (requires { settings.needles; }) settings.needles = needlesKnob (layers, withHand).mode;
        plan.needs = Planned<Fields>::needs (in, settings);
        all |= plan.needs;
        plan.tick = withHand ? tickFrom (in.rules, layers) : TickFrom::Machine;
        // The tick as it SOUNDS: a device the target, the source or the shell rules out is out of the chain whatever
        // tick the project keeps for it (a person's dither tick above the depth that is dithered).
        const bool possible = offeredByShell (in, device) && offered (in.rules, in.row, in.channels, device);
        // The EQ bands sound where they are on and any band is not at 0 dB.
        if constexpr (requires { settings.body; })
            plan.on = settings.on && possible
                && ! (detail::same (settings.body, 0.0) && detail::same (settings.mud, 0.0)
                      && detail::same (settings.forward, 0.0) && detail::same (settings.brightness, 0.0)
                      && detail::same (settings.air, 0.0));
        else if constexpr (requires { settings.on; }) plan.on = settings.on && possible;
        else plan.on = true;
    });
    return all;
}

void placeMachine (const PlanInputs& in, Devices& devices) noexcept
{
    DevicePlans plans;
    PlanFindings found;
    propose (in, devices, plans, found);
    eachDevice (devices, [&] (Device device, auto& layers)
    {
        if (! offeredByShell (in, device)) layers.hand = {};
    });
}

std::uint32_t waiting (const PlanInputs& in, std::uint32_t needed) noexcept
{
    std::uint32_t out = 0;
    for (const auto& result : in.measurements)
        if ((needed & bitOf (result.analyzer)) != 0
            && (! ended (result) || (result.analyzer == Analyzer::Excursions && ! in.needlesCurrent)))
            out |= bitOf (result.analyzer);
    // Before a source there are no results: whatever is needed has not ended.
    if (in.measurements.empty()) out = needed;
    return out;
}

Awaited awaited (std::uint32_t waitingFor, const DevicePlans& plans) noexcept
{
    Awaited out;
    for (const auto analyzer : { Analyzer::Excursions, Analyzer::Tempo })
        if ((waitingFor & bitOf (analyzer)) != 0) { out.analyzer = analyzer; break; }
    if (! out.analyzer)
        for (unsigned a = 0; a < kAnalyzers && ! out.analyzer; ++a)
            if ((waitingFor & bitOf (Analyzer (a))) != 0) out.analyzer = Analyzer (a);
    if (out.analyzer)
        for (unsigned d = 0; d <= unsigned (Device::Bands) && ! out.device; ++d)
            if ((planOf (plans, Device (d)).needs & bitOf (*out.analyzer)) != 0) out.device = Device (d);
    return out;
}

namespace
{
template <class P> auto& planIn (P& plans, Device device) noexcept
{
    switch (device)
    {
        case Device::Hpf:        return plans.hpf;
        case Device::MonoBass:   return plans.monoBass;
        case Device::Glue:       return plans.glue;
        case Device::Saturation: return plans.saturation;
        case Device::Tilt:       return plans.tilt;
        case Device::Limiter:    return plans.limiter;
        case Device::Dither:     return plans.dither;
        case Device::Low:        return plans.low;
        case Device::Bands:      return plans.bands;
    }
    storageOverflow();
}
} // namespace
DevicePlan& planOf (DevicePlans& plans, Device device) noexcept { return planIn (plans, device); }
const DevicePlan& planOf (const DevicePlans& plans, Device device) noexcept { return planIn (plans, device); }

} // namespace felitronics::session::detail

//==============================================================================
// THE SESSION'S PLAN — the planner's inputs gathered, placement, and the plan the snapshot and the table read.

namespace felitronics::session
{
namespace
{
std::optional<double> finiteReading (const MeasurementResult& result, std::string_view name) noexcept
{
    if (result.status == MeasurementStatus::Ready)
        for (const auto& value : result.numbers)
            if (value.name == name && value.value && std::isfinite (*value.value)) return value.value;
    return {};
}
// 64-bit FNV-1a, a byte at a time: the plan's key, the same on every row.
struct Key
{
    std::uint64_t h = 0xCBF29CE484222325ull;
    void byte (std::uint8_t b) noexcept { h = (h ^ b) * 0x100000001B3ull; }
    void u64 (std::uint64_t v) noexcept { for (int i = 0; i < 8; ++i) byte (std::uint8_t (v >> (8 * i))); }
    void number (double v) noexcept { u64 (std::bit_cast<std::uint64_t> (v)); }
    template <class T> void field (const T& v) noexcept
    {
        if constexpr (std::is_same_v<T, double>) number (v);
        else u64 (std::uint64_t (v));
    }
    template <class T> void field (const std::optional<T>& v) noexcept
    {
        byte (v ? 1 : 0);
        if (v) field (*v);
    }
};
} // namespace

std::optional<double> Session::needlesNeed (const Project& project) const noexcept
{
    if (source_.channels == 0) return {};
    return detail::loudnessNeed (detail::rules(), project.target, project.targetEdit,
                                 measurementResults_[std::size_t (Analyzer::Loudness)]);
}

detail::PlanInputs Session::planInputs (const Project& project) const noexcept
{
    detail::PlanInputs in;
    in.rules = detail::rules();
    in.row = project.target;
    in.targetEdit = project.targetEdit;
    in.channels = source_.channels;
    in.sampleRate = source_.sampleRate;
    in.frames = source_.frames;
    in.bitDepth = source_.bitDepth;
    in.offered = capabilities_.offeredDevices;
    if (source_.channels != 0) in.measurements = measurementResults_;
    const auto need = needlesNeed (project);
    const auto peak = finiteReading (measurementResults_[std::size_t (Analyzer::Loudness)], "truePeakDb");
    std::optional<double> ceiling;
    if (need && peak) ceiling = *peak - *need;
    in.needlesCurrent = source_.channels != 0 && needlesSource_ == source_.hash
        && ceiling.has_value() == needlesCeilingDb_.has_value()
        && (! ceiling || std::bit_cast<std::uint64_t> (*ceiling) == std::bit_cast<std::uint64_t> (*needlesCeilingDb_));
    return in;
}

void Session::place (Project& project) const noexcept
{
    detail::placeMachine (planInputs (project), project.devices);
}

std::uint32_t Session::planWaiting (const Project& project) const noexcept
{
    const auto in = planInputs (project);
    DevicePlans plans;
    return detail::waiting (in, detail::needs (in, project.devices, true, plans));
}

namespace detail
{
namespace
{
void state (PlanView& plan, Device device, const text::Fact& fact) noexcept
{
    // Room for every line PlanText can state of one plan and the waiting fact (kPlanFacts): never full.
    if (plan.facts.count == plan.facts.items.size()) storageOverflow();
    plan.facts.items[plan.facts.count++] = { device, fact };
}
void state (PlanView& plan, Device device, const std::optional<text::Fact>& fact) noexcept
{
    if (fact) state (plan, device, *fact);
}
std::optional<text::Term> termOf (Analyzer analyzer) noexcept
{
    using text::Term;
    switch (analyzer)
    {
        case Analyzer::LowEnd:       return Term::AnalyzerLowEnd;
        case Analyzer::LowEnd150:    return Term::AnalyzerLowEnd150;
        case Analyzer::InfraLow:     return Term::AnalyzerInfraLow;
        case Analyzer::Forensics:    return Term::AnalyzerForensics;
        case Analyzer::Stereo:       return Term::AnalyzerStereo;
        case Analyzer::StereoBursts: return Term::AnalyzerBursts;
        case Analyzer::Crest:        return Term::AnalyzerCrest;
        case Analyzer::Hum:          return Term::AnalyzerHum;
        case Analyzer::Waveform:     return Term::AnalyzerWaveform;
        case Analyzer::Tempo:        return Term::AnalyzerTempo;
        case Analyzer::Excursions:   return Term::AnalyzerNeedles;
        // The first measurement's own: it has ended before a device is placed, so no plan waits for one of them.
        case Analyzer::Loudness:
        case Analyzer::Clipping:
        case Analyzer::Programme:    return std::nullopt;
    }
    return std::nullopt;
}
text::Term termOf (Device device) noexcept
{
    using text::Term;
    switch (device)
    {
        case Device::Hpf:        return Term::DeviceHpf;
        case Device::MonoBass:   return Term::DeviceMonoBass;
        case Device::Glue:       return Term::DeviceGlue;
        case Device::Saturation: return Term::DeviceSaturation;
        case Device::Tilt:       return Term::DeviceTilt;
        case Device::Limiter:    return Term::DeviceLimiter;
        case Device::Dither:     return Term::DeviceDither;
        case Device::Low:        return Term::DeviceLow;
        case Device::Bands:      return Term::DeviceBands;
    }
    storageOverflow();
}
} // namespace

void stateReasons (PlanView& plan, const EqFinding& eq, const AdviceHands& hands) noexcept
{
    // A piece of advice is said of the value a person set — the cutoff, the slope, the crossover, the shelves — and of
    // nothing the machine chose: a slope by hand says nothing of the machine's cutoff beside it.
    const auto byHand = [] (bool hand, std::optional<text::Fact> said) { return hand ? said : std::nullopt; };
    state (plan, Device::Hpf, PlanText::hpf (plan.hpf));
    state (plan, Device::Hpf, byHand (hands.hpfFq, PlanText::hpfCutoffAdvice (plan.hpf)));
    state (plan, Device::Hpf, byHand (hands.hpfSlope, PlanText::hpfSlopeAdvice (plan.hpf)));
    state (plan, Device::MonoBass, PlanText::monoBass (plan.monoBass));
    state (plan, Device::MonoBass, PlanText::monoBassPolarity (plan.monoBass));
    state (plan, Device::MonoBass, PlanText::monoBassCoverage (plan.monoBass));
    state (plan, Device::MonoBass, byHand (hands.monoBassFq, PlanText::monoBassAdvice (plan.monoBass)));
    state (plan, Device::Glue, PlanText::glue (plan.glue));
    state (plan, Device::Glue, PlanText::glueTempo (plan.glue));
    state (plan, Device::Glue, PlanText::glueRelease (plan.glue));
    if (eq.device == Device::Tilt) state (plan, Device::Tilt, byHand (hands.eqKnobs, PlanText::eqAdvice (eq)));
    state (plan, Device::Limiter, PlanText::limiter (plan.limiter));
    state (plan, Device::Limiter, PlanText::needlesAgainstMachine (plan.limiter));
    state (plan, Device::Limiter, PlanText::vinylCeiling (plan.limiter));
    state (plan, Device::Limiter, PlanText::vinylNeedles (plan.limiter));
    state (plan, Device::Limiter, PlanText::vinylTop (plan.limiter));
    state (plan, Device::Dither, PlanText::dither (plan.dither));
    if (eq.device == Device::Low) state (plan, Device::Low, byHand (hands.eqKnobs, PlanText::eqAdvice (eq)));
    if (eq.device == Device::Bands) state (plan, Device::Bands, byHand (hands.eqKnobs, PlanText::eqAdvice (eq)));
}

bool anyPending (const DevicePlans& plans) noexcept
{
    bool any = false;
    for (unsigned d = 0; d <= unsigned (Device::Bands); ++d) any = any || planOf (plans, Device (d)).pending != 0;
    return any;
}
void stateUnmeasured (PlanView& plan) noexcept
{
    // A card whose machine fields are not measured yet says so — the core's words, naming what it waits for.
    for (unsigned d = 0; d <= unsigned (Device::Bands); ++d)
    {
        const auto& device = planOf (plan.devices, Device (d));
        if (device.pending == 0) continue;
        std::optional<text::Term> analyzer;
        for (unsigned a = 0; a < kAnalyzers && ! analyzer; ++a)
            if ((device.needs & plan.waiting & bitOf (Analyzer (a))) != 0) analyzer = termOf (Analyzer (a));
        if (analyzer) state (plan, Device (d), text::Fact::of (text::FactId::DeviceUnmeasured, text::Arg::term (*analyzer)));
    }
}
void stateWaiting (PlanView& plan) noexcept
{
    // The progress moves between plans: the waiting fact stated before gives way to the one the plan names now.
    auto& facts = plan.facts;
    if (facts.count != 0 && facts.items[facts.count - 1u].fact.id == text::FactId::PlanWaiting) facts.items[--facts.count] = {};
    if (! plan.awaited || ! plan.awaitedBy) return;
    const auto analyzer = termOf (*plan.awaited);
    if (! analyzer) return;
    state (plan, *plan.awaitedBy, text::Fact::of (text::FactId::PlanWaiting, text::Arg::term (termOf (*plan.awaitedBy)),
        text::Arg::term (*analyzer), text::Arg::value (100.0 * plan.awaitedFraction, text::Unit::Percent, 0)));
}
} // namespace detail

void Session::replan() noexcept
{
    // The pump gives source measurement priority over grades. State the current hold after every command and step;
    // cancelling or continuing that measurement changes the reason even before the grade gets another unit.
    for (std::size_t i = 0; i < damageCount_; ++i)
        damageJobs_[i].waitReason = damageJobs_[i].state == DamageJobState::Running ? std::nullopt
            : std::optional { measurementJob_ != 0 || needlesJob_ != 0
                ? DamageWaitReason::SourceMeasurement : DamageWaitReason::Queue };
    const auto in = planInputs (project_);
    Key key;
    key.u64 (source_.hash); key.u64 (measurementKey_); key.u64 (config::Config::versions().all);
    const auto core = version();
    key.u64 (core.major); key.u64 (core.minor); key.u64 (core.patch);
    key.u64 (in.row); key.field (in.targetEdit.lufs); key.field (in.targetEdit.tp);
    key.u64 (in.channels); key.u64 (in.sampleRate); key.u64 (in.frames); key.u64 (in.offered); key.byte (in.needlesCurrent ? 1 : 0);
    const bool firstEnded = sourceMeasurements_ ? sourceMeasurements_->firstPublished : measurementsFromSidecar_;
    key.byte (devicesPlaced_ ? 1 : 0); key.byte (firstEnded ? 1 : 0); key.byte (mandatoryReady() ? 1 : 0);
    for (const auto& r : in.measurements)
    {
        key.byte (std::uint8_t (r.status)); key.byte (std::uint8_t (r.reason)); key.u64 (r.key);
    }
    const auto rules = detail::rules();
    detail::eachDevice (project_.devices, [&] (Device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        Of::each (rules, [&] (std::uint8_t, const detail::FieldRule&, const auto& machine, const auto& hand)
        {
            key.field (machine); key.field (hand);
        }, layers.machine, layers.hand);
    });
    const bool observe = key.h != plan_.key || planRuns_ == 0;
    EqFinding eq;
    if (observe)
    {
        ++planRuns_;
        plan_ = {};
        plan_.key = key.h;
        if (! devicesPlaced_)
        {
            // Nothing placed, nothing planned: no needs and nothing awaited — only whether a plan can be made at all.
            plan_.status = source_.channels != 0 && firstEnded && ! mandatoryReady() ? PlanStatus::Unavailable : PlanStatus::None;
        }
        else
        {
            Devices proposed;
            detail::PlanFindings found;
            detail::propose (in, proposed, plan_.devices, found);
            plan_.hpf = found.hpf;
            plan_.monoBass = found.monoBass;
            plan_.monoBass.againstMachine = ! proposed.monoBass.machine.on
                && detail::settingsOf (in.rules, project_.devices.monoBass).on;
            // What sounds against what the planner proposes: the project's own device, a person's layer over a
            // machine's that a file may have written.
            const auto sounding = [&] (Device device, bool on, bool asProposed, bool byHand)
            {
                const bool offeredHere = (in.offered & (1u << unsigned (device))) != 0;
                return ! on || ! offeredHere ? Sounding::Off : asProposed ? Sounding::Proposal : byHand ? Sounding::Hand : Sounding::File;
            };
            const auto hpf = detail::settingsOf (in.rules, project_.devices.hpf);
            plan_.hpf.soundingHz = hpf.fq;
            plan_.hpf.soundingSlope = hpf.slope;
            plan_.hpf.sounding = sounding (Device::Hpf, hpf.on,
                detail::same (hpf.fq, proposed.hpf.machine.fq) && hpf.slope == proposed.hpf.machine.slope,
                project_.devices.hpf.hand.fq.has_value() || project_.devices.hpf.hand.slope.has_value());
            const auto mono = detail::settingsOf (in.rules, project_.devices.monoBass);
            plan_.monoBass.soundingHz = mono.fq;
            plan_.monoBass.sounding = sounding (Device::MonoBass, mono.on,
                detail::same (mono.fq, proposed.monoBass.machine.fq) && detail::same (mono.width, proposed.monoBass.machine.width),
                project_.devices.monoBass.hand.fq.has_value() || project_.devices.monoBass.hand.width.has_value());
            eq = detail::eqFinding (project_.devices, in.rules, double (in.sampleRate));
            plan_.inputGainDb = detail::inputLevels (in).gainDb;
            plan_.glue = detail::glueFinding (in, project_.devices);
            plan_.saturation = detail::saturationFinding (in, project_.devices);
            plan_.limiter = detail::limiterFinding (in, project_.devices);
            plan_.dither = detail::ditherFinding (in, project_.devices);
            plan_.needs = detail::needs (in, project_.devices, true, plan_.devices);
            plan_.waiting = detail::waiting (in, plan_.needs);
            DevicePlans machineOnly;
            const auto machineWaiting = detail::waiting (in, detail::needs (in, project_.devices, false, machineOnly));
            bool stopped = false;
            for (const auto& r : in.measurements)
                stopped = stopped || ((machineWaiting & detail::bitOf (r.analyzer)) != 0 && r.status == MeasurementStatus::Cancelled);
            plan_.status = machineWaiting == 0 ? PlanStatus::Ready : stopped ? PlanStatus::Stopped : PlanStatus::Pending;
            const auto awaited = detail::awaited (plan_.waiting, plan_.devices);
            plan_.awaited = awaited.analyzer;
            plan_.awaitedBy = awaited.device;
        }
        // Placed devices take edits, a field the machine has not measured yet included (owner, 02.10).
        plan_.readOnly = ! devicesPlaced_;
        if (plan_.status == PlanStatus::Pending || plan_.status == PlanStatus::Stopped) detail::stateUnmeasured (plan_);
        const auto& d = project_.devices;
        detail::AdviceHands hands;
        hands.hpfFq = d.hpf.hand.fq.has_value();
        hands.hpfSlope = d.hpf.hand.slope.has_value();
        hands.monoBassFq = d.monoBass.hand.fq.has_value();
        // The EQ curve's advice is the summed curve's: a hand on any of its knobs — tilt's, low's, a band's — raises it.
        const auto& b = d.bands.hand;
        hands.eqKnobs = d.tilt.hand.on.has_value() || d.tilt.hand.db.has_value() || d.low.hand.on.has_value()
            || d.low.hand.db.has_value() || b.on.has_value() || b.body.has_value() || b.mud.has_value() || b.forward.has_value()
            || b.brightness.has_value() || b.air.has_value();
        if (plan_.status == PlanStatus::Ready) detail::stateReasons (plan_, eq, hands);
    }
    if (observe)
    {
        // What the measurements found in the file, beside the plan: on the same inputs, and saying which device that
        // is in the chain deals with each.
        detail::ObservationInputs seen;
        seen.rules = in.rules;
        seen.measurements = in.measurements;
        seen.channels = in.channels; seen.sampleRate = in.sampleRate; seen.frames = in.frames;
        seen.bitDepth = in.bitDepth;
        seen.hpfOn = devicesPlaced_ && plan_.devices.hpf.on;
        seen.monoBassOn = devicesPlaced_ && plan_.devices.monoBass.on;
        detail::observe (seen, observations_);
    }
    plan_.fromFile = machineFromFile_;
    plan_.awaitedFraction = 0.0;
    if (plan_.awaited == Analyzer::Excursions && needlesJob_ != 0) plan_.awaitedFraction = needlesProgress_.fraction;
    else if (plan_.awaited == Analyzer::Tempo && sourceMeasurements_ && source_.frames != 0
             && sourceMeasurements_->cursor < sourceMeasurements_->order.size()
             && sourceMeasurements_->order[sourceMeasurements_->cursor] == Analyzer::Tempo)
        plan_.awaitedFraction = double (sourceMeasurements_->frames) / double (source_.frames);
    detail::stateWaiting (plan_);
}
} // namespace felitronics::session

namespace felitronics::session
{
text::Fact PlanText::hpf (const HpfFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    // The planner's reasons are said of its own cutoff, and only where that cutoff sounds.
    switch (f.sounding)
    {
        case Sounding::Hand: return Fact::of (FactId::HpfByHand, Arg::value (f.soundingHz, Unit::Hz, 1));
        case Sounding::File: return Fact::of (FactId::HpfKept, Arg::value (f.soundingHz, Unit::Hz, 1));
        case Sounding::Off:  return Fact::of (FactId::HpfOff);
        case Sounding::Proposal: break;
    }
    const auto cutoff = Arg::value (f.cutoffHz, Unit::Hz, 1);
    const auto note = [&] { return Arg::midi (f.noteMidi.value_or (0)); };
    const auto hz = [&] { return Arg::value (f.noteHz.value_or (0.0), Unit::Hz, 1); };
    const auto loss = [&] { return Arg::value (f.noteLossDb.value_or (0.0), Unit::Db, 1); };
    switch (f.cut)
    {
        case HpfCut::Note:       return Fact::of (FactId::HpfNote, cutoff, note(), hz(), loss());
        case HpfCut::Floor:      return Fact::of (FactId::HpfFloor, cutoff, note(), hz(), loss());
        case HpfCut::BelowFloor: return Fact::of (FactId::HpfBelowFloor, cutoff, note(), hz(), loss());
        case HpfCut::Top:        return Fact::of (FactId::HpfTop, cutoff, note(), hz());
        case HpfCut::Unsure:     return Fact::of (FactId::HpfUnsure, cutoff);
        case HpfCut::Short:
        {
            const auto shortest = detail::rules().engine.find ("input").find ("shortSeconds");
            const double seconds = shortest.decimal() ? shortest.decimal()->toDouble() : double (shortest.integer().value_or (0));
            return Fact::of (FactId::HpfShort, cutoff, Arg::value (seconds, Unit::S, 0));
        }
        case HpfCut::Quiet:      return Fact::of (FactId::HpfQuiet, cutoff);
        case HpfCut::Unmeasured: return Fact::of (FactId::HpfUnmeasured, cutoff);
    }
    detail::storageOverflow();
}
std::optional<text::Fact> PlanText::hpfCutoffAdvice (const HpfFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    // Advice on a person's value only (owner, 01.10): the machine's cutoff — proposed now or kept from a file — is its
    // own rule's, and the comfort window says nothing against it.
    if (f.sounding != Sounding::Hand) return std::nullopt;
    const auto comfort = detail::rules().engine.find ("hpf").find ("comfort");
    const double low = detail::configured (comfort.find ("lowHz")), high = detail::configured (comfort.find ("highHz"));
    const auto window = [&] (FactId id)
    {
        return Fact::of (id, Arg::value (f.soundingHz, Unit::Hz, 1), Arg::value (low, Unit::None, 0), Arg::value (high, Unit::Hz, 0));
    };
    // The knob's heat states the side: the advice and the knob's colour are one comparison (Kit.h).
    const auto side = Kit::heat (text::Term::FieldHpfFq, f.soundingHz).side;
    if (side < 0) return window (FactId::HpfBelowComfort);
    if (side > 0) return window (FactId::HpfAboveComfort);
    return std::nullopt;
}
std::optional<text::Fact> PlanText::hpfSlopeAdvice (const HpfFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId;
    if (f.sounding != Sounding::Hand) return std::nullopt;
    // slopesNormal is ascending (the schema holds it): its first is the gentlest, its last the steepest.
    std::optional<std::int64_t> gentlest, steepest;
    const auto slopes = detail::rules().engine.find ("hpf").find ("slopesNormal");
    for (const auto item : slopes)
    {
        const auto slope = item.integer();
        if (! slope) continue;
        if (! gentlest) gentlest = *slope;
        steepest = *slope;
    }
    if (! gentlest || ! steepest) return std::nullopt;
    if (f.soundingSlope < *gentlest) return Fact::of (FactId::HpfSlopeGentle, Arg::count (f.soundingSlope), Arg::count (*gentlest));
    if (f.soundingSlope > *steepest) return Fact::of (FactId::HpfSlopeSteep, Arg::count (f.soundingSlope), Arg::count (*steepest));
    return std::nullopt;
}
std::optional<text::Fact> PlanText::monoBassAdvice (const MonoBassFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    if (f.sounding != Sounding::Hand) return std::nullopt;
    const auto zones = detail::rules().engine.find ("monoBass").find ("zones");
    const auto club = zones.find ("club"), vinyl = zones.find ("vinyl");
    const double clubFrom = detail::configured (club.find ("fromHz")), clubTo = detail::configured (club.find ("toHz"));
    const double vinylFrom = detail::configured (vinyl.find ("fromHz")), vinylTo = detail::configured (vinyl.find ("toHz"));
    // The zones the kit draws are the zones the advice reads (Kit.h): one comparison. Outside every zone the crossover
    // is either below the lowest start or above the highest end — each side its own line, with the zones' ends it passed.
    if (Kit::monoZonesAt (f.soundingHz) != 0) return std::nullopt;
    if (f.soundingHz < std::min (clubFrom, vinylFrom))
        return Fact::of (FactId::MonoBassBelowZones, Arg::value (f.soundingHz, Unit::Hz, 0), Arg::value (clubFrom, Unit::Hz, 0),
            Arg::value (vinylFrom, Unit::Hz, 0));
    return Fact::of (FactId::MonoBassOutsideZones, Arg::value (f.soundingHz, Unit::Hz, 0), Arg::value (clubTo, Unit::Hz, 0),
        Arg::value (vinylTo, Unit::Hz, 0));
}
std::optional<text::Fact> PlanText::eqAdvice (const EqFinding& f) noexcept
{
    if (! f.over) return std::nullopt;
    return text::Fact::of (text::FactId::EqOvershoot, text::Arg::value (f.db, text::Unit::Db, 1, text::Sign::Always),
        text::Arg::value (f.hz, text::Unit::Hz, 0));
}
std::optional<text::Fact> PlanText::monoBass (const MonoBassFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    // The loss was weighed at the machine's crossover, folded whole: it is said of that fold alone. "Will take" is true
    // of a fold that sounds as proposed; "would take — check the polarity" of that fold sounding against the machine or
    // left out; "off: too little bass" of a fold left out.
    const bool off = f.sounding == Sounding::Off, proposed = f.sounding == Sounding::Proposal;
    if (f.sounding == Sounding::Hand) return Fact::of (FactId::MonoBassByHand, Arg::value (f.soundingHz, Unit::Hz, 0));
    if (f.sounding == Sounding::File) return Fact::of (FactId::MonoBassKept, Arg::value (f.soundingHz, Unit::Hz, 0));
    switch (f.verdict)
    {
        case MonoBassVerdict::Partial:
            return proposed ? std::optional (Fact::of (FactId::MonoBassPartial, Arg::value (f.lossDb.value_or (0.0), Unit::Db, 1))) : std::nullopt;
        case MonoBassVerdict::AntiPhase:  return Fact::of (FactId::MonoBassAntiPhase, Arg::value (f.lossDb.value_or (0.0), Unit::Db, 1));
        case MonoBassVerdict::Unmeasured: return off ? std::optional (Fact::of (FactId::MonoBassUnmeasured)) : std::nullopt;
        case MonoBassVerdict::On:
        case MonoBassVerdict::MonoSource:
        case MonoBassVerdict::Quiet:      return std::nullopt;
    }
    detail::storageOverflow();
}
std::optional<text::Fact> PlanText::monoBassPolarity (const MonoBassFinding& f) noexcept
{
    // A fold at another crossover than the one weighed is named as a person's (or a file's); the bass is in opposite
    // polarity all the same, and the warning stands beside that sentence.
    if (f.verdict != MonoBassVerdict::AntiPhase || (f.sounding != Sounding::Hand && f.sounding != Sounding::File)) return std::nullopt;
    return text::Fact::of (text::FactId::MonoBassAntiPhase, text::Arg::value (f.lossDb.value_or (0.0), text::Unit::Db, 1));
}
std::optional<text::Fact> PlanText::monoBassCoverage (const MonoBassFinding& f) noexcept
{
    if (! f.coveredSeconds) return std::nullopt;
    return text::Fact::of (text::FactId::MonoBassPartWeighed, text::Arg::value (*f.coveredSeconds, text::Unit::S, 0),
        text::Arg::value (f.pieceSeconds, text::Unit::S, 0));
}
std::optional<text::Fact> PlanText::glue (const GlueFinding& f) noexcept
{
    if (f.state != GlueState::Unavailable) return std::nullopt;
    return text::Fact::of (text::FactId::GlueUnavailable, text::Arg::value (f.upToDb, text::Unit::Db, 1));
}
std::optional<text::Fact> PlanText::glueTempo (const GlueFinding& f) noexcept
{
    if (f.state != GlueState::Active || ! f.bpm || f.tempoMeasured) return std::nullopt;
    if (f.tempoUnsureBpm)
        return text::Fact::of (text::FactId::GlueTempoUnsure, text::Arg::value (*f.tempoUnsureBpm, text::Unit::Bpm, 1),
            text::Arg::value (*f.bpm, text::Unit::Bpm, 0));
    return text::Fact::of (text::FactId::GlueTempoFallback, text::Arg::value (*f.bpm, text::Unit::Bpm, 0));
}
std::optional<text::Fact> PlanText::glueRelease (const GlueFinding& f) noexcept
{
    if (f.state != GlueState::Active || ! f.releaseClamped || ! f.releaseMs || ! f.releaseAskedMs) return std::nullopt;
    return text::Fact::of (text::FactId::GlueReleaseHeld, text::Arg::value (*f.releaseMs, text::Unit::Ms, 0),
        text::Arg::value (*f.releaseAskedMs, text::Unit::Ms, 0));
}
text::Fact PlanText::limiter (const LimiterFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    const auto ceiling = Arg::value (f.ceilingDbTp, Unit::DbTp, 1);
    const auto db = [] (const std::optional<double>& x) { return Arg::value (x.value_or (0.0), Unit::Db, 1); };
    const auto p90 = Arg::value (f.p90Ms.value_or (0.0), Unit::Ms, 1);
    const auto bass = Arg::value (100.0 * f.bassShare.value_or (0.0), Unit::Percent, 0);
    const auto clipper = detail::rules().engine.find ("limiter").find ("peakClipper");
    const auto configured = [&] (std::string_view key)
    {
        const auto v = clipper.find (key);
        return v.decimal() ? v.decimal()->toDouble() : double (v.integer().value_or (0));
    };
    // What sounds: the peak clipper cutting — by a person's threshold, or by the machine's class.
    if (f.cutting)
    {
        // A cap, not an amount: the clipper may take this much off the peaks; how much it takes is the landing's. The
        // message says "no more than" in its own words, so the number is exact — never "no more than ≤ …".
        const auto cut = Arg::value (f.overDb, Unit::Db, 1);
        if (f.mode == Needles::Manual) return Fact::of (FactId::LimiterManual, ceiling, cut);
        return Fact::of (f.proposed == NeedlesClass::Short ? FactId::LimiterShort : FactId::LimiterBetween,
                         ceiling, cut, p90, bass, db (f.plrDb));
    }
    // Not cutting against the machine's answer: the knob is off, a person's or a file's.
    if (f.sounding != Sounding::Proposal) return Fact::of (FactId::LimiterNeedlesOff, ceiling);
    // Not cutting as the machine answers: its reason.
    switch (f.why)
    {
        case NeedlesWhy::LittleNeed:   return Fact::of (FactId::LimiterLittleNeed, ceiling, db (f.needDb), Arg::value (configured ("littleNeedDb"), Unit::Db, 1));
        case NeedlesWhy::NoExcursions: return Fact::of (FactId::LimiterNoExcursions, ceiling);
        case NeedlesWhy::Unmeasured:   return Fact::of (FactId::LimiterUnmeasured, ceiling);
        case NeedlesWhy::Clipped:      return Fact::of (FactId::LimiterClipped, ceiling, Arg::value (f.clipsPerMinute.value_or (0.0), Unit::None, 1));
        case NeedlesWhy::LowPlr:       return Fact::of (FactId::LimiterLowPlr, ceiling, db (f.plrDb), Arg::value (configured ("longPlrDb"), Unit::Db, 1));
        case NeedlesWhy::Bass:         return Fact::of (FactId::LimiterBass, ceiling, bass);
        case NeedlesWhy::Long:         return Fact::of (FactId::LimiterLong, ceiling, p90);
        case NeedlesWhy::Target:       return Fact::of (FactId::LimiterNoClipper, ceiling);
        case NeedlesWhy::Quiet:        return Fact::of (FactId::LimiterQuiet, ceiling);
        case NeedlesWhy::Pending:      return Fact::of (FactId::LimiterPending, ceiling);
        case NeedlesWhy::Cuts:
        case NeedlesWhy::Shell:
        case NeedlesWhy::NoReadings:   return Fact::of (FactId::LimiterPlain, ceiling);
    }
    detail::storageOverflow();
}
std::optional<text::Fact> PlanText::needlesAgainstMachine (const LimiterFinding& f) noexcept
{
    using text::Term;
    if (! f.againstMachine) return std::nullopt;
    Term why = Term::NeedlesWhyUnmeasured;
    switch (f.why)
    {
        case NeedlesWhy::Shell:        why = Term::NeedlesWhyShell; break;
        case NeedlesWhy::Target:       why = Term::NeedlesWhyTarget; break;
        case NeedlesWhy::Quiet:        why = Term::NeedlesWhyQuiet; break;
        case NeedlesWhy::NoReadings:   why = Term::NeedlesWhyNoReadings; break;
        case NeedlesWhy::LittleNeed:   why = Term::NeedlesWhyLittleNeed; break;
        case NeedlesWhy::Unmeasured:   why = Term::NeedlesWhyUnmeasured; break;
        case NeedlesWhy::NoExcursions: why = Term::NeedlesWhyNoExcursions; break;
        case NeedlesWhy::Clipped:      why = Term::NeedlesWhyClipped; break;
        case NeedlesWhy::LowPlr:       why = Term::NeedlesWhyLowPlr; break;
        case NeedlesWhy::Bass:         why = Term::NeedlesWhyBass; break;
        case NeedlesWhy::Long:         why = Term::NeedlesWhyLong; break;
        case NeedlesWhy::Cuts:
        case NeedlesWhy::Pending:      return std::nullopt;   // the machine cuts too, or has no answer yet
    }
    return text::Fact::of (text::FactId::NeedlesAgainstMachine, text::Arg::term (why));
}
std::optional<text::Fact> PlanText::vinylCeiling (const LimiterFinding& f) noexcept
{
    if (! f.ceilingAboveMedium) return std::nullopt;
    return text::Fact::of (text::FactId::VinylCeiling, text::Arg::value (f.ceilingDbTp, text::Unit::DbTp, 1),
        text::Arg::value (f.mediumCeilingDbTp, text::Unit::DbTp, 1));
}
std::optional<text::Fact> PlanText::vinylNeedles (const LimiterFinding& f) noexcept
{
    if (! f.needlesAgainstMedium) return std::nullopt;
    return text::Fact::of (text::FactId::VinylNeedles);
}
std::optional<text::Fact> PlanText::vinylTop (const LimiterFinding& f) noexcept
{
    if (! f.vinyl) return std::nullopt;
    return text::Fact::of (text::FactId::VinylTop, text::Arg::value (f.vinylTopHz / 1000.0, text::Unit::KHz, 0));
}
text::Fact PlanText::dither (const DitherFinding& f) noexcept
{
    using text::Arg; using text::Fact; using text::FactId;
    const auto bits = Arg::count (f.bits), upTo = Arg::count (detail::rules().ditherUpToBits);
    if (f.on) return Fact::of (FactId::DitherOn, bits);
    if (f.offByHand) return Fact::of (FactId::DitherOffByHand, bits);
    if (f.applies) return Fact::of (FactId::DitherOff, bits);
    return Fact::of (f.keptWithoutEffect ? FactId::DitherKept : FactId::DitherNotApplied, bits, upTo);
}
} // namespace felitronics::session
