// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "SnapshotStorage.h"
#include "Devices.h"
#include "EqCurve.h"
#include "Needles.h"
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
    return detail::snapshotStorage (v.target.size(), v.source.name.size(), v.masters.size_bytes(),
                                    v.momentary.size_bytes(), v.shortTerm.size_bytes(), v.runs.size_bytes())
         + std::uint64_t (v.machineDifferences.size_bytes()) + std::uint64_t (v.eqCurve.size_bytes()) + OwnedMeasurements::storageFor (v.measurements);
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
        out.view_.masters = { out.masters_.get(), v.masters.size() };
    }
    if (! v.machineDifferences.empty())
    {
        out.differences_.reset (new MachineDifference[v.machineDifferences.size()]);
        std::copy (v.machineDifferences.begin(), v.machineDifferences.end(), out.differences_.get());
        out.view_.machineDifferences = { out.differences_.get(), v.machineDifferences.size() };
    }
    if (! v.eqCurve.empty())
    {
        out.eqCurve_.reset (new EqPoint[v.eqCurve.size()]);
        std::copy (v.eqCurve.begin(), v.eqCurve.end(), out.eqCurve_.get());
        out.view_.eqCurve = { out.eqCurve_.get(), v.eqCurve.size() };
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
    if (placed()) detail::eqCurve (project_, detail::rules(), double (source_.sampleRate), eqCurve_);
}
SnapshotView Session::buildView() const noexcept
{
    SnapshotView v;
    v.offeredDevices = capabilities_.offeredDevices;
    v.state = state_;
    v.mandatoryMeasurementsReady = mandatoryReady();
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
    if (placed()) v.eqCurve = eqCurve_;
    v.target = targetName();
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
    v.job = job_;
    v.measurementJob = measurementJob_;
    v.jobRecipe = jobRecipe_;
    v.masters = masters();
    v.machineDifferences = { differences_, differenceCount_ };
    v.measurementProgress = measurementProgress_;
    v.masterProgress = masterProgress_;
    v.integratedLufs = std::numeric_limits<double>::quiet_NaN();
    for (const auto& value : measurementResults_[0].numbers)
        if (value.name == "integratedLufs" && value.value) v.integratedLufs = *value.value;
    v.sourceBytes = double (source_.frames * source_.channels * sizeof (float));
    return v;
}
} // namespace felitronics::session
