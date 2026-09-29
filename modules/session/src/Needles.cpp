// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "BuildContract.h"
#include "Needles.h"
#include "Rules.h"
#include <felitronics/session/Session.h>
#include <felitronics/session/Config.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace felitronics::session
{
namespace
{
using Excursions = analysis::PeakExcursions;
constexpr auto slot = std::size_t (Analyzer::Excursions);
// Object storage includes the fixed histograms; storageFor includes the 65,536 run records,
// oversamplers and their constructor proxies, including MSVC's debug containers.
constexpr std::uint64_t allocatorAllowance = 64u * 64u;
std::optional<double> reading (const MeasurementResult& result, std::string_view name) noexcept
{
    if (result.status == MeasurementStatus::Ready)
        for (const auto& value : result.numbers)
            if (value.name == name && value.value && std::isfinite (*value.value)) return value.value;
    return {};
}
double configured (std::string_view name) noexcept
{
    const auto value = detail::rules().engine.find ("limiter").find ("peakClipper").find (name);
    if (const auto d = value.decimal()) return d->toDouble();
    if (const auto i = value.integer()) return double (*i);
    detail::storageOverflow();
}
std::uint64_t resultKey (std::uint64_t sourceKey, double ceiling) noexcept
{
    auto key = sourceKey;
    const auto bits = std::bit_cast<std::uint64_t> (ceiling);
    for (unsigned i = 0; i < 8; ++i) key = (key ^ std::uint8_t (bits >> (8 * i))) * 0x100000001B3ull;
    return key;
}
}
Checked Session::needlesStorage (double ceilingDb) const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok) return { Rejection::FloatingPointEnvironment };
    if (source_.channels == 0) return { Rejection::NoSource };
    if ((source_.frames + Excursions::kChunk - 1u) / Excursions::kChunk > std::numeric_limits<std::uint32_t>::max() - 2u)
        return { Rejection::TooLong };
    Excursions::Params params; params.thresholdDbtp = ceilingDb;
    const auto native = Excursions::storageFor (double (source_.sampleRate), int (source_.channels), params);
    if (! native.ok) return { Rejection::OutOfDomain };
    const auto workspace = sizeof (detail::NeedlesWork) + native.firstBytes();
    // Names copied into a snapshot are bounded by the result's fixed field counts and name limit.
    const auto result = sizeof (detail::NeedlesResult) + sizeof (MeasurementResult)
                      + (detail::NeedlesResult::kNumbers + 5u) * kMeasurementNameBytes;
    // One owned output, one deep snapshot/decode copy and a conservative JSON/row buffer bound.
    const auto bytes = workspace + 8u * result + allocatorAllowance;
    const auto block = std::max<std::uint64_t> ({ native.runs * sizeof (Excursions::Run), workspace - native.firstBytes(), 6u * result });
    return { Rejection::None, kNoField, bytes, 0, block };
}
JobId Session::needlesJob() const noexcept { return needlesJob_; }
void Session::clearNeedles() noexcept
{
    needlesJob_ = 0;
    needlesWork_.reset();
    needlesResult_.reset();
    // No job, no result: unavailable until a job starts (Pending belongs to a running job alone).
    measurementResults_[slot] = {};
    measurementResults_[slot].analyzer = Analyzer::Excursions;
    measurementResults_[slot].status = MeasurementStatus::Unavailable;
    measurementResults_[slot].key = needlesKey_;
}
void Session::needlesChanged() noexcept
{
    const auto& result = measurementResults_[slot];
    Notification event; event.kind = EventKind::Measurement; event.jobId = needlesJob_;
    event.payload.measurement = { result.analyzer, result.status, result.reason, result.key, needlesSource_,
                                  ++revision_, result.framesRead, result.total, result.stored, result.complete };
    emit (event);
}
void Session::requestNeedles() noexcept
{
    const auto& loudness = measurementResults_[std::size_t (Analyzer::Loudness)];
    const auto lufs = reading (loudness, "integratedLufs");
    const auto peak = reading (loudness, "truePeakDb");
    std::optional<double> need, ceiling;
    if (lufs && peak)
    {
        const auto target = detail::rules().row (project_.target);
        const double targetLufs = project_.targetEdit.lufs ? *project_.targetEdit.lufs : target.lufs.toDouble();
        const double targetTp = project_.targetEdit.tp ? *project_.targetEdit.tp : target.tp.toDouble();
        need = (*peak - *lufs) - (targetTp - targetLufs);
        ceiling = *peak - *need;
    }
    if (ceiling && needlesSource_ == source_.hash && needlesCeilingDb_ == ceiling
        && (needlesJob_ != 0 || measurementResults_[slot].status == MeasurementStatus::Ready)) return;
    clearNeedles();
    needlesSource_ = source_.hash;
    needlesNeedDb_ = need; needlesCeilingDb_ = ceiling;
    needlesKey_ = ceiling ? resultKey (measurementKey_, *ceiling) : measurementKey_;
    needlesDemand_ = {}; needlesProgress_ = {};
    auto& result = measurementResults_[slot]; result.key = needlesKey_;
    if (! samples_) { result.reason = MeasurementReason::Unsupported; return; }
    // Without usable input readings nothing is scheduled, and the result says why: the loudness result's own reason —
    // Pending while it is still measured (the first phase's end asks again), its refusal once it has ended.
    if (! need || ! ceiling)
        result.reason = loudness.status == MeasurementStatus::Ready ? MeasurementReason::NoSignal : loudness.reason;
    else if (! std::isfinite (*need) || ! std::isfinite (*ceiling)) result.reason = MeasurementReason::Unsupported;
    else if (*need <= configured ("littleNeedDb")) result.reason = MeasurementReason::NeedNotAbove3;
    else
    {
        needlesDemand_ = needlesStorage (*ceiling);
        if (needlesDemand_.rejection != Rejection::None) result.reason = MeasurementReason::Unsupported;
        else if (lastJob_ == std::numeric_limits<JobId>::max()) result.reason = MeasurementReason::Capacity;
        else
        {
            needlesJob_ = ++lastJob_;
            result.status = MeasurementStatus::Pending; result.reason = MeasurementReason::Pending;
            // One preparation, ceil(frames / chunk) reads and one finish. Load already bounds frames.
            const auto chunks = (source_.frames + Excursions::kChunk - 1u) / Excursions::kChunk;
            needlesProgress_ = { PhaseName::Analyzers, 0, config::Config::versions().all, 0, 0, 0, std::uint32_t (chunks + 2u) };
        }
    }
    // No event here: a transition in the main pump already uses its three-event allowance.
    // The revision that requested this job publishes its status through the snapshot.
}
void Session::stepNeedles() noexcept
{
    auto& result = measurementResults_[slot];
    const auto fail = [&] (MeasurementReason reason, ErrorCode code, double bytes)
    {
        result.status = MeasurementStatus::Unavailable; result.reason = reason;
        Notification error; error.jobId = needlesJob_; error.kind = EventKind::Error;
        error.payload.error.code = code; error.payload.error.needBytes = bytes;
        (void) error.payload.error.fact.assign (text::Fact::of (MeasurementText::fact (reason)));
        emit (error); needlesChanged();
        needlesJob_ = 0; needlesWork_.reset(); needlesResult_.reset();
    };
    if (! needlesWork_)
    {
        const auto checked = demand (needlesDemand_);
        if (checked.rejection != Rejection::None)
        {
            fail (MeasurementReason::Memory, ErrorCode::Memory, checked.needBytes);
            return;
        }
        needlesWork_.reset (new detail::NeedlesWork);
        needlesResult_.reset (new detail::NeedlesResult);
        auto& work = *needlesWork_;
        work.source = needlesSource_; work.key = needlesKey_; work.job = needlesJob_;
        Excursions::Params params; params.thresholdDbtp = *needlesCeilingDb_;
        work.analyzer.setParams (params);
        work.bytes = sizeof (detail::NeedlesWork) + Excursions::storageFor (double (source_.sampleRate), int (source_.channels), params).firstBytes()
                   + allocatorAllowance;
        if (! work.analyzer.prepare (double (source_.sampleRate), int (source_.channels)))
        { fail (MeasurementReason::Unsupported, ErrorCode::Contract, 0); return; }
    }
    else
    {
        auto& work = *needlesWork_;
        if (work.job != needlesJob_ || work.source != source_.hash || work.key != needlesKey_)
        { fail (MeasurementReason::Cancelled, ErrorCode::Stale, 0); return; }
        if (work.frames < source_.frames)
        {
            const auto count = int (std::min<std::uint64_t> (Excursions::kChunk, source_.frames - work.frames));
            const float* channels[2] { samples_.get() + work.frames, samples_.get() + (source_.channels == 2 ? source_.frames : 0) + work.frames };
            if (! work.analyzer.process (channels, int (source_.channels), count))
            { fail (MeasurementReason::Unsupported, ErrorCode::Contract, 0); return; }
            work.frames += std::uint64_t (count); result.framesRead = work.frames;
        }
        else
        {
            if (! work.analyzer.finish())
            { fail (MeasurementReason::Unsupported, ErrorCode::Contract, 0); return; }
            needlesResult_->read (work.analyzer, result);
        }
    }
    ++needlesProgress_.completedUnits;
    needlesProgress_.fraction = double (needlesProgress_.completedUnits) / double (needlesProgress_.totalUnits);
    Notification event; event.jobId = needlesJob_; event.kind = EventKind::Phase; event.payload.phase = needlesProgress_;
    emit (event);
    if (needlesProgress_.completedUnits == needlesProgress_.totalUnits)
    {
        needlesChanged();
        if (needlesResult_->runsTruncated)
        {
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::NeedlesRunsTruncated));
            emit (event);
        }
        needlesJob_ = 0;
        needlesWork_.reset();
    }
}
void detail::NeedlesResult::read (const Excursions& a, MeasurementResult& result) noexcept
{
    runsTruncated = ! a.runsComplete();
    const double values[kNumbers] = { a.sampleRate(), double (a.channels()), a.params().thresholdDbtp, a.thresholdLinear(), a.params().mergeMs,
        double (a.samplesProcessed()), double (a.measuredOs()), double (a.reason()), a.valid() ? 1.0 : 0.0,
        a.reconstructedPeak(), a.samplePeakLinear(), a.truePeakLinear(), double (a.runCount()), double (a.storedRunCount()),
        a.runsComplete() ? 1.0 : 0.0, double (a.aboveOs()), a.occupancy(), a.totalDose(), a.maxExcess(), a.p90Ms(),
        a.p90Saturated() ? 1.0 : 0.0, a.runsPerMinute(), double (a.nonFiniteSamples()), double (a.firstNonFiniteAt()),
        a.doseShareBelow (configured ("bassBelowHz")), a.ceilingDensity(),
        a.ceilingDensityAbove (configured ("densityMinusDb"), configured ("densityWithinDb")), 0.0, a.valid() ? 1.0 : 0.0 };
    constexpr std::string_view names[kNumbers] = { "sampleRate", "channels", "thresholdDbTp", "thresholdLinear", "mergeMs",
        "samplesProcessed", "measuredOs", "reason", "valid", "reconstructedPeak", "samplePeakLinear", "truePeakLinear",
        "runCount", "storedRunCount", "runsComplete", "aboveOs", "occupancy", "totalDose", "maxExcess", "p90Ms",
        "p90Saturated", "runsPerMinute", "nonFiniteSamples", "firstNonFiniteAt", "bassDoseShare", "ceilingDensity",
        "ceilingDensityAbove", "runListIncluded", "aggregatesComplete" };
    for (std::size_t i = 0; i < kNumbers; ++i) numbers[i] = { names[i], values[i], MeasurementReason::None, 0 };
    for (int i = 0; i < Excursions::kDurationBins; ++i) duration[i] = double (a.durationBin (i));
    for (int i = 0; i < Excursions::kCeilingBins; ++i) ceiling[i] = double (a.ceilingBin (i));
    for (int i = 0; i < Excursions::kCrestBins; ++i)
    { crest[3*i] = Excursions::crestBinLowHz (i); crest[3*i+1] = double (a.crestBinCount (i)); crest[3*i+2] = a.crestBinDose (i); }
    for (int i = 0; i < Excursions::kClasses; ++i)
    { classes[3*i] = i < Excursions::kClassEdges ? a.params().classEdgesMs[i] : std::numeric_limits<double>::infinity();
      classes[3*i+1] = double (a.classCount (i)); classes[3*i+2] = a.classDose (i); }
    for (int i = 0; i < a.channels(); ++i) above[i] = double (a.aboveOs (i));
    const MeasurementGrid grid { 0, 0, std::uint64_t (a.samplesProcessed()), std::uint32_t (a.sampleRate()) };
    arrays[0] = { "duration", grid, 1, Excursions::kDurationBins, Excursions::kDurationBins, true, duration };
    arrays[1] = { "ceiling", grid, 1, Excursions::kCeilingBins, Excursions::kCeilingBins, true, ceiling };
    arrays[2] = { "crest", grid, 3, Excursions::kCrestBins, Excursions::kCrestBins, true, crest };
    arrays[3] = { "classes", grid, 3, Excursions::kClasses, Excursions::kClasses, true, classes };
    arrays[4] = { "above", grid, 1, std::uint64_t (a.channels()), std::uint64_t (a.channels()), true, { above, std::size_t (a.channels()) } };
    result.status = a.valid() ? MeasurementStatus::Ready : MeasurementStatus::Unavailable;
    result.reason = a.valid() ? MeasurementReason::None : MeasurementReason::NonFinite;
    result.framesRead = std::uint64_t (a.samplesProcessed());
    result.total = std::uint64_t (a.runCount()); result.stored = 0; result.complete = result.total == 0;
    result.numbers = numbers; result.arrays = arrays;
}
} // namespace felitronics::session
