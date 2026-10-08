// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// Allocation-free reads of the build-checked embedded config: domains, slider hints, targets and defaults.
// The schema proves the shape before compilation. A broken required lookup is a contract trap, not a runtime status.
// State tests compare these reads with Config::load()'s typed binding.

#include <felitronics/session/Project.h>
#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{

using Decimal = toml::Decimal;

// The EQ bands' keys under [bands], in the order Project.h writes BandsFields (and Rules::bands holds their knobs).
inline constexpr std::string_view kBandNames[] = { "body", "mud", "forward", "brightness", "air" };

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
enum class TargetClass : std::uint8_t { Specification, Streaming, Other };
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
    TargetClass targetClass = TargetClass::Other;
    LoudnessMode loudnessMode = LoudnessMode::Manual;   // loudnessMode: a max row's mode
    std::optional<Decimal> lowDb;
    std::optional<Decimal> glue;               // [glue] byTarget, on the knob, when it names this target
};

struct Rules
{
    toml::embedded::View targets, engine;      // the two documents
    toml::embedded::View geometry;             // felitronics-bands' bands.toml [bands]: each EQ device's type, hz, q
    std::uint16_t rows = 0;                    // [targets]
    std::uint16_t defaultRow = 0;              // `default`
    Knob lufs {}, tp {};                       // [edit] lufs, tp
    Knob hpfFq {};                             // [hpf] hzMin…hzMax, slider hints
    Decimal hpfTop {};                         // [hpf] machineTopHz: the machine's cutoff never goes above it
    Knob monoBassFq {}, monoBassWidth {};      // [monoBass] frequencyRange / frequencyStep, lowWidthRange / lowWidthStep
    Decimal monoBassWidthDefault {};           // [monoBass] lowWidth
    Knob glue {};                              // [glue] knobMinDb…knobMaxDb by knobStepDb: "up to N dB"
    Decimal glueDefault {};                    // [glue] default, on the knob
    Knob glueMix {};                           // [glue] mixRange by mixStep in mixDomain: the parallel share
    Decimal glueMixDefault {};                 // [glue] mix
    Knob glueThreshold {}, glueRatio {}, glueKnee {}, glueAttack {}, glueRelease {};   // [glue] the five by hand
    Knob drive {}, mix {};                     // [saturation] driveRange, mixRange, by their steps
    Decimal driveDefault {}, mixDefault {};   // [saturation] driveDb, mix
    std::string_view shapeDefault;             // [saturation] shape: the machine's type (Devices.h names them)
    Knob tilt {}, low {};                 // [tilt] hard / step, [low] hard / step
    Knob bands[5] {};                          // [bands] body, mud, forward, brightness, air: hard / step / domain
    Knob needles {};                           // [limiter.peakClipper] manualMinDb…manualMaxDb by manualStepDb
    Decimal needlesDefault {};                 // [limiter.peakClipper] betweenCutDb: where the manual cut starts
    Knob limiterRelease {}, limiterLookahead {};   // [limiter] releaseMs…, lookaheadMs… by hand (08.10)
    Decimal limiterReleaseDefault {}, limiterLookaheadDefault {};   // [limiter] releaseMs, lookaheadMs: the machine's
    std::int32_t oversamplingDefault = 0;      // [chain] oversampleFactor: the machine's oversampling
    bool eq = false, monoBass = false, compressor = false, clipper = false, dither = false;   // [stages]
    std::int32_t ditherUpToBits = 0;           // [dither] onUpToBits

    // Row `i` of [targets], in the order written (i < rows).
    [[nodiscard]] TargetRow row (std::uint16_t i) const noexcept;
    // The row whose key is `key`.
    [[nodiscard]] std::optional<std::uint16_t> find (std::string_view key) const noexcept;
    // Is dbPerOct in the filter order domain (multiples of 6, 6 through 96)?
    [[nodiscard]] bool slope (std::int32_t dbPerOct) const noexcept;
    // [limiter] oversamplingDomain: a power of two within it — a factor the limiter's oversampler takes by hand.
    [[nodiscard]] bool oversampling (std::int32_t factor) const noexcept;
};

// Read two build-checked documents without allocations.
[[nodiscard]] Rules readRules (toml::embedded::View targets, toml::embedded::View engine, toml::embedded::View bands) noexcept;

// The rules of the config compiled into the library (src/Config.cpp).
[[nodiscard]] Rules rules() noexcept;

// The loudness mode a project's next master lands in: the person's edit, else the target row's.
[[nodiscard]] inline LoudnessMode loudnessModeOf (const Rules& r, const Project& p) noexcept
{
    return p.targetEdit.loudnessMode.value_or (r.row (p.target).loudnessMode);
}


} // namespace felitronics::session::detail
