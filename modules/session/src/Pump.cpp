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
#include "Dynamics.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>
#include <algorithm>
#include <iterator>
#include <limits>

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
bool Session::hasWork() const noexcept
{ return measurementJob_ != 0 || job_ != 0 || needlesJob_ != 0 || lateMasterJob_ || crestJoin_ || damageCount_ != 0 || queueReady(); }
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
    if (job == damageJobId_) return damageProgress_;
    if (const auto at = damageIndex (job); at < damageCount_) return damageJobs_[at].progress;
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
    if (event.kind == EventKind::Phase)
        for (std::size_t i = 0; i < eventCount_; ++i)
            if ((*events_)[i].kind == EventKind::Phase && (*events_)[i].jobId == event.jobId)
            {
                // A phase describes state, not a transition. Keep its latest state at the point it became latest while
                // every reading, fact, result and terminal event retains its order. This batch owns the tail of the
                // global sequence, so close the removed number as well.
                for (std::size_t j = i + 1; j < eventCount_; ++j)
                {
                    (*events_)[j - 1] = (*events_)[j];
                    --(*events_)[j - 1].seq;
                }
                --eventCount_;
                --sequence_;
                if (oldWorldEvents_ > i) --oldWorldEvents_;
                break;
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
    if (run.firstPublished && run.cursor < tempo && run.stage == 0
        && (waiting & detail::bitOf (run.order[run.cursor])) == 0
        && measurementResults_[std::size_t (Analyzer::Tempo)].status == MeasurementStatus::Pending)
    {
        run.tempoReturnCursor = run.cursor;
        run.cursor = tempo;
    }
}
void Session::dropJob (JobId job) noexcept
{
    if (job != 0 && job == lateMasterId_)
    { cancelLateMasterCrest (MeasurementReason::Cancelled, false); return; }
    // The damage's job ends as its own (cancel says its fact first and ends it itself); never as the master's below.
    if (const auto at = damageIndex (job); job != 0 && at < damageCount_) { endDamage (at, MeasurementReason::Cancelled, true); return; }
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
            noteMasterJob (job_, MasterJobState::Cancelled);
            mastering_ = false; job_ = 0; jobRecipe_ = {}; jobWaiting_ = false; jobMasterAnyway_ = false;
            jobBudgetResolutionDb_.reset();
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
        noteMasterJob (job_, MasterJobState::Failed);
        mastering_ = false;
        job_ = 0;
        jobRecipe_ = {};
        jobWaiting_ = false;
        jobMasterAnyway_ = false;
        jobBudgetResolutionDb_.reset();
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

// THE DAMAGE'S JOBS. Asked by the shell (command::GradeDamage) for a master kept; what its walks need to start — the
// master's winning parameters and the walks' plan — is the master's own (MasterRows), and the walks are begun when its
// turn comes.
// A grade's progress before its walks: two walks of the source a block a unit, each with the chains' latency after it,
// and the gate between them.
Phase Session::damageWaitingProgress() const noexcept
{
    const auto walk = (source_.frames + 1023u) / 1024u + 2u;
    const auto total = std::uint32_t (std::min<std::uint64_t> (4294967295u, 2u * walk + 16u));
    return { PhaseName::Reference, 0.0, config::Config::versions().all, 0, 0, 0, total, std::nullopt };
}
std::size_t Session::damageIndex (JobId job) const noexcept
{
    for (std::size_t i = 0; i < damageCount_; ++i)
        if (damageJobs_[i].job == job) return i;
    return damageCount_;
}
// THE FIRST WAITING GRADE TAKES ITS TURN: its walks' room checked against the capacity as it stands now — a master kept
// since, a smaller capacity set — and refused, said with MeasurementReason::Memory, where it is not there; else its job
// is made (src/Damage.h: the chains at 48 kHz, PEAQ, the meters are begun by its first step, in the same unit).
bool Session::startDamage() noexcept
{
    const Kept* const first = masters_.get();
    const auto index = std::size_t (std::find_if (first, first + masterCount_,
        [&] (const Kept& k) { return k.id == damageJobs_[0].masterId; }) - first);
    detail::debugBound (index < masterCount_ && masterRows_[index].damageGradable);
    const auto& rows = masterRows_[index];
    const auto bytes = detail::DamageJob::bytes (rows.damagePlan);
    const Checked room = demand ({ Rejection::None, kNoField, bytes, 0, rows.damagePlan.largestBlock });
    if (room.rejection != Rejection::None)
    {
        endDamage (0, MeasurementReason::Memory, true);
        return false;
    }
    damageJob_.reset (new detail::DamageJob);
    damageJob_->master = damageJobs_[0].masterId;
    damageJob_->plan = rows.damagePlan;
    damageJob_->winning = rows.damageWinning;
    damageJobId_ = damageJobs_[0].job;
    damageJobBytes_ = bytes;
    damageProgress_ = damageWaitingProgress();
    damageJobs_[0].state = DamageJobState::Running;
    damageJobs_[0].progress = damageProgress_;
    return true;
}
// A NEW MASTER PARKS THE RUNNING GRADE before it allocates: its walks are freed (the master's check counted them as
// released), and it waits first in the queue, to start again from its beginning when its turn comes back — its grade is
// never lost to a new master. Its progress says so (a phase at its start).
void Session::parkDamage() noexcept
{
    if (damageJobId_ == 0) return;
    damageJob_.reset();
    damageJobBytes_ = 0;
    damageProgress_ = {};
    damageJobs_[0].state = DamageJobState::Waiting;
    damageJobs_[0].progress = damageWaitingProgress();
    Notification event;
    event.jobId = damageJobId_;
    event.kind = EventKind::Phase;
    event.payload.phase = damageJobs_[0].progress;
    damageJobId_ = 0;
    emit (event);
}
void Session::stepDamage() noexcept
{
    auto& job = *damageJob_;
    bool done = false;
    if (! job.begun)
    {
        job.begun = true;
        job.source[0] = samples_.get();
        job.source[1] = source_.channels == 2 ? samples_.get() + source_.frames : nullptr;
        // A chain or PEAQ that cannot be prepared: nothing to grade, and the report says Unsupported.
        job.refused = ! job.damage.begin (job.plan, job.chain, job.winning, job.source, source_.frames, int (source_.channels));
        done = job.refused;
    }
    else done = job.damage.step (1024);
    ++job.units;
    auto& p = damageProgress_;
    p.completedUnits = job.units;
    p.totalUnits = std::max (p.totalUnits, job.units + 1u);
    p.fraction = std::min (0.99, double (job.units) / double (p.totalUnits));
    p.name = job.damage.phase();
    p.stepFraction = job.damage.walk();
    damageJobs_[0].progress = p;
    if (! done)
    {
        Notification event;
        event.jobId = damageJobId_;
        event.kind = EventKind::Phase;
        event.payload.phase = p;
        emit (event);
        return;
    }
    p = { PhaseName::Final, 1.0, config::Config::versions().all, 0, 0, job.units, job.units, std::nullopt };
    damageJobs_[0].progress = p;
    Notification event;
    event.jobId = damageJobId_;
    event.kind = EventKind::Phase;
    event.payload.phase = p;
    ++revision_;
    emit (event);
    endDamage (0, MeasurementReason::None, true);
}
// THE GRADE AT `index` ENDS: the running one with its walks' result (`stopped` None) or stopped, a waiting one stopped —
// by cancel (Cancelled), by its master gone (MasterForgotten: forget, a new source) or by the room its turn lacked
// (Memory, Unavailable). The running one's walks go at once, in this call: nothing of them is stepped or said after it.
// Its master's report settles, its line where `line`, its Damage event — the job's last word; its entry leaves the
// queue, the order of the rest kept.
void Session::endDamage (std::size_t index, MeasurementReason stopped, bool line) noexcept
{
    const auto entry = damageJobs_[index];
    const bool running = entry.state == DamageJobState::Running && entry.job == damageJobId_;
    const Phase progress = running ? damageProgress_ : entry.progress;
    Kept* const first = masters_.get();
    Kept* const last = first + masterCount_;
    Kept* const kept = std::find_if (first, last, [&] (const Kept& k) { return k.id == entry.masterId; });
    Notification event;
    event.jobId = entry.job;
    event.payload.damage = { entry.masterId, MeasurementStatus::Cancelled, stopped };
    if (kept != last && kept->report)
    {
        auto& d = kept->report->damage;
        if (stopped == MeasurementReason::Memory) { d.status = MeasurementStatus::Unavailable; d.reason = stopped; }
        else if (stopped != MeasurementReason::None) { d.status = MeasurementStatus::Cancelled; d.reason = stopped; }
        else if (damageJob_->refused) { d.status = MeasurementStatus::Unavailable; d.reason = MeasurementReason::Unsupported; }
        else damageJob_->damage.publish (d);
        event.payload.damage.status = d.status;
        event.payload.damage.reason = d.reason;
        if (line)
        {
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (MasterReportText::damage (d));
            emit (event, progress);
        }
    }
    event.kind = EventKind::Damage;
    emit (event, progress);
    if (running)
    {
        damageJob_.reset();
        damageJobId_ = 0;
        damageJobBytes_ = 0;
        damageProgress_ = {};
    }
    for (std::size_t i = index; i + 1 < damageCount_; ++i) damageJobs_[i] = damageJobs_[i + 1];
    --damageCount_;
    damageJobs_[damageCount_] = {};
}
// EVERY GRADE ENDS with its masters gone (a new source: the walks re-render from the old one): each says so — its last
// word, MasterForgotten — in the load's own batch, in queue order, before the source is replaced, so each is stamped with
// the source, state and revision it belonged to and comes before anything of the new one; the queue's room
// (kMaxDamageGrades) keeps them all within the batch beside the load's own events.
void Session::endAllDamage() noexcept
{
    while (damageCount_ != 0) endDamage (0, MeasurementReason::MasterForgotten, false);
    oldWorldEvents_ = eventCount_;    // the command's revision is not theirs (Session::apply)
}

Stepped Session::step (std::uint32_t budget) noexcept
{
    eventCount_ = 0; oldWorldEvents_ = 0;
    // With no work there is no arithmetic to refuse and no publication to number.
    // A queue waiting only for the previous master's PCM to be taken keeps the shell stepping (MVP).
    if (! hasWork()) return { queuedCount_ != 0 && pendingMaster_.master != 0 ? StepState::More : StepState::Done, 0, false };
    if (checkFloatingPointEnvironment() != Status::Ok)
    {
        Notification event;
        event.jobId = job_ != 0 ? job_ : needlesJob_ != 0 ? needlesJob_ : measurementJob_ != 0 ? measurementJob_ : damageJobId_;
        event.kind = EventKind::Error;
        event.payload.error.code = ErrorCode::Refusal;
        (void) event.payload.error.fact.assign (text::Fact::of (text::FactId::SessionRefusal));
        event.payload.error.recover = Recover::Continue;
        emit (event);
        return { hasWork() ? StepState::More : StepState::Done, 0, true };
    }
    const auto progress = detail::rules().engine.find ("progress");
    const auto master = progress.find ("master");
    // The renders a master's bar expects, by the loudness mode it lands in (a waiting master: its recipe's).
    const auto expectedIn = [] (LoudnessMode mode) { return detail::expectedPasses (detail::rules().engine, mode); };
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
        // THE MASTERS' QUEUE (MVP): the head starts when the session's master slot is free and a master may start.
        if (queueReady()) { promoteQueuedMaster(); ++units; continue; }
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
                        const auto passes = expectedIn (detail::loudnessModeOf (detail::rules(), jobRecipe_.project));
                        p.name = PhaseName::Analyzers; p.pass = 0; p.totalPasses = passes;
                        p.analyzers.emplace();
                        if ((waits & detail::bitOf (Analyzer::Excursions)) != 0)
                        {
                            p.analyzers->count = 1;
                            p.analyzers->items[0] = { Analyzer::Excursions, needlesProgress_.fraction };
                        }
                        else if (measurementProgress_.name == PhaseName::Analyzers)
                            p.analyzers = measurementProgress_.analyzers;
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
                command::Master waitingRequest;
                waitingRequest.masterAnyway = jobMasterAnyway_;
                waitingRequest.budgetResolutionDb = jobBudgetResolutionDb_;
                const auto plan = jobWaiting_ ? detail::MasterJob::plan (*this, waitingRequest, jobRecipe_.project)
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
            const bool delivery = masterJob_->deliveryMode != DeliveryMode::Mastered;
            const int beforePass = delivery ? 0 : masterJob_->startedPasses();
            const auto outcome = masterTraceActive_ ? mastering::StepResult::Done : masterJob_->step (1024);
            ++masterUnit_;
            const bool end = outcome != mastering::StepResult::More;
            const auto expectedRenders = delivery ? 0u : expectedIn (masterJob_->mode);
            const auto total = std::uint32_t (std::min<std::uint64_t> (
                4294967295u, (source_.frames / 1024u + 2u * std::uint64_t (masterJob_->frames) / 1024u + 64u)
                    * std::max (1u, expectedRenders)));
            double fraction = masterProgress_.fraction;
            if (! delivery && ! end)
            {
                const double expected = double (std::max (1u, expectedRenders));
                const double rendered = masterJob_->renderProgress();
                const double scaled = rendered <= expected ? 0.8 * rendered / expected
                    : 1.0 - 0.2 * expected / rendered;
                fraction = std::max (fraction, scaled);
                // Once the search and its certificate have ended, leave only the fixed 20% completion reserve.
                if (! masterJob_->search.active()) fraction = std::max (fraction, 0.8);
            }
            // A delivery (as-is, peaks-only) has no search: its bar is its own walks'.
            else if (! end) fraction = std::max (fraction, masterJob_->deliveryFraction());
            // The master's passes, and how far its current walk over the file has come — a new count for each walk.
            masterProgress_ = { end ? PhaseName::Final : PhaseName::Pass,
                end ? 1.0 : std::min (0.999999, fraction),
                config::Config::versions().all, delivery ? 0u : std::uint32_t (masterJob_->startedRenders()), expectedRenders,
                masterUnit_, total, end ? std::nullopt : masterJob_->stepFraction() };
            event.payload.phase = masterProgress_;
            emit (event);
            if (! delivery && masterJob_->startedPasses() != beforePass)
            {
                event.kind = EventKind::Fact;
                (void) event.payload.fact.assign (text::Fact::of (text::FactId::MasterPass,
                    text::Arg::count (masterJob_->startedPasses())));
                emit (event, masterProgress_);
            }
            if (end)
            {
                if (delivery)
                {
                    if (outcome != mastering::StepResult::Done)
                    { contract (event.jobId); ++units; continue; }
                    const auto completedJob = job_;
                    masterTraceActive_ = true; // keep the job until its report and PCM have been moved below
                    if (! detail::Driver::mastered (*this, completedJob)) { contract (completedJob); ++units; continue; }
                    auto& kept = masters_[masterCount_ - 1];
                    kept.report = masterJob_->reportResult();
                    kept.report->readings = MasterReportText::readings (*kept.report, 0);
                    event.kind = EventKind::Fact;
                    const auto said = kept.report->deliveryMode == DeliveryMode::AsIs
                        ? text::Fact::of (text::FactId::SourceDeliveryAsIs)
                        : text::Fact::of (text::FactId::SourceDeliveryPeaksOnly,
                            text::Arg::value (kept.report->deliveryGainDb, text::Unit::Db, 1),
                            text::Arg::value (kept.report->ceilingDbTp, text::Unit::DbTp, 1));
                    (void) event.payload.fact.assign (said); emit (event, masterProgress_);
                    masterAudioBits_ = masterJob_->ready.deliveryBits;
                    masterAudio_ = { masterJob_->takeOutput(), std::uint64_t (masterJob_->frames),
                        std::uint32_t (masterJob_->channels), masterJob_->deliveryRate };
                    pendingMaster_ = { source_.hash, revision_, completedJob, completedJob };
                    masterJob_.reset(); masterJobBytes_ = 0;
                    masterSummary_ = {}; masterTraceCursor_ = 0; masterTraceActive_ = false;
                    event.kind = EventKind::Fact;
                    (void) event.payload.fact.assign (text::Fact::of (text::FactId::MasterReady));
                    emit (event, masterProgress_);
                    event.kind = EventKind::Done; event.payload.done.masterId = completedJob;
                    emit (event, masterProgress_);
                    ++units;
                    continue;
                }
                auto& rows = masterRows_[masterCount_];
                const auto& solution = masterJob_->result();
                const auto trace = std::span<LandingTraceBucket> (rows.traces.get(), rows.traceCapacity);
                const auto clip = std::span<LandingTraceBucket> (rows.traces.get() + rows.traceCapacity, rows.traceCapacity);
                if (! masterTraceActive_)
                {
                    if (! LandingOps::summarize (solution, masterJob_->floorFirstPasses, masterJob_->floorFirstWork,
                            { rows.passes.get(), mastering::TargetLoudnessSolverLimits::kMaxPasses }, masterSummary_))
                    { contract (event.jobId); ++units; continue; }
                    masterTraceCursor_ = 0; masterTraceActive_ = true;
                }
                const auto beforeTrace = masterTraceCursor_;
                // The glue's trace, where it compresses, in the same bounded copies as the limiter's and the clipper's.
                const auto glueRows = std::span<LandingTraceBucket> (rows.glueRows.get(), rows.glueRows ? rows.traceCapacity : 0u);
                // The saturation's shave the same way, where the soft clipper shapes.
                const auto shaveRows = std::span<LandingTraceBucket> (rows.saturationRows.get(),
                    rows.saturationRows ? rows.traceCapacity : 0u);
                const bool tracesDone = LandingOps::stepTraces (solution, trace, clip, glueRows, shaveRows,
                    masterJob_->deliveryRate, masterSummary_, rows.glueTrace, rows.saturationTrace, masterTraceCursor_, 1024);
                if (! tracesDone)
                {
                    if (beforeTrace == masterTraceCursor_) contract (event.jobId);
                    ++units; continue;
                }
                const auto completedJob = job_;
                if (! detail::Driver::mastered (*this, completedJob)) { contract (completedJob); ++units; continue; }
                masters_[masterCount_ - 1].report = masterJob_->reportResult();
                rows.workedReady.params = masterJob_->winningParams();
                // THE GLUE'S TRACE THROUGH ITS MIX (MVP): what the glue took off the song — the compressor's reduction
                // blended with the dry path at the mix that sounded (detail::glueTakenDb, the report's own law), not the
                // detector's reduction before the mix. The law is monotonic: a bucket's least and most stay exact.
                if (rows.glueTrace)
                {
                    const double mix = std::clamp (rows.workedReady.params.compressorMix, 0.0, 1.0);
                    const auto through = [mix] (double db) noexcept { return db > 0.0 ? detail::glueTakenDb (db, mix) : db; };
                    for (auto& b : std::span<LandingTraceBucket> (rows.glueRows.get(), rows.glueRows ? rows.glueTrace->rows.size() : 0u)) { b.minDb = through (b.minDb); b.maxDb = through (b.maxDb); b.meanDb = through (b.meanDb); }
                }
                // What its damage grade needs, kept with the master for a grade the shell asks (command::GradeDamage):
                // the damage's line below says Pending — or why it cannot be graded.
                if (masterJob_->damageFollows)
                {
                    rows.damageGradable = true;
                    rows.damagePlan = masterJob_->damagePlan;
                    rows.damageWinning = masterJob_->winningParams();
                }
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
                    const LandingMeasure measure { masterJob_->search.landedLufs(), masterJob_->search.gateLufs(),
                                                   detail::limiterBudgetDb (detail::rules().engine, report.targetLufs),
                                                   masterJob_->search.overBudgetDb() };
                    // A max mode's verdict names the mode, what ended it and the loudness; the damage line below its grade.
                    const bool max = report.loudnessMode != LoudnessMode::Manual;
                    if (const auto verdict = max ? MasterReportText::max (report, detail::maxBudgetDb (detail::rules().engine, report.loudnessMode),
                                                                          masterJob_->search.overBudgetDb())
                                                 : MasterReportText::landing (report, masterSummary_, toleranceLu, measure))
                    { (void) event.payload.fact.assign (*verdict); emit (event, masterProgress_); }
                    // The limiter wall's numbers follow its plain verdict as a line for the log alone (619).
                    if (masterSummary_.limiterWall && masterSummary_.limiterSlope && masterSummary_.limiterWallP95Db)
                    {
                        (void) event.payload.fact.assign (text::Fact::of (text::FactId::MasterLandingWallDetail,
                            text::Arg::value (*masterSummary_.limiterSlope, text::Unit::None, 3),
                            text::Arg::value (*masterSummary_.limiterWallP95Db, text::Unit::Db, 2)));
                        emit (event, masterProgress_);
                    }
                    // Pulled up to the floor: the numbers behind the plain verdict, a line of its own for the log (617).
                    if (const auto detail = MasterReportText::maxFloorDetail (report,
                            detail::maxBudgetDb (detail::rules().engine, report.loudnessMode),
                            masterJob_->search.result().limiterActive.stats.p95Db, masterJob_->floorLufs,
                            masterJob_->floorFirstLufs))
                    { (void) event.payload.fact.assign (*detail); emit (event, masterProgress_); }
                    // Delivered above the ceiling (no render stayed under it): the mark, beside the verdict.
                    if (const auto above = MasterReportText::peaksAboveCeiling (report))
                    { (void) event.payload.fact.assign (*above); emit (event, masterProgress_); }
                    // Landed on the source's gate and read apart from it by BS.1770: both numbers, in the miss's place.
                    if (const auto gate = MasterReportText::gate (report, measure, toleranceLu))
                    { (void) event.payload.fact.assign (*gate); emit (event, masterProgress_); }
                    else if (const auto miss = max || masterSummary_.limiterWall ? std::optional<text::Fact> {} : MasterReportText::miss (report))
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
                    if (const auto glue = MasterReportText::glue (*cost))
                    { (void) event.payload.fact.assign (*glue); emit (event, masterProgress_); }
                    if (const auto saturation = MasterReportText::saturation (*cost))
                    { (void) event.payload.fact.assign (*saturation); emit (event, masterProgress_); }
                }
                // The damage the processing did, heard — Pending here, graded by its own job when the shell asks —
                // and the loudness range's change: two lines, each its own or why not.
                {
                    const auto& damage = masters_[masterCount_ - 1].report->damage;
                    event.kind = EventKind::Fact;
                    (void) event.payload.fact.assign (MasterReportText::damage (damage)); emit (event, masterProgress_);
                    (void) event.payload.fact.assign (MasterReportText::lra (damage)); emit (event, masterProgress_);
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
                const bool lateCrest = outcome == mastering::StepResult::Done
                    && masterJob_->readyForLateCrest() && masterJob_->beginLateCrest();
                if (lateCrest)
                {
                    const auto audioBytes = std::uint64_t (masterJob_->frames) * std::uint64_t (masterJob_->channels)
                        * sizeof (float);
                    lateMasterJobBytes_ = masterJobBytes_ > audioBytes ? masterJobBytes_ - audioBytes : 0;
                    lateMasterId_ = completedJob;
                    lateMasterJob_ = std::move (masterJob_);
                    masterJobBytes_ = 0;
                }
                else { masterJob_.reset(); masterJobBytes_ = 0; }
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
        else if (lateMasterJob_)
        {
            stepLateMasterCrest();
        }
        else if (crestJoin_)
        {
            stepMasterCrestJoin();
        }
        else if (needlesJob_ != 0)
        {
            stepNeedles();
        }
        // The damage of a delivered master: behind every other work — a master, its crest, the needles, the source's
        // measurement — and it never holds one of them up. The first waiting grade starts in the unit of its first step,
        // or is refused there for the room it lacks.
        else if (damageCount_ != 0 && measurementJob_ == 0)
        {
            if (damageJobId_ != 0 || startDamage()) stepDamage();
        }
        else if (liveMeasurements_ && liveMeasurements_->stage < 9 && measurementUnit_ < 2)
        {
            stepMeasurements();
        }
        else if (waveform_ && ! waveform_->finished)
        {
            stepWaveform();
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
            measurementProgress_ = { PhaseName::Analyzers, 1.0, config::Config::versions().all, 0, 0, 1, 1, std::nullopt };
            measurementProgress_.analyzers.emplace();
            ++revision_;
        }
        else contract (measurementJob_);
        ++units;
    }
    replan();
    return { hasWork() || (queuedCount_ != 0 && pendingMaster_.master != 0) ? StepState::More : StepState::Done, units, false };
}
} // namespace felitronics::session
