// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Commands.h>
#include <felitronics/analysis/ProgrammeReport.h>
#include <felitronics/analysis/ClipDetector.h>
#include <felitronics/analysis/LowEnd.h>
#include <felitronics/analysis/SourceForensics.h>
#include <felitronics/analysis/StereoColumns.h>
#include <felitronics/analysis/WaveformPeaks.h>
#include <felitronics/analysis/StereoBandBursts.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/HumDetector.h>
#include <felitronics/tempo/TempoDetector.h>
#include <array>

namespace felitronics::session::detail
{
// These exact values feed storageFor and preparation. The target is deliberately absent.
struct MeasurementParameters
{
    int maxBlock = 1024, clipRuns = 65536, columns = 1200, waveformBuckets = 1200;
    analysis::PeakMix waveformMix = analysis::PeakMix::Average;
    analysis::ProgrammeReportParams programme {};
    analysis::LowEndParams lowEnd {}, infraLow {}, lowEnd150 {};
    analysis::SourceForensicsParams forensics {};
    analysis::StereoBandBursts::Params bursts {};
    analysis::BandCrestParams crest {};
    analysis::HumDetectorParams hum {};
    tempo::TempoParams tempo {};
};
struct AnalyzerDemand
{
    bool available = false;
    std::uint64_t workspace = 0, result = 0, rowValues = 0;
};
struct MeasurementPlan
{
    // The instruments the pump currently runs together. Other prices are independent preparations,
    // admitted before use; scheduling another instrument must also add its lifetime to this plan.
    static constexpr std::array streaming { Analyzer::Loudness, Analyzer::Clipping, Analyzer::Programme };
    MeasurementParameters parameters {};
    std::array<AnalyzerDemand, kAnalyzers> analyzers {};
    MeasurementStorage storage {};
    Rejection rejection = Rejection::None;
    [[nodiscard]] static MeasurementParameters parametersFor (const Pcm& pcm) noexcept;
    [[nodiscard]] static MeasurementPlan storageFor (const Pcm& pcm, const MeasurementParameters& parameters) noexcept;
    [[nodiscard]] static std::uint64_t key (std::uint64_t pcmHash, const MeasurementParameters& parameters,
                                           std::uint64_t configVersion, Version core, Version session) noexcept;
};
} // namespace felitronics::session::detail
