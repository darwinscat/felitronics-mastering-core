// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Project.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace felitronics::analysis { struct BandCrestParams; }

namespace felitronics::session
{
enum class LandingStatus : std::uint8_t
{
    Solved, TargetUnreachable, PassLimit, TargetBetweenAchievable, Unavailable, Cancelled, TechnicalFailure
};
enum class LandingReason : std::uint8_t
{
    None, ExcessSubBass, SharpPeaks, DarkMix, LoudnessDemand, GainRange, TruePeak
};
// The limit that held an unreachable landing short of its target: the landing's solver names it where it decides the
// verdict (mastering::LoudnessSolution::binding). None for every other status, and where the solver named none.
enum class LandingConstraint : std::uint8_t
{
    None, TruePeakCeiling, LimiterGainReduction, PeakToLoudness, LoudnessRange, GainRange
};
struct LandingPass
{
    double gainDb = 0.0, ceilingDbTp = 0.0, achievedLufs = 0.0, truePeakDbTp = 0.0;
    double limiterMaxReductionDb = 0.0;
    bool ceilingSafe = false;
};
struct LandingTraceBucket
{
    double minDb = 0.0, maxDb = 0.0, meanDb = 0.0;
    std::uint64_t samples = 0, nonFinite = 0;
};
struct LandingTrace
{
    std::uint64_t fromFrame = 0, toFrame = 0, samples = 0, nonFinite = 0;
    std::uint32_t sampleRateHz = 0, columns = 0;
    bool complete = false, valid = false;
    std::span<const LandingTraceBucket> rows;
};
struct LandingSummary
{
    LandingStatus status = LandingStatus::Unavailable;
    LandingReason mainReason = LandingReason::None, secondReason = LandingReason::None;
    LandingConstraint binding = LandingConstraint::None;   // TargetUnreachable: the limit that held it
    // TargetBetweenAchievable: the two achievable levels the target fell between, the quieter first. Absent otherwise,
    // and absent when the solver gave a side that is not a number: the verdict and the delivered master stand.
    std::optional<double> belowLufs, aboveLufs;
    bool deliverable = false;
    std::optional<double> achievedLufs, missLu, distanceLu, truePeakDbTp;
    std::optional<double> sourceSubBassShare, sourcePresenceShare, limiterMeanReductionDb;
    std::uint32_t passes = 0;
    std::uint64_t workUnits = 0;
    std::span<const LandingPass> log;
    std::optional<LandingTrace> limiterTrace, peakClipTrace;
    // No render under the ceiling: the delivered master is the gentlest measured, its true peak above the ceiling
    // (deliverable, TargetUnreachable, binding TruePeakCeiling). False on every other landing.
    bool peaksAboveCeiling = false;
};

// The delivered meter and the source-rate crest check answer different questions. Rows are linear
// (peak amplitude, mean-square power) in Low, LowMid, HighMid, High, Full order. A mask value of one
// means that the corresponding source band was suitable for comparison.
struct MasterCrest
{
    MeasurementStatus status = MeasurementStatus::Pending;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint32_t version = 1, sampleRateHz = 0, hopFrames = 0, blockHops = 0;
    double edgeLowHz = 0, edgeMidHz = 0, edgeHighHz = 0;
    std::uint64_t frames = 0, blocks = 0;
    bool complete = false, sourceRateCheck = false;
    std::span<const double> rows, sourceMask;
};
struct MasterHint
{
    LandingReason reason = LandingReason::None;
    double evidence = 0;
    bool percent = false;
};
// A section covers source frames [fromFrame,toFrame). A shift is output short-term loudness
// minus source short-term loudness after removing the integrated loudness change.
struct MasterSection
{
    std::uint64_t fromFrame, toFrame;
    double sourceLufs, masterLufs, shiftLu;
    bool compared;
};
struct MasterCostValue
{
    MeasurementReason reason = MeasurementReason::NotImplemented;
    std::optional<double> value;
    std::uint64_t compared = 0;
};
struct MasterWaveformBucket
{
    std::uint64_t fromFrame, toFrame;
    std::uint32_t channel;
    double minimum, maximum, rms;
    std::uint64_t finite;
};
struct MasterCost
{
    // Positive crest loss means flatter attacks. The last band is full band.
    MasterCostValue crestLowDb {}, crestLowMidDb {}, crestHighMidDb {}, crestHighDb {}, crestFullDb {};
    MasterCostValue shapeP95Lu {}, largestSectionShiftLu {}, pumpingRmsDb {};
    MasterCostValue limiterP50Db {}, limiterP95Db {}, limiterActiveShare {}, activeWindowShare {};
    std::optional<std::uint32_t> worstSectionIndex;
    MeasurementReason k2Reason = MeasurementReason::NotImplemented;
    std::uint32_t sourceRateHz = 0, masterRateHz = 0;
    std::uint64_t sourceFrames = 0, masterFrames = 0;
    std::span<const MasterSection> sections;
    std::span<const MasterWaveformBucket> waveform;
    // WHAT THE GLUE AND THE SATURATION DID, each measured on its own stage (owner decisions 3.8, 3.9), dB, positive.
    // The glue: the compressor's gain reduction over the programme in 4 ms windows — its P95, the loud places, and its
    // largest sample. The saturation: how much less the peak of a loud place got than a quiet sound does — the peak
    // of the stage's input against the peak of its output, quantum by quantum, the mix and the output knob included;
    // the largest cut and the usual one, the median, over the loudest [saturation] cut.loudShare of the quanta. Never
    // the fall of the whole chain's true peak. A stage out of the chain has no number: NoSignal.
    MasterCostValue glueP95Db {}, glueMaxDb {}, saturationCutMaxDb {}, saturationCutUsualDb {};
};
// WHAT A MASTER'S MEDIUM AND ITS INPUT ADD TO ITS REPORT — for a master the session decided (version 0), from the chain
// it ran:
//   vinyl       the target is cut to vinyl (owner decision 3.12). `ready`: the medium's rules held in that chain — the
//               bass folded to mono from the target's crossover or above at the machine's width (or a mono source), the
//               high-pass in from the target's floor or above at its slope or steeper, the ceiling no higher than the
//               target's own, no needles cut. Then the master is ready for cutting: no cardinal corrections of mono
//               bass, infra-low or peaks; RIAA and the level for the side's length are the cutting room's, and what a
//               file cannot show (the side's length, sibilance at the cutter, distortion towards the centre) stays
//               unchecked. Not ready: the settings depart from those rules, a person's or the machine's.
//   quietInput  the input was too quiet to measure ([input] quiet.gainOnlyLufs): the machine placed no device but the
//               high-pass at the target's floor and the dither of the delivery's format.
struct MasterMedium
{
    bool vinyl = false, ready = false;
    double crossoverHz = 0.0;                  // the mono-bass crossover the chain ran; 0 where the fold was out
    double cutoffHz = 0.0;                     // the high-pass's cutoff the chain ran; 0 where it was out
    bool quietInput = false;
    double inputLufs = 0.0;                    // the input's loudness (meaningful where quietInput)
    // Where vinyl, each rule of the medium the chain departs from — any one takes the readiness away — with the
    // chain's number beside the rule's: the fold (the crossover and the width below it), the high-pass (its cutoff and
    // slope), the ceiling, the needles (cut, up to overDb off the peaks).
    bool foldDeparts = false, cutDeparts = false, ceilingDeparts = false, needlesDeparts = false;
    double lowWidth = 0.0;                     // the fold's width below its crossover (0, full mono), where it ran
    std::int32_t slopeDbPerOct = 0;            // the high-pass's slope, where it ran
    double ceilingDbTp = 0.0, overDb = 0.0;    // the ceiling the master was held to; the needles' cut, where cut
    double ruleCrossoverHz = 0.0, ruleLowWidth = 0.0, ruleCutoffHz = 0.0, ruleCeilingDbTp = 0.0;
    std::int32_t ruleSlopeDbPerOct = 0;        // the medium's rules, where vinyl
};
struct MasterReport
{
    MeasurementStatus status = MeasurementStatus::Unavailable;
    MeasurementReason reason = MeasurementReason::NotImplemented;
    double targetLufs = 0, ceilingDbTp = 0;
    std::optional<double> achievedLufs, truePeakDbTp, lraLu, plrDb, gainFromSourceDb, missLu;
    MeasurementReason lraReason = MeasurementReason::Pending, plrReason = MeasurementReason::Pending;
    bool peakSafe = false;
    bool deliverable = false;
    bool targetMet = false;
    std::uint32_t checkPasses = 0;
    MasterCrest crest {};
    std::optional<MasterHint> firstHint, secondHint;
    std::optional<MasterCost> cost;
    std::optional<MasterMedium> medium;        // a master the session decided; absent for a ready chain a shell supplied
    // THE MASTER'S READINGS (ReadingKind), as MasterReportText::readings states them when the job settles the report:
    // the achieved loudness, true peak, LRA and PLR where measured, the target, the ceiling, the gain where measured,
    // the landing's passes and the check passes. Empty for a report the job did not finish.
    BoundedList<ReadingFact, kMasterReadings> readings {};
    // The landing's mark (LandingSummary::peaksAboveCeiling): a delivered master whose true peak stands above the
    // ceiling — peakSafe false, truePeakDbTp above ceilingDbTp — because no render stayed under it.
    bool peaksAboveCeiling = false;
};
// What a landing was given and what it put on the target, beside its report: the level it landed where it landed on the
// source's gate (NaN on its own gate, where the level landed is the report's achievedLufs), and the limiter's budget it
// was held to (NaN: none).
struct LandingMeasure
{
    double landedLufs = std::numeric_limits<double>::quiet_NaN();
    double limiterBudgetDb = std::numeric_limits<double>::quiet_NaN();
};
struct MasterReportText
{
    [[nodiscard]] static std::optional<text::Fact> miss (const MasterReport& report) noexcept;
    // The landing's verdict: one fact per status — solved (the level landed against the target and the tolerance),
    // unreachable (against the tolerance, naming the summary's binding; held by the limiter's budget, the target, the
    // level landed and the budget), pass limit (against the tolerance), between (the summary's two nearest levels),
    // technical failure. Nothing for an unavailable or cancelled landing, a solved one without a measured loudness, or a
    // between one without its two levels.
    [[nodiscard]] static std::optional<text::Fact> landing (const MasterReport& report, const LandingSummary& landing,
                                                            double toleranceLu, const LandingMeasure& measure = {}) noexcept;
    // A landing on the source's gate whose level and the file's BS.1770 reading part by more than the tolerance: both.
    // Said in place of the miss's line. Nothing otherwise.
    [[nodiscard]] static std::optional<text::Fact> gate (const MasterReport& report, const LandingMeasure& measure,
                                                         double toleranceLu) noexcept;
    [[nodiscard]] static std::optional<text::Fact> hint (const MasterHint& hint) noexcept;
    // A master delivered above its ceiling (peaksAboveCeiling): its true peak and the ceiling. Nothing otherwise.
    [[nodiscard]] static std::optional<text::Fact> peaksAboveCeiling (const MasterReport& report) noexcept;
    [[nodiscard]] static text::Fact crest (const MasterCrest& crest) noexcept;
    [[nodiscard]] static text::Fact shape (const MasterCost& cost) noexcept;
    [[nodiscard]] static text::Fact impact (const MasterCost& cost) noexcept;
    [[nodiscard]] static text::Fact pumping (const MasterCost& cost) noexcept;
    [[nodiscard]] static text::Fact tonal() noexcept;
    // The cost's lines beside shape, impact and pumping, each nothing where its numbers were not measured: the section
    // that moved most (its shift, and where it lies in the source, seconds), how many sections were compared, the
    // limiter's median and P95 reduction over the active windows, the shares it worked in and that were active, and the
    // impact loss of the four bands below the full band.
    [[nodiscard]] static std::optional<text::Fact> section (const MasterCost& cost) noexcept;
    [[nodiscard]] static std::optional<text::Fact> sections (const MasterCost& cost) noexcept;
    [[nodiscard]] static std::optional<text::Fact> limiter (const MasterCost& cost) noexcept;
    [[nodiscard]] static std::optional<text::Fact> active (const MasterCost& cost) noexcept;
    [[nodiscard]] static std::optional<text::Fact> bands (const MasterCost& cost) noexcept;
    // The master's readings: the report's numbers and the landing's passes (`passes`), in the order of ReadingKind.
    [[nodiscard]] static BoundedList<ReadingFact, kMasterReadings> readings (const MasterReport& report,
                                                                            std::uint32_t passes) noexcept;
    // What the glue and the saturation did; nothing for a stage that was out of the chain.
    [[nodiscard]] static std::optional<text::Fact> glue (const MasterCost& cost) noexcept;
    [[nodiscard]] static std::optional<text::Fact> saturation (const MasterCost& cost) noexcept;
    // A deliverable master for vinyl: ready for cutting — or that its settings depart from the medium's rules; what the
    // file shows of it (a ready master only); what no file can show. And a very quiet input's line. Each nothing where
    // it does not apply.
    [[nodiscard]] static std::optional<text::Fact> vinyl (const MasterReport& report) noexcept;
    [[nodiscard]] static std::optional<text::Fact> vinylChecked (const MasterReport& report) noexcept;
    [[nodiscard]] static std::optional<text::Fact> vinylUncheckable (const MasterReport& report) noexcept;
    // Each rule of vinyl a deliverable master departs from, its own line with the chain's number and the rule's — in
    // the order fold, high-pass, ceiling, needles; nothing for a rule it keeps.
    [[nodiscard]] static std::array<std::optional<text::Fact>, 4> vinylDepartures (const MasterReport& report) noexcept;
    [[nodiscard]] static std::optional<text::Fact> quietInput (const MasterReport& report) noexcept;
};
// THE RECIPE OF A MASTER — what a master is made from, captured when it is asked for: the project as it was then (the
// machine's layer included), the source it renders and the config's sound version. Two masters with equal recipes,
// made by one release, are one master.
struct Recipe
{
    Project project {};
    std::uint64_t source = 0;                  // the source's hash (Session::source)
    std::uint64_t sound = 0;                   // config::Config::versions().sound
    std::uint64_t readyHash = 0;              // exact ready topology, parameters and delivery choice
    std::uint32_t deliveryRateHz = 0;
    std::uint32_t readyVersion = 0;
};

// A master kept: its id and its recipe.
struct Kept
{
    MasterId id = 0;
    Recipe recipe {};
    std::optional<LandingSummary> landing;
    std::optional<MasterReport> report;
};

struct MasterCrestGrid
{
    [[nodiscard]] static bool compatible (const MasterCrest& master,
                                          const analysis::BandCrestResult& source) noexcept;
    [[nodiscard]] static bool compatible (const MasterCrest& master,
                                          const analysis::BandCrestResult& source,
                                          const analysis::BandCrestParams& expected) noexcept;
};
} // namespace felitronics::session
