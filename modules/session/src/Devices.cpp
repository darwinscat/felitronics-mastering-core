// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE MACHINE'S LAYER AND WHAT IS OFFERED WHERE (src/Devices.h). Every number comes from the config through the rules
// read in place (src/Rules.h), as the decimal written, and becomes a double here by one correctly rounded division.

#include "BuildGuards.h"

#include "Devices.h"
#include "Grid.h"
#include "Rules.h"

#include <felitronics/session/Project.h>

#include <cstdint>

namespace felitronics::session::detail
{

bool offered (const Rules& rules, std::uint16_t row, std::uint32_t channels, Device device) noexcept
{
    switch (device)
    {
        case Device::LowShelf: return rules.row (row).lowShelfDb.has_value();
        case Device::Dither:   return rules.row (row).bitDepth <= rules.ditherUpToBits;
        case Device::MonoBass: return channels != 1;
        case Device::Hpf:
        case Device::Glue:
        case Device::Saturation:
        case Device::Tilt:
        case Device::Limiter:  return true;
    }
    return false;
}

void placeDefaults (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept
{
    const TargetRow target = rules.row (row);
    const auto number = [] (const Decimal& d) { return kept (d.toDouble()); };   // −0 as +0, as every number kept

    auto& hpf = devices.hpf.machine;
    hpf.on = rules.eq;
    // hzDefault, never below the target's floor.
    hpf.fq = number (compare (rules.hpfDefault, target.hpfFloor) < 0 ? target.hpfFloor : rules.hpfDefault);
    hpf.slope = target.hpfSlope;

    auto& mono = devices.monoBass.machine;
    mono.on = rules.monoBass && offered (rules, row, channels, Device::MonoBass);
    mono.fq = number (target.monoBass);
    mono.width = number (rules.monoBassWidthDefault);

    auto& glue = devices.glue.machine;
    glue.on = rules.compressor;
    glue.upToDb = number (target.glue ? *target.glue : rules.glueDefault);

    auto& sat = devices.saturation.machine;
    sat.on = rules.clipper;
    sat.drive = number (rules.driveDefault);
    sat.mix = number (rules.mixDefault);
    sat.output = number (rules.outputDefault);

    auto& tilt = devices.tilt.machine;
    tilt.on = rules.eq;
    tilt.db = 0.0;

    auto& limiter = devices.limiter.machine;
    limiter.needles = target.noClipper ? Needles::Off : Needles::Auto;
    limiter.needlesDb = number (rules.needlesDefault);

    devices.dither.machine.on = rules.dither && offered (rules, row, channels, Device::Dither);

    auto& shelf = devices.lowShelf.machine;
    shelf.on = rules.eq && target.lowShelfDb.has_value();
    shelf.db = target.lowShelfDb ? number (*target.lowShelfDb) : 0.0;
}

void placeMachine (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept
{
    placeDefaults (rules, row, channels, devices);
}

} // namespace felitronics::session::detail
