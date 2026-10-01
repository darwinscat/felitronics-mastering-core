// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// Allocation-free reads of the build-checked embedded config: domains, slider hints, targets and defaults.
// The schema proves the shape before compilation. A broken required lookup is a contract trap, not a runtime status.
// State tests compare these reads with Config::load()'s typed binding.

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{

using Decimal = toml::Decimal;

// A knob: independent slider hints and an accepted domain.
struct Knob
{
    Decimal from {}, to {}, step {}; // slider hints
    enum class Domain : std::uint8_t { Bounded, Finite, SourceNyquist };
    Domain domain = Domain::Bounded;
    Decimal minimum {}, maximum {};
    [[nodiscard]] bool accepts (double value, std::uint32_t sourceRate) const noexcept;

};

// One row of [targets] (targets.toml), as far as the commands read it.
struct TargetRow
{
    std::string_view key;
    Decimal lufs {}, tp {}, monoBass {}, hpfFloor {};
    std::int32_t hpfSlope = 0;                 // hpfSlopeDbPerOct
    Decimal noteLossDb {};                     // what the high-pass may take at the lowest note, dB
    std::int32_t bitDepth = 0;
    std::int32_t sampleRate = 0;               // delivery rate, Hz; 0 keeps the source's
    bool noClipper = false;
    bool vinyl = false;                        // the master goes to a cutting lathe: its tp is the medium's ceiling
    std::optional<Decimal> lowDb;
    std::optional<Decimal> glue;               // [glue] byTarget, on the knob, when it names this target
};

struct Rules
{
    toml::embedded::View targets, engine;      // the two documents
    std::uint16_t rows = 0;                    // [targets]
    std::uint16_t defaultRow = 0;              // `default`
    Knob lufs {}, tp {};                       // [edit] lufs, tp
    Knob hpfFq {};                             // [hpf] hzMin…hzMax, slider hints
    Knob monoBassFq {}, monoBassWidth {};      // [monoBass] frequencyRange / frequencyStep, lowWidthRange / lowWidthStep
    Decimal monoBassWidthDefault {};           // [monoBass] lowWidth
    Knob glue {};                              // [glue] knobMinDb…knobMaxDb by knobStepDb: "up to N dB"
    Decimal glueDefault {};                    // [glue] default, on the knob
    Knob drive {}, mix {}, output {};          // [saturation] driveRange, mixRange, outputRange, by their steps
    Decimal driveDefault {}, mixDefault {}, outputDefault {};   // [saturation] driveDb, mix, outputDb
    std::string_view shapeDefault;             // [saturation] shape: the machine's type (Devices.h names them)
    Knob tilt {}, low {};                 // [tilt] hard / step, [low] hard / step
    Knob needles {};                           // [limiter.peakClipper] manualMinDb…manualMaxDb by manualStepDb
    Decimal needlesDefault {};                 // [limiter.peakClipper] betweenCutDb: where the manual cut starts
    bool eq = false, monoBass = false, compressor = false, clipper = false, dither = false;   // [stages]
    std::int32_t ditherUpToBits = 0;           // [dither] onUpToBits

    // Row `i` of [targets], in the order written (i < rows).
    [[nodiscard]] TargetRow row (std::uint16_t i) const noexcept;
    // The row whose key is `key`.
    [[nodiscard]] std::optional<std::uint16_t> find (std::string_view key) const noexcept;
    // Is dbPerOct in the filter order domain (multiples of 6, 6 through 96)?
    [[nodiscard]] bool slope (std::int32_t dbPerOct) const noexcept;
};

// Read two build-checked documents without allocations.
[[nodiscard]] Rules readRules (toml::embedded::View targets, toml::embedded::View engine) noexcept;

// The rules of the config compiled into the library (src/Config.cpp).
[[nodiscard]] Rules rules() noexcept;


} // namespace felitronics::session::detail
