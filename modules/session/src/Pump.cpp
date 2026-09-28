// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "BuildContract.h"
#include "Driver.h"
#include "LiveMeasurements.h"
#include "SourceMeasurements.h"
#include "QueryState.h"
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
    emit (event, event.kind == EventKind::Phase ? event.payload.phase : jobProgress (event.jobId));
}
Phase Session::jobProgress (JobId job) const noexcept
{
    if (job == 0) return {};
    if (job == job_) return masterProgress_;
    if (job == needlesJob_) return needlesProgress_;
    if (job == measurementJob_) return measurementProgress_;
    return {};
}
void Session::emit (Notification event, const Phase& progress) noexcept
{
    if (event.kind == EventKind::Measurement) invalidateQueryCache (event.payload.measurement.analyzer);
    else if (event.kind == EventKind::Reading)
    {
        invalidateQueryCache (Analyzer::Loudness);
        invalidateQueryCache (Analyzer::Clipping);
    }
    detail::debugBound (eventCount_ < kEventBatch);
    event.seq = ++sequence_;
    event.source = source_.hash;
    event.state = state_;
    event.revision = revision_;
    event.phase = progress.name;
    event.completedWork = progress.completedUnits;
    event.totalWork = progress.totalUnits;

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
        stoppedState_ = state_; state_ = State::MeasurementStopped;
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
    const auto master = progress.find ("master");
    const auto passes = std::uint32_t (*master.find ("expectedPasses").integer());
    const double passWeight = weight (master.find ("passWeight"));
    const double measureWeight = weight (master.find ("measureWeight"));
    const auto version = config::Config::versions().all;
    std::uint32_t units = 0;
    const auto contract = [&] (JobId job)
    {
        const auto stoppedProgress = jobProgress (job);
        Notification error;
        error.jobId = job;
        error.kind = EventKind::Error;
        error.payload.error.code = ErrorCode::Contract;
        (void) error.payload.error.fact.assign (text::Fact::of (text::FactId::SessionContract));
        dropJob (job);
        ++revision_;
        emit (error, stoppedProgress);
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
                emit (event, masterProgress_);
                if (end)
                {
                    event.kind = EventKind::Done;
                    event.payload.done.masterId = event.jobId;
                    emit (event, masterProgress_);
                }
            }
        }
        else if (needlesJob_ != 0)
        {
            stepNeedles();
        }
        else if (waveform_ && ! waveform_->finished)
        {
            stepWaveform();
        }
        else if (liveMeasurements_ && liveMeasurements_->stage < 9 && measurementUnit_ < 2)
        {
            stepMeasurements();
        }
        else if (sourceMeasurements_)
        {
            if (sourceMeasurements_->cursor > sourceMeasurements_->order.size() || sourceMeasurements_->stage > 5)
                contract (measurementJob_);
            else stepSourceMeasurements();
        }
        else contract (measurementJob_);
        ++units;
    }
    return { hasWork() ? StepState::More : StepState::Done, units, false };
}
} // namespace felitronics::session
