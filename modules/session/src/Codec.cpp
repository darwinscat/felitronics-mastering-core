// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "JsonCodec.h"
#include "JsonNumber.h"
#include "BuildContract.h"
#include "Utf8.h"
#include <felitronics/session/Snapshot.h>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace felitronics::session
{
namespace
{
using detail::Writer;
using detail::Storage;
using detail::Reader;
bool valid (const SnapshotView& v) noexcept
{
    for (const Kept& master : v.masters)
        if (master.landing)
        {
            const LandingSummary& landing = *master.landing;
            if (landing.passes > 12 || landing.log.size() != landing.passes
                || (landing.deliverable && (! landing.achievedLufs || ! landing.missLu
                    || ! landing.distanceLu || ! landing.truePeakDbTp))
                || (landing.peaksAboveCeiling && (! landing.deliverable || landing.status != LandingStatus::TargetUnreachable
                    || landing.binding != LandingConstraint::TruePeakCeiling))) return false;
            for (const auto& trace : { landing.limiterTrace, landing.peakClipTrace })
                if (trace)
                {
                    if (trace->columns != trace->rows.size() || trace->columns > 65536
                        || trace->fromFrame > trace->toFrame || (trace->columns != 0 && trace->sampleRateHz == 0)
                        || (trace->valid && (! trace->complete || trace->nonFinite != 0 || trace->samples == 0))
                        || trace->nonFinite > trace->samples) return false;
                    std::uint64_t samples = 0, nonFinite = 0;
                    for (const auto& row : trace->rows)
                    {
                        if (row.nonFinite > row.samples || ! std::isfinite (row.minDb)
                            || ! std::isfinite (row.maxDb) || ! std::isfinite (row.meanDb)
                            || row.minDb < 0.0 || row.maxDb < row.minDb || row.meanDb < 0.0) return false;
                        if (row.samples > std::numeric_limits<std::uint64_t>::max() - samples
                            || row.nonFinite > std::numeric_limits<std::uint64_t>::max() - nonFinite) return false;
                        samples += row.samples; nonFinite += row.nonFinite;
                    }
                    if (trace->complete && (samples != trace->samples || nonFinite != trace->nonFinite)) return false;
                }
        }
    for (const Kept& master : v.masters) if (master.report)
    {
        const auto& r = *master.report;
        const auto& c = r.crest;
        if (! master.landing || r.deliverable != master.landing->deliverable
            || r.peaksAboveCeiling != master.landing->peaksAboveCeiling
            || r.targetMet != (master.landing->status == LandingStatus::Solved)
            || ! std::isfinite (r.targetLufs) || ! std::isfinite (r.ceilingDbTp)
            || r.checkPasses > 1
            || (r.status == MeasurementStatus::Ready
                && (! r.achievedLufs || ! r.truePeakDbTp || ! r.missLu || ! r.gainFromSourceDb))
            // Delivered: under the ceiling, or above it and marked (no render stayed under it, owner 01.10).
            || (r.deliverable && (r.status != MeasurementStatus::Ready || ! (r.peakSafe || r.peaksAboveCeiling)))
            || (r.peakSafe && (! r.truePeakDbTp || *r.truePeakDbTp > r.ceilingDbTp))
            || (r.peaksAboveCeiling && (r.peakSafe || ! r.deliverable || r.targetMet || ! r.truePeakDbTp
                || *r.truePeakDbTp <= r.ceilingDbTp))
            || (c.status == MeasurementStatus::Ready
                && r.checkPasses != (c.sourceRateCheck ? 1u : 0u))
            || (r.status == MeasurementStatus::Ready ? r.reason != MeasurementReason::None
                : r.reason == MeasurementReason::None)
            || (r.lraLu ? r.lraReason != MeasurementReason::None
                : r.lraReason == MeasurementReason::None)
            || (r.plrDb ? r.plrReason != MeasurementReason::None
                : r.plrReason == MeasurementReason::None)
            || c.version != 1 || (c.blocks != 0 && (c.sampleRateHz == 0 || c.hopFrames == 0 || c.blockHops == 0))
            // A lean summary (masterRowsIncluded false) carries a master's scalars without its heavy rows: no trace,
            // no crest rows or mask, no waveform buckets — and nothing else may be missing.
            || (v.masterRowsIncluded
                ? c.blocks > c.rows.size() / 10u || c.rows.size() != c.blocks * 10u
                  || c.sourceMask.size() != (c.status == MeasurementStatus::Ready ? c.blocks * 5u : 0u)
                : ! c.rows.empty() || ! c.sourceMask.empty() || master.landing->limiterTrace || master.landing->peakClipTrace
                  || (r.cost && ! r.cost->waveform.empty()))
            || (c.status == MeasurementStatus::Ready ? c.reason != MeasurementReason::None || ! c.complete
                : c.reason == MeasurementReason::None)) return false;
        for (double value : c.rows) if (! std::isfinite (value) || value < 0) return false;
        for (double value : c.sourceMask) if (! detail::maskValue (value)) return false;
        for (const auto& value : { r.achievedLufs, r.truePeakDbTp, r.lraLu, r.plrDb,
                                   r.gainFromSourceDb, r.missLu })
            if (value && ! std::isfinite (*value)) return false;
        for (const auto& hint : { r.firstHint, r.secondHint })
            if (hint && (hint->reason == LandingReason::None || ! std::isfinite (hint->evidence)
                || hint->percent != (hint->reason == LandingReason::ExcessSubBass
                                  || hint->reason == LandingReason::DarkMix))) return false;
        if (r.cost)
        {
            const auto& cost = *r.cost;
            if (cost.k2Reason != MeasurementReason::NotImplemented || cost.sourceRateHz == 0
                || cost.masterRateHz == 0 || cost.sections.size() > 1000000u
                || cost.waveform.size() > 4096u
                || (cost.worstSectionIndex && *cost.worstSectionIndex >= cost.sections.size())) return false;
            for (const auto& value : { cost.crestLowDb, cost.crestLowMidDb, cost.crestHighMidDb,
                                       cost.crestHighDb, cost.crestFullDb, cost.shapeP95Lu,
                                       cost.largestSectionShiftLu,
                                       cost.pumpingRmsDb, cost.limiterP50Db, cost.limiterP95Db,
                                       cost.limiterActiveShare, cost.activeWindowShare })
                if (value.value ? value.reason != MeasurementReason::None || ! std::isfinite (*value.value)
                    : value.reason == MeasurementReason::None) return false;
            for (const auto& section : cost.sections)
                if (section.toFrame <= section.fromFrame || ! std::isfinite (section.sourceLufs)
                    || ! std::isfinite (section.masterLufs) || ! std::isfinite (section.shiftLu)) return false;
            for (const auto& row : cost.waveform)
                if (row.toFrame <= row.fromFrame || row.channel > 1 || row.finite > row.toFrame - row.fromFrame
                    || ! std::isfinite (row.minimum) || ! std::isfinite (row.maximum)
                    || ! std::isfinite (row.rms) || row.minimum > row.maximum || row.rms < 0) return false;
        }
    }
    const auto& m = v.measurementStorage;
    for (const auto bytes : { m.sourceBytes, m.resultBytes, m.workspaceBytes, m.copyBytes,
                             m.codecBytes, m.allocatorBytes, m.loadPeakBytes, m.workPeakBytes, m.peakBytes, m.largestBlockBytes, v.needlesBytes, v.needlesLargestBlockBytes })
        if (! std::isfinite (bytes) || bytes < 0.0 || bytes >= 9007199254740992.0
            || std::bit_cast<std::uint64_t> (double (std::uint64_t (bytes))) != std::bit_cast<std::uint64_t> (bytes)) return false;
    return OwnedMeasurements::valid (v.measurements) && std::isfinite (v.sourceBytes) && v.sourceBytes >= 0.0 && v.sourceBytes < 9007199254740992.0
        && std::bit_cast<std::uint64_t> (double (std::uint64_t (v.sourceBytes))) == std::bit_cast<std::uint64_t> (v.sourceBytes);
}
bool read (std::string_view json, Storage& storage, SnapshotView& view) noexcept
{
    if (! detail::validUtf8 (json)) return false;
    Reader reader { json, storage, 0, true, {}, 0, 0, false };
    reader.value (view); reader.space();
    return reader.good && reader.pos == json.size() && valid (view);
}
} // namespace

namespace detail
{
CodecStatus snapshotEncodable (const SnapshotView& v) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    if (! valid (v)) return CodecStatus::Invalid;
    for (const auto& d : v.machineDifferences)
        if (unsigned (d.device) > enumLast<Device>()) return CodecStatus::Invalid;
    return CodecStatus::Ok;
}
} // namespace detail

CodecNeed Codec::encodedBytes (const SnapshotView& view) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { CodecStatus::FloatingPointEnvironment, 0 };
    if (! valid (view)) return { CodecStatus::Invalid, 0 };
    Writer writer; writer.value (view);
    return writer.good ? CodecNeed { CodecStatus::Ok, writer.size } : CodecNeed { CodecStatus::Invalid, 0 };
}
CodecStatus Codec::encode (const SnapshotView& view, std::span<char> output) noexcept
{
    const auto need = encodedBytes (view);
    if (need.status != CodecStatus::Ok) return need.status;
    if (output.size() < need.bytes) return CodecStatus::TooSmall;
    Writer writer { output.data() }; writer.value (view);
    return CodecStatus::Ok;
}
CodecNeed Codec::decodedBytes (std::string_view json) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { CodecStatus::FloatingPointEnvironment, 0 };
    Storage storage; SnapshotView view;
    if (! read (json, storage, view)) return { CodecStatus::Invalid, 0 };
    return { CodecStatus::Ok, storage.bytes() };
}
CodecStatus Codec::decode (std::string_view json, Snapshot& output) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    Storage sizes; SnapshotView view;
    if (! read (json, sizes, view)) return CodecStatus::Invalid;
    Snapshot out;
    if (sizes.chars) out.text_.reset (new char[sizes.chars]);
    if (sizes.masters) out.masters_.reset (new Kept[sizes.masters]);
    if (sizes.landingPasses) out.landingPasses_.reset (new LandingPass[sizes.landingPasses]);
    if (sizes.landingTraceRows) out.landingTraceRows_.reset (new LandingTraceBucket[sizes.landingTraceRows]);
    if (sizes.masterCrestRows) out.masterCrestRows_.reset (new double[sizes.masterCrestRows]);
    if (sizes.masterSections) out.masterSections_.reset (new MasterSection[sizes.masterSections]);
    if (sizes.masterWaveformRows) out.masterWaveform_.reset (new MasterWaveformBucket[sizes.masterWaveformRows]);
    if (sizes.points) out.points_.reset (new ReadingPoint[sizes.points]);
    if (sizes.runs) out.runs_.reset (new ReadingRun[sizes.runs]);
    if (sizes.differences) out.differences_.reset (new MachineDifference[sizes.differences]);
    if (sizes.damageJobs) out.damageJobs_.reset (new DamageJobEntry[sizes.damageJobs]);
    if (sizes.eqPoints) out.eqCurve_.reset (new EqPoint[sizes.eqPoints]);
    if (sizes.measurementResults) out.measurements_.results_.reset (new MeasurementResult[sizes.measurementResults]);
    if (sizes.measurementNumbers) out.measurements_.numbers_.reset (new MeasurementValue[sizes.measurementNumbers]);
    if (sizes.measurementArrays) out.measurements_.arrays_.reset (new MeasurementArray[sizes.measurementArrays]);
    if (sizes.measurementRows) out.measurements_.rows_.reset (new double[sizes.measurementRows]);
    out.measurements_.count_ = sizes.measurementResults;
    Storage storage { 0, 0, 0, 0, 0, out.text_.get(), out.masters_.get(), out.points_.get(), out.runs_.get(), out.differences_.get(), 0, out.eqCurve_.get() };
    storage.measurementResult = out.measurements_.results_.get();
    storage.measurementNumber = out.measurements_.numbers_.get();
    storage.measurementArray = out.measurements_.arrays_.get();
    storage.measurementRow = out.measurements_.rows_.get();
    storage.landingPass = out.landingPasses_.get();
    storage.landingTraceRow = out.landingTraceRows_.get();
    storage.masterCrestRow = out.masterCrestRows_.get();
    storage.masterSection = out.masterSections_.get();
    storage.masterWaveformRow = out.masterWaveform_.get();
    storage.damageJob = out.damageJobs_.get();
    const bool filled = read (json, storage, out.view_);
    detail::debugBound (filled);
    output = std::move (out);
    return CodecStatus::Ok;
}
} // namespace felitronics::session
