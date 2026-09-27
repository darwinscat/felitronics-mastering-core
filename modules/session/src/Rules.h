// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE NUMBERS THE SESSION'S COMMANDS CHECK AGAINST, READ IN PLACE (internal to modules/session). The knobs' travels and
// steps, the targets' rows and the defaults a device starts from, read straight from the config compiled into the
// library (felitronics-toml's embedded documents) — not from Config::load(), which builds tables and binds structs and
// so allocates: a command's demand is declared before it runs (law 11d), and reading the config costs none. The numbers
// are the documents' own decimals, which is what a knob's grid is checked on (src/Grid.h).
//
// ONE SCHEMA, AND THIS READS WHAT IT ALREADY HELD. Every build runs the config's schema over these documents before the
// library is built, so every number read here is one the schema required and checked; `complete` says every read found
// it. The state suite holds every number of this reading to the schema's binding of the same documents
// (Config::load()), and reads a document with keys missing to see `complete` go false.

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{

using Decimal = toml::Decimal;

// A knob: every value from `from` to `to`, in whole steps of `step` counted from `from`.
struct Knob
{
    Decimal from {}, to {}, step {};
};

// One row of [targets] (targets.toml), as far as the commands read it.
struct TargetRow
{
    std::string_view key;
    Decimal lufs {}, tp {}, monoBass {}, hpfFloor {};
    std::int32_t hpfSlope = 0;                 // hpfSlopeDbPerOct
    std::int32_t bitDepth = 0;
    bool noClipper = false;
    std::optional<Decimal> lowShelfDb;
    std::optional<Decimal> glue;               // [glue] byTarget, on the knob, when it names this target
};

struct Rules
{
    bool complete = false;                     // every number below was where the schema puts it
    toml::embedded::View targets, engine;      // the two documents
    std::uint16_t rows = 0;                    // [targets]
    std::uint16_t defaultRow = 0;              // `default`
    Knob lufs {}, tp {};                       // [edit] lufs, tp
    Knob hpfFq {};                             // [hpf] hzMin…hzMax, in whole hertz
    Decimal hpfDefault {};                     // [hpf] hzDefault
    Knob monoBassFq {}, monoBassWidth {};      // [monoBass] frequencyRange / frequencyStep, lowWidthRange / lowWidthStep
    Decimal monoBassWidthDefault {};           // [monoBass] lowWidth
    Knob glue {};                              // [glue] knobMinDb…knobMaxDb by knobStepDb: "up to N dB"
    Decimal glueDefault {};                    // [glue] default, on the knob
    Knob drive {}, mix {}, output {};          // [saturation] driveRange, mixRange, outputRange, by their steps
    Decimal driveDefault {}, mixDefault {}, outputDefault {};   // [saturation] driveDb, mix, outputDb
    Knob tilt {}, lowShelf {};                 // [tilt] hard / step, [lowShelf] hard / step
    Knob needles {};                           // [limiter.peakClipper] manualMinDb…manualMaxDb by manualStepDb
    Decimal needlesDefault {};                 // [limiter.peakClipper] betweenOverDb: where the manual threshold starts
    bool eq = false, monoBass = false, compressor = false, clipper = false, dither = false;   // [stages]
    std::int32_t ditherUpToBits = 0;           // [dither] onUpToBits

    // Row `i` of [targets], in the order written (i < rows).
    [[nodiscard]] TargetRow row (std::uint16_t i) const noexcept;
    // The row whose key is `key`.
    [[nodiscard]] std::optional<std::uint16_t> find (std::string_view key) const noexcept;
    // Is `dbPerOct` one of [hpf] slopes?
    [[nodiscard]] bool slope (std::int32_t dbPerOct) const noexcept;
};

// The rules of two documents — `complete` false when a number is not where the schema puts it. Reads, allocates nothing.
[[nodiscard]] Rules readRules (toml::embedded::View targets, toml::embedded::View engine) noexcept;

// The rules of the config compiled into the library (src/Config.cpp).
[[nodiscard]] Rules rules() noexcept;

} // namespace felitronics::session::detail
