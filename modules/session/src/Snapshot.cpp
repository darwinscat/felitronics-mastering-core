// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
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
    return v.target.size() + v.source.name.size() + v.masters.size_bytes()
         + v.momentary.size_bytes() + v.shortTerm.size_bytes() + v.runs.size_bytes();
}
Snapshot Snapshot::copy (const SnapshotView& v) noexcept
{
    Snapshot out;
    out.view_ = v;
    const auto chars = v.target.size() + v.source.name.size();
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
    const auto points = v.momentary.size() + v.shortTerm.size();
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
SnapshotView Session::buildView() const noexcept
{
    SnapshotView v;
    v.state = state_;
    v.mastering = mastering_;
    v.revision = revision_;
    v.project = project_;
    v.target = targetName();
    v.source = source_;
    v.job = job_;
    v.measurementJob = measurementJob_;
    v.jobRecipe = jobRecipe_;
    v.masters = masters();
    v.measurementProgress = measurementProgress_;
    v.masterProgress = masterProgress_;
    v.integratedLufs = std::numeric_limits<double>::quiet_NaN(); // the stub establishes no audio measurement
    v.sourceBytes = double (source_.frames * source_.channels * sizeof (float));
    return v;
}
} // namespace felitronics::session
