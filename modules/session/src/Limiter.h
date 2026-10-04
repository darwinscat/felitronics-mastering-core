// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

// THE LIMITER WITH ITS NEEDLES, AND THE DITHER, AS THE CHAIN GETS THEM (internal to modules/session; owner decisions
// 3.6, 3.7, 3.12, technical decision 3О11).
//
// THE LIMITER is always in the chain, with no tick. The ceiling is hard; how far it goes for the loudness is held by its
// budget ([landing] limiterBudget: the P95 of its gain reduction on its active windows, by the target's loudness) and by
// nothing else — no PLR, no other cost. Its one knob is the PEAK CLIPPER's, inside the limiter's oversampler
// (never a stage of its own): decide by itself, cut as much as a person sets, or not at all.
//
// DECIDING BY ITSELF, the clipper reads the needles measured on the INPUT at the ceiling the target's numbers give — the
// input's peak less the NEED, (input peak − input loudness) − (ceiling − target loudness) — and classes them
// ([limiter.peakClipper]): short needles lose up to shortCutDb off their peaks, the ones between up to betweenCutDb,
// and long, bassy, already-limited or clipped material is not cut at all; the limiter does the rest. A need of
// littleNeedDb or less is not measured. The class is THE MACHINE'S ANSWER; what sounds is the project's knob, and a
// person's manual cut sounds over every refusal of the machine's, with the machine's reason beside it. The amount — the
// class's or a person's — is what the finding carries (`overDb`); the chain's threshold is max(0, need − amount) above
// the ceiling (writeLimiter).

#include "Planner.h"
#include "Rules.h"

#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>

#include <optional>

namespace felitronics::session::detail
{
// THE NEED the target's numbers — a person's edits of them included — set an input of this loudness and true peak, dB.
// Absent without both readings.
[[nodiscard]] std::optional<double> loudnessNeed (const Rules& rules, std::uint16_t row, const TargetFields<Touched>& edit,
                                                  const MeasurementResult& loudness) noexcept;

// THE KNOB AS THE PROJECT GIVES IT: a person's mode where they set one; MANUAL where they turned the threshold and set
// no mode (a knob turned is a device wanted — the tick rule, for a device without a tick); else the machine's. The
// amount off the peaks is a person's, else the machine's layer's.
struct NeedlesKnob
{
    Needles mode = Needles::Auto;
    double overDb = 0.0;
    bool byHand = false;                       // a person's field decides the mode
};
[[nodiscard]] NeedlesKnob needlesKnob (const Layers<LimiterFields>& limiter, bool withHand = true) noexcept;

// THE MACHINE'S ANSWER for the inputs — its class, the first reason it does not cut (NeedlesWhy's order) and what it
// stood on. It reads the needles only where `in.needlesCurrent`: a result measured at another ceiling is Pending.
struct NeedlesAnswer
{
    NeedlesClass proposed = NeedlesClass::None;
    NeedlesWhy why = NeedlesWhy::NoReadings;
    MeasurementReason reason = MeasurementReason::None;
    std::optional<double> needDb, overDb, p90Ms, bassShare, plrDb, clipsPerMinute;
    std::uint64_t clips = 0;
};
[[nodiscard]] NeedlesAnswer needlesAnswer (const PlanInputs& in) noexcept;

// What the project's limiter and dither come to on this input and target.
[[nodiscard]] LimiterFinding limiterFinding (const PlanInputs& in, const Devices& devices) noexcept;
[[nodiscard]] DitherFinding ditherFinding (const PlanInputs& in, const Devices& devices) noexcept;

// THE WRITE: the limiter — its release, the peak clipper's flag, threshold and knee — and the dither of `params`, every
// field of both stages named, so the sound depends on no default of the core's. The ceiling written is the one the
// master holds; the landing search stands its first limiter ceiling a margin under it and moves it from there.
void writeLimiter (const PlanInputs& in, const Devices& devices, mastering::MasteringChainParams& params) noexcept;
} // namespace felitronics::session::detail
