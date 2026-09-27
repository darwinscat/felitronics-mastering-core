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

void placeMachine (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept
{
    const TargetRow target = rules.row (row);

    auto& hpf = devices.hpf.machine;
    hpf.on = rules.eq;
    // hzDefault, never below the target's floor.
    hpf.fq = (compare (rules.hpfDefault, target.hpfFloor) < 0 ? target.hpfFloor : rules.hpfDefault).toDouble();
    hpf.slope = target.hpfSlope;

    auto& mono = devices.monoBass.machine;
    mono.on = rules.monoBass && offered (rules, row, channels, Device::MonoBass);
    mono.fq = target.monoBass.toDouble();
    mono.width = rules.monoBassWidthDefault.toDouble();

    auto& glue = devices.glue.machine;
    glue.on = rules.compressor;
    glue.amount = (target.glue ? *target.glue : rules.glueDefault).toDouble();

    auto& sat = devices.saturation.machine;
    sat.on = rules.clipper;
    sat.drive = rules.driveDefault.toDouble();
    sat.mix = rules.mixDefault.toDouble();
    sat.output = rules.outputDefault.toDouble();

    auto& tilt = devices.tilt.machine;
    tilt.on = rules.eq;
    tilt.db = 0.0;

    auto& limiter = devices.limiter.machine;
    limiter.needles = target.noClipper ? Needles::Off : Needles::Auto;
    limiter.needlesDb = rules.needlesDefault.toDouble();

    devices.dither.machine.on = rules.dither && offered (rules, row, channels, Device::Dither);

    auto& shelf = devices.lowShelf.machine;
    shelf.on = rules.eq && target.lowShelfDb.has_value();
    shelf.db = target.lowShelfDb ? target.lowShelfDb->toDouble() : 0.0;
}

} // namespace felitronics::session::detail
