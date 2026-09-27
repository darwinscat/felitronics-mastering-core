// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE NUMBERS THE COMMANDS CHECK AGAINST, READ IN PLACE (src/Rules.h): a walk over two embedded documents that keeps
// every number as the decimal written, and says whether it found every one. Nothing is allocated.

#include "BuildGuards.h"

#include "Rules.h"

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <cstdint>
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
std::optional<Decimal> number (View v) noexcept
{
    if (const auto d = v.decimal(); d && d->valid()) return *d;
    if (const auto n = v.integer(); n && *n <= Decimal::kMaxMantissa / 10 && *n >= -Decimal::kMaxMantissa / 10)
        return Decimal { *n * 10, 1, false };
    return std::nullopt;
}

std::optional<std::int32_t> int32 (View v) noexcept
{
    const auto n = v.integer();
    if (! n || *n < std::numeric_limits<std::int32_t>::min() || *n > std::numeric_limits<std::int32_t>::max())
        return std::nullopt;
    return std::int32_t (*n);
}

// A range, written as [min, max] or as { min, max }.
bool span (View v, Decimal& min, Decimal& max) noexcept
{
    std::optional<Decimal> lo, hi;
    if (v.is (Type::Array) && v.size() == 2) { lo = number (v[0]); hi = number (v[1]); }
    else if (v.is (Type::Table)) { lo = number (v.find ("min")); hi = number (v.find ("max")); }
    if (! lo || ! hi) return false;
    min = *lo;
    max = *hi;
    return true;
}

// ONE READING: every read that misses a number marks the whole reading incomplete.
struct Reading
{
    bool complete = true;

    void read (View v, Decimal& out) noexcept
    {
        if (const auto d = number (v)) out = *d;
        else complete = false;
    }
    void read (View v, std::int32_t& out) noexcept
    {
        if (const auto n = int32 (v)) out = *n;
        else complete = false;
    }
    void read (View v, bool& out) noexcept
    {
        if (const auto b = v.boolean()) out = *b;
        else complete = false;
    }
    // A knob: a range from its key, a step from another.
    void knob (View range, View step, Knob& out) noexcept
    {
        if (! span (range, out.from, out.to)) complete = false;
        read (step, out.step);
    }
    // A knob written as { from, to, step } ([edit] in targets.toml).
    void edit (View v, Knob& out) noexcept
    {
        read (v.find ("from"), out.from);
        read (v.find ("to"), out.to);
        read (v.find ("step"), out.step);
    }
};

// A row, read into `out`; false when a number the schema requires in every row is not there.
bool readRow (View rowView, View byTarget, TargetRow& out) noexcept
{
    Reading r;
    out.key = rowView.key();
    r.read (rowView.find ("lufs"), out.lufs);
    r.read (rowView.find ("tp"), out.tp);
    r.read (rowView.find ("monoBass"), out.monoBass);
    r.read (rowView.find ("hpfFloor"), out.hpfFloor);
    r.read (rowView.find ("hpfSlopeDbPerOct"), out.hpfSlope);
    r.read (rowView.find ("bitDepth"), out.bitDepth);
    // Optional in a row: absent is their default.
    if (const View v = rowView.find ("noClipper")) r.read (v, out.noClipper);
    else out.noClipper = false;
    if (const View v = rowView.find ("lowShelfDb"))
    {
        Decimal d {};
        r.read (v, d);
        out.lowShelfDb = d;
    }
    else out.lowShelfDb.reset();
    if (const View v = byTarget.find (out.key))
    {
        Decimal d {};
        r.read (v, d);
        out.glue = d;
    }
    else out.glue.reset();
    return r.complete && ! out.key.empty() && rowView.is (Type::Table);
}
} // namespace

Rules readRules (View targets, View engine) noexcept
{
    Rules out;
    out.targets = targets;
    out.engine = engine;
    Reading r;

    // A document holds at most felitronics-toml's kMaxEntries keys, [targets] itself among them, so every row's index fits
    // the project's 16 bits after any document the parser took.
    static_assert (toml::kMaxEntries - 1 <= std::numeric_limits<std::uint16_t>::max());
    const View rows = targets.find ("targets");
    if (! rows.is (Type::Table) || rows.size() == 0 || rows.size() > std::numeric_limits<std::uint16_t>::max()) r.complete = false;
    else out.rows = std::uint16_t (rows.size());
    if (const auto name = targets.find ("default").string(); name && out.rows != 0)
    {
        if (const auto row = out.find (*name)) out.defaultRow = *row;
        else r.complete = false;
    }
    else r.complete = false;

    const View edit = targets.find ("edit");
    r.edit (edit.find ("lufs"), out.lufs);
    r.edit (edit.find ("tp"), out.tp);

    const View hpf = engine.find ("hpf");
    r.read (hpf.find ("hzMin"), out.hpfFq.from);
    r.read (hpf.find ("hzMax"), out.hpfFq.to);
    out.hpfFq.step = Decimal { 10, 1, false };   // the cutoff is whole hertz (the schema's grid for it, and for hpfFloor)
    r.read (hpf.find ("hzDefault"), out.hpfDefault);
    const View slopes = hpf.find ("slopes");
    if (! slopes.is (Type::Array) || slopes.size() == 0) r.complete = false;
    for (const View s : slopes)
        if (! int32 (s)) r.complete = false;

    const View mono = engine.find ("monoBass");
    r.knob (mono.find ("frequencyRange"), mono.find ("frequencyStep"), out.monoBassFq);
    r.knob (mono.find ("lowWidthRange"), mono.find ("lowWidthStep"), out.monoBassWidth);
    r.read (mono.find ("lowWidth"), out.monoBassWidthDefault);

    const View glue = engine.find ("glue");
    out.glue.from = Decimal { 0, 1, false };     // the glue's travel is 0…1 (engine.toml, [glue] THE TRAVEL)
    out.glue.to = Decimal { 10, 1, false };
    r.read (glue.find ("step"), out.glue.step);
    r.read (glue.find ("default"), out.glueDefault);
    const View byTarget = glue.find ("byTarget");
    if (! byTarget.is (Type::Table)) r.complete = false;

    const View sat = engine.find ("saturation");
    r.knob (sat.find ("driveRange"), sat.find ("driveStep"), out.drive);
    r.knob (sat.find ("mixRange"), sat.find ("mixStep"), out.mix);
    r.knob (sat.find ("outputRange"), sat.find ("outputStep"), out.output);
    r.read (sat.find ("driveDb"), out.driveDefault);
    r.read (sat.find ("mix"), out.mixDefault);
    r.read (sat.find ("outputDb"), out.outputDefault);

    const View tilt = engine.find ("tilt");
    r.knob (tilt.find ("hard"), tilt.find ("step"), out.tilt);
    const View shelf = engine.find ("lowShelf");
    r.knob (shelf.find ("hard"), shelf.find ("step"), out.lowShelf);

    const View clipper = engine.find ("limiter").find ("peakClipper");
    r.read (clipper.find ("manualMinDb"), out.needles.from);
    r.read (clipper.find ("manualMaxDb"), out.needles.to);
    r.read (clipper.find ("manualStepDb"), out.needles.step);
    r.read (clipper.find ("betweenOverDb"), out.needlesDefault);

    const View stages = engine.find ("stages");
    r.read (stages.find ("eq"), out.eq);
    r.read (stages.find ("monoBass"), out.monoBass);
    r.read (stages.find ("compressor"), out.compressor);
    r.read (stages.find ("clipper"), out.clipper);
    r.read (stages.find ("dither"), out.dither);
    r.read (engine.find ("dither").find ("onUpToBits"), out.ditherUpToBits);

    // Every row, once: a later row() of a complete reading finds every number.
    for (std::uint16_t i = 0; i < out.rows; ++i)
    {
        TargetRow row;
        if (! readRow (rows[i], byTarget, row)) r.complete = false;
    }
    out.complete = r.complete;
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
    for (const View s : engine.find ("hpf").find ("slopes"))
        if (const auto n = int32 (s); n && *n == dbPerOct) return true;
    return false;
}

} // namespace felitronics::session::detail
