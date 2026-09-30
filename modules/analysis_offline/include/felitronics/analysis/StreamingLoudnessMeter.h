// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/analysis/LoudnessMeter.h>
#include <felitronics/storage/Buffer.h>
#include <array>

namespace felitronics::analysis
{
// The deterministic core v0.55 loudness kernel with write-before-read stores. Core's public meter
// value-initializes both duration-sized vectors during prepare. The offline pump must yield without
// that scan: reserve uninitialized scalar storage, reset counts, and initialize each observation as
// it arrives. The filter, accumulation order, gates and damage handling match the core meter.
// Storage geometry remains core's; live/direct and kernel parity tests pin every numerical boundary.
template <class MathPolicy = core::DetMath>
class BasicStreamingLoudnessMeter
{
public:
    using Math = MathPolicy;
    struct Storage : DeterministicLoudnessMeter::Storage
    {
        std::uint64_t bytes() const noexcept
        { return DeterministicLoudnessMeter::Storage::bytes() - 300u * sizeof (double); }
    };
    static constexpr double kMinSampleRate = core::kMinSampleRate;
    [[nodiscard]] static bool storageFor (double rate, double samples, Storage& out) noexcept
    { return DeterministicLoudnessMeter::storageFor (rate, samples, out); }
    [[nodiscard]] bool prepare (double sampleRate, int channels, double duration = 3600.0)
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        return prepareForSamples (sampleRate, channels, std::max (0.0, duration) * rate);
    }
    [[nodiscard]] bool prepareForSamples (double sampleRate, int channels, double samples)
    {
        prepared_ = false;
        reset();
        if (channels < 1 || channels > kMaxChannels) return false;
        Storage st;
        if (! storageFor (sampleRate, samples, st)) return false;
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        ch = channels;
        kw.prepare (fs, ch);
        subSamples = st.subSamples;
        for (int c = 0; c < kMaxChannels; ++c) w[c] = 1.0;
        blockE.resizeForOverwrite (st.blocks);
        stE.resizeForOverwrite (st.shortTerm);
        reset();
        prepared_ = true;
        return true;
    }
    void reset() noexcept
    {
        kw.reset();
        for (int c = 0; c < kMaxChannels; ++c) subSumSq[c] = 0.0;
        ranNc_ = 0;
        subCount = 0; subWrite = 0; subFilled = 0; subInHop = 0; blockCount = 0; droppedBlocks_ = 0;
        nonFiniteSubHops_ = 0;
        stCount = 0; droppedShortTerm_ = 0;
        std::fill (subRing.begin(), subRing.end(), 0.0);
    }
    void setChannelWeight (int c, double weight) noexcept
    {
        if (c >= 0 && c < kMaxChannels && std::isfinite (weight)) w[c] = weight;
    }
    [[nodiscard]] bool process (const float* const* channels, int numChannels, int n) noexcept
    {
        if (numChannels < 0 || n < 0) return false;
        if (! prepared_) return false;
        if (numChannels > ch) return false;
        if (n == 0) return true;
        const int nc = numChannels;
        for (int c = nc; c < ranNc_; ++c) kw.resetChannel (c);
        ranNc_ = nc;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nc; ++c) { const double y = kw.process (c, (double) channels[c][i]); subSumSq[c] += y * y; }
            if (++subCount >= subSamples) finishSubHop (nc);
        }
        return true;
    }
    double momentaryLufs()  const noexcept { return lufsOf (meanLastSubHops (kMomentarySubHops)); }
    double shortTermLufs()  const noexcept { return lufsOf (meanLastSubHops (kSubRing)); }
    double integratedLufs() const noexcept { return integrated(); }
    double loudnessRangeLu() const noexcept { return lra(); }
    int shortTermCount() const noexcept { return stCount; }
    int droppedBlocks() const noexcept { return droppedBlocks_; }
    int droppedShortTermSamples() const noexcept { return droppedShortTerm_; }
    std::uint64_t nonFiniteSubHops() const noexcept { return nonFiniteSubHops_; }
    std::span<const double> gatingBlockEnergies() const noexcept
    {
        return { blockE.data(), (std::size_t) blockCount };
    }
    int gatingBlockCount() const noexcept { return blockCount; }
    // The whole-call accessors above remain the numerical oracle. These saved scans
    // use the same gates and accumulation order with a bounded number of bins per call.
    void beginIntegratedScan() noexcept
    { scanIndex_ = 0; scanPhase_ = 0; scanSum_ = 0.0; scanCount_ = 0; }
    bool stepIntegratedScan (int limit, double& value, int& used) noexcept
    {
        used = 0;
        const double absT = Math::pow10 ((-70.0 + 0.691) / 10.0);
        for (; used < limit; ++used)
        {
            if (scanIndex_ == blockCount)
            {
                if (scanPhase_ == 0)
                {
                    if (scanCount_ == 0) { value = -120.0; return true; }
                    scanThreshold_ = 0.1 * (scanSum_ / scanCount_);
                    scanPhase_ = 1; scanIndex_ = 0; scanSum_ = 0.0; scanCount_ = 0;
                }
                else { value = scanCount_ > 0 ? lufsOf (scanSum_ / scanCount_) : -120.0; return true; }
            }
            const double e = blockE[(std::size_t) scanIndex_++];
            if (std::isfinite (e) && e > absT && (scanPhase_ == 0 || e > scanThreshold_))
                { scanSum_ += e; ++scanCount_; }
        }
        return false;
    }
    void beginRangeScan() noexcept
    { scanIndex_ = 0; scanPhase_ = 0; scanSum_ = 0.0; scanCount_ = 0; rangeTotal_ = 0;
      rangeCum_ = 0; rangeLowBin_ = -1; }
    bool stepRangeScan (int limit, double& value, int& used) noexcept
    {
        used = 0;
        const double absT = Math::pow10 ((-70.0 + 0.691) / 10.0);
        for (; used < limit; ++used)
        {
            if (scanPhase_ == 0 && scanIndex_ == stCount)
            {
                if (scanCount_ == 0) { value = 0.0; return true; }
                scanThreshold_ = 0.01 * (scanSum_ / scanCount_);
                scanPhase_ = 1; scanIndex_ = 0;
            }
            if (scanPhase_ == 1)
            {
                rangeHist_[(std::size_t) scanIndex_++] = 0;
                if (scanIndex_ == (int) rangeHist_.size()) { scanPhase_ = 2; scanIndex_ = 0; }
                continue;
            }
            if (scanPhase_ == 2 && scanIndex_ == stCount)
            {
                if (rangeTotal_ < 2) { value = 0.0; return true; }
                rangeLowRank_ = (int) ((double) (rangeTotal_ - 1) * 0.10 + 0.5);
                rangeHighRank_ = (int) ((double) (rangeTotal_ - 1) * 0.95 + 0.5);
                scanPhase_ = 3; scanIndex_ = 0;
            }
            if (scanPhase_ == 3)
            {
                rangeCum_ += rangeHist_[(std::size_t) scanIndex_];
                if (rangeCum_ > rangeLowRank_ && rangeLowBin_ < 0) rangeLowBin_ = scanIndex_;
                if (rangeCum_ > rangeHighRank_)
                {
                    // Preserve the whole-call subtraction order and rounding.
                    value = (-70.0 + (double) scanIndex_ * 0.1)
                          - (-70.0 + (double) rangeLowBin_ * 0.1);
                    ++used;
                    return true;
                }
                ++scanIndex_;
                continue;
            }
            const double e = stE[(std::size_t) scanIndex_++];
            if (std::isfinite (e) && e >= absT)
            {
                if (scanPhase_ == 0) { scanSum_ += e; ++scanCount_; }
                else if (e >= scanThreshold_)
                {
                    int b = (int) ((lufsOf (e) + 70.0) * 10.0);
                    b = b < 0 ? 0 : (b >= (int) rangeHist_.size() ? (int) rangeHist_.size() - 1 : b);
                    ++rangeHist_[(std::size_t) b]; ++rangeTotal_;
                }
            }
        }
        return false;
    }
private:
    static constexpr int kMaxChannels      = core::kMaxChannels;
    static constexpr int kSubHopsPerHop    = 10;
    static constexpr int kMomentarySubHops = 40;
    static constexpr int kSubRing          = 300;
    static constexpr int kMaxSubHop        = std::numeric_limits<int>::max() / 10;
    static constexpr int kMaxBlocks        = std::numeric_limits<int>::max() - 8;
    void finishSubHop (int nc) noexcept
    {
        kw.flushDenormals();
        double subMS = 0.0; bool poisoned = false;
        for (int c = 0; c < nc; ++c)
        {
            const double e = subSumSq[c];
            if (! std::isfinite (e))
            {
                const bool excluded = core::exactlyEqual (w[c], 0.0);
                if (! excluded) poisoned = true;
                continue;
            }
            subMS += w[c] * (e / (double) subSamples);
        }
        if (poisoned && nonFiniteSubHops_ != ~std::uint64_t {}) ++nonFiniteSubHops_;
        if (! std::isfinite (subMS)) { subMS = 0.0; if (! poisoned && nonFiniteSubHops_ != ~std::uint64_t {}) ++nonFiniteSubHops_; }
        subRing[(std::size_t) subWrite] = subMS;
        subWrite = (subWrite + 1) % kSubRing;
        if (subFilled < kSubRing) ++subFilled;
        if (++subInHop >= kSubHopsPerHop) { subInHop = 0; finishHop(); }
        for (int c = 0; c < kMaxChannels; ++c) subSumSq[c] = 0.0;
        subCount = 0;
    }
    void finishHop() noexcept
    {
        if (subFilled >= kMomentarySubHops)
        {
            if (blockCount < (int) blockE.size()) blockE[(std::size_t) blockCount++] = meanLastSubHops (kMomentarySubHops);
            else ++droppedBlocks_;
        }
        if (subFilled >= kSubRing)
        {
            if (stCount < (int) stE.size()) stE[(std::size_t) stCount++] = meanLastSubHops (kSubRing);
            else ++droppedShortTerm_;
        }
    }
    double meanLastSubHops (int k) const noexcept
    {
        const int kk = k < subFilled ? k : subFilled;
        if (kk <= 0) return 0.0;
        double s = 0.0;
        for (int j = 0; j < kk; ++j) { const int idx = (subWrite - 1 - j + kSubRing) % kSubRing; s += subRing[(std::size_t) idx]; }
        return s / kk;
    }
    static double lufsOf (double meanSquare) noexcept { return meanSquare > 1e-12 ? -0.691 + 10.0 * Math::log10 (meanSquare) : -120.0; }
    double integrated() const noexcept
    {
        if (blockCount <= 0) return -120.0;
        const double absT = Math::pow10 ((-70.0 + 0.691) / 10.0);
        double sum = 0.0; int cnt = 0;
        for (int j = 0; j < blockCount; ++j) { const double z = blockE[(std::size_t) j]; if (std::isfinite (z) && z > absT) { sum += z; ++cnt; } }
        if (cnt == 0) return -120.0;
        const double relT = 0.1 * (sum / cnt);
        double s2 = 0.0; int c2 = 0;
        for (int j = 0; j < blockCount; ++j) { const double z = blockE[(std::size_t) j]; if (std::isfinite (z) && z > absT && z > relT) { s2 += z; ++c2; } }
        return c2 > 0 ? lufsOf (s2 / c2) : -120.0;
    }
    double lra() const noexcept
    {
        if (stCount <= 0) return 0.0;
        const double absT = Math::pow10 ((-70.0 + 0.691) / 10.0);
        double sum = 0.0; int cnt = 0;
        for (int j = 0; j < stCount; ++j) { const double e = stE[(std::size_t) j]; if (std::isfinite (e) && e >= absT) { sum += e; ++cnt; } }
        if (cnt == 0) return 0.0;
        const double relT = 0.01 * (sum / cnt);
        constexpr int kBins = 1000;
        int hist[kBins] = { 0 }; int total = 0;
        for (int j = 0; j < stCount; ++j)
        {
            const double e = stE[(std::size_t) j];
            if (std::isfinite (e) && e >= absT && e >= relT)
            {
                int b = (int) ((lufsOf (e) + 70.0) * 10.0);
                b = b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
                ++hist[b]; ++total;
            }
        }
        if (total < 2) return 0.0;
        auto pct = [&] (double p) {
            const int rank = (int) ((double) (total - 1) * p + 0.5);
            int cum = 0, b = 0;
            for (; b < kBins; ++b) { cum += hist[b]; if (cum > rank) break; }
            return -70.0 + (double) (b < kBins ? b : kBins - 1) * 0.1;
        };
        return pct (0.95) - pct (0.10);
    }
    double fs = 48000.0; int ch = 2, subSamples = 480;
    int ranNc_ = 0;
    bool prepared_ = false;
    DeterministicKWeightingFilter kw;
    double w[kMaxChannels] {};
    double subSumSq[kMaxChannels] {};
    int subCount = 0;
    std::array<double, 300> subRing {};
    int subWrite = 0, subFilled = 0, subInHop = 0;
    storage::Buffer<double> blockE;
    int blockCount = 0, droppedBlocks_ = 0, droppedShortTerm_ = 0;
    std::uint64_t nonFiniteSubHops_ = 0;
    storage::Buffer<double> stE;
    int stCount = 0;
    int scanIndex_ = 0, scanPhase_ = 0, scanCount_ = 0;
    double scanSum_ = 0.0, scanThreshold_ = 0.0;
    std::array<int, 1000> rangeHist_ {};
    int rangeTotal_ = 0, rangeCum_ = 0, rangeLowRank_ = 0, rangeHighRank_ = 0, rangeLowBin_ = -1;
};
using StreamingLoudnessMeter = BasicStreamingLoudnessMeter<core::DetMath>;
using SystemStreamingLoudnessMeter = BasicStreamingLoudnessMeter<core::SystemMath>;
} // namespace felitronics::analysis
