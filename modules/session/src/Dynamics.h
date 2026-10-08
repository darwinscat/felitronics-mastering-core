// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

// THE GLUE AND THE SATURATION AS THE CHAIN GETS THEM (internal to modules/session; owner decisions 3.8, 3.8а, 3.9).
//
// ONE SYSTEM OF LEVELS. The input reaches the chain brought to [input] referenceLufs by one gain — the landing search
// adds it to the chain's input gain, once — and everything a device ahead of the limiter compares with a level is read in
// that system: the short-term P95 the glue's threshold stands on and the true peak the saturation's drive is counted
// from are the measured ones plus that gain. So one mix exported louder or quieter gets the same compressor and the same
// shaper, and the search, which moves only the gain before the limiter, recalibrates neither.
//
// THE GLUE'S KNOB, "up to N dB", is the loss on the loud places: the travel g at which the core's own static curve
// (dynamics::GainComputer, its soft knee included) takes exactly N dB at the loud places — one smooth formula over the
// whole domain ([glue] in engine.toml), found on that curve by bisection. The loud places stand, on the detector's
// scale, [glue] detectorOverP95Db above the short-term P95: the calibration that makes the knob the gain reduction
// music really gets there (the median of the owner's mixes), not only the curve's. THE SATURATION'S KNOB is the drive the input would
// get at 0 dBTP: the shaper is driven at k = 10^(drive/20) − 1, so its gain is what is aligned.

#include "Planner.h"
#include "Rules.h"

#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Session.h>

#include <optional>

namespace felitronics::session::detail
{
// The compressor's values at a travel g: each by its law of [glue], from `from` at 0 towards `to` at 1 and on past it.
struct GlueCurve
{
    double travel = 0.0, ratio = 1.0, threshOffsetDb = 0.0, kneeDb = 0.0, attackMs = 0.0, divisor = 1.0;
};
[[nodiscard]] GlueCurve glueAt (const Rules& rules, double travel) noexcept;
// What the core's static curve takes at the loud places with those values, dB: the knob's N at that travel.
[[nodiscard]] double glueStaticLossDb (const GlueCurve& curve) noexcept;
// The values at a knob of `upToDb`: the travel whose static loss at the loud places is `upToDb`.
[[nodiscard]] GlueCurve glueFor (const Rules& rules, double upToDb) noexcept;
// The release those values give at `bpm`: a beat over the travel's divisor, within [compressor.limits] release, ms.
[[nodiscard]] double glueReleaseMs (const Rules& rules, const GlueCurve& curve, double bpm) noexcept;
// [compressor.tempo] bpmWhenUnsure.
[[nodiscard]] double glueBpmWhenUnsure (const Rules& rules) noexcept;

// The input's levels in the normalised system, where they were measured: the gain to [input] referenceLufs, the
// short-term P95 and the true peak after it.
struct InputLevels
{
    std::optional<double> gainDb, p95Db, truePeakDb;
};
[[nodiscard]] InputLevels inputLevels (const PlanInputs& in) noexcept;

// The tempo a device may use: the measured one when its label is [compressor.tempo] trustedConfidence; the fallback when
// the measurement ended otherwise; not ready while it runs or is stopped.
[[nodiscard]] TempoChoice tempoChoice (const Rules& rules, const MeasurementResult& tempo) noexcept;
// The tempo a ready result heard — its finite, positive headline BPM, whatever its label; none otherwise.
[[nodiscard]] std::optional<double> tempoHeard (const MeasurementResult& tempo) noexcept;

// A person ticked the glue on without touching a knob the machine left at 0: the knob takes [glue] whenTicked and the
// five [glue] ticked's character (the threshold the travel's at the amount), as the machine's layer.
[[nodiscard]] bool glueTicked (const Layers<GlueFields>& glue) noexcept;
// The glue knob as it sounds: a person's value, or the machine's — and [glue] whenTicked where glueTicked.
[[nodiscard]] double glueKnob (const Rules& rules, const Layers<GlueFields>& glue) noexcept;
// A person ticked the saturation on without touching a drive the machine left at 0: the drive takes [saturation]
// whenTicked, as the machine's layer.
[[nodiscard]] bool saturationTicked (const Layers<SaturationFields>& saturation) noexcept;
// The saturation's drive as it sounds: a person's value, or the machine's — and [saturation] whenTicked where
// saturationTicked.
[[nodiscard]] double saturationKnob (const Rules& rules, const Layers<SaturationFields>& saturation) noexcept;

// The travel's five at a knob of `upToDb` on this input: ratio, knee and attack always; the threshold where the P95 is
// measured; the release once a tempo is decided, or at [compressor.tempo] bpmWhenUnsure when `waitsForTempo` is false.
// No person's field and no state: glueFinding lays those over it, and the machine's layer takes it at its own amount.
[[nodiscard]] GlueFinding glueLaw (const PlanInputs& in, double upToDb, bool waitsForTempo) noexcept;

// The five a field left alone takes — the machine's layer as it applies: the law at the knob as it sounds, on this
// input's P95 and decided tempo, the release at [compressor.tempo] bpmWhenUnsure until one is decided; where glueTicked,
// [glue] ticked's character over it. No person's five enter it, so no field's machine value moves with another's hand
// value.
[[nodiscard]] GlueFinding glueMachine (const PlanInputs& in, const Devices& devices) noexcept;

// What the project's glue and saturation come to on this input.
[[nodiscard]] GlueFinding glueFinding (const PlanInputs& in, const Devices& devices) noexcept;
[[nodiscard]] SaturationFinding saturationFinding (const PlanInputs& in, const Devices& devices) noexcept;

// THE CLIPPER'S PARAMETERS — the chain's saturator stage at a type, the shaper's drive and a mix, with [saturation]
// bias, autoComp and dcBlockHz and no trim (the landing sets the level before the limiter, so a trim would be undone).
// writeDynamics writes the stage with it, and the kit draws its curve from it (Kit::saturationCurve).
[[nodiscard]] saturation::Saturator::Params clipperParams (const Rules& rules, SaturationType type, double driveDb,
                                                           double mix) noexcept;
// THE WRITE: the compressor and the clipper (the chain's tanh saturator stage) of `params`, their bypasses and the
// compressor's mix, from the findings above — every field of both stages named, so the sound depends on no default of the
// core's. A glue that is out, unavailable or still waiting for its tempo, and a saturation that is off, are bypassed.
// The chain's input gain is not touched: the normalising gain is the landing search's to add, once.
void writeDynamics (const PlanInputs& in, const Devices& devices, mastering::MasteringChainParams& params) noexcept;

// WHAT THE SONG GOT FROM THE GLUE (owner, 08.10), dB, positive: the drop of the parallel stage's output — the dry signal
// at 1 - mix under the compressed one at mix — against its input, where the compressed path is reduced by `reductionDb`.
// The compressor applies a gain and no makeup, and the dry path is aligned to it, so sample by sample the output is the
// input times (1 - mix) + mix * 10^(-reductionDb / 20). The drop grows with the reduction, so the largest sample of one is
// the largest of the other, and a P95 of one is the other's P95 (the report's 4 ms windows average in dB, and the blend of
// a window's mean stands for the mean of its blend). `mix` is the share the stage applies
// (MasteringChainResolved::compressorMix). At mix 1 the reduction as it is, to the bit; at mix 0, +0.
[[nodiscard]] double glueTakenDb (double reductionDb, double mix) noexcept;
// THE WATERFALL: a wished share at or under this is 0 % — the zone's stage leaves the chain (the page sends shares
// rounded to 0.001).
inline constexpr double kZeroShare = 0.0005;
} // namespace felitronics::session::detail
