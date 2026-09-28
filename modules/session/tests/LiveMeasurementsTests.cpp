// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "DeclaredBudget.h"
#include "MeasurementPlan.h"
#include "MeasurementWorkspace.h"
#include "QueryState.h"
#include "Driver.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <felitronics/analysis/ProgrammeReport.h>
#include <felitronics/analysis/ClipDetector.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics::session;
using namespace felitronics::analysis;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;

bool same (double a, double b);

struct felitronics::session::detail::Inspector
{
    static bool waveformFinished (const Session& s) { return s.waveform_->finished; }
    static void eventJobs (Session& s)
    {
        s.measurementJob_ = 11; s.needlesJob_ = 12; s.job_ = 13;
        s.measurementProgress_ = { PhaseName::Report, 0.2, 1, 0, 0, 214, 257568 };
        s.needlesProgress_ = { PhaseName::Analyzers, 0.1, 1, 0, 0, 2, 190 };
        s.masterProgress_ = { PhaseName::Pass, 0.5, 1, 1, 2, 1, 3 };
    }
    static Notification event (Session& s, JobId job, EventKind kind, const Phase& progress)
    {
        s.eventCount_ = 0;
        Notification event; event.jobId = job; event.kind = kind;
        if (kind == EventKind::Phase) event.payload.phase = progress;
        s.emit (event);
        return s.events()[0];
    }
};

void capacityBoundary()
{
    std::vector<float> pcm (60u * 48000u, 0.25f);
    const float* channels[] { pcm.data(), pcm.data() };
    const Pcm audio { channels, 2, pcm.size(), 48000 };
    auto measured = Session::create();
    (void) measured.session->apply (command::Load { 1, audio, {} });
    while (! detail::Inspector::waveformFinished (*measured.session)) (void) measured.session->step (1);
    for (unsigned stage = 0; stage < 3; ++stage)
    {
        const auto before = measured.session->liveBytes();
        const auto spent = budget::spend ([&] { (void) measured.session->step (1); });
        ok (measured.session->liveBytes() - before >= double (spent.bytes),
            "each admitted workspace retains every raw allocation byte, including MSVC Debug overhead");
        std::printf ("unlimited preparation=%u retained=%.0f raw=%lld\n", stage, measured.session->liveBytes() - before, spent.bytes);
    }
    auto made = Session::create(); auto& s = *made.session;
    (void) s.apply (command::Load { 1, audio, {} });
    while (! detail::Inspector::waveformFinished (s)) (void) s.step (1);
    const auto plan = detail::MeasurementPlan::storageFor (audio, detail::MeasurementPlan::parametersFor (audio));
    for (unsigned stage = 0; stage < 3; ++stage)
    {
        const auto& price = plan.analyzers[stage];
        const auto raw = price.workspace + price.rowValues * sizeof (double);
        const auto before = s.liveBytes();
        (void) s.setCapacity ({ before + double (raw), before + double (raw) });
        auto spent = budget::spend ([&] { (void) s.step (1); });
        ok (spent.bytes == 0 && s.events().size() == 1 && s.events()[0].kind == EventKind::Error
            && s.events()[0].payload.error.code == ErrorCode::Memory, "preparation refuses a capacity without allocator allowance before allocating");
        (void) s.setCapacity ({});
        spent = budget::spend ([&] { (void) s.step (1); });
        ok (s.liveBytes() - before >= double (spent.bytes), "retained preparation accounting covers raw allocations, including MSVC padding");
        std::printf ("preparation=%u retained=%.0f raw=%lld\n", stage, s.liveBytes() - before, spent.bytes);
    }
}

void eventMetadata()
{
    auto made = Session::create(); auto& s = *made.session;
    detail::Inspector::eventJobs (s);
    const Phase progress[] { {}, { PhaseName::Report, 0.2, 1, 0, 0, 214, 257568 },
        { PhaseName::Analyzers, 0.1, 1, 0, 0, 2, 190 }, { PhaseName::Pass, 0.5, 1, 1, 2, 1, 3 } };
    for (unsigned job = 0; job < 4; ++job)
        for (auto kind : { EventKind::Phase, EventKind::Fact, EventKind::Reading, EventKind::Done,
                           EventKind::Rejected, EventKind::Error, EventKind::Measurement })
        {
            const auto e = detail::Inspector::event (s, job == 0 ? 0 : 10 + job, kind, progress[job]);
            ok (e.phase == progress[job].name && e.completedWork == progress[job].completedUnits
                && e.totalWork == progress[job].totalUnits, "every event kind describes its own emitting job; commands have no job work");
        }
    made = Session::create(); auto& real = *made.session;
    std::vector<float> pcm (8192, 0.25f); const float* planes[] { pcm.data(), pcm.data() };
    (void) real.apply (command::Load { 1, { planes, 2, pcm.size(), 48000 }, {} });
    const auto check = [&] (const Phase& expected)
    {
        for (const auto& event : real.events())
            ok (event.phase == expected.name && event.completedWork == expected.completedUnits
                && event.totalWork == expected.totalUnits, "real publications keep the emitting progress across completion and cancellation");
        const auto wire = Wire::eventsBytes (real.events());
        std::string json (std::size_t (wire.jsonBytes), '\0');
        std::vector<double> rows (std::size_t (wire.rowBytes / sizeof (double)));
        ok (wire.status == CodecStatus::Ok && Wire::events (real.events(), json, rows) == CodecStatus::Ok,
            "job metadata survives the production event transport");
        for (const auto& event : real.events())
            ok (json.find ("\"completedWork\":\"" + std::to_string (event.completedWork) + "\"") != std::string::npos
                && json.find ("\"totalWork\":\"" + std::to_string (event.totalWork) + "\"") != std::string::npos,
                "wire envelopes retain deterministic work");
    };
    (void) real.step (1);
    const auto paused = real.snapshot().view().measurementProgress;
    (void) real.apply (command::Cancel { 2, real.measurementJob() }); check (paused);
    (void) real.apply (command::ContinueMeasurement { 3 }); check (paused);
    while (real.measurementJob() != 0)
    {
        (void) real.step (1);
        check (real.snapshot().view().measurementProgress);
    }
    (void) real.apply (command::Master { 4 });
    while (real.job() != 0) { (void) real.step (1); check (real.snapshot().view().masterProgress); }
    (void) real.apply (command::Master { 5 }); (void) real.step (1);
    const auto master = real.snapshot().view().masterProgress;
    (void) real.apply (command::Cancel { 6, real.job() }); check (master);
    (void) real.apply (command::Cancel { 7, 999 }); check ({});

    made = Session::create(); auto& needles = *made.session;
    (void) needles.apply (command::Load { 1, { planes, 2, pcm.size(), 48000 }, {} });
    const auto snapshot = needles.snapshot();
    MeasurementValue values[] { { "integratedLufs", -18.0, MeasurementReason::None, 0 }, { "truePeakDb", 0.0, MeasurementReason::None, 0 } };
    MeasurementResult result; result.analyzer = Analyzer::Loudness; result.status = MeasurementStatus::Ready;
    result.reason = MeasurementReason::None; result.complete = true;
    result.key = snapshot.view().measurements[0].key; result.numbers = values;
    ok (detail::Driver::retain (needles, needles.measurementJob(), result), "supply the target-dependent readings");
    command::EditTarget target; target.id = 2; target.fields.lufs = -13; target.fields.tp = -1;
    (void) needles.apply (target);
    ok (detail::Driver::measured1 (needles, needles.measurementJob(), needles.source().hash) && needles.needlesJob() != 0,
        "needles run alongside the unfinished measurement");
    while (needles.needlesJob() != 0)
    {
        (void) needles.step (1);
        const auto expected = needles.snapshot().view().needlesProgress;
        for (const auto& event : needles.events())
            ok (event.phase == expected.name && event.completedWork == expected.completedUnits
                && event.totalWork == expected.totalUnits, "needles phase and completion envelopes advance with the needles payload");
    }
}

void initialization()
{
    const Pcm audio { nullptr, 2, 60u * 48000u, 48000 };
    const auto plan = detail::MeasurementPlan::storageFor (audio, detail::MeasurementPlan::parametersFor (audio));
    detail::MeasurementWorkspace work;
    ok (work.prepare (Analyzer::Loudness, audio, plan), "prepare the streaming instrument");
    std::vector<float> pcm (48000, 0.25f); const float* planes[] { pcm.data(), pcm.data() };
    ok (work.loudness->process (planes, 2, int (pcm.size())), "write gating records before reusing their storage");
    const auto records = work.loudness->gatingBlockEnergies();
    ok (! records.empty() && records[0] != 0, "the retained store contains a real observation");
    const auto first = records[0];
    ok (work.loudness->prepare (48000, 2, 60), "reprepare retains capacity");
    ok (records[0] == first && work.loudness->gatingBlockEnergies().empty(),
        "preparation resets counts without visiting whole-source storage; entries are overwritten when produced");
}

void streamingKernel()
{
    for (const unsigned rate : { 8000u, 44100u, 48000u }) for (const int channels : { 1, 2, 16 })
    {
        DeterministicLoudnessMeter reference;
        StreamingLoudnessMeter stream;
        ok (reference.prepare (rate, channels, 4) && stream.prepare (rate, channels, 4), "both loudness kernels prepare the same geometry");
        float data[16][1024]; const float* planes[16];
        for (int c = 0; c < channels; ++c)
        {
            planes[c] = data[c];
            for (unsigned i = 0; i < 1024; ++i) data[c][i] = float (0.3 * felitronics::core::det::sin (double (i + unsigned (c)) * 0.07));
            reference.setChannelWeight (c, c == 3 ? 0.0 : 1.0); stream.setChannelWeight (c, c == 3 ? 0.0 : 1.0);
        }
        for (unsigned part = 0; part < 420; ++part)
        {
            const int n = part % 7 == 0 ? 0 : part % 5 == 0 ? 37 : 1024;
            const int width = part % 17 == 0 ? 0 : part % 11 == 0 ? 1 : channels;
            data[0][0] = part == 101 ? std::numeric_limits<float>::infinity() : part == 104 ? std::numeric_limits<float>::quiet_NaN() : 0.1f;
            ok (reference.process (planes, width, n) == stream.process (planes, width, n), "kernel call acceptance agrees");
            const auto a = reference.gatingBlockEnergies(), b = stream.gatingBlockEnergies();
            bool equal = a.size() == b.size();
            for (std::size_t i = 0; equal && i < a.size(); ++i) equal = same (a[i], b[i]);
            ok (equal && same (reference.momentaryLufs(), stream.momentaryLufs())
                && same (reference.shortTermLufs(), stream.shortTermLufs())
                && same (reference.integratedLufs(), stream.integratedLufs())
                && same (reference.loudnessRangeLu(), stream.loudnessRangeLu())
                && reference.nonFiniteSubHops() == stream.nonFiniteSubHops()
                && reference.droppedBlocks() == stream.droppedBlocks()
                && reference.droppedShortTermSamples() == stream.droppedShortTermSamples(),
                "every kernel energy, reading and damage counter matches core across call boundaries");
            if (part == 200)
            {
                auto copied = stream;
                ok (same (copied.integratedLufs(), stream.integratedLufs())
                    && same (copied.loudnessRangeLu(), stream.loudnessRangeLu()), "copying a partial store preserves only its published observations");
                stream = std::move (copied);
            }
            if (part == 300) { reference.reset(); stream.reset(); }
        }
        ok (! stream.prepare (rate, 0, 4) && stream.gatingBlockEnergies().empty(), "refused preparation hides previous observations");
    }
}

bool same (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
const MeasurementValue* find (const MeasurementResult& r, std::string_view name)
{
    for (const auto& v : r.numbers) if (v.name == name) return &v;
    return nullptr;
}
std::string encoded (const SnapshotView& view)
{
    auto n = Codec::encodedBytes (view);
    std::string out (std::size_t (n.bytes), '\0');
    ok (n.status == CodecStatus::Ok && Codec::encode (view, out) == CodecStatus::Ok, "live snapshot codec accepts owned rows");
    return out;
}
void run (std::uint32_t rate, unsigned channels, unsigned frames, unsigned shape, unsigned modes = 4)
{
    std::vector<float> pcm (std::size_t (frames) * channels);
    for (unsigned c = 0; c < channels; ++c)
        for (unsigned i = 0; i < frames; ++i)
        {
            const double v = 0.9 * felitronics::core::det::sin (double (i) * 0.131 + double (c) * 0.2);
            pcm[std::size_t (c) * frames + i] = shape == 1 ? 0.0f : float (std::clamp (v, -0.4, 0.4));
        }
    if (shape == 2) pcm.back() = std::numeric_limits<float>::max();
    const float* planes[] { pcm.data(), pcm.data() + frames };
    Pcm audio { planes, channels, frames, rate };
    const auto params = detail::MeasurementPlan::parametersFor (audio);
    ProgrammeReport report; report.setParams (params.programme);
    ClipDetector clips; clips.setParams ({params.clipRuns});
    ok (report.prepare (rate, 1024, int (channels)) && clips.prepare (rate, 1024, int (channels)), "direct analyzers prepare");
    ok (report.process (planes, int (channels), int (frames)) && clips.process (planes, int (channels), int (frames)), "direct analyzers consume one block");
    report.finish(); clips.finish();
    DeterministicLoudnessMeter meter;
    ok (meter.prepare (rate, int (channels), params.programme.maxDurationSec), "direct streaming loudness prepares");
    const unsigned hop = 10u * unsigned (std::floor (0.01 * rate + 0.5));
    std::vector<double> momentary, shortTerm;
    for (unsigned at = 0; at < frames;)
    {
        const auto n = std::min (hop, frames - at);
        const float* part[] { planes[0] + at, channels == 2 ? planes[1] + at : nullptr };
        ok (meter.process (part, int (channels), int (n)), "direct meter processes complete grid intervals");
        at += n;
        if (n == hop) { momentary.push_back (meter.momentaryLufs()); shortTerm.push_back (meter.shortTermLufs()); }
    }
    std::string baseline;
    for (unsigned mode = 0; mode < modes; ++mode)
    {
        auto made = Session::create(); auto& s = *made.session;
        const auto declaration = s.measurementStorage (audio);
        Answer answer;
        auto allocated = budget::spend ([&] { answer = s.apply (command::Load {1, audio, {}}); });
        ok (answer.rejection == Rejection::None, "live PCM loaded");
        std::uint64_t bytes = std::uint64_t (allocated.bytes), points = 0, runs = 0, seq = 0;
        unsigned steps = 0; bool cancelled = false; double slowest = 0;
        while (s.measurementJob() != 0)
        {
            const auto before = std::chrono::steady_clock::now();
            allocated = budget::spend ([&] { (void) s.step (mode == 0 ? 16u : mode == 2 ? 1u + steps % 7u : 1u); });
            slowest = std::max (slowest, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - before).count());
            bytes += std::uint64_t (allocated.bytes);
            for (const auto& event : s.events())
            {
                ok (event.seq > seq && event.jobId != 0 && event.source == s.source().hash, "events carry ordered identities");
                seq = event.seq;
                if (event.kind != EventKind::Reading) continue;
                const auto& r = event.payload.reading;
                for (unsigned i = 0; i < r.momentaryCount; ++i)
                    ok (r.momentary[i].index == points++, "reading transfers each grid point once");
                runs += r.runCount;
                ok (r.grid.firstFrame == r.grid.stepFrames && r.grid.framesRead <= frames, "reading grid is fixed in source frames");
            }
            if (! cancelled && s.measurementJob() != 0
                && ((mode == 2 && steps >= 2) || (mode == 3 && s.events().size() == 1
                    && s.snapshot().view().measurementProgress.name == PhaseName::Report)))
            {
                const auto key = s.source().hash;
                ok (s.apply (command::Cancel {2, s.measurementJob()}).rejection == Rejection::None
                    && s.state() == State::MeasurementStopped && s.source().hash == key, "cancellation retains source and enters stopped state");
                const auto stopped = encoded (s.snapshot().view());
                (void) s.step (16);
                ok (stopped == encoded (s.snapshot().view()), "stopped pump neither feeds zeros nor finishes a report");
                ok (s.apply (command::ContinueMeasurement {3}).job != answer.job, "continuation issues a new job");
                cancelled = true;
            }
            if (++steps > 1000000) { ok (false, "live pump terminates"); break; }
        }
        const auto snap = s.snapshot();
        const auto& result = snap.view().measurements[2];
        ok (result.status == MeasurementStatus::Ready && result.complete, "full report is retained after finish");
        report.report().visitValues ([&] (std::string_view name, int channel, const ProgrammeValue& value)
        {
            auto key = std::string (name); if (channel >= 0) key += "[" + std::to_string (channel) + "]";
            const auto* v = find (result, key);
            ok (v && v->value.has_value() == value.valid && v->analyzerReason == unsigned (value.reason)
                && (! value.valid || same (*v->value, value.value)), "report scalar and native reason match direct analyzer");
        });
        report.report().visitCounts ([&] (std::string_view name, int channel, std::int64_t value)
        {
            auto key = std::string (name); if (channel >= 0) key += "[" + std::to_string (channel) + "]";
            const auto* v = find (result, key); ok (v && v->value && *v->value == double (value), "report count matches direct analyzer");
        });
        ok (mode < 2 || cancelled, "stream and report cancellations are exercised");
        const auto& loudness = snap.view().measurements[0];
        ok (points == frames / hop && loudness.arrays[0].stored == points, "wire volume is exactly one row per grid interval");
        for (std::size_t i = 0; i < points; ++i)
            for (unsigned series = 0; series < 2; ++series)
            {
                const auto why = MeasurementReason (unsigned (loudness.arrays[series + 2].values[i]));
                const auto v = loudness.arrays[series].values[i];
                ok (why != MeasurementReason::None || same (v, series == 0 ? momentary[i] : shortTerm[i]), "every usable loudness point equals the direct meter");
                ok (why != MeasurementReason::TooShort || std::isnan (v), "unready window is a gap with an explicit reason");
                ok (why != MeasurementReason::NoSignal || (std::isinf (v) && v < 0), "silent window is negative infinity with a reason");
            }
        const auto& damage = snap.view().measurements[1];
        ok (damage.total == std::uint64_t (clips.runCount()) && damage.stored == runs, "clip total and delta storage match direct analyzer");
        ok (damage.complete == clips.runsComplete() && damage.stored == std::uint64_t (clips.storedRunCount()), "truncated rows retain honest aggregate completeness");
        if (frames >= 1000000) ok (! damage.complete && damage.total > damage.stored, "overflowed clip list preserves exact totals and a lower bound");
        for (std::uint64_t i = 0; i < runs; ++i)
        {
            const auto r = clips.run (std::int64_t (i)); const auto row = damage.arrays[0].values.subspan (std::size_t (i * 6), 6);
            ok (row[0] == double (r.start) && row[1] == double (r.length) && row[2] == r.channel && row[3] == r.sign
                && same (row[4], r.level) && row[5] == double (r.evidence), "every clip field matches direct detector");
        }
        ok (declaration.peakBytes >= double (bytes), "declared measurement peak covers every allocation request");
        std::uint64_t digest = 0xcbf29ce484222325ull;
        const auto hash = [&] (std::uint64_t value)
        { for (unsigned i = 0; i < 8; ++i) digest = (digest ^ std::uint8_t (value >> (8 * i))) * 0x100000001b3ull; };
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto& measured = snap.view().measurements[i];
            for (const auto& value : measured.numbers)
            {
                hash (std::uint64_t (value.reason)); hash (value.analyzerReason);
                hash (value.value ? std::bit_cast<std::uint64_t> (*value.value) : 0);
            }
            for (const auto& array : measured.arrays)
                for (double value : array.values)
                    hash (std::isnan (value) ? 0x7ff8000000000000ull : std::bit_cast<std::uint64_t> (value));
        }
        std::printf ("rate=%u channels=%u frames=%u mode=%u declared=%.0f allocated=%llu maxStepMs=%.3f digest=%016llx\n", rate, channels, frames, mode,
                     declaration.peakBytes, static_cast<unsigned long long> (bytes), slowest, static_cast<unsigned long long> (digest));
        auto view = snap.view(); view.revision = 0; view.measurementProgress = {}; view.measurementJob = 0;
        const auto text = encoded (view);
        if (mode == 0) baseline = text;
        else ok (text == baseline, "chunk size and cancellation leave identical owned results");
        made.session.reset();
        ok (encoded (view) == text, "owned report survives session and workspace destruction");
    }
}
int main (int argc, char** argv)
{
    if (argc > 1 && std::string_view (argv[1]) == "--capacity") { capacityBoundary(); return felitronics::test::report(); }
    if (argc > 1 && std::string_view (argv[1]) == "--events") { eventMetadata(); return felitronics::test::report(); }
    if (argc > 1 && std::string_view (argv[1]) == "--initialization") { initialization(); return felitronics::test::report(); }
    if (argc > 1 && std::string_view (argv[1]) == "--overflow")
    {
        run (8000, 2, 1000000, 0, 1);
        return felitronics::test::report();
    }
    capacityBoundary(); eventMetadata(); initialization(); streamingKernel();
    run (8000, 1, 8000 * 4 + 7, 0);
    run (48000, 2, 48000 * 4 + 1, 0);
    run (44100, 2, 44100 * 4 + 4410, 0);
    run (48000, 1, 31, 0);
    run (48000, 1, 48000, 1);
    run (48000, 2, 48000, 2);
    return felitronics::test::report();
}
