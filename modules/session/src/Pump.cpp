// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "BuildContract.h"
#include "Driver.h"
#include "Rules.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>
#include <algorithm>
#include <iterator>

namespace felitronics::session
{
namespace
{
double weight (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    detail::storageOverflow(); // the build gate requires every weight
}
}

bool OwnedFact::assign (const text::Fact& fact) noexcept
{
    if (fact.argCount > text::Fact::kMaxArgs) return false;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < fact.argCount; ++i)
        if (fact.args[i].kind == text::ArgKind::UserText)
        {
            if (fact.args[i].userText.size() > kTextCapacity - bytes) return false;
            bytes += fact.args[i].userText.size();
        }
    // A temporary also handles assigning a view of this very value.
    OwnedFact out;
    out.fact_ = fact;
    bytes = 0;
    for (std::size_t i = 0; i < text::Fact::kMaxArgs; ++i)
    {
        const auto& arg = fact.args[i];
        if (i < fact.argCount && arg.kind == text::ArgKind::UserText)
        {
            out.lengths_[i] = arg.userText.size();
            std::copy (arg.userText.begin(), arg.userText.end(), out.text_ + bytes);
            bytes += arg.userText.size();
        }
        out.fact_.args[i].userText = {};
    }
    *this = out;
    return true;
}
text::Fact OwnedFact::view() const noexcept
{
    text::Fact out = fact_;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < out.argCount; ++i)
        if (out.args[i].kind == text::ArgKind::UserText)
        {
            out.args[i].userText = { text_ + offset, lengths_[i] };
            offset += lengths_[i];
        }
    return out;
}

std::uint64_t Session::stepBytes() noexcept { return 0; }
JobId Session::measurementJob() const noexcept { return measurementJob_; }
bool Session::hasWork() const noexcept { return measurementJob_ != 0 || job_ != 0 || needlesJob_ != 0; }
std::span<const Notification> Session::events() const noexcept { return { events_, eventCount_ }; }
void Session::emit (Notification event) noexcept
{
    detail::debugBound (eventCount_ < kEventBatch);
    event.seq = ++sequence_;
    events_[eventCount_++] = event; // at most three per unit, or one per command
}

void Session::dropJob (JobId job) noexcept
{
    if (job == needlesJob_ && job != 0)
    {
        clearNeedles();
        auto& result = measurementResults_[std::size_t (Analyzer::Excursions)];
        result.status = MeasurementStatus::Cancelled;
        result.reason = MeasurementReason::Cancelled;
    }
    else if (job == measurementJob_)
    {
        measurementJob_ = 0;
        for (auto& result : measurementResults_)
            if (result.analyzer != Analyzer::Excursions && result.status == MeasurementStatus::Pending)
            {
                result.status = MeasurementStatus::Cancelled;
                result.reason = MeasurementReason::Cancelled;
            }
    }
    else
    {
        mastering_ = false;
        job_ = 0;
        jobRecipe_ = {};
        masterUnit_ = 0;
        masterProgress_ = {};
    }
}

Stepped Session::step (std::uint32_t budget) noexcept
{
    eventCount_ = 0;
    // With no work there is no arithmetic to refuse and no publication to number.
    if (! hasWork()) return { StepState::Done, 0, false };
    if (checkFloatingPointEnvironment() != Status::Ok)
    {
        Notification event;
        event.jobId = job_ != 0 ? job_ : needlesJob_ != 0 ? needlesJob_ : measurementJob_;
        event.kind = EventKind::Error;
        event.payload.error.code = ErrorCode::Refusal;
        (void) event.payload.error.fact.assign (text::Fact::of (text::FactId::SessionRefusal));
        event.payload.error.recover = Recover::Continue;
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
    const auto contract = [&] (JobId job)
    {
        Notification error;
        error.jobId = job;
        error.kind = EventKind::Error;
        error.payload.error.code = ErrorCode::Contract;
        (void) error.payload.error.fact.assign (text::Fact::of (text::FactId::SessionContract));
        dropJob (job);
        ++revision_;
        emit (error);
    };
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
            const bool ended = ! end || detail::Driver::mastered (*this, event.jobId);
            if (! ended) contract (event.jobId);
            else
            {
                event.kind = EventKind::Fact;
                (void) event.payload.fact.assign (end ? text::Fact::of (text::FactId::MasterReady)
                    : text::Fact::of (text::FactId::MasterPass, text::Arg::count (masterUnit_)));
                emit (event);
                if (end)
                {
                    event.kind = EventKind::Done;
                    event.payload.done.masterId = event.jobId;
                    emit (event);
                }
            }
        }
        else if (needlesJob_ != 0)
        {
            stepNeedles();
        }
        else
        {
            event.jobId = measurementJob_;
            if (measurementUnit_ >= std::size (analysis))
            {
                contract (event.jobId);
                ++units;
                continue;
            }
            ++measurementUnit_;
            double completed = 0.0;
            for (std::size_t i = 0; i < std::size_t (measurementUnit_); ++i)
                completed += analysis[i];
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
                    (void) event.payload.fact.assign (text::Fact::of (first ? text::FactId::Measurement1 : text::FactId::Measurement2));
                    emit (event);
                }
                else contract (event.jobId);
            }
        }
        ++units;
    }
    return { hasWork() ? StepState::More : StepState::Done, units, false };
}
} // namespace felitronics::session
