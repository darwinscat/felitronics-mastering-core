// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE WHOLE PLAN AS THE CHAIN GETS IT (src/Chain.h).

#include "BuildGuards.h"

#include "Chain.h"
#include "Devices.h"
#include "Dynamics.h"
#include "EqCurve.h"
#include "Limiter.h"
#include "BuildContract.h"

#include <algorithm>
#include <cstdint>

namespace felitronics::session::detail
{
namespace
{
using View = toml::embedded::View;
double number (View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    storageOverflow();
}
int whole (View v) noexcept
{
    const auto i = v.integer();
    if (! i) storageOverflow();
    return int (*i);
}
} // namespace

void writeChain (const PlanInputs& in, const Devices& devices, command::MasterReady& ready) noexcept
{
    ready = {};
    ready.version = 0;
    auto& topology = ready.topology;
    auto& params = ready.params;
    const auto engine = in.rules.engine;
    const auto offeredHere = [&] (Device device)
    {
        return (in.offered & (1u << unsigned (device))) != 0 && offered (in.rules, in.row, in.channels, device);
    };

    // The geometry no device decides.
    topology.internalBlock = whole (engine.find ("chain").find ("internalBlock"));
    // The oversampling and the limiter's lookahead are the limiter's fields since 08.10: a person's where set, else the
    // machine's — [chain] oversampleFactor and [limiter] lookaheadMs, as before.
    const auto limiterSettings = settingsOf (in.rules, devices.limiter);
    topology.oversampleFactor = int (limiterSettings.oversampling);
    topology.tapsPerPhase = whole (engine.find ("chain").find ("tapsPerPhase"));
    topology.compressorLookaheadMs = number (engine.find ("compressor").find ("lookaheadMs"));
    topology.sidechainHpfHz = number (engine.find ("compressor").find ("sidechainHpfHz"));
    topology.limiterLookaheadMs = limiterSettings.lookaheadMs;

    // Neither gain is a device's: the landing search normalises the input and lands the loudness.
    params.inputGainDb = 0.0;
    params.preLimiterGainDb = 0.0;

    // THE EQ STAGE: the high-pass, tilt and low, each its own band; in the chain when one of them is on.
    EqStage stage;
    writeEq (devices, in.rules, stage);
    std::copy (std::begin (stage.bands), std::end (stage.bands), std::begin (params.eqBands));
    topology.eq = std::any_of (std::begin (stage.bands), std::end (stage.bands), [] (const eq::BandParams& b) { return b.on; });
    params.bypassEq = false;

    // MONO BASS: the fold below the crossover, at the width kept. A mono source has no side.
    const auto mono = settingsOf (in.rules, devices.monoBass);
    topology.monoBass = mono.on && offeredHere (Device::MonoBass);
    params.monoBass = {};
    params.monoBass.enabled = topology.monoBass;
    params.monoBass.frequencyHz = float (mono.fq);
    params.monoBass.lowWidth = float (mono.width);
    params.bypassMonoBass = false;
    // The side's air shelf is no device of this release: out of the chain, its values the neutral ones.
    topology.stereoAir = false;
    params.stereoAir = {};
    params.stereoAir.enabled = false;
    params.stereoAir.gainDb = 0.0f;

    // THE GLUE'S COMPRESSOR AND THE SATURATION: in the chain when they act.
    writeDynamics (in, devices, params);
    topology.compressor = glueFinding (in, devices).state == GlueState::Active;
    topology.clipper = saturationFinding (in, devices).active;

    // THE LIMITER, always, with its peak clipper inside; the dither where the delivery takes it.
    writeLimiter (in, devices, params);
    topology.limiter = true;
    topology.dither = ditherFinding (in, devices).on;
}
} // namespace felitronics::session::detail
