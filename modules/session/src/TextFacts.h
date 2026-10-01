// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE SHAPES OF THE FACTS (internal to modules/session): what the code states and the catalog is held to. The renderer
// (src/Text.cpp) and the build's gate (src/TextSchema.cpp, run by felitronics_session_text_check before the library is
// built) read the same tables from here, so the catalog is checked against the declarations the renderer uses.
//
//   * kFacts — for each FactId, its key in modules/session/text/catalog.toml and its arguments IN ORDER, each with the
//     name its placeholder spells ({passes}) and its kind; an argument of kind Term names the term group it takes.
//   * kTerms — for each Term, its group and its key: terms.<group>.<key> in the catalog.
//   * the twelve languages' codes, the units' keys in modules/session/text/format.toml, and each language's CLDR plural
//     categories (the gate requires exactly these in a plural message; src/TextNumber.cpp selects among them).
//   * the placeholder grammar: `{` name `}`, the name an ASCII letter followed by letters and digits. Nothing else in a
//     message may be a brace: the gate refuses one, so a message never needs an escape.
//
// ADDING A FACT: a FactId in <felitronics/session/Text.h> with the next number of its range, its row here (in id order),
// and its message in every language the catalog declares — the build is red until all three agree.
// RETIRING A FACT: its row here and its message go; its FactId stays (the ABI manifest freezes it), marked retired, and
// no other fact ever takes its number. A retired id renders as its number and the snapshot decoder refuses it.

#include <felitronics/session/Text.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace felitronics::session::text::detail
{

struct ArgShape
{
    std::string_view name;            // what its placeholder spells, without the braces
    ArgKind kind = ArgKind::None;
    std::string_view group;           // Term: the term group of the catalog it takes; empty otherwise

};

struct FactShape
{
    FactId id = FactId::Value;
    std::string_view key;             // messages.<key> in the catalog; the id a missing message renders as
    std::array<ArgShape, Fact::kMaxArgs> args {};
    std::size_t argCount = 0;
};

inline constexpr FactShape kFacts[] = {
    { FactId::Value, "value", { { { "value", ArgKind::Value, {} } } }, 1 },
    { FactId::LandingPass, "landingPass",
      { { { "pass", ArgKind::Count, {} }, { "passes", ArgKind::Count, {} } } }, 2 },
    { FactId::LandingConverged, "landingConverged", { { { "passes", ArgKind::Count, {} } } }, 1 },
    { FactId::PairsConsistent, "pairsConsistent",
      { { { "agreed", ArgKind::Count, {} }, { "pairs", ArgKind::Count, {} } } }, 2 },
    { FactId::LoudestLowNote, "loudestLowNote", { { { "note", ArgKind::Midi, {} } } }, 1 },
    { FactId::WideBass, "wideBass", { { { "side", ArgKind::Value, {} } } }, 1 },
    { FactId::RateAboveLimit, "rateAboveLimit",
      { { { "rate", ArgKind::Value, {} }, { "limit", ArgKind::Value, {} }, { "platform", ArgKind::Term, "platform" } } },
      3 },
    { FactId::MachineDifferences, "machineDifferences", { { { "count", ArgKind::Count, {} } } }, 1 },
    // DefaultsConverted (9) is retired: no row, no message; the id stays reserved.
    { FactId::SameCoreMachineDifferences, "sameCoreMachineDifferences", { { { "count", ArgKind::Count, {} } } }, 1 },
    { FactId::MasterLandingMiss, "masterLandingMiss", { { { "achieved", ArgKind::Value, {} }, { "target", ArgKind::Value, {} }, { "gap", ArgKind::Value, {} } } }, 3 },
    { FactId::MasterHintSubBass, "masterHintSubBass", { { { "share", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterHintPeaks, "masterHintPeaks", { { { "reduction", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterHintDark, "masterHintDark", { { { "share", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterHintDemand, "masterHintDemand", { { { "gap", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterHintGainRange, "masterHintGainRange", { { { "gain", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterHintTruePeak, "masterHintTruePeak", { { { "peak", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterCrestSourceRate, "masterCrestSourceRate", { { { "rate", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterCrestPending, "masterCrestPending", {}, 0 },
    { FactId::MasterCrestUnavailable, "masterCrestUnavailable", {}, 0 },
    { FactId::MasterReportUnavailable, "masterReportUnavailable", {}, 0 },
    { FactId::MasterCrestDelivered, "masterCrestDelivered", { { { "rate", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterLandingAbove, "masterLandingAbove", { { { "achieved", ArgKind::Value, {} }, { "target", ArgKind::Value, {} }, { "gap", ArgKind::Value, {} } } }, 3 },
    { FactId::MasterCostShape, "masterCostShape", { { { "shift", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterCostCrest, "masterCostCrest", { { { "loss", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterCostPumping, "masterCostPumping", { { { "level", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterCostK2Deferred, "masterCostK2Deferred", {}, 0 },
    { FactId::MasterCostUnavailable, "masterCostUnavailable", {}, 0 },
    { FactId::PlanWaiting, "planWaiting",
      { { { "device", ArgKind::Term, "device" }, { "analyzer", ArgKind::Term, "analyzer" }, { "progress", ArgKind::Value, {} } } }, 3 },
    { FactId::HpfNote, "hpfNote",
      { { { "cutoff", ArgKind::Value, {} }, { "note", ArgKind::Midi, {} }, { "hz", ArgKind::Value, {} }, { "loss", ArgKind::Value, {} } } }, 4 },
    { FactId::HpfBelowFloor, "hpfBelowFloor",
      { { { "cutoff", ArgKind::Value, {} }, { "note", ArgKind::Midi, {} }, { "hz", ArgKind::Value, {} }, { "loss", ArgKind::Value, {} } } }, 4 },
    { FactId::HpfTop, "hpfTop", { { { "cutoff", ArgKind::Value, {} }, { "note", ArgKind::Midi, {} }, { "hz", ArgKind::Value, {} } } }, 3 },
    { FactId::HpfUnsure, "hpfUnsure", { { { "cutoff", ArgKind::Value, {} } } }, 1 },
    { FactId::HpfShort, "hpfShort", { { { "cutoff", ArgKind::Value, {} }, { "seconds", ArgKind::Value, {} } } }, 2 },
    { FactId::HpfQuiet, "hpfQuiet", { { { "cutoff", ArgKind::Value, {} } } }, 1 },
    { FactId::HpfUnmeasured, "hpfUnmeasured", { { { "cutoff", ArgKind::Value, {} } } }, 1 },
    { FactId::MonoBassPartial, "monoBassPartial", { { { "loss", ArgKind::Value, {} } } }, 1 },
    { FactId::MonoBassAntiPhase, "monoBassAntiPhase", { { { "loss", ArgKind::Value, {} } } }, 1 },
    { FactId::MonoBassUnmeasured, "monoBassUnmeasured", {}, 0 },
    { FactId::HpfFloor, "hpfFloor",
      { { { "cutoff", ArgKind::Value, {} }, { "note", ArgKind::Midi, {} }, { "hz", ArgKind::Value, {} }, { "loss", ArgKind::Value, {} } } }, 4 },
    { FactId::GlueUnavailable, "glueUnavailable", { { { "upTo", ArgKind::Value, {} } } }, 1 },
    { FactId::GlueTempoFallback, "glueTempoFallback", { { { "bpm", ArgKind::Value, {} } } }, 1 },
    { FactId::GlueReleaseHeld, "glueReleaseHeld", { { { "release", ArgKind::Value, {} }, { "asked", ArgKind::Value, {} } } }, 2 },
    { FactId::MasterGlue, "masterGlue", { { { "usual", ArgKind::Value, {} }, { "largest", ArgKind::Value, {} } } }, 2 },
    { FactId::MasterSaturation, "masterSaturation", { { { "largest", ArgKind::Value, {} }, { "usual", ArgKind::Value, {} } } }, 2 },
    { FactId::HpfByHand, "hpfByHand", { { { "cutoff", ArgKind::Value, {} } } }, 1 },
    { FactId::HpfKept, "hpfKept", { { { "cutoff", ArgKind::Value, {} } } }, 1 },
    { FactId::HpfOff, "hpfOff", {}, 0 },
    { FactId::MonoBassByHand, "monoBassByHand", { { { "crossover", ArgKind::Value, {} } } }, 1 },
    { FactId::MonoBassKept, "monoBassKept", { { { "crossover", ArgKind::Value, {} } } }, 1 },
    { FactId::MonoBassPartWeighed, "monoBassPartWeighed", { { { "covered", ArgKind::Value, {} }, { "whole", ArgKind::Value, {} } } }, 2 },
    { FactId::LimiterShort, "limiterShort", { { { "ceiling", ArgKind::Value, {} }, { "cut", ArgKind::Value, {} }, { "p90", ArgKind::Value, {} }, { "bass", ArgKind::Value, {} }, { "plr", ArgKind::Value, {} } } }, 5 },
    { FactId::LimiterBetween, "limiterBetween", { { { "ceiling", ArgKind::Value, {} }, { "cut", ArgKind::Value, {} }, { "p90", ArgKind::Value, {} }, { "bass", ArgKind::Value, {} }, { "plr", ArgKind::Value, {} } } }, 5 },
    { FactId::LimiterManual, "limiterManual", { { { "ceiling", ArgKind::Value, {} }, { "cut", ArgKind::Value, {} } } }, 2 },
    { FactId::LimiterNeedlesOff, "limiterNeedlesOff", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::LimiterLittleNeed, "limiterLittleNeed", { { { "ceiling", ArgKind::Value, {} }, { "need", ArgKind::Value, {} }, { "little", ArgKind::Value, {} } } }, 3 },
    { FactId::LimiterNoExcursions, "limiterNoExcursions", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::LimiterUnmeasured, "limiterUnmeasured", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::LimiterClipped, "limiterClipped", { { { "ceiling", ArgKind::Value, {} }, { "rate", ArgKind::Value, {} } } }, 2 },
    { FactId::LimiterLowPlr, "limiterLowPlr", { { { "ceiling", ArgKind::Value, {} }, { "plr", ArgKind::Value, {} }, { "bound", ArgKind::Value, {} } } }, 3 },
    { FactId::LimiterBass, "limiterBass", { { { "ceiling", ArgKind::Value, {} }, { "bass", ArgKind::Value, {} } } }, 2 },
    { FactId::LimiterLong, "limiterLong", { { { "ceiling", ArgKind::Value, {} }, { "p90", ArgKind::Value, {} } } }, 2 },
    { FactId::LimiterNoClipper, "limiterNoClipper", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::LimiterQuiet, "limiterQuiet", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::LimiterPending, "limiterPending", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::LimiterPlain, "limiterPlain", { { { "ceiling", ArgKind::Value, {} } } }, 1 },
    { FactId::NeedlesAgainstMachine, "needlesAgainstMachine", { { { "why", ArgKind::Term, "needlesWhy" } } }, 1 },
    { FactId::VinylCeiling, "vinylCeiling", { { { "ceiling", ArgKind::Value, {} }, { "medium", ArgKind::Value, {} } } }, 2 },
    { FactId::VinylNeedles, "vinylNeedles", {}, 0 },
    { FactId::VinylTop, "vinylTop", { { { "hz", ArgKind::Value, {} } } }, 1 },
    { FactId::DitherOn, "ditherOn", { { { "bits", ArgKind::Count, {} } } }, 1 },
    { FactId::DitherOffByHand, "ditherOffByHand", { { { "bits", ArgKind::Count, {} } } }, 1 },
    { FactId::DitherOff, "ditherOff", { { { "bits", ArgKind::Count, {} } } }, 1 },
    { FactId::DitherNotApplied, "ditherNotApplied", { { { "bits", ArgKind::Count, {} }, { "upTo", ArgKind::Count, {} } } }, 2 },
    { FactId::DitherKept, "ditherKept", { { { "bits", ArgKind::Count, {} }, { "upTo", ArgKind::Count, {} } } }, 2 },
    { FactId::MasterVinylReady, "masterVinylReady", {}, 0 },
    { FactId::MasterVinylDeparts, "masterVinylDeparts", {}, 0 },
    { FactId::MasterVinylChecked, "masterVinylChecked", { { { "crossover", ArgKind::Value, {} }, { "cutoff", ArgKind::Value, {} }, { "peak", ArgKind::Value, {} }, { "ceiling", ArgKind::Value, {} } } }, 4 },
    { FactId::MasterVinylUncheckable, "masterVinylUncheckable", {}, 0 },
    { FactId::MasterQuietInput, "masterQuietInput", { { { "lufs", ArgKind::Value, {} }, { "gain", ArgKind::Value, {} } } }, 2 },
    { FactId::TargetChangeResetsEdits, "targetChangeResetsEdits", { { { "count", ArgKind::Count, {} } } }, 1 },
    { FactId::MasterVinylNoFold, "masterVinylNoFold", { { { "medium", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterVinylFoldDeparts, "masterVinylFoldDeparts", { { { "crossover", ArgKind::Value, {} }, { "width", ArgKind::Value, {} }, { "medium", ArgKind::Value, {} }, { "mediumWidth", ArgKind::Value, {} } } }, 4 },
    { FactId::MasterVinylNoHighPass, "masterVinylNoHighPass", { { { "medium", ArgKind::Value, {} }, { "mediumSlope", ArgKind::Count, {} } } }, 2 },
    { FactId::MasterVinylHighPassDeparts, "masterVinylHighPassDeparts", { { { "cutoff", ArgKind::Value, {} }, { "slope", ArgKind::Count, {} }, { "medium", ArgKind::Value, {} }, { "mediumSlope", ArgKind::Count, {} } } }, 4 },
    { FactId::MasterVinylCeilingDeparts, "masterVinylCeilingDeparts", { { { "ceiling", ArgKind::Value, {} }, { "medium", ArgKind::Value, {} } } }, 2 },
    { FactId::MasterVinylNeedlesDeparts, "masterVinylNeedlesDeparts", { { { "cut", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterLandingSolved, "masterLandingSolved", { { { "achieved", ArgKind::Value, {} }, { "target", ArgKind::Value, {} }, { "tolerance", ArgKind::Value, {} } } }, 3 },
    { FactId::MasterLandingUnreachable, "masterLandingUnreachable", { { { "tolerance", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterLandingPassLimit, "masterLandingPassLimit", { { { "tolerance", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterLandingBetween, "masterLandingBetween", { { { "tolerance", ArgKind::Value, {} } } }, 1 },
    { FactId::MasterLandingFailed, "masterLandingFailed", {}, 0 },
    { FactId::MasterCostSection, "masterCostSection", { { { "shift", ArgKind::Value, {} }, { "from", ArgKind::Value, {} }, { "to", ArgKind::Value, {} } } }, 3 },
    { FactId::MasterCostSections, "masterCostSections", { { { "count", ArgKind::Count, {} } } }, 1 },
    { FactId::MasterCostLimiter, "masterCostLimiter", { { { "median", ArgKind::Value, {} }, { "p95", ArgKind::Value, {} } } }, 2 },
    { FactId::MasterCostActive, "masterCostActive", { { { "limiter", ArgKind::Value, {} }, { "active", ArgKind::Value, {} } } }, 2 },
    { FactId::MasterCostBands, "masterCostBands", { { { "low", ArgKind::Value, {} }, { "lowMid", ArgKind::Value, {} }, { "highMid", ArgKind::Value, {} }, { "high", ArgKind::Value, {} } } }, 4 },
    // A command's rejection (Commands.h), one per code; the four a field refuses name it.
    { FactId::RejectedFloatingPointEnvironment, "rejectedFloatingPointEnvironment", {}, 0 },
    { FactId::RejectedNoSource, "rejectedNoSource", {}, 0 },
    { FactId::RejectedNotPlaced, "rejectedNotPlaced", {}, 0 },
    { FactId::RejectedNotMeasured, "rejectedNotMeasured", {}, 0 },
    { FactId::RejectedBusy, "rejectedBusy", {}, 0 },
    { FactId::RejectedNoJob, "rejectedNoJob", {}, 0 },
    { FactId::RejectedNoMaster, "rejectedNoMaster", {}, 0 },
    { FactId::RejectedUnknownTarget, "rejectedUnknownTarget", {}, 0 },
    { FactId::RejectedNotOffered, "rejectedNotOffered", {}, 0 },
    { FactId::RejectedUnknownJob, "rejectedUnknownJob", {}, 0 },
    { FactId::RejectedUnknownMaster, "rejectedUnknownMaster", {}, 0 },
    { FactId::RejectedNotFinite, "rejectedNotFinite", { { { "field", ArgKind::Term, "field" } } }, 1 },
    { FactId::RejectedNotOneOf, "rejectedNotOneOf", { { { "field", ArgKind::Term, "field" } } }, 1 },
    { FactId::RejectedOutOfDomain, "rejectedOutOfDomain", { { { "field", ArgKind::Term, "field" } } }, 1 },
    { FactId::RejectedBadChannels, "rejectedBadChannels", {}, 0 },
    { FactId::RejectedBadRate, "rejectedBadRate", {}, 0 },
    { FactId::RejectedNoAudio, "rejectedNoAudio", {}, 0 },
    { FactId::RejectedTooLong, "rejectedTooLong", {}, 0 },
    { FactId::RejectedNoJobId, "rejectedNoJobId", {}, 0 },
    { FactId::RejectedInvalidUtf8, "rejectedInvalidUtf8", {}, 0 },
    { FactId::RejectedProjectTooLarge, "rejectedProjectTooLarge", {}, 0 },
    { FactId::RejectedProjectSyntax, "rejectedProjectSyntax", {}, 0 },
    { FactId::RejectedProjectMissing, "rejectedProjectMissing", {}, 0 },
    { FactId::RejectedProjectType, "rejectedProjectType", {}, 0 },
    { FactId::RejectedProjectUnknownKey, "rejectedProjectUnknownKey", {}, 0 },
    { FactId::RejectedUnknownDefaults, "rejectedUnknownDefaults", {}, 0 },
    { FactId::RejectedProjectCore, "rejectedProjectCore", {}, 0 },
    { FactId::RejectedNewerDefaults, "rejectedNewerDefaults", {}, 0 },
    { FactId::RejectedRateAboveLimit, "rejectedRateAboveLimit", {}, 0 },
    { FactId::RejectedMemory, "rejectedMemory", {}, 0 },
    { FactId::RejectedContract, "rejectedContract", {}, 0 },
    { FactId::RejectedOutputPending, "rejectedOutputPending", {}, 0 },
    { FactId::RejectedMandatoryUnavailable, "rejectedMandatoryUnavailable", {}, 0 },
    { FactId::RejectedDeliveryFormat, "rejectedDeliveryFormat",
      { { { "bits", ArgKind::Count, {} }, { "rate", ArgKind::Value, {} } } }, 2 },
    { FactId::RejectedPlanPending, "rejectedPlanPending", {}, 0 },
    { FactId::Measurement1, "measurement1", {}, 0 },
    { FactId::Measurement2, "measurement2", {}, 0 },
    { FactId::MasterPass, "masterPass", { { { "pass", ArgKind::Count, {} } } }, 1 },
    { FactId::MasterReady, "masterReady", {}, 0 },
    { FactId::Cancelled, "cancelled", {}, 0 },
    { FactId::Convert, "convert", {}, 0 },
    { FactId::Lra, "lra", {}, 0 },
    { FactId::Final, "final", {}, 0 },
    { FactId::SessionTrap, "sessionTrap", {}, 0 },
    { FactId::SessionContract, "sessionContract", {}, 0 },
    { FactId::SessionRefusal, "sessionRefusal", {}, 0 },
    { FactId::SessionMemory, "sessionMemory", {}, 0 },
    { FactId::SessionPoisoned, "sessionPoisoned", {}, 0 },
    { FactId::SessionStale, "sessionStale", {}, 0 },
    { FactId::MeasurementPending, "measurementPending", {}, 0 },
    { FactId::MeasurementReady, "measurementReady", {}, 0 },
    { FactId::MeasurementStopped, "measurementStopped", {}, 0 },
    { FactId::MeasurementUnsupported, "measurementUnsupported", {}, 0 },
    { FactId::MeasurementTooShort, "measurementTooShort", {}, 0 },
    { FactId::MeasurementNonFinite, "measurementNonFinite", {}, 0 },
    { FactId::MeasurementCapacity, "measurementCapacity", {}, 0 },
    { FactId::MeasurementNoSignal, "measurementNoSignal", {}, 0 },
    { FactId::MeasurementUnavailable, "measurementUnavailable", {}, 0 },
    { FactId::NeedlesSkipped, "needlesSkipped", {}, 0 },
    { FactId::NeedlesRunsTruncated, "needlesRunsTruncated", {}, 0 },
    { FactId::SourceClipping, "sourceClipping", {}, 0 },
    { FactId::ContinueMeasurement, "continueMeasurement", {}, 0 },
    { FactId::AnalyzerStatus, "analyzerStatus", { { { "analyzer", ArgKind::Term, "analyzer" }, { "status", ArgKind::Term, "measurementStatus" } } }, 2 },
    { FactId::SourceQuiet, "sourceQuiet", { { { "lufs", ArgKind::Value, {} } } }, 1 },
    { FactId::SourceGainOnly, "sourceGainOnly", { { { "lufs", ArgKind::Value, {} } } }, 1 },
    { FactId::SourceDc, "sourceDc", { { { "offset", ArgKind::Value, {} } } }, 1 },
    { FactId::SourceUnusedBits, "sourceUnusedBits", { { { "bits", ArgKind::Count, {} } } }, 1 },
    { FactId::SourcePolarity, "sourcePolarity", {}, 0 },
    { FactId::SourceClips, "sourceClips", { { { "count", ArgKind::Count, {} } } }, 1 },
    { FactId::SourceClipsAt1, "sourceClipsAt1", { { { "count", ArgKind::Count, {} }, { "first", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceClipsAt2, "sourceClipsAt2", { { { "count", ArgKind::Count, {} }, { "first", ArgKind::Value, {} }, { "second", ArgKind::Value, {} } } }, 3 },
    { FactId::SourceClipsAt3, "sourceClipsAt3", { { { "count", ArgKind::Count, {} }, { "first", ArgKind::Value, {} }, { "second", ArgKind::Value, {} }, { "third", ArgKind::Value, {} } } }, 4 },
    { FactId::SourceClipped, "sourceClipped", { { { "count", ArgKind::Count, {} }, { "rate", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceDualMono, "sourceDualMono", {}, 0 },
    { FactId::SourceEdgeSilence, "sourceEdgeSilence", { { { "leading", ArgKind::Value, {} }, { "trailing", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceShort, "sourceShort", { { { "seconds", ArgKind::Value, {} }, { "limit", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceLimited, "sourceLimited", { { { "plr", ArgKind::Value, {} }, { "bound", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceLimitedClipped, "sourceLimitedClipped", { { { "plr", ArgKind::Value, {} } } }, 1 },
    { FactId::SourceWall, "sourceWall", { { { "cutoff", ArgKind::Value, {} }, { "drop", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceLowestBand, "sourceLowestBand", { { { "note", ArgKind::Midi, {} }, { "hz", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceInfraLow, "sourceInfraLow", { { { "share", ArgKind::Value, {} }, { "hz", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceSibilance, "sourceSibilance", { { { "excess", ArgKind::Value, {} }, { "rate", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceSibilanceAt, "sourceSibilanceAt", { { { "excess", ArgKind::Value, {} }, { "rate", ArgKind::Value, {} }, { "first", ArgKind::Value, {} }, { "second", ArgKind::Value, {} }, { "third", ArgKind::Value, {} } } }, 5 },
    { FactId::SourceHum, "sourceHum", { { { "hz", ArgKind::Value, {} }, { "prominence", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceHumWandered, "sourceHumWandered", { { { "hz", ArgKind::Value, {} }, { "prominence", ArgKind::Value, {} } } }, 2 },
    { FactId::SourceLowestBandUnsure, "sourceLowestBandUnsure", { { { "note", ArgKind::Midi, {} }, { "hz", ArgKind::Value, {} } } }, 2 },
    { FactId::ObservationUnmeasured, "observationUnmeasured", { { { "name", ArgKind::Term, "observation" }, { "reason", ArgKind::Term, "measurementReason" } } }, 2 },
    { FactId::TempoConfidence, "tempoConfidence", { { { "confidence", ArgKind::Term, "tempoConfidence" } } }, 1 },

};
inline constexpr std::size_t kFactCount = sizeof (kFacts) / sizeof (kFacts[0]);

struct TermShape
{
    Term id {};
    std::string_view group;           // terms.<group>.<key> in the catalog
    std::string_view key;
};

inline constexpr TermShape kTerms[] = {
    { Term::PlatformWeb, "platform", "web" },
    { Term::PlatformDesktop, "platform", "desktop" },
    { Term::FieldTargetLufs, "field", "targetLufs" },
    { Term::FieldTargetTp, "field", "targetTp" },
    { Term::FieldHpfFq, "field", "hpfFq" },
    { Term::FieldHpfSlope, "field", "hpfSlope" },
    { Term::FieldMonoBassFq, "field", "monoBassFq" },
    { Term::FieldMonoBassWidth, "field", "monoBassWidth" },
    { Term::FieldGlueUpToDb, "field", "glueUpToDb" },
    { Term::FieldSaturationDrive, "field", "saturationDrive" },
    { Term::FieldSaturationMix, "field", "saturationMix" },
    { Term::FieldSaturationOutput, "field", "saturationOutput" },
    { Term::FieldTiltDb, "field", "tiltDb" },
    { Term::FieldLimiterNeedles, "field", "limiterNeedles" },
    { Term::FieldLimiterNeedlesDb, "field", "limiterNeedlesDb" },
    { Term::FieldLowDb, "field", "lowDb" },
    { Term::FieldAudio, "field", "audio" },
    { Term::AnalyzerLowEnd, "analyzer", "lowEnd" },
    { Term::AnalyzerLowEnd150, "analyzer", "lowEnd150" },
    { Term::AnalyzerInfraLow, "analyzer", "infraLow" },
    { Term::AnalyzerForensics, "analyzer", "forensics" },
    { Term::AnalyzerStereo, "analyzer", "stereo" },
    { Term::AnalyzerCrest, "analyzer", "crest" },
    { Term::AnalyzerHum, "analyzer", "hum" },
    { Term::AnalyzerBursts, "analyzer", "bursts" },
    { Term::StatusReady, "measurementStatus", "ready" },
    { Term::StatusUnsupported, "measurementStatus", "unsupported" },
    { Term::StatusShort, "measurementStatus", "short" },
    { Term::StatusNonFinite, "measurementStatus", "nonFinite" },
    { Term::StatusCapacity, "measurementStatus", "capacity" },
    { Term::StatusNoSignal, "measurementStatus", "noSignal" },
    { Term::StatusMemory, "measurementStatus", "memory" },

    { Term::AnalyzerWaveform, "analyzer", "waveform" },
    { Term::AnalyzerTempo, "analyzer", "tempo" },
    { Term::AnalyzerNeedles, "analyzer", "needles" },
    { Term::DeviceHpf, "device", "hpf" },
    { Term::DeviceMonoBass, "device", "monoBass" },
    { Term::DeviceGlue, "device", "glue" },
    { Term::DeviceSaturation, "device", "saturation" },
    { Term::DeviceTilt, "device", "tilt" },
    { Term::DeviceLimiter, "device", "limiter" },
    { Term::DeviceDither, "device", "dither" },
    { Term::DeviceLow, "device", "low" },
    { Term::NeedlesWhyShell, "needlesWhy", "shell" },
    { Term::NeedlesWhyTarget, "needlesWhy", "target" },
    { Term::NeedlesWhyQuiet, "needlesWhy", "quiet" },
    { Term::NeedlesWhyNoReadings, "needlesWhy", "noReadings" },
    { Term::NeedlesWhyLittleNeed, "needlesWhy", "littleNeed" },
    { Term::NeedlesWhyUnmeasured, "needlesWhy", "unmeasured" },
    { Term::NeedlesWhyNoExcursions, "needlesWhy", "noExcursions" },
    { Term::NeedlesWhyClipped, "needlesWhy", "clipped" },
    { Term::NeedlesWhyLowPlr, "needlesWhy", "lowPlr" },
    { Term::NeedlesWhyBass, "needlesWhy", "bass" },
    { Term::NeedlesWhyLong, "needlesWhy", "long" },
    { Term::FieldSaturationType, "field", "saturationType" },
    { Term::SaturationTypeTanh, "saturationType", "tanh" },
    { Term::SaturationTypeTube, "saturationType", "tube" },
    { Term::SaturationTypeTransistor, "saturationType", "transistor" },
    { Term::SaturationTypeTransformer, "saturationType", "transformer" },
    { Term::SaturationTypeTape, "saturationType", "tape" },
    { Term::ObservationClipping, "observation", "clipping" },
    { Term::ObservationDcOffset, "observation", "dcOffset" },
    { Term::ObservationBitsUnused, "observation", "bitsUnused" },
    { Term::ObservationDualMono, "observation", "dualMono" },
    { Term::ObservationEdgeSilence, "observation", "edgeSilence" },
    { Term::ObservationTooQuiet, "observation", "tooQuiet" },
    { Term::ObservationTooShort, "observation", "tooShort" },
    { Term::ObservationAlreadyLimited, "observation", "alreadyLimited" },
    { Term::ObservationSpectralWall, "observation", "spectralWall" },
    { Term::ObservationLoudestLowNote, "observation", "loudestLowNote" },
    { Term::ObservationLowestLowBand, "observation", "lowestLowBand" },
    { Term::ObservationInfraLow, "observation", "infraLow" },
    { Term::ObservationWideBass, "observation", "wideBass" },
    { Term::ObservationPolarity, "observation", "polarity" },
    { Term::ObservationSibilance, "observation", "sibilance" },
    { Term::ObservationHum, "observation", "hum" },
    { Term::ObservationHumWandered, "observation", "humWandered" },
    { Term::HandledNothing, "handledBy", "nothing" },
    { Term::HandledHpf, "handledBy", "hpf" },
    { Term::HandledMonoBass, "handledBy", "monoBass" },
    { Term::HandledPerson, "handledBy", "person" },
    { Term::ReasonPending, "measurementReason", "pending" },
    { Term::ReasonCancelled, "measurementReason", "cancelled" },
    { Term::ReasonUnsupported, "measurementReason", "unsupported" },
    { Term::ReasonTooShort, "measurementReason", "tooShort" },
    { Term::ReasonNonFinite, "measurementReason", "nonFinite" },
    { Term::ReasonCapacity, "measurementReason", "capacity" },
    { Term::ReasonNoSignal, "measurementReason", "noSignal" },
    { Term::ReasonNotImplemented, "measurementReason", "notImplemented" },
    { Term::ReasonNeedNotAbove3, "measurementReason", "needNotAbove3" },
    { Term::ReasonMemory, "measurementReason", "memory" },
    { Term::ReadingIntegrated, "reading", "integrated" },
    { Term::ReadingTruePeak, "reading", "truePeak" },
    { Term::ReadingLra, "reading", "lra" },
    { Term::ReadingPlr, "reading", "plr" },
    { Term::ReadingDcOffset, "reading", "dcOffset" },
    { Term::ReadingDcOffsetLeft, "reading", "dcOffsetLeft" },
    { Term::ReadingDcOffsetRight, "reading", "dcOffsetRight" },
    { Term::ReadingLowestBand, "reading", "lowestBand" },
    { Term::ReadingPcmBits, "reading", "pcmBits" },
    { Term::ReadingCorrelation, "reading", "correlation" },
    { Term::ReadingBurstsMid, "reading", "burstsMid" },
    { Term::ReadingBurstsSide, "reading", "burstsSide" },
    { Term::ReadingHum, "reading", "hum" },
    { Term::ReadingTempo, "reading", "tempo" },
    { Term::ReadingTempoConfidence, "reading", "tempoConfidence" },
    { Term::ReadingClipRuns, "reading", "clipRuns" },
    { Term::ReadingLongestRun, "reading", "longestRun" },
    { Term::ReadingClippedSamples, "reading", "clippedSamples" },
    { Term::ReadingSamplePeak, "reading", "samplePeak" },
    { Term::ReadingLowSide, "reading", "lowSide" },
    { Term::ReadingStereoWindows, "reading", "stereoWindows" },
    { Term::ReadingCrestBlocksLow, "reading", "crestBlocksLow" },
    { Term::ReadingCrestBlocksLowMid, "reading", "crestBlocksLowMid" },
    { Term::ReadingCrestBlocksHighMid, "reading", "crestBlocksHighMid" },
    { Term::ReadingCrestBlocksHigh, "reading", "crestBlocksHigh" },
    { Term::ReadingCrestBlocksFull, "reading", "crestBlocksFull" },
    { Term::ReadingTarget, "reading", "target" },
    { Term::ReadingCeiling, "reading", "ceiling" },
    { Term::ReadingGain, "reading", "gain" },
    { Term::ReadingPasses, "reading", "passes" },
    { Term::ReadingCheckPasses, "reading", "checkPasses" },
    { Term::TempoUndetermined, "tempoConfidence", "undetermined" },
    { Term::TempoLow, "tempoConfidence", "low" },
    { Term::TempoMedium, "tempoConfidence", "medium" },
    { Term::TempoHigh, "tempoConfidence", "high" },
};
inline constexpr std::size_t kTermCount = sizeof (kTerms) / sizeof (kTerms[0]);

// The position of the argument a placeholder names, or −1 for a name the fact does not have.
[[nodiscard]] constexpr int argIndex (const FactShape& shape, std::string_view name) noexcept
{
    for (std::size_t i = 0; i < shape.argCount; ++i)
        if (shape.args[i].name == name) return (int) i;
    return -1;
}

// The facts in ascending id order (ids fall in ranges, Text.h, so they have gaps); the terms' ids are their positions
// plus one, in order, with no gap.
[[nodiscard]] constexpr bool tablesInOrder() noexcept
{
    for (std::size_t i = 0; i < kFactCount; ++i)
        if ((i > 0 && (std::size_t) kFacts[i].id <= (std::size_t) kFacts[i - 1].id) || kFacts[i].argCount > Fact::kMaxArgs)
            return false;
    for (std::size_t i = 0; i < kTermCount; ++i)
        if ((std::size_t) kTerms[i].id != i + 1) return false;
    return true;
}
static_assert (tablesInOrder(), "kFacts ascends by id; kTerms lists every id in order, from 1, with no gap");

// The shape of a fact, or null for an id the table does not have: a binary search over the ascending ids.
[[nodiscard]] constexpr const FactShape* shapeOf (FactId id) noexcept
{
    std::size_t low = 0, high = kFactCount;
    while (low < high)
    {
        const std::size_t middle = low + (high - low) / 2;
        if (kFacts[middle].id == id) return &kFacts[middle];
        if ((std::size_t) kFacts[middle].id < (std::size_t) id) low = middle + 1;
        else high = middle;
    }
    return nullptr;
}
[[nodiscard]] constexpr const TermShape* shapeOf (Term id) noexcept
{
    const auto i = (std::size_t) id;
    return i >= 1 && i <= kTermCount ? &kTerms[i - 1] : nullptr;
}

//==============================================================================
// A COMMAND'S REJECTION AS A FACT — 100 + its code. A switch over every Rejection, with no default: a code the state
// machine adds and nobody maps here is a warning this repository's builds make an error (-Wswitch-enum, -Werror), and
// felitronics_session_text_tests holds the table code by code.
[[nodiscard]] constexpr std::optional<FactId> factOf (Rejection r) noexcept
{
    switch (r)
    {
        case Rejection::None: return std::nullopt;
        case Rejection::FloatingPointEnvironment: return FactId::RejectedFloatingPointEnvironment;
        case Rejection::NoSource: return FactId::RejectedNoSource;
        case Rejection::NotPlaced: return FactId::RejectedNotPlaced;
        case Rejection::NotMeasured: return FactId::RejectedNotMeasured;
        case Rejection::Busy: return FactId::RejectedBusy;
        case Rejection::NoJob: return FactId::RejectedNoJob;
        case Rejection::NoMaster: return FactId::RejectedNoMaster;
        case Rejection::UnknownTarget: return FactId::RejectedUnknownTarget;
        case Rejection::NotOffered: return FactId::RejectedNotOffered;
        case Rejection::UnknownJob: return FactId::RejectedUnknownJob;
        case Rejection::UnknownMaster: return FactId::RejectedUnknownMaster;
        case Rejection::NotFinite: return FactId::RejectedNotFinite;
        case Rejection::NotOneOf: return FactId::RejectedNotOneOf;
        case Rejection::OutOfDomain: return FactId::RejectedOutOfDomain;
        case Rejection::BadChannels: return FactId::RejectedBadChannels;
        case Rejection::BadRate: return FactId::RejectedBadRate;
        case Rejection::NoAudio: return FactId::RejectedNoAudio;
        case Rejection::TooLong: return FactId::RejectedTooLong;
        case Rejection::NoJobId: return FactId::RejectedNoJobId;
        case Rejection::InvalidUtf8: return FactId::RejectedInvalidUtf8;
        case Rejection::ProjectTooLarge: return FactId::RejectedProjectTooLarge;
        case Rejection::ProjectSyntax: return FactId::RejectedProjectSyntax;
        case Rejection::ProjectMissing: return FactId::RejectedProjectMissing;
        case Rejection::ProjectType: return FactId::RejectedProjectType;
        case Rejection::ProjectUnknownKey: return FactId::RejectedProjectUnknownKey;
        case Rejection::UnknownDefaults: return FactId::RejectedUnknownDefaults;
        case Rejection::ProjectCore: return FactId::RejectedProjectCore;
        case Rejection::RateAboveLimit: return FactId::RejectedRateAboveLimit;
        case Rejection::Contract: return FactId::RejectedContract;
        case Rejection::OutputPending: return FactId::RejectedOutputPending;
        case Rejection::MandatoryUnavailable: return FactId::RejectedMandatoryUnavailable;
        case Rejection::DeliveryFormat: return FactId::RejectedDeliveryFormat;
        case Rejection::PlanPending: return FactId::RejectedPlanPending;
        case Rejection::Memory: return FactId::RejectedMemory;
        case Rejection::NewerDefaults: return FactId::RejectedNewerDefaults;
    }
    return std::nullopt;
}

// A FIELD A CHECK REFUSED, as its term: the target's number at `field` (Answer::field: lufs 0, tp 1), or a device's field
// at `field` in the order Project.h writes it (src/Devices.h walks the same order). A tick is never refused and has
// none; nor has a position past the device's fields. The suite holds this against src/Devices.h: a term for exactly the
// fields a check can refuse.
[[nodiscard]] constexpr std::optional<Term> targetFieldTerm (std::uint8_t field) noexcept
{
    return field == 0 ? std::optional<Term> (Term::FieldTargetLufs)
         : field == 1 ? std::optional<Term> (Term::FieldTargetTp) : std::nullopt;
}

[[nodiscard]] constexpr std::optional<Term> deviceFieldTerm (Device device, std::uint8_t field) noexcept
{
    const auto at = [field] (std::initializer_list<Term> byPosition) -> std::optional<Term>
    {
        if (field >= byPosition.size()) return std::nullopt;
        const Term t = byPosition.begin()[field];
        return t == Term {} ? std::nullopt : std::optional<Term> (t);
    };
    switch (device)
    {
        case Device::Hpf: return at ({ Term {}, Term::FieldHpfFq, Term::FieldHpfSlope });
        case Device::MonoBass: return at ({ Term {}, Term::FieldMonoBassFq, Term::FieldMonoBassWidth });
        case Device::Glue: return at ({ Term {}, Term::FieldGlueUpToDb });
        case Device::Saturation: return at ({ Term {}, Term::FieldSaturationDrive, Term::FieldSaturationMix,
                                              Term::FieldSaturationOutput, Term::FieldSaturationType });
        case Device::Tilt: return at ({ Term {}, Term::FieldTiltDb });
        case Device::Limiter: return at ({ Term::FieldLimiterNeedles, Term::FieldLimiterNeedlesDb });
        case Device::Dither: return at ({ Term {} });
        case Device::Low: return at ({ Term {}, Term::FieldLowDb });
    }
    return std::nullopt;
}

//==============================================================================
// Languages and units, by their keys in the documents.

inline constexpr std::array<std::string_view, kLangCount> kLangCodes = {
    "en", "de", "ru", "uk", "cs", "es", "fr", "it", "pl", "pt", "ro", "tr" };

// The language of a code, or nullopt for one that is not one of the twelve.
[[nodiscard]] constexpr std::optional<Lang> langOfCode (std::string_view code) noexcept
{
    for (std::size_t i = 0; i < kLangCount; ++i)
        if (kLangCodes[i] == code) return (Lang) i;
    return std::nullopt;
}

// Unit::None has no key: the number alone has no pattern.
inline constexpr std::array<std::string_view, kUnitCount> kUnitKeys = {
    "", "percent", "db", "dbtp", "dbfs", "lufs", "lu", "hz", "khz", "ms", "s", "bpm" };

//==============================================================================
// CLDR's plural categories: their keys in a plural message, and each language's set (CLDR 48 cardinal rules, as ICU 78
// states them). A set is a bit per category.

inline constexpr std::array<std::string_view, 6> kPluralKeys = { "zero", "one", "two", "few", "many", "other" };

[[nodiscard]] constexpr std::uint8_t bit (Plural p) noexcept { return (std::uint8_t) (1u << (unsigned) p); }

[[nodiscard]] constexpr std::uint8_t pluralSet (Lang lang) noexcept
{
    const std::uint8_t oneOther = bit (Plural::One) | bit (Plural::Other);
    switch (lang)
    {
        case Lang::En: case Lang::De: case Lang::Tr:
            return oneOther;
        case Lang::Es: case Lang::Fr: case Lang::It: case Lang::Pt:
            return oneOther | bit (Plural::Many);
        case Lang::Ro:
            return oneOther | bit (Plural::Few);
        case Lang::Ru: case Lang::Uk: case Lang::Pl: case Lang::Cs:
            return oneOther | bit (Plural::Few) | bit (Plural::Many);
    }
    return bit (Plural::Other);
}

//==============================================================================
// THE PLACEHOLDER GRAMMAR, one scanner for the renderer and the gate. A message is text and placeholders; `{name}` is a
// placeholder when the name is an ASCII letter followed by ASCII letters and digits. Any other brace is malformed.

struct Piece
{
    enum class Kind : std::uint8_t { Text, Placeholder, Malformed } kind = Kind::Text;
    std::string_view text;            // Text: the bytes; Placeholder: the name; Malformed: from the brace on
    std::size_t offset = 0;           // where it starts in the message, in bytes
};

[[nodiscard]] constexpr bool nameStart (char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] constexpr bool nameChar (char c) noexcept { return nameStart (c) || (c >= '0' && c <= '9'); }

// The piece of `message` that starts at `at`; a message is walked by calling this until `at` reaches its size.
[[nodiscard]] constexpr Piece pieceAt (std::string_view message, std::size_t at) noexcept
{
    if (at >= message.size()) return { Piece::Kind::Text, {}, at };
    const char c = message[at];
    if (c == '}') return { Piece::Kind::Malformed, message.substr (at), at };
    if (c != '{')
    {
        std::size_t end = at;
        while (end < message.size() && message[end] != '{' && message[end] != '}') ++end;
        return { Piece::Kind::Text, message.substr (at, end - at), at };
    }
    std::size_t end = at + 1;
    if (end >= message.size() || ! nameStart (message[end])) return { Piece::Kind::Malformed, message.substr (at), at };
    while (end < message.size() && nameChar (message[end])) ++end;
    if (end >= message.size() || message[end] != '}') return { Piece::Kind::Malformed, message.substr (at), at };
    return { Piece::Kind::Placeholder, message.substr (at + 1, end - at - 1), at };
}

// How many bytes of the message the piece took (a malformed piece takes the rest: nothing after it is read).
[[nodiscard]] constexpr std::size_t length (const Piece& p) noexcept
{
    return p.kind == Piece::Kind::Placeholder ? p.text.size() + 2 : p.text.size();
}

} // namespace felitronics::session::text::detail
