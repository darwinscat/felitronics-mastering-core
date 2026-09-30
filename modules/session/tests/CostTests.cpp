// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "Cost.h"
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace felitronics;
using felitronics::test::ok;
namespace
{
struct CrestFixture
{
    std::vector<double> sourceRows, masterRows, mask, scratch;
    analysis::BandCrestResult source;
    session::MasterCrest master;
    explicit CrestFixture (std::size_t count)
        : sourceRows (count * 10u), masterRows (count * 10u), mask (count * 5u, 1.0), scratch (count)
    {
        source.reason = analysis::BandCrestInvalid::None;
        source.blocks = sourceRows;
        source.mask = mask;
        master.status = session::MeasurementStatus::Ready;
        master.reason = session::MeasurementReason::None;
        master.complete = true; master.blocks = count; master.rows = masterRows; master.sourceMask = mask;
        for (std::size_t i = 0; i < count; ++i)
            for (std::size_t band = 0; band < 5; ++band)
            {
                sourceRows[i * 10u + band * 2u] = 1.0;
                sourceRows[i * 10u + band * 2u + 1u] = .01;
                masterRows[i * 10u + band * 2u] = 1.0;
                masterRows[i * 10u + band * 2u + 1u] = .01;
            }
    }
    void loss (std::size_t row, double db)
    {
        masterRows[row * 10u] = core::DetMath::pow10 (-db / 20.0);
    }
    session::MasterCostValue measure() noexcept
    { return session::detail::CostMath::crest (source, master, 0, scratch); }
};
std::uint64_t fixtureHash (std::span<const double> values) noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (double value : values)
    {
        const auto bits = std::bit_cast<std::uint64_t> (value);
        for (unsigned b = 0; b < 8; ++b)
            hash = (hash ^ std::uint8_t (bits >> (8u * b))) * 0x100000001B3ull;
    }
    return hash;
}
}
int main()
{
    CrestFixture gain (25);
    for (std::size_t i = 0; i < 25; ++i)
    {
        gain.masterRows[i * 10u] = 2.0;
        gain.masterRows[i * 10u + 1u] = .04;
    }
    const auto zero = gain.measure();
    ok (zero.value && std::fabs (*zero.value) < 1e-11 && zero.compared == 25,
        "gain-only crest has zero loss");
    CrestFixture fractional (25);
    fractional.loss (0, 10.0); fractional.loss (1, 6.0);
    const auto tail = fractional.measure();
    ok (tail.value && std::fabs (*tail.value - 9.2) < 1e-9 && tail.compared == 25,
        "the 1.25-window worst tail weights the second window by one quarter");
    session::detail::CrestScan crestCursor;
    crestCursor.start (fractional.source, fractional.master, 0, fractional.scratch);
    for (unsigned n = 0; n < 1000 && crestCursor.phase != session::detail::CrestScan::Phase::Done; ++n)
        (void) crestCursor.step (fractional.source, fractional.master, fractional.scratch, 1);
    ok (crestCursor.phase == session::detail::CrestScan::Phase::Done
        && crestCursor.answer.value && tail.value
        && std::bit_cast<std::uint64_t> (*crestCursor.answer.value) == std::bit_cast<std::uint64_t> (*tail.value),
        "one-unit crest cursor matches the whole-call fractional CVaR bit for bit");
    fractional.loss (1, 10.0);
    const auto tied = fractional.measure();
    ok (tied.value && std::fabs (*tied.value - 10.0) < 1e-9,
        "equal losses have the same weighted answer independent of index");
    CrestFixture increasing (25);
    increasing.loss (0, 1.0);
    const auto quietPrice = increasing.measure();
    increasing.loss (0, 2.0);
    const auto middlePrice = increasing.measure();
    increasing.loss (0, 3.0);
    const auto loudPrice = increasing.measure();
    ok (quietPrice.value && middlePrice.value && loudPrice.value
        && *quietPrice.value < *middlePrice.value && *middlePrice.value < *loudPrice.value,
        "successively flatter synthetic attacks raise the measured price smoothly");
    fractional.mask.assign (fractional.mask.size(), 0.0);
    fractional.source.mask = fractional.mask; fractional.master.sourceMask = fractional.mask;
    const auto empty = fractional.measure();
    ok (! empty.value && empty.reason == session::MeasurementReason::NoSignal,
        "an empty source mask is unmeasured");
    CrestFixture shortOne (1);
    shortOne.loss (0, 4.0);
    const auto single = shortOne.measure();
    ok (single.value && std::fabs (*single.value - 4.0) < 1e-9,
        "a fractional tail smaller than one takes the sole suitable window");
    shortOne.master.blocks = 2;
    ok (shortOne.measure().reason == session::MeasurementReason::Unsupported,
        "incompatible crest grids have a reason");

    std::vector<double> source (330), master (330), scratch (330);
    std::vector<session::MasterSection> sections (330);
    for (std::size_t i = 0; i < source.size(); ++i)
    { source[i] = i < 110 ? -20 : i < 220 ? -16 : -12; master[i] = source[i] + 6; }
    std::uint64_t count = 0;
    const session::detail::CostRules rules;
    const auto shape = session::detail::CostMath::shape (source, master, 48000, 4800, 48000, 4800,
        -16, -10, rules, sections, scratch, count);
    ok (shape.value && *shape.value == 0 && count == 3 && shape.compared > 0,
        "gain-only shape removes integrated gain and keeps three 11-second sections");
    for (std::size_t i = 110; i < 220; ++i) master[i] += 2;
    const auto shifted = session::detail::CostMath::shape (source, master, 48000, 4800, 48000, 4800,
        -16, -10, rules, sections, scratch, count);
    ok (shifted.value && *shifted.value >= 2 && count == 3 && sections[1].shiftLu > sections[0].shiftLu,
        "a section change is located and raises the continuous P95 shape");
    std::vector<double> reordered (source.size());
    for (std::size_t i = 0; i < reordered.size(); ++i)
        reordered[i] = source[i < 110 ? i + 220 : i < 220 ? i : i - 220] + 6;
    const auto moved = session::detail::CostMath::shape (source, reordered, 48000, 4800,
        48000, 4800, -16, -10, rules, sections, scratch, count);
    ok (moved.value && std::fabs (*moved.value - 8.0) < 1e-12 && count == 3,
        "reordering the quiet and loud sections gives the expected eight-LU P95 deviation");
    std::vector<session::MasterSection> cursorSections (source.size());
    std::vector<double> cursorScratch (source.size());
    session::detail::ShapeScan shapeCursor;
    shapeCursor.start (source, reordered, 48000, 4800, 48000, 4800, -16, -10,
        rules, cursorSections, cursorScratch);
    for (unsigned n = 0; n < 100000 && shapeCursor.phase != session::detail::ShapeScan::Phase::Done; ++n)
        (void) shapeCursor.step (source, reordered, cursorSections, cursorScratch, 1);
    bool sameSections = shapeCursor.sectionCount == count;
    if (sameSections) for (std::size_t i = 0; i < count; ++i)
        sameSections = sameSections && cursorSections[i].fromFrame == sections[i].fromFrame
            && cursorSections[i].toFrame == sections[i].toFrame
            && std::bit_cast<std::uint64_t> (cursorSections[i].sourceLufs)
                == std::bit_cast<std::uint64_t> (sections[i].sourceLufs)
            && std::bit_cast<std::uint64_t> (cursorSections[i].masterLufs)
                == std::bit_cast<std::uint64_t> (sections[i].masterLufs)
            && std::bit_cast<std::uint64_t> (cursorSections[i].shiftLu)
                == std::bit_cast<std::uint64_t> (sections[i].shiftLu);
    ok (shapeCursor.phase == session::detail::ShapeScan::Phase::Done
        && shapeCursor.answer.value && moved.value
        && std::bit_cast<std::uint64_t> (*shapeCursor.answer.value) == std::bit_cast<std::uint64_t> (*moved.value)
        && sameSections,
        "one-unit shape cursor matches whole-call sections and P95 bits");
    std::vector<double> rapidSource (350), rapidMaster (350);
    constexpr double rapidLevels[] { -25, -20, -15, -20, -25 };
    for (std::size_t i = 0; i < rapidSource.size(); ++i)
    { rapidSource[i] = rapidLevels[i / 70u]; rapidMaster[i] = rapidSource[i] + 6.0 + double (i % 3u) * .25; }
    std::vector<session::MasterSection> rapidSections (350), rapidCursorSections (350);
    std::vector<double> rapidScratch (350), rapidCursorScratch (350);
    std::uint64_t rapidCount = 0;
    const auto rapidWhole = session::detail::CostMath::shape (rapidSource, rapidMaster,
        48000, 4800, 48000, 4800, -20, -14, rules,
        rapidSections, rapidScratch, rapidCount);
    session::detail::ShapeScan rapidCursor;
    rapidCursor.start (rapidSource, rapidMaster, 48000, 4800, 48000, 4800,
        -20, -14, rules, rapidCursorSections, rapidCursorScratch);
    for (unsigned n = 0; n < 200000 && rapidCursor.phase != session::detail::ShapeScan::Phase::Done; ++n)
        (void) rapidCursor.step (rapidSource, rapidMaster, rapidCursorSections, rapidCursorScratch,
            1u + (n % 17u));
    bool rapidEqual = rapidCursor.sectionCount == rapidCount;
    if (rapidEqual) for (std::size_t i = 0; i < rapidCount; ++i)
        rapidEqual = rapidEqual && rapidCursorSections[i].fromFrame == rapidSections[i].fromFrame
            && rapidCursorSections[i].toFrame == rapidSections[i].toFrame
            && std::bit_cast<std::uint64_t> (rapidCursorSections[i].sourceLufs)
                == std::bit_cast<std::uint64_t> (rapidSections[i].sourceLufs)
            && std::bit_cast<std::uint64_t> (rapidCursorSections[i].shiftLu)
                == std::bit_cast<std::uint64_t> (rapidSections[i].shiftLu);
    ok (rapidWhole.value && rapidCursor.answer.value && rapidCount < 5 && rapidEqual
        && std::bit_cast<std::uint64_t> (*rapidWhole.value)
            == std::bit_cast<std::uint64_t> (*rapidCursor.answer.value),
        "irregular cursor cuts reproduce chained short-section merges and shape bits");
    std::vector<double> edgeSource (189, -20.0), edgeMaster (189, -14.0);
    for (std::size_t i = 109; i < 189; ++i)
    { edgeSource[i] = -16.0; edgeMaster[i] = -10.0; }
    const auto exactEight = session::detail::CostMath::shape (edgeSource, edgeMaster, 48000, 4800,
        48000, 4800, -18, -12, rules, sections, scratch, count);
    ok (exactEight.value && count == 2 && sections[0].toFrame - sections[0].fromFrame == 80u * 4800u,
        "an exactly eight-second first section remains distinct");
    edgeSource[108] = -16.0; edgeMaster[108] = -10.0;
    const auto underEight = session::detail::CostMath::shape (edgeSource, edgeMaster, 48000, 4800,
        48000, 4800, -18, -12, rules, sections, scratch, count);
    ok (underEight.value && count == 1, "a section shorter than eight seconds joins its neighbour");
    std::fill (edgeSource.begin(), edgeSource.end(), -20.0);
    std::fill (edgeMaster.begin(), edgeMaster.end(), -14.0);
    for (std::size_t i = 109; i < 189; ++i)
    { edgeSource[i] = -17.0; edgeMaster[i] = -11.0; }
    const auto exactThree = session::detail::CostMath::shape (edgeSource, edgeMaster, 48000, 4800,
        48000, 4800, -18, -12, rules, sections, scratch, count);
    ok (exactThree.value && count == 1, "a reading exactly three LU away does not split a section");
    for (std::size_t i = 109; i < 189; ++i)
    { edgeSource[i] = -16.99; edgeMaster[i] = -10.99; }
    const auto overThree = session::detail::CostMath::shape (edgeSource, edgeMaster, 48000, 4800,
        48000, 4800, -18, -12, rules, sections, scratch, count);
    ok (overThree.value && count == 2, "a reading just beyond three LU starts a section");
    const auto shorter = session::detail::CostMath::shape (source, std::span<const double> (master).first (325),
        48000, 4800, 48000, 4800, -16, -10, rules, sections, scratch, count);
    ok (! shorter.value && shorter.reason == session::MeasurementReason::TooShort,
        "more than 0.1 second of missing master is unmeasured");
    const auto oneHopShorter = session::detail::CostMath::shape (source,
        std::span<const double> (reordered).first (329), 48000, 4800, 48000, 4800,
        -16, -10, rules, sections, scratch, count);
    ok (oneHopShorter.value && count == 3,
        "one missing 0.1-second output hop remains comparable with source-sized section storage");

    session::LandingTrace noTrace;
    ok (session::detail::CostMath::pumping (noTrace, rules).reason == session::MeasurementReason::Unsupported,
        "absent GR cannot invent pumping");
    std::vector<session::LandingTraceBucket> steady (1000);
    for (auto& row : steady) { row.meanDb = row.minDb = row.maxDb = 2.0; row.samples = 1; }
    session::LandingTrace trace;
    trace.fromFrame = 0; trace.toFrame = 192000; trace.sampleRateHz = 48000;
    trace.complete = trace.valid = true; trace.rows = steady;
    const auto steadyPump = session::detail::CostMath::pumping (trace, rules);
    ok (steadyPump.value && *steadyPump.value < .01,
        "a constant limiter reduction has no 1-8 Hz pumping after settling");
    mastering::GainReductionTrace gainTrace;
    gainTrace.bucket.resize (1000);
    for (auto& row : gainTrace.bucket) { row.meanDb = 2.0; row.samples = 1; }
    gainTrace.programmeFrames = 192000; gainTrace.complete = gainTrace.valid = true;
    const auto wholePump = session::detail::CostMath::pumping (gainTrace, 48000, rules);
    session::detail::PumpScan pumpCursor;
    pumpCursor.start (gainTrace, 48000, rules);
    for (unsigned n = 0; n < 2000 && pumpCursor.phase != session::detail::PumpScan::Phase::Done; ++n)
        (void) pumpCursor.step (gainTrace, 1);
    ok (pumpCursor.phase == session::detail::PumpScan::Phase::Done
        && pumpCursor.answer.value && wholePump.value
        && std::bit_cast<std::uint64_t> (*pumpCursor.answer.value)
            == std::bit_cast<std::uint64_t> (*wholePump.value),
        "one-unit pumping cursor matches the whole trace bit for bit");
    std::printf ("cost-fixture-fnv64=%016llx\n", (unsigned long long) fixtureHash (source));
    return felitronics::test::report();
}
