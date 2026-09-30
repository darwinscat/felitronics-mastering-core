// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE PLANNER (src/Planner.h): one Planned<> per device, walked in the order of Device. Every proposal starts from the
// config's defaults for the target and the source (src/Devices.h) and says where each field came from; what a device
// reads follows from the settings the project gives it.

#include "BuildGuards.h"

#include "Planner.h"
#include "Devices.h"
#include "Grid.h"
#include "Rules.h"
#include "BuildContract.h"
#include "Needles.h"
#include "SourceMeasurements.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>

#include <bit>
#include <cmath>
#include <cstdint>
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

template <class Fields> struct Planned;

// [hpf]: on by [stages]; the cutoff at the config's default, never below the target's floor; the slope the target's.
template <> struct Planned<HpfFields<Value>>
{
    static void propose (const PlanInputs& in, HpfFields<Value>& m, DevicePlan& plan) noexcept
    {
        const TargetRow target = in.rules.row (in.row);
        if (compare (in.rules.hpfDefault, target.hpfFloor) < 0) plan.target |= fieldBit (in.rules, m, m.fq);
        plan.target |= fieldBit (in.rules, m, m.slope);
    }
    static std::uint32_t needs (const PlanInputs&, const HpfFields<Value>&) noexcept { return 0; }
};

// [monoBass]: the crossover the target's; nothing to gather on a mono source.
template <> struct Planned<MonoBassFields<Value>>
{
    static void propose (const PlanInputs& in, MonoBassFields<Value>& m, DevicePlan& plan) noexcept
    {
        plan.target |= fieldBit (in.rules, m, m.fq);
        if (! offered (in.rules, in.row, in.channels, Device::MonoBass)) plan.heldBack = HeldBack::Source;
    }
    static std::uint32_t needs (const PlanInputs&, const MonoBassFields<Value>&) noexcept { return 0; }
};

// [glue]: "up to N dB" where [glue] byTarget names the target. A glue that compresses reads the tempo — its release
// follows it — so a master waits for it.
template <> struct Planned<GlueFields<Value>>
{
    static void propose (const PlanInputs& in, GlueFields<Value>& m, DevicePlan& plan) noexcept
    {
        if (in.rules.row (in.row).glue) plan.target |= fieldBit (in.rules, m, m.upToDb);
    }
    static std::uint32_t needs (const PlanInputs&, const GlueFields<Value>& glue) noexcept
    {
        return glue.on && glue.upToDb > 0.0 ? bitOf (Analyzer::Tempo) : 0u;
    }
};

// [saturation]: the config's defaults — a taste of the manual mode; the machine does not set it.
template <> struct Planned<SaturationFields<Value>>
{
    static void propose (const PlanInputs&, SaturationFields<Value>&, DevicePlan&) noexcept {}
    static std::uint32_t needs (const PlanInputs&, const SaturationFields<Value>&) noexcept { return 0; }
};

// [tilt]: flat — the machine does not touch timbre.
template <> struct Planned<TiltFields<Value>>
{
    static void propose (const PlanInputs&, TiltFields<Value>&, DevicePlan&) noexcept {}
    static std::uint32_t needs (const PlanInputs&, const TiltFields<Value>&) noexcept { return 0; }
};

// [limiter]: the needles decided by the peak clipper (auto), or none where the target has no peak clipper. Deciding by
// itself, the clipper reads the needles at the ceiling the target's numbers give.
template <> struct Planned<LimiterFields<Value>>
{
    static void propose (const PlanInputs& in, LimiterFields<Value>& m, DevicePlan& plan) noexcept
    {
        if (in.rules.row (in.row).noClipper)
        {
            plan.target |= fieldBit (in.rules, m, m.needles);
            plan.heldBack = HeldBack::Target;
        }
    }
    static std::uint32_t needs (const PlanInputs&, const LimiterFields<Value>& limiter) noexcept
    {
        return limiter.needles == Needles::Auto ? bitOf (Analyzer::Excursions) : 0u;
    }
};

// [dither]: on a delivery of the bit depths it serves.
template <> struct Planned<DitherFields<Value>>
{
    static void propose (const PlanInputs& in, DitherFields<Value>& m, DevicePlan& plan) noexcept
    {
        plan.target |= fieldBit (in.rules, m, m.on);
        if (! offered (in.rules, in.row, in.channels, Device::Dither)) plan.heldBack = HeldBack::Target;
    }
    static std::uint32_t needs (const PlanInputs&, const DitherFields<Value>&) noexcept { return 0; }
};

// [low]: the target's correction for its medium (lowDb), where it names one.
template <> struct Planned<LowFields<Value>>
{
    static void propose (const PlanInputs& in, LowFields<Value>& m, DevicePlan& plan) noexcept
    {
        if (in.rules.row (in.row).lowDb) plan.target |= std::uint8_t (fieldBit (in.rules, m, m.on) | fieldBit (in.rules, m, m.db));
    }
    static std::uint32_t needs (const PlanInputs&, const LowFields<Value>&) noexcept { return 0; }
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

void propose (const PlanInputs& in, Devices& machine, DevicePlans& plans) noexcept
{
    Devices defaults;
    placeDefaults (in.rules, in.row, in.channels, defaults);
    plans = {};
    eachPlan (machine, plans, [&] (Device device, auto& layers, DevicePlan& plan)
    {
        using Fields = std::remove_cvref_t<decltype (layers.machine)>;
        layers.machine = DeviceOf<Fields>::layers (defaults).machine;
        Planned<Fields>::propose (in, layers.machine, plan);
        if (! offeredByShell (in, device))
        {
            plan.heldBack = HeldBack::Shell;
            if constexpr (requires { layers.machine.on; }) layers.machine.on = false;
            if constexpr (requires { layers.machine.needles; }) layers.machine.needles = Needles::Off;
        }
    });
}

std::uint32_t needs (const PlanInputs& in, const Devices& devices, bool withHand, DevicePlans& plans) noexcept
{
    std::uint32_t all = 0;
    eachPlan (devices, plans, [&] (Device, const auto& layers, DevicePlan& plan)
    {
        using Fields = std::remove_cvref_t<decltype (layers.machine)>;
        plan.needs = Planned<Fields>::needs (in, settingsOf (in.rules, layers, withHand));
        all |= plan.needs;
    });
    return all;
}

void placeMachine (const PlanInputs& in, Devices& devices) noexcept
{
    DevicePlans plans;
    propose (in, devices, plans);
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
        for (unsigned d = 0; d <= unsigned (Device::Low) && ! out.device; ++d)
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
    const auto& loudness = measurementResults_[std::size_t (Analyzer::Loudness)];
    const auto lufs = finiteReading (loudness, "integratedLufs"), peak = finiteReading (loudness, "truePeakDb");
    if (source_.channels == 0 || ! lufs || ! peak) return {};
    const auto target = detail::rules().row (project.target);
    const double targetLufs = project.targetEdit.lufs ? *project.targetEdit.lufs : target.lufs.toDouble();
    const double targetTp = project.targetEdit.tp ? *project.targetEdit.tp : target.tp.toDouble();
    return (*peak - *lufs) - (targetTp - targetLufs);
}

detail::PlanInputs Session::planInputs (const Project& project) const noexcept
{
    detail::PlanInputs in;
    in.rules = detail::rules();
    in.row = project.target;
    in.targetEdit = project.targetEdit;
    in.channels = source_.channels;
    in.sampleRate = source_.sampleRate;
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

void Session::replan() noexcept
{
    const auto in = planInputs (project_);
    Key key;
    key.u64 (source_.hash); key.u64 (measurementKey_); key.u64 (config::Config::versions().all);
    const auto core = version();
    key.u64 (core.major); key.u64 (core.minor); key.u64 (core.patch);
    key.u64 (in.row); key.field (in.targetEdit.lufs); key.field (in.targetEdit.tp);
    key.u64 (in.channels); key.u64 (in.sampleRate); key.u64 (in.offered); key.byte (in.needlesCurrent ? 1 : 0);
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
    if (key.h != plan_.key || planRuns_ == 0)
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
            detail::propose (in, proposed, plan_.devices);
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
        plan_.readOnly = plan_.status != PlanStatus::Ready;
    }
    plan_.fromFile = machineFromFile_;
    plan_.awaitedFraction = 0.0;
    if (plan_.awaited == Analyzer::Excursions && needlesJob_ != 0) plan_.awaitedFraction = needlesProgress_.fraction;
    else if (plan_.awaited == Analyzer::Tempo && sourceMeasurements_ && source_.frames != 0
             && sourceMeasurements_->cursor < sourceMeasurements_->order.size()
             && sourceMeasurements_->order[sourceMeasurements_->cursor] == Analyzer::Tempo)
        plan_.awaitedFraction = double (sourceMeasurements_->frames) / double (source_.frames);
}
} // namespace felitronics::session
