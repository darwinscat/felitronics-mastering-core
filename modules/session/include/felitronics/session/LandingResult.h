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
// How the delivered file was made. AsIs is the original source unchanged; PeaksOnly is delivery conversion with a
// plain non-positive gain and format dither, and no mastering devices; Mastered is the ordinary chain and landing.
enum class DeliveryMode : std::uint8_t { Mastered, AsIs, PeaksOnly };
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
enum class LandingPassReason : std::uint8_t
{
    AimAtTarget, PeakProbe, StepBackBySlope, InsideBracket, ProveEdge, DeliverWinner
};
struct LandingPass
{
    double gainDb = 0.0, ceilingDbTp = 0.0, achievedLufs = 0.0, truePeakDbTp = 0.0;
    double limiterMaxReductionDb = 0.0;
    bool ceilingSafe = false;
    bool overBudget = false;   // the limiter took more than the landing's budget on this render: no candidate
    std::optional<double> limiterP95Db;
    LandingPassReason reason = LandingPassReason::AimAtTarget;
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
// A landing measures at most twelve passes; a max master pulled up to its floor lands twice, and its record holds both.
inline constexpr std::uint32_t kLandingRecordPasses = 24;
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
    // The manual limiter wall: no useful loudness left for the cut. Its proof is for the log, not the person's verdict.
    bool limiterWall = false;
    std::optional<double> limiterSlope, limiterWallP95Db;
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
    // WHAT THE GLUE AND THE SATURATION DID, each measured on its own stage after its own mix (owner decisions 3.8, 3.9),
    // dB, positive. The glue: what its output lost against its input — the compressor's gain reduction over the
    // programme in 4 ms windows, its P95, the loud places, and its largest sample, each carried through the parallel
    // blend at the mix the stage applied (owner, 08.10); at mix 1 the compressor's own numbers. The saturation: how much less the peak of a loud place got than a quiet sound does — the peak
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
// THE DAMAGE THE PROCESSING DID, HEARD, AND WHAT BECAME OF THE MACRODYNAMICS. PEAQ of the master against the same chain
// with its dynamics at rest ([cost.damage] in engine.toml says how), graded in windows on the BS.1116 scale; and the
// loudness range of the input against the master's.
struct MasterDamage
{
    MeasurementStatus status = MeasurementStatus::Unavailable;   // Ready when at least one window was graded
    MeasurementReason reason = MeasurementReason::NotImplemented;
    // The worst window's verdict (Graded or Transparent); where no window was graded, why (NoSignal, NonFinite,
    // Undefined, OutOfRange), NotRun where PEAQ did not run.
    DamageVerdict verdict = DamageVerdict::NotRun;
    std::uint32_t grade = 0;                   // the worst window's BS.1116 grade, 5..1; 0 where none was graded
    std::optional<double> worstOdg, worstDi;   // the worst window's ODG (0 for Transparent) and the network's DI
    std::optional<double> worstFromSeconds;    // where the worst window starts
    std::uint32_t windows = 0;                 // windows graded (Graded or Transparent)
    std::uint32_t audibleWindows = 0;          // ...of them below grade 5
    std::uint32_t ungradedWindows = 0;         // windows PEAQ could not grade (NoSignal, Undefined)
    std::optional<double> audibleShare;        // audibleWindows / windows
    double windowSeconds = 0, hopSeconds = 0;
    std::optional<double> referenceGainDb;     // the gain that brought the reference to the master's loudness
    // The loudness range: the input's (the source's programme report), the master's (the report's lraLu), the change
    // (master minus input) in LU and as a share of the input's, negative where range was lost. lraReason says why a number
    // is absent: the master's or the input's own reason, NoSignal for a percentage of an input with no range.
    std::optional<double> sourceLraLu, masterLraLu, lraChangeLu, lraChangePercent;
    MeasurementReason lraReason = MeasurementReason::NotImplemented;
};
// THE WATERFALL: what stopped a zone short of (or past) its asked share, by more than 0.1 dB of the total's share
// (or 3 % of the total, where that is more): its mix at 1 or at 0,
// the clipper's cut at its domain's end or at 0, its comfort window's red (no moved knob has one yet), a stage that did
// not sound, or the steering out of moves (Passes). Reached within that; Rest, the limiter's; NoWish, no share asked.
// DriveAtCeiling: the saturation's mix at 1 and its steered drive at [saturation] steerDriveMaxDb.
enum class WaterfallStop : std::uint8_t { Reached, MixAtOne, MixAtZero, CutAtEnd, CutAtZero, ComfortRed, NotSounding, Passes, Rest,
                                          NoWish, DriveAtCeiling };
// One zone: the share of the peak work a person asked (absent: no wish; the limiter's, the rest), the share the delivered
// render reached, the dB the zone took — the glue's P95 through its mix, the saturation's usual cut, the needles' clipper's
// P95 over what it clipped, the limiter's P95 on its active windows — the setting the landing steered to (the glue's and
// the saturation's mix, the clipper's cut in dB; absent for the limiter and a stage that did not sound), and its stop.
struct MasterWaterfallZone
{
    std::optional<double> asked, reached, db, setting;
    WaterfallStop stop = WaterfallStop::NoWish;
    std::optional<double> drive;   // the saturation's drive as the landing steered it, the knob's dB; absent elsewhere
};
// The four zones, their total in dB, and the passes the fitting took beyond the landing's own (each a render the steering
// moved the stages after, so never a candidate).
struct MasterWaterfall
{
    MasterWaterfallZone glue, saturation, cut, limiter;
    std::optional<double> totalDb;
    std::uint32_t extraPasses = 0;
    // A max mode with a wish (cleaner, not louder): the loudness the landing without the wishes reached, and its
    // limiter's take there (P95 on the active windows) — the master with the zones stands on the same loudness.
    std::optional<double> aloneLufs, limiterAloneDb;
    // Two clippers ([limiter.peakClipper] place): the cut zone was the start clipper's, and this is what the
    // limiter's own clipper took off the peaks the glue and the saturation regrew (a P95 over what it clipped) — a part
    // of the limiter's rest. Absent where the cut zone was the limiter's clipper.
    std::optional<double> regrownDb;
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
    MasterDamage damage {};
    // The loudness mode this master landed in, and for a max mode what ended it; guardSteps, the steps back of v0.15.0's
    // PEAQ guard, is 0 since v0.16.0 (no guard in the loop: a max master's damage is graded as any master's).
    LoudnessMode loudnessMode = LoudnessMode::Manual;
    MaxStop maxStop = MaxStop::None;
    std::uint32_t guardSteps = 0;
    DeliveryMode deliveryMode = DeliveryMode::Mastered;
    double deliveryGainDb = 0.0;
    bool deliveryDithered = false;
    std::optional<MasterWaterfall> waterfall;  // a person's wish of shares only
};
// What a landing was given and what it put on the target, beside its report: the level it landed where it landed on the
// source's gate (NaN on its own gate, where the level landed is the report's achievedLufs), the limiter's budget it was
// held to (NaN: none), and, where no render kept that budget, what the delivered one's limiter takes (NaN otherwise).
struct LandingMeasure
{
    double landedLufs = std::numeric_limits<double>::quiet_NaN();
    double gateLufs = std::numeric_limits<double>::quiet_NaN();   // the level on the source's gate alone (NaN: none)
    double limiterBudgetDb = std::numeric_limits<double>::quiet_NaN();
    double overBudgetDb = std::numeric_limits<double>::quiet_NaN();
};
struct MasterReportText
{
    [[nodiscard]] static std::optional<text::Fact> miss (const MasterReport& report) noexcept;
    // The landing's verdict: one fact per status — solved (the level landed against the target and the tolerance),
    // unreachable (against the tolerance, naming the summary's binding; held by the limiter's budget, the target, the
    // level landed and the budget — and what the limiter takes where no render kept it), pass limit (against the tolerance), between (the summary's two nearest levels),
    // technical failure. Nothing for an unavailable or cancelled landing, a solved one without a measured loudness, or a
    // between one without its two levels.
    [[nodiscard]] static std::optional<text::Fact> landing (const MasterReport& report, const LandingSummary& landing,
                                                            double toleranceLu, const LandingMeasure& measure = {}) noexcept;
    // A landing on the source's gate whose level on that gate and the file's BS.1770 reading part by more than the
    // tolerance, either way round: both. Said in place of the miss's line. Nothing otherwise.
    [[nodiscard]] static std::optional<text::Fact> gate (const MasterReport& report, const LandingMeasure& measure,
                                                         double toleranceLu) noexcept;
    [[nodiscard]] static std::optional<text::Fact> hint (const MasterHint& hint) noexcept;
    // A max mode's verdict (report.loudnessMode, maxStop): the mode, the file's loudness and what ended the mode — the
    // mode's limiter budget `budgetDb` where that held it, the delivered render's statistic `overBudgetDb` beside the
    // budget where it broke it (LandingSearch::overBudgetDb); pulled up to the floor, the mode and the level alone.
    // Nothing for a manual master or one not delivered.
    [[nodiscard]] static std::optional<text::Fact> max (const MasterReport& report, double budgetDb, double overBudgetDb) noexcept;
    // A max master pulled up to the floor (MaxStop::Floor): its numbers, for the log only — where the first landing's file
    // stopped `firstLufs` (whatever held it: the budget or the passes, not named), the floor `floorLufs`, and the
    // active-window P95 `p95Db` the limiter took to reach it beside the mode's budget `budgetDb` (above it, printed above
    // it as 615 prints its excess; within it, as it is). The verdict beside it stays the mode and the level alone.
    // Nothing for any other stop.
    [[nodiscard]] static std::optional<text::Fact> maxFloorDetail (const MasterReport& report, double budgetDb, double p95Db,
                                                                   double floorLufs, double firstLufs) noexcept;
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
    // The damage's line: the worst window's grade, where it starts and the share heard — or that nothing was heard — with
    // the windows graded of all; or why nothing was graded. And the loudness range's: the input's, the master's and the change, or why not measured.
    [[nodiscard]] static text::Fact damage (const MasterDamage& damage) noexcept;
    [[nodiscard]] static text::Fact lra (const MasterDamage& damage) noexcept;
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
    std::uint8_t deliveryBits = 0;
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
