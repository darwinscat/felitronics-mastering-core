// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE CONFIG'S VERSIONS, ONE WALK FOR TWO TREES (internal to modules/session; Config.h has the public face). A 64-bit FNV-1a
// hash of the documents' NORMALISED data, targets, engine, then felitronics-bands' bands.toml. The same bytes are fed from the data compiled into the
// library (toml::embedded::View — Config::versions(), no allocation, which is why the C ABI can answer it) and from a
// document parsed from text (toml::Table — Config::versionsOf(), how the source files are hashed).
//
// NORMALISED, so that what is no data does not move it:
//   * a number — an integer or a decimal — is the 64 bits of its correctly rounded double, with −0 read as +0: 50, 50.0
//     and 50.00 are one value, and so are 0.1 and 0.10;
//   * a table's entries are walked in the byte order of their keys, by selection over the entries (no allocation): the
//     order keys are written in is no data, and neither is whether a table is inline or under a header — with one
//     exception, the rows of [targets], whose order is the order a shell lists them in after the main ones: `all` walks
//     them as written, `sound` by key;
//   * an array keeps its order — there it means something (the main targets, a series, a band's edges).
// UNAMBIGUOUS: every value starts with a byte naming its kind; a string and a key are their length and their bytes; an
// entry is a marker, its key and its value, and a table closes with an end byte; an array is its count and its items.
//
// TWO VERSIONS. `all` walks everything. `sound` is what can change a master, and when it is not sure it keeps a key: it
// leaves out only what the lists below name — what is only shown, prints a finding or a warning, is measured after the
// master, or serves development — and, while no shell offers the de-esser (deEsser.offered = false), the de-esser's block
// with the measurement only it reads. A recipe records `sound`, so a master names the numbers it was made with and
// nothing else. Moving a key between the two is a change of these lists and of the suite that holds them
// (tests/ConfigTests.cpp).

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <variant>

namespace felitronics::session::config::detail
{

class Fnv
{
public:
    constexpr void byte (std::uint8_t b) noexcept { h_ = (h_ ^ b) * 1099511628211ull; }
    constexpr void u32 (std::uint32_t x) noexcept
    {
        for (int i = 0; i < 4; ++i) byte (static_cast<std::uint8_t> (x >> (8 * i)));
    }
    constexpr void u64 (std::uint64_t x) noexcept
    {
        for (int i = 0; i < 8; ++i) byte (static_cast<std::uint8_t> (x >> (8 * i)));
    }
    constexpr void text (std::string_view s) noexcept
    {
        u32 (static_cast<std::uint32_t> (s.size()));
        for (const char c : s) byte (static_cast<std::uint8_t> (c));
    }
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return h_; }

private:
    std::uint64_t h_ = 14695981039346656037ull;
};

// The kind bytes, shared by both walks so they cannot disagree on a spelling.
enum : std::uint8_t { kString = 0, kNumber = 1, kBoolean = 2, kArray = 3, kTable = 4, kEntry = 5, kEnd = 6 };

// A number as the bits of its double, −0 as +0.
inline void feedNumber (Fnv& f, double x) noexcept
{
    const std::uint64_t bits = std::bit_cast<std::uint64_t> (x);
    f.byte (kNumber);
    f.u64 (bits == 0x8000000000000000ull ? 0u : bits);
}

// WHAT CANNOT CHANGE A MASTER, by key path: left out of `sound`. A path covers everything under it.
inline constexpr std::string_view kTargetsPresentation[] = {
    "main",                                            // the order of the list
    "notes",                                           // the note shown beside a target
    "edit.lufs.from", "edit.lufs.to", "edit.lufs.green",   // the hand edit's travels and green ranges; a master holds its
    "edit.tp.from", "edit.tp.to", "edit.tp.green",         // own numbers, whatever the travel (the steps stay in)
};
inline constexpr std::string_view kEnginePresentation[] = {
    "defaults",                                        // the name of this set of defaults — a label, recorded beside it
    "limiter.peakClipper.densityMinusDb",              // the density near the ceiling, printed beside the class
    "limiter.peakClipper.densityWithinDb",
    "hpf.slopesNormal",                                // which slopes warn
    "hpf.comfort",                                     // the knob field's colours
    "hpf.curveTopDb", "hpf.curveBottomDb", "hpf.curveStepDb", "hpf.curveHeadroomDb",
    "hpf.marks",
    "monoBass.zones",                                  // the knob scale's regions
    "saturation.cut",                                  // measured after the master
    "tilt.normal",                                     // where a knob's value turns red
    "low.normal",
    "bands.body.normal", "bands.mud.normal", "bands.forward.normal", "bands.brightness.normal", "bands.air.normal",
    "eq",                                              // the summed curve's colours and scale
    // The observations weight and print findings; none of these switches a device — polarity included: mono bass is
    // decided by the loss of its own band, never by the programme's correlation.
    "observations.doubtfulBelow", "observations.clipping", "observations.dcOffset", "observations.bitsUnused",
    "observations.edgeSilence", "observations.hum", "observations.humWandered", "observations.spectralWall",
    "observations.infraLow", "observations.wideBass", "observations.polarity", "observations.alreadyLimited", "observations.vinylTop", "observations.kinds",
    "observations.sibilance",
    "observations.alreadyMastered",                    // a source-report finding, no device setting
    "crest",                                           // measured after the master
    "cost",                                            // measured after the master
    "progress",                                        // the progress bar's weights
    "blindTest",                                       // a development tool
};
// ...and, while no shell offers the de-esser, its block and the measurement only it reads.
inline constexpr std::string_view kEngineWhileNoDeEsser[] = { "deEsser", "stereoBursts" };
// The table whose entries `all` walks in their written order.
inline constexpr std::string_view kTargetsRowOrder = "targets";

// The key path being walked, without allocating: the keys from the root, at most one per level.
struct Path
{
    std::string_view part[toml::kMaxDepth + 1];
    std::size_t size = 0;
};

// Not string_view::substr: it checks its position and throws, and under -fno-exceptions that check links libc++'s abort
// path, printf included — a tenth of the wasm module for a bound this loop already keeps.
inline bool equals (std::string_view dotted, const Path& p) noexcept
{
    std::size_t i = 0, at = 0;
    for (;;)
    {
        const std::size_t dot = dotted.find ('.', at);
        const std::size_t end = dot == std::string_view::npos ? dotted.size() : dot;
        const std::string_view part (dotted.data() + at, end - at);
        if (i >= p.size || part != p.part[i]) return false;
        ++i;
        if (dot == std::string_view::npos) return i == p.size;
        at = dot + 1;
    }
}

// What a walk leaves out (nothing for `all`), and which table it walks in written order (only `all` does).
struct Skip
{
    const std::string_view* paths = nullptr;
    std::size_t count = 0;
    const std::string_view* more = nullptr;
    std::size_t moreCount = 0;
    std::string_view written;
    [[nodiscard]] bool covers (const Path& p) const noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
            if (equals (paths[i], p)) return true;
        for (std::size_t i = 0; i < moreCount; ++i)
            if (equals (more[i], p)) return true;
        return false;
    }
    [[nodiscard]] bool inWrittenOrder (const Path& p) const noexcept { return ! written.empty() && equals (written, p); }
};

//==============================================================================
// The data compiled into the library.

inline void feed (Fnv& f, toml::embedded::View v, Path& path, const Skip& skip) noexcept
{
    using toml::embedded::Type;
    const auto type = v.type();
    if (! type || path.size > toml::kMaxDepth) return;
    switch (*type)
    {
        case Type::String:  f.byte (kString); f.text (*v.string()); break;
        case Type::Integer: feedNumber (f, static_cast<double> (*v.integer())); break;
        case Type::Decimal: feedNumber (f, v.decimal()->toDouble()); break;
        case Type::Boolean: f.byte (kBoolean); f.byte (*v.boolean() ? 1 : 0); break;
        case Type::Array:
        case Type::Tables:
            f.byte (kArray);
            f.u32 (static_cast<std::uint32_t> (v.size()));
            for (const auto item : v) feed (f, item, path, skip);
            break;
        case Type::Table:
        {
            f.byte (kTable);
            const bool written = skip.inWrittenOrder (path);
            std::string_view last;
            for (std::size_t fed = 0; fed < v.size(); ++fed)
            {
                std::size_t best = written ? fed : v.size();
                for (std::size_t i = 0; i < v.size() && ! written; ++i)
                {
                    const std::string_view k = v[i].key();
                    if ((fed == 0 || last < k) && (best == v.size() || k < v[best].key())) best = i;
                }
                if (best == v.size()) break;
                last = v[best].key();
                path.part[path.size++] = last;
                if (! skip.covers (path))
                {
                    f.byte (kEntry);
                    f.text (last);
                    feed (f, v[best], path, skip);
                }
                --path.size;
            }
            f.byte (kEnd);
            break;
        }
    }
}

//==============================================================================
// A document parsed from text: the same bytes for the same data.

inline void feed (Fnv& f, const toml::Value& v, Path& path, const Skip& skip) noexcept;
inline void feed (Fnv& f, const toml::Table& t, Path& path, const Skip& skip) noexcept
{
    if (path.size > toml::kMaxDepth) return;
    const auto& entries = t.entries();
    f.byte (kTable);
    const bool written = skip.inWrittenOrder (path);
    std::string_view last;
    for (std::size_t fed = 0; fed < entries.size(); ++fed)
    {
        std::size_t best = written ? fed : entries.size();
        for (std::size_t i = 0; i < entries.size() && ! written; ++i)
        {
            const std::string_view k = entries[i].key;
            if ((fed == 0 || last < k) && (best == entries.size() || k < std::string_view (entries[best].key))) best = i;
        }
        if (best == entries.size()) break;
        last = entries[best].key;
        path.part[path.size++] = last;
        if (! skip.covers (path))
        {
            f.byte (kEntry);
            f.text (last);
            feed (f, entries[best].value, path, skip);
        }
        --path.size;
    }
    f.byte (kEnd);
}
inline void feed (Fnv& f, const toml::Value& v, Path& path, const Skip& skip) noexcept
{
    if (path.size > toml::kMaxDepth) return;
    if (const auto* s = std::get_if<std::string> (&v.data)) { f.byte (kString); f.text (*s); }
    else if (const auto* n = std::get_if<std::int64_t> (&v.data)) feedNumber (f, static_cast<double> (*n));
    else if (const auto* d = std::get_if<toml::Decimal> (&v.data)) feedNumber (f, d->toDouble());
    else if (const auto* b = std::get_if<bool> (&v.data)) { f.byte (kBoolean); f.byte (*b ? 1 : 0); }
    else if (const auto* a = std::get_if<toml::Array> (&v.data))
    {
        f.byte (kArray);
        f.u32 (static_cast<std::uint32_t> (a->size()));
        for (const auto& item : *a) feed (f, item, path, skip);
    }
    else if (const auto* t = std::get_if<toml::Table> (&v.data)) feed (f, *t, path, skip);
    else if (const auto* ts = std::get_if<toml::Tables> (&v.data))
    {
        f.byte (kArray);
        f.u32 (static_cast<std::uint32_t> (ts->size()));
        for (const auto& table : *ts) feed (f, table, path, skip);
    }
}

// Is the de-esser offered by a shell? Absent or not a boolean reads as offered — the answer that keeps its keys in `sound`.
inline bool deEsserOffered (toml::embedded::View engine) noexcept
{
    return engine.find ("deEsser").find ("offered").boolean().value_or (true);
}
inline bool deEsserOffered (const toml::Table& engine) noexcept
{
    const toml::Value* d = engine.find ("deEsser");
    const auto* t = d != nullptr ? std::get_if<toml::Table> (&d->data) : nullptr;
    const toml::Value* o = t != nullptr ? t->find ("offered") : nullptr;
    const auto* b = o != nullptr ? std::get_if<bool> (&o->data) : nullptr;
    return b == nullptr || *b;
}

// The three documents, targets first, each behind a tag of its own. felitronics-bands' bands.toml — the filters of tilt,
// low and the EQ bands — is sound, every key of it, in both versions.
template <class Tree> std::uint64_t version (const Tree& targets, const Tree& engine, const Tree& bands, bool sound) noexcept
{
    const bool deEsser = deEsserOffered (engine);
    const Skip targetsSkip = sound ? Skip { kTargetsPresentation, std::size (kTargetsPresentation), nullptr, 0, {} }
                                   : Skip { nullptr, 0, nullptr, 0, kTargetsRowOrder };
    const Skip engineSkip = sound ? Skip { kEnginePresentation, std::size (kEnginePresentation),
                                           deEsser ? nullptr : kEngineWhileNoDeEsser,
                                           deEsser ? 0 : std::size (kEngineWhileNoDeEsser), {} }
                                  : Skip {};
    Fnv f;
    Path path;
    f.byte ('T');
    feed (f, targets, path, targetsSkip);
    f.byte ('E');
    feed (f, engine, path, engineSkip);
    f.byte ('B');
    feed (f, bands, path, Skip {});
    return f.value();
}

} // namespace felitronics::session::config::detail
