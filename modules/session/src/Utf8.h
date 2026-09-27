// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <string_view>
#include <cstdint>

namespace felitronics::session::detail
{
// Unicode scalar values in their shortest UTF-8 encoding; embedded NUL is a valid scalar too.
[[nodiscard]] inline bool validUtf8 (std::string_view text) noexcept
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto first = static_cast<unsigned char> (text[i++]);
        if (first < 0x80) continue;
        unsigned count = 0;
        std::uint32_t cp = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 1; cp = first & 31u; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 2; cp = first & 15u; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 3; cp = first & 7u; minimum = 0x10000; }
        else return false;
        if (count > text.size() - i) return false;
        while (count--)
        {
            const auto next = static_cast<unsigned char> (text[i++]);
            if ((next & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (next & 63u);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}
} // namespace felitronics::session::detail
