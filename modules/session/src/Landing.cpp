// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include <felitronics/session/Landing.h>

#include <cmath>

namespace felitronics::session
{
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
    plan.initialCeilingDbTp = targetTruePeakDbTp - engine.limiter.ceilingMarginDb;
    plan.sourceRateImpactPass = ! core::exactlyEqual (sourceRate, deliveryRate);
    return plan;
}

bool LandingOps::summarize (const mastering::LoudnessSolution& solution,
                       std::span<LandingPass> rows, LandingSummary& out) noexcept
{
    if (solution.passes < 0 || solution.passes > 12 || solution.logCount != solution.passes
        || rows.size() < (std::size_t) solution.logCount) return false;
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
    out = next;
    return true;
}
} // namespace felitronics::session
