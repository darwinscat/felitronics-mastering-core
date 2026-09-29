// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/session/Landing.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>
#include "DeclaredBudget.h"

#include <cmath>
#include <limits>
#include <string>

using namespace felitronics::session;
namespace test = felitronics::test;
namespace mastering = felitronics::mastering;
namespace budget = felitronics::session::testing;

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
    const std::string added = "\"landing\":null,";
    Kept oldKept; oldKept.id = 8;
    view.masters = { &oldKept, 1 };
    const CodecNeed oldNeed = Codec::encodedBytes (view);
    std::string oldJson ((std::size_t) oldNeed.bytes, '\0');
    const CodecStatus oldWritten = Codec::encode (view, { oldJson.data(), oldJson.size() });
    const std::size_t at = oldJson.find (added);
    if (at != std::string::npos) oldJson.erase (at, added.size());
    Snapshot oldDecoded;
    const CodecStatus oldRead = Codec::decode (oldJson, oldDecoded);
    test::ok (oldWritten == CodecStatus::Ok && at != std::string::npos && oldRead == CodecStatus::Ok
              && ! oldDecoded.view().masters[0].landing,
              "a baseline v1 master without the appended landing field decodes as absent");

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
    const auto copied = budget::spend ([&] { traceOwned = Snapshot::copy (view); });
    limiterRows[0].maxDb = clipRows[0].maxDb = 99.0;
    const auto& saved = *traceOwned.view().masters[0].landing;
    test::ok (declared >= 4u * sizeof (LandingTraceBucket)
              && budget::covers (declared, copied) && declared == (std::uint64_t) copied.bytes
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
    const auto decodedSpent = budget::spend ([&] { traceRead = Codec::decode (traceJson, traceDecoded); });
    test::ok (traceNeed.status == CodecStatus::Ok && traceWrite == CodecStatus::Ok
              && decodedNeed.status == CodecStatus::Ok && decodedNeed.bytes == declared
              && budget::covers (decodedNeed.bytes, decodedSpent) && traceRead == CodecStatus::Ok
              && traceDecoded.view().masters[0].landing
              && traceDecoded.view().masters[0].landing->peakClipTrace
              && traceDecoded.view().masters[0].landing->peakClipTrace->rows[0].maxDb == 2.0,
              "generated codec round trips both owned series (need " + std::to_string (int (traceNeed.status))
              + ", write " + std::to_string (int (traceWrite)) + ", read " + std::to_string (int (traceRead)) + ")");
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
    const auto optionalNeed = Codec::encodedBytes (view);
    std::string optionalJson ((std::size_t) optionalNeed.bytes, '\0');
    const auto optionalWrite = Codec::encode (view, { optionalJson.data(), optionalJson.size() });
    const std::string limiterNull = "\"limiterTrace\":null,";
    const std::string clipNull = "\"peakClipTrace\":null,";
    const auto limiterAt = optionalJson.find (limiterNull), clipAt = optionalJson.find (clipNull);
    if (limiterAt != std::string::npos) optionalJson.erase (limiterAt, limiterNull.size());
    const auto clipAtAfter = optionalJson.find (clipNull);
    if (clipAtAfter != std::string::npos) optionalJson.erase (clipAtAfter, clipNull.size());
    Snapshot optionalDecoded;
    const auto optionalRead = Codec::decode (optionalJson, optionalDecoded);
    test::ok (optionalWrite == CodecStatus::Ok && clipAt != std::string::npos && limiterAt != std::string::npos
              && optionalRead == CodecStatus::Ok && optionalDecoded.view().masters[0].landing
              && ! optionalDecoded.view().masters[0].landing->limiterTrace
              && ! optionalDecoded.view().masters[0].landing->peakClipTrace,
              "older landing JSON decodes both appended trace fields as absent");

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
    return test::report();
}
