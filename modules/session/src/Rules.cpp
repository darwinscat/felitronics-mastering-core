// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE NUMBERS THE COMMANDS CHECK AGAINST, READ IN PLACE (src/Rules.h): a walk over two embedded documents that keeps
// every number as the decimal written. Missing required values are contract failures. Nothing is allocated.

#include "BuildGuards.h"

#include "Rules.h"
#include "BuildContract.h"

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{
namespace
{
using View = toml::embedded::View;
using Type = toml::embedded::Type;

// A number of a document — an integer or a decimal — as a decimal: an integer n is n at one place.
Decimal number (View v) noexcept
{
    if (const auto d = v.decimal()) return *d;
    if (const auto n = v.integer()) return Decimal { *n * 10, 1, false };
    storageOverflow();
}

std::int32_t int32 (View v) noexcept
{
    const auto n = v.integer();
    if (! n) storageOverflow();
    return std::int32_t (*n);
}

// A range, written as [min, max] or as { min, max }.
void span (View v, Decimal& min, Decimal& max) noexcept
{
    if (v.is (Type::Array)) { min = number (v[0]); max = number (v[1]); }
    else { min = number (v.find ("min")); max = number (v.find ("max")); }
}

// Every required value was checked by the build gate.
struct Reading
{
    void read (View v, Decimal& out) noexcept { out = number (v); }
    void read (View v, std::int32_t& out) noexcept { out = int32 (v); }
    void read (View v, bool& out) noexcept
    {
        const auto b = v.boolean();
        if (! b) storageOverflow();
        out = *b;
    }
    void domain (View v, Knob& out) noexcept
    {
        if (const auto name = v.string())
        {
            if (*name == "finite") out.domain = Knob::Domain::Finite;
            else if (*name == "sourceNyquist") out.domain = Knob::Domain::SourceNyquist;
            else storageOverflow();
        }
        else span (v, out.minimum, out.maximum);
    }
    void knob (View range, View step, View bounds, Knob& out) noexcept
    {
        span (range, out.from, out.to);
        read (step, out.step);
        domain (bounds, out);
    }
    void edit (View v, Knob& out) noexcept
    {
        read (v.find ("from"), out.from); read (v.find ("to"), out.to);
        read (v.find ("step"), out.step); domain (v.find ("domain"), out);
    }
};

// A build-checked row, read into `out`.
void readRow (View rowView, View byTarget, TargetRow& out) noexcept
{
    Reading r;
    out.key = rowView.key();
    const auto targetClass = rowView.find ("class").string();
    if (! targetClass) storageOverflow();
    out.targetClass = *targetClass == "specification" ? TargetClass::Specification
                    : *targetClass == "streaming" ? TargetClass::Streaming : TargetClass::Other;
    r.read (rowView.find ("lufs"), out.lufs);
    r.read (rowView.find ("tp"), out.tp);
    r.read (rowView.find ("monoBass"), out.monoBass);
    r.read (rowView.find ("hpfFloor"), out.hpfFloor);
    r.read (rowView.find ("hpfSlopeDbPerOct"), out.hpfSlope);
    r.read (rowView.find ("noteLossDb"), out.noteLossDb);
    r.read (rowView.find ("bitDepth"), out.bitDepth);
    r.read (rowView.find ("sampleRate"), out.sampleRate);
    // Optional in a row: absent is their default.
    if (const View v = rowView.find ("noClipper")) r.read (v, out.noClipper);
    else out.noClipper = false;
    if (const View v = rowView.find ("vinyl")) r.read (v, out.vinyl);
    else out.vinyl = false;
    out.loudnessMode = LoudnessMode::Manual;
    if (const View v = rowView.find ("loudnessMode"))
    {
        const auto name = v.string().value_or (std::string_view {});
        out.loudnessMode = name == "maxClean" ? LoudnessMode::MaxClean : name == "maxDense" ? LoudnessMode::MaxDense
                                                                                             : LoudnessMode::Manual;
    }
    if (const View v = rowView.find ("lowDb"))
    {
        Decimal d {};
        r.read (v, d);
        out.lowDb = d;
    }
    else out.lowDb.reset();
    if (const View v = byTarget.find (out.key))
    {
        Decimal d {};
        r.read (v, d);
        out.glue = d;
    }
    else out.glue.reset();
}
} // namespace

Rules readRules (View targets, View engine, View geometry) noexcept
{
    Rules out;
    out.targets = targets;
    out.engine = engine;
    out.geometry = geometry;
    Reading r;

    // A document holds at most felitronics-toml's kMaxEntries keys, [targets] itself among them, so every row's index fits
    // the project's 16 bits after any document the parser took.
    static_assert (toml::kMaxEntries - 1 <= std::numeric_limits<std::uint16_t>::max());
    const View rows = targets.find ("targets");
    out.rows = std::uint16_t (rows.size());
    const auto defaultName = targets.find ("default").string();
    if (! defaultName) storageOverflow();
    const auto defaultRow = out.find (*defaultName);
    if (! defaultRow) storageOverflow();
    out.defaultRow = *defaultRow;

    const View edit = targets.find ("edit");
    r.edit (edit.find ("lufs"), out.lufs);
    r.edit (edit.find ("tp"), out.tp);

    const View hpf = engine.find ("hpf");
    r.read (hpf.find ("hzMin"), out.hpfFq.from);
    r.read (hpf.find ("hzMax"), out.hpfFq.to);
    r.read (hpf.find ("machineTopHz"), out.hpfTop);
    r.read (hpf.find ("hzStep"), out.hpfFq.step);
    r.domain (hpf.find ("frequencyDomain"), out.hpfFq);
    const View mono = engine.find ("monoBass");
    r.knob (mono.find ("frequencyRange"), mono.find ("frequencyStep"), mono.find ("frequencyDomain"), out.monoBassFq);
    r.knob (mono.find ("lowWidthRange"), mono.find ("lowWidthStep"), mono.find ("lowWidthDomain"), out.monoBassWidth);
    r.read (mono.find ("lowWidth"), out.monoBassWidthDefault);

    const View glue = engine.find ("glue");
    r.read (glue.find ("knobMinDb"), out.glue.from);
    r.read (glue.find ("knobMaxDb"), out.glue.to);
    r.read (glue.find ("knobStepDb"), out.glue.step);
    r.read (glue.find ("default"), out.glueDefault);
    r.domain (glue.find ("domain"), out.glue);
    r.knob (glue.find ("mixRange"), glue.find ("mixStep"), glue.find ("mixDomain"), out.glueMix);
    r.read (glue.find ("mix"), out.glueMixDefault);
    r.knob (glue.find ("thresholdDbRange"), glue.find ("thresholdDbStep"), glue.find ("thresholdDbDomain"), out.glueThreshold);
    r.knob (glue.find ("ratioRange"), glue.find ("ratioStep"), glue.find ("ratioDomain"), out.glueRatio);
    r.knob (glue.find ("kneeDbRange"), glue.find ("kneeDbStep"), glue.find ("kneeDbDomain"), out.glueKnee);
    r.knob (glue.find ("attackMsRange"), glue.find ("attackMsStep"), glue.find ("attackMsDomain"), out.glueAttack);
    r.knob (glue.find ("releaseMsRange"), glue.find ("releaseMsStep"), glue.find ("releaseMsDomain"), out.glueRelease);

    const View sat = engine.find ("saturation");
    r.knob (sat.find ("driveRange"), sat.find ("driveStep"), sat.find ("driveDomain"), out.drive);
    r.knob (sat.find ("mixRange"), sat.find ("mixStep"), sat.find ("mixDomain"), out.mix);
    r.read (sat.find ("driveDb"), out.driveDefault);
    r.read (sat.find ("mix"), out.mixDefault);
    if (const auto shape = sat.find ("shape").string()) out.shapeDefault = *shape;

    const View tilt = engine.find ("tilt");
    r.knob (tilt.find ("hard"), tilt.find ("step"), tilt.find ("domain"), out.tilt);
    const View shelf = engine.find ("low");
    r.knob (shelf.find ("hard"), shelf.find ("step"), shelf.find ("domain"), out.low);
    const View bands = engine.find ("bands");
    for (std::size_t i = 0; i < std::size (kBandNames); ++i)
    {
        const View band = bands.find (kBandNames[i]);
        r.knob (band.find ("hard"), band.find ("step"), band.find ("domain"), out.bands[i]);
    }

    const View clipper = engine.find ("limiter").find ("peakClipper");
    r.read (clipper.find ("manualMinDb"), out.needles.from);
    r.read (clipper.find ("manualMaxDb"), out.needles.to);
    r.read (clipper.find ("manualStepDb"), out.needles.step);
    r.read (clipper.find ("betweenCutDb"), out.needlesDefault);
    r.domain (clipper.find ("manualDomain"), out.needles);

    const View stages = engine.find ("stages");
    r.read (stages.find ("eq"), out.eq);
    r.read (stages.find ("monoBass"), out.monoBass);
    r.read (stages.find ("compressor"), out.compressor);
    r.read (stages.find ("clipper"), out.clipper);
    r.read (stages.find ("dither"), out.dither);
    r.read (engine.find ("dither").find ("onUpToBits"), out.ditherUpToBits);

    return out;
}

TargetRow Rules::row (std::uint16_t i) const noexcept
{
    TargetRow out;
    (void) readRow (targets.find ("targets")[i], engine.find ("glue").find ("byTarget"), out);
    return out;
}

std::optional<std::uint16_t> Rules::find (std::string_view key) const noexcept
{
    const View all = targets.find ("targets");
    for (std::uint16_t i = 0; i < rows; ++i)
        if (all[i].key() == key) return i;
    return std::nullopt;
}

bool Rules::slope (std::int32_t dbPerOct) const noexcept
{
    const View hpf = engine.find ("hpf");
    const View bounds = hpf.find ("slopeDomain");
    return dbPerOct >= int32 (bounds[0]) && dbPerOct <= int32 (bounds[1])
        && dbPerOct % int32 (hpf.find ("slopeMultiple")) == 0;
}

bool Knob::accepts (double value, std::uint32_t sourceRate) const noexcept
{
    if (domain == Domain::Finite) return true; // caller checks finiteness first
    if (domain == Domain::SourceNyquist) return value > 0 && value < double (sourceRate) * 0.5;
    return value >= minimum.toDouble() && value <= maximum.toDouble();
}

} // namespace felitronics::session::detail
