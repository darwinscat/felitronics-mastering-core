// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/Measurements.h>
#include <felitronics/analysis/PeakExcursions.h>

namespace felitronics::session::detail
{
// Aggregates and histograms only. K10 has no run-coordinate consumer. Fixed output storage is allocated
// before processing, survives analyzer destruction, and is copied by the ordinary owned snapshot path.
struct NeedlesResult
{
    static constexpr std::size_t kNumbers = 29;
    MeasurementValue numbers[kNumbers] {};
    MeasurementArray arrays[5] {};
    double duration[analysis::PeakExcursions::kDurationBins] {};
    double ceiling[analysis::PeakExcursions::kCeilingBins] {};
    double crest[3 * analysis::PeakExcursions::kCrestBins] {};
    double classes[3 * analysis::PeakExcursions::kClasses] {};
    double above[2] {};
    bool runsTruncated = false;
    void read (const analysis::PeakExcursions& analyzer, MeasurementResult& result) noexcept;
};
struct NeedlesWork
{
    analysis::PeakExcursions analyzer;
    std::uint64_t source = 0, key = 0, frames = 0, bytes = 0;
    std::uint32_t job = 0;
};
} // namespace felitronics::session::detail
