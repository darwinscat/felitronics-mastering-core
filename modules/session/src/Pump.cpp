// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "BuildContract.h"
#include "Driver.h"
#include "LiveMeasurements.h"
#include "SourceMeasurements.h"
#include "QueryState.h"
#include "MasterJob.h"
#include "Planner.h"
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
    detail::storageOverflow();
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
bool Session::hasWork() const noexcept { return measurementJob_ != 0 || job_ != 0 || needlesJob_ != 0 || crestJoin_; }
std::span<const Notification> Session::events() const noexcept { return { events_->data(), eventCount_ }; }
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
    if (event.kind == EventKind::Measurement)
    {
        invalidateQueryCache (event.payload.measurement.analyzer);
        if (event.payload.measurement.analyzer == Analyzer::Crest
            && event.payload.measurement.status == MeasurementStatus::Unavailable)
            settleMasterCrest (event.payload.measurement.reason);
        else if (event.payload.measurement.analyzer == Analyzer::Crest
                 && event.payload.measurement.status == MeasurementStatus::Ready)
        { crestJoin_ = masterCount_ != 0; crestJoinIndex_ = 0; }
    }
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

    (*events_)[eventCount_++] = event; // three per ordinary unit; a master completion has ten extra slots
}

void Session::preferTempo (std::uint32_t waiting) noexcept
{
    if (! sourceMeasurements_) return;
    auto& run = *sourceMeasurements_;
    constexpr unsigned tempo = unsigned (detail::SourceMeasurements::order.size()) - 1u;
    static_assert (detail::SourceMeasurements::order[tempo] == Analyzer::Tempo, "tempo is the source job's last analyzer");
    // Never ahead of a run the devices read themselves (the low end): only ahead of a finding.
    if (run.firstPublished && run.cursor >= run.firstCount && run.cursor < tempo && run.stage == 0
        && (waiting & detail::bitOf (run.order[run.cursor])) == 0
        && measurementResults_[std::size_t (Analyzer::Tempo)].status == MeasurementStatus::Pending)
    {
        run.tempoReturnCursor = run.cursor;
        run.cursor = tempo;
    }
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
        if (measurementResults_[std::size_t (Analyzer::Crest)].status == MeasurementStatus::Ready)
        { crestJoin_ = masterCount_ != 0; crestJoinIndex_ = 0; }
        else settleMasterCrest (MeasurementReason::Cancelled);
        // A master waiting for a measurement of the source's job stops with it: it cannot end without it — and says
        // so under its own job's id, the measurement's stop being another job's fact.
        if (job_ != 0 && jobWaiting_ && (planWaiting (jobRecipe_.project) & ~detail::bitOf (Analyzer::Excursions)) != 0)
        {
            Notification event;
            event.jobId = job_;
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::Cancelled));
            const auto progress = masterProgress_;
            mastering_ = false; job_ = 0; jobRecipe_ = {}; jobWaiting_ = false;
            masterUnit_ = 0; masterProgress_ = {};
            emit (event, progress);
            needlesAfterDroppedMaster();
        }
    }
    else
    {
        if (masterJob_) masterJob_->search.cancel();
        masterJob_.reset(); masterJobBytes_ = 0;
        masterSummary_ = {}; masterTraceCursor_ = 0; masterTraceActive_ = false;
        if (masterRows_ && masterCount_ < masterRoom_) masterRows_[masterCount_] = {};
        mastering_ = false;
        job_ = 0;
        jobRecipe_ = {};
        jobWaiting_ = false;
        masterUnit_ = 0;
        masterProgress_ = {};
        needlesAfterDroppedMaster();
    }
}
// WHEN A SOURCE MEASUREMENT ENDS the machine's layer is placed again: a field it decides — the high-pass's cutoff,
// mono bass's tick — is "not measured yet" until it ends, and the machine fills it then (owner, 02.10). A person's
// layer stays (placement writes the machine's alone); a file's machine layer is the file's (law 14). A waiting master's
// recipe is placed again with it, on its own target, until its wait ends: it sounds as one asked after the measurement.
void Session::placeAgain (Analyzer ended) noexcept
{
    if (ended == Analyzer::Excursions || ended == Analyzer::Waveform) return;
    if (devicesPlaced_ && ! machineFromFile_) { place (project_); refreshEqCurve(); }
    if (job_ != 0 && jobWaiting_ && ! jobMachineFromFile_) place (jobRecipe_.project);
}
// A WAITING MASTER HELD THE NEEDLES AT ITS OWN CEILING (requestNeedles reads the recipe it captured). Dropped — by its
// own cancel, with a stopped measurement, or by a contract fault — it leaves them there, and the project's plan would
// wait for ever for needles nobody measures: they go back to the project's ceiling, as they do when a wait ends.
void Session::needlesAfterDroppedMaster() noexcept
{
    if (source_.channels != 0 && ! planInputs (project_).needlesCurrent) requestNeedles();
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
            if (! masterJob_)
            {
                // WAITING: the recipe was captured when the master was asked for; what its devices read ends first —
                // the needles at its own ceiling, then what the source's job measures (tempo ahead of the optional
                // analyzers). A needed measurement that cannot run is a contract fault, never a master that waits for ever.
                const auto waits = jobWaiting_ ? planWaiting (jobRecipe_.project) : 0u;
                if (waits != 0)
                {
                    const auto masterJob = job_;
                    if ((waits & detail::bitOf (Analyzer::Excursions)) != 0)
                    {
                        if (needlesJob_ == 0) requestNeedles();
                        if (needlesJob_ != 0) stepNeedles();
                        else if ((planWaiting (jobRecipe_.project) & detail::bitOf (Analyzer::Excursions)) != 0) contract (masterJob);
                    }
                    else if (measurementJob_ == 0 && detail::Driver::continueMeasurement (*this) == 0)
                        contract (masterJob);
                    else if (! sourceMeasurements_ || sourceMeasurements_->cursor > sourceMeasurements_->order.size()
                             || sourceMeasurements_->stage > 5)
                        contract (masterJob);
                    else
                    {
                        preferTempo (waits);
                        stepSourceMeasurements();
                    }
                    if (job_ == masterJob)
                    {
                        auto& p = masterProgress_;
                        p.name = PhaseName::Analyzers; p.pass = 0; p.totalPasses = passes;
                        if (p.completedUnits < 4294967294u) ++p.completedUnits;
                        p.totalUnits = std::max (p.totalUnits, p.completedUnits + 1u);
                        p.fraction = measureWeight * double (p.completedUnits) /
                            (double (p.totalUnits) * (double (passes) * passWeight + measureWeight));
                        event.kind = EventKind::Phase; event.jobId = masterJob; event.payload.phase = p; emit (event);
                    }
                    replan();
                    ++units;
                    continue;
                }
                // WHAT THE RECIPE'S DEVICES READ HAS ENDED: the master's chain is taken from its project's devices on
                // those measurements — the needles still at the recipe's own ceiling — its demand is checked against
                // the heap as it is now, and its job starts. Then the needles go back to the project's own ceiling,
                // where it is another than the recipe's (a cancelled job at the same ceiling is not built again).
                const auto masterJob = job_;
                const auto plan = jobWaiting_ ? detail::MasterJob::plan (*this, command::Master {}, jobRecipe_.project)
                                              : detail::MasterPlan {};
                if (plan.rejection != Rejection::None) { contract (masterJob); ++units; continue; }
                const auto roomed = demand ({ Rejection::None, kNoField, plan.bytes, 0, plan.largestBlock });
                if (roomed.rejection != Rejection::None)
                {
                    const auto stoppedProgress = masterProgress_;
                    Notification error;
                    error.jobId = masterJob;
                    error.kind = EventKind::Error;
                    error.payload.error.code = ErrorCode::Memory;
                    error.payload.error.needBytes = roomed.needBytes;
                    (void) error.payload.error.fact.assign (text::Fact::of (text::FactId::SessionMemory));
                    dropJob (masterJob);
                    ++revision_;
                    emit (error, stoppedProgress);
                    ++units;
                    continue;
                }
                startMaster (plan);
                if (! planInputs (project_).needlesCurrent) requestNeedles();
                event.payload.phase = masterProgress_;
                emit (event);
                ++revision_;
                ++units;
                continue;
            }
            const auto beforePass = masterJob_->search.startedPasses();
            const auto outcome = masterTraceActive_ ? mastering::StepResult::Done : masterJob_->step (1024);
            ++masterUnit_;
            const bool end = outcome != mastering::StepResult::More;
            const auto total = std::uint32_t (std::min<std::uint64_t> (
                4294967295u, (source_.frames / 1024u + 2u * std::uint64_t (masterJob_->frames) / 1024u + 64u) * 12u));
            masterProgress_ = { end ? PhaseName::Final : PhaseName::Pass,
                end ? 1.0 : std::min (0.99, double (masterUnit_) / double (std::max (1u, total))),
                config::Config::versions().all, std::uint32_t (masterJob_->search.startedPasses()), 12,
                masterUnit_, total };
            event.payload.phase = masterProgress_;
            emit (event);
            if (masterJob_->search.startedPasses() != beforePass)
            {
                event.kind = EventKind::Fact;
                (void) event.payload.fact.assign (text::Fact::of (text::FactId::MasterPass,
                    text::Arg::count (masterJob_->search.startedPasses())));
                emit (event, masterProgress_);
            }
            if (end)
            {
                auto& rows = masterRows_[masterCount_];
                const auto& solution = masterJob_->result();
                const auto trace = std::span<LandingTraceBucket> (rows.traces.get(), rows.traceCapacity);
                const auto clip = std::span<LandingTraceBucket> (rows.traces.get() + rows.traceCapacity, rows.traceCapacity);
                if (! masterTraceActive_)
                {
                    if (! LandingOps::summarize (solution, { rows.passes.get(), 12 }, masterSummary_))
                    { contract (event.jobId); ++units; continue; }
                    masterTraceCursor_ = 0; masterTraceActive_ = true;
                }
                const auto beforeTrace = masterTraceCursor_;
                const bool tracesDone = LandingOps::stepTraces (solution, trace, clip,
                    masterJob_->deliveryRate, masterSummary_, masterTraceCursor_, 1024);
                if (! tracesDone)
                {
                    if (beforeTrace == masterTraceCursor_) contract (event.jobId);
                    ++units; continue;
                }
                const auto completedJob = job_;
                if (! detail::Driver::mastered (*this, completedJob)) { contract (completedJob); ++units; continue; }
                masters_[masterCount_ - 1].report = masterJob_->reportResult();
                // A crest joined inside the job is complete: mask copied, five cost bands scanned. Marked so a later
                // join (a cancelled or finished source measurement) passes over it instead of publishing it again.
                if (masters_[masterCount_ - 1].report->crest.status == MeasurementStatus::Ready)
                { rows.crestMaskCopied = std::size_t (masters_[masterCount_ - 1].report->crest.blocks); rows.crestCostBand = 5; }
                masterSummary_.deliverable = masterSummary_.deliverable
                    && masters_[masterCount_ - 1].report->deliverable;
                masters_[masterCount_ - 1].landing = masterSummary_;
                masters_[masterCount_ - 1].report->readings
                    = MasterReportText::readings (*masters_[masterCount_ - 1].report, masterSummary_.passes);
                // The landing's verdict, its own line and what stood behind a miss: the report's facts, the core's to
                // say — a shell composes none of them from the report's fields.
                {
                    const auto& report = *masters_[masterCount_ - 1].report;
                    event.kind = EventKind::Fact;
                    // The tolerance the job landed to: the config's, as MasterJob::plan read it into the request.
                    const auto tolerance = detail::rules().engine.find ("landing").find ("toleranceLu");
                    const double toleranceLu = tolerance.decimal() ? tolerance.decimal()->toDouble()
                                                                   : double (tolerance.integer().value_or (0));
                    if (const auto verdict = MasterReportText::landing (report, masterSummary_, toleranceLu))
                    { (void) event.payload.fact.assign (*verdict); emit (event, masterProgress_); }
                    // Delivered above the ceiling (no render stayed under it): the mark, beside the verdict.
                    if (const auto above = MasterReportText::peaksAboveCeiling (report))
                    { (void) event.payload.fact.assign (*above); emit (event, masterProgress_); }
                    if (const auto miss = MasterReportText::miss (report))
                    { (void) event.payload.fact.assign (*miss); emit (event, masterProgress_); }
                    for (const auto* hint : { &report.firstHint, &report.secondHint })
                        if (*hint)
                            if (const auto said = MasterReportText::hint (**hint))
                            { (void) event.payload.fact.assign (*said); emit (event, masterProgress_); }
                }
                // The crest's line, once: here when the report settles it (joined inside the job, or unavailable); a
                // crest still pending is said by the late join (stepMasterCrestJoin), which passes over a settled one.
                if (masters_[masterCount_ - 1].report->crest.status != MeasurementStatus::Pending)
                {
                    event.kind = EventKind::Fact;
                    (void) event.payload.fact.assign (MasterReportText::crest (masters_[masterCount_ - 1].report->crest));
                    emit (event, masterProgress_);
                }
                if (const auto& cost = masters_[masterCount_ - 1].report->cost)
                {
                    event.kind = EventKind::Fact;
                    (void) event.payload.fact.assign (MasterReportText::shape (*cost)); emit (event, masterProgress_);
                    // Each of the cost's other lines where its numbers were measured, beside the one it details.
                    const auto say = [&] (const std::optional<text::Fact>& said) noexcept
                    { if (said) { (void) event.payload.fact.assign (*said); emit (event, masterProgress_); } };
                    say (MasterReportText::section (*cost));
                    say (MasterReportText::sections (*cost));
                    if (cost->crestFullDb.value)
                    { (void) event.payload.fact.assign (MasterReportText::impact (*cost)); emit (event, masterProgress_); }
                    say (MasterReportText::bands (*cost));
                    if (cost->pumpingRmsDb.value)
                    { (void) event.payload.fact.assign (MasterReportText::pumping (*cost)); emit (event, masterProgress_); }
                    say (MasterReportText::limiter (*cost));
                    say (MasterReportText::active (*cost));
                    (void) event.payload.fact.assign (MasterReportText::tonal()); emit (event, masterProgress_);
                    if (const auto glue = MasterReportText::glue (*cost))
                    { (void) event.payload.fact.assign (*glue); emit (event, masterProgress_); }
                    if (const auto saturation = MasterReportText::saturation (*cost))
                    { (void) event.payload.fact.assign (*saturation); emit (event, masterProgress_); }
                }
                // The medium's lines and a very quiet input's: what the master is ready for, what the file shows of
                // it and what it cannot show.
                {
                    const auto& report = *masters_[masterCount_ - 1].report;
                    event.kind = EventKind::Fact;
                    const auto departures = MasterReportText::vinylDepartures (report);
                    for (const auto& said : { MasterReportText::vinyl (report), departures[0], departures[1], departures[2],
                                              departures[3], MasterReportText::vinylChecked (report),
                                              MasterReportText::vinylUncheckable (report), MasterReportText::quietInput (report) })
                        if (said) { (void) event.payload.fact.assign (*said); emit (event, masterProgress_); }
                }
                if (masterSummary_.deliverable && outcome == mastering::StepResult::Done)
                {
                    // The job's own resolved depth: the recipe was handed to masters_ above and jobRecipe_ is empty.
                    masterAudioBits_ = masterJob_->ready.deliveryBits;
                    masterAudio_ = { masterJob_->takeOutput(), std::uint64_t (masterJob_->frames),
                        std::uint32_t (masterJob_->channels), masterJob_->deliveryRate };
                    pendingMaster_ = { source_.hash, revision_, completedJob, completedJob };
                }
                masterJob_.reset(); masterJobBytes_ = 0;
                masterSummary_ = {}; masterTraceCursor_ = 0; masterTraceActive_ = false;
                if (masters_[masterCount_ - 1].landing->deliverable)
                {
                    event.kind = EventKind::Fact;
                    (void) event.payload.fact.assign (text::Fact::of (text::FactId::MasterReady));
                    emit (event, masterProgress_);
                }
                event.kind = EventKind::Done;
                event.payload.done.masterId = completedJob;
                emit (event, masterProgress_);
            }
        }
        else if (crestJoin_)
        {
            stepMasterCrestJoin();
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
            else
            {
                // The measurements the devices read go ahead of the optional findings: a needed tempo first.
                if ((plan_.waiting & detail::bitOf (Analyzer::Tempo)) != 0) preferTempo (plan_.waiting);
                stepSourceMeasurements();
            }
        }
        else if (measurementsFromSidecar_ && measurementJob_ != 0)
        {
            measurementJob_ = 0;
            measurementProgress_ = { PhaseName::Analyzers, 1.0, config::Config::versions().all, 0, 0, 1, 1 };
            ++revision_;
        }
        else contract (measurementJob_);
        ++units;
    }
    replan();
    return { hasWork() ? StepState::More : StepState::Done, units, false };
}
} // namespace felitronics::session
