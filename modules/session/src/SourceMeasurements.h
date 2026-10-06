// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Events.h>
#include <felitronics/analysis/StereoColumns.h>
#include <array>
#include <memory>

namespace felitronics::analysis { class LowEnd; class SourceForensics; class HumDetector; class StereoBandBursts; }
namespace felitronics::tempo { class TempoDetector; }

namespace felitronics::session::detail
{
// Rows are reserved before preparation, filled in bounded batches, then published without a copy.
// Names and descriptors have fixed bounds; only the numeric rows grow with the source.
struct MeasurementStore
{
    MeasurementValue numbers[kMeasurementNumbers] {};
    MeasurementArray arrays[kMeasurementArrays] {};
    char names[kMeasurementNumbers][kMeasurementNameBytes] {};
    std::unique_ptr<double[]> rows;
    std::uint64_t capacity = 0, used = 0;
    std::size_t numberCount = 0, arrayCount = 0;
    void number (std::string_view name, double value, int channel = -1,
                 MeasurementReason reason = MeasurementReason::None, unsigned nativeReason = 0) noexcept;
    [[nodiscard]] double* array (std::string_view name, std::uint32_t columns, std::uint64_t total,
                                 std::uint64_t stored, bool complete, MeasurementGrid grid) noexcept;
};
struct SourceMeasurements
{
    static constexpr std::array order { Analyzer::LowEnd, Analyzer::LowEnd150, Analyzer::InfraLow,
        Analyzer::Forensics, Analyzer::Stereo, Analyzer::Crest, Analyzer::Hum, Analyzer::StereoBursts, Analyzer::Tempo };
    // The first measurement is the loudness and the true peak (owner, 02.10): it ends before any of these runs. The two
    // low-end runs a target's devices read (the high-pass the 120 Hz one, mono bass the one at the target's crossover:
    // 120 or 150) lead the order, so a change of target finds its run next or done; the findings follow.
    static constexpr unsigned firstCount = 0;
    std::unique_ptr<MeasurementStore> results[kAnalyzers];
    std::uint64_t bytes = 0, frames = 0, copied = 0, work = 0;
    unsigned cursor = 0, stage = 0, facts = 0;
    // A waiting master may move tempo ahead of the remaining optional analyzers
    // at a stage boundary, then return to this cursor without repeating work.
    unsigned tempoReturnCursor = unsigned (order.size());
    bool tempoPublishedEarly = false;
    text::Fact warnings[8] {};
    unsigned warningCount = 0, warningRead = 0;
    bool initialWarnings = false, firstReady = false, firstPublished = false, finished = false;
    bool lowEndsReady = false;
    analysis::StereoSums stereo {};
    std::uint64_t stereoFinite = 0, stereoNonFinite = 0;
    bool dualMono = true;
    double crestSum = 0, crestFloor = 0;
    std::uint64_t crestCount = 0;
};
struct SourceWarnings
{
    [[nodiscard]] static text::FactId quiet (double lufs) noexcept;
    [[nodiscard]] static bool wideBass (double side) noexcept;
};
struct SourceResults
{
    static void lowEnd (MeasurementStore& out, const analysis::LowEnd& instrument, double duty, double margin, double fromHz) noexcept;
    static void forensics (MeasurementStore& out, const analysis::SourceForensics& instrument, int bitDepth) noexcept;
    [[nodiscard]] static int forensicsMetadata (MeasurementStore& out, int bitDepth) noexcept;
    static void hum (MeasurementStore& out, const analysis::HumDetector& instrument) noexcept;
    static void bursts (MeasurementStore& out, const analysis::StereoBandBursts& instrument) noexcept;
    static void tempo (MeasurementStore& out, const tempo::TempoDetector& instrument, std::uint32_t rate,
                       std::uint64_t frames) noexcept;
};
} // namespace felitronics::session::detail
