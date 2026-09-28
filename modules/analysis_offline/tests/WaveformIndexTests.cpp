// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include <alloc_counter.h>
#include <felitronics/analysis/WaveformIndex.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <vector>

using namespace felitronics::analysis;
using felitronics::test::ok;
namespace alloc = felitronics::test::alloc;
std::uint64_t fingerprint = 0xCBF29CE484222325ull;
void hash (double value)
{
    const auto bits = std::bit_cast<std::uint64_t> (value);
    for (unsigned i = 0; i < 8; ++i) fingerprint = (fingerprint ^ std::uint8_t (bits >> (i * 8u))) * 0x100000001B3ull;
}
bool close (double a, double b) { return std::fabs (a - b) <= 1e-11 * std::max (1.0, std::fabs (b)); }
// Independent full-history reference: retain each filtered sample, then reduce the chosen range.
// The implementation stores no sample trace and combines interior tree nodes instead.
WaveformColumn reference (const std::vector<float>& left, const std::vector<float>& right, unsigned channels,
                          unsigned rate, std::size_t first, std::size_t last)
{
    WaveformColumn out {};
    constexpr double twoPi = 6.283185307179586476925286766559;
    const auto decimation = std::max (1u, (rate + 4000u) / 8000u);
    for (unsigned axis = 0; axis < 4; ++axis)
    {
        if (channels == 1 && (axis == 1 || axis == 3)) continue;
        std::vector<double> samples (last), lows (last), uppers (last);
        double low = 0, upper = 0;
        for (std::size_t frame = 0; frame < last; ++frame)
        {
            const double l = left[frame], r = channels == 2 ? right[frame] : 0;
            const double sample = axis == 0 ? l : axis == 1 ? r : axis == 2 ? channels == 1 ? l : (l + r) * 0.5 : (l - r) * 0.5;
            samples[frame] = sample;
            if (std::isfinite (sample))
            {
                low = low + (twoPi * 250.0 / (double (rate) + twoPi * 250.0)) * (sample - low);
                upper = upper + (twoPi * 2500.0 / (double (rate) + twoPi * 2500.0)) * (sample - upper);
            }
            else low = upper = 0;
            lows[frame] = low; uppers[frame] = upper;
        }
        auto& a = out.axes[axis];
        for (auto frame = first; frame < last; ++frame)
        {
            const auto sample = samples[frame]; if (! std::isfinite (sample)) continue;
            if (a.finite == 0) a.minimum = a.maximum = sample;
            a.minimum = std::min (a.minimum, sample); a.maximum = std::max (a.maximum, sample);
            a.squareSum += sample * sample;
            a.lowEnergy += lows[frame] * lows[frame];
            const auto middle = uppers[frame] - lows[frame], high = sample - uppers[frame];
            a.middleEnergy += middle * middle; a.highEnergy += high * high; ++a.finite;
        }
        for (auto frame = first; frame < last;)
        {
            const auto end = std::min (last, (frame / decimation + 1u) * decimation);
            double sum = 0; unsigned count = 0;
            for (; frame < end; ++frame) if (std::isfinite (samples[frame])) { sum += samples[frame]; ++count; }
            if (count) a.envelope = std::max (a.envelope, std::fabs (sum / double (count)));
        }
    }
    return out;
}
void compare (const WaveformColumn& a, const WaveformColumn& b, bool exact)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto& x = a.axes[i]; const auto& y = b.axes[i];
        const double first[] { x.minimum, x.maximum, x.squareSum, x.envelope, x.lowEnergy, x.middleEnergy, x.highEnergy };
        const double second[] { y.minimum, y.maximum, y.squareSum, y.envelope, y.lowEnergy, y.middleEnergy, y.highEnergy };
        ok (x.finite == y.finite, "finite count and absent axes");
        for (unsigned j = 0; j < 7; ++j)
        {
            hash (first[j]);
            ok (exact ? std::bit_cast<std::uint64_t> (first[j]) == std::bit_cast<std::uint64_t> (second[j]) : close (first[j], second[j]),
                exact ? "input split preserves every bit" : "index agrees with independent full-history reduction");
        }
    }
}
void signal (unsigned mode, unsigned channels, unsigned rate)
{
    constexpr std::size_t frames = 2063;
    std::vector<float> l (frames), r (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        // A sampled sinusoid with exact input bits, portable independently of the host sin().
        constexpr float sine[] { 0, 0.5f, 0.8660253882408142f, 1, 0.8660253882408142f, 0.5f, 0, -0.5f, -0.8660253882408142f, -1, -0.8660253882408142f, -0.5f };
        l[i] = mode == 0 ? i == 17 ? 1.0f : i == frames - 1 ? -0.75f : 0.0f
            : mode == 1 ? 0.25f : sine[i % 12];
        r[i] = mode == 3 ? -l[i] : l[i] * 0.5f;
    }
    if (mode == 4) { l[100] = std::numeric_limits<float>::quiet_NaN(); r[701] = std::numeric_limits<float>::infinity(); }
    const float* planes[] { l.data(), r.data() };
    WaveformIndex whole, split;
    const auto storage = WaveformIndex::storageFor (rate, channels, frames);
    const auto before = alloc::rawBytes.load();
    const auto prepared = whole.prepare (rate, channels, frames);
    const auto preparationBytes = alloc::rawBytes.load() - before;
    ok (prepared, "prepare index");
    ok (std::uint64_t (preparationBytes) <= storage.bytes(), "index declaration covers raw allocator bytes");
    ok (split.prepare (rate, channels, frames), "prepare split index");
    const auto processing = alloc::count.load();
    bool processed = false;
    {
        WaveformBuilder builder (whole);
        processed = builder.process (planes, channels, frames);
    }
    std::size_t at = 0; unsigned rng = 71; bool chunksProcessed = true;
    while (at < frames)
    {
        rng = rng * 1664525u + 1013904223u;
        const auto count = std::min (frames - at, std::size_t (1u + rng % 97u));
        const float* block[] { l.data() + at, r.data() + at };
        chunksProcessed = split.process (block, channels, std::uint32_t (count)) && chunksProcessed; at += count;
    }
    const auto processingAllocations = alloc::count.load() - processing;
    ok (processed && chunksProcessed, "one input block and random chunks");
    ok (processingAllocations == 0, "streaming allocates nothing");
    ok (whole.complete() && split.complete(), "partial final leaf completes");
    for (const auto range : { std::pair<std::size_t, std::size_t> { 0, frames }, { 3, 2049 }, { 256, 1799 }, { 17, 18 }, { frames - 1, frames }, { 0, 0 } })
    {
        WaveformColumn a {}, b {}; std::uint64_t reads = 0, otherReads = 0;
        ok (whole.read (planes, range.first, range.second, a, reads), "owned index survives builder destruction");
        ok (split.read (planes, range.first, range.second, b, otherReads), "split range");
        ok (reads <= 2u * whole.leafFrames(), "at most two exact edge leaves are replayed");
        compare (a, b, true);
        compare (a, reference (l, r, channels, rate, range.first, range.second), false);
    }
    WaveformColumn sentinel {}; sentinel.axes[0].finite = 777;
    std::uint64_t reads = 0;
    ok (! whole.read (planes, 2, 1, sentinel, reads) && sentinel.axes[0].finite == 777 && reads == 0, "invalid range changes no output");
    ok (! whole.process (planes, channels, 1) && whole.framesSeen() == frames, "past-end input is atomic");
}
int main()
{
    for (const auto rate : { 8000u, 44100u, 48000u })
        for (unsigned mode = 0; mode < 5; ++mode) for (unsigned channels = 1; channels <= 2; ++channels) signal (mode, channels, rate);
    for (const auto rate : { 8000u, 48000u, 96000u })
        for (unsigned channels = 1; channels <= 2; ++channels)
        {
            const auto s = WaveformIndex::storageFor (rate, channels, std::uint64_t (rate) * 600);
            ok (s.ok && s.bytes() < std::uint64_t (rate) * 600u * channels * sizeof (float), "index is smaller than a PCM copy");
        }
    ok (! WaveformIndex::storageFor (48000, 2, 0).ok && ! WaveformIndex::storageFor (48000, 3, 8).ok, "invalid geometry");
    std::printf ("waveform-index-digest=%016llx\n", static_cast<unsigned long long> (fingerprint));
    return felitronics::test::report();
}
