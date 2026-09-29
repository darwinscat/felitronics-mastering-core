// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "Cost.h"
#include <felitronics/core/DetMath.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace felitronics::session::detail
{
namespace
{
using Math = core::DetMath;
constexpr double pi = 3.14159265358979323846264338327950288;
struct LessDouble
{
    bool operator() (double a, double b) const noexcept { return a < b; }
};

using Biquad = CostBiquad;
Biquad butterworth (double cutoff, double rate, bool high) noexcept
{
    const double k = Math::tan (pi * cutoff / rate);
    const double k2 = k * k, root2 = 1.4142135623730950488;
    const double scale = 1.0 / (1.0 + root2 * k + k2);
    Biquad b;
    b.b0 = (high ? 1.0 : k2) * scale;
    b.b1 = (high ? -2.0 : 2.0) * b.b0;
    b.b2 = b.b0;
    b.a1 = 2.0 * (k2 - 1.0) * scale;
    b.a2 = (1.0 - root2 * k + k2) * scale;
    return b;
}
}
MasterCostValue CostMath::crest (const analysis::BandCrestResult& source,
                                 const MasterCrest& master, unsigned band,
                                 std::span<double> scratch) noexcept
{
    MasterCostValue out;
    if (band >= 5 || master.status != MeasurementStatus::Ready || ! master.complete
        || source.reason != analysis::BandCrestInvalid::None
        || source.blockCount() != master.blocks || master.rows.size() != master.blocks * 10u
        || master.sourceMask.size() != master.blocks * 5u || scratch.size() < master.blocks)
    { out.reason = MeasurementReason::Unsupported; return out; }
    for (std::size_t i = 0; i < master.blocks; ++i)
    {
        if (master.sourceMask[i * 5u + band] <= 0) continue;
        const double sp = source.blockPeakLin (i, band), se = source.blockMeanSq (i, band);
        const double mp = master.rows[i * 10u + band * 2u];
        const double me = master.rows[i * 10u + band * 2u + 1u];
        if (! std::isfinite (sp) || ! std::isfinite (se) || ! std::isfinite (mp)
            || ! std::isfinite (me) || sp <= 0 || se <= 0 || mp <= 0 || me <= 0)
        { out.reason = MeasurementReason::NonFinite; return out; }
        const double loss = 20.0 * Math::log10 (sp / mp) - 10.0 * Math::log10 (se / me);
        if (! std::isfinite (loss)) { out.reason = MeasurementReason::NonFinite; return out; }
        scratch[std::size_t (out.compared++)] = std::max (0.0, loss);
    }
    if (out.compared == 0) { out.reason = MeasurementReason::NoSignal; return out; }
    const auto greaterBits = [] (double a, double b) noexcept
    { return std::bit_cast<std::uint64_t> (a) > std::bit_cast<std::uint64_t> (b); };
    const auto tailEnd = scratch.begin() + std::ptrdiff_t (out.compared);
    std::make_heap (scratch.begin(), tailEnd, greaterBits);
    std::sort_heap (scratch.begin(), tailEnd, greaterBits);
    const double tail = 0.05 * double (out.compared);
    const auto whole = std::uint64_t (std::floor (tail));
    double sum = 0;
    for (std::uint64_t i = 0; i < whole; ++i) sum += scratch[std::size_t (i)];
    if (double (whole) < tail) sum += (tail - double (whole)) * scratch[std::size_t (whole)];
    out.value = sum / tail;
    out.reason = MeasurementReason::None;
    return out;
}
MasterCostValue CostMath::shape (std::span<const double> source, std::span<const double> master,
                                 std::uint32_t sourceRate, std::uint64_t sourceHop,
                                 std::uint32_t masterRate, std::uint64_t masterHop,
                                 double sourceIntegrated, double masterIntegrated,
                                 const CostRules& rules, std::span<MasterSection> sections,
                                 std::span<double> scratch, std::uint64_t& sectionCount) noexcept
{
    sectionCount = 0;
    MasterCostValue out;
    if (sourceRate == 0 || masterRate == 0 || sourceHop == 0 || masterHop == 0
        || source.empty() || master.empty() || sections.size() < source.size()
        || scratch.size() < source.size() || ! std::isfinite (sourceIntegrated)
        || ! std::isfinite (masterIntegrated))
    { out.reason = MeasurementReason::Unsupported; return out; }
    const double sourceSeconds = double (source.size() * sourceHop) / double (sourceRate);
    const double masterSeconds = double (master.size() * masterHop) / double (masterRate);
    if (sourceSeconds - masterSeconds > rules.maxShortfallSeconds + 1e-9)
    { out.reason = MeasurementReason::TooShort; return out; }
    if (sourceIntegrated < rules.tooQuietLufs || masterIntegrated < rules.tooQuietLufs)
    { out.reason = MeasurementReason::NoSignal; return out; }
    const double sourceStep = double (sourceHop) / double (sourceRate);
    const double masterStep = double (masterHop) / double (masterRate);
    if (std::fabs (sourceStep - .1) > .0001 || std::fabs (masterStep - .1) > .0001)
    { out.reason = MeasurementReason::Unsupported; return out; }
    const double gate = std::max (rules.activityFloorLufs, sourceIntegrated - rules.activityBelowLu);
    const double gain = masterIntegrated - sourceIntegrated;
    const auto count = std::min (source.size(), master.size());
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        if (double (i + 1u) * sourceStep + 1e-9 < 3.0) continue;
        const double s = source[i];
        if (! std::isfinite (s)) continue;
        const auto first = std::uint64_t (i) * sourceHop;
        const auto last = first + sourceHop;
        if (sectionCount == 0 || std::fabs (s - sections[std::size_t (sectionCount - 1u)].sourceLufs) > rules.changeLu)
            sections[std::size_t (sectionCount++)] = { first, last, s, 0, 0, false };
        else
        {
            auto& section = sections[std::size_t (sectionCount - 1u)];
            const auto old = (section.toFrame - section.fromFrame) / sourceHop;
            section.sourceLufs = (section.sourceLufs * double (old) + s) / double (old + 1u);
            section.toFrame = last;
        }
        if (i < count && s >= gate && std::isfinite (master[i]))
            scratch[std::size_t (out.compared++)] = std::fabs ((master[i] - s) - gain);
    }
    if (sectionCount == 0) { out.reason = MeasurementReason::TooShort; return out; }
    const auto merge = [&] (std::size_t left) noexcept
    {
        auto& a = sections[left]; const auto& b = sections[left + 1u];
        const double na = double (a.toFrame - a.fromFrame), nb = double (b.toFrame - b.fromFrame);
        a.sourceLufs = (a.sourceLufs * na + b.sourceLufs * nb) / (na + nb);
        a.toFrame = b.toFrame;
        for (std::size_t j = left + 1u; j + 1u < sectionCount; ++j) sections[j] = sections[j + 1u];
        --sectionCount;
    };
    // A short section joins the nearer neighbour in source loudness; a tie joins the earlier one.
    // Then adjacent sections separated by no more than changeLu become one section.
    bool changed = true;
    while (changed && sectionCount > 1)
    {
        changed = false;
        for (std::size_t i = 0; i < sectionCount; ++i)
        {
            if (double (sections[i].toFrame - sections[i].fromFrame) / double (sourceRate) + 1e-9 >= rules.minSeconds) continue;
            const std::size_t left = i == 0 ? 0 : i - 1u;
            const bool right = i + 1u < sectionCount;
            const std::size_t at = ! right ? left : i == 0 ? 0
                : std::fabs (sections[i].sourceLufs - sections[left].sourceLufs)
                    <= std::fabs (sections[i].sourceLufs - sections[i + 1u].sourceLufs) ? left : i;
            merge (at); changed = true; break;
        }
    }
    changed = true;
    while (changed && sectionCount > 1)
    {
        changed = false;
        for (std::size_t i = 0; i + 1u < sectionCount; ++i)
            if (std::fabs (sections[i].sourceLufs - sections[i + 1u].sourceLufs) <= rules.changeLu)
            { merge (i); changed = true; break; }
    }
    std::uint64_t comparedSections = 0;
    for (std::size_t j = 0; j < sectionCount; ++j)
    {
        auto& section = sections[j];
        double sum = 0; std::uint64_t used = 0;
        const auto first = section.fromFrame / sourceHop, last = std::min<std::uint64_t> (count, section.toFrame / sourceHop);
        for (auto i = first; i < last; ++i)
            if (std::isfinite (source[std::size_t (i)]) && source[std::size_t (i)] >= gate
                && std::isfinite (master[std::size_t (i)]))
            { sum += master[std::size_t (i)] - source[std::size_t (i)]; ++used; }
        section.compared = used != 0;
        if (used) { section.shiftLu = sum / double (used) - gain; section.masterLufs = section.sourceLufs + sum / double (used); ++comparedSections; }
    }
    if (double (comparedSections) / double (sectionCount) < rules.minComparedShare || out.compared == 0)
    { out.reason = MeasurementReason::NoSignal; return out; }
    const auto lessBits = [] (double a, double b) noexcept
    { return std::bit_cast<std::uint64_t> (a) < std::bit_cast<std::uint64_t> (b); };
    const auto shapeEnd = scratch.begin() + std::ptrdiff_t (out.compared);
    std::make_heap (scratch.begin(), shapeEnd, lessBits);
    std::sort_heap (scratch.begin(), shapeEnd, lessBits);
    const auto rank = std::max<std::uint64_t> (1u, std::uint64_t (std::ceil (rules.quantile * double (out.compared)))) - 1u;
    out.value = scratch[std::size_t (std::min (rank, out.compared - 1u))];
    out.reason = MeasurementReason::None;
    return out;
}
MasterCostValue CostMath::pumping (const LandingTrace& trace, const CostRules& rules) noexcept
{
    MasterCostValue out;
    if (! trace.complete || ! trace.valid || trace.sampleRateHz == 0 || trace.rows.empty()
        || trace.toFrame <= trace.fromFrame)
    { out.reason = MeasurementReason::Unsupported; return out; }
    const double seconds = double (trace.toFrame - trace.fromFrame) / double (trace.sampleRateHz);
    const double rate = double (trace.rows.size()) / seconds;
    if (rate < rules.pumpMinRateHz || rules.pumpLowPassHz >= .5 * rate)
    { out.reason = MeasurementReason::Unsupported; return out; }
    auto hp = butterworth (rules.pumpHighPassHz, rate, true);
    auto lp = butterworth (rules.pumpLowPassHz, rate, false);
    double sum = 0;
    for (std::size_t i = 0; i < trace.rows.size(); ++i)
    {
        if (! std::isfinite (trace.rows[i].meanDb) || trace.rows[i].nonFinite != 0)
        { out.reason = MeasurementReason::NonFinite; return out; }
        const double y = lp.process (hp.process (trace.rows[i].meanDb));
        if (double (i + 1u) / rate > rules.pumpSettleSeconds)
        { sum += y * y; ++out.compared; }
    }
    if (out.compared == 0) { out.reason = MeasurementReason::TooShort; return out; }
    out.value = Math::sqrt (sum / double (out.compared));
    out.reason = MeasurementReason::None;
    return out;
}
MasterCostValue CostMath::pumping (const mastering::GainReductionTrace& trace,
                                   std::uint32_t sampleRate, const CostRules& rules) noexcept
{
    MasterCostValue out;
    if (! trace.complete || ! trace.valid || sampleRate == 0 || trace.bucket.empty()
        || trace.programmeFrames == 0)
    { out.reason = MeasurementReason::Unsupported; return out; }
    const double rate = double (trace.bucket.size()) * double (sampleRate) / double (trace.programmeFrames);
    if (rate < rules.pumpMinRateHz || rules.pumpLowPassHz >= .5 * rate)
    { out.reason = MeasurementReason::Unsupported; return out; }
    auto hp = butterworth (rules.pumpHighPassHz, rate, true);
    auto lp = butterworth (rules.pumpLowPassHz, rate, false);
    double sum = 0;
    for (std::size_t i = 0; i < trace.bucket.size(); ++i)
    {
        if (! std::isfinite (trace.bucket[i].meanDb) || trace.bucket[i].nonFinite != 0)
        { out.reason = MeasurementReason::NonFinite; return out; }
        const double y = lp.process (hp.process (trace.bucket[i].meanDb));
        if (double (i + 1u) / rate > rules.pumpSettleSeconds)
        { sum += y * y; ++out.compared; }
    }
    if (out.compared == 0) { out.reason = MeasurementReason::TooShort; return out; }
    out.value = Math::sqrt (sum / double (out.compared));
    out.reason = MeasurementReason::None;
    return out;
}

void CrestScan::start (const analysis::BandCrestResult& source, const MasterCrest& master,
                       unsigned selected, std::span<double> scratch) noexcept
{
    *this = {};
    band = selected;
    if (band >= 5 || master.status != MeasurementStatus::Ready || ! master.complete
        || source.reason != analysis::BandCrestInvalid::None
        || source.blockCount() != master.blocks || master.rows.size() != master.blocks * 10u
        || master.sourceMask.size() != master.blocks * 5u || scratch.size() < master.blocks)
    { answer.reason = MeasurementReason::Unsupported; return; }
    phase = Phase::Read;
}
bool CrestScan::step (const analysis::BandCrestResult& source, const MasterCrest& master,
                      std::span<double> scratch, std::uint32_t budget) noexcept
{
    for (std::uint32_t work = 0; work < std::min (budget, 1024u) && phase != Phase::Done; ++work)
    {
        if (phase == Phase::Read)
        {
            if (cursor == master.blocks)
            {
                if (answer.compared == 0) { answer.reason = MeasurementReason::NoSignal; phase = Phase::Done; }
                else
                {
                    tailWeight = .05 * double (answer.compared);
                    tailCount = std::uint64_t (std::ceil (tailWeight));
                    phase = Phase::Tail;
                }
                continue;
            }
            const auto i = std::size_t (cursor++);
            if (master.sourceMask[i * 5u + band] <= 0) continue;
            const double sp = source.blockPeakLin (i, band), se = source.blockMeanSq (i, band);
            const double mp = master.rows[i * 10u + band * 2u];
            const double me = master.rows[i * 10u + band * 2u + 1u];
            if (! std::isfinite (sp) || ! std::isfinite (se) || ! std::isfinite (mp)
                || ! std::isfinite (me) || sp <= 0 || se <= 0 || mp <= 0 || me <= 0)
            { answer.reason = MeasurementReason::NonFinite; phase = Phase::Done; continue; }
            const double loss = 20.0 * Math::log10 (sp / mp) - 10.0 * Math::log10 (se / me);
            if (! std::isfinite (loss))
            { answer.reason = MeasurementReason::NonFinite; phase = Phase::Done; continue; }
            scratch[std::size_t (answer.compared++)] = std::max (0.0, loss);
            std::push_heap (scratch.begin(), scratch.begin() + std::ptrdiff_t (answer.compared), LessDouble {});
            continue;
        }
        const auto remaining = answer.compared - popped;
        std::pop_heap (scratch.begin(), scratch.begin() + std::ptrdiff_t (remaining), LessDouble {});
        const double value = scratch[std::size_t (remaining - 1u)];
        const auto whole = std::uint64_t (std::floor (tailWeight));
        sum += (popped < whole ? 1.0 : tailWeight - double (whole)) * value;
        if (++popped == tailCount)
        {
            answer.value = sum / tailWeight;
            answer.reason = MeasurementReason::None;
            phase = Phase::Done;
        }
    }
    return phase == Phase::Done;
}

void ShapeScan::start (std::span<const double> source, std::span<const double> master,
                       std::uint32_t sourceRateHz, std::uint64_t sourceHopFrames,
                       std::uint32_t masterRateHz, std::uint64_t masterHopFrames,
                       double sourceIntegrated, double masterIntegrated, const CostRules& requested,
                       std::span<MasterSection> sections, std::span<double> scratch) noexcept
{
    *this = {};
    rules = requested;
    sourceRate = sourceRateHz; sourceHop = sourceHopFrames;
    if (sourceRate == 0 || masterRateHz == 0 || sourceHop == 0 || masterHopFrames == 0
        || source.empty() || master.empty() || sections.size() < source.size()
        || scratch.size() < source.size() || ! std::isfinite (sourceIntegrated)
        || ! std::isfinite (masterIntegrated))
    { answer.reason = MeasurementReason::Unsupported; return; }
    const double sourceSeconds = double (source.size() * sourceHop) / double (sourceRate);
    const double masterSeconds = double (master.size() * masterHopFrames) / double (masterRateHz);
    if (sourceSeconds - masterSeconds > rules.maxShortfallSeconds + 1e-9)
    { answer.reason = MeasurementReason::TooShort; return; }
    if (sourceIntegrated < rules.tooQuietLufs || masterIntegrated < rules.tooQuietLufs)
    { answer.reason = MeasurementReason::NoSignal; return; }
    sourceStep = double (sourceHop) / double (sourceRate);
    const double masterStep = double (masterHopFrames) / double (masterRateHz);
    if (std::fabs (sourceStep - .1) > .0001 || std::fabs (masterStep - .1) > .0001)
    { answer.reason = MeasurementReason::Unsupported; return; }
    gate = std::max (rules.activityFloorLufs, sourceIntegrated - rules.activityBelowLu);
    gain = masterIntegrated - sourceIntegrated;
    phase = Phase::Read;
}
bool ShapeScan::step (std::span<const double> source, std::span<const double> master,
                      std::span<MasterSection> sections, std::span<double> scratch,
                      std::uint32_t budget) noexcept
{
    const auto count = std::min (source.size(), master.size());
    for (std::uint32_t work = 0; work < std::min (budget, 1024u) && phase != Phase::Done; ++work)
    {
        if (phase == Phase::Read)
        {
            if (cursor == source.size())
            {
                if (sectionCount == 0) { answer.reason = MeasurementReason::TooShort; phase = Phase::Done; }
                else phase = Phase::Short;
                continue;
            }
            const auto i = std::size_t (cursor++);
            if (double (i + 1u) * sourceStep + 1e-9 < 3.0) continue;
            const double s = source[i];
            if (! std::isfinite (s)) continue;
            const auto first = std::uint64_t (i) * sourceHop;
            const auto last = first + sourceHop;
            if (sectionCount == 0 || std::fabs (s - sections[std::size_t (sectionCount - 1u)].sourceLufs) > rules.changeLu)
                sections[std::size_t (sectionCount++)] = { first, last, s, 0, 0, false };
            else
            {
                auto& section = sections[std::size_t (sectionCount - 1u)];
                const auto old = (section.toFrame - section.fromFrame) / sourceHop;
                section.sourceLufs = (section.sourceLufs * double (old) + s) / double (old + 1u);
                section.toFrame = last;
            }
            if (i < count && s >= gate && std::isfinite (master[i]))
            {
                scratch[std::size_t (answer.compared++)] = std::fabs ((master[i] - s) - gain);
                std::push_heap (scratch.begin(), scratch.begin() + std::ptrdiff_t (answer.compared), LessDouble {});
            }
            continue;
        }
        if (phase == Phase::Short || phase == Phase::Near)
        {
            const bool shortPass = phase == Phase::Short;
            if (sectionCount <= 1u || (shortPass ? scan >= sectionCount : scan + 1u >= sectionCount))
            {
                phase = shortPass ? Phase::Near : Phase::Compare;
                scan = 0;
                if (! shortPass) sectionRow = sections[0].fromFrame / sourceHop;
                continue;
            }
            std::uint64_t at = scan;
            bool join = false;
            if (shortPass)
            {
                const auto& section = sections[std::size_t (scan)];
                join = double (section.toFrame - section.fromFrame) / double (sourceRate) + 1e-9 < rules.minSeconds;
                if (join)
                {
                    const auto left = scan == 0 ? 0 : scan - 1u;
                    const bool right = scan + 1u < sectionCount;
                    at = ! right ? left : scan == 0 ? 0
                        : std::fabs (section.sourceLufs - sections[std::size_t (left)].sourceLufs)
                            <= std::fabs (section.sourceLufs - sections[std::size_t (scan + 1u)].sourceLufs) ? left : scan;
                }
            }
            else join = std::fabs (sections[std::size_t (scan)].sourceLufs
                                  - sections[std::size_t (scan + 1u)].sourceLufs) <= rules.changeLu;
            if (! join) { ++scan; continue; }
            auto& a = sections[std::size_t (at)]; const auto& b = sections[std::size_t (at + 1u)];
            const double na = double (a.toFrame - a.fromFrame), nb = double (b.toFrame - b.fromFrame);
            a.sourceLufs = (a.sourceLufs * na + b.sourceLufs * nb) / (na + nb);
            a.toFrame = b.toFrame;
            shift = at + 1u; resume = phase; phase = Phase::Shift;
            continue;
        }
        if (phase == Phase::Shift)
        {
            if (shift + 1u < sectionCount)
            {
                sections[std::size_t (shift)] = sections[std::size_t (shift + 1u)];
                ++shift;
            }
            else { --sectionCount; scan = 0; phase = resume; }
            continue;
        }
        if (phase == Phase::Compare)
        {
            if (scan >= sectionCount)
            {
                if (double (comparedSections) / double (sectionCount) < rules.minComparedShare
                    || answer.compared == 0)
                { answer.reason = MeasurementReason::NoSignal; phase = Phase::Done; }
                else phase = Phase::Rank;
                continue;
            }
            auto& section = sections[std::size_t (scan)];
            const auto first = section.fromFrame / sourceHop;
            const auto last = std::min<std::uint64_t> (count, section.toFrame / sourceHop);
            if (sectionRow < first) sectionRow = first;
            if (sectionRow < last)
            {
                const auto i = std::size_t (sectionRow++);
                if (std::isfinite (source[i]) && source[i] >= gate && std::isfinite (master[i]))
                { sectionSum += master[i] - source[i]; ++sectionUsed; }
                continue;
            }
            section.compared = sectionUsed != 0;
            if (sectionUsed)
            {
                section.shiftLu = sectionSum / double (sectionUsed) - gain;
                section.masterLufs = section.sourceLufs + sectionSum / double (sectionUsed);
                ++comparedSections;
            }
            ++scan; sectionSum = 0; sectionUsed = 0;
            if (scan < sectionCount) sectionRow = sections[std::size_t (scan)].fromFrame / sourceHop;
            continue;
        }
        const auto rank = std::max<std::uint64_t> (1u,
            std::uint64_t (std::ceil (rules.quantile * double (answer.compared)))) - 1u;
        const auto target = answer.compared - 1u - std::min (rank, answer.compared - 1u);
        const auto remaining = answer.compared - popped;
        std::pop_heap (scratch.begin(), scratch.begin() + std::ptrdiff_t (remaining), LessDouble {});
        if (popped++ == target)
        {
            answer.value = scratch[std::size_t (remaining - 1u)];
            answer.reason = MeasurementReason::None;
            phase = Phase::Done;
        }
    }
    return phase == Phase::Done;
}

void PumpScan::start (const mastering::GainReductionTrace& trace, std::uint32_t sampleRate,
                      const CostRules& rules) noexcept
{
    *this = {};
    if (! trace.complete || ! trace.valid || sampleRate == 0 || trace.bucket.empty()
        || trace.programmeFrames == 0)
    { answer.reason = MeasurementReason::Unsupported; return; }
    rate = double (trace.bucket.size()) * double (sampleRate) / double (trace.programmeFrames);
    if (rate < rules.pumpMinRateHz || rules.pumpLowPassHz >= .5 * rate)
    { answer.reason = MeasurementReason::Unsupported; return; }
    hp = butterworth (rules.pumpHighPassHz, rate, true);
    lp = butterworth (rules.pumpLowPassHz, rate, false);
    settle = rules.pumpSettleSeconds;
    phase = Phase::Read;
}
bool PumpScan::step (const mastering::GainReductionTrace& trace, std::uint32_t budget) noexcept
{
    for (std::uint32_t work = 0; work < std::min (budget, 1024u) && phase != Phase::Done; ++work)
    {
        if (cursor == trace.bucket.size())
        {
            if (answer.compared == 0) answer.reason = MeasurementReason::TooShort;
            else { answer.value = Math::sqrt (sum / double (answer.compared)); answer.reason = MeasurementReason::None; }
            phase = Phase::Done;
            continue;
        }
        const auto i = std::size_t (cursor++);
        if (! std::isfinite (trace.bucket[i].meanDb) || trace.bucket[i].nonFinite != 0)
        { answer.reason = MeasurementReason::NonFinite; phase = Phase::Done; continue; }
        const double y = lp.process (hp.process (trace.bucket[i].meanDb));
        if (double (i + 1u) / rate > settle)
        { sum += y * y; ++answer.compared; }
    }
    return phase == Phase::Done;
}
} // namespace felitronics::session::detail
