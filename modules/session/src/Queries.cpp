// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "BuildContract.h"
#include "QueryState.h"
#include <felitronics/session/Session.h>
#include <felitronics/session/Landing.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace felitronics::session
{
namespace
{
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
std::uint64_t boundary (std::uint64_t length, std::uint64_t i, std::uint64_t count) noexcept
{ return (length / count) * i + ((length % count) * i) / count; }
bool sameNumber (double a, double b) noexcept
{ return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
bool masterKind (QueryKind kind) noexcept
{ return kind == QueryKind::LimiterGr || kind == QueryKind::PeakClipGr || kind == QueryKind::MasterWaveform; }
const Kept* masterFor (std::span<const Kept> masters, std::uint32_t id) noexcept
{
    for (const Kept& master : masters)
        if (master.id == id) return &master;
    return nullptr;
}
const LandingTrace* masterTrace (const Kept& master, QueryKind kind) noexcept
{
    if (! master.landing) return nullptr;
    const auto& trace = kind == QueryKind::LimiterGr
        ? master.landing->limiterTrace : master.landing->peakClipTrace;
    if (trace) return &*trace;
    return nullptr;
}
QueryStatus validate (const MeasurementQuery& q, const Source& source) noexcept
{
    if (unsigned (q.kind) > unsigned (QueryKind::MasterWaveform) || ! std::isfinite (q.crossoverHz)
        || (! sameNumber (q.crossoverHz, 120) && ! sameNumber (q.crossoverHz, 150)) || ! std::isfinite (q.fromHz) || ! std::isfinite (q.toHz)) return QueryStatus::Contract;
    if (masterKind (q.kind))
    {
        if (q.audioId == 0 || q.masterId == 0) return QueryStatus::Contract;
        if (q.columns == 0 || q.columns > kQueryColumns) return QueryStatus::ColumnLimit;
        return QueryStatus::Ready;
    }
    if (q.masterId != 0) return QueryStatus::Contract;
    if (q.audioId == 0 || q.audioId != source.hash) return QueryStatus::StaleSource;
    if (q.columns == 0 || q.columns > kQueryColumns) return QueryStatus::ColumnLimit;
    if (q.fromFrame > q.toFrame || q.toFrame > source.frames) return QueryStatus::InvalidRange;
    if (q.fromFrame == q.toFrame) return QueryStatus::Empty;
    if (q.kind == QueryKind::LowSpectrum || q.kind == QueryKind::LowSide)
    {
        if (q.fromFrame != 0 || q.toFrame != source.frames || q.fromHz < 0 || q.fromHz > q.toHz
            || q.toHz > double (source.sampleRate) * 0.5) return QueryStatus::InvalidRange;
    }
    return QueryStatus::Ready;
}
Analyzer analyzer (const MeasurementQuery& q) noexcept
{
    switch (q.kind)
    {
        case QueryKind::Waveform: return Analyzer::Waveform;
        case QueryKind::LowSpectrum: case QueryKind::LowSide: return sameNumber (q.crossoverHz, 150) ? Analyzer::LowEnd150 : Analyzer::LowEnd;
        case QueryKind::Momentary: case QueryKind::ShortTerm: return Analyzer::Loudness;
        case QueryKind::Clipping: return Analyzer::Clipping;
        case QueryKind::Stereo: return Analyzer::Stereo;
        case QueryKind::LimiterGr: case QueryKind::PeakClipGr: case QueryKind::MasterWaveform: break;
    }
    detail::storageOverflow();
}
const MeasurementArray* array (const MeasurementResult& r, std::string_view name) noexcept
{
    for (const auto& a : r.arrays) if (a.name == name) return &a;
    return nullptr;
}
std::uint32_t stride (QueryKind kind) noexcept
{
    switch (kind)
    {
        case QueryKind::Waveform: return kWaveformStride;
        case QueryKind::LowSpectrum: case QueryKind::LowSide: return 3;
        case QueryKind::Momentary: case QueryKind::ShortTerm: return 3;
        case QueryKind::Clipping: return 6;
        case QueryKind::Stereo: return 6;
        case QueryKind::LimiterGr: case QueryKind::PeakClipGr: case QueryKind::MasterWaveform: return 7;
    }
    detail::storageOverflow();
}
std::uint64_t maxRows (const MeasurementQuery& q) noexcept
{
    const auto n = std::uint64_t (q.columns);
    return q.kind == QueryKind::Waveform ? 4u * std::min (n, q.toFrame - q.fromFrame) : n;
}
bool equal (const MeasurementQuery& a, const MeasurementQuery& b) noexcept
{
    return a.kind == b.kind && a.audioId == b.audioId && a.masterId == b.masterId && a.fromFrame == b.fromFrame && a.toFrame == b.toFrame
        && a.columns == b.columns && sameNumber (a.crossoverHz, b.crossoverHz) && sameNumber (a.fromHz, b.fromHz) && sameNumber (a.toHz, b.toHz);
}
std::uint64_t freshness (const MeasurementResult& r) noexcept
{
    auto h = r.key;
    for (auto v : { r.framesRead, r.stored, std::uint64_t (r.status), std::uint64_t (r.reason), std::uint64_t (r.complete) })
        h = (h ^ v) * 0x100000001B3ull;
    return h;
}
QueryView header (const MeasurementQuery& q, const Source& source, std::uint64_t revision, std::uint64_t key) noexcept
{
    QueryView v; v.request = q; v.audioId = source.hash; v.revision = revision; v.measurementKey = key;
    v.sampleRate = source.sampleRate; v.channels = source.channels; v.stride = stride (q.kind);
    return v;
}
void curve (QueryView& v, double* rows, const MeasurementResult& result) noexcept
{
    const auto* bands = array (result, "bands");
    if (! bands || bands->stored == 0) return;
    const auto& q = v.request;
    for (std::uint32_t i = 0; i < q.columns; ++i)
    {
        const double hz = q.columns == 1 ? q.fromHz : q.fromHz + (q.toHz - q.fromHz) * double (i) / double (q.columns - 1u);
        double value = missing; auto reason = MeasurementReason::Unsupported;
        for (std::uint64_t j = 0; j < bands->stored; ++j)
        {
            const auto* a = bands->values.data() + std::size_t (16u * j);
            const auto* b = j + 1u < bands->stored ? a + 16 : a;
            if (hz < a[1] || hz > b[1]) continue;
            if (a[15] <= 0 || b[15] <= 0) reason = MeasurementReason::TooShort;
            else
            {
                const double t = a == b ? 0 : (hz - a[1]) / (b[1] - a[1]);
                const double mid = a[4] + t * (b[4] - a[4]), side = a[5] + t * (b[5] - a[5]);
                reason = mid + side > 0 ? MeasurementReason::None : MeasurementReason::NoSignal;
                value = q.kind == QueryKind::LowSpectrum ? a[7] + t * (b[7] - a[7])
                    : mid + side > 0 ? side / (mid + side) : missing;
                if (! std::isfinite (value) && reason == MeasurementReason::None) reason = MeasurementReason::NonFinite;
            }
            break;
        }
        if (q.kind == QueryKind::LowSide && v.channels == 1) { value = missing; reason = MeasurementReason::Unsupported; }
        const double row[] { hz, value, double (reason) };
        std::copy_n (row, 3, rows + std::size_t (3u * i));
    }
    v.total = v.stored = q.columns; v.complete = result.complete;
}
void loudness (QueryView& v, double* rows, const MeasurementResult& result) noexcept
{
    const bool momentary = v.request.kind == QueryKind::Momentary;
    const auto* values = array (result, momentary ? "momentary" : "shortTerm");
    const auto* reasons = array (result, momentary ? "momentaryReasons" : "shortTermReasons");
    if (! values || ! reasons || values->grid.stepFrames == 0) return;
    const auto& grid = values->grid; const auto& q = v.request;
    // A row names the end of its window. The query is [from, to), so include an
    // end at to and exclude one at from; otherwise the source's last row is lost.
    const auto offset = [&] (std::uint64_t frame)
    { return frame < grid.firstFrame ? std::uint64_t (0)
        : std::min (values->stored, 1u + (frame - grid.firstFrame) / grid.stepFrames); };
    const auto first = offset (q.fromFrame), end = offset (q.toFrame);
    v.total = end - first; v.stored = std::min (v.total, std::uint64_t (q.columns));
    for (std::uint64_t i = 0; i < v.stored; ++i)
    {
        const auto at = first + (v.stored == 1 ? 0 : boundary (v.total - 1u, i, v.stored - 1u));
        const double row[] { double (grid.firstFrame + at * grid.stepFrames), values->values[std::size_t (at)], reasons->values[std::size_t (at)] };
        std::copy_n (row, 3, rows + std::size_t (3u * i));
    }
    v.complete = values->complete && v.stored == v.total;
}
void clips (QueryView& v, double* rows, const MeasurementResult& result) noexcept
{
    const auto* runs = array (result, "clips");
    if (! runs) return;
    for (std::uint64_t i = 0; i < runs->stored; ++i)
    {
        const auto* row = runs->values.data() + std::size_t (6u * i);
        if (row[0] >= double (v.request.toFrame) || row[0] + row[1] <= double (v.request.fromFrame)) continue;
        ++v.total;
        if (v.stored < v.request.columns) std::copy_n (row, 6, rows + std::size_t (6u * v.stored++));
    }
    v.complete = runs->complete && v.stored == v.total;
}
void stereo (QueryView& v, double* rows, const MeasurementResult& result, std::uint64_t frames) noexcept
{
    const auto* data = array (result, "columns");
    if (! data || ! data->stored) return;
    const auto& q = v.request;
    // StereoColumns uses these floating-point boundaries, including its known
    // rounding behavior. A retained value must carry the interval that made it.
    const double per = double (frames) / double (data->stored);
    const auto bounds = [&] (std::uint64_t i)
    {
        return std::pair { std::uint64_t (std::floor (double (i) * per)),
            std::min (frames, std::uint64_t (std::floor (double (i + 1u) * per))) };
    };
    for (std::uint64_t i = 0; i < data->stored; ++i)
    {
        const auto [first, last] = bounds (i);
        if (first < q.toFrame && last > q.fromFrame) ++v.total;
    }
    v.stored = std::min (v.total, std::uint64_t (q.columns));
    std::uint64_t ordinal = 0, selected = 0;
    for (std::uint64_t i = 0; i < data->stored && selected < v.stored; ++i)
    {
        const auto [first, last] = bounds (i);
        if (first >= q.toFrame || last <= q.fromFrame) continue;
        const auto target = v.stored == 1 ? v.total / 2u : boundary (v.total - 1u, selected, v.stored - 1u);
        if (ordinal++ != target) continue;
        const auto* source = data->values.data() + std::size_t (i * 3u);
        const auto reason = std::isfinite (source[2]) ? source[2] <= 0 ? MeasurementReason::NoSignal : MeasurementReason::None : MeasurementReason::NonFinite;
        const double row[] { double (first), double (last), source[0], source[1], source[2], double (reason) };
        std::copy_n (row, 6, rows + std::size_t (6u * selected++));
    }
    v.complete = data->complete && v.total != 0 && v.stored == v.total;
}
}
QueryResult::QueryResult() noexcept = default;
QueryResult::~QueryResult() = default;
QueryResult::QueryResult (QueryResult&&) noexcept = default;
QueryResult& QueryResult::operator= (QueryResult&&) noexcept = default;
const QueryView& QueryResult::view() const noexcept { return view_; }
std::uint64_t QueryResult::storageFor (const QueryView& view) noexcept
{ return view.values.empty() ? 0 : std::uint64_t (view.values.size()) * sizeof (double) + 128u; }
QueryResult QueryResult::copy (const QueryView& view) noexcept
{
    QueryResult out; out.view_ = view;
    if (! view.values.empty())
    {
        if (view.values.size() > kQueryValues) detail::storageOverflow();
        out.rows_.reset (new double[view.values.size()]);
        std::copy (view.values.begin(), view.values.end(), out.rows_.get());
        out.view_.values = { out.rows_.get(), view.values.size() };
    }
    return out;
}
std::uint64_t detail::QueryCache::bytes() const noexcept
{ return sizeof (QueryCache) + entries[0].bytes + entries[1].bytes; }
void Session::invalidateQueryCache (Analyzer id) noexcept
{
    if (! queryCache_) return;
    for (auto& entry : queryCache_->entries)
        if (entry.request.audioId != 0 && analyzer (entry.request) == id) entry = {};
}
QueryDemand Session::queryStorage (const MeasurementQuery& q) const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok) return { QueryStatus::FloatingPointEnvironment, 0, 0, 0 };
    const auto status = validate (q, source_);
    if (status != QueryStatus::Ready) return { status, 0, 0 };
    if (masterKind (q.kind))
    {
        const Kept* master = masterFor (masters(), q.masterId);
        if (! master) return { QueryStatus::Unavailable, 0, 0, 0 };
        if (master->recipe.source != q.audioId) return { QueryStatus::StaleSource, 0, 0, 0 };
        if (q.kind == QueryKind::MasterWaveform)
        {
            if (! master->report || ! master->report->cost || master->report->cost->waveform.empty())
                return { QueryStatus::Unavailable, 0, 0, 0 };
            const auto& cost = *master->report->cost;
            if (q.fromFrame > q.toFrame || q.toFrame > cost.masterFrames) return { QueryStatus::InvalidRange, 0, 0, 0 };
            if (q.fromFrame == q.toFrame) return { QueryStatus::Empty, 0, 0, 0 };
            std::uint64_t buckets = 0;
            for (std::size_t i = 0; i < cost.waveform.size(); i += 1u + (cost.waveform.size() > 1 && cost.waveform[1].channel == 1 ? 1u : 0u))
                if (cost.waveform[i].fromFrame < q.toFrame && cost.waveform[i].toFrame > q.fromFrame) ++buckets;
            const auto channels = cost.waveform.size() > 1 && cost.waveform[1].channel == 1 ? 2u : 1u;
            const auto bytes = std::uint64_t (std::min<std::uint64_t> (q.columns, buckets) * channels) * 7u * sizeof (double) + 128u;
            return { QueryStatus::Ready, 2u * bytes, bytes, bytes - 128u };
        }
        const LandingTrace* trace = masterTrace (*master, q.kind);
        if (! trace) return { QueryStatus::Unavailable, 0, 0, 0 };
        if (q.fromFrame < trace->fromFrame || q.fromFrame > q.toFrame || q.toFrame > trace->toFrame)
            return { QueryStatus::InvalidRange, 0, 0, 0 };
        if (q.fromFrame == q.toFrame) return { QueryStatus::Empty, 0, 0, 0 };
        if (! trace->complete) return { QueryStatus::Pending, 0, 0, 0 };
        const auto [first, last] = LandingOps::bucketRange (*trace, q.fromFrame, q.toFrame);
        const std::uint64_t columns = std::min<std::uint64_t> (q.columns, last - first);
        const std::uint64_t rowBytes = columns * 7u * sizeof (double);
        const std::uint64_t block = rowBytes + 128u;
        return { QueryStatus::Ready, 2u * block, block, rowBytes };
    }
    const auto& r = measurementResults_[std::size_t (analyzer (q))];
    if (q.kind == QueryKind::Waveform)
    {
        if (! waveform_ || ! waveform_->prepared || q.toFrame > waveform_->index.framesSeen())
            return { QueryStatus::Ready, 0, 0, 0 };
    }
    else if (r.status == MeasurementStatus::Unavailable || r.arrays.empty())
        return { QueryStatus::Ready, 0, 0, 0 };
    const auto key = freshness (r);
    if (queryCache_) for (const auto& e : queryCache_->entries)
        if (e.key == key && equal (e.request, q))
        { const auto bytes = QueryResult::storageFor (e.result.view()); return { QueryStatus::Ready, bytes, bytes, std::uint64_t (e.result.view().values.size()) * sizeof (double) }; }
    const auto bytes = maxRows (q) * stride (q.kind) * sizeof (double) + 128u;
    // Temporary working rows, cache copy, and detached response coexist on a miss.
    return { QueryStatus::Ready, 3u * bytes + (queryCache_ ? 0 : sizeof (detail::QueryCache)), std::max (bytes, queryCache_ ? 0u : std::uint64_t (sizeof (detail::QueryCache))), bytes - 128u };
}
QueryResult Session::query (const MeasurementQuery& q) noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok)
    { QueryView v; v.request = q; v.status = QueryStatus::FloatingPointEnvironment; v.reason = MeasurementReason::Unsupported; return QueryResult::copy (v); }
    // Invalid enum cannot reach stride/analyzer dispatch.
    if (unsigned (q.kind) > unsigned (QueryKind::MasterWaveform))
    { QueryView v; v.request = q; v.status = QueryStatus::Contract; v.reason = MeasurementReason::Unsupported; return QueryResult::copy (v); }
    if (masterKind (q.kind))
    {
        QueryView v; v.request = q; v.audioId = q.audioId; v.revision = revision_; v.stride = 7;
        const auto need = queryStorage (q);
        v.status = need.status;
        v.reason = need.status == QueryStatus::Ready ? MeasurementReason::None : MeasurementReason::Unsupported;
        if (need.status != QueryStatus::Ready) return QueryResult::copy (v);
        Checked request; request.bytes = need.bytes; request.largestBlockBytes = need.largestBlockBytes;
        if (demand (request).rejection != Rejection::None)
        { v.status = QueryStatus::Memory; v.reason = MeasurementReason::Memory; return QueryResult::copy (v); }
        if (q.kind == QueryKind::MasterWaveform)
        {
            const auto& cost = *masterFor (masters(), q.masterId)->report->cost;
            const auto channels = cost.waveform.size() > 1 && cost.waveform[1].channel == 1 ? 2u : 1u;
            std::uint64_t first = 0, last = 0;
            const auto bucketCount = cost.waveform.size() / channels;
            while (first < bucketCount && cost.waveform[std::size_t (first * channels)].toFrame <= q.fromFrame) ++first;
            last = first;
            while (last < bucketCount && cost.waveform[std::size_t (last * channels)].fromFrame < q.toFrame) ++last;
            const auto groups = std::min<std::uint64_t> (q.columns, last - first);
            v.sampleRate = cost.masterRateHz; v.channels = channels; v.measurementKey = q.masterId;
            v.total = (last - first) * channels; v.stored = groups * channels;
            v.complete = true; v.reason = MeasurementReason::None;
            std::unique_ptr<double[]> rows (new double[std::size_t (v.stored * v.stride)]);
            for (std::uint64_t group = 0; group < groups; ++group)
            {
                const auto begin = first + boundary (last - first, group, groups);
                const auto end = first + boundary (last - first, group + 1u, groups);
                for (std::uint32_t ch = 0; ch < channels; ++ch)
                {
                    const auto& head = cost.waveform[std::size_t (begin * channels + ch)];
                    const auto& tail = cost.waveform[std::size_t ((end - 1u) * channels + ch)];
                    double lo = head.minimum, hi = head.maximum, energy = 0; std::uint64_t finite = 0;
                    for (auto i = begin; i < end; ++i)
                    {
                        const auto& bucket = cost.waveform[std::size_t (i * channels + ch)];
                        lo = std::min (lo, bucket.minimum); hi = std::max (hi, bucket.maximum);
                        energy += bucket.rms * bucket.rms * double (bucket.finite);
                        finite += bucket.finite;
                    }
                    const double row[] { double (head.fromFrame), double (tail.toFrame), double (ch), lo, hi,
                        finite ? std::sqrt (energy / double (finite)) : missing, double (finite) };
                    std::copy_n (row, 7, rows.get() + std::size_t ((group * channels + ch) * 7u));
                }
            }
            v.values = { rows.get(), std::size_t (v.stored * v.stride) };
            return QueryResult::copy (v);
        }
        const LandingTrace* trace = masterTrace (*masterFor (masters(), q.masterId), q.kind);
        const auto [first, last] = LandingOps::bucketRange (*trace, q.fromFrame, q.toFrame);
        v.sampleRate = trace->sampleRateHz;
        v.measurementKey = q.masterId;
        v.total = last - first;
        v.stored = std::min<std::uint64_t> (q.columns, v.total);
        v.complete = trace->complete && v.stored == v.total;
        if (! trace->valid) v.reason = MeasurementReason::NonFinite;
        std::unique_ptr<double[]> rows (new double[std::size_t (v.stored * v.stride)]);
        if (LandingOps::query (*trace, q.fromFrame, q.toFrame, q.columns,
                               { rows.get(), std::size_t (v.stored * v.stride) }) != v.stored)
            detail::storageOverflow();
        v.values = { rows.get(), std::size_t (v.stored * v.stride) };
        return QueryResult::copy (v);
    }
    auto v = header (q, source_, revision_, measurementKey_);
    const auto need = queryStorage (q); v.status = need.status;
    v.reason = need.status == QueryStatus::Empty ? MeasurementReason::None : MeasurementReason::Unsupported;
    v.complete = need.status == QueryStatus::Empty;
    if (need.status != QueryStatus::Ready) return QueryResult::copy (v);
    Checked request; request.bytes = need.bytes; request.largestBlockBytes = need.largestBlockBytes;
    if (demand (request).rejection != Rejection::None)
    { v.status = QueryStatus::Memory; v.reason = MeasurementReason::Memory; return QueryResult::copy (v); }
    const auto& result = measurementResults_[std::size_t (analyzer (q))];
    const auto key = freshness (result);
    if (queryCache_) for (const auto& e : queryCache_->entries)
        if (e.key == key && equal (e.request, q))
        {
            auto cached = e.result.view(); cached.request = q; cached.revision = revision_; cached.cacheHit = true; cached.pcmFramesRead = 0;
            return QueryResult::copy (cached);
        }
    v.reason = result.reason;
    v.status = result.status == MeasurementStatus::Cancelled ? QueryStatus::Cancelled
        : result.status == MeasurementStatus::Unavailable ? QueryStatus::Unavailable
        : result.status == MeasurementStatus::Pending ? QueryStatus::Pending : QueryStatus::Ready;
    if (need.bytes == 0 || result.status == MeasurementStatus::Unavailable) return QueryResult::copy (v);
    std::unique_ptr<double[]> rows (new double[std::size_t (maxRows (q) * v.stride)]);
    if (q.kind == QueryKind::Waveform)
    {
        if (waveform_ && waveform_->prepared && q.toFrame <= waveform_->index.framesSeen())
        {
            const auto n = std::min<std::uint64_t> (q.columns, q.toFrame - q.fromFrame);
            const float* planes[] { samples_.get(), source_.channels == 2 ? samples_.get() + source_.frames : nullptr };
            for (std::uint64_t i = 0; i < n; ++i)
            {
                const auto first = q.fromFrame + boundary (q.toFrame - q.fromFrame, i, n);
                const auto last = q.fromFrame + boundary (q.toFrame - q.fromFrame, i + 1u, n);
                analysis::WaveformColumn column {};
                if (! waveform_->index.read (planes, first, last, column, v.pcmFramesRead)) detail::storageOverflow();
                for (unsigned axis = 0; axis < 4; ++axis)
                {
                    const auto& a = column.axes[axis];
                    const bool present = waveform_->index.axisPresent (axis);
                    const auto why = ! present ? MeasurementReason::Unsupported : a.finite != last - first ? MeasurementReason::NonFinite
                        : a.squareSum <= 0 ? MeasurementReason::NoSignal : MeasurementReason::None;
                    const double row[] { double (first), double (last), double (axis), a.finite ? a.minimum : missing,
                        a.finite ? a.maximum : missing, a.finite ? std::max (std::fabs (a.minimum), std::fabs (a.maximum)) : missing,
                        a.finite ? a.envelope : missing, a.finite ? std::sqrt (a.squareSum / double (a.finite)) : missing,
                        a.finite ? a.lowEnergy : missing, a.finite ? a.middleEnergy : missing, a.finite ? a.highEnergy : missing,
                        double (a.finite), double (why) };
                    std::copy_n (row, kWaveformStride, rows.get() + std::size_t (v.stored++ * kWaveformStride));
                }
            }
            v.total = v.stored; v.complete = true; v.status = QueryStatus::Ready; v.reason = MeasurementReason::None;
        }
    }
    else switch (q.kind)
    {
        case QueryKind::LowSpectrum: case QueryKind::LowSide: curve (v, rows.get(), result); break;
        case QueryKind::Momentary: case QueryKind::ShortTerm: loudness (v, rows.get(), result); break;
        case QueryKind::Clipping: clips (v, rows.get(), result); break;
        case QueryKind::Stereo: stereo (v, rows.get(), result, source_.frames); break;
        case QueryKind::Waveform: break;
        case QueryKind::LimiterGr: case QueryKind::PeakClipGr: case QueryKind::MasterWaveform: break;
    }
    v.values = { rows.get(), std::size_t (v.stored * v.stride) };
    if (! queryCache_) queryCache_.reset (new detail::QueryCache);
    auto& entry = queryCache_->entries[queryCache_->next]; queryCache_->next = (queryCache_->next + 1u) % 2u;
    entry.request = q; entry.key = key; entry.result = QueryResult::copy (v); entry.bytes = QueryResult::storageFor (v);
    return QueryResult::copy (v);
}
QueryDemand Session::masterWaveformChunkStorage (const MeasurementQuery& q, const Pcm& chunk) const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok)
        return { QueryStatus::FloatingPointEnvironment, 0, 0, 0 };
    const auto checked = validate (q, source_);
    if (checked != QueryStatus::Ready) return { checked, 0, 0, 0 };
    if (q.kind != QueryKind::MasterWaveform || q.fromFrame >= q.toFrame
        || q.toFrame - q.fromFrame != chunk.frames || chunk.frames > 65536u)
        return { QueryStatus::InvalidRange, 0, 0, 0 };
    const auto* master = masterFor (masters(), q.masterId);
    if (! master || ! master->report || ! master->report->cost)
        return { QueryStatus::Unavailable, 0, 0, 0 };
    if (master->recipe.source != q.audioId) return { QueryStatus::StaleSource, 0, 0, 0 };
    const auto& cost = *master->report->cost;
    const auto masterChannels = cost.waveform.size() > 1 && cost.waveform[1].channel == 1 ? 2u : 1u;
    if (q.toFrame > cost.masterFrames || chunk.sampleRate != cost.masterRateHz
        || chunk.channelCount != masterChannels)
        return { QueryStatus::Contract, 0, 0, 0 };
    const auto columns = std::min<std::uint64_t> (q.columns, chunk.frames);
    const auto rowBytes = columns * chunk.channelCount * 7u * sizeof (double);
    const auto block = rowBytes + 128u;
    return { QueryStatus::Ready, 2u * block, block, rowBytes };
}
QueryResult Session::masterWaveformChunk (const MeasurementQuery& q, const Pcm& chunk) noexcept
{
    QueryView view; view.request = q; view.audioId = q.audioId; view.revision = revision_;
    view.status = masterWaveformChunkStorage (q, chunk).status;
    view.reason = view.status == QueryStatus::Ready ? MeasurementReason::None : MeasurementReason::Unsupported;
    view.stride = 7;
    if (view.status != QueryStatus::Ready) return QueryResult::copy (view);
    if (! chunk.channels || ! chunk.channels[0] || (chunk.channelCount == 2 && ! chunk.channels[1]))
    { view.status = QueryStatus::Contract; view.reason = MeasurementReason::Unsupported; return QueryResult::copy (view); }
    const auto need = masterWaveformChunkStorage (q, chunk);
    Checked storage; storage.bytes = need.bytes; storage.largestBlockBytes = need.largestBlockBytes;
    if (demand (storage).rejection != Rejection::None)
    { view.status = QueryStatus::Memory; view.reason = MeasurementReason::Memory; return QueryResult::copy (view); }
    view.sampleRate = chunk.sampleRate; view.channels = chunk.channelCount; view.measurementKey = q.masterId;
    const auto columns = std::min<std::uint64_t> (q.columns, chunk.frames);
    view.total = view.stored = columns * chunk.channelCount;
    view.complete = true; view.pcmFramesRead = chunk.frames;
    std::unique_ptr<double[]> rows (new double[std::size_t (view.stored * view.stride)]);
    for (std::uint64_t i = 0; i < columns; ++i)
    {
        const auto first = boundary (chunk.frames, i, columns);
        const auto last = boundary (chunk.frames, i + 1u, columns);
        for (std::uint32_t ch = 0; ch < chunk.channelCount; ++ch)
        {
            double lo = 0, hi = 0, sum = 0; std::uint64_t finite = 0;
            for (auto frame = first; frame < last; ++frame)
            {
                const double x = chunk.channels[ch][std::size_t (frame)];
                if (! std::isfinite (x)) continue;
                if (finite == 0) { lo = x; hi = x; }
                else { lo = std::min (lo, x); hi = std::max (hi, x); }
                sum += x * x; ++finite;
            }
            const double row[] { double (q.fromFrame + first), double (q.fromFrame + last), double (ch),
                finite ? lo : missing, finite ? hi : missing,
                finite ? std::sqrt (sum / double (finite)) : missing, double (finite) };
            std::copy_n (row, 7, rows.get() + std::size_t ((i * chunk.channelCount + ch) * 7u));
        }
    }
    view.values = { rows.get(), std::size_t (view.stored * view.stride) };
    return QueryResult::copy (view);
}
} // namespace felitronics::session
