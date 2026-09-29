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
                    || ! landing.distanceLu || ! landing.truePeakDbTp))) return false;
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
    if (sizes.points) out.points_.reset (new ReadingPoint[sizes.points]);
    if (sizes.runs) out.runs_.reset (new ReadingRun[sizes.runs]);
    if (sizes.differences) out.differences_.reset (new MachineDifference[sizes.differences]);
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
    const bool filled = read (json, storage, out.view_);
    detail::debugBound (filled);
    output = std::move (out);
    return CodecStatus::Ok;
}
} // namespace felitronics::session
