// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once


#include <felitronics/session/Text.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace felitronics::analysis { struct BandCrestResult; }

namespace felitronics::session
{
enum class Analyzer : std::uint8_t
{
    Loudness, Clipping, Programme, LowEnd, InfraLow, Forensics, Stereo, Waveform,
    StereoBursts, Crest, Hum, Tempo, Excursions, LowEnd150
};
inline constexpr std::size_t kAnalyzers = 14;
inline constexpr std::size_t kMeasurementNumbers = 256, kMeasurementArrays = 16, kMeasurementNameBytes = 64;
enum class MeasurementStatus : std::uint8_t { Pending, Ready, Unavailable, Cancelled };
enum class MeasurementReason : std::uint8_t
{
    None, Pending, Cancelled, Unsupported, TooShort, NonFinite, Capacity, NoSignal, NotImplemented, NeedNotAbove3, Memory
};
// A missing number always carries a reason. analyzerReason preserves the instrument's more specific code.
struct MeasurementValue
{
    std::string_view name;
    std::optional<double> value;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint32_t analyzerReason = 0;
};
// Coordinates are source frames. A zero step describes explicitly positioned rows.
struct MeasurementGrid
{
    std::uint64_t firstFrame = 0, stepFrames = 0, framesRead = 0;
    std::uint32_t sampleRate = 0;
};
struct MeasurementArray
{
    std::string_view name;
    MeasurementGrid grid {};
    std::uint32_t columns = 1;
    std::uint64_t total = 0, stored = 0;
    bool complete = false;
    std::span<const double> values;
};
struct MeasurementResult
{
    Analyzer analyzer = Analyzer::Loudness;
    MeasurementStatus status = MeasurementStatus::Pending;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint64_t key = 0;
    std::uint64_t framesRead = 0, total = 0, stored = 0;
    bool complete = false;
    std::span<const MeasurementValue> numbers;
    std::span<const MeasurementArray> arrays;
};
struct MeasurementStorage
{
    double sourceBytes = 0, resultBytes = 0, workspaceBytes = 0;
    double copyBytes = 0, codecBytes = 0, allocatorBytes = 0;
    double loadPeakBytes = 0, workPeakBytes = 0, peakBytes = 0, largestBlockBytes = 0;
};
class MeasurementText final
{
public:
    [[nodiscard]] static text::FactId fact (MeasurementReason reason) noexcept;
};
// Exact arrays, including text and nested rows; no analyzer or scratch pointer survives copy().
struct MeasurementCrest
{
    // The returned spans share the result owner's lifetime, including an owned Snapshot after Session destruction.
    [[nodiscard]] static analysis::BandCrestResult view (const MeasurementResult& result) noexcept;
};
class OwnedMeasurements final
{
public:
    OwnedMeasurements() noexcept;
    ~OwnedMeasurements();
    OwnedMeasurements (OwnedMeasurements&&) noexcept;
    OwnedMeasurements& operator= (OwnedMeasurements&&) noexcept;
    OwnedMeasurements (const OwnedMeasurements&) = delete;
    OwnedMeasurements& operator= (const OwnedMeasurements&) = delete;
    [[nodiscard]] std::span<const MeasurementResult> view() const noexcept;
    [[nodiscard]] static bool valid (std::span<const MeasurementResult> results) noexcept;
    [[nodiscard]] static std::uint64_t storageFor (std::span<const MeasurementResult> results) noexcept;
    [[nodiscard]] static OwnedMeasurements copy (std::span<const MeasurementResult> results) noexcept;
private:
    friend class Codec;
    std::size_t count_ = 0;
    std::unique_ptr<MeasurementResult[]> results_;
    std::unique_ptr<MeasurementValue[]> numbers_;
    std::unique_ptr<MeasurementArray[]> arrays_;
    std::unique_ptr<double[]> rows_;
    std::unique_ptr<char[]> text_;
};
} // namespace felitronics::session
