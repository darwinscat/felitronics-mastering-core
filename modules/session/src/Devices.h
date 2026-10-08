// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE DEVICES, FOR THE CODE (internal to modules/session). Project.h writes each device's fields once, as a template over
// a field's form; this file lists them once more for the code that walks them — each in the order written, with the
// rule its value is checked by — so an edit, a revert, a person's layer and the machine's are walked by one loop. A field
// added in Project.h and not here would be walked by nothing: the state suite counts, for every device, the fields
// walked here against its mask, which is one bool per field and nothing else.

#include "Rules.h"

#include <felitronics/session/Project.h>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{

// How a field's value is checked: a tick (anything goes), a knob (finite, within its domain), a filter slope
// of [hpf], or one of the Needles modes.
struct FieldRule
{
    enum class Kind : std::uint8_t { Flag, Knob, Slope, Needles, SaturationType, Oversampling };
    Kind kind = Kind::Flag;
    Knob knob {};
};
inline FieldRule flagRule() noexcept { return {}; }
inline FieldRule knobRule (const Knob& k) noexcept { return { FieldRule::Kind::Knob, k }; }
inline FieldRule slopeRule() noexcept { return { FieldRule::Kind::Slope, {} }; }
inline FieldRule needlesRule() noexcept { return { FieldRule::Kind::Needles, {} }; }
inline FieldRule saturationTypeRule() noexcept { return { FieldRule::Kind::SaturationType, {} }; }
inline FieldRule oversamplingRule() noexcept { return { FieldRule::Kind::Oversampling, {} }; }
// A waterfall share (the glue's and the saturation's `share`, the limiter's `cutShare`): a fraction of the peak work, on
// 0…1 with no step (the page moves it by 0.001) — a rule of its own, no device's knob read from the config.
inline FieldRule shareRule() noexcept
{
    Knob k;
    k.from = { 0, 1, false }; k.to = { 10, 1, false }; k.step = { 0, 1, false };
    k.domain = Knob::Domain::Bounded; k.minimum = k.from; k.maximum = k.to;
    return knobRule (k);
}

// The saturation types a person may pick (the page offers them): Tanh, the four of felitronics-core v0.57.0, and the two
// diodes (owner, 07.10): Cubic, the symmetric, and Asym, the asymmetric. Atan is the config's only — a research setting,
// never a hand choice.
[[nodiscard]] constexpr bool handSaturationType (SaturationType t) noexcept
{
    return t == SaturationType::Tanh || t == SaturationType::Tube || t == SaturationType::Transistor
        || t == SaturationType::Transformer || t == SaturationType::Tape || t == SaturationType::Cubic || t == SaturationType::Asym;
}

// The types' names, in the config and in a project file: [saturation] shape, `type.hand = "tape"`.
inline constexpr std::string_view kSaturationTypeNames[] = { "tanh", "atan", "cubic", "asym", "tube", "transistor",
                                                             "transformer", "tape" };
static_assert (std::size (kSaturationTypeNames) == std::size_t (SaturationType::Tape) + 1);
[[nodiscard]] constexpr std::optional<SaturationType> saturationTypeNamed (std::string_view name) noexcept
{
    for (std::size_t i = 0; i < std::size (kSaturationTypeNames); ++i)
        if (kSaturationTypeNames[i] == name) return SaturationType (i);
    return std::nullopt;
}

// DeviceOf<a device's fields, in any form>:
//   device        which device
//   layers(d)     its two layers in Devices
//   each(r, v, s...)  v(index, rule, s.field...) for every field, in the order Project.h writes them; s... are the same
//                     device's fields in any forms (an edit and a person's layer, a mask and a person's layer, …)
template <class Fields> struct DeviceOf;

template <template <class> class F> struct DeviceOf<HpfFields<F>>
{
    static constexpr Device device = Device::Hpf;
    static constexpr std::string_view name = "hpf";
    static constexpr std::string_view fields[] = { "on", "fq", "slope" };
    static auto& layers (Devices& d) noexcept { return d.hpf; }
    static const auto& layers (const Devices& d) noexcept { return d.hpf; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.hpfFq), s.fq...);
        v (2, slopeRule(), s.slope...);
    }
};

template <template <class> class F> struct DeviceOf<MonoBassFields<F>>
{
    static constexpr Device device = Device::MonoBass;
    static constexpr std::string_view name = "monoBass";
    static constexpr std::string_view fields[] = { "on", "fq", "width" };
    static auto& layers (Devices& d) noexcept { return d.monoBass; }
    static const auto& layers (const Devices& d) noexcept { return d.monoBass; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.monoBassFq), s.fq...);
        v (2, knobRule (r.monoBassWidth), s.width...);
    }
};

template <template <class> class F> struct DeviceOf<GlueFields<F>>
{
    static constexpr Device device = Device::Glue;
    static constexpr std::string_view name = "glue";
    static constexpr std::string_view fields[] = { "on", "upToDb", "mix", "thresholdDb", "ratio", "kneeDb", "attackMs",
                                                   "releaseMs", "share" };
    static auto& layers (Devices& d) noexcept { return d.glue; }
    static const auto& layers (const Devices& d) noexcept { return d.glue; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.glue), s.upToDb...);
        v (2, knobRule (r.glueMix), s.mix...);
        v (3, knobRule (r.glueThreshold), s.thresholdDb...);
        v (4, knobRule (r.glueRatio), s.ratio...);
        v (5, knobRule (r.glueKnee), s.kneeDb...);
        v (6, knobRule (r.glueAttack), s.attackMs...);
        v (7, knobRule (r.glueRelease), s.releaseMs...);
        v (8, shareRule(), s.share...);
    }
};

template <template <class> class F> struct DeviceOf<SaturationFields<F>>
{
    static constexpr Device device = Device::Saturation;
    static constexpr std::string_view name = "saturation";
    static constexpr std::string_view fields[] = { "on", "drive", "mix", {}, "type", "share" };   // 3 names nothing
    static auto& layers (Devices& d) noexcept { return d.saturation; }
    static const auto& layers (const Devices& d) noexcept { return d.saturation; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.drive), s.drive...);
        v (2, knobRule (r.mix), s.mix...);
        v (4, saturationTypeRule(), s.type...);
        v (5, shareRule(), s.share...);
    }
};

template <template <class> class F> struct DeviceOf<TiltFields<F>>
{
    static constexpr Device device = Device::Tilt;
    static constexpr std::string_view name = "tilt";
    static constexpr std::string_view fields[] = { "on", "db" };
    static auto& layers (Devices& d) noexcept { return d.tilt; }
    static const auto& layers (const Devices& d) noexcept { return d.tilt; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.tilt), s.db...);
    }
};

template <template <class> class F> struct DeviceOf<LimiterFields<F>>
{
    static constexpr Device device = Device::Limiter;
    static constexpr std::string_view name = "limiter";
    static constexpr std::string_view fields[] = { "needles", "needlesDb", "releaseMs", "lookaheadMs", "oversampling",
                                                   "cutShare" };
    static auto& layers (Devices& d) noexcept { return d.limiter; }
    static const auto& layers (const Devices& d) noexcept { return d.limiter; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, needlesRule(), s.needles...);
        v (1, knobRule (r.needles), s.needlesDb...);
        v (2, knobRule (r.limiterRelease), s.releaseMs...);
        v (3, knobRule (r.limiterLookahead), s.lookaheadMs...);
        v (4, oversamplingRule(), s.oversampling...);
        v (5, shareRule(), s.cutShare...);
    }
};

template <template <class> class F> struct DeviceOf<DitherFields<F>>
{
    static constexpr Device device = Device::Dither;
    static constexpr std::string_view name = "dither";
    static constexpr std::string_view fields[] = { "on" };
    static auto& layers (Devices& d) noexcept { return d.dither; }
    static const auto& layers (const Devices& d) noexcept { return d.dither; }
    template <class V, class... S> static void each (const Rules&, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
    }
};

template <template <class> class F> struct DeviceOf<LowFields<F>>
{
    static constexpr Device device = Device::Low;
    static constexpr std::string_view name = "low";
    static constexpr std::string_view fields[] = { "on", "db" };
    static auto& layers (Devices& d) noexcept { return d.low; }
    static const auto& layers (const Devices& d) noexcept { return d.low; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.low), s.db...);
    }
};

template <template <class> class F> struct DeviceOf<BandsFields<F>>
{
    static constexpr Device device = Device::Bands;
    static constexpr std::string_view name = "bands";
    static constexpr std::string_view fields[] = { "body", "mud", "forward", "brightness", "air", "on" };
    static auto& layers (Devices& d) noexcept { return d.bands; }
    static const auto& layers (const Devices& d) noexcept { return d.bands; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, knobRule (r.bands[0]), s.body...);
        v (1, knobRule (r.bands[1]), s.mud...);
        v (2, knobRule (r.bands[2]), s.forward...);
        v (3, knobRule (r.bands[3]), s.brightness...);
        v (4, knobRule (r.bands[4]), s.air...);
        v (5, flagRule(), s.on...);
    }
};

// WHERE A DEVICE'S TICK COMES FROM (the owner's rule: a person's edit always sounds). The person's own tick when they
// set one — on or off; otherwise ON when any of the device's fields carries a person's value (a knob turned is a
// device wanted: `[tilt] db.hand = 3` sounds with no tick written); otherwise the machine's.
template <template <template <class> class> class F>
TickFrom tickFrom (const Rules& rules, const Layers<F>& layers) noexcept
{
    if constexpr (requires { layers.hand.on; })
    {
        if (layers.hand.on) return TickFrom::Hand;
        bool touched = false;
        DeviceOf<F<Value>>::each (rules, [&] (std::uint8_t, const FieldRule&, const auto& hand) { touched = touched || hand.has_value(); },
                                  layers.hand);
        return touched ? TickFrom::Touched : TickFrom::Machine;
    }
    else
        return TickFrom::Machine;
}

// A DEVICE'S SETTINGS in the project — what sounds: the machine's layer with a person's touched fields over it, its tick
// by tickFrom() (the machine's layer alone where `withHand` is false).
template <template <template <class> class> class F>
F<Value> settingsOf (const Rules& rules, const Layers<F>& layers, bool withHand = true) noexcept
{
    F<Value> out = layers.machine;
    if (withHand)
    {
        DeviceOf<F<Value>>::each (rules, [] (std::uint8_t, const FieldRule&, auto& value, const auto& hand)
        {
            if (hand) value = *hand;
        }, out, layers.hand);
        if constexpr (requires { out.on; })
            if (tickFrom (rules, layers) == TickFrom::Touched) out.on = true;
    }
    return out;
}

// Every device's layers, in the order of Device: v(device, layers).
template <class D, class V> void eachDevice (D& devices, V&& v)
{
    v (Device::Hpf, devices.hpf);
    v (Device::MonoBass, devices.monoBass);
    v (Device::Glue, devices.glue);
    v (Device::Saturation, devices.saturation);
    v (Device::Tilt, devices.tilt);
    v (Device::Limiter, devices.limiter);
    v (Device::Dither, devices.dither);
    v (Device::Low, devices.low);
    v (Device::Bands, devices.bands);
}

// THE CONFIG'S DEFAULTS of every device for the target in row `row` and a source of `channels` channels (0: none) — the
// numbers the machine starts from: [stages] for the ticks before anything is measured (the planner then decides every
// tick it has a rule for — mono bass by its loss whatever [stages] says), the target's row for what the target decides (the high-pass's
// slope and floor, the mono-bass crossover, the needles off where the target has no peak clipper, the dither at its bit
// depth, the low shelf's gain, the glue of [glue] byTarget) and each device's section for the rest. Tilt starts off at
// 0 dB, and the EQ bands on at 0 dB: the machine does not touch timbre, and never takes the bands out of the chain. They are the defaults layer, not a decision taken
// from a measurement: the planner (src/Planner.h) proposes the machine's layer from them, and a project file writes a
// machine value only where it differs from them.
void placeDefaults (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept;

// Is `device` offered for the target in row `row` and a source of `channels` channels?
// Dither where the target's bit depth is one it serves; mono bass except on a mono source (it has no
// side). Every other device, always.
[[nodiscard]] bool offered (const Rules& rules, std::uint16_t row, std::uint32_t channels, Device device) noexcept;

} // namespace felitronics::session::detail
