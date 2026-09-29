// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace felitronics::analysis
{
// Four axes, in order L, R, (L+R)/2, (L-R)/2. Mono has L and Mid only.
// Energies are sums of squares, not dB. Bands use complementary one-pole splits:
// low = LP250(x), middle = LP2500(x)-low, high = x-LP2500(x).
// LP is y += a*(x-y), a = 2*pi*f/(rate+2*pi*f), initially zero. These are drawing
// coordinates, not the LR4 bass measurements. Non-finite samples reset that axis's filters.
struct WaveformAxis
{
    double minimum, maximum, squareSum, envelope, lowEnergy, middleEnergy, highEnergy;
    std::uint64_t finite;
};
struct WaveformColumn { WaveformAxis axes[4]; };

// Source-owned, compact pyramid. No PCM is retained. Allocation initializes no source-sized
// store: leaves/checkpoints and their completed ancestors are initialized as written.
// A leaf is at least 256 frames and an integer number of envelope boxes. Exact edges replay
// only their leaf from its filter checkpoint; complete interior leaves use the pyramid.
class WaveformIndex
{
public:
    struct Filters { double low[4], upper[4]; };
    struct Storage
    {
        bool ok = false;
        std::uint64_t leaves = 0, nodes = 0;
        std::uint32_t boxFrames = 0, leafFrames = 0;
        [[nodiscard]] std::uint64_t bytes() const noexcept
        { return nodes * sizeof (WaveformColumn) + leaves * sizeof (Filters); }
    };
    [[nodiscard]] static Storage storageFor (std::uint32_t rate, unsigned channels, std::uint64_t frames) noexcept
    {
        if (rate < 8000 || rate > 768000 || channels < 1 || channels > 2 || frames == 0 || frames >= (1ull << 53)) return {};
        Storage out;
        out.boxFrames = std::max (1u, (rate + 4000u) / 8000u);
        out.leafFrames = out.boxFrames * ((256u + out.boxFrames - 1u) / out.boxFrames);
        out.leaves = (frames + out.leafFrames - 1u) / out.leafFrames;
        for (auto n = out.leaves;; n = (n + 1u) / 2u) { out.nodes += n; if (n == 1) break; }
        const auto limit = std::uint64_t (std::numeric_limits<std::size_t>::max());
        if (out.nodes <= limit / sizeof (WaveformColumn))
            out.ok = out.leaves <= (limit - out.nodes * sizeof (WaveformColumn)) / sizeof (Filters);
        return out;
    }
    [[nodiscard]] bool prepare (std::uint32_t rate, unsigned channels, std::uint64_t frames)
    {
        const auto s = storageFor (rate, channels, frames);
        if (! s.ok) return false;
        nodes_.reset (new WaveformColumn[std::size_t (s.nodes)]);
        checkpoints_.reset (new Filters[std::size_t (s.leaves)]);
        storage_ = s; frames_ = frames; channels_ = channels; seen_ = readyLeaves_ = 0;
        levels_ = 0; std::uint64_t offset = 0;
        for (auto n = s.leaves;; n = (n + 1u) / 2u)
        {
            offsets_[levels_] = offset; counts_[levels_++] = n; offset += n;
            if (n == 1) break;
        }
        constexpr double twoPi = 6.283185307179586476925286766559;
        lowA_ = (twoPi * 250.0) / (double (rate) + twoPi * 250.0);
        upperA_ = (twoPi * 2500.0) / (double (rate) + twoPi * 2500.0);
        filters_ = {}; active_ = {}; box_ = {}; boxCount_ = {};
        return true;
    }
    [[nodiscard]] bool process (const float* const* planes, unsigned channels, std::uint32_t count) noexcept
    {
        if (! nodes_ || channels != channels_ || planes == nullptr || count > frames_ - seen_) return false;
        for (unsigned c = 0; c < channels; ++c) if (count != 0 && planes[c] == nullptr) return false;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (seen_ % storage_.leafFrames == 0) checkpoints_[std::size_t (seen_ / storage_.leafFrames)] = filters_;
            sample (planes[0][i], channels == 2 ? double (planes[1][i]) : 0, filters_, active_, box_, boxCount_, true);
            ++seen_;
            if (seen_ % storage_.boxFrames == 0 || seen_ == frames_) flushBox (active_, box_, boxCount_);
            if (seen_ % storage_.leafFrames == 0 || seen_ == frames_)
            {
                std::uint64_t iNode = readyLeaves_++;
                nodes_[std::size_t (iNode)] = active_; active_ = {};
                for (unsigned level = 0; level + 1 < levels_; ++level)
                {
                    if ((iNode & 1u) == 0 && iNode + 1u < counts_[level]) break;
                    const auto left = iNode & ~std::uint64_t (1);
                    auto value = nodes_[std::size_t (offsets_[level] + left)];
                    if (left + 1u < counts_[level]) merge (value, nodes_[std::size_t (offsets_[level] + left + 1u)]);
                    iNode /= 2u;
                    nodes_[std::size_t (offsets_[level + 1u] + iNode)] = value;
                }
            }
        }
        return true;
    }
    // [first,last), including a partial final leaf. Reads no frame outside the resident source.
    // An empty range is a valid empty column. A not-yet-built range is refused without touching out.
    // pcmFramesRead is deterministic cost evidence, including checkpoint replay before an exact edge.
    [[nodiscard]] bool read (const float* const* planes, std::uint64_t first, std::uint64_t last,
                             WaveformColumn& out, std::uint64_t& pcmFramesRead) const noexcept
    {
        if (! nodes_ || first > last || last > seen_ || planes == nullptr) return false;
        for (unsigned c = 0; c < channels_; ++c) if (planes[c] == nullptr) return false;
        WaveformColumn value {};
        auto cursor = first;
        while (cursor < last)
        {
            const auto leaf = cursor / storage_.leafFrames;
            const auto leafEnd = std::min (frames_, (leaf + 1u) * storage_.leafFrames);
            if (cursor % storage_.leafFrames == 0 && leafEnd <= last && leaf < readyLeaves_)
            {
                unsigned level = 0; std::uint64_t span = 1;
                while (level + 1u < levels_ && leaf % (span * 2u) == 0)
                {
                    const auto endLeaf = std::min (storage_.leaves, leaf + span * 2u);
                    if (endLeaf > readyLeaves_ || std::min (frames_, endLeaf * storage_.leafFrames) > last) break;
                    ++level; span *= 2u;
                }
                merge (value, nodes_[std::size_t (offsets_[level] + leaf / span)]);
                cursor = std::min (frames_, (leaf + span) * storage_.leafFrames);
            }
            else
            {
                auto filters = checkpoints_[std::size_t (leaf)];
                WaveformColumn edge {}; Box box {}; BoxCount count {};
                const auto end = std::min (last, leafEnd);
                for (auto frame = leaf * storage_.leafFrames; frame < end; ++frame)
                {
                    sample (planes[0][std::size_t (frame)], channels_ == 2 ? double (planes[1][std::size_t (frame)]) : 0,
                            filters, edge, box, count, frame >= cursor);
                    ++pcmFramesRead;
                    if ((frame + 1u) % storage_.boxFrames == 0 || frame + 1u == end) flushBox (edge, box, count);
                }
                merge (value, edge); cursor = end;
            }
        }
        out = value;
        return true;
    }
    [[nodiscard]] bool complete() const noexcept { return nodes_ && seen_ == frames_; }
    [[nodiscard]] std::uint64_t framesSeen() const noexcept { return seen_; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return storage_.bytes(); }
    [[nodiscard]] std::uint32_t leafFrames() const noexcept { return storage_.leafFrames; }
    [[nodiscard]] std::uint32_t boxFrames() const noexcept { return storage_.boxFrames; }
    [[nodiscard]] bool axisPresent (unsigned axis) const noexcept { return axis < 4 && (channels_ == 2 || axis == 0 || axis == 2); }
    static void merge (WaveformColumn& into, const WaveformColumn& from) noexcept
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            auto& a = into.axes[i]; const auto& b = from.axes[i];
            if (b.finite == 0) continue;
            if (a.finite == 0) { a = b; continue; }
            a.minimum = std::min (a.minimum, b.minimum); a.maximum = std::max (a.maximum, b.maximum);
            a.squareSum += b.squareSum; a.envelope = std::max (a.envelope, b.envelope);
            a.lowEnergy += b.lowEnergy; a.middleEnergy += b.middleEnergy; a.highEnergy += b.highEnergy; a.finite += b.finite;
        }
    }
private:
    struct Box { double values[4]; };
    struct BoxCount { unsigned values[4]; };
    void sample (double left, double right, Filters& filters, WaveformColumn& out, Box& box, BoxCount& counts, bool keep) const noexcept
    {
        const double values[] { left, right, channels_ == 1 ? left : (left + right) * 0.5, (left - right) * 0.5 };
        for (unsigned i = 0; i < 4; ++i)
        {
            if (! axisPresent (i)) continue;
            const double x = values[i];
            if (! std::isfinite (x)) { filters.low[i] = filters.upper[i] = 0; continue; }
            filters.low[i] += lowA_ * (x - filters.low[i]);
            filters.upper[i] += upperA_ * (x - filters.upper[i]);
            if (! keep) continue;
            auto& a = out.axes[i];
            if (a.finite == 0) a.minimum = a.maximum = x;
            else { a.minimum = std::min (a.minimum, x); a.maximum = std::max (a.maximum, x); }
            a.squareSum += x * x;
            const double low = filters.low[i], middle = filters.upper[i] - low, high = x - filters.upper[i];
            a.lowEnergy += low * low; a.middleEnergy += middle * middle; a.highEnergy += high * high;
            ++a.finite; box.values[i] += x; ++counts.values[i];
        }
    }
    static void flushBox (WaveformColumn& out, Box& box, BoxCount& counts) noexcept
    {
        for (unsigned i = 0; i < 4; ++i)
            if (counts.values[i]) out.axes[i].envelope = std::max (out.axes[i].envelope, std::fabs (box.values[i] / double (counts.values[i])));
        box = {}; counts = {};
    }
    std::unique_ptr<WaveformColumn[]> nodes_;
    std::unique_ptr<Filters[]> checkpoints_;
    Storage storage_ {};
    std::uint64_t frames_ = 0, seen_ = 0, readyLeaves_ = 0, offsets_[64] {}, counts_[64] {};
    unsigned channels_ = 0, levels_ = 0;
    double lowA_ = 0, upperA_ = 0;
    Filters filters_ {};
    WaveformColumn active_ {};
    Box box_ {};
    BoxCount boxCount_ {};
};
// A non-owning producer can be released at any point; its source-owned index and cursor survive.
class WaveformBuilder
{
public:
    explicit WaveformBuilder (WaveformIndex& index) noexcept : index_ (index) {}
    [[nodiscard]] bool process (const float* const* planes, unsigned channels, std::uint32_t frames) noexcept
    { return index_.process (planes, channels, frames); }
private:
    WaveformIndex& index_;
};
} // namespace felitronics::analysis
