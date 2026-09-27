// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE NUMBERS OF THE TEXT (internal to modules/session; <felitronics/session/Text.h> states the rules): a double or an
// integer turned into its decimal digits — a double through its shortest round-trip decimal (std::to_chars), rounded on
// that decimal's digits; no libm, no printf, no locale, no floating-point arithmetic, so every row prints the same
// digits; the CLDR plural category of those digits; and what a person typed read back as a number.
// Where the separators, signs and units go is the formatting table's (src/Text.cpp reads it); this file only counts.

#include <felitronics/session/Text.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace felitronics::session::text::detail
{

// A number's digits as it is printed, before any separator, sign or unit: the integer digits (at least one — "0" below
// one — and no leading zero) followed by exactly `precision` fraction digits.
struct Digits
{
    // The largest double is 309 integer digits; nine fraction digits, a carry and room to spare.
    static constexpr std::size_t kCapacity = 336;
    std::array<char, kCapacity> buffer {};
    std::size_t integerDigits = 0;
    std::size_t fractionDigits = 0;
    bool negative = false;             // below zero AS PRINTED: a value whose digits are all 0 (−0.04 at one digit) is not

    [[nodiscard]] std::string_view integer() const noexcept;
    [[nodiscard]] std::string_view fraction() const noexcept;
    [[nodiscard]] bool zero() const noexcept;   // every digit is 0: the number prints as zero, and takes no sign
};

// The largest precision a Value takes.
inline constexpr unsigned kMaxPrecision = 9;

// |value| on the decimal grid of `precision` fraction digits: the double's shortest round-trip decimal (the fewest
// significant digits that read back as the same double) rounded to the nearest multiple of 10^−precision, a decimal
// exactly halfway rounded away from zero — so 1.005 is 1.01. False, and nothing written, for a value that is not finite
// or a precision above kMaxPrecision.
[[nodiscard]] bool gridDigits (double value, unsigned precision, Digits& out) noexcept;

// The digits of an integer (no fraction digits).
void integerDigits (std::int64_t n, Digits& out) noexcept;

// The sign of a finite double read from its bits, not by a comparison: −1, 0 or +1, either zero 0. A thread that reads
// subnormal inputs as zero (DAZ) would compare −5e−324 as equal to zero; its bits still say negative.
[[nodiscard]] int signOf (double value) noexcept;

// CLDR's cardinal plural category of a number printed with these digits in `lang` (CLDR 48, as ICU 78 selects): the
// operands i (the integer digits), v (how many fraction digits are shown) and t (whether any of them is not zero) read
// off the digits, so "1" and "1.0" are different numbers here, as they are to a reader.
[[nodiscard]] Plural pluralOf (Lang lang, std::string_view integer, std::string_view fraction) noexcept;

// What a person typed, read as <felitronics/session/Text.h> states (Text::parse): `decimalSign` and `groupSeparator` are
// the language's own — "." is a decimal sign too, unless it is the language's grouping separator.
[[nodiscard]] std::optional<double> parseTyped (std::string_view typed, std::string_view decimalSign,
                                                std::string_view groupSeparator) noexcept;

} // namespace felitronics::session::text::detail
