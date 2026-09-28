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

namespace felitronics::session::detail
{

// How a field's value is checked: a tick (anything goes), a knob (finite, within its domain), a filter slope
// of [hpf], or one of the Needles modes.
struct FieldRule
{
    enum class Kind : std::uint8_t { Flag, Knob, Slope, Needles };
    Kind kind = Kind::Flag;
    Knob knob {};
};
inline FieldRule flagRule() noexcept { return {}; }
inline FieldRule knobRule (const Knob& k) noexcept { return { FieldRule::Kind::Knob, k }; }
inline FieldRule slopeRule() noexcept { return { FieldRule::Kind::Slope, {} }; }
inline FieldRule needlesRule() noexcept { return { FieldRule::Kind::Needles, {} }; }

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
    static constexpr std::string_view fields[] = { "on", "upToDb" };
    static auto& layers (Devices& d) noexcept { return d.glue; }
    static const auto& layers (const Devices& d) noexcept { return d.glue; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.glue), s.upToDb...);
    }
};

template <template <class> class F> struct DeviceOf<SaturationFields<F>>
{
    static constexpr Device device = Device::Saturation;
    static constexpr std::string_view name = "saturation";
    static constexpr std::string_view fields[] = { "on", "drive", "mix", "output" };
    static auto& layers (Devices& d) noexcept { return d.saturation; }
    static const auto& layers (const Devices& d) noexcept { return d.saturation; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.drive), s.drive...);
        v (2, knobRule (r.mix), s.mix...);
        v (3, knobRule (r.output), s.output...);
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
    static constexpr std::string_view fields[] = { "needles", "needlesDb" };
    static auto& layers (Devices& d) noexcept { return d.limiter; }
    static const auto& layers (const Devices& d) noexcept { return d.limiter; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, needlesRule(), s.needles...);
        v (1, knobRule (r.needles), s.needlesDb...);
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

template <template <class> class F> struct DeviceOf<LowShelfFields<F>>
{
    static constexpr Device device = Device::LowShelf;
    static constexpr std::string_view name = "lowShelf";
    static constexpr std::string_view fields[] = { "on", "db" };
    static auto& layers (Devices& d) noexcept { return d.lowShelf; }
    static const auto& layers (const Devices& d) noexcept { return d.lowShelf; }
    template <class V, class... S> static void each (const Rules& r, V&& v, S&&... s)
    {
        v (0, flagRule(), s.on...);
        v (1, knobRule (r.lowShelf), s.db...);
    }
};

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
    v (Device::LowShelf, devices.lowShelf);
}

// THE MACHINE'S LAYER of every device for the target in row `row` and a source of `channels` channels (0: none) — the
// numbers the devices start from, from the config: [stages] for the ticks, the target's row for what the target decides
// (the high-pass's slope and floor, the mono-bass crossover, the needles off where the target has no peak clipper, the
// dither at its bit depth, the low shelf's gain, the glue of [glue] byTarget) and each device's section for the rest.
// Tilt starts at 0 dB: the machine does not touch timbre. These are the config's defaults, not a decision taken from a
// measurement — no planner reads the measurements here, so mono bass, which [stages] leaves off, is off. The session
// places the devices on these numbers when the first measurement ends, and a change of target after that places them
// again.
void placeDefaults (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept;

// The current planner places defaults; file omissions are filled independently of planner decisions.
void placeMachine (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices, std::uint32_t offeredDevices = 255u) noexcept;

// Is `device` offered for the target in row `row` and a source of `channels` channels? The low shelf where the target
// carries one; the dither where the target's bit depth is one it serves; mono bass except on a mono source (it has no
// side). Every other device, always.
[[nodiscard]] bool offered (const Rules& rules, std::uint16_t row, std::uint32_t channels, Device device) noexcept;

} // namespace felitronics::session::detail
