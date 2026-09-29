// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/session/Wav.h>
#if ! defined(_MSC_VER)
#include <felitronics/io/Wav.h>
#endif
#include <felitronics/dither/Dither.h>
#include <felitronics_test.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using felitronics::session::WavWriter;
using felitronics::test::ok;

namespace
{
std::uint32_t le (const std::vector<std::uint8_t>& b, std::size_t at, unsigned width)
{
    std::uint32_t v = 0;
    for (unsigned i = 0; i < width; ++i) v |= std::uint32_t (b[at + i]) << (8u * i);
    return v;
}
std::vector<std::uint8_t> image (const std::vector<float>& pcm, std::uint32_t channels,
                                 std::uint32_t rate, std::uint32_t bits, std::size_t cut)
{
    const auto plan = WavWriter::plan (pcm.size() / channels, channels, rate, bits);
    std::vector<std::uint8_t> out (std::size_t (plan.bytes));
    for (std::size_t at = 0; at < out.size(); at += cut)
    {
        const auto n = std::min (cut, out.size() - at);
        ok (WavWriter::copy (plan, pcm, at, { out.data() + at, n }), "bounded WAV slice writes");
    }
    return out;
}
std::vector<float> readIndependent (const std::vector<std::uint8_t>& b, unsigned bits,
                                    unsigned rate, unsigned channels)
{
    ok (b.size() >= 44 && std::memcmp (b.data(), "RIFF", 4) == 0
        && std::memcmp (b.data() + 8, "WAVEfmt ", 8) == 0
        && std::memcmp (b.data() + 36, "data", 4) == 0, "independent reader sees RIFF/WAVE chunks");
    ok (le (b, 4, 4) + 8u == b.size() && le (b, 16, 4) == 16u
        && le (b, 20, 2) == 1u
        && le (b, 22, 2) == channels && le (b, 24, 4) == rate
        && le (b, 28, 4) == rate * channels * (bits / 8u)
        && le (b, 32, 2) == channels * (bits / 8u) && le (b, 34, 2) == bits,
        "independent reader validates format, rate and block alignment");
    const auto data = le (b, 40, 4);
    ok (b.size() == 44u + data + (data & 1u) && data % (channels * (bits / 8u)) == 0u
        && (!(data & 1u) || b.back() == 0), "reader validates data length and RIFF pad");
    const auto frames = data / (channels * (bits / 8u));
    std::vector<float> pcm (frames * channels);
    for (std::size_t f = 0; f < frames; ++f)
        for (std::size_t c = 0; c < channels; ++c)
        {
            const auto at = 44u + (f * channels + c) * (bits / 8u);
            const auto raw = le (b, at, bits / 8u);
            const auto full = std::int32_t (1u << (bits - 1u));
            const auto signedCode = std::int32_t (raw) - ((raw & std::uint32_t (full)) ? 2 * full : 0);
            pcm[c * frames + f] = float (double (signedCode) / full);
        }
    return pcm;
}
}

int main()
{
    ok (! WavWriter::plan (1, 1, 0, 24) && ! WavWriter::plan (0, 1, 48000, 24)
        && ! WavWriter::plan (1, 1, 48000, 20) && ! WavWriter::plan (1, 1, 48000, 32)
        && ! WavWriter::plan (1, 2, 48000, 8)
        && ! WavWriter::plan (std::uint64_t (UINT32_MAX), 2, 48000, 24)
        && ! WavWriter::plan (1, 65535, 48000, 24), "invalid shapes, depths other than PCM16/PCM24 (float32 included) and RIFF overflow refuse before allocation");
    std::uint8_t untouched = 0x5au;
    ok (! WavWriter::copy ({ 44, 1, 48000, 0, 24, 0 }, {}, 0, { &untouched, 1 })
        && untouched == 0x5au, "invalid plan refuses before writing a byte");
    for (const unsigned bits : { 16u, 24u })
    {
        const float lsb = 1.0f / float (1u << (bits - 1u));
        const std::vector<float> source { -2.0f, -1.0f, -0.5f * lsb, 0.5f * lsb,
            1.5f * lsb, 20000.0f * lsb, 1.0f, 2.0f };
        const auto a = image (source, 1, 48000, bits, 1);
        const auto b = image (source, 1, 48000, bits, 17);
        ok (a == b, "irregular slices reproduce identical WAV bytes");
        const auto decoded = readIndependent (a, bits, 48000, 1);
        const std::vector<float> want { -1.0f, -1.0f, 0.0f, lsb, 2.0f * lsb,
            20000.0f * lsb, 1.0f - lsb, 1.0f - lsb };
        ok (decoded == want, "signed clamp, every half-LSB tie and the Dither grid agree");
#if ! defined(_MSC_VER)
        std::vector<double> coreInput (source.begin(), source.end());
        ok (felitronics::io::writeWavMemory ({ coreInput }, 48000.0, int (bits), false) == a,
            "installed core WAV writer agrees byte for byte on PCM grid and header");
#endif
    }
    const std::vector<float> odd { -1.0f, 0.25f, 1.0f };
    const auto mono24 = image (odd, 1, 44100, 24, 5);
    ok (mono24.size() == 54u && le (mono24, 40, 4) == 9u && mono24.back() == 0
        && readIndependent (mono24, 24, 44100, 1).size() == odd.size(),
        "odd mono24 data has one uncounted zero pad and a counted RIFF pad");
#if ! defined(_MSC_VER)
    ok (felitronics::io::writeWavMemory ({ std::vector<double> (odd.begin(), odd.end()) },
        44100.0, 24, false) == mono24, "installed core emits the same odd RIFF pad");
#endif
    const std::vector<float> stereo { -1.0f, 0.25f, 0.5f, 1.0f };
    const auto s16 = image (stereo, 2, 48000, 16, 3);
    ok (readIndependent (s16, 16, 48000, 2) == std::vector<float> { -1.0f, 0.25f, 0.5f, 1.0f - 1.0f / 32768.0f },
        "stereo PCM16 interleaves the planar channels frame by frame");
    std::vector<float> dithered (1024);
    for (std::size_t i = 0; i < dithered.size(); ++i) dithered[i] = float (int (i % 31u) - 15) / 32768.0f;
    felitronics::dither::Dither dither;
    felitronics::dither::DitherParams params; params.bits = 16;
    dither.setParams (params);
    float* ditherPlane[] { dithered.data() };
    ok (dither.prepare (48000.0, 1024, 1) && dither.process (ditherPlane, 1, int (dithered.size())),
        "fixed-seed 16-bit dither prepares and processes");
    const auto ditherImage = image (dithered, 1, 48000, 16, 31);
    ok (readIndependent (ditherImage, 16, 48000, 1) == dithered,
        "WAV preserves every dithered code without adding noise or another quantization");
    dither.reset();
    for (std::size_t i = 0; i < dithered.size(); ++i) dithered[i] = float (int (i % 31u) - 15) / 32768.0f;
    ok (dither.process (ditherPlane, 1, int (dithered.size()))
        && image (dithered, 1, 48000, 16, 47) == ditherImage,
        "fixed dither reset and repeated export reproduce the same WAV bytes");
    return felitronics::test::report();
}
