// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE CONFIG'S VERSIONS, ONE WALK FOR TWO TREES (internal to modules/session; Config.h has the public face). A 64-bit FNV-1a
// hash of both documents' NORMALISED data, targets then engine. The same bytes are fed from the data compiled into the
// library (toml::embedded::View — config::versions(), no allocation, which is why the C ABI can answer it) and from a
// document parsed from text (toml::Table — config::versionsOf(), how the source files are hashed).
//
// NORMALISED, so that what is no data does not move it:
//   * a number — an integer or a decimal — is the 64 bits of its correctly rounded double, with −0 read as +0: 50, 50.0
//     and 50.00 are one value, and so are 0.1 and 0.10;
//   * a table's entries are walked in the byte order of their keys, by selection over the entries (no allocation): the
//     order keys are written in is no data, and neither is whether a table is inline or under a header;
//   * an array keeps its order — there it means something (the main targets, a series, a band's edges).
// UNAMBIGUOUS: every value starts with a byte naming its kind; a string and a key are their length and their bytes; an
// entry is a marker, its key and its value, and a table closes with an end byte; an array is its count and its items.
//
// TWO VERSIONS. `all` walks everything. `sound` leaves out what cannot change a master — kPresentation below: what is
// only shown, measured after the master, or used in development — so that a recipe, which records `sound`, names the
// numbers a master was made with and nothing else. Moving a key between the two is a change of this list and of the
// suite that holds it (tests/ConfigTests.cpp).

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
    "default",          // the target a session starts with — a master names its own
    "main",             // the order of the list
    "edit.lufs.green",  // the ranges drawn green
    "edit.tp.green",
};
inline constexpr std::string_view kEnginePresentation[] = {
    "defaults",             // the name of this set of defaults — a label, recorded beside the version
    "hpf.comfort",          // the knob field's colours
    "hpf.curveTopDb", "hpf.curveBottomDb", "hpf.curveStepDb", "hpf.curveHeadroomDb",
    "hpf.marks",
    "monoBass.zones",       // the knob scale's regions
    "eq",                   // the summed curve's colours and scale
    "observations.kinds",   // how a finding is styled
    "crest",                // measured after the master
    "cost",                 // measured after the master
    "progress",             // the progress bar's weights
    "blindTest",            // a development tool
};

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

// Which paths a walk leaves out: none for `all`.
struct Skip
{
    const std::string_view* paths = nullptr;
    std::size_t count = 0;
    [[nodiscard]] bool covers (const Path& p) const noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
            if (equals (paths[i], p)) return true;
        return false;
    }
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
            std::string_view last;
            for (std::size_t fed = 0; fed < v.size(); ++fed)
            {
                std::size_t best = v.size();
                for (std::size_t i = 0; i < v.size(); ++i)
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
    std::string_view last;
    for (std::size_t fed = 0; fed < entries.size(); ++fed)
    {
        std::size_t best = entries.size();
        for (std::size_t i = 0; i < entries.size(); ++i)
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

// Both documents, targets first, each behind a tag of its own. `sound` leaves out kPresentation.
template <class Tree> std::uint64_t version (const Tree& targets, const Tree& engine, bool sound) noexcept
{
    const Skip targetsSkip = sound ? Skip { kTargetsPresentation, std::size (kTargetsPresentation) } : Skip {};
    const Skip engineSkip = sound ? Skip { kEnginePresentation, std::size (kEnginePresentation) } : Skip {};
    Fnv f;
    Path path;
    f.byte ('T');
    feed (f, targets, path, targetsSkip);
    f.byte ('E');
    feed (f, engine, path, engineSkip);
    return f.value();
}

} // namespace felitronics::session::config::detail
