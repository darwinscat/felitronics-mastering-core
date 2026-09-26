// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE CONFIG VERSION, ONE WALK FOR TWO TREES (internal to modules/session; Config.h has the public face). The version is a
// 64-bit FNV-1a hash of both documents' DATA, targets then engine, walked in document order. The same walk reads the data
// compiled into the library (toml::embedded::View — no allocation, so config::version() can answer through the C ABI
// without a demand to declare) and a document parsed from text (toml::Table — how config::versionOf() hashes the source
// files, and what a shell or a test can compare the library's number with).
//
// WHAT IS FED, and why it cannot be read two ways: every value starts with a byte naming its type, so an integer 3 and a
// decimal 3.0 hash apart; an integer is its 8 bytes, a decimal its 8-byte mantissa, its scale and its negative-zero flag
// (1.5 and 1.50 are different text, and different data), a boolean one byte; a string, and every key, its length and then
// its bytes; a table, an array and an array of tables their count and then their items. So no two trees feed the same
// bytes. Every step of FNV-1a is (h ^ byte) · prime with an odd prime — a bijection of h — so two trees whose bytes differ
// in one place at the same length (any number changed: numbers are fixed width) always hash apart.
//
// WHAT IS NOT DATA does not move it: positions, comments, spacing, and whether a table was written inline.

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Toml.h>

#include <cstddef>
#include <cstdint>
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

// The type bytes. Shared by both walks, so they cannot disagree on a spelling.
enum : std::uint8_t { kString = 0, kInteger = 1, kDecimal = 2, kBoolean = 3, kArray = 4, kTable = 5, kTables = 6 };

inline void feedDecimal (Fnv& f, const toml::Decimal& d) noexcept
{
    f.byte (kDecimal);
    f.u64 (static_cast<std::uint64_t> (d.mantissa));
    f.byte (d.scale);
    f.byte (d.negativeZero ? 1 : 0);
}

// The data compiled into the library. `depth` bounds the recursion as the parser bounds the document (kMaxDepth).
inline void feed (Fnv& f, toml::embedded::View v, std::size_t depth) noexcept
{
    using toml::embedded::Type;
    const auto type = v.type();
    if (! type || depth > toml::kMaxDepth) return;
    switch (*type)
    {
        case Type::String:  f.byte (kString); f.text (*v.string()); break;
        case Type::Integer: f.byte (kInteger); f.u64 (static_cast<std::uint64_t> (*v.integer())); break;
        case Type::Decimal: feedDecimal (f, *v.decimal()); break;
        case Type::Boolean: f.byte (kBoolean); f.byte (*v.boolean() ? 1 : 0); break;
        case Type::Array:
            f.byte (kArray);
            f.u32 (static_cast<std::uint32_t> (v.size()));
            for (const auto item : v) feed (f, item, depth + 1);
            break;
        case Type::Table:
            f.byte (kTable);
            f.u32 (static_cast<std::uint32_t> (v.size()));
            for (const auto entry : v)
            {
                f.text (entry.key());
                feed (f, entry, depth + 1);
            }
            break;
        case Type::Tables:
            f.byte (kTables);
            f.u32 (static_cast<std::uint32_t> (v.size()));
            for (const auto table : v) feed (f, table, depth + 1);
            break;
    }
}

// A document parsed from text: the same bytes for the same data.
inline void feed (Fnv& f, const toml::Value& v, std::size_t depth) noexcept;
inline void feed (Fnv& f, const toml::Table& t, std::size_t depth) noexcept
{
    if (depth > toml::kMaxDepth) return;
    f.byte (kTable);
    f.u32 (static_cast<std::uint32_t> (t.entries().size()));
    for (const auto& e : t.entries())
    {
        f.text (e.key);
        feed (f, e.value, depth + 1);
    }
}
inline void feed (Fnv& f, const toml::Value& v, std::size_t depth) noexcept
{
    if (depth > toml::kMaxDepth) return;
    if (const auto* s = std::get_if<std::string> (&v.data)) { f.byte (kString); f.text (*s); }
    else if (const auto* n = std::get_if<std::int64_t> (&v.data)) { f.byte (kInteger); f.u64 (static_cast<std::uint64_t> (*n)); }
    else if (const auto* d = std::get_if<toml::Decimal> (&v.data)) feedDecimal (f, *d);
    else if (const auto* b = std::get_if<bool> (&v.data)) { f.byte (kBoolean); f.byte (*b ? 1 : 0); }
    else if (const auto* a = std::get_if<toml::Array> (&v.data))
    {
        f.byte (kArray);
        f.u32 (static_cast<std::uint32_t> (a->size()));
        for (const auto& item : *a) feed (f, item, depth + 1);
    }
    else if (const auto* t = std::get_if<toml::Table> (&v.data)) feed (f, *t, depth);
    else if (const auto* ts = std::get_if<toml::Tables> (&v.data))
    {
        f.byte (kTables);
        f.u32 (static_cast<std::uint32_t> (ts->size()));
        for (const auto& table : *ts) feed (f, table, depth + 1);
    }
}

// Both documents, targets first. A document tag before each, so the pair is read as a pair.
template <class Tree> std::uint64_t versionOf (const Tree& targets, const Tree& engine) noexcept
{
    Fnv f;
    f.byte ('T');
    feed (f, targets, 0);
    f.byte ('E');
    feed (f, engine, 0);
    return f.value();
}

} // namespace felitronics::session::config::detail
