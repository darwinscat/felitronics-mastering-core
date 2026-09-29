// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Text.h>
#include <felitronics/session/Measurements.h>
#include <cstddef>
#include <cstdint>

namespace felitronics::session
{
// An event owns a text fact, including every UserText byte. Stored arguments contain no views into another value;
// view() binds them to this value's buffer for the renderer. Copies and moves remain independent, without allocation.
class OwnedFact final
{
public:
    static constexpr std::size_t kTextCapacity = 256;
    // Too many arguments or text bytes refuses whole and leaves this value unchanged. No text is truncated.
    [[nodiscard]] bool assign (const text::Fact& fact) noexcept;
    [[nodiscard]] text::Fact view() const noexcept;
private:
    text::Fact fact_ {};
    char text_[kTextCapacity] {};
    std::size_t lengths_[text::Fact::kMaxArgs] {};
};
enum class PhaseName : std::uint8_t { Stream, Report, Analyzers, Pass, Remeasure, Convert, Lra, Final };
struct Phase
{
    PhaseName name = PhaseName::Stream;
    double fraction = 0.0;                  // weighted estimate for the whole job; may move backwards
    std::uint64_t weightsVersion = 0;
    std::uint32_t pass = 0;                 // the human line is "pass N"; no budget in that line
    std::uint32_t totalPasses = 0;          // diagnostic journal only
    std::uint32_t completedUnits = 0;
    std::uint32_t totalUnits = 0;
};
struct ReadingPoint { std::uint64_t index = 0; double value = 0.0; };
struct ReadingRun { std::uint64_t first = 0; std::uint64_t count = 0; double value = 0.0; };
// Every delta owns only new rows. Frame coordinates are fixed from the first publication.
struct Reading
{
    ReadingPoint momentary[4] {};
    ReadingPoint shortTerm[4] {};
    ReadingRun runs[4] {};
    std::uint8_t momentaryCount = 0, shortTermCount = 0, runCount = 0;
    MeasurementGrid grid {};
    MeasurementReason momentaryReason = MeasurementReason::Pending, shortTermReason = MeasurementReason::Pending;
    // Six columns per run: start, length, channel, sign, level, evidence.
    double clips[24] {};
    std::uint8_t clipCount = 0;
    std::uint64_t totalRuns = 0, storedRuns = 0, tailFrames = 0;
    bool runsComplete = true, finished = false;

};
struct Done { MasterId masterId = 0; };
struct Rejected { CommandId commandId = 0; Rejection code = Rejection::None; };
enum class ErrorCode : std::uint8_t { Trap, Contract, Refusal, Memory, Poisoned, Stale };
enum class Recover : std::uint8_t { None, Replay, Continue };
struct Error
{
    ErrorCode code = ErrorCode::Contract;
    OwnedFact fact {};
    Recover recover = Recover::None;
    double needBytes = 0.0;                // exact integer, strictly below 2^53
};
struct MeasurementChange
{
    Analyzer analyzer = Analyzer::Loudness;
    MeasurementStatus status = MeasurementStatus::Pending;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint64_t key = 0, source = 0, revision = 0, framesRead = 0, total = 0, stored = 0;
    bool complete = false;
};
enum class EventKind : std::uint8_t { Phase, Fact, Reading, Done, Rejected, Error, Measurement };
struct EventPayload
{
    // Only the member named by kind is meaningful. All payloads are self-contained values.
    Phase phase {};
    OwnedFact fact {};
    Reading reading {};
    Done done {};
    Rejected rejected {};
    Error error {};
    MeasurementChange measurement {};
};
struct Notification
{
    std::uint64_t seq = 0;
    JobId jobId = 0;
    EventKind kind = EventKind::Phase;
    EventPayload payload {};
    std::uint64_t source = 0, revision = 0, completedWork = 0, totalWork = 0;
    PhaseName phase = PhaseName::Stream;
    State state = State::Empty;

};
enum class StepState : std::uint8_t { More, Done };
struct Stepped
{
    StepState state = StepState::Done;
    std::uint32_t units = 0;
    bool refused = false;                  // FP refusal leaves the job resumable and publishes Recover::Continue
};
inline constexpr std::uint32_t kStepUnits = 16;
inline constexpr std::size_t kEventBatch = 3 * kStepUnits;
} // namespace felitronics::session
