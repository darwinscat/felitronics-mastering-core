// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/analysis/BandCrest.h>
#include <span>

namespace felitronics::analysis
{
// A read-only source result. The owner of the spans controls its lifetime; no instrument, filters,
// PCM or probe slot is needed. Each block has five pairs (linear peak, mean square), Low to Full.
struct BandCrestResult
{
    double sampleRate = 0;
    BandCrestParams parameters {};
    std::uint64_t hopSamples = 0, frames = 0, droppedHops = 0, nonFiniteSamples = 0;
    BandCrestInvalid reason = BandCrestInvalid::NoBlock;
    double programmeMeanSquareDb = BandCrest::kSilenceDb, activityFloorDb = -70;
    std::span<const double> blocks {}, mask {};
    std::size_t blockCount() const noexcept { return blocks.size() / 10; }
    double blockPeakLin (std::size_t block, unsigned band) const noexcept
    { return block < blockCount() && band < 5 ? blocks[block * 10 + band * 2] : 0; }
    double blockMeanSq (std::size_t block, unsigned band) const noexcept
    { return block < blockCount() && band < 5 ? blocks[block * 10 + band * 2 + 1] : 0; }
    bool blockActive (std::size_t block, unsigned band) const noexcept
    { return block < blockCount() && band < 5 && mask.size() / 5 > block && mask[block * 5 + band] > 0; }
};
}
