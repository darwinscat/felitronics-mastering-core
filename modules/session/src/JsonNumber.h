// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace felitronics::session::detail
{
// Exact decimal JSON numbers without libc, locale, or a library's floating charconv.
// 8192 bits cover the bounded decimal input and the shifts for binary64 rounding.
struct JsonInteger
{
    std::uint32_t words[256] {};
    unsigned used = 0;
    explicit JsonInteger (std::uint64_t n = 0) noexcept
    {
        words[0] = std::uint32_t (n); words[1] = std::uint32_t (n >> 32);
        used = words[1] ? 2u : words[0] ? 1u : 0u;
    }
    void trim() noexcept { while (used && words[used - 1] == 0) --used; }
    bool multiply (std::uint32_t m, std::uint32_t add = 0) noexcept
    {
        std::uint64_t carry = add;
        for (unsigned i = 0; i < used; ++i)
        {
            const auto n = std::uint64_t (words[i]) * m + carry;
            words[i] = std::uint32_t (n); carry = n >> 32;
        }
        if (carry) { if (used == 256) return false; words[used++] = std::uint32_t (carry); }
        trim(); return true;
    }
    bool shift (unsigned bits) noexcept
    {
        while (bits >= 31) { if (! multiply (std::uint32_t (1) << 31)) return false; bits -= 31; }
        return multiply (std::uint32_t (1) << bits);
    }
    int compare (const JsonInteger& b) const noexcept
    {
        if (used != b.used) return used < b.used ? -1 : 1;
        for (unsigned i = used; i > 0; --i)
            if (words[i - 1] != b.words[i - 1]) return words[i - 1] < b.words[i - 1] ? -1 : 1;
        return 0;
    }
    void subtract (const JsonInteger& b) noexcept
    {
        std::uint64_t borrow = 0;
        for (unsigned i = 0; i < used; ++i)
        {
            const auto rhs = std::uint64_t (i < b.used ? b.words[i] : 0) + borrow;
            borrow = std::uint64_t (words[i]) < rhs ? 1 : 0;
            words[i] = std::uint32_t (std::uint64_t (words[i]) - rhs);
        }
        trim();
    }
    unsigned bits() const noexcept
    {
        if (! used) return 0;
        unsigned topBits = 0;
        auto top = words[used - 1];
        do { ++topBits; top >>= 1; } while (top);
        return (used - 1) * 32 + topBits;
    }
    char digit() noexcept
    {
        std::uint64_t rem = 0;
        for (unsigned i = used; i > 0; --i)
        {
            const auto n = (rem << 32) | words[i - 1];
            words[i - 1] = std::uint32_t (n / 10); rem = n % 10;
        }
        trim(); return char ('0' + rem);
    }
};
struct JsonNumber
{
    static constexpr std::size_t capacity = 1100;
    // Exact finite binary64 as a decimal, retaining signed zero. The caller supplies capacity bytes.
    static std::size_t write (double value, char* out) noexcept
    {
        const auto bits = std::bit_cast<std::uint64_t> (value);
        const unsigned exponent = unsigned ((bits >> 52) & 2047);
        const auto fraction = bits & 0xFFFFFFFFFFFFFull;
        JsonInteger n (fraction | (exponent ? std::uint64_t (1) << 52 : 0));
        const int power = exponent ? int (exponent) - 1023 - 52 : -1074;
        unsigned scale = 0;
        if (power >= 0) (void) n.shift (unsigned (power));
        else if (n.used)
        {
            scale = unsigned (-power);
            for (unsigned i = 0; i < scale; ++i) (void) n.multiply (5);
        }
        char reverse[capacity]; std::size_t digits = 0;
        do { reverse[digits++] = n.digit(); } while (n.used);
        std::size_t size = 0;
        if ((bits >> 63) != 0) out[size++] = '-';
        // Trailing decimal zeros do not carry information.
        std::size_t skip = 0;
        while (skip < scale && skip + 1 < digits && reverse[skip] == '0') { ++skip; }
        scale -= unsigned (skip);
        digits -= skip;
        if (digits <= scale)
        {
            out[size++] = '0'; out[size++] = '.';
            for (std::size_t i = digits; i < scale; ++i) out[size++] = '0';
        }
        for (std::size_t i = digits; i > 0; --i)
        {
            out[size++] = reverse[skip + i - 1];
            if (i > 1 && i - 1 == scale) out[size++] = '.';
        }
        return size;
    }
    // The caller has checked JSON number grammar. Round the exact decimal ratio once,
    // to nearest, ties to even, by integer division; no host floating parser participates.
    static bool read (std::string_view text, double& value) noexcept
    {
        JsonInteger num;
        std::size_t pos = 0; bool negative = false, dot = false;
        if (text[pos] == '-') { negative = true; ++pos; }
        int power = 0;
        for (; pos < text.size() && text[pos] != 'e' && text[pos] != 'E'; ++pos)
        {
            if (text[pos] == '.') { dot = true; continue; }
            if (! num.multiply (10, std::uint32_t (text[pos] - '0'))) return false;
            if (dot) --power;
        }
        if (pos < text.size())
        {
            ++pos; bool minus = false;
            if (text[pos] == '+' || text[pos] == '-') { minus = text[pos] == '-'; ++pos; }
            int exponent = 0;
            for (; pos < text.size(); ++pos)
            {
                if (exponent > 2000) return false;
                exponent = exponent * 10 + int (text[pos] - '0');
            }
            power += minus ? -exponent : exponent;
        }
        const auto sign = negative ? std::uint64_t (1) << 63 : 0;
        if (! num.used) { value = std::bit_cast<double> (sign); return true; }
        if (power < -1200 || power > 400) return false;
        JsonInteger den (1);
        if (power >= 0) { for (int i = 0; i < power; ++i) if (! num.multiply (10)) return false; }
        else { for (int i = 0; i < -power; ++i) if (! den.multiply (10)) return false; }
        int exponent = int (num.bits()) - int (den.bits());
        JsonInteger a = num, b = den;
        if (exponent >= 0) { if (! b.shift (unsigned (exponent))) return false; }
        else if (! a.shift (unsigned (-exponent))) return false;
        if (a.compare (b) < 0) --exponent;
        if (exponent > 1023 || exponent < -1075) return false;
        const int shift = exponent < -1022 ? 1074 : 52 - exponent;
        if (shift >= 0) { if (! num.shift (unsigned (shift))) return false; }
        else if (! den.shift (unsigned (-shift))) return false;
        std::uint64_t mantissa = 0;
        for (int i = 53; i >= 0; --i)
        {
            JsonInteger divisor = den;
            if (! divisor.shift (unsigned (i))) return false;
            if (num.compare (divisor) >= 0) { num.subtract (divisor); mantissa |= std::uint64_t (1) << i; }
        }
        if (! num.shift (1)) return false;
        const int rounding = num.compare (den);
        if (rounding > 0 || (rounding == 0 && (mantissa & 1))) ++mantissa;
        std::uint64_t result = mantissa;
        if (exponent >= -1022)
        {
            if (mantissa == (std::uint64_t (1) << 53)) { mantissa >>= 1; ++exponent; }
            if (exponent > 1023) return false;
            result = (std::uint64_t (exponent + 1023) << 52) | (mantissa & 0xFFFFFFFFFFFFFull);
        }
        value = std::bit_cast<double> (sign | result);
        return true;
    }
};
} // namespace felitronics::session::detail
