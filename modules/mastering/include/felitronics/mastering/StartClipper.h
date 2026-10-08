// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/core/Config.h>
#include <felitronics/core/DetMath.h>
#include <felitronics/core/DryAligner.h>
#include <felitronics/core/Math.h>
#include <felitronics/oversampling/Oversampler.h>
#include <felitronics/storage/Buffer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace felitronics::mastering
{

//==============================================================================
// THE PEAK CLIPPER AT THE START OF THE CHAIN — after the EQ and the mono bass, ahead of the glue: a linked hard
// clip on its own oversampled grid (the chain's factor and taps, Kaiser), so the peaks the glue and the saturation would
// otherwise work on are gone before they reach them. The threshold is absolute, in dBFS on that grid; NaN clips nothing
// and only measures the highest peak the stage saw (its input does not depend on any gain a landing moves). Bypassed, the
// stage is an exact delay of its latency (the oversampler's round trip), so a bypassed stage renders the chain without it,
// shifted. Its take is counted as the limiter's clipper counts its own: per clipped oversampled sample, in 0.1 dB bins.
class StartClipper
{
public:
    static constexpr int    kBins  = 128;
    static constexpr double kBinDb = 0.1;

    struct Storage
    {
        oversampling::Oversampler::Storage os {};
        std::size_t osBuf = 0;                          // floats: channels x block x factor
        core::DryAligner::Storage align {};
        int latency = 0;
        std::uint64_t bytes() const noexcept
        {
            return os.bytes() + (std::uint64_t) sizeof (float) * (std::uint64_t) osBuf + align.freshBytes();
        }
        bool fitsWithin (const Storage& other) const noexcept
        {
            return os.fitsWithin (other.os) && osBuf <= other.osBuf && align.ring <= other.align.ring
                && align.scratch <= other.align.scratch && latency <= other.latency;
        }
    };

    [[nodiscard]] static bool storageFor (double sampleRate, int block, int channels, int factor, int tapsPerPhase,
                                          Storage& out) noexcept
    {
        Storage st;
        if (factor < 2 || block < 1 || channels < 1) return false;
        if (! oversampling::Oversampler::storageFor (oversampling::Topology::Kaiser, sampleRate, factor, channels,
                                                     tapsPerPhase, st.os)) return false;
        st.latency = oversampling::Oversampler::latencyFor (oversampling::Topology::Kaiser, sampleRate, factor, tapsPerPhase);
        st.osBuf = (std::size_t) channels * (std::size_t) block * (std::size_t) factor;
        st.align = core::DryAligner::storageFor (channels, block, st.latency + 2);
        out = st;
        return true;
    }

    bool prepare (double sampleRate, int block, int channels, int factor, int tapsPerPhase)
    {
        Storage st;
        if (! storageFor (sampleRate, block, channels, factor, tapsPerPhase, st)) return false;
        if (! os_.prepare (oversampling::Topology::Kaiser, sampleRate, factor, channels, tapsPerPhase)) return false;
        buf_.assign (st.osBuf, 0.0f);
        align_.prepare (channels, block, st.latency + 2);
        factor_ = factor; block_ = block; channels_ = channels; latency_ = st.latency;
        reset();
        return true;
    }

    void reset() noexcept
    {
        os_.reset(); align_.reset();
        std::fill (buf_.begin(), buf_.end(), 0.0f);
        resetCounters();
    }

    void resetCounters() noexcept
    {
        peak_ = 0.0f; clipped_ = 0; judged_ = 0; maxRedDb_ = 0.0;
        hist_.fill (0);
    }

    int latencySamples() const noexcept { return latency_; }

    // The threshold in dBFS on the oversampled grid (NaN: no clip), and the bypass. Read at the next quantum.
    void set (double thresholdDb, bool bypass) noexcept
    {
        bypass_ = bypass;
        clipping_ = std::isfinite (thresholdDb);
        thresholdDb_ = thresholdDb;
        threshold_ = clipping_ ? (float) core::det::pow10 (thresholdDb / 20.0) : std::numeric_limits<float>::infinity();
    }
    bool bypassed() const noexcept { return bypass_; }
    bool clipping() const noexcept { return clipping_ && ! bypass_; }

    void process (float* const* io, int channels, int block) noexcept
    {
        align_.advance ((const float* const*) io, channels, block, latency_);
        if (bypass_)
        {
            for (int c = 0; c < channels; ++c) std::copy_n (align_.delayed (c), block, io[c]);
            return;
        }
        const int n = block * factor_;
        float* up[core::kMaxChannels] {};
        for (int c = 0; c < channels && c < core::kMaxChannels; ++c) up[c] = buf_.data() + (std::size_t) c * (std::size_t) n;
        os_.upsample ((const float* const*) io, channels, block, up);
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < channels; ++c) { const float a = std::fabs (up[c][i]); if (a > peak) peak = a; }
            if (peak > peak_) peak_ = peak;
            if (! clipping_) continue;
            ++judged_;
            if (! (peak > threshold_)) continue;
            const float g = threshold_ / peak;
            for (int c = 0; c < channels; ++c) up[c][i] *= g;
            ++clipped_;
            const double red = core::gainToDbDet ((double) peak) - thresholdDb_;
            if (red > maxRedDb_) maxRedDb_ = red;
            int b = (int) (red / kBinDb);
            b = std::clamp (b, 0, kBins - 1);
            ++hist_[(std::size_t) b];
        }
        os_.downsample ((const float* const*) up, channels, block, io);
    }

    // The highest linked peak the stage saw on its grid, dBFS (−200 before any).
    double inputPeakDb() const noexcept { return peak_ > 0.0f ? core::gainToDbDet ((double) peak_) : -200.0; }
    double reductionMaxDb() const noexcept { return maxRedDb_; }
    std::int64_t clippedSamples() const noexcept { return clipped_; }
    // Of the samples it clipped, the reduction q of them stay under (the bin's upper edge); −1 where none were.
    double reductionQuantileDb (double q) const noexcept
    {
        if (! (q >= 0.0) || ! (q <= 1.0) || clipped_ <= 0) return -1.0;
        const std::int64_t want = (std::int64_t) std::ceil (q * (double) clipped_);
        std::int64_t seen = 0;
        for (int b = 0; b < kBins; ++b)
        {
            seen += hist_[(std::size_t) b];
            if (seen >= want || seen == clipped_) return (double) (b + 1) * kBinDb;
        }
        return (double) kBins * kBinDb;
    }

private:
    oversampling::Oversampler os_;
    storage::Buffer<float> buf_;
    core::DryAligner align_;
    int factor_ = 4, block_ = 0, channels_ = 0, latency_ = 0;
    bool bypass_ = false, clipping_ = false;
    double thresholdDb_ = 0.0;
    float threshold_ = std::numeric_limits<float>::infinity();
    float peak_ = 0.0f;
    std::int64_t clipped_ = 0, judged_ = 0;
    double maxRedDb_ = 0.0;
    std::array<std::int64_t, kBins> hist_ {};
};

} // namespace felitronics::mastering
