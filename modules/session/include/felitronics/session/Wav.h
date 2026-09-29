// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <cstdint>
#include <span>

namespace felitronics::session
{
// A WAV image is copied in bounded slices from one planar f32 allocation. The
// writer owns no samples, file handles, or output allocation. Integer samples
// use the same 2^(bits-1) grid and floor(x + 0.5) rule as core Dither/Wav.
struct WavPlan
{
    std::uint64_t bytes = 0, frames = 0;
    std::uint32_t rate = 0, channels = 0, bits = 0, dataBytes = 0;
    [[nodiscard]] explicit operator bool() const noexcept;
};

class WavWriter
{
public:
    [[nodiscard]] static WavPlan plan (std::uint64_t frames, std::uint32_t channels,
                                       std::uint32_t rate, std::uint32_t bits) noexcept;
    // A nonempty output spans at most 65536 bytes. `plan` and the PCM must
    // describe the same programme. The exact slice is repeatable at any offset.
    [[nodiscard]] static bool copy (WavPlan plan, std::span<const float> planar,
                                    std::uint64_t offset, std::span<std::uint8_t> output) noexcept;
};
}
