// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once


#include <felitronics/session/Text.h>
#include <array>
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
    None, Pending, Cancelled, Unsupported, TooShort, NonFinite, Capacity, NoSignal, NotImplemented, NeedNotAbove3, Memory,
    Superseded                                 // a master's damage stopped by a new master (MasterDamage)
};
// What PEAQ (analysis::Peaq, ITU-R BS.1387 Basic) said of a window of a master (MasterDamage): its PeaqVerdict, by number.
enum class DamageVerdict : std::uint8_t { NotRun, Graded, Transparent, NoSignal, NonFinite, Undefined, OutOfRange };
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
// Synthetic sidecar facts for a source whose PCM may arrive later. The sourceHash
// is the normal session FNV hash of planar f32 samples and the source shape.
struct MeasuredSource
{
    std::string_view name;
    std::uint64_t sourceHash = 0, frames = 0;
    std::uint32_t sampleRate = 0, channels = 0, fileRate = 0, bitDepth = 0;
    bool rateKnown = false;
    std::optional<double> integratedLufs, truePeakDb;
};
// An instrument may use a tempo only after this decision becomes ready. A low-confidence or
// unavailable measurement yields the configured fallback without overwriting the measured BPM.
struct TempoChoice
{
    bool ready = false, measured = false;
    double bpm = 0.0;
    MeasurementReason reason = MeasurementReason::Pending;
};
// A LIST HELD IN PLACE: room for N values, the first `count` of them meaningful — no heap, copied whole with what holds
// it. The wire carries the `count` values as an array (tools/session-codec-schema.json writes the type `T[<=N]`).
template <class T, std::size_t N> struct BoundedList
{
    static_assert (N <= 255, "count is one byte");
    std::array<T, N> items {};
    std::uint8_t count = 0;
};

// THE READINGS — the numbers a shell shows about the sound, each a fact: FactId::Value with the core's unit and
// precision (a word, for the tempo's confidence: FactId::TempoConfidence), keyed by its kind; the quantity's name is the
// catalogue's term ReadingText::name gives (terms.reading, in the order of ReadingKind). Two places carry them, each one
// ordered list: the snapshot, the source's (ReadingText::source, following the measurement), and a master's report,
// the master's (MasterReportText::readings). A kind is in a list where its number was measured and absent otherwise;
// a list names a quantity once. Append-only: a new kind takes the next number.
//   Integrated … Plr        loudness: LUFS, dBTP, LU, dB — the source's, or the master's achieved
//   DcOffset…               the DC offset, a share of full scale, signed: one channel (DcOffset), or left and right
//   LowestBand              the lowest occupied band's centre, Hz
//   PcmBits                 the most exact PCM bits a channel's grid holds
//   Correlation             the stereo correlation over the programme (absent for one channel)
//   BurstsMid, BurstsSide   the burst events of the mid and of the side (sibilance); the side absent for one channel
//   Hum                     the first channel's observed mains fundamental, Hz
//   Tempo, TempoConfidence  the headline tempo, BPM, and how sure it is (a word)
//   ClipRuns … SamplePeak   the clipped runs, the longest run and the clipped samples (both summed or most over the
//                           channels, as named), the sample peak, dBFS
//   LowSide                 the side's share of the low end
//   StereoWindows           the stereo measurement's windows
//   CrestBlocks…            the crest measurement's active blocks per band: low, low-mid, high-mid, high, full
//   Target, Ceiling         the master's target loudness and the ceiling it was held to
//   Gain, Passes, CheckPasses   the gain from the source, the landing's passes and the passes that checked it
enum class ReadingKind : std::uint8_t
{
    Integrated, TruePeak, Lra, Plr, DcOffset, DcOffsetLeft, DcOffsetRight, LowestBand, PcmBits, Correlation,
    BurstsMid, BurstsSide, Hum, Tempo, TempoConfidence, ClipRuns, LongestRun, ClippedSamples, SamplePeak, LowSide,
    StereoWindows, CrestBlocksLow, CrestBlocksLowMid, CrestBlocksHighMid, CrestBlocksHigh, CrestBlocksFull,
    Target, Ceiling, Gain, Passes, CheckPasses
};
inline constexpr std::size_t kReadingKinds = 31;
// The room of each place: the source's kinds (DcOffset and its two channels never together) and the master's.
inline constexpr std::size_t kSourceReadings = 26, kMasterReadings = 9;
struct ReadingFact
{
    ReadingKind kind = ReadingKind::Integrated;
    text::Fact fact {};
};
struct ReadingText
{
    [[nodiscard]] static text::Term name (ReadingKind kind) noexcept;
    // The source's readings from its measurement results (indexed by Analyzer), for a source of `channels`.
    [[nodiscard]] static BoundedList<ReadingFact, kSourceReadings> source (std::span<const MeasurementResult> measurements,
                                                                          std::uint32_t channels) noexcept;
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
