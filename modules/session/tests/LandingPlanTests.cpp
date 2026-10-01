// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/session/Landing.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>
#include "../../../tests/DeclaredBudget.h"

#include <cmath>
#include <bit>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics::session;
namespace test = felitronics::test;
namespace mastering = felitronics::mastering;
namespace declared = felitronics::declared;

// The trace constructor/add/finish path from 42fa0e8, kept independent of the
// cursor implementation so their common arithmetic cannot hide a regression.
struct PreviousTraceBuilder
{
    mastering::GainReductionTrace& t;
    std::uint64_t frames, k = 0, start = 0, end = 0;
    PreviousTraceBuilder (mastering::GainReductionTrace& trace, int programmeFrames, int requested)
        : t (trace), frames (programmeFrames > 0 ? std::uint64_t (programmeFrames) : 0u)
    {
        t.buckets = mastering::GainReductionTrace::bucketsFor (
            std::clamp (requested, 1, mastering::GainReductionTrace::kMaxBuckets), programmeFrames);
        t.bucket.assign (std::size_t (t.buckets), mastering::GainReductionTraceBucket {});
        t.valid = false; t.complete = false; t.programmeFrames = frames;
        t.samples = 0; t.nonFinite = 0;
        end = t.buckets > 0 ? bucketEnd (0) : 0u;
    }
    std::uint64_t bucketEnd (std::uint64_t i) const noexcept
    { return (i + 1u) * frames / std::uint64_t (t.buckets); }
    void add (std::uint64_t frame, double a) noexcept
    {
        if (t.buckets <= 0 || frame >= frames) return;
        if (frame < start) { k = 0; start = 0; end = bucketEnd (0); }
        while (frame >= end && k + 1u < std::uint64_t (t.buckets))
        { ++k; start = end; end = bucketEnd (k); }
        auto& b = t.bucket[std::size_t (k)];
        ++b.samples;
        if (! std::isfinite (a)) { ++b.nonFinite; return; }
        if (b.samples - b.nonFinite == 1 || a < b.minDb) b.minDb = a;
        if (a > b.maxDb) b.maxDb = a;
        b.meanDb += a;
    }
    void finish() noexcept
    {
        std::uint64_t samples = 0, nonFinite = 0;
        for (int i = 0; i < t.buckets; ++i)
        {
            auto& b = t.bucket[std::size_t (i)];
            const auto finite = b.samples - b.nonFinite;
            b.meanDb = finite > 0 ? b.meanDb / double (finite) : 0.0;
            samples += b.samples; nonFinite += b.nonFinite;
        }
        t.samples = samples; t.nonFinite = nonFinite;
        t.valid = samples > 0 && nonFinite == 0; t.complete = true;
    }
};

int main()
{
    test::group ("the Session landing plan captures the source trim and one render budget");
    const config::Loaded loaded = config::Config::load();
    if (! test::run (loaded.ok())) return test::report();
    const LandingPlan ready = LandingOps::plan (loaded.config.engine, true, -20.0, -14.0, -1.0, 48000.0, 48000.0);
    test::ok (ready.status == LandingPlanStatus::Ready && ready.request.productLanding
              && ready.request.maxPasses == 12 && ready.request.normalizationGainDb == 2.0
              && ready.request.initialGainDb == 4.0 && ready.initialCeilingDbTp == -1.15,
              "-20 LUFS input is raised two decibels before the chain; search starts four decibels after it");
    test::ok (ready.request.truePeakAimDb == 0.05 && ready.request.toleranceLu == 0.1,
              "the promised ceiling, aim, and loudness tolerance stay separate");
    const LandingPlan silent = LandingOps::plan (loaded.config.engine, false, -120.0, -14.0, -1.0, 48000.0, 48000.0);
    test::ok (silent.status == LandingPlanStatus::UnavailableSource,
              "unmeasured source loudness yields unavailable data, not an invented gain");
    const LandingPlan extreme = LandingOps::plan (loaded.config.engine, true, -18.0,
        std::numeric_limits<double>::max(), -1.0, 48000.0, 48000.0);
    test::ok (extreme.status == LandingPlanStatus::Ready && std::isfinite (extreme.request.initialGainDb),
              "a finite extreme target remains finite and reaches the bounded search");
    const LandingPlan converted = LandingOps::plan (loaded.config.engine, true, -20.0, -14.0, -1.0, 44100.0, 48000.0);
    test::ok (! ready.sourceRateImpactPass && converted.sourceRateImpactPass && converted.request.maxPasses == 12,
              "a rate change schedules an unconditional source-rate impact pass outside the twelve-render landing budget");

    test::group ("a kept landing result owns its pass log and the codec accepts old v1 masters");
    LandingPass rows[2] {};
    rows[0].gainDb = 3.0; rows[0].ceilingDbTp = -1.15; rows[0].achievedLufs = -15.0;
    rows[0].truePeakDbTp = -1.2; rows[0].ceilingSafe = true;
    rows[1].gainDb = 4.0; rows[1].ceilingDbTp = -1.16; rows[1].achievedLufs = -14.5;
    rows[1].truePeakDbTp = -1.1; rows[1].ceilingSafe = true;
    LandingSummary landing;
    landing.status = LandingStatus::PassLimit; landing.deliverable = true;
    landing.achievedLufs = -14.5; landing.missLu = -0.5; landing.distanceLu = 0.5;
    landing.truePeakDbTp = -1.1; landing.passes = 2; landing.workUnits = 123456;
    landing.log = { rows, 2 };
    Kept kept; kept.id = 7; kept.landing = landing;
    SnapshotView view; view.masters = { &kept, 1 };
    const Snapshot owned = Snapshot::copy (view);
    rows[0].gainDb = 99.0;
    test::ok (owned.view().masters.size() == 1 && owned.view().masters[0].landing
              && owned.view().masters[0].landing->log[0].gainDb == 3.0,
              "the owned snapshot copies pass rows, not their span pointer");
    const CodecNeed need = Codec::encodedBytes (owned.view());
    std::string json ((std::size_t) need.bytes, '\0');
    const CodecStatus written = Codec::encode (owned.view(), { json.data(), json.size() });
    Snapshot decoded;
    const CodecStatus read = Codec::decode (json, decoded);
    test::ok (need.status == CodecStatus::Ok && written == CodecStatus::Ok && read == CodecStatus::Ok
              && decoded.view().masters[0].landing && decoded.view().masters[0].landing->passes == 2
              && decoded.view().masters[0].landing->log[1].achievedLufs == -14.5,
              "the generated codec carries status, achieved loudness, work, and the exact pass rows");

    test::group ("K13 and limiter rows are owned and share an explicit delivered grid");
    LandingTraceBucket limiterRows[2] { { 0.0, 3.0, 1.5, 4, 0 }, { 0.0, 1.0, 0.25, 4, 0 } };
    LandingTraceBucket clipRows[2] { { 0.0, 2.0, 0.5, 4, 0 }, { 0.0, 0.0, 0.0, 4, 0 } };
    LandingTrace limiterTrace;
    limiterTrace.toFrame = 2; limiterTrace.sampleRateHz = 96000; limiterTrace.columns = 2;
    limiterTrace.complete = limiterTrace.valid = true; limiterTrace.samples = 8;
    limiterTrace.rows = limiterRows;
    LandingTrace clipTrace = limiterTrace; clipTrace.rows = clipRows;
    landing.limiterTrace = limiterTrace; landing.peakClipTrace = clipTrace;
    kept.landing = landing; view.masters = { &kept, 1 };
    const auto declared = Snapshot::storageFor (view);
    Snapshot traceOwned;
    const auto copied = declared::spend ([&] { traceOwned = Snapshot::copy (view); });
    limiterRows[0].maxDb = clipRows[0].maxDb = 99.0;
    const auto& saved = *traceOwned.view().masters[0].landing;
    test::ok (declared >= 4u * sizeof (LandingTraceBucket)
              && declared::covers (declared, copied) && declared == (std::uint64_t) copied.bytes
              && saved.limiterTrace && saved.peakClipTrace
              && saved.limiterTrace->rows[0].maxDb == 3.0
              && saved.peakClipTrace->rows[0].maxDb == 2.0
              && saved.limiterTrace->sampleRateHz == saved.peakClipTrace->sampleRateHz
              && saved.limiterTrace->columns == saved.peakClipTrace->columns,
              "snapshot copies both rows and declares their storage");
    const auto traceNeed = Codec::encodedBytes (traceOwned.view());
    std::string traceJson ((std::size_t) traceNeed.bytes, '\0');
    const auto traceWrite = Codec::encode (traceOwned.view(), { traceJson.data(), traceJson.size() });
    Snapshot traceDecoded;
    const auto decodedNeed = Codec::decodedBytes (traceJson);
    CodecStatus traceRead {};
    const auto decodedSpent = declared::spend ([&] { traceRead = Codec::decode (traceJson, traceDecoded); });
    test::ok (traceNeed.status == CodecStatus::Ok && traceWrite == CodecStatus::Ok
              && decodedNeed.status == CodecStatus::Ok && decodedNeed.bytes == declared
              && declared::covers (decodedNeed.bytes, decodedSpent) && traceRead == CodecStatus::Ok
              && traceDecoded.view().masters[0].landing
              && traceDecoded.view().masters[0].landing->peakClipTrace
              && traceDecoded.view().masters[0].landing->peakClipTrace->rows[0].maxDb == 2.0,
              "generated codec round trips both owned series (need " + std::to_string (int (traceNeed.status))
              + ", write " + std::to_string (int (traceWrite)) + ", read " + std::to_string (int (traceRead)) + ")");
    std::string inconsistentTrace = traceJson;
    const auto traceAt = inconsistentTrace.find ("\"limiterTrace\":{");
    const auto totalAt = traceAt == std::string::npos ? std::string::npos
        : inconsistentTrace.find ("\"samples\":\"8\"", traceAt);
    if (totalAt != std::string::npos) inconsistentTrace.replace (totalAt, sizeof ("\"samples\":\"8\"") - 1u,
                                                               "\"samples\":\"9\"");
    Snapshot refusedTrace;
    const auto refusedNeed = Codec::decodedBytes (inconsistentTrace);
    CodecStatus refusedRead {};
    const auto refusedSpent = declared::spend ([&] { refusedRead = Codec::decode (inconsistentTrace, refusedTrace); });
    test::ok (totalAt != std::string::npos && refusedNeed.status == CodecStatus::Invalid
              && refusedRead == CodecStatus::Invalid && refusedSpent.bytes == 0
              && refusedTrace.view().masters.empty(),
              "inconsistent completed trace totals refuse before allocation or publication");
    double reduced[7] {};
    const auto reducedCount = LandingOps::query (*saved.limiterTrace, 1, reduced);
    test::ok (reducedCount == 1 && reduced[0] == 0.0 && reduced[1] == 2.0
              && reduced[2] == 0.0 && reduced[3] == 3.0 && reduced[4] == 0.875
              && reduced[5] == 8.0 && reduced[6] == 0.0,
              "one requested column combines both retained buckets with weighted mean");
    LandingTrace incomplete = *saved.peakClipTrace; incomplete.complete = false;
    test::ok (LandingOps::query (incomplete, 1, reduced) == 0
              && LandingOps::query (*saved.limiterTrace, 0, reduced) == 0,
              "unfinished and zero-column requests refuse before row writes");
    landing.limiterTrace.reset(); landing.peakClipTrace.reset(); kept.landing = landing;

    test::group ("the landing carries what held it and the levels it fell between, across the wire");
    {
        LandingSummary carried = landing; carried.status = LandingStatus::TargetBetweenAchievable;
        carried.belowLufs = -14.46; carried.aboveLufs = -13.52; kept.landing = carried;
        const auto need = Codec::encodedBytes (view);
        std::string json ((std::size_t) need.bytes, '\0');
        Snapshot back;
        const bool round = Codec::encode (view, { json.data(), json.size() }) == CodecStatus::Ok
            && Codec::decode (json, back) == CodecStatus::Ok && back.view().masters.size() == 1 && back.view().masters[0].landing
            && back.view().masters[0].landing->belowLufs == -14.46 && back.view().masters[0].landing->aboveLufs == -13.52
            && back.view().masters[0].landing->binding == LandingConstraint::None;
        // A named limit on a landing that is not unreachable, or levels out of order, is no landing.
        carried.binding = LandingConstraint::GainRange; kept.landing = carried;
        std::string named ((std::size_t) Codec::encodedBytes (view).bytes, '\0');
        Snapshot refusedNamed;
        const bool namedRefused = Codec::encode (view, { named.data(), named.size() }) == CodecStatus::Ok
            && Codec::decode (named, refusedNamed) == CodecStatus::Invalid;
        carried.binding = LandingConstraint::None; carried.belowLufs = -13.0; kept.landing = carried;
        std::string disordered ((std::size_t) Codec::encodedBytes (view).bytes, '\0');
        Snapshot refusedOrder;
        const bool orderRefused = Codec::encode (view, { disordered.data(), disordered.size() }) == CodecStatus::Ok
            && Codec::decode (disordered, refusedOrder) == CodecStatus::Invalid;
        test::ok (round && namedRefused && orderRefused,
                  "the two levels round-trip; a misplaced limit or levels out of order refuse");
        kept.landing = landing;

        // Where the verdict is decided: the solver's binding and its bracket, carried by summarize — nothing invented.
        mastering::LoudnessSolution unreachable;
        unreachable.status = mastering::MasteringSolveStatus::TargetUnreachable;
        unreachable.binding = mastering::MasteringConstraint::GainRange;
        LandingPass none[1] {};
        LandingSummary held;
        const bool heldOk = LandingOps::summarize (unreachable, none, held);
        mastering::LoudnessSolution bracket;
        bracket.status = mastering::MasteringSolveStatus::TargetBetweenAchievable;
        bracket.binding = mastering::MasteringConstraint::TruePeakCeiling;
        bracket.achievedBelowLufs = -14.46; bracket.achievedAboveLufs = -13.52;
        LandingSummary between;
        const bool betweenOk = LandingOps::summarize (bracket, none, between);
        unreachable.binding = mastering::MasteringConstraint::None;
        LandingSummary unnamed;
        const bool unnamedOk = LandingOps::summarize (unreachable, none, unnamed);
        test::ok (heldOk && held.binding == LandingConstraint::GainRange && ! held.belowLufs
                  && betweenOk && between.binding == LandingConstraint::None && between.belowLufs == -14.46 && between.aboveLufs == -13.52
                  && unnamedOk && unnamed.binding == LandingConstraint::None,
                  "summarize carries the solver's binding for an unreachable landing alone, and the bracket for a between one");
        // A between side that is not a number — no known path makes one (LandingSearchTests) — never stops the delivery:
        // the verdict stands without its levels and the master is returned (owner, 2026-10-01).
        bool levelless = true;
        for (const double side : { std::numeric_limits<double>::quiet_NaN(), -std::numeric_limits<double>::infinity() })
            for (const bool above : { false, true })
            {
                mastering::LoudnessSolution blind = bracket;
                (above ? blind.achievedAboveLufs : blind.achievedBelowLufs) = side;
                blind.deliverable = true; blind.achievedLufs = -13.9; blind.missLu = 0.1; blind.distanceLu = 0.1;
                blind.measured.truePeakDbTp = -1.2;
                LandingSummary summary;
                levelless = levelless && LandingOps::summarize (blind, none, summary)
                    && summary.status == LandingStatus::TargetBetweenAchievable && ! summary.belowLufs && ! summary.aboveLufs
                    && summary.deliverable && summary.achievedLufs == -13.9 && summary.truePeakDbTp == -1.2;
            }
        test::ok (levelless, "a between verdict with a side that is not a number is kept without its levels, and the master delivered");
    }

    test::group ("adapter copies a completed trace and refuses stale second results");
    mastering::LoudnessSolution solution;
    solution.status = mastering::MasteringSolveStatus::PassLimit;
    solution.passes = solution.logCount = 1;
    mastering::GainReductionTraceBuilder limBuilder (solution.limiterTrace, 2, 2);
    mastering::GainReductionTraceBuilder clipBuilder (solution.peakClipTrace, 2, 2);
    limBuilder.add (0, 1.0); limBuilder.add (1, 2.0); limBuilder.finish();
    clipBuilder.add (0, 0.5); clipBuilder.add (1, 0.0); clipBuilder.finish();
    LandingPass adaptedPass[1] {};
    LandingTraceBucket adaptedLim[2] {}, adaptedClip[2] {};
    LandingSummary adapted;
    const bool summarized = LandingOps::summarize (solution, adaptedPass, adaptedLim, adaptedClip, 48000, adapted);
    test::ok (summarized && adapted.limiterTrace && adapted.peakClipTrace
              && adapted.limiterTrace->toFrame == 2 && adapted.peakClipTrace->rows[0].maxDb == 0.5,
              "adapter preserves the common delivered frame grid and K13 rows");
    solution.logCount = 2;
    const bool refused = LandingOps::summarize (solution, adaptedPass, adaptedLim, adaptedClip, 48000, adapted);
    test::ok (! refused && adapted.peakClipTrace && adapted.peakClipTrace->rows[0].maxDb == 0.5,
              "invalid repeat does not publish partial rows or mutate the previous result");
    solution.logCount = 1; solution.status = mastering::MasteringSolveStatus::Cancelled;
    mastering::GainReductionTraceBuilder partialLim (solution.limiterTrace, 2, 1);
    mastering::GainReductionTraceBuilder partialClip (solution.peakClipTrace, 2, 1);
    partialLim.add (0, 1.0); partialLim.add (0, 3.0);
    partialClip.add (0, 0.5); partialClip.add (0, 1.5);
    LandingTraceBucket partialLimRow[1] {}, partialClipRow[1] {};
    LandingSummary partial;
    const bool partialOk = LandingOps::summarize (solution, adaptedPass, partialLimRow, partialClipRow, 48000, partial);
    test::ok (partialOk && partial.peakClipTrace && ! partial.peakClipTrace->complete
              && ! partial.peakClipTrace->valid && partial.peakClipTrace->samples == 2
              && partial.peakClipTrace->rows[0].minDb == 0.5
              && partial.peakClipTrace->rows[0].maxDb == 1.5
              && partial.peakClipTrace->rows[0].meanDb == 1.0,
              "cancelled rows name their incompleteness and retain a real partial mean");
    mastering::LoudnessSolution longSolution = solution;
    longSolution.status = mastering::MasteringSolveStatus::Solved;
    constexpr std::size_t longBuckets = 65536;
    for (auto* trace : { &longSolution.limiterTrace, &longSolution.peakClipTrace })
    {
        trace->buckets = int (longBuckets);
        trace->programmeFrames = longBuckets;
        trace->bucket.resize (longBuckets);
        trace->complete = trace->valid = true;
        trace->samples = longBuckets; trace->nonFinite = 0;
        for (std::size_t i = 0; i < longBuckets; ++i)
        {
            trace->bucket[i].samples = 1;
            trace->bucket[i].meanDb = double (i % 7u);
            trace->bucket[i].minDb = trace->bucket[i].maxDb = trace->bucket[i].meanDb;
        }
    }
    LandingPass wholePass[1] {}, slicedPass[1] {};
    std::vector<LandingTraceBucket> wholeLim (longBuckets), wholeClip (longBuckets);
    std::vector<LandingTraceBucket> slicedLim (longBuckets), slicedClip (longBuckets);
    LandingSummary whole, sliced;
    const bool wholeOk = LandingOps::summarize (longSolution, wholePass, wholeLim, wholeClip, 48000, whole);
    const bool baseOk = LandingOps::summarize (longSolution, slicedPass, sliced);
    std::uint32_t cursor = 0;
    bool bounded = true, complete = false;
    for (unsigned i = 0; i < 5000 && ! complete; ++i)
    {
        const auto before = cursor;
        complete = LandingOps::stepTraces (longSolution, slicedLim, slicedClip, 48000, sliced, cursor, 17);
        bounded = bounded && cursor > before && cursor - before <= 17;
    }
    bool equal = wholeOk && baseOk && complete && bounded && whole.limiterTrace && sliced.limiterTrace
        && whole.peakClipTrace && sliced.peakClipTrace;
    if (equal) for (std::size_t i = 0; i < longBuckets; ++i)
        equal = equal && wholeLim[i].meanDb == slicedLim[i].meanDb
            && wholeClip[i].meanDb == slicedClip[i].meanDb
            && wholeLim[i].samples == slicedLim[i].samples
            && wholeClip[i].samples == slicedClip[i].samples;
    test::ok (equal, "long trace adapter advances in bounded cuts and matches whole-call rows");
    mastering::GainReductionTrace stagedTrace;
    mastering::GainReductionTraceBuilder stagedBuilder (stagedTrace, int (longBuckets), int (longBuckets), true);
    std::size_t initialized = 0;
    bool traceBounded = true;
    while (! stagedBuilder.stepBegin (17))
    {
        const auto after = stagedBuilder.beginWorkUnits();
        traceBounded = traceBounded && after > initialized && after - initialized <= 17;
        initialized = after;
    }
    for (std::size_t i = 0; i < longBuckets; ++i) stagedBuilder.add (i, double (i % 7u));
    std::size_t finished = 0;
    while (! stagedBuilder.stepFinish (17))
    {
        const auto after = stagedBuilder.finishedBuckets();
        traceBounded = traceBounded && after > finished && after - finished <= 17;
        finished = after;
    }
    bool oldTraceEqual = traceBounded && stagedTrace.complete && stagedTrace.valid
        && stagedTrace.samples == longBuckets && stagedTrace.nonFinite == 0
        && stagedTrace.bucket.size() == longBuckets;
    if (oldTraceEqual) for (std::size_t i = 0; i < longBuckets; ++i)
        oldTraceEqual = oldTraceEqual && stagedTrace.bucket[i].samples == 1
            && std::bit_cast<std::uint64_t> (stagedTrace.bucket[i].meanDb)
                == std::bit_cast<std::uint64_t> (double (i % 7u));
    test::ok (oldTraceEqual, "trace initialization and finish stay bounded at the maximum bucket count");
    mastering::GainReductionTrace previousTrace, resumedTrace;
    PreviousTraceBuilder previous (previousTrace, int (2u * longBuckets), int (longBuckets));
    mastering::GainReductionTraceBuilder resumed (resumedTrace, int (2u * longBuckets),
        int (longBuckets), true);
    while (! resumed.stepBegin (17)) {}
    for (std::size_t i = 0; i < 2u * longBuckets; ++i)
    {
        const double value = i % 101u == 0 ? std::numeric_limits<double>::quiet_NaN()
            : double (int (i % 13u) - 6) * .125;
        previous.add (i, value); resumed.add (i, value);
    }
    previous.finish();
    while (! resumed.stepFinish (17)) {}
    bool previousEqual = previousTrace.buckets == resumedTrace.buckets
        && previousTrace.samples == resumedTrace.samples
        && previousTrace.nonFinite == resumedTrace.nonFinite
        && previousTrace.valid == resumedTrace.valid && resumedTrace.complete;
    if (previousEqual) for (std::size_t i = 0; i < longBuckets; ++i)
    {
        const auto& a = previousTrace.bucket[i]; const auto& b = resumedTrace.bucket[i];
        previousEqual = previousEqual && a.samples == b.samples && a.nonFinite == b.nonFinite
            && std::bit_cast<std::uint64_t> (a.minDb) == std::bit_cast<std::uint64_t> (b.minDb)
            && std::bit_cast<std::uint64_t> (a.maxDb) == std::bit_cast<std::uint64_t> (b.maxDb)
            && std::bit_cast<std::uint64_t> (a.meanDb) == std::bit_cast<std::uint64_t> (b.meanDb);
    }
    test::ok (previousEqual, "maximum resumed trace matches the saved previous builder bit for bit");
    return test::report();
}
