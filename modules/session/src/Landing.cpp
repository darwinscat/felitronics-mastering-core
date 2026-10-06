// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include <felitronics/session/Landing.h>
#include "Observations.h"
#include <felitronics/analysis/BandCrestResult.h>

#include <cmath>
#include <algorithm>

namespace felitronics::session
{
namespace
{
// A REDUCTION OVER THE BUDGET, AS PRINTED (facts 602 and 615, one decimal): rounded UP to a tenth, and at least a tenth
// above the budget's own tenths, so the printed reduction always reads above the printed budget however close above it
// the delivered render is — 3.01 over 3 prints 3.1, 7.26 over 7.25 prints 7.3; to the nearest tenth, 3.01 read "3.0".
double overBudgetShown (double overDb, double budgetDb) noexcept
{
    return std::max (std::ceil (overDb * 10.0), std::floor (budgetDb * 10.0) + 1.0) / 10.0;
}
}

bool MasterCrestGrid::compatible (const MasterCrest& master,
                                  const analysis::BandCrestResult& source) noexcept
{
    return master.version == 1 && master.complete
        && source.reason == analysis::BandCrestInvalid::None && source.droppedHops == 0
        && source.nonFiniteSamples == 0 && source.sampleRate > 0
        && core::exactlyEqual (source.sampleRate, double (master.sampleRateHz))
        && source.hopSamples == master.hopFrames && source.parameters.blockHops == int (master.blockHops)
        && source.frames == master.frames && source.blocks.size() % 10u == 0
        && source.blockCount() == master.blocks
        && source.mask.size() == master.blocks * 5u
        && core::exactlyEqual (source.parameters.bandEdgeHz[0], master.edgeLowHz)
        && core::exactlyEqual (source.parameters.bandEdgeHz[1], master.edgeMidHz)
        && core::exactlyEqual (source.parameters.bandEdgeHz[2], master.edgeHighHz);
}
bool MasterCrestGrid::compatible (const MasterCrest& master,
                                  const analysis::BandCrestResult& source,
                                  const analysis::BandCrestParams& expected) noexcept
{
    return compatible (master, source)
        && core::exactlyEqual (source.parameters.hopMs, expected.hopMs)
        && source.parameters.blockHops == expected.blockHops
        && core::exactlyEqual (source.parameters.programmeFloorDb, expected.programmeFloorDb)
        && core::exactlyEqual (source.parameters.bandShareFloorDb, expected.bandShareFloorDb)
        && core::exactlyEqual (source.parameters.bandEdgeHz[0], expected.bandEdgeHz[0])
        && core::exactlyEqual (source.parameters.bandEdgeHz[1], expected.bandEdgeHz[1])
        && core::exactlyEqual (source.parameters.bandEdgeHz[2], expected.bandEdgeHz[2]);
}
std::optional<text::Fact> MasterReportText::miss (const MasterReport& report) noexcept
{
    // A master above its ceiling is said by its own line (peaksAboveCeiling): these two say the true peak held.
    if (report.status != MeasurementStatus::Ready || ! report.deliverable || report.targetMet || report.peaksAboveCeiling
        || ! report.achievedLufs || ! report.missLu || ! std::isfinite (*report.missLu)
        || std::fabs (*report.missLu) <= 0.0) return {};
    return text::Fact::of (*report.missLu < 0 ? text::FactId::MasterLandingMiss
                                              : text::FactId::MasterLandingAbove,
        text::Arg::value (*report.achievedLufs, text::Unit::Lufs, 1),
        text::Arg::value (report.targetLufs, text::Unit::Lufs, 1),
        text::Arg::value (std::fabs (*report.missLu), text::Unit::Lu, 1));
}
std::optional<text::Fact> MasterReportText::gate (const MasterReport& report, const LandingMeasure& measure,
                                                  double toleranceLu) noexcept
{
    if (report.status != MeasurementStatus::Ready || ! report.deliverable || ! report.achievedLufs
        || ! std::isfinite (measure.gateLufs) || ! (std::fabs (measure.gateLufs - *report.achievedLufs) > toleranceLu))
        return std::nullopt;
    return text::Fact::of (text::FactId::MasterLandingGate, text::Arg::value (measure.gateLufs, text::Unit::Lufs, 1),
                           text::Arg::value (*report.achievedLufs, text::Unit::Lufs, 1));
}
std::optional<text::Fact> MasterReportText::landing (const MasterReport& report, const LandingSummary& landing,
                                                     double toleranceLu, const LandingMeasure& measure) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Term; using text::Unit;
    const auto tolerance = Arg::value (toleranceLu, Unit::Lu, 1);
    // The limiter's budget as the config states it, a whole number of quarter dB: its digits up to two, the trailing
    // zeros dropped — 7, 7.5, 7.25 — read off its hundredths as an integer. Asked only of a finite budget (a landing
    // without one, NaN, says no budget's line), within the schema's 1…60 dB.
    const auto budget = [&measure] () noexcept
    {
        const auto hundredths = (long long) std::floor (std::clamp (measure.limiterBudgetDb, 0.0, 1.0e6) * 100.0 + 0.5);
        return Arg::value (measure.limiterBudgetDb, Unit::Db,
            hundredths % 100 == 0 ? std::uint8_t (0) : hundredths % 10 == 0 ? std::uint8_t (1) : std::uint8_t (2));
    };
    // The level landed: on the source's gate where the landing measured it there, else the file's.
    const std::optional<double> landed = std::isfinite (measure.landedLufs) ? std::optional<double> (measure.landedLufs)
                                                                            : report.achievedLufs;
    const auto limit = [] (LandingConstraint c) noexcept
    {
        switch (c)
        {
            case LandingConstraint::TruePeakCeiling: return Term::LandingLimitTruePeak;
            case LandingConstraint::LimiterGainReduction: return Term::LandingLimitLimiter;
            case LandingConstraint::PeakToLoudness: return Term::LandingLimitPlr;
            case LandingConstraint::LoudnessRange: return Term::LandingLimitLra;
            case LandingConstraint::GainRange: return Term::LandingLimitGain;
            case LandingConstraint::None: break;
        }
        return Term::LandingLimitNone;
    };
    switch (landing.status)
    {
        case LandingStatus::Solved:
            if (report.status != MeasurementStatus::Ready || ! landed) return std::nullopt;
            return Fact::of (FactId::MasterLandingSolved, Arg::value (*landed, Unit::Lufs, 1),
                             Arg::value (report.targetLufs, Unit::Lufs, 1), tolerance);
        case LandingStatus::TargetUnreachable:
            if (landing.limiterWall && landed)
                return Fact::of (FactId::MasterLandingWall, Arg::value (*landed, Unit::Lufs, 1));
            if (landing.binding == LandingConstraint::LimiterGainReduction && std::isfinite (measure.limiterBudgetDb)
                && report.status == MeasurementStatus::Ready && landed)
                return std::isfinite (measure.overBudgetDb)
                    ? Fact::of (FactId::MasterLandingOverBudget, Arg::value (report.targetLufs, Unit::Lufs, 1),
                                Arg::value (*landed, Unit::Lufs, 1), budget(),
                                Arg::value (overBudgetShown (measure.overBudgetDb, measure.limiterBudgetDb), Unit::Db, 1))
                    : Fact::of (FactId::MasterLandingBudget, Arg::value (report.targetLufs, Unit::Lufs, 1),
                                Arg::value (*landed, Unit::Lufs, 1), budget());
            return Fact::of (FactId::MasterLandingUnreachable, tolerance, Arg::term (limit (landing.binding)));
        case LandingStatus::PassLimit: return Fact::of (FactId::MasterLandingPassLimit, tolerance);
        case LandingStatus::TargetBetweenAchievable:
            if (! landing.belowLufs || ! landing.aboveLufs) return std::nullopt;
            return Fact::of (FactId::MasterLandingBetween, Arg::value (*landing.belowLufs, Unit::Lufs, 1),
                             Arg::value (*landing.aboveLufs, Unit::Lufs, 1));
        case LandingStatus::TechnicalFailure: return Fact::of (FactId::MasterLandingFailed);
        case LandingStatus::Unavailable:
        case LandingStatus::Cancelled: break;
    }
    return std::nullopt;
}
std::optional<text::Fact> MasterReportText::maxFloorDetail (const MasterReport& report, double budgetDb, double p95Db,
                                                            double floorLufs, double firstLufs) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Term; using text::Unit;
    if (report.maxStop != MaxStop::Floor || ! report.deliverable || ! std::isfinite (budgetDb) || ! std::isfinite (p95Db)
        || ! std::isfinite (floorLufs) || ! std::isfinite (firstLufs)) return std::nullopt;
    const auto mode = Arg::term (report.loudnessMode == LoudnessMode::MaxClean ? Term::LoudnessModeMaxClean : Term::LoudnessModeMaxDense);
    const auto hundredths = (long long) std::floor (std::clamp (budgetDb, 0.0, 1.0e6) * 100.0 + 0.5);
    const auto budget = Arg::value (budgetDb, Unit::Db,
        hundredths % 100 == 0 ? std::uint8_t (0) : hundredths % 10 == 0 ? std::uint8_t (1) : std::uint8_t (2));
    return Fact::of (FactId::MasterMaxFloorDetail, mode, Arg::value (firstLufs, Unit::Lufs, 1), Arg::value (floorLufs, Unit::Lufs, 1),
                     Arg::value (p95Db > budgetDb ? overBudgetShown (p95Db, budgetDb) : p95Db, Unit::Db, 1), budget);
}

std::optional<text::Fact> MasterReportText::max (const MasterReport& report, double budgetDb, double overBudgetDb) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Term; using text::Unit;
    if (report.loudnessMode == LoudnessMode::Manual || report.status != MeasurementStatus::Ready || ! report.deliverable
        || ! report.achievedLufs) return std::nullopt;
    const auto mode = Arg::term (report.loudnessMode == LoudnessMode::MaxClean ? Term::LoudnessModeMaxClean : Term::LoudnessModeMaxDense);
    const auto achieved = Arg::value (*report.achievedLufs, Unit::Lufs, 1);
    const auto steps = Arg::count (std::int64_t (report.guardSteps));
    // The budget printed whole: up to two decimals, the trailing zeros dropped, decided on its hundredths as an integer.
    const auto budget = [budgetDb]
    {
        const auto hundredths = (long long) std::floor (std::clamp (budgetDb, 0.0, 1.0e6) * 100.0 + 0.5);
        return Arg::value (budgetDb, Unit::Db,
            hundredths % 100 == 0 ? std::uint8_t (0) : hundredths % 10 == 0 ? std::uint8_t (1) : std::uint8_t (2));
    };
    switch (report.maxStop)
    {
        case MaxStop::Budget:
            if (! std::isfinite (budgetDb)) return std::nullopt;
            return Fact::of (FactId::MasterMaxBudget, mode, achieved, budget());
        case MaxStop::OverBudget:
            if (! std::isfinite (budgetDb) || ! std::isfinite (overBudgetDb)) return std::nullopt;
            return Fact::of (FactId::MasterMaxOverBudget, mode, achieved,
                             Arg::value (overBudgetShown (overBudgetDb, budgetDb), Unit::Db, 1), budget());
        case MaxStop::Floor: return Fact::of (FactId::MasterMaxFloor, mode, achieved);
        case MaxStop::Unguarded: return Fact::of (FactId::MasterMaxUnguarded, mode, achieved);
        case MaxStop::Guard: return Fact::of (FactId::MasterMaxGuard, mode, achieved, steps);
        case MaxStop::GuardUnmet: return Fact::of (FactId::MasterMaxGuardUnmet, mode, achieved, steps);
        case MaxStop::SearchCeiling: return Fact::of (FactId::MasterMaxCeiling, mode, achieved);
        case MaxStop::Passes: return Fact::of (FactId::MasterMaxPasses, mode, achieved);
        case MaxStop::TruePeak: return Fact::of (FactId::MasterMaxTruePeak, mode, achieved);
        case MaxStop::None: break;
    }
    return std::nullopt;
}
std::optional<text::Fact> MasterReportText::peaksAboveCeiling (const MasterReport& report) noexcept
{
    if (! report.peaksAboveCeiling || ! report.truePeakDbTp) return std::nullopt;
    return text::Fact::of (text::FactId::MasterPeaksAboveCeiling, text::Arg::value (*report.truePeakDbTp, text::Unit::DbTp, 2),
                           text::Arg::value (report.ceilingDbTp, text::Unit::DbTp, 2));
}
std::optional<text::Fact> MasterReportText::hint (const MasterHint& hint) noexcept
{
    if (! std::isfinite (hint.evidence)) return {};
    using text::FactId;
    FactId id = FactId::MasterHintDemand;
    text::Unit unit = text::Unit::Db;
    switch (hint.reason)
    {
        case LandingReason::ExcessSubBass: id = FactId::MasterHintSubBass; unit = text::Unit::Percent; break;
        case LandingReason::SharpPeaks: id = FactId::MasterHintPeaks; break;
        case LandingReason::DarkMix: id = FactId::MasterHintDark; unit = text::Unit::Percent; break;
        case LandingReason::LoudnessDemand: id = FactId::MasterHintDemand; unit = text::Unit::Lu; break;
        case LandingReason::GainRange: id = FactId::MasterHintGainRange; break;
        case LandingReason::TruePeak: id = FactId::MasterHintTruePeak; unit = text::Unit::DbTp; break;
        case LandingReason::None: return {};
    }
    return text::Fact::of (id, text::Arg::value (hint.evidence, unit, 1));
}
text::Fact MasterReportText::crest (const MasterCrest& crest) noexcept
{
    if (crest.status == MeasurementStatus::Pending)
        return text::Fact::of (text::FactId::MasterCrestPending);
    if (crest.status != MeasurementStatus::Ready)
        return text::Fact::of (text::FactId::MasterCrestUnavailable);
    return text::Fact::of (crest.sourceRateCheck ? text::FactId::MasterCrestSourceRate
                                                : text::FactId::MasterCrestDelivered,
        text::Arg::value (double (crest.sampleRateHz), text::Unit::Hz, 0));
}
text::Fact MasterReportText::shape (const MasterCost& cost) noexcept
{
    return cost.shapeP95Lu.value
        ? text::Fact::of (text::FactId::MasterCostShape,
            text::Arg::value (*cost.shapeP95Lu.value, text::Unit::Lu, 1))
        : text::Fact::of (text::FactId::MasterCostUnavailable);
}
text::Fact MasterReportText::impact (const MasterCost& cost) noexcept
{
    return cost.crestFullDb.value
        ? text::Fact::of (text::FactId::MasterCostCrest,
            text::Arg::value (*cost.crestFullDb.value, text::Unit::Db, 1))
        : text::Fact::of (text::FactId::MasterCostUnavailable);
}
text::Fact MasterReportText::pumping (const MasterCost& cost) noexcept
{
    return cost.pumpingRmsDb.value
        ? text::Fact::of (text::FactId::MasterCostPumping,
            text::Arg::value (*cost.pumpingRmsDb.value, text::Unit::Db, 1))
        : text::Fact::of (text::FactId::MasterCostUnavailable);
}
text::Fact MasterReportText::tonal() noexcept
{ return text::Fact::of (text::FactId::MasterCostK2Deferred); }
std::optional<text::Fact> MasterReportText::section (const MasterCost& cost) noexcept
{
    if (! cost.largestSectionShiftLu.value || ! cost.worstSectionIndex || *cost.worstSectionIndex >= cost.sections.size()
        || cost.sourceRateHz == 0) return std::nullopt;
    const auto& worst = cost.sections[*cost.worstSectionIndex];
    const auto at = [&] (std::uint64_t frame) { return text::Arg::value (double (frame) / double (cost.sourceRateHz), text::Unit::S, 0); };
    return text::Fact::of (text::FactId::MasterCostSection,
        text::Arg::value (*cost.largestSectionShiftLu.value, text::Unit::Lu, 1, text::Sign::Always), at (worst.fromFrame), at (worst.toFrame));
}
std::optional<text::Fact> MasterReportText::sections (const MasterCost& cost) noexcept
{
    if (cost.sections.empty()) return std::nullopt;
    std::int64_t compared = 0;
    for (const auto& one : cost.sections) compared += one.compared ? 1 : 0;
    return text::Fact::of (text::FactId::MasterCostSections, text::Arg::count (compared));
}
std::optional<text::Fact> MasterReportText::limiter (const MasterCost& cost) noexcept
{
    if (! cost.limiterP50Db.value || ! cost.limiterP95Db.value) return std::nullopt;
    return text::Fact::of (text::FactId::MasterCostLimiter, text::Arg::value (*cost.limiterP50Db.value, text::Unit::Db, 1),
        text::Arg::value (*cost.limiterP95Db.value, text::Unit::Db, 1));
}
std::optional<text::Fact> MasterReportText::active (const MasterCost& cost) noexcept
{
    if (! cost.limiterActiveShare.value || ! cost.activeWindowShare.value) return std::nullopt;
    return text::Fact::of (text::FactId::MasterCostActive,
        text::Arg::value (100.0 * *cost.limiterActiveShare.value, text::Unit::Percent, 0),
        text::Arg::value (100.0 * *cost.activeWindowShare.value, text::Unit::Percent, 0));
}
std::optional<text::Fact> MasterReportText::bands (const MasterCost& cost) noexcept
{
    if (! cost.crestLowDb.value || ! cost.crestLowMidDb.value || ! cost.crestHighMidDb.value || ! cost.crestHighDb.value)
        return std::nullopt;
    const auto db = [] (double v) { return text::Arg::value (v, text::Unit::Db, 1); };
    return text::Fact::of (text::FactId::MasterCostBands, db (*cost.crestLowDb.value), db (*cost.crestLowMidDb.value),
        db (*cost.crestHighMidDb.value), db (*cost.crestHighDb.value));
}
BoundedList<ReadingFact, kMasterReadings> MasterReportText::readings (const MasterReport& report, std::uint32_t passes) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    BoundedList<ReadingFact, kMasterReadings> out;
    const auto put = [&] (ReadingKind kind, std::optional<double> v, Unit unit, text::Sign sign = text::Sign::Negative) noexcept
    {
        if (v && std::isfinite (*v) && out.count < kMasterReadings)
            out.items[out.count++] = { kind, Fact::of (FactId::Value, Arg::value (*v, unit, unit == Unit::None ? 0 : 1, sign)) };
    };
    put (ReadingKind::Integrated, report.achievedLufs, Unit::Lufs);
    put (ReadingKind::TruePeak, report.truePeakDbTp, Unit::DbTp);
    put (ReadingKind::Lra, report.lraLu, Unit::Lu);
    put (ReadingKind::Plr, report.plrDb, Unit::Db);
    put (ReadingKind::Target, report.targetLufs, Unit::Lufs);
    put (ReadingKind::Ceiling, report.ceilingDbTp, Unit::DbTp);
    put (ReadingKind::Gain, report.gainFromSourceDb, Unit::Db, text::Sign::Always);
    put (ReadingKind::Passes, double (passes), Unit::None);
    put (ReadingKind::CheckPasses, double (report.checkPasses), Unit::None);
    return out;
}
std::optional<text::Fact> MasterReportText::glue (const MasterCost& cost) noexcept
{
    if (! cost.glueP95Db.value || ! cost.glueMaxDb.value) return std::nullopt;
    return text::Fact::of (text::FactId::MasterGlue, text::Arg::value (*cost.glueP95Db.value, text::Unit::Db, 1),
        text::Arg::value (*cost.glueMaxDb.value, text::Unit::Db, 1));
}
std::optional<text::Fact> MasterReportText::saturation (const MasterCost& cost) noexcept
{
    if (! cost.saturationCutMaxDb.value || ! cost.saturationCutUsualDb.value) return std::nullopt;
    return text::Fact::of (text::FactId::MasterSaturation, text::Arg::value (*cost.saturationCutMaxDb.value, text::Unit::Db, 1),
        text::Arg::value (*cost.saturationCutUsualDb.value, text::Unit::Db, 1));
}
text::Fact MasterReportText::damage (const MasterDamage& d) noexcept
{
    if (d.status != MeasurementStatus::Ready || d.grade < 1 || d.grade > 5 || ! d.worstFromSeconds || ! d.audibleShare)
        return text::Fact::of (text::FactId::MasterDamageUnmeasured, text::Arg::term (detail::reasonTerm (d.reason)));
    // What was graded, of all the windows: those PEAQ could not grade (no signal, undefined) are no part of the verdict.
    const auto graded = text::Arg::count (d.windows), windows = text::Arg::count (std::int64_t (d.windows) + d.ungradedWindows);
    if (d.grade == 5) return text::Fact::of (text::FactId::MasterDamageInaudible, graded, windows);
    const auto grade = text::Term (unsigned (text::Term::DamageGradeImperceptible) + (5u - d.grade));
    return text::Fact::of (text::FactId::MasterDamage, text::Arg::term (grade),
        text::Arg::value (*d.worstFromSeconds, text::Unit::S, 0), text::Arg::value (100.0 * *d.audibleShare, text::Unit::Percent, 0),
        graded, windows);
}
text::Fact MasterReportText::lra (const MasterDamage& d) noexcept
{
    if (! d.sourceLraLu || ! d.masterLraLu || ! d.lraChangeLu || ! d.lraChangePercent)
        return text::Fact::of (text::FactId::MasterLraUnmeasured, text::Arg::term (detail::reasonTerm (d.lraReason)));
    return text::Fact::of (text::FactId::MasterLraChange, text::Arg::value (*d.sourceLraLu, text::Unit::Lu, 1),
        text::Arg::value (*d.masterLraLu, text::Unit::Lu, 1),
        text::Arg::value (*d.lraChangePercent, text::Unit::Percent, 0, text::Sign::Always),
        text::Arg::value (*d.lraChangeLu, text::Unit::Lu, 1, text::Sign::Always));
}
std::optional<text::Fact> MasterReportText::vinyl (const MasterReport& report) noexcept
{
    if (! report.medium || ! report.medium->vinyl || ! report.deliverable) return std::nullopt;
    return text::Fact::of (report.medium->ready ? text::FactId::MasterVinylReady : text::FactId::MasterVinylDeparts);
}
std::optional<text::Fact> MasterReportText::vinylChecked (const MasterReport& report) noexcept
{
    if (! report.medium || ! report.medium->vinyl || ! report.medium->ready || ! report.deliverable || ! report.truePeakDbTp)
        return std::nullopt;
    return text::Fact::of (text::FactId::MasterVinylChecked, text::Arg::value (report.medium->crossoverHz, text::Unit::Hz, 0),
        text::Arg::value (report.medium->cutoffHz, text::Unit::Hz, 0), text::Arg::value (*report.truePeakDbTp, text::Unit::DbTp, 1),
        text::Arg::value (report.ceilingDbTp, text::Unit::DbTp, 1));
}
std::array<std::optional<text::Fact>, 4> MasterReportText::vinylDepartures (const MasterReport& report) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    std::array<std::optional<Fact>, 4> said {};
    if (! report.medium || ! report.medium->vinyl || ! report.deliverable) return said;
    const auto& m = *report.medium;
    const auto hz = [] (double v) { return Arg::value (v, Unit::Hz, 0); };
    const auto width = [] (double v) { return Arg::value (100.0 * v, Unit::Percent, 0); };
    const auto slope = [] (std::int32_t v) { return Arg::count (v); };
    // An off stage has no number of its own: its line names the rule alone.
    if (m.foldDeparts)
        said[0] = m.crossoverHz > 0.0
            ? Fact::of (FactId::MasterVinylFoldDeparts, hz (m.crossoverHz), width (m.lowWidth), hz (m.ruleCrossoverHz), width (m.ruleLowWidth))
            : Fact::of (FactId::MasterVinylNoFold, hz (m.ruleCrossoverHz));
    if (m.cutDeparts)
        said[1] = m.cutoffHz > 0.0
            ? Fact::of (FactId::MasterVinylHighPassDeparts, hz (m.cutoffHz), slope (m.slopeDbPerOct), hz (m.ruleCutoffHz), slope (m.ruleSlopeDbPerOct))
            : Fact::of (FactId::MasterVinylNoHighPass, hz (m.ruleCutoffHz), slope (m.ruleSlopeDbPerOct));
    if (m.ceilingDeparts)
        said[2] = Fact::of (FactId::MasterVinylCeilingDeparts, Arg::value (m.ceilingDbTp, Unit::DbTp, 1), Arg::value (m.ruleCeilingDbTp, Unit::DbTp, 1));
    if (m.needlesDeparts)
        said[3] = Fact::of (FactId::MasterVinylNeedlesDeparts, Arg::value (m.overDb, Unit::Db, 1));
    return said;
}
std::optional<text::Fact> MasterReportText::vinylUncheckable (const MasterReport& report) noexcept
{
    if (! report.medium || ! report.medium->vinyl || ! report.deliverable) return std::nullopt;
    return text::Fact::of (text::FactId::MasterVinylUncheckable);
}
std::optional<text::Fact> MasterReportText::quietInput (const MasterReport& report) noexcept
{
    if (! report.medium || ! report.medium->quietInput || ! report.deliverable || ! report.gainFromSourceDb) return std::nullopt;
    return text::Fact::of (text::FactId::MasterQuietInput, text::Arg::value (report.medium->inputLufs, text::Unit::Lufs, 1),
        text::Arg::value (*report.gainFromSourceDb, text::Unit::Db, 1, text::Sign::Always));
}
LandingPlan LandingOps::plan (const config::Engine& engine, bool sourceLoudnessValid,
                         double sourceLufs, double targetLufs, double targetTruePeakDbTp,
                         double sourceRate, double deliveryRate) noexcept
{
    LandingPlan plan;
    if (! std::isfinite (targetLufs) || ! std::isfinite (targetTruePeakDbTp)
        || ! std::isfinite (sourceRate) || sourceRate <= 0.0
        || ! std::isfinite (deliveryRate) || deliveryRate <= 0.0
        || engine.landing.passes < 1 || engine.landing.passes > 12
        || ! std::isfinite (engine.landing.toleranceLu) || engine.landing.toleranceLu < 0.0
        || ! std::isfinite (engine.landing.truePeakAimDb) || engine.landing.truePeakAimDb < 0.0
        || ! std::isfinite (engine.input.referenceLufs)
        || ! std::isfinite (engine.limiter.ceilingMarginDb) || engine.limiter.ceilingMarginDb < 0.0)
        return plan;
    if (! sourceLoudnessValid || ! std::isfinite (sourceLufs) || sourceLufs <= -120.0)
    {
        plan.status = LandingPlanStatus::UnavailableSource;
        return plan;
    }
    const double normalization = engine.input.referenceLufs - sourceLufs;
    if (! std::isfinite (normalization) || std::fabs (normalization) > 60.0)
    {
        plan.status = LandingPlanStatus::GainRange;
        return plan;
    }
    plan.status = LandingPlanStatus::Ready;
    plan.request.targetLufs = targetLufs;
    plan.request.maxTruePeakDbTp = targetTruePeakDbTp;
    plan.request.toleranceLu = engine.landing.toleranceLu;
    plan.request.truePeakAimDb = engine.landing.truePeakAimDb;
    plan.request.maxPasses = engine.landing.passes;
    plan.request.normalizationGainDb = normalization;
    plan.request.initialGainDb = targetLufs - engine.input.referenceLufs;
    plan.request.productLanding = true;
    plan.request.ceilingMarginDb = engine.limiter.ceilingMarginDb;
    plan.initialCeilingDbTp = targetTruePeakDbTp - engine.limiter.ceilingMarginDb;
    plan.sourceRateImpactPass = ! core::exactlyEqual (sourceRate, deliveryRate);
    return plan;
}

namespace
{
LandingPass passRow (const mastering::SolvePassRecord& source) noexcept
{
    const auto passReason = [] (mastering::SolvePassRecord::Reason value) noexcept
    {
        using Source = mastering::SolvePassRecord::Reason;
        switch (value)
        {
            case Source::AimAtTarget: return LandingPassReason::AimAtTarget;
            case Source::PeakProbe: return LandingPassReason::PeakProbe;
            case Source::StepBackBySlope: return LandingPassReason::StepBackBySlope;
            case Source::InsideBracket: return LandingPassReason::InsideBracket;
            case Source::ProveEdge: return LandingPassReason::ProveEdge;
            case Source::DeliverWinner: return LandingPassReason::DeliverWinner;
        }
        return LandingPassReason::AimAtTarget;
    };
    LandingPass row;
    row.gainDb = source.gainDb; row.ceilingDbTp = source.ceilingDb;
    row.achievedLufs = source.integratedLufs; row.truePeakDbTp = source.truePeakDbTp;
    row.limiterMaxReductionDb = source.limiterMaxGrDb;
    if (std::isfinite (source.limiterP95Db)) row.limiterP95Db = source.limiterP95Db;
    row.reason = passReason (source.reason);
    row.ceilingSafe = (source.violated & mastering::constraintBit (mastering::MasteringConstraint::TruePeakCeiling)) == 0;
    row.overBudget = (source.violated & mastering::constraintBit (mastering::MasteringConstraint::LimiterGainReduction)) != 0;
    return row;
}
bool summaryOf (const mastering::LoudnessSolution& solution, std::uint32_t earlier, std::uint64_t earlierWork,
                std::span<LandingPass> rows, std::span<LandingTraceBucket> limiterRows,
                std::span<LandingTraceBucket> peakClipRows, std::uint32_t deliveryRateHz, LandingSummary& out) noexcept;
} // namespace

bool LandingOps::summarize (const mastering::LoudnessSolution& solution,
                       std::span<LandingPass> rows, LandingSummary& out) noexcept
{
    return summaryOf (solution, 0, 0, rows, {}, {}, 0, out);
}

bool LandingOps::summarize (const mastering::LoudnessSolution& solution,
                       std::span<LandingPass> rows, std::span<LandingTraceBucket> limiterRows,
                       std::span<LandingTraceBucket> peakClipRows, std::uint32_t deliveryRateHz,
                       LandingSummary& out) noexcept
{
    return summaryOf (solution, 0, 0, rows, limiterRows, peakClipRows, deliveryRateHz, out);
}

bool LandingOps::summarize (const mastering::LoudnessSolution& solution, std::uint32_t earlier, std::uint64_t earlierWork,
                       std::span<LandingPass> rows, LandingSummary& out) noexcept
{
    return summaryOf (solution, earlier, earlierWork, rows, {}, {}, 0, out);
}

bool LandingOps::passRows (const mastering::LoudnessSolution& solution, std::span<LandingPass> rows) noexcept
{
    if (solution.logCount < 0 || solution.logCount > 12 || rows.size() < (std::size_t) solution.logCount) return false;
    for (int i = 0; i < solution.logCount; ++i) rows[(std::size_t) i] = passRow (solution.log[i]);
    return true;
}

namespace
{
bool summaryOf (const mastering::LoudnessSolution& solution, std::uint32_t earlier, std::uint64_t earlierWork,
                std::span<LandingPass> rows, std::span<LandingTraceBucket> limiterRows,
                std::span<LandingTraceBucket> peakClipRows, std::uint32_t deliveryRateHz, LandingSummary& out) noexcept
{
    // One landing measures at most twelve passes; a max master pulled up to its floor has the first landing's `earlier`
    // rows ahead of its own (passRows wrote them when the floor landing began): one record, one count, one work.
    if (solution.passes < 0 || solution.passes > 12 || solution.logCount < solution.passes
        || solution.logCount > mastering::TargetLoudnessSolverLimits::kMaxPasses || earlier > 12
        || rows.size() < std::size_t (earlier) + (std::size_t) solution.logCount) return false;
    const auto& limiter = solution.limiterTrace;
    const auto& clipper = solution.peakClipTrace;
    const bool haveTraces = deliveryRateHz > 0;
    if (haveTraces && (limiter.buckets != clipper.buckets
        || limiter.programmeFrames != clipper.programmeFrames
        || limiterRows.size() < limiter.bucket.size() || peakClipRows.size() < clipper.bucket.size()
        || limiter.bucket.size() != (std::size_t) limiter.buckets
        || clipper.bucket.size() != (std::size_t) clipper.buckets)) return false;
    LandingSummary next;
    switch (solution.status)
    {
        case mastering::MasteringSolveStatus::Solved: next.status = LandingStatus::Solved; break;
        case mastering::MasteringSolveStatus::TargetUnreachable: next.status = LandingStatus::TargetUnreachable; break;
        case mastering::MasteringSolveStatus::PassLimit: next.status = LandingStatus::PassLimit; break;
        case mastering::MasteringSolveStatus::TargetBetweenAchievable: next.status = LandingStatus::TargetBetweenAchievable; break;
        case mastering::MasteringSolveStatus::Cancelled: next.status = LandingStatus::Cancelled; break;
        case mastering::MasteringSolveStatus::MeasurementInvalid:
        case mastering::MasteringSolveStatus::Unavailable: next.status = LandingStatus::Unavailable; break;
        case mastering::MasteringSolveStatus::UpstreamViolation:
        case mastering::MasteringSolveStatus::RenderFailed:
        case mastering::MasteringSolveStatus::NotPrepared:
        case mastering::MasteringSolveStatus::InvalidRequest: next.status = LandingStatus::TechnicalFailure; break;
    }
    const auto reason = [] (mastering::LandingReason value) noexcept -> LandingReason
    {
        switch (value)
        {
            case mastering::LandingReason::None: return LandingReason::None;
            case mastering::LandingReason::ExcessSubBass: return LandingReason::ExcessSubBass;
            case mastering::LandingReason::SharpPeaks: return LandingReason::SharpPeaks;
            case mastering::LandingReason::DarkMix: return LandingReason::DarkMix;
            case mastering::LandingReason::LoudnessDemand: return LandingReason::LoudnessDemand;
            case mastering::LandingReason::GainRange: return LandingReason::GainRange;
            case mastering::LandingReason::TruePeak: return LandingReason::TruePeak;
        }
        return LandingReason::None;
    };
    next.mainReason = reason (solution.mainReason);
    next.secondReason = reason (solution.secondReason);
    // What held an unreachable landing, as the solver named it where it decided the verdict; nothing for another status.
    if (next.status == LandingStatus::TargetUnreachable)
        switch (solution.binding)
        {
            case mastering::MasteringConstraint::TruePeakCeiling: next.binding = LandingConstraint::TruePeakCeiling; break;
            case mastering::MasteringConstraint::LimiterGainReduction: next.binding = LandingConstraint::LimiterGainReduction; break;
            case mastering::MasteringConstraint::PeakToLoudness: next.binding = LandingConstraint::PeakToLoudness; break;
            case mastering::MasteringConstraint::LoudnessRange: next.binding = LandingConstraint::LoudnessRange; break;
            case mastering::MasteringConstraint::GainRange: next.binding = LandingConstraint::GainRange; break;
            case mastering::MasteringConstraint::None:
            case mastering::MasteringConstraint::CompressorGainReduction: break;
        }
    // The two levels a target fell between, as the solver measured them where it found the bracket. Both are numbers on
    // every known path (LandingSearch takes a side only from a finite, ceiling-safe pass); were one not, the verdict
    // stands without its levels and the master is still delivered — a target that cannot be hit always returns the file
    // (owner, 2026-10-01).
    if (next.status == LandingStatus::TargetBetweenAchievable
        && std::isfinite (solution.achievedBelowLufs) && std::isfinite (solution.achievedAboveLufs))
    {
        next.belowLufs = std::fmin (solution.achievedBelowLufs, solution.achievedAboveLufs);
        next.aboveLufs = std::fmax (solution.achievedBelowLufs, solution.achievedAboveLufs);
    }
    next.deliverable = solution.deliverable;
    next.limiterWall = solution.limiterWall;
    if (solution.limiterWall)
    {
        next.limiterSlope = solution.limiterSlope;
        next.limiterWallP95Db = solution.limiterWallP95Db;
    }
    next.passes = earlier + (std::uint32_t) solution.passes;
    next.workUnits = earlierWork + solution.workUnits;
    if (solution.deliverable)
    {
        if (! std::isfinite (solution.achievedLufs) || ! std::isfinite (solution.missLu)
            || ! std::isfinite (solution.distanceLu) || ! std::isfinite (solution.measured.truePeakDbTp)) return false;
        next.achievedLufs = solution.achievedLufs;
        next.missLu = solution.missLu;
        next.distanceLu = solution.distanceLu;
        next.truePeakDbTp = solution.measured.truePeakDbTp;
        next.peaksAboveCeiling = solution.peaksAboveCeiling;
    }
    if (std::isfinite (solution.sourceSubBassShare)) next.sourceSubBassShare = solution.sourceSubBassShare;
    if (std::isfinite (solution.sourcePresenceShare)) next.sourcePresenceShare = solution.sourcePresenceShare;
    if (std::isfinite (solution.limiterMeanReductionDb)) next.limiterMeanReductionDb = solution.limiterMeanReductionDb;
    for (int i = 0; i < solution.logCount; ++i) rows[std::size_t (earlier) + (std::size_t) i] = passRow (solution.log[i]);
    next.log = { rows.data(), std::size_t (earlier) + (std::size_t) solution.logCount };
    if (haveTraces && limiter.buckets > 0)
    {
        const auto copyTrace = [deliveryRateHz] (const mastering::GainReductionTrace& source,
                                                  std::span<LandingTraceBucket> storage) noexcept
        {
            LandingTrace trace;
            trace.toFrame = source.programmeFrames;
            trace.sampleRateHz = deliveryRateHz;
            trace.columns = (std::uint32_t) source.buckets;
            trace.complete = source.complete;
            trace.valid = source.valid;
            trace.samples = source.complete ? source.samples : 0;
            trace.nonFinite = source.complete ? source.nonFinite : 0;
            for (std::size_t i = 0; i < source.bucket.size(); ++i)
            {
                const auto& b = source.bucket[i];
                const auto finite = b.samples - b.nonFinite;
                const double mean = source.complete || finite == 0 ? b.meanDb : b.meanDb / double (finite);
                storage[i] = { b.minDb, b.maxDb, mean, b.samples, b.nonFinite };
                if (! source.complete) { trace.samples += b.samples; trace.nonFinite += b.nonFinite; }
            }
            trace.rows = storage.first (source.bucket.size());
            return trace;
        };
        next.limiterTrace = copyTrace (limiter, limiterRows);
        next.peakClipTrace = copyTrace (clipper, peakClipRows);
    }
    out = next;
    return true;
}
} // namespace

bool LandingOps::stepTraces (const mastering::LoudnessSolution& solution,
                            std::span<LandingTraceBucket> limiterRows,
                            std::span<LandingTraceBucket> peakClipRows,
                            std::uint32_t deliveryRateHz, LandingSummary& out,
                            std::uint32_t& cursor, std::uint32_t budget) noexcept
{
    std::optional<LandingTrace> none, alsoNone;
    return stepTraces (solution, limiterRows, peakClipRows, {}, {}, deliveryRateHz, out, none, alsoNone, cursor, budget);
}

bool LandingOps::stepTraces (const mastering::LoudnessSolution& solution,
                            std::span<LandingTraceBucket> limiterRows,
                            std::span<LandingTraceBucket> peakClipRows,
                            std::span<LandingTraceBucket> compressorRows,
                            std::span<LandingTraceBucket> saturationRows,
                            std::uint32_t deliveryRateHz, LandingSummary& out,
                            std::optional<LandingTrace>& compressor,
                            std::optional<LandingTrace>& saturation,
                            std::uint32_t& cursor, std::uint32_t budget) noexcept
{
    const auto& limiter = solution.limiterTrace;
    const auto& clipper = solution.peakClipTrace;
    const auto& glue = solution.compressorTrace;
    const auto& shave = solution.saturationTrace;
    const bool third = ! compressorRows.empty(), fourth = ! saturationRows.empty();
    if (deliveryRateHz == 0 || limiter.buckets < 0 || limiter.buckets != clipper.buckets
        || limiter.programmeFrames != clipper.programmeFrames
        || limiter.bucket.size() != (std::size_t) limiter.buckets
        || clipper.bucket.size() != (std::size_t) clipper.buckets
        || limiterRows.size() < limiter.bucket.size() || peakClipRows.size() < clipper.bucket.size()
        || (third && (glue.buckets != limiter.buckets || glue.programmeFrames != limiter.programmeFrames
                      || glue.bucket.size() != (std::size_t) glue.buckets || compressorRows.size() < glue.bucket.size()))
        || (fourth && (shave.buckets != limiter.buckets || shave.programmeFrames != limiter.programmeFrames
                       || shave.bucket.size() != (std::size_t) shave.buckets || saturationRows.size() < shave.bucket.size()))
        || cursor > limiter.bucket.size() || budget == 0) return false;
    const auto n = limiter.bucket.size();
    if (cursor == 0 && n > 0)
    {
        const auto start = [deliveryRateHz] (const mastering::GainReductionTrace& source,
                                              std::span<LandingTraceBucket> storage) noexcept
        {
            LandingTrace trace;
            trace.toFrame = source.programmeFrames;
            trace.sampleRateHz = deliveryRateHz;
            trace.columns = (std::uint32_t) source.buckets;
            trace.complete = source.complete; trace.valid = source.valid;
            trace.samples = source.complete ? source.samples : 0;
            trace.nonFinite = source.complete ? source.nonFinite : 0;
            trace.rows = storage.first (source.bucket.size());
            return trace;
        };
        out.limiterTrace = start (limiter, limiterRows);
        out.peakClipTrace = start (clipper, peakClipRows);
        if (third) compressor = start (glue, compressorRows);
        if (fourth) saturation = start (shave, saturationRows);
    }
    if (n > 0 && (! out.limiterTrace || ! out.peakClipTrace || (third && ! compressor) || (fourth && ! saturation))) return false;
    const auto end = std::min<std::size_t> (n, std::size_t (cursor) + budget);
    while (cursor < end)
    {
        const auto copy = [cursor] (const mastering::GainReductionTrace& source,
                                    std::span<LandingTraceBucket> storage, LandingTrace& trace) noexcept
        {
            const auto& b = source.bucket[cursor];
            const auto finite = b.samples - b.nonFinite;
            const double mean = source.complete || finite == 0 ? b.meanDb : b.meanDb / double (finite);
            storage[cursor] = { b.minDb, b.maxDb, mean, b.samples, b.nonFinite };
            if (! source.complete) { trace.samples += b.samples; trace.nonFinite += b.nonFinite; }
        };
        copy (limiter, limiterRows, *out.limiterTrace);
        copy (clipper, peakClipRows, *out.peakClipTrace);
        if (third) copy (glue, compressorRows, *compressor);
        if (fourth) copy (shave, saturationRows, *saturation);
        ++cursor;
    }
    return cursor == n;
}

std::uint32_t LandingOps::query (const LandingTrace& trace, std::uint32_t columns,
                                 std::span<double> output) noexcept
{
    return query (trace, trace.fromFrame, trace.toFrame, columns, output);
}

std::pair<std::uint32_t, std::uint32_t> LandingOps::bucketRange (
    const LandingTrace& trace, std::uint64_t fromFrame, std::uint64_t toFrame) noexcept
{
    if (trace.columns == 0 || trace.columns != trace.rows.size() || trace.fromFrame > trace.toFrame
        || fromFrame < trace.fromFrame || fromFrame >= toFrame || toFrame > trace.toFrame) return { 0, 0 };
    const auto boundary = [] (std::uint64_t length, std::uint64_t i, std::uint64_t total) noexcept
    { return (length / total) * i + ((length % total) * i) / total; };
    const auto length = trace.toFrame - trace.fromFrame;
    std::uint32_t lo = 0, hi = trace.columns;
    while (lo < hi)
    {
        const auto mid = lo + (hi - lo) / 2u;
        const auto end = trace.fromFrame + boundary (length, std::uint64_t (mid) + 1u, trace.columns);
        if (end <= fromFrame) lo = mid + 1u; else hi = mid;
    }
    const auto first = lo;
    hi = trace.columns;
    while (lo < hi)
    {
        const auto mid = lo + (hi - lo) / 2u;
        const auto start = trace.fromFrame + boundary (length, mid, trace.columns);
        if (start < toFrame) lo = mid + 1u; else hi = mid;
    }
    return { first, lo };
}

std::uint32_t LandingOps::query (const LandingTrace& trace,
                                 std::uint64_t fromFrame, std::uint64_t toFrame,
                                 std::uint32_t columns, std::span<double> output) noexcept
{
    if (! trace.complete || columns == 0 || columns > 2048 || trace.rows.empty()) return 0;
    const auto [firstBucket, lastBucket] = bucketRange (trace, fromFrame, toFrame);
    if (firstBucket == lastBucket) return 0;
    for (const auto& row : trace.rows) if (row.nonFinite > row.samples) return 0;
    const auto selected = lastBucket - firstBucket;
    const auto count = std::min<std::uint32_t> (columns, selected);
    if (output.size() < std::size_t (count) * 7u) return 0;
    const auto boundary = [] (std::uint64_t length, std::uint64_t i, std::uint64_t total) noexcept
    { return (length / total) * i + ((length % total) * i) / total; };
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto first = firstBucket + boundary (selected, i, count);
        const auto last = firstBucket + boundary (selected, i + 1u, count);
        double min = 0.0, max = 0.0, sum = 0.0;
        std::uint64_t samples = 0, nonFinite = 0;
        for (std::uint64_t j = first; j < last; ++j)
        {
            const auto& b = trace.rows[std::size_t (j)];
            const auto finite = b.samples - b.nonFinite;
            if (finite != 0 && (samples == nonFinite || b.minDb < min)) min = b.minDb;
            max = std::max (max, b.maxDb);
            sum += b.meanDb * double (finite);
            samples += b.samples; nonFinite += b.nonFinite;
        }
        const auto finite = samples - nonFinite;
        const double row[] { double (trace.fromFrame + boundary (trace.toFrame - trace.fromFrame, first, trace.columns)),
            double (trace.fromFrame + boundary (trace.toFrame - trace.fromFrame, last, trace.columns)),
            min, max, finite == 0 ? 0.0 : sum / double (finite), double (samples), double (nonFinite) };
        std::copy_n (row, 7, output.data() + std::size_t (7u * i));
    }
    return count;
}
} // namespace felitronics::session
