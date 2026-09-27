// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE NUMBERS OF THE TEXT (src/TextNumber.h). The digits are EXACT: a finite double is m · 2^e with m an integer of at
// most 53 bits, so |value| · 10^p = m · 10^p · 2^e is an integer times a power of two, and a fixed-size big integer holds
// every one of them — the largest double at nine fraction digits is below 2^1054. Rounding to the grid is then a shift
// right by −e bits, and "exactly halfway or more" is the one bit below the cut: set means the remainder is at least half,
// and ties go up, away from zero. Integer arithmetic only, the same on every row; nothing here allocates.

#include "BuildGuards.h"

#include "TextNumber.h"

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace felitronics::session::text::detail
{
namespace
{
// A non-negative integer of up to 36 × 32 bits, least significant limb first; `size` limbs are in use, 0 is zero.
struct Big
{
    static constexpr std::size_t kLimbs = 36;
    std::array<std::uint32_t, kLimbs> limb {};
    std::size_t size = 0;
};

void trim (Big& b) noexcept
{
    while (b.size > 0 && b.limb[b.size - 1] == 0) --b.size;
}

void set (Big& b, std::uint64_t v) noexcept
{
    b.limb = {};
    b.limb[0] = (std::uint32_t) v;
    b.limb[1] = (std::uint32_t) (v >> 32);
    b.size = 2;
    trim (b);
}

void multiply (Big& b, std::uint32_t factor) noexcept
{
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < b.size; ++i)
    {
        const std::uint64_t t = (std::uint64_t) b.limb[i] * factor + carry;
        b.limb[i] = (std::uint32_t) t;
        carry = t >> 32;
    }
    if (carry != 0 && b.size < Big::kLimbs) b.limb[b.size++] = (std::uint32_t) carry;
}

void shiftLeft (Big& b, unsigned bits) noexcept
{
    if (b.size == 0 || bits == 0) return;
    const std::size_t whole = bits / 32;
    const unsigned part = bits % 32;
    std::size_t size = b.size + whole + 1;
    if (size > Big::kLimbs) size = Big::kLimbs;   // never reached: the callers stay below 2^1054
    for (std::size_t i = size; i-- > 0;)
    {
        const std::uint64_t hi = i >= whole && i - whole < b.size ? b.limb[i - whole] : 0u;
        const std::uint64_t lo = part != 0 && i >= whole + 1 && i - whole - 1 < b.size ? b.limb[i - whole - 1] : 0u;
        b.limb[i] = (std::uint32_t) ((hi << part) | (part != 0 ? lo >> (32 - part) : 0u));
    }
    b.size = size;
    trim (b);
}

[[nodiscard]] bool bitAt (const Big& b, std::size_t bit) noexcept
{
    const std::size_t whole = bit / 32;
    return whole < b.size && ((b.limb[whole] >> (bit % 32)) & 1u) != 0;
}

void shiftRight (Big& b, std::size_t bits) noexcept
{
    const std::size_t whole = bits / 32;
    const unsigned part = (unsigned) (bits % 32);
    if (whole >= b.size) { b.limb = {}; b.size = 0; return; }
    const std::size_t size = b.size - whole;
    for (std::size_t i = 0; i < size; ++i)
    {
        const std::uint64_t lo = b.limb[i + whole];
        const std::uint64_t hi = i + whole + 1 < b.size ? b.limb[i + whole + 1] : 0u;
        b.limb[i] = (std::uint32_t) (part != 0 ? (lo >> part) | (hi << (32 - part)) : lo);
    }
    for (std::size_t i = size; i < b.size; ++i) b.limb[i] = 0;
    b.size = size;
    trim (b);
}

void addOne (Big& b) noexcept
{
    for (std::size_t i = 0; i < b.size; ++i)
        if (++b.limb[i] != 0) return;
    if (b.size < Big::kLimbs) b.limb[b.size++] = 1;
}

// b ← b / divisor; the remainder.
[[nodiscard]] std::uint32_t divide (Big& b, std::uint32_t divisor) noexcept
{
    std::uint64_t rest = 0;
    for (std::size_t i = b.size; i-- > 0;)
    {
        const std::uint64_t current = (rest << 32) | b.limb[i];
        b.limb[i] = (std::uint32_t) (current / divisor);
        rest = current % divisor;
    }
    trim (b);
    return (std::uint32_t) rest;
}

constexpr std::array<std::uint32_t, kMaxPrecision + 1> kPow10 = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u };

// The decimal digits of b (b is consumed) into out, most significant first, with at least `minimum` digits (zeros in
// front); the count. The digits come out in groups of nine, least significant group first.
std::size_t decimal (Big& b, std::size_t minimum, std::array<char, Digits::kCapacity>& out) noexcept
{
    std::array<std::uint32_t, 40> groups {};
    std::size_t count = 0;
    while (b.size != 0 && count < groups.size()) groups[count++] = divide (b, 1000000000u);
    std::array<char, Digits::kCapacity> reversed {};
    std::size_t n = 0;
    for (std::size_t g = 0; g < count; ++g)
    {
        std::uint32_t v = groups[g];
        const bool last = g + 1 == count;
        for (int d = 0; d < 9 && (! last || v != 0) && n < reversed.size(); ++d)
        {
            reversed[n++] = (char) ('0' + v % 10);
            v /= 10;
        }
    }
    while (n < minimum && n < reversed.size()) reversed[n++] = '0';
    for (std::size_t i = 0; i < n; ++i) out[i] = reversed[n - 1 - i];
    return n;
}

// The bits of a double, through a volatile. Every test on them must stay an integer test: clang folded `(bits << 1) == 0`
// back into `fcmp d0, #0.0` (measured, Apple clang 21, arm64), a floating-point comparison that a thread reading
// subnormals as zero answers "zero" for −5e−324. A volatile read is opaque to the optimiser, as core's pinned roundings
// are.
[[nodiscard]] std::uint64_t bitsOf (double value) noexcept
{
    volatile std::uint64_t bits = std::bit_cast<std::uint64_t> (value);
    return bits;
}

// The last `k` (≤ 6) integer digits as a number: i % 10^k.
[[nodiscard]] std::uint32_t lastDigits (std::string_view integer, std::size_t k) noexcept
{
    std::uint32_t v = 0;
    const std::size_t from = integer.size() > k ? integer.size() - k : 0;
    for (std::size_t i = from; i < integer.size(); ++i) v = v * 10 + (std::uint32_t) (integer[i] - '0');
    return v;
}
} // namespace

std::string_view Digits::integer() const noexcept
{
    return { buffer.data(), integerDigits };
}

std::string_view Digits::fraction() const noexcept
{
    return { buffer.data() + integerDigits, fractionDigits };
}

bool gridDigits (double value, unsigned precision, Digits& out) noexcept
{
    const std::uint64_t bits = bitsOf (value);
    const auto exponent = (unsigned) ((bits >> 52) & 0x7FFu);
    if (exponent == 0x7FFu || precision > kMaxPrecision) return false;
    const std::uint64_t fraction = bits & ((std::uint64_t (1) << 52) - 1);
    const std::uint64_t m = exponent == 0 ? fraction : fraction | (std::uint64_t (1) << 52);
    const int e = exponent == 0 ? -1074 : (int) exponent - 1075;

    Big n;
    set (n, m);
    multiply (n, kPow10[precision]);
    if (e >= 0) shiftLeft (n, (unsigned) e);
    else
    {
        const auto cut = (std::size_t) -e;
        const bool halfOrMore = bitAt (n, cut - 1);   // the remainder below the cut is at least 2^(cut−1)
        shiftRight (n, cut);
        if (halfOrMore) addOne (n);
    }
    const std::size_t count = decimal (n, precision + 1, out.buffer);
    out.integerDigits = count - precision;
    out.fractionDigits = precision;
    out.negative = (bits >> 63) != 0 && (bits & ~(std::uint64_t (1) << 63)) != 0;
    return true;
}

int signOf (double value) noexcept
{
    const std::uint64_t bits = bitsOf (value);
    if ((bits & ~(std::uint64_t (1) << 63)) == 0) return 0;
    return (bits >> 63) != 0 ? -1 : 1;
}

void integerDigits (std::int64_t n, Digits& out) noexcept
{
    const std::uint64_t magnitude = n < 0 ? 0u - (std::uint64_t) n : (std::uint64_t) n;
    Big b;
    set (b, magnitude);
    out.integerDigits = decimal (b, 1, out.buffer);
    out.fractionDigits = 0;
    out.negative = n < 0;
}

Plural pluralOf (Lang lang, std::string_view i, std::string_view f) noexcept
{
    const std::size_t v = f.size();
    bool t = false;                                     // a fraction digit that is not zero
    for (const char c : f) t = t || c != '0';
    const bool iZero = i == "0", iOne = i == "1";
    const bool nOne = iOne && ! t, nZero = iZero && ! t;
    const std::uint32_t i10 = lastDigits (i, 1), i100 = lastDigits (i, 2);
    const bool million = v == 0 && ! iZero && lastDigits (i, 6) == 0;   // e = 0: no compact exponent is printed
    switch (lang)
    {
        case Lang::En: case Lang::De:
            return iOne && v == 0 ? Plural::One : Plural::Other;
        case Lang::Tr:
            return nOne ? Plural::One : Plural::Other;
        case Lang::Es:
            return nOne ? Plural::One : million ? Plural::Many : Plural::Other;
        case Lang::Fr: case Lang::Pt:
            return iZero || iOne ? Plural::One : million ? Plural::Many : Plural::Other;
        case Lang::It:
            return iOne && v == 0 ? Plural::One : million ? Plural::Many : Plural::Other;
        case Lang::Ru: case Lang::Uk:
            if (v != 0) return Plural::Other;
            if (i10 == 1 && i100 != 11) return Plural::One;
            if (i10 >= 2 && i10 <= 4 && (i100 < 12 || i100 > 14)) return Plural::Few;
            return Plural::Many;
        case Lang::Pl:
            if (v != 0) return Plural::Other;
            if (iOne) return Plural::One;
            if (i10 >= 2 && i10 <= 4 && (i100 < 12 || i100 > 14)) return Plural::Few;
            return Plural::Many;
        case Lang::Cs:
            if (v != 0) return Plural::Many;
            if (iOne) return Plural::One;
            if (i.size() == 1 && i10 >= 2 && i10 <= 4) return Plural::Few;
            return Plural::Other;
        case Lang::Ro:
            if (iOne && v == 0) return Plural::One;
            if (v != 0 || nZero || (! nOne && i100 >= 1 && i100 <= 19)) return Plural::Few;
            return Plural::Other;
    }
    return Plural::Other;
}

std::optional<double> parseTyped (std::string_view typed, std::string_view decimalSign, std::string_view groupSeparator) noexcept
{
    const bool pointIsDecimal = groupSeparator != ".";   // "1.234" is a thousand where "." groups: refused, not guessed
    constexpr std::string_view kMinus = "\xE2\x88\x92";   // U+2212
    while (! typed.empty() && typed.front() == ' ') typed.remove_prefix (1);
    while (! typed.empty() && typed.back() == ' ') typed.remove_suffix (1);
    bool negative = false;
    if (typed.starts_with (kMinus)) { negative = true; typed.remove_prefix (kMinus.size()); }
    else if (typed.starts_with ('-')) { negative = true; typed.remove_prefix (1); }
    else if (typed.starts_with ('+')) typed.remove_prefix (1);

    // The digits without the integer part's leading zeros, which change nothing; the fraction's all count.
    std::array<char, 32> digits {};
    std::size_t count = 0, scale = 0;
    bool separated = false, anyDigit = false;
    while (! typed.empty())
    {
        const char c = typed.front();
        if (c >= '0' && c <= '9')
        {
            anyDigit = true;
            typed.remove_prefix (1);
            if (separated) ++scale;
            else if (c == '0' && count == 0) continue;
            if (count == digits.size()) return std::nullopt;
            digits[count++] = c;
            continue;
        }
        const std::size_t sign = ! decimalSign.empty() && typed.starts_with (decimalSign) ? decimalSign.size()
                               : c == '.' && pointIsDecimal ? 1 : 0;
        if (sign == 0 || separated) return std::nullopt;
        separated = true;
        typed.remove_prefix (sign);
    }
    if (! anyDigit || scale > kMaxPrecision) return std::nullopt;

    std::uint64_t mantissa = 0;
    if (count != 0)
    {
        const std::from_chars_result read = std::from_chars (digits.data(), digits.data() + count, mantissa);
        if (read.ec != decltype (read.ec) {} || read.ptr != digits.data() + count) return std::nullopt;
    }
    if (mantissa > (std::uint64_t (1) << 53)) return std::nullopt;
    // Both operands are exact binary64 values, so the one IEEE division rounds the exact quotient correctly — under the
    // default floating-point environment, which Text::parse asks for before it calls this.
    const double magnitude = (double) mantissa / (double) kPow10[scale];
    return negative ? -magnitude : magnitude;
}

} // namespace felitronics::session::text::detail
