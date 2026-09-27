// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE NUMBERS OF THE TEXT (src/TextNumber.h). A double is printed as its SHORTEST ROUND-TRIP DECIMAL — the fewest
// significant digits that read back as the same double, std::to_chars's shortest form, which the standard specifies
// exactly (the closest such decimal, ties to even), so every standard library gives the same digits, with no locale —
// and that decimal is rounded to the grid of `precision` fraction digits, a half away from zero. Rounding a finite
// decimal is exact: the first dropped digit is 5 or more exactly when the dropped part is half a unit or more. So 1.005,
// whose double is 1.00499999999999989…, prints as 1.01, the number a person wrote; and no floating-point operation
// takes part in it — the digits and the rounding are characters and integers, and nothing here allocates.

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
constexpr std::array<std::uint32_t, kMaxPrecision + 1> kPow10 = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u };

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

bool Digits::zero() const noexcept
{
    for (std::size_t i = 0; i < integerDigits + fractionDigits; ++i)
        if (buffer[i] != '0') return false;
    return true;
}

bool gridDigits (double value, unsigned precision, Digits& out) noexcept
{
    const std::uint64_t bits = bitsOf (value);
    if (((bits >> 52) & 0x7FFu) == 0x7FFu || precision > kMaxPrecision) return false;

    // The shortest round-trip decimal of |value|, as "d.ddde±x": its significant digits, and the power of ten of the first.
    const double magnitude = std::bit_cast<double> (bits & ~(std::uint64_t (1) << 63));
    std::array<char, 32> text {};
    const std::to_chars_result shortest = std::to_chars (text.data(), text.data() + text.size(), magnitude,
                                                         std::chars_format::scientific);
    if (shortest.ec != decltype (shortest.ec) {}) return false;   // never: 32 bytes hold every double's form
    std::array<char, 20> significant {};
    std::size_t count = 0;
    const char* at = text.data();
    for (; at < shortest.ptr && *at != 'e'; ++at)
        if (*at != '.' && count < significant.size()) significant[count++] = *at;
    if (at + 2 >= shortest.ptr || count == 0) return false;
    const bool negativeExponent = at[1] == '-';
    int power = 0;
    const std::from_chars_result read = std::from_chars (at + 2, shortest.ptr, power);
    if (read.ec != decltype (read.ec) {} || read.ptr != shortest.ptr) return false;
    if (negativeExponent) power = -power;

    // The digit of the decimal at the place 10^place: significant[power − place], zeros around it.
    const auto digitAt = [&] (int place) -> char
    {
        const int i = power - place;
        return i >= 0 && (std::size_t) i < count ? significant[(std::size_t) i] : '0';
    };
    // Places from the highest integer place (at least the units) down to 10^−precision; then the rounding, on the first
    // dropped digit: 5 or more is half a unit or more of a finite decimal, and a half goes up, away from zero.
    const int top = power > 0 ? power : 0;
    const int bottom = -(int) precision;
    std::size_t n = 0;
    out.buffer[n++] = '0';                                   // room for a carry out of the top
    for (int place = top; place >= bottom; --place) out.buffer[n++] = digitAt (place);
    if (digitAt (bottom - 1) >= '5')
    {
        std::size_t i = n;
        while (out.buffer[--i] == '9') out.buffer[i] = '0';
        ++out.buffer[i];
    }
    const std::size_t lead = out.buffer[0] == '0' ? 1 : 0;   // the carry's place, if nothing carried into it
    for (std::size_t i = lead; i < n; ++i) out.buffer[i - lead] = out.buffer[i];
    out.integerDigits = n - lead - precision;
    out.fractionDigits = precision;
    out.negative = (bits >> 63) != 0 && ! out.zero();   // a value that rounds to zero prints unsigned: never "−0.0"
    return true;
}

void integerDigits (std::int64_t n, Digits& out) noexcept
{
    std::uint64_t magnitude = n < 0 ? 0u - (std::uint64_t) n : (std::uint64_t) n;
    std::array<char, 20> reversed {};
    std::size_t count = 0;
    do
    {
        reversed[count++] = (char) ('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    for (std::size_t i = 0; i < count; ++i) out.buffer[i] = reversed[count - 1 - i];
    out.integerDigits = count;
    out.fractionDigits = 0;
    out.negative = n < 0;
}

int signOf (double value) noexcept
{
    const std::uint64_t bits = bitsOf (value);
    if ((bits & ~(std::uint64_t (1) << 63)) == 0) return 0;
    return (bits >> 63) != 0 ? -1 : 1;
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
