// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE CONFIG'S DEFAULTS AND WHAT IS OFFERED WHERE (src/Devices.h). Every number comes from the config through the rules
// read in place (src/Rules.h), as the decimal written, and becomes a double here by one correctly rounded division.

#include "BuildGuards.h"

#include "Devices.h"
#include "Dynamics.h"
#include "Grid.h"
#include "Rules.h"
#include "BuildContract.h"

#include <felitronics/session/Project.h>

#include <cstdint>

namespace felitronics::session::detail
{

bool offered (const Rules& rules, std::uint16_t row, std::uint32_t channels, Device device) noexcept
{
    switch (device)
    {
        case Device::Dither:   return rules.row (row).bitDepth <= rules.ditherUpToBits;
        case Device::MonoBass: return channels != 1;
        case Device::Hpf:
        case Device::Glue:
        case Device::Saturation:
        case Device::Tilt:
        case Device::Low:
        case Device::Bands:
        case Device::Limiter:  return true;
    }
    storageOverflow();
}

void placeDefaults (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept
{
    const TargetRow target = rules.row (row);
    const auto number = [] (const Decimal& d) { return kept (d.toDouble()); };   // −0 as +0, as every number kept

    auto& hpf = devices.hpf.machine;
    hpf.on = rules.eq;
    // The target's floor: where the machine's cutoff stands until a sure lowest note allows a higher one.
    hpf.fq = number (target.hpfFloor);
    hpf.slope = target.hpfSlope;

    auto& mono = devices.monoBass.machine;
    mono.on = rules.monoBass && offered (rules, row, channels, Device::MonoBass);
    mono.fq = number (target.monoBass);
    mono.width = number (rules.monoBassWidthDefault);

    // The machine glues only where [glue] byTarget names the target; elsewhere the glue is a person's.
    auto& glue = devices.glue.machine;
    glue.on = rules.compressor && target.glue.has_value();
    glue.upToDb = number (target.glue ? *target.glue : rules.glueDefault);
    glue.mix = number (rules.glueMixDefault);
    // The five: the travel's values at the machine's amount; the threshold at its domain's top and the release at
    // [compressor.tempo] bpmWhenUnsure until the planner has a P95 and a tempo to put them on.
    const auto curve = glueFor (rules, glue.upToDb);
    glue.thresholdDb = number (rules.glueThreshold.maximum);
    glue.ratio = kept (curve.ratio);
    glue.kneeDb = kept (curve.kneeDb);
    glue.attackMs = kept (curve.attackMs);
    glue.releaseMs = kept (glueReleaseMs (rules, curve, glueBpmWhenUnsure (rules)));

    auto& sat = devices.saturation.machine;
    sat.on = rules.clipper;
    sat.drive = number (rules.driveDefault);
    sat.mix = number (rules.mixDefault);
    // The machine never picks a type: its layer holds the config's, which the build checked is one of the eight.
    const auto type = saturationTypeNamed (rules.shapeDefault);
    if (! type) storageOverflow();
    sat.type = *type;

    // Tilt is a person's: the machine never ticks it and never sets it.
    auto& tilt = devices.tilt.machine;
    tilt.on = false;
    tilt.db = 0.0;

    auto& limiter = devices.limiter.machine;
    limiter.needles = target.noClipper ? Needles::Off : Needles::Auto;
    limiter.needlesDb = number (rules.needlesDefault);
    // The three by hand (08.10): the machine's are the constants the chain had — [limiter] releaseMs and lookaheadMs,
    // [chain] oversampleFactor.
    limiter.releaseMs = number (rules.limiterReleaseDefault);
    limiter.lookaheadMs = number (rules.limiterLookaheadDefault);
    limiter.oversampling = rules.oversamplingDefault;

    devices.dither.machine.on = rules.dither && offered (rules, row, channels, Device::Dither);

    auto& shelf = devices.low.machine;
    shelf.on = rules.eq && target.lowDb.has_value();
    shelf.db = target.lowDb ? number (*target.lowDb) : 0.0;

    // The EQ bands are a person's: the machine leaves every one at 0 dB, and the device on (it never bypasses it).
    devices.bands.machine = {};
    devices.bands.machine.on = true;
}

} // namespace felitronics::session::detail
