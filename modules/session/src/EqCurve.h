// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

// THE ONE EQ STAGE THE EQ DEVICES SHARE (internal to modules/session) — the chain's EQ, MasteringChainParams::eqBands. The
// high-pass, tilt, low and the EQ bands are four devices and one stage: each owns its own bands of it, by the fixed table
// of eqBand() and the slots [bands] names, and writes only those, so none overwrites another's and one's tick bypasses
// nothing but its own. The curve the snapshot shows is drawn FROM THE BANDS WRITTEN, by the designs felitronics-core's
// EqEngine runs for them (designBand: the high-pass's Butterworth cascade, tilt's two shelves, the resonant low shelf, the
// bands' bells and high shelves) at the rate the chain processes at, so the curve is what sounds — the EQ-stage tests hold
// the curve to core's response and to the engine's own output. A new EQ device takes bands of its own here; a type no EQ
// device writes is refused by the curve (a contract fault).

#include "Rules.h"

#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>

#include <cstdint>
#include <span>

namespace felitronics::session::detail
{
struct EqStage
{
    eq::BandParams bands[eq::EqEngine::kMaxBands] {};
};

// The band a device owns, or −1 for a device that writes none or several: the high-pass 0, tilt 1, low 2. The EQ bands
// device writes five, each the band [bands] gives it (bandsSlot: i in the order of kBandNames).
[[nodiscard]] int eqBand (Device device) noexcept;
[[nodiscard]] int bandsSlot (const Rules& rules, std::size_t i) noexcept;

// Every EQ device writes its band from its settings in the project — the machine's layer with a person's over it, by the
// tick rule (settingsOf). A device that is off, or a shelf at 0 dB, writes its band off. The other bands are left as
// they are. (The summed curve is drawn from it today; the render of a session-decided master goes through it next.)
void writeEq (const Devices& devices, const Rules& rules, EqStage& stage) noexcept;
// ...from the four EQ devices' settings as they sound (what settingsOf gives; the pure kit's preview passes them as given).
// A band of the EQ bands at 0 dB leaves its slot as the default band, off: the stage the chain got before the device;
// the EQ bands off leave all five so.
void writeEq (const HpfFields<Value>& hpf, const TiltFields<Value>& tilt, const LowFields<Value>& low,
              const BandsFields<Value>& bands, const Rules& rules, EqStage& stage) noexcept;

// The summed response of `bands` at `rate`: kEqCurvePoints logarithmic points from 20 Hz to min(20 kHz, 0.49 · rate).
void eqCurve (std::span<const eq::BandParams> bands, double rate, std::span<EqPoint> output) noexcept;

// ...of the bands the project's EQ devices write.
void eqCurve (const Project& project, const Rules& rules, double rate, std::span<EqPoint> output) noexcept;
// The same curve with the high-pass's band out: tilt, low and the EQ bands (SnapshotView::eqOnlyCurve).
void eqOnlyCurve (const Project& project, const Rules& rules, double rate, std::span<EqPoint> output) noexcept;

// THE EQ CURVE AGAINST ITS NORM (EqFinding, Session.h): the curve eqOnlyCurve draws — tilt, low and the EQ bands, as they
// sound, the high-pass out — on the snapshot's kEqCurvePoints at `rate`, against [eq] curve.warnDb.
[[nodiscard]] EqFinding eqFinding (const Devices& devices, const Rules& rules, double rate) noexcept;
// ...of the bands a written stage holds.
[[nodiscard]] EqFinding eqFinding (const EqStage& stage, const Rules& rules, double rate) noexcept;

// THE HIGH-PASS AS THE CHAIN RUNS IT — the same cascade the band above describes (cutoff `fc`, `slope` dB/oct, at `rate`,
// the cutoff clamped as the engine clamps it). The loss it takes at `hz`, dB, positive; and the cutoff at which it takes
// exactly `lossDb` at `hz`, found on that response by bisection — never below the lowest cutoff the engine designs, where
// the answer is that one.
[[nodiscard]] double highPassLossDb (double fc, std::int32_t slope, double rate, double hz) noexcept;
[[nodiscard]] double highPassCutoffFor (double hz, double lossDb, std::int32_t slope, double rate) noexcept;
}
