// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "BuildContract.h"
#include "Utf8.h"
#include <felitronics/session/Measurements.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace felitronics::session
{
namespace
{
struct Sizes
{
    std::uint64_t results = 0, numbers = 0, arrays = 0, rows = 0, chars = 0;
    std::uint64_t bytes() const noexcept
    {
        return results * sizeof (MeasurementResult) + numbers * sizeof (MeasurementValue)
             + arrays * sizeof (MeasurementArray) + rows * sizeof (double) + chars;
    }
};
Sizes sizes (std::span<const MeasurementResult> results) noexcept
{
    Sizes out;
    out.results = results.size();
    for (const auto& r : results)
    {
        out.numbers += r.numbers.size(); out.arrays += r.arrays.size();
        for (const auto& v : r.numbers) out.chars += v.name.size();
        for (const auto& a : r.arrays) { out.chars += a.name.size(); out.rows += a.values.size(); }
    }
    return out;
}
bool counts (std::uint64_t total, std::uint64_t stored, bool complete) noexcept
{
    return stored <= total && (! complete || stored == total);
}
}
text::FactId MeasurementText::fact (MeasurementReason reason) noexcept
{
    switch (reason)
    {
        case MeasurementReason::None: return text::FactId::MeasurementReady;
        case MeasurementReason::Pending: return text::FactId::MeasurementPending;
        case MeasurementReason::Cancelled: return text::FactId::MeasurementStopped;
        case MeasurementReason::Unsupported: return text::FactId::MeasurementUnsupported;
        case MeasurementReason::TooShort: return text::FactId::MeasurementTooShort;
        case MeasurementReason::NonFinite: return text::FactId::MeasurementNonFinite;
        case MeasurementReason::Capacity: return text::FactId::MeasurementCapacity;
        case MeasurementReason::NoSignal: return text::FactId::MeasurementNoSignal;
        case MeasurementReason::NotImplemented: return text::FactId::MeasurementUnavailable;
    }
    detail::storageOverflow();
}
OwnedMeasurements::OwnedMeasurements() noexcept = default;
OwnedMeasurements::~OwnedMeasurements() = default;
OwnedMeasurements::OwnedMeasurements (OwnedMeasurements&& other) noexcept = default;
OwnedMeasurements& OwnedMeasurements::operator= (OwnedMeasurements&& other) noexcept = default;
std::span<const MeasurementResult> OwnedMeasurements::view() const noexcept
{
    return results_ ? std::span<const MeasurementResult> (results_.get(), count_) : std::span<const MeasurementResult> {};
}
bool OwnedMeasurements::valid (std::span<const MeasurementResult> results) noexcept
{
    if (results.size() > kAnalyzers) return false;
    std::uint32_t seen = 0;
    for (const auto& r : results)
    {
        const auto id = unsigned (r.analyzer);
        if (r.numbers.size() > kMeasurementNumbers || r.arrays.size() > kMeasurementArrays) return false;
        if (id >= kAnalyzers || (seen & (1u << id)) != 0) return false;
        seen |= 1u << id;
        if (unsigned (r.status) > unsigned (MeasurementStatus::Cancelled)
            || unsigned (r.reason) > unsigned (MeasurementReason::NotImplemented)
            || ! counts (r.total, r.stored, r.complete)) return false;
        if (r.status == MeasurementStatus::Ready ? r.reason != MeasurementReason::None : r.reason == MeasurementReason::None) return false;
        for (const auto& v : r.numbers)
            if (v.name.empty() || v.name.size() > kMeasurementNameBytes || ! detail::validUtf8 (v.name)
                || unsigned (v.reason) > unsigned (MeasurementReason::NotImplemented)
                || (v.value ? v.reason != MeasurementReason::None : v.reason == MeasurementReason::None)) return false;
        for (const auto& a : r.arrays)
            if (a.name.empty() || a.name.size() > kMeasurementNameBytes || ! detail::validUtf8 (a.name) || a.columns == 0
                || a.values.size() % a.columns != 0 || a.stored != a.values.size() / a.columns
                || ! counts (a.total, a.stored, a.complete)) return false;
    }
    return true;
}
std::uint64_t OwnedMeasurements::storageFor (std::span<const MeasurementResult> results) noexcept
{
    return sizes (results).bytes();
}
OwnedMeasurements OwnedMeasurements::copy (std::span<const MeasurementResult> results) noexcept
{
    OwnedMeasurements out;
    const auto n = sizes (results);
    if (n.bytes() > std::numeric_limits<std::size_t>::max()) detail::storageOverflow();
    if (n.results) out.results_.reset (new MeasurementResult[std::size_t (n.results)]);
    if (n.numbers) out.numbers_.reset (new MeasurementValue[std::size_t (n.numbers)]);
    if (n.arrays) out.arrays_.reset (new MeasurementArray[std::size_t (n.arrays)]);
    if (n.rows) out.rows_.reset (new double[std::size_t (n.rows)]);
    if (n.chars) out.text_.reset (new char[std::size_t (n.chars)]);
    out.count_ = results.size();
    std::size_t ni = 0, ai = 0, ri = 0, ti = 0;
    const auto name = [&] (std::string_view s)
    {
        if (s.empty()) return std::string_view {};
        char* p = out.text_.get() + ti; ti += s.size();
        std::copy (s.begin(), s.end(), p);
        return std::string_view (p, s.size());
    };
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& source = results[i];
        auto& target = out.results_[i]; target = source;
        target.numbers = source.numbers.empty() ? std::span<const MeasurementValue> {} : std::span<const MeasurementValue> (out.numbers_.get() + ni, source.numbers.size());
        for (auto value : source.numbers) { value.name = name (value.name); out.numbers_[ni++] = value; }
        target.arrays = source.arrays.empty() ? std::span<const MeasurementArray> {} : std::span<const MeasurementArray> (out.arrays_.get() + ai, source.arrays.size());
        for (auto array : source.arrays)
        {
            array.name = name (array.name);
            if (! array.values.empty())
            {
                std::copy (array.values.begin(), array.values.end(), out.rows_.get() + ri);
                array.values = { out.rows_.get() + ri, array.values.size() }; ri += array.values.size();
            }
            out.arrays_[ai++] = array;
        }
    }
    return out;
}
} // namespace felitronics::session
