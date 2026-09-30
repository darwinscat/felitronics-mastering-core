// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include <felitronics/session/Landing.h>
#include <felitronics/analysis/BandCrestResult.h>

#include <cmath>
#include <algorithm>

namespace felitronics::session
{
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
    if (report.status != MeasurementStatus::Ready || ! report.deliverable || report.targetMet
        || ! report.achievedLufs || ! report.missLu || ! std::isfinite (*report.missLu)
        || std::fabs (*report.missLu) <= 0.0) return {};
    return text::Fact::of (*report.missLu < 0 ? text::FactId::MasterLandingMiss
                                              : text::FactId::MasterLandingAbove,
        text::Arg::value (*report.achievedLufs, text::Unit::Lufs, 1),
        text::Arg::value (report.targetLufs, text::Unit::Lufs, 1),
        text::Arg::value (std::fabs (*report.missLu), text::Unit::Lu, 1));
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

bool LandingOps::summarize (const mastering::LoudnessSolution& solution,
                       std::span<LandingPass> rows, LandingSummary& out) noexcept
{
    return summarize (solution, rows, {}, {}, 0, out);
}

bool LandingOps::summarize (const mastering::LoudnessSolution& solution,
                       std::span<LandingPass> rows, std::span<LandingTraceBucket> limiterRows,
                       std::span<LandingTraceBucket> peakClipRows, std::uint32_t deliveryRateHz,
                       LandingSummary& out) noexcept
{
    if (solution.passes < 0 || solution.passes > 12 || solution.logCount != solution.passes
        || rows.size() < (std::size_t) solution.logCount) return false;
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
    next.deliverable = solution.deliverable;
    next.passes = (std::uint32_t) solution.passes;
    next.workUnits = solution.workUnits;
    if (solution.deliverable)
    {
        if (! std::isfinite (solution.achievedLufs) || ! std::isfinite (solution.missLu)
            || ! std::isfinite (solution.distanceLu) || ! std::isfinite (solution.measured.truePeakDbTp)) return false;
        next.achievedLufs = solution.achievedLufs;
        next.missLu = solution.missLu;
        next.distanceLu = solution.distanceLu;
        next.truePeakDbTp = solution.measured.truePeakDbTp;
    }
    if (std::isfinite (solution.sourceSubBassShare)) next.sourceSubBassShare = solution.sourceSubBassShare;
    if (std::isfinite (solution.sourcePresenceShare)) next.sourcePresenceShare = solution.sourcePresenceShare;
    if (std::isfinite (solution.limiterMeanReductionDb)) next.limiterMeanReductionDb = solution.limiterMeanReductionDb;
    for (int i = 0; i < solution.logCount; ++i)
    {
        const mastering::SolvePassRecord& source = solution.log[i];
        LandingPass& row = rows[(std::size_t) i];
        row.gainDb = source.gainDb; row.ceilingDbTp = source.ceilingDb;
        row.achievedLufs = source.integratedLufs; row.truePeakDbTp = source.truePeakDbTp;
        row.limiterMaxReductionDb = source.limiterMaxGrDb;
        row.ceilingSafe = (source.violated & mastering::constraintBit (mastering::MasteringConstraint::TruePeakCeiling)) == 0;
    }
    next.log = { rows.data(), (std::size_t) solution.logCount };
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

bool LandingOps::stepTraces (const mastering::LoudnessSolution& solution,
                            std::span<LandingTraceBucket> limiterRows,
                            std::span<LandingTraceBucket> peakClipRows,
                            std::uint32_t deliveryRateHz, LandingSummary& out,
                            std::uint32_t& cursor, std::uint32_t budget) noexcept
{
    const auto& limiter = solution.limiterTrace;
    const auto& clipper = solution.peakClipTrace;
    if (deliveryRateHz == 0 || limiter.buckets < 0 || limiter.buckets != clipper.buckets
        || limiter.programmeFrames != clipper.programmeFrames
        || limiter.bucket.size() != (std::size_t) limiter.buckets
        || clipper.bucket.size() != (std::size_t) clipper.buckets
        || limiterRows.size() < limiter.bucket.size() || peakClipRows.size() < clipper.bucket.size()
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
    }
    if (n > 0 && (! out.limiterTrace || ! out.peakClipTrace)) return false;
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
