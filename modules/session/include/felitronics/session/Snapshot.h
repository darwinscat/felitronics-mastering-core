// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Session.h>
#include <felitronics/session/Measurements.h>
#include <memory>
#include <span>
#include <string_view>

namespace felitronics::session
{
struct SnapshotView
{
    std::uint32_t offeredDevices = 0;
    State state = State::Empty;
    bool mastering = false;
    std::uint64_t revision = 0;
    Project project {};
    // Touched device fields, including equal-to-machine values and false ticks.
    std::uint32_t handFieldCount = 0;
    std::string_view target;
    Source source {};
    JobId job = 0, measurementJob = 0;
    Recipe jobRecipe {};
    std::span<const Kept> masters;
    std::span<const MachineDifference> machineDifferences;
    // Empty until placement; 128 logarithmic points from 20 Hz to min(20 kHz, 0.49 * source rate).
    std::span<const EqPoint> eqCurve;
    Phase measurementProgress {}, masterProgress {};
    double sourceBytes = 0.0;
    double integratedLufs = 0.0;
    std::span<const ReadingPoint> momentary, shortTerm;
    std::span<const ReadingRun> runs;
    MeasurementStorage measurementStorage {};
    std::span<const MeasurementResult> measurements;
    JobId needlesJob = 0;
    std::uint64_t needlesSource = 0;
    std::optional<double> needlesNeedDb, needlesCeilingDb;
    Phase needlesProgress {};
    double needlesBytes = 0, needlesLargestBlockBytes = 0;
    bool needlesRunsTruncated = false;
    bool canContinueMeasurement = false;
    State measurementResumeState = State::Empty;
    bool mandatoryMeasurementsReady = false, devicesPlaced = false;
    bool measurementRowsIncluded = true;
    // False in a lean summary (Capabilities::leanSummary): every master without its traces, crest rows and mask and
    // waveform buckets — QueryKind::MasterReport gives one whole.
    bool masterRowsIncluded = true;
    TempoChoice tempoChoice {};
    bool measurementsFromSidecar = false;
    bool sourceMissingAudio = false;
    bool canMaster = false;
    MasterToken pendingMaster {};
    double pendingMasterBytes = 0.0;
    PlanView plan {};
};
class Codec;
// An immutable, owned value. view() remains valid until this value is moved or destroyed,
// independent of the session that produced it. All arrays are exact allocations.
class Snapshot final
{
public:
    Snapshot() noexcept;
    ~Snapshot();
    Snapshot (Snapshot&&) noexcept;
    Snapshot& operator= (Snapshot&&) noexcept;
    Snapshot (const Snapshot&) = delete;
    Snapshot& operator= (const Snapshot&) = delete;
    [[nodiscard]] const SnapshotView& view() const noexcept;
    [[nodiscard]] static std::uint64_t storageFor (const SnapshotView& view) noexcept;
    // Traps before allocating if combined text or reading-point storage cannot fit size_t, in every build.
    [[nodiscard]] static Snapshot copy (const SnapshotView& view) noexcept;
private:
    friend class Codec;
    SnapshotView view_ {};
    OwnedMeasurements measurements_;
    std::unique_ptr<char[]> text_;
    std::unique_ptr<Kept[]> masters_;
    std::unique_ptr<LandingPass[]> landingPasses_;
    std::unique_ptr<LandingTraceBucket[]> landingTraceRows_;
    std::unique_ptr<double[]> masterCrestRows_;
    std::unique_ptr<MasterSection[]> masterSections_;
    std::unique_ptr<MasterWaveformBucket[]> masterWaveform_;
    std::unique_ptr<MachineDifference[]> differences_;
    std::unique_ptr<EqPoint[]> eqCurve_;
    std::unique_ptr<ReadingPoint[]> points_;
    std::unique_ptr<ReadingRun[]> runs_;
};
enum class CodecStatus : std::uint8_t { Ok, Invalid, TooSmall, FloatingPointEnvironment };
struct CodecNeed
{
    CodecStatus status = CodecStatus::Ok;
    std::uint64_t bytes = 0;
};
// Named-field JSON. uint64 identities are decimal strings; byte counts are exact JSON
// numbers below 2^53. Non-finite doubles are "-Infinity", "Infinity", and "NaN".
// encode writes into caller storage; decode validates completely before it allocates.
class Codec final
{
public:
    [[nodiscard]] static CodecNeed encodedBytes (const SnapshotView& view) noexcept;
    [[nodiscard]] static CodecStatus encode (const SnapshotView& view, std::span<char> output) noexcept;
    [[nodiscard]] static CodecNeed decodedBytes (std::string_view json) noexcept;
    [[nodiscard]] static CodecStatus decode (std::string_view json, Snapshot& output) noexcept;
};
} // namespace felitronics::session
