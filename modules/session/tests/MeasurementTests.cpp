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
void allLowEndReadings()
{
    // Appended identity: the existing 120 Hz, infra-low and excursions IDs must stay put.
    constexpr auto low150 = Analyzer (13);
    static_assert (Analyzer::LowEnd150 == low150);
    const std::array ids { Analyzer::LowEnd, low150, Analyzer::InfraLow };
    const std::array frequencies { 120.0, 150.0, 30.0 };
    ok (kAnalyzers == 14, "all-target plan has a separate appended 150 Hz identity");
    if (kAnalyzers != 14) return;
    std::vector<float> pcm (4096, 0.25f); const float* channels[] { pcm.data() };
    auto created = Session::create(); auto& s = *created.session;
    const command::Load load { 1, { channels, 1, pcm.size(), 48000 }, {} };
    const auto answer = s.apply (load);
    const auto plan = detail::MeasurementPlan::storageFor (load.pcm, detail::MeasurementPlan::parametersFor (load.pcm));
    detail::MeasurementWorkspace work;
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const auto id = ids[i]; const auto index = std::size_t (id);
        const auto& demand = plan.analyzers[index];
        ok (demand.available && demand.workspace > 0 && demand.result > 0 && demand.rowValues > 0,
            "each low-end reading has preparation, retained rows and result storage");
        bool prepared = false;
        const auto spent = budget::spend ([&] { prepared = work.prepare (id, load.pcm, plan); });
        ok (prepared && budget::covers (demand.workspace + 256u * 64u, spent), "all three low-end preparations coexist within their demand");
        std::string name = "crossoverHz"; double rows[] { frequencies[i], double (i + 1) };
        MeasurementValue value { name, frequencies[i], MeasurementReason::None, 0 };
        MeasurementArray array { "bands", { 0, 1024, pcm.size(), 48000 }, 2, 1, 1, true, rows };
        MeasurementResult result; result.analyzer = id; result.status = MeasurementStatus::Ready;
        result.reason = MeasurementReason::None; result.complete = true; result.framesRead = pcm.size();
        result.key = s.snapshot().view().measurements[index].key; result.numbers = { &value, 1 }; result.arrays = { &array, 1 };
        bool retained = false;
        const auto retainedBytes = budget::spend ([&] { retained = detail::Driver::retain (s, answer.job, result); });
        ok (retained && budget::covers (demand.result, retainedBytes), "each low-end reading is independently retained within its result demand");
        rows[0] = -1; name.assign (name.size(), 'x');
    }
    const std::array instruments { work.lowEnd.get(), work.lowEnd150.get(), work.infraLow.get() };
    for (std::size_t i = 0; i < instruments.size(); ++i)
        ok (instruments[i] != nullptr && instruments[i]->crossoverHz() == frequencies[i],
            "coexisting low-end preparations install their own crossover");
    ok (instruments[0] != instruments[1] && instruments[1] != instruments[2] && instruments[0] != instruments[2],
        "all three low-end preparations have separate owners");
    const auto saved = s.snapshot();
    for (const auto id : ids) work.release (id);
    ok (work.bytes() == sizeof (detail::MeasurementWorkspace), "releasing all low-end preparations releases their declared bytes");
    (void) s.apply (command::SetTarget { 2, "lp", OnEdits::Keep });
    (void) s.apply (command::Cancel { 3, answer.job });
    const auto stopped = s.snapshot();
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const auto& result = stopped.view().measurements[std::size_t (ids[i])];
        ok (result.status == MeasurementStatus::Ready && result.numbers[0].value == frequencies[i],
            "target switch and cancellation retain all target readings");
    }
    pcm[0] = 0.5f; (void) s.apply (load); created.session.reset();
    Snapshot decoded;
    const auto json = encode (saved.view());
    ok (Codec::decode (json, decoded) == CodecStatus::Ok && encode (decoded.view()) == json,
        "all three low-end identities survive the single codec");
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const auto& result = decoded.view().measurements[std::size_t (ids[i])];
        ok (result.numbers[0].name == "crossoverHz" && result.numbers[0].value == frequencies[i]
            && result.arrays[0].values[0] == frequencies[i] && result.arrays[0].values[1] == double (i + 1),
            "owned low-end scalars and rows survive scratch release, reload and session destruction");
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
        std::uint64_t allocated = 0, streamingAllocated = 0;
        for (std::size_t i = 0; i < kAnalyzers; ++i)
        {
            bool prepared = false;
            const auto spent = budget::spend ([&] { prepared = work.prepare (Analyzer (i), pcm, plan); });
            const auto declared = plan.analyzers[i].workspace + 256u * 64u;
            allocated += std::uint64_t (spent.bytes);
            for (const auto id : detail::MeasurementPlan::streaming)
                if (Analyzer (i) == id) streamingAllocated += std::uint64_t (spent.bytes);
            ok (prepared == plan.analyzers[i].available && budget::covers (declared, spent), "native storageFor covers object construction and preparation, including allocator overhead");
            const auto again = budget::spend ([&] { (void) work.prepare (Analyzer (i), pcm, plan); });
            ok (again.bytes == 0, "a paused analyzer keeps its preparation");
            work.release (Analyzer (i));
        }
        std::printf ("measurement %u Hz: scheduled workspace %.0f + allowance %.0f >= allocated %llu; independent preparations allocated %llu; peak %.0f\n",
            rate, plan.storage.workspaceBytes, plan.storage.allocatorBytes, (unsigned long long) streamingAllocated,
            (unsigned long long) allocated, plan.storage.peakBytes);
        ok (streamingAllocated <= std::uint64_t (plan.storage.workspaceBytes + plan.storage.allocatorBytes), "scheduled preparations fit the whole measurement demand");
        const auto hash = [&] { return detail::MeasurementPlan::key (123, parameters, 456, {0,55,0}, {0,2,2}); };
        const auto original = hash();
        parameters.lowEnd.crossoverHz = 150;
        ok (hash() != original, "120 Hz and 150 Hz are distinct actual measurement inputs");
        ok (parameters.lowEnd150.crossoverHz == 150 && plan.parameters.lowEnd.crossoverHz == 120
            && parameters.infraLow.crossoverHz == 30, "all three low-end splits have independent parameters");
        parameters = plan.parameters; parameters.lowEnd150.crossoverHz += 1;
        ok (hash() != original, "150 Hz crossover participates in the measurement key");
        parameters = plan.parameters; parameters.lowEnd150.hop += 1;
        ok (hash() != original, "150 Hz geometry participates in the measurement key");
        parameters = plan.parameters; parameters.tempo.minBpm += 1;
        ok (hash() != original, "tempo parameters participate in the key");
        ok (detail::MeasurementPlan::key (124, plan.parameters, 456, {0,55,0}, {0,2,2}) != original
            && detail::MeasurementPlan::key (123, plan.parameters, 457, {0,55,0}, {0,2,2}) != original
            && detail::MeasurementPlan::key (123, plan.parameters, 456, {0,55,1}, {0,2,2}) != original,
            "PCM, config and core version independently invalidate the key");
    }
}
int main (int argc, char** argv)
{
    if (argc > 1 && std::string_view (argv[1]) == "--prices")
    {
        for (const unsigned seconds : {4u, 60u, 600u})
        {
            const Pcm pcm { nullptr, 2, 48000u * seconds, 48000 };
            const auto plan = detail::MeasurementPlan::storageFor (pcm, detail::MeasurementPlan::parametersFor (pcm));
            std::printf ("seconds=%u source=%.0f workspace=%.0f results=%.0f copy=%.0f codec=%.0f allowance=%.0f peak=%.0f\n",
                seconds, plan.storage.sourceBytes, plan.storage.workspaceBytes, plan.storage.resultBytes,
                plan.storage.copyBytes, plan.storage.codecBytes, plan.storage.allocatorBytes, plan.storage.peakBytes);
            for (unsigned i = 0; i < kAnalyzers; ++i)
                std::printf ("  analyzer=%u workspace=%llu rows=%llu result=%llu\n", i,
                    static_cast<unsigned long long> (plan.analyzers[i].workspace),
                    static_cast<unsigned long long> (plan.analyzers[i].rowValues),
                    static_cast<unsigned long long> (plan.analyzers[i].result));
        }
        return 0;
    }
    ownership(); allLowEndReadings(); nativePrices();
    return felitronics::test::report();
}
