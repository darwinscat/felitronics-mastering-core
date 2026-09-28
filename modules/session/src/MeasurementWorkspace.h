// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include "MeasurementPlan.h"
#include <memory>

namespace felitronics::session::detail
{
// One preparation per active analyzer. Pausing leaves these objects and their filter state intact.
// Publish the owned result before release(); reset/clear would retain vector capacity and is insufficient.
struct MeasurementWorkspace
{
    std::uint64_t parameterKey = 0, frames = 0;
    std::uint32_t rate = 0, channels = 0;
    bool bound = false;
    std::uint64_t allocations[kAnalyzers] {};
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    std::unique_ptr<analysis::DeterministicLoudnessMeter> loudness;
    std::unique_ptr<analysis::ClipDetector> clipping;
    std::unique_ptr<analysis::ProgrammeReport> programme;
    std::unique_ptr<analysis::LowEnd> lowEnd, infraLow;
    std::unique_ptr<analysis::SourceForensics> forensics;
    std::unique_ptr<analysis::StereoColumns> stereo;
    std::unique_ptr<analysis::WaveformPeaks> waveform;
    std::unique_ptr<analysis::StereoBandBursts> bursts;
    std::unique_ptr<analysis::BandCrest> crest;
    std::unique_ptr<analysis::HumDetector> hum;
    std::unique_ptr<tempo::TempoDetector> tempo;
    [[nodiscard]] bool prepare (Analyzer analyzer, const Pcm& pcm, const MeasurementPlan& plan) noexcept;
    void release (Analyzer analyzer) noexcept;
};
}
