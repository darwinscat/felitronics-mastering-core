// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/Measurements.h>

namespace felitronics::session
{
inline constexpr std::uint32_t kQueryColumns = 2048, kWaveformStride = 13;
inline constexpr std::uint64_t kQueryValues = std::uint64_t (kQueryColumns) * 4u * kWaveformStride;
enum class QueryKind : std::uint8_t { Waveform, LowSpectrum, LowSide, Momentary, ShortTerm, Clipping, Stereo, LimiterGr, PeakClipGr };
enum class QueryStatus : std::uint8_t { Ready, Pending, Unavailable, Empty, InvalidRange, ColumnLimit, StaleSource, Memory, Contract, Cancelled, FloatingPointEnvironment };
// Frame ranges are integer [fromFrame,toFrame). Invalid/out-of-source ranges are refused,
// never clamped. Waveform emits min(columns,range length) buckets with integer floor boundaries.
// Spectrum/Side use columns equally spaced Hz points, inclusive endpoints (one point: fromHz).
// The spectral measurements cover the whole source: those requests require [0,source.frames).
// crossoverHz selects the retained 120 or 150 Hz measurement, never a new analysis.
struct MeasurementQuery
{
    QueryKind kind = QueryKind::Waveform;
    std::uint64_t audioId = 0, fromFrame = 0, toFrame = 0;
    std::uint32_t columns = 512;
    std::uint64_t requestId = 0;
    double crossoverHz = 120, fromHz = 20, toHz = 250;
    std::uint32_t masterId = 0; // required for LimiterGr and PeakClipGr; absent in baseline source queries
};
struct QueryView
{
    MeasurementQuery request {};
    std::uint64_t audioId = 0, revision = 0, measurementKey = 0;
    QueryStatus status = QueryStatus::Pending;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint32_t sampleRate = 0, channels = 0, stride = 0;
    std::uint64_t total = 0, stored = 0;
    bool complete = false, cacheHit = false;
    std::uint64_t pcmFramesRead = 0;
    std::span<const double> values;
};
// A response owns its rows independently of the Session, cache eviction, load and other answers.
class QueryResult final
{
public:
    QueryResult() noexcept;
    ~QueryResult();
    QueryResult (QueryResult&&) noexcept;
    QueryResult& operator= (QueryResult&&) noexcept;
    QueryResult (const QueryResult&) = delete;
    QueryResult& operator= (const QueryResult&) = delete;
    [[nodiscard]] const QueryView& view() const noexcept;
    [[nodiscard]] static std::uint64_t storageFor (const QueryView& view) noexcept;
    [[nodiscard]] static QueryResult copy (const QueryView& view) noexcept;
private:
    QueryView view_ {};
    std::unique_ptr<double[]> rows_;
};
struct QueryDemand
{
    QueryStatus status = QueryStatus::Ready;
    std::uint64_t bytes = 0, largestBlockBytes = 0, rowBytes = 0;
};
} // namespace felitronics::session
