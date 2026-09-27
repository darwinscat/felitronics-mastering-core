// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE NUMBERS OF THE TEXT (internal to modules/session; <felitronics/session/Text.h> states the rules): a double or an
// integer turned into its decimal digits, exactly, by integer arithmetic alone — no libm, no printf, no locale, so every
// row prints the same digits; the CLDR plural category of those digits; and what a person typed read back as a number.
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
    // The largest double is 309 integer digits; nine fraction digits and room to spare.
    static constexpr std::size_t kCapacity = 336;
    std::array<char, kCapacity> buffer {};
    std::size_t integerDigits = 0;
    std::size_t fractionDigits = 0;
    bool negative = false;             // the VALUE is below zero — its digits may all be 0 (−0.04 at one digit)

    [[nodiscard]] std::string_view integer() const noexcept;
    [[nodiscard]] std::string_view fraction() const noexcept;
};

// The largest precision a Value takes.
inline constexpr unsigned kMaxPrecision = 9;

// |value| on the decimal grid of `precision` fraction digits: the exact value of the double rounded to the nearest
// multiple of 10^−precision, a value exactly halfway rounded away from zero. False, and nothing written, for a value that
// is not finite or a precision above kMaxPrecision.
[[nodiscard]] bool gridDigits (double value, unsigned precision, Digits& out) noexcept;

// The digits of an integer (no fraction digits).
void integerDigits (std::int64_t n, Digits& out) noexcept;

// CLDR's cardinal plural category of a number printed with these digits in `lang` (CLDR 48, as ICU 78 selects): the
// operands i (the integer digits), v (how many fraction digits are shown) and t (whether any of them is not zero) read
// off the digits, so "1" and "1.0" are different numbers here, as they are to a reader.
[[nodiscard]] Plural pluralOf (Lang lang, std::string_view integer, std::string_view fraction) noexcept;

// What a person typed, read as <felitronics/session/Text.h> states (Text::parse); `decimalSign` is the language's own.
[[nodiscard]] std::optional<double> parseTyped (std::string_view typed, std::string_view decimalSign) noexcept;

} // namespace felitronics::session::text::detail
