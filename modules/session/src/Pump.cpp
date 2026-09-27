// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "Driver.h"
#include "Rules.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>
#include <algorithm>

namespace felitronics::session
{
namespace
{
double weight (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    return 0.0; // the build's config gate establishes all the weights
}
}

std::uint64_t Session::stepBytes() noexcept { return 0; }
JobId Session::measurementJob() const noexcept { return measurementJob_; }
bool Session::hasWork() const noexcept { return measurementJob_ != 0 || job_ != 0; }
std::span<const Notification> Session::events() const noexcept { return { events_, eventCount_ }; }
void Session::emit (Notification event) noexcept
{
    event.seq = ++sequence_;
    events_[eventCount_++] = event; // at most three per unit, or one per command
}

Stepped Session::step (std::uint32_t budget) noexcept
{
    eventCount_ = 0;
    if (checkFloatingPointEnvironment() != Status::Ok)
    {
        Notification event;
        event.jobId = job_ != 0 ? job_ : measurementJob_;
        event.kind = EventKind::Error;
        event.payload.error.code = ErrorCode::Contract;
        event.payload.error.fact.id = FactId::FloatingPointEnvironment;
        emit (event);
        return { hasWork() ? StepState::More : StepState::Done, 0, true };
    }
    const auto progress = detail::rules().engine.find ("progress");
    const auto weights = progress.find ("analysis").find ("weights");
    const double analysis[] = {
        weight (weights.find ("loudness")), weight (weights.find ("report")), weight (weights.find ("lowEnd120")),
        weight (weights.find ("forensics")), weight (weights.find ("stereo")), weight (weights.find ("lowEndSweep")),
        weight (weights.find ("stereoBursts")), weight (weights.find ("crest")), weight (weights.find ("hum")),
        weight (weights.find ("tempo"))
    };
    double total = 0.0;
    for (double w : analysis) total += w;
    const auto master = progress.find ("master");
    const auto passes = std::uint32_t (*master.find ("expectedPasses").integer());
    const double passWeight = weight (master.find ("passWeight"));
    const double measureWeight = weight (master.find ("measureWeight"));
    const auto version = config::Config::versions().all;
    std::uint32_t units = 0;
    // The foreground master takes priority over phase two; phase two resumes after it.
    while (units < std::min (budget, kStepUnits) && hasWork())
    {
        Notification event;
        event.kind = EventKind::Phase;
        if (job_ != 0)
        {
            event.jobId = job_;
            ++masterUnit_;
            const bool end = masterUnit_ > passes;
            masterProgress_ = { end ? PhaseName::Remeasure : PhaseName::Pass,
                end ? 1.0 : double (masterUnit_) * passWeight / (double (passes) * passWeight + measureWeight),
                version, end ? 0 : masterUnit_, passes, masterUnit_, passes + 1 };
            event.payload.phase = masterProgress_;
            emit (event);
            event.kind = EventKind::Fact;
            event.payload.fact.id = end ? FactId::MasterReady : FactId::MasterPass;
            event.payload.fact.count = 1;
            event.payload.fact.args[0].kind = ArgKind::Count;
            event.payload.fact.args[0].count = end ? event.jobId : masterUnit_;
            emit (event);
            if (end && detail::Driver::mastered (*this, event.jobId))
            {
                event.kind = EventKind::Done;
                event.payload.done.masterId = event.jobId;
                emit (event);
            }
        }
        else
        {
            event.jobId = measurementJob_;
            ++measurementUnit_;
            double completed = 0.0;
            for (std::uint32_t i = 0; i < measurementUnit_; ++i) completed += analysis[i];
            measurementProgress_ = { measurementUnit_ == 1 ? PhaseName::Stream
                : measurementUnit_ == 2 ? PhaseName::Report : PhaseName::Analyzers,
                completed / total, version, 0, 0, measurementUnit_, 10 };
            event.payload.phase = measurementProgress_;
            emit (event);
            if (measurementUnit_ == 5 || measurementUnit_ == 10)
            {
                const bool first = measurementUnit_ == 5;
                const bool ended = first ? detail::Driver::measured1 (*this, event.jobId, source_.hash)
                                         : detail::Driver::measured2 (*this, event.jobId, source_.hash);
                if (ended)
                {
                    event.kind = EventKind::Fact;
                    event.payload.fact.id = first ? FactId::Measurement1 : FactId::Measurement2;
                    emit (event);
                }
            }
        }
        ++units;
    }
    return { hasWork() ? StepState::More : StepState::Done, units, false };
}
} // namespace felitronics::session
