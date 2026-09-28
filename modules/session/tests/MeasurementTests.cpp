// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "DeclaredBudget.h"
#include "MeasurementPlan.h"
#include "MeasurementWorkspace.h"
#include "Driver.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Wire.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace felitronics::session;
namespace budget = felitronics::session::testing;
using felitronics::test::ok;

std::string encode (const SnapshotView& view)
{
    const auto n = Codec::encodedBytes (view);
    std::string text (std::size_t (n.bytes), '\0');
    ok (n.status == CodecStatus::Ok && Codec::encode (view, text) == CodecStatus::Ok, "owned snapshot encodes");
    return text;
}
void ownership()
{
    auto created = Session::create(); auto& s = *created.session;
    std::vector<float> pcm (4096, 0.25f);
    const float* channels[] { pcm.data() };
    command::Load load { 1, { channels, 1, pcm.size(), 48000 }, { "first.wav", 48000, true, 24 } };
    const auto demand = s.check (load);
    auto longNameLoad = load;
    const std::string longName (8192, '\t');
    longNameLoad.meta.name = longName;
    ok (s.check (longNameLoad).bytes == demand.bytes + 8u * (longName.size() - load.meta.name.size()),
        "load includes the resident name, its snapshot copy and JSON escaping");
    Answer answer;
    auto spent = budget::spend ([&] { answer = s.apply (load); });
    ok (answer.rejection == Rejection::None && budget::covers (demand.bytes, spent), "load declares the whole measurement before copying PCM");
    const auto original = s.snapshot();
    auto values = std::make_unique<double[]> (3);
    values[0] = 0.25; values[1] = 0.5; values[2] = 0.75;
    std::string name = "peaks";
    MeasurementArray array { name, { 0, 1024, 4096, 48000 }, 1, 4, 3, false, { values.get(), 3 } };
    MeasurementValue number { "peak", 0.75, MeasurementReason::None, 0 };
    MeasurementResult result;
    result.analyzer = Analyzer::Waveform; result.status = MeasurementStatus::Ready; result.reason = MeasurementReason::None;
    result.key = original.view().measurements[std::size_t (Analyzer::Waveform)].key;
    result.framesRead = 4096; result.total = 4; result.stored = 3; result.numbers = { &number, 1 }; result.arrays = { &array, 1 };
    result.framesRead = 4097;
    ok (! detail::Driver::retain (s, answer.job, result), "analyzer completion cannot read past its source");
    result.framesRead = 4096;
    bool retained = false;
    const auto price = OwnedMeasurements::storageFor ({ &result, 1 });
    spent = budget::spend ([&] { retained = detail::Driver::retain (s, answer.job, result); });
    ok (retained && budget::covers (price, spent) && price == std::uint64_t (spent.bytes), "retaining an analyzer result requests exactly its own data");
    const auto event = s.events().back();
    Snapshot saved;
    const auto copyPrice = s.snapshotBytes();
    spent = budget::spend ([&] { saved = s.snapshot(); });
    ok (copyPrice == std::uint64_t (spent.bytes), "snapshot copy includes all nested measurement arrays and text");
    name.assign (name.size(), 'x'); values.reset();
    (void) s.step (2);
    ok (s.apply (command::Cancel { 2, answer.job }).rejection == Rejection::None, "measurement cancellation accepted");
    const auto stopped = s.snapshot();
    ok (s.source().frames == 4096 && stopped.view().measurements[7].status == MeasurementStatus::Ready
        && stopped.view().measurements[0].status == MeasurementStatus::Cancelled, "cancel retains PCM and completed results, marks unfinished results");
    const auto resumed = detail::Driver::continueMeasurement (s);
    ok (resumed != 0 && resumed != answer.job && s.snapshot().view().measurementProgress.completedUnits == 2,
        "continuation keeps the saved position and gets a fresh identity");
    ok (! detail::Driver::retain (s, answer.job, result), "stale analyzer completion cannot overwrite a continued source");
    const auto beforeReuse = s.liveBytes();
    load.id = 3;
    spent = budget::spend ([&] { answer = s.apply (load); });
    ok (answer.rejection == Rejection::None && spent.bytes == 9 && s.liveBytes() == beforeReuse,
        "identical input reuses PCM and results, allocating only the new source name");
    ok (s.snapshot().view().measurements[7].arrays[0].values[2] == 0.75, "cached result survived its analyzer scratch");
    const auto key = s.snapshot().view().measurements[7].key;
    (void) s.apply (command::SetTarget { 4, "cd", OnEdits::Keep });
    ok (s.snapshot().view().measurements[7].key == key, "target is outside the measurement cache key");
    const auto revision = s.revision(); const auto job = s.measurementJob();
    const auto whole = encode (s.snapshot().view());
    (void) s.setCapacity ({ s.liveBytes(), 0 });
    load.id = 5; pcm[0] = 0.5f;
    spent = budget::spend ([&] { answer = s.apply (load); });
    ok (answer.rejection == Rejection::Memory && spent.bytes == 0 && s.revision() == revision
        && s.measurementJob() == job && encode (s.snapshot().view()) == whole, "small budget preserves the complete source, revision, job and results");
    (void) s.setCapacity ({});
    (void) s.apply (load);
    ok (s.snapshot().view().measurements[7].key != key && s.snapshot().view().measurements[7].arrays.empty(), "changed PCM invalidates the cached source results");
    auto independent = Session::create();
    ok (independent.session->source().channels == 0, "a second session shares no measurement state");
    created.session.reset();
    ok (saved.view().measurements[7].arrays[0].name == "peaks" && saved.view().measurements[7].arrays[0].values[2] == 0.75
        && event.payload.measurement.key == key && event.payload.measurement.stored == 3, "retained snapshot and event survive scratch release, reload and session destruction");
    const auto json = encode (saved.view());
    auto invalidStorage = saved.view(); invalidStorage.measurementStorage.resultBytes = -1;
    ok (Codec::encodedBytes (invalidStorage).status == CodecStatus::Invalid, "negative measurement byte demand cannot be encoded");
    Snapshot decoded;
    const auto decodePrice = Codec::decodedBytes (json);
    CodecStatus status {};
    spent = budget::spend ([&] { status = Codec::decode (json, decoded); });
    ok (status == CodecStatus::Ok && budget::covers (decodePrice.bytes, spent) && encode (decoded.view()) == json,
        "nested result codec round-trips with a declared allocation budget");
    auto broken = json;
    const auto at = broken.find ("\"stored\":\"3\"");
    if (at != std::string::npos) broken.replace (at, 12, "\"stored\":\"9\"");
    spent = budget::spend ([&] { status = Codec::decode (broken, decoded); });
    ok (status == CodecStatus::Invalid && spent.bytes == 0, "inconsistent result counts refuse before allocation");
    for (unsigned reason = 0; reason <= unsigned (MeasurementReason::NotImplemented); ++reason)
    {
        const auto fact = MeasurementText::fact (MeasurementReason (reason));
        ok (! text::Text::key (fact).empty(), "every measurement reason has a catalog fact");
    }
}
void nativePrices()
{
    for (const std::uint32_t rate : { 8000u, 48000u, 192000u })
    {
        const Pcm pcm { nullptr, 2, rate, rate };
        auto parameters = detail::MeasurementPlan::parametersFor (pcm);
        const auto plan = detail::MeasurementPlan::storageFor (pcm, parameters);
        ok (plan.rejection == Rejection::None && plan.storage.workspaceBytes > 0 && plan.storage.codecBytes > 0,
            "all analyzer costs and the codec are declared at the actual source rate");
        detail::MeasurementWorkspace work;
        std::uint64_t allocated = 0;
        for (std::size_t i = 0; i < kAnalyzers; ++i)
        {
            bool prepared = false;
            const auto spent = budget::spend ([&] { prepared = work.prepare (Analyzer (i), pcm, plan); });
            const auto declared = plan.analyzers[i].workspace + 256u * 64u;
            allocated += std::uint64_t (spent.bytes);
            ok (prepared == plan.analyzers[i].available && budget::covers (declared, spent), "native storageFor covers object construction and preparation, including allocator overhead");
            const auto again = budget::spend ([&] { (void) work.prepare (Analyzer (i), pcm, plan); });
            ok (again.bytes == 0, "a paused analyzer keeps its preparation");
            work.release (Analyzer (i));
        }
        std::printf ("measurement %u Hz: workspace declared %.0f + allowance %.0f, allocated %llu; peak %.0f\n",
            rate, plan.storage.workspaceBytes, plan.storage.allocatorBytes, (unsigned long long) allocated, plan.storage.peakBytes);
        ok (allocated <= std::uint64_t (plan.storage.workspaceBytes + plan.storage.allocatorBytes), "whole analyzer preparation budget covers every allocation");
        const auto hash = [&] { return detail::MeasurementPlan::key (123, parameters, 456, {0,55,0}, {0,2,2}); };
        const auto original = hash();
        parameters.lowEnd.crossoverHz = 150;
        ok (hash() != original, "120 Hz and 150 Hz are distinct actual measurement inputs");
        parameters = plan.parameters; parameters.tempo.minBpm += 1;
        ok (hash() != original, "tempo parameters participate in the key");
        ok (detail::MeasurementPlan::key (124, plan.parameters, 456, {0,55,0}, {0,2,2}) != original
            && detail::MeasurementPlan::key (123, plan.parameters, 457, {0,55,0}, {0,2,2}) != original
            && detail::MeasurementPlan::key (123, plan.parameters, 456, {0,55,1}, {0,2,2}) != original,
            "PCM, config and core version independently invalidate the key");
    }
}
int main()
{
    ownership(); nativePrices();
    return felitronics::test::report();
}
