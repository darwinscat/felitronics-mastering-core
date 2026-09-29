// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include <felitronics/session/Wav.h>
#include <felitronics/mastering/PcmQuantizer.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace felitronics::session
{
WavPlan::operator bool() const noexcept { return bytes != 0; }

WavPlan WavWriter::plan (std::uint64_t frames, std::uint32_t channels,
                         std::uint32_t rate, std::uint32_t bits) noexcept
{
    if (frames == 0 || channels == 0 || channels > 65535u || rate == 0
        || (bits != 16 && bits != 24 && bits != 32)) return {};
    const std::uint64_t block = std::uint64_t (channels) * (bits / 8u);
    if (block > 65535u || std::uint64_t (rate) * block > UINT32_MAX
        || frames > (std::uint64_t (UINT32_MAX) - 44u) / block) return {};
    const auto data = std::uint32_t (frames * block);
    return { 44u + std::uint64_t (data) + (data & 1u), frames, rate, channels, bits, data };
}

bool WavWriter::copy (WavPlan p, std::span<const float> planar, std::uint64_t offset,
                      std::span<std::uint8_t> output) noexcept
{
    const auto canonical = plan (p.frames, p.channels, p.rate, p.bits);
    if (! canonical || canonical.bytes != p.bytes || canonical.dataBytes != p.dataBytes
        || output.empty() || output.size() > 65536u || offset >= p.bytes
        || output.size() > p.bytes - offset
        || p.frames > std::uint64_t (std::numeric_limits<std::size_t>::max()) / p.channels
        || planar.size() != std::size_t (p.frames * p.channels)) return false;
    const std::uint32_t format = p.bits == 32 ? 3u : 1u;
    const std::uint32_t block = p.channels * (p.bits / 8u);
    const std::uint32_t riff = 36u + p.dataBytes + (p.dataBytes & 1u);
    const std::uint32_t byteRate = p.rate * block;
    const auto header = [&] (std::uint64_t at) noexcept -> std::uint8_t
    {
        if (at < 4) return std::uint8_t ("RIFF"[at]);
        if (at >= 8 && at < 12) return std::uint8_t ("WAVE"[at - 8]);
        if (at >= 12 && at < 16) return std::uint8_t ("fmt "[at - 12]);
        if (at >= 36 && at < 40) return std::uint8_t ("data"[at - 36]);
        const auto word = [&] (std::uint64_t base, std::uint32_t value, unsigned width) noexcept -> int
        { return at >= base && at < base + width ? int ((value >> (8u * unsigned (at - base))) & 255u) : -1; };
        for (const auto item : { word (4, riff, 4), word (16, 16, 4), word (20, format, 2),
                                 word (22, p.channels, 2), word (24, p.rate, 4), word (28, byteRate, 4),
                                 word (32, block, 2), word (34, p.bits, 2), word (40, p.dataBytes, 4) })
            if (item >= 0) return std::uint8_t (item);
        return 0;
    };
    const std::uint64_t bytesPer = p.bits / 8u;
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        const auto at = offset + i;
        if (at < 44) { output[i] = header (at); continue; }
        if (at >= 44u + p.dataBytes) { output[i] = 0; continue; }
        const auto data = at - 44u;
        const auto interleaved = data / bytesPer;
        const auto frame = interleaved / p.channels;
        const auto channel = interleaved % p.channels;
        const float sample = planar[std::size_t (channel * p.frames + frame)];
        std::uint32_t code = 0;
        if (p.bits == 32)
            code = std::isfinite (sample) ? std::bit_cast<std::uint32_t> (sample) : 0u;
        else code = std::uint32_t (mastering::pcmCode (sample, p.bits));
        output[i] = std::uint8_t ((code >> (8u * unsigned (data % bytesPer))) & 255u);
    }
    return true;
}
}
