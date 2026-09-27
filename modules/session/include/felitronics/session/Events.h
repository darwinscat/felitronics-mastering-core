// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <cstddef>
#include <cstdint>

namespace felitronics::session
{
// Wire values are stable. Facts carry typed arguments; text is a separate consumer.
enum class FactId : std::uint8_t { Measurement1, Measurement2, MasterPass, MasterReady, Cancelled, FloatingPointEnvironment };
enum class ArgKind : std::uint8_t { Number, Count, Enum };
struct FactArg
{
    ArgKind kind = ArgKind::Number;
    double value = 0.0;
    std::uint32_t count = 0;
    std::uint32_t enumId = 0;
};
struct Fact
{
    FactId id = FactId::Measurement1;
    FactArg args[2] {};
    std::uint8_t count = 0;
};
enum class PhaseName : std::uint8_t { Stream, Report, Analyzers, Pass, Remeasure };
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
// Every delta owns its rows and their positions. The stub produces no readings.
struct Reading
{
    ReadingPoint momentary[4] {};
    ReadingPoint shortTerm[4] {};
    ReadingRun runs[4] {};
    std::uint8_t momentaryCount = 0, shortTermCount = 0, runCount = 0;
};
struct Done { MasterId masterId = 0; };
struct Rejected { CommandId commandId = 0; Rejection code = Rejection::None; };
enum class ErrorCode : std::uint8_t { Trap, Contract, Refusal, Memory, Poisoned, Stale };
enum class Recover : std::uint8_t { None, Replay };
struct Error
{
    ErrorCode code = ErrorCode::Contract;
    Fact fact {};
    Recover recover = Recover::None;
    double needBytes = 0.0;                // exact integer, strictly below 2^53
};
enum class EventKind : std::uint8_t { Phase, Fact, Reading, Done, Rejected, Error };
struct EventPayload
{
    // Only the member named by kind is meaningful. All payloads are self-contained values.
    Phase phase {};
    Fact fact {};
    Reading reading {};
    Done done {};
    Rejected rejected {};
    Error error {};
};
struct Notification
{
    std::uint64_t seq = 0;
    JobId jobId = 0;
    EventKind kind = EventKind::Phase;
    EventPayload payload {};
};
enum class StepState : std::uint8_t { More, Done };
struct Stepped
{
    StepState state = StepState::Done;
    std::uint32_t units = 0;
    bool refused = false;                  // FP environment refused; the job remains resumable
};
inline constexpr std::uint32_t kStepUnits = 16;
inline constexpr std::size_t kEventBatch = 3 * kStepUnits;
} // namespace felitronics::session
