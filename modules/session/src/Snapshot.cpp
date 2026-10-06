// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "SnapshotStorage.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Needles.h"
#include "Observations.h"
#include <felitronics/session/Snapshot.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace felitronics::session
{
Snapshot::Snapshot() noexcept = default;
Snapshot::~Snapshot() = default;
Snapshot::Snapshot (Snapshot&&) noexcept = default;
Snapshot& Snapshot::operator= (Snapshot&&) noexcept = default;
const SnapshotView& Snapshot::view() const noexcept { return view_; }
std::uint64_t Snapshot::storageFor (const SnapshotView& v) noexcept
{
    std::uint64_t landingBytes = 0;
    for (const Kept& master : v.masters)
        if (master.landing)
        {
            landingBytes += std::uint64_t (master.landing->log.size_bytes());
            if (master.landing->limiterTrace) landingBytes += std::uint64_t (master.landing->limiterTrace->rows.size_bytes());
            if (master.landing->peakClipTrace) landingBytes += std::uint64_t (master.landing->peakClipTrace->rows.size_bytes());
        }
    for (const Kept& master : v.masters)
        if (master.report)
        {
            landingBytes += std::uint64_t (master.report->crest.rows.size_bytes())
                          + std::uint64_t (master.report->crest.sourceMask.size_bytes());
            if (master.report->cost)
                landingBytes += std::uint64_t (master.report->cost->sections.size_bytes())
                              + std::uint64_t (master.report->cost->waveform.size_bytes());
        }
    return detail::snapshotStorage (v.target.size(), v.source.name.size(), v.masters.size_bytes(),
                                    v.momentary.size_bytes(), v.shortTerm.size_bytes(), v.runs.size_bytes())
         + std::uint64_t (v.machineDifferences.size_bytes()) + std::uint64_t (v.eqCurve.size_bytes()) + std::uint64_t (v.eqOnlyCurve.size_bytes())
         + std::uint64_t (v.damageJobs.size_bytes())
         + OwnedMeasurements::storageFor (v.measurements) + landingBytes;
}
Snapshot Snapshot::copy (const SnapshotView& v) noexcept
{
    Snapshot out;
    out.view_ = v;
    out.measurements_ = OwnedMeasurements::copy (v.measurements);
    out.view_.measurements = out.measurements_.view();
    const auto chars = detail::snapshotAllocationSize<std::size_t> (
        detail::snapshotTextBytes (v.target.size(), v.source.name.size()));
    const auto pointBytes = std::uint64_t (v.momentary.size_bytes()) + std::uint64_t (v.shortTerm.size_bytes());
    const auto points = detail::snapshotAllocationSize<std::size_t> (pointBytes) / sizeof (ReadingPoint);
    if (chars != 0)
    {
        out.text_.reset (new char[chars]);
        std::copy (v.target.begin(), v.target.end(), out.text_.get());
        std::copy (v.source.name.begin(), v.source.name.end(), out.text_.get() + v.target.size());
        out.view_.target = { out.text_.get(), v.target.size() };
        out.view_.source.name = { out.text_.get() + v.target.size(), v.source.name.size() };
    }
    if (! v.masters.empty())
    {
        out.masters_.reset (new Kept[v.masters.size()]);
        std::copy (v.masters.begin(), v.masters.end(), out.masters_.get());
        std::size_t landingCount = 0;
        std::uint64_t traceCount = 0;
        std::uint64_t crestCount = 0, sectionCount = 0, waveformCount = 0;
        for (const Kept& master : v.masters) if (master.landing)
        {
            landingCount += master.landing->log.size();
            if (master.landing->limiterTrace) traceCount += master.landing->limiterTrace->rows.size();
            if (master.landing->peakClipTrace) traceCount += master.landing->peakClipTrace->rows.size();
        }
        for (const Kept& master : v.masters) if (master.report)
        {
            crestCount += master.report->crest.rows.size() + master.report->crest.sourceMask.size();
            if (master.report->cost)
            {
                sectionCount += master.report->cost->sections.size();
                waveformCount += master.report->cost->waveform.size();
            }
        }
        if (landingCount != 0) out.landingPasses_.reset (new LandingPass[landingCount]);
        if (traceCount != 0)
        {
            const auto bytes = traceCount * sizeof (LandingTraceBucket);
            out.landingTraceRows_.reset (new LandingTraceBucket[
                detail::snapshotAllocationSize<std::size_t> (bytes) / sizeof (LandingTraceBucket)]);
        }
        if (crestCount != 0) out.masterCrestRows_.reset (new double[
            detail::snapshotAllocationSize<std::size_t> (crestCount * sizeof (double)) / sizeof (double)]);
        if (sectionCount != 0) out.masterSections_.reset (new MasterSection[
            detail::snapshotAllocationSize<std::size_t> (sectionCount * sizeof (MasterSection)) / sizeof (MasterSection)]);
        if (waveformCount != 0) out.masterWaveform_.reset (new MasterWaveformBucket[
            detail::snapshotAllocationSize<std::size_t> (waveformCount * sizeof (MasterWaveformBucket)) / sizeof (MasterWaveformBucket)]);
        std::size_t offset = 0, traceOffset = 0, crestOffset = 0, sectionOffset = 0, waveformOffset = 0;
        for (std::size_t i = 0; i < v.masters.size(); ++i)
            if (v.masters[i].landing)
            {
                const auto log = v.masters[i].landing->log;
                if (! log.empty()) std::copy (log.begin(), log.end(), out.landingPasses_.get() + offset);
                out.masters_[i].landing->log = log.empty()
                    ? std::span<const LandingPass> {} : std::span<const LandingPass> { out.landingPasses_.get() + offset, log.size() };
                offset += log.size();
                for (auto member : { &LandingSummary::limiterTrace, &LandingSummary::peakClipTrace })
                {
                    const auto& source = (*v.masters[i].landing).*member;
                    if (! source) continue;
                    const auto rows = source->rows;
                    if (! rows.empty()) std::copy (rows.begin(), rows.end(), out.landingTraceRows_.get() + traceOffset);
                    ((*out.masters_[i].landing).*member)->rows = rows.empty()
                        ? std::span<const LandingTraceBucket> {}
                        : std::span<const LandingTraceBucket> { out.landingTraceRows_.get() + traceOffset, rows.size() };
                    traceOffset += rows.size();
                }
            }
        for (std::size_t i = 0; i < v.masters.size(); ++i)
            if (v.masters[i].report)
            {
                for (auto member : { &MasterCrest::rows, &MasterCrest::sourceMask })
                {
                    const auto data = (*v.masters[i].report).crest.*member;
                    if (! data.empty()) std::copy (data.begin(), data.end(), out.masterCrestRows_.get() + crestOffset);
                    ((*out.masters_[i].report).crest.*member) = data.empty() ? std::span<const double> {}
                        : std::span<const double> { out.masterCrestRows_.get() + crestOffset, data.size() };
                    crestOffset += data.size();
                }
                if (v.masters[i].report->cost)
                {
                    const auto sections = v.masters[i].report->cost->sections;
                    const auto waveform = v.masters[i].report->cost->waveform;
                    if (! sections.empty()) std::copy (sections.begin(), sections.end(), out.masterSections_.get() + sectionOffset);
                    if (! waveform.empty()) std::copy (waveform.begin(), waveform.end(), out.masterWaveform_.get() + waveformOffset);
                    out.masters_[i].report->cost->sections = sections.empty() ? std::span<const MasterSection> {}
                        : std::span<const MasterSection> { out.masterSections_.get() + sectionOffset, sections.size() };
                    out.masters_[i].report->cost->waveform = waveform.empty() ? std::span<const MasterWaveformBucket> {}
                        : std::span<const MasterWaveformBucket> { out.masterWaveform_.get() + waveformOffset, waveform.size() };
                    sectionOffset += sections.size(); waveformOffset += waveform.size();
                }
            }
        out.view_.masters = { out.masters_.get(), v.masters.size() };
    }
    if (! v.machineDifferences.empty())
    {
        out.differences_.reset (new MachineDifference[v.machineDifferences.size()]);
        std::copy (v.machineDifferences.begin(), v.machineDifferences.end(), out.differences_.get());
        out.view_.machineDifferences = { out.differences_.get(), v.machineDifferences.size() };
    }
    if (! v.damageJobs.empty())
    {
        out.damageJobs_.reset (new DamageJobEntry[v.damageJobs.size()]);
        std::copy (v.damageJobs.begin(), v.damageJobs.end(), out.damageJobs_.get());
        out.view_.damageJobs = { out.damageJobs_.get(), v.damageJobs.size() };
    }
    // Both curves in one block, the whole curve first — as the codec reads them back.
    if (const auto curves = v.eqCurve.size() + v.eqOnlyCurve.size(); curves != 0)
    {
        out.eqCurve_.reset (new EqPoint[curves]);
        std::copy (v.eqCurve.begin(), v.eqCurve.end(), out.eqCurve_.get());
        std::copy (v.eqOnlyCurve.begin(), v.eqOnlyCurve.end(), out.eqCurve_.get() + v.eqCurve.size());
        out.view_.eqCurve = { out.eqCurve_.get(), v.eqCurve.size() };
        out.view_.eqOnlyCurve = { out.eqCurve_.get() + v.eqCurve.size(), v.eqOnlyCurve.size() };
    }
    if (points != 0)
    {
        out.points_.reset (new ReadingPoint[points]);
        std::copy (v.momentary.begin(), v.momentary.end(), out.points_.get());
        std::copy (v.shortTerm.begin(), v.shortTerm.end(), out.points_.get() + v.momentary.size());
        out.view_.momentary = { out.points_.get(), v.momentary.size() };
        out.view_.shortTerm = { out.points_.get() + v.momentary.size(), v.shortTerm.size() };
    }
    if (! v.runs.empty())
    {
        out.runs_.reset (new ReadingRun[v.runs.size()]);
        std::copy (v.runs.begin(), v.runs.end(), out.runs_.get());
        out.view_.runs = { out.runs_.get(), v.runs.size() };
    }
    return out;
}
SnapshotView Session::buildSummary (std::span<MeasurementResult> results) const noexcept
{
    auto v = buildView();
    std::copy (v.measurements.begin(), v.measurements.end(), results.begin());
    results = results.first (v.measurements.size());
    for (auto& r : results) r.arrays = {};
    v.measurements = results; v.momentary = {}; v.shortTerm = {}; v.runs = {};
    v.measurementRowsIncluded = false;
    if (capabilities_.leanSummary) stripMasterRows (v);
    return v;
}
void Session::stripMasterRows (SnapshotView& v) const noexcept
{
    // Every master without its heavy rows — its scalars, pass log and cost sections stay; a MasterReport query
    // gives one whole. The scratch room is the session's, as long as masters_, and written afresh for every view.
    for (std::size_t i = 0; i < v.masters.size(); ++i)
    {
        auto& lean = leanMasters_[i] = v.masters[i];
        if (lean.landing) { lean.landing->limiterTrace.reset(); lean.landing->peakClipTrace.reset(); }
        if (lean.report)
        {
            lean.report->crest.rows = {}; lean.report->crest.sourceMask = {};
            if (lean.report->cost) lean.report->cost->waveform = {};
        }
    }
    v.masters = { leanMasters_.get(), v.masters.size() };
    v.masterRowsIncluded = false;
}
std::uint64_t Session::summaryBytes() const noexcept
{
    MeasurementResult results[kAnalyzers];
    return Snapshot::storageFor (buildSummary (results));
}
Snapshot Session::summary() const noexcept
{
    MeasurementResult results[kAnalyzers];
    return Snapshot::copy (buildSummary (results));
}
SnapshotView Session::buildSourceSnapshot() const noexcept
{
    auto view = buildView();
    stripMasterRows (view);
    return view;
}
std::uint64_t Session::sourceSnapshotBytes() const noexcept { return Snapshot::storageFor (buildSourceSnapshot()); }
Snapshot Session::sourceSnapshot() const noexcept { return Snapshot::copy (buildSourceSnapshot()); }
std::uint64_t Session::snapshotBytes() const noexcept
{
    return Snapshot::storageFor (buildView());
}
Snapshot Session::snapshot() const noexcept
{
    return Snapshot::copy (buildView());
}
void Session::refreshEqCurve() noexcept
{
    if (! devicesPlaced_) return;
    detail::eqCurve (project_, detail::rules(), double (source_.sampleRate), eqCurve_);
    detail::eqOnlyCurve (project_, detail::rules(), double (source_.sampleRate), eqOnlyCurve_);
}
SnapshotView Session::buildView() const noexcept
{
    SnapshotView v;
    v.offeredDevices = capabilities_.offeredDevices;
    v.state = state_;
    v.mandatoryMeasurementsReady = mandatoryReady();
    v.tempoChoice = tempoForDevice();
    v.measurementsFromSidecar = measurementsFromSidecar_;
    v.sourceMissingAudio = source_.channels != 0 && ! samples_;
    v.canMaster = mandatoryReady() && pendingMaster_.master == 0
        && storageFor (command::Master {}).rejection == Rejection::None;
    v.pendingMaster = pendingMaster_;
    v.pendingMasterBytes = double (masterAudioBytes (pendingMaster_));
    v.devicesPlaced = placed();
    v.canContinueMeasurement = state_ == State::MeasurementStopped;
    v.measurementResumeState = v.canContinueMeasurement ? stoppedState_ : State::Empty;
    v.mastering = mastering_;
    v.revision = revision_;
    v.project = project_;
    const auto rules = detail::rules();
    detail::eachDevice (project_.devices, [&] (Device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        Of::each (rules, [&] (std::uint8_t, const detail::FieldRule&, const auto& hand)
        { if (hand) ++v.handFieldCount; }, layers.hand);
    });
    if (devicesPlaced_) { v.eqCurve = eqCurve_; v.eqOnlyCurve = eqOnlyCurve_; }
    v.plan = plan_;
    v.observations = observations_;
    if (source_.channels != 0)
        v.sourceReport = detail::sourceReport ({ rules, measurementResults_, source_.channels, source_.sampleRate,
                                                source_.frames, source_.bitDepth }, &project_);
    if (source_.channels != 0) v.observationFacts = ObservationText::facts (observations_);
    v.target = targetName();
    v.targetNote = SnapshotText::targetNote (v.target);
    v.source = source_;
    v.measurementStorage = measurementStorage_;
    v.needlesJob = needlesJob_;
    v.needlesSource = needlesSource_;
    v.needlesNeedDb = needlesNeedDb_;
    v.needlesCeilingDb = needlesCeilingDb_;
    v.needlesProgress = needlesProgress_;
    v.needlesBytes = double (needlesDemand_.bytes);
    v.needlesLargestBlockBytes = double (needlesDemand_.largestBlockBytes);
    v.needlesRunsTruncated = needlesResult_ && needlesResult_->runsTruncated;
    if (source_.channels != 0) v.measurements = measurementResults_;
    if (source_.channels != 0) v.readings = ReadingText::source (measurementResults_, source_.channels);
    v.job = job_;
    v.measurementJob = measurementJob_;
    v.jobRecipe = jobRecipe_;
    v.masters = masters();
    v.machineDifferences = { differences_, differenceCount_ };
    v.measurementProgress = measurementProgress_;
    v.masterProgress = masterProgress_;
    v.damageJob = damageJobId_;
    v.damageProgress = damageProgress_;
    v.damageJobs = damageJobs();
    v.loudnessMode = detail::loudnessModeOf (detail::rules(), project_);
    v.integratedLufs = std::numeric_limits<double>::quiet_NaN();
    for (const auto& value : measurementResults_[0].numbers)
        if (value.name == "integratedLufs" && value.value) v.integratedLufs = *value.value;
    v.sourceBytes = samples_ ? double (source_.frames * source_.channels * sizeof (float)) : 0.0;
    return v;
}
std::optional<text::Fact> SnapshotText::targetChange (const SnapshotView& view) noexcept
{
    if (view.handFieldCount == 0) return std::nullopt;
    return text::Fact::of (text::FactId::TargetChangeResetsEdits, text::Arg::count (view.handFieldCount));
}
std::optional<text::Fact> SnapshotText::targetNote (std::string_view target) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    const auto rules = detail::rules();
    const auto row = rules.find (target);
    const auto note = rules.targets.find ("notes").find (target).string();
    if (! row || ! note) return std::nullopt;
    const auto lufs = Arg::value (rules.row (*row).lufs.toDouble(), Unit::Lufs, 1);
    if (*note == "measured") return Fact::of (FactId::TargetMeasured, lufs);
    if (*note == "practice") return Fact::of (FactId::TargetPractice, lufs);
    if (*note == "noNormalisation") return Fact::of (FactId::TargetNoNormalisation);
    return std::nullopt;
}
} // namespace felitronics::session
