// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE PLANNER (internal to modules/session) — the one place the machine decides. Each of the eight devices proposes its
// machine layer from what it may read (PlanInputs: the target, the source's shape, what the shell offers, the
// measurements) and says what it reads for the settings the project gives it; the planner collects the proposals, and
// the session writes them as the machine's layer, with a person's layer over it. A device decides nothing by itself and
// never writes another's fields: its proposal is its own fields, in the one form Project.h writes them.
//
// A DEVICE is a specialization of Planned<its fields> (src/Planner.cpp), walked in the order of Device:
//   propose (in, machine, plan)   its machine fields from `in`, and into `plan` what held it back and where each field
//                                 came from (the target, a measurement, or the config's default)
//   needs (in, effective)         the analyzers it reads — for its proposal and for what it writes into the chain —
//                                 when the project gives it `effective` (the machine's layer with a person's over it).
//                                 Not the programme's loudness and true peak: every device may read them, and the first
//                                 measurement does not end without them, so no plan waits for them.
// and an EQ device writes its own band of the one EQ stage the curve is drawn from (src/EqCurve.h). A new device is its
// fields in Project.h, its row in Devices.h and its Planned<> — the snapshot, the project file and the commands follow
// from those lists — and the planner test holds every device to having one.

#include "Devices.h"
#include "Rules.h"

#include <felitronics/session/Measurements.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>

#include <cstdint>
#include <span>

namespace felitronics::session::detail
{

// An analyzer as a bit of a set.
[[nodiscard]] constexpr std::uint32_t bitOf (Analyzer a) noexcept { return 1u << unsigned (a); }

// THE INPUTS OF A PLAN — everything a device may read, and nothing else. The planner's answer is a function of these
// alone, and the session keys its cached plan by them (and by a person's layer, which decides what the devices read).
struct PlanInputs
{
    Rules rules {};
    std::uint16_t row = 0;                              // the target: a row of [targets]
    TargetFields<Touched> targetEdit {};                // ...and a person's edits of its numbers
    std::uint32_t channels = 0, sampleRate = 0;         // the source's shape; 0 channels before a source
    std::uint64_t frames = 0;
    std::uint32_t bitDepth = 0;                         // the source's container bits; 0 when it has none (a lossy file)
    std::uint32_t offered = kAllDevices;                // Capabilities::offeredDevices, a bit per Device
    std::span<const MeasurementResult> measurements;    // every analyzer's result, by Analyzer; empty before a source
    // The needles are measured at a ceiling the target's numbers set: the retained result is this project's only when
    // it was measured at this project's ceiling. Another target's result has not ended for this one.
    bool needlesCurrent = false;
};

// HAS A MEASUREMENT ENDED for a device that reads it — with a value, or with its reason? Ready and Unavailable have.
// Pending has not; nor has a Cancelled result of the source's measurement job, which continueMeasurement (or a master)
// resumes. A cancelled needles job HAS ended: it is not built again, and the limiter takes its path without needles.
[[nodiscard]] bool ended (const MeasurementResult& result) noexcept;

// What the devices found, typed, beside their fields: the high-pass's cutoff and the note it came from, mono bass's loss.
struct PlanFindings
{
    HpfFinding hpf {};
    MonoBassFinding monoBass {};
};

// Is the input too quiet to measure — strictly below [input] quiet.gainOnlyLufs? There the machine places no device but
// the high-pass, at its floor, and the dither of the delivery's format. At the boundary itself the input is ordinary.
[[nodiscard]] bool quietInput (const PlanInputs& in) noexcept;

// EVERY DEVICE'S PROPOSAL for the target and the source in `in`: the machine's layers of `machine` (its person's layers
// are not touched), each device's plan (its needs are left to needs()) and what the devices found.
void propose (const PlanInputs& in, Devices& machine, DevicePlans& plans, PlanFindings& found) noexcept;

// WHAT THE DEVICES READ when the project gives them `devices` — the machine's layer, with a person's over it where
// `withHand` — into each device's plan, and their union: a bit per Analyzer.
[[nodiscard]] std::uint32_t needs (const PlanInputs& in, const Devices& devices, bool withHand, DevicePlans& plans) noexcept;

// Mono bass by the loss the low end takes folded to mono, dB ([monoBass] loss): placed under warnFromDb, placed with the
// number from it to offAboveDb, both included, left out above.
[[nodiscard]] MonoBassVerdict monoBassVerdictFor (const Rules& rules, double lossDb) noexcept;

// PLACEMENT: the machine's layers of `devices` become the proposals for `in`, and a person's layer of a device the shell
// does not offer goes (its edits could not be taken now either). The other person's layers stay.
void placeMachine (const PlanInputs& in, Devices& devices) noexcept;

// The needed analyzers that have not ended (by ended()), a bit per Analyzer.
[[nodiscard]] std::uint32_t waiting (const PlanInputs& in, std::uint32_t needs) noexcept;

// The waited-for analyzer the session's work reaches first — the needles job runs ahead of the source's measurement —
// and the first device, in the order of Device, that reads it.
struct Awaited
{
    std::optional<Analyzer> analyzer;
    std::optional<Device> device;
};
[[nodiscard]] Awaited awaited (std::uint32_t waiting, const DevicePlans& plans) noexcept;

// THE PLAN'S REASONS (PlanView::facts), stated by PlanText and nothing else: every line it gives for a Ready plan, and
// the waiting fact the plan's awaited, awaitedBy and awaitedFraction name — which replaces the one stated before it.
// `eq` is the EQ curve against its norm (eqFinding, EqCurve.h), which the plan does not carry. `hands` says which of the
// judged values a person set: the advice beside a knob is raised on a person's value only (owner, 01.10) — the machine's
// own value is its proposal, inside its own rule, and a file's machine layer is the machine's too.
struct AdviceHands
{
    bool hpfFq = false, hpfSlope = false, monoBassFq = false, eqKnobs = false;
};
void stateReasons (PlanView& plan, const EqFinding& eq, const AdviceHands& hands) noexcept;
void stateWaiting (PlanView& plan) noexcept;
// Does any device hold a machine field not measured yet (DevicePlan::pending)? Then no project file is written or read:
// a file would carry a placeholder as the machine's opinion, an import would be compared against an incomplete one.
[[nodiscard]] bool anyPending (const DevicePlans& plans) noexcept;
// A pending plan's card lines: every device with fields not measured yet (DevicePlan::pending) — what it waits for.
void stateUnmeasured (PlanView& plan) noexcept;

// A device's plan in `plans`, by its place in the order of Device.
[[nodiscard]] DevicePlan& planOf (DevicePlans& plans, Device device) noexcept;
[[nodiscard]] const DevicePlan& planOf (const DevicePlans& plans, Device device) noexcept;

} // namespace felitronics::session::detail
