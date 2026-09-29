// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/session/Landing.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>

#include <cmath>
#include <limits>
#include <string>

using namespace felitronics::session;
namespace test = felitronics::test;

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
    return test::report();
}
