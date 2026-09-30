// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE CONFIG'S DEFAULTS AND WHAT IS OFFERED WHERE (src/Devices.h). Every number comes from the config through the rules
// read in place (src/Rules.h), as the decimal written, and becomes a double here by one correctly rounded division.

#include "BuildGuards.h"

#include "Devices.h"
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

    auto& sat = devices.saturation.machine;
    sat.on = rules.clipper;
    sat.drive = number (rules.driveDefault);
    sat.mix = number (rules.mixDefault);
    sat.output = number (rules.outputDefault);

    // Tilt is a person's: the machine never ticks it and never sets it.
    auto& tilt = devices.tilt.machine;
    tilt.on = false;
    tilt.db = 0.0;

    auto& limiter = devices.limiter.machine;
    limiter.needles = target.noClipper ? Needles::Off : Needles::Auto;
    limiter.needlesDb = number (rules.needlesDefault);

    devices.dither.machine.on = rules.dither && offered (rules, row, channels, Device::Dither);

    auto& shelf = devices.low.machine;
    shelf.on = rules.eq && target.lowDb.has_value();
    shelf.db = target.lowDb ? number (*target.lowDb) : 0.0;
}

} // namespace felitronics::session::detail
