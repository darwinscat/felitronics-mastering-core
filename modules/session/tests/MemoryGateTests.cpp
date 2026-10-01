// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <felitronics_test.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace declared = felitronics::declared;

namespace
{
struct Figures
{
    std::uint64_t source = 0, output = 0, peak = 0, largest = 0, declared = 0, allocated = 0,
                  external = 0;
};

bool measure (Session& s, const command::Load& load, Figures& f)
{
    const auto quoted = s.check (load);
    if (quoted.rejection != Rejection::None) { std::fprintf (stderr, "measure price %u\n", unsigned (quoted.rejection)); return false; }
    const auto before = std::uint64_t (s.liveBytes());
    // The counter totals a call's allocations; one load can ask for several blocks.
    // Assert the block price separately against the known full PCM allocation.
    if (quoted.largestBlockBytes < std::uint64_t (load.pcm.frames) * load.pcm.channelCount * sizeof (float)) return false;
    declared::LifecycleBudget demand { quoted.bytes, quoted.bytes };
    auto localPeak = before;
    Answer answer;
    const auto first = declared::spend ([&] { answer = s.apply (load); });
    if (answer.rejection != Rejection::None || ! demand.charge (first))
    { std::fprintf (stderr, "measure load %u %lld demand=%llu largest=%llu\n", unsigned (answer.rejection), first.bytes,
        (unsigned long long) quoted.bytes, (unsigned long long) quoted.largestBlockBytes); return false; }
    localPeak = std::max (localPeak, std::uint64_t (s.liveBytes()));
    for (unsigned i = 0; i < 200000 && (s.measurementJob() || s.needlesJob()); ++i)
    {
        const auto spent = declared::spend ([&] { (void) s.step (i % 3u == 0 ? 1u : i % 3u == 1 ? 7u : 16u); });
        if (! demand.charge (spent))
        { std::fprintf (stderr, "measure step %u allocated=%llu demand=%llu spent=%lld\n", i,
              (unsigned long long) demand.allocated, (unsigned long long) quoted.bytes, spent.bytes); return false; }
        localPeak = std::max (localPeak, std::uint64_t (s.liveBytes()));
    }
    f.allocated += demand.allocated;
    f.declared += quoted.bytes;
    f.largest = std::max (f.largest, quoted.largestBlockBytes);
    f.peak = std::max (f.peak, localPeak);
    const auto complete = s.measurementJob() == 0 && s.needlesJob() == 0
        && s.state() == State::Measured2
        && localPeak <= before + quoted.bytes;
    if (! complete) std::fprintf (stderr, "measure incomplete job=%llu needles=%llu ready=%u peak=%llu bound=%llu\n",
        (unsigned long long) s.measurementJob(), (unsigned long long) s.needlesJob(),
        s.state() == State::Measured2 ? 1u : 0u,
        (unsigned long long) localPeak, (unsigned long long) (before + quoted.bytes));
    return complete;
}

bool master (Session& s, command::Master request, Figures& f,
             std::vector<MasterAudio>& external, bool keepExternally)
{
    request.source = s.source().hash;
    request.revision = s.revision();
    const auto quoted = s.check (request);
    if (quoted.rejection != Rejection::None) { std::fprintf (stderr, "master preflight %u\n", unsigned (quoted.rejection)); return false; }
    const auto before = std::uint64_t (s.liveBytes());
    const auto outputFrames = std::uint64_t (s.source().frames) * request.ready.deliveryRateHz / s.source().sampleRate;
    const auto inputBytes = std::uint64_t (s.source().frames) * s.source().channels * sizeof (float);
    const auto outputBytes = outputFrames * s.source().channels * sizeof (float);
    // The source remains resident. The search owns only one full output PCM, even with SRC.
    if (quoted.bytes < outputBytes || quoted.largestBlockBytes < outputBytes
        || before + quoted.bytes < inputBytes + outputBytes) { std::fprintf (stderr, "master pcm price\n"); return false; }
    declared::LifecycleBudget demand { quoted.bytes, quoted.bytes };
    auto localPeak = before;
    Answer answer;
    if (! demand.charge (declared::spend ([&] { answer = s.apply (request); }))
        || answer.rejection != Rejection::None) { std::fprintf (stderr, "master start %u\n", unsigned (answer.rejection)); return false; }
    for (unsigned i = 0; i < 400000 && s.job(); ++i)
    {
        const auto spent = declared::spend ([&] { (void) s.step (i % 3u == 0 ? 1u : i % 3u == 1 ? 7u : 16u); });
        if (! demand.charge (spent)) { std::fprintf (stderr, "master step budget %u %lld of %llu\n", i, spent.bytes, (unsigned long long) quoted.bytes); return false; }
        localPeak = std::max (localPeak, std::uint64_t (s.liveBytes()));
    }
    f.peak = std::max (f.peak, localPeak);
    if (s.job() || ! s.pendingMaster().master || localPeak > before + quoted.bytes) { std::fprintf (stderr, "master end %llu %llu %llu\n", (unsigned long long) s.job(), (unsigned long long) s.pendingMaster().master, (unsigned long long) localPeak); return false; }
    const auto token = s.pendingMaster();
    const auto shape = s.masterAudioShape (token);
    if (shape.frames == 0 || shape.sampleRate != request.ready.deliveryRateHz) { std::fprintf (stderr, "master shape\n"); return false; }
    f.source = inputBytes;
    f.output = s.masterAudioBytes (token);
    f.allocated += demand.allocated;
    f.declared += quoted.bytes;
    f.largest = std::max (f.largest, quoted.largestBlockBytes);

    const auto snapshotPrice = s.snapshotBytes();
    Snapshot detached;
    const auto snapshotSpent = declared::spend ([&] { detached = s.snapshot(); });
    if (! declared::covers (snapshotPrice, snapshotSpent)) { std::fprintf (stderr, "master snapshot\n"); return false; }
    const auto wire = Wire::snapshotBytes (detached.view());
    if (wire.status != CodecStatus::Ok) { std::fprintf (stderr, "master wire size\n"); return false; }
    std::unique_ptr<char[]> json;
    std::unique_ptr<double[]> rows;
    const auto transferSpent = declared::spend ([&]
    {
        json.reset (new char[wire.jsonBytes]);
        rows.reset (new double[wire.rowBytes / sizeof (double)]);
    });
    if (! declared::covers (std::uint64_t (wire.jsonBytes) + wire.rowBytes, transferSpent)
        || Wire::snapshot (detached.view(), { json.get(), wire.jsonBytes },
            { rows.get(), wire.rowBytes / sizeof (double) }) != CodecStatus::Ok) { std::fprintf (stderr, "master wire copy\n"); return false; }
    f.allocated += std::uint64_t (snapshotSpent.bytes + transferSpent.bytes);
    f.declared += snapshotPrice + std::uint64_t (wire.jsonBytes) + wire.rowBytes;
    const auto copyPeak = std::uint64_t (s.liveBytes()) + std::uint64_t (snapshotSpent.bytes + transferSpent.bytes);
    f.peak = std::max (f.peak, copyPeak);
    if (copyPeak > before + quoted.bytes + snapshotPrice + std::uint64_t (wire.jsonBytes) + wire.rowBytes)
        return false;

    const auto plan = s.masterWavPlan (token);
    if (! plan || plan.frames != shape.frames) { std::fprintf (stderr, "master wav plan\n"); return false; }
    std::uint8_t wav[4096] {};
    bool wavCopied = true;
    const auto wavSpent = declared::spend ([&]
    {
        for (std::uint64_t at = 0; at < plan.bytes; at += sizeof (wav))
            if (s.copyMasterWav (token, at, { wav, std::size_t (std::min<std::uint64_t> (sizeof (wav), plan.bytes - at)) })
                != MasterTransferStatus::Ok) { wavCopied = false; return; }
    });
    if (! wavCopied || wavSpent.requests != 0) { std::fprintf (stderr, "master wav copy\n"); return false; }

    if (keepExternally)
    {
        MasterAudio taken;
        MasterTransferStatus moved = MasterTransferStatus::Unknown;
        const auto movedSpent = declared::spend ([&] { moved = s.takeMaster (token, taken); });
        if (moved != MasterTransferStatus::Ok || movedSpent.requests != 0 || ! taken.samples
            || taken.frames != shape.frames) { std::fprintf (stderr, "master transfer\n"); return false; }
        const auto first = taken.samples[0];
        if (s.pendingMaster().master || taken.samples[0] != first) return false;
        external.push_back (std::move (taken));
        f.external += f.output;
    }
    else
    {
        MasterTransferStatus released = MasterTransferStatus::Unknown;
        const auto releaseSpent = declared::spend ([&] { released = s.releaseMaster (token); });
        if (released != MasterTransferStatus::Ok || releaseSpent.requests != 0 || s.pendingMaster().master)
            return false;
    }
    return true;
}

bool cancelled (Session& s, command::Master request, Figures& f)
{
    request.source = s.source().hash;
    request.revision = s.revision();
    const auto quoted = s.check (request);
    if (quoted.rejection != Rejection::None) return false;
    const auto before = std::uint64_t (s.liveBytes());
    auto localPeak = before;
    declared::LifecycleBudget demand { quoted.bytes, quoted.bytes };
    Answer answer;
    if (! demand.charge (declared::spend ([&] { answer = s.apply (request); }))
        || answer.rejection != Rejection::None) return false;
    for (unsigned i = 0; i < 21 && s.job(); ++i)
    {
        if (! demand.charge (declared::spend ([&] { (void) s.step (1); }))) return false;
        localPeak = std::max (localPeak, std::uint64_t (s.liveBytes()));
    }
    Answer stop;
    if (! demand.charge (declared::spend ([&]
        { stop = s.apply (command::Cancel { request.id + 1u, answer.job }); }))
        || stop.rejection != Rejection::None || s.job() || s.pendingMaster().master
        || localPeak > before + quoted.bytes) return false;
    f.peak = std::max (f.peak, localPeak);
    f.allocated += demand.allocated;
    f.declared += quoted.bytes;
    f.largest = std::max (f.largest, quoted.largestBlockBytes);
    return true;
}

bool run (unsigned sourceRate, unsigned deliveryRate, unsigned channels, unsigned seconds,
          Figures& f, bool lifecycle = false)
{
    const auto frames = std::size_t (sourceRate) * seconds;
    std::vector<float> left (frames), right (channels == 2 ? frames : 0u);
    for (std::size_t i = 0; i < frames; ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 1024.0f;
        if (channels == 2) right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 1024.0f;
    }
    const float* planes[] { left.data(), channels == 2 ? right.data() : nullptr };
    Created made;
    const auto created = declared::spend ([&] { made = Session::create(); });
    if (made.status != Status::Ok || ! declared::covers (Session::createBytes(), created)) return false;
    f.declared += Session::createBytes();
    f.allocated += std::uint64_t (created.bytes);
    auto& s = *made.session;
    const command::Load load { 1, { planes, channels, frames, sourceRate }, { "gate.wav", sourceRate, true, 24 } };
    if (! measure (s, load, f)) { std::fprintf (stderr, "measure failed %u %u\n", sourceRate, channels); return false; }
    Answer reused;
    const auto reusePrice = s.check (load);
    const auto reuseSpent = declared::spend ([&] { reused = s.apply (load); });
    if (reused.rejection != Rejection::None || ! declared::covers (reusePrice.bytes, reuseSpent)
        || std::uint64_t (reuseSpent.bytes) >= std::uint64_t (frames) * channels * sizeof (float))
    { std::fprintf (stderr, "reuse failed %u %lld\n", unsigned (reused.rejection), reuseSpent.requests); return false; }
    f.declared += reusePrice.bytes;
    f.allocated += std::uint64_t (reuseSpent.bytes);
    // The delivery rate is the target's: youtube delivers 48 kHz and cdDynamic 44.1 kHz; allStreaming keeps the
    // source's rate. The request restates it.
    const auto targetFor = [sourceRate] (unsigned rate) -> const char*
    { return rate == sourceRate ? "allStreaming" : rate == 48000 ? "youtube" : rate == 44100 ? "cdDynamic" : nullptr; };
    const auto select = [&] (unsigned rate, CommandId id)
    { return targetFor (rate) && s.apply (command::SetTarget { id, targetFor (rate) }).rejection == Rejection::None; };
    if (! select (deliveryRate, 9)) { std::fprintf (stderr, "no target delivers %u from %u\n", deliveryRate, sourceRate); return false; }
    command::Master request { 2 };
    request.ready.version = 1;
    request.ready.deliveryRateHz = deliveryRate;
    request.ready.topology.eq = false;
    request.ready.topology.compressor = false;
    request.ready.topology.dither = false;
    std::vector<MasterAudio> external;
    if (! master (s, request, f, external, true)) { std::fprintf (stderr, "first master failed %u %u\n", sourceRate, channels); return false; }
    const auto sourceLive = std::uint64_t (s.liveBytes());
    auto repeat = request; repeat.id = 3;
    if (! master (s, repeat, f, external, lifecycle)) { std::fprintf (stderr, "repeat master failed %u %u\n", sourceRate, channels); return false; }
    if (s.liveBytes() < double (sourceLive)) return false;
    const auto old = s.masters().front().id;
    if (s.apply (command::Forget { 11, old }).rejection != Rejection::None) return false;
    if (lifecycle)
    {
        if (external.size() != 2 || ! external[0].samples || ! external[1].samples
            || external[0].samples.get() == external[1].samples.get()) return false;
        auto otherMade = Session::create();
        if (otherMade.status != Status::Ok || ! otherMade.session) return false;
        Figures otherFigures;
        if (! measure (*otherMade.session, load, otherFigures)
            || otherMade.session->source().hash != s.source().hash
            || otherMade.session->liveBytes() <= double (Session::createBytes())) return false;
        if (otherMade.session->apply (command::SetTarget { 19, targetFor (deliveryRate) }).rejection != Rejection::None)
            return false;
        auto otherRequest = request; otherRequest.id = 20;
        if (! cancelled (*otherMade.session, otherRequest, otherFigures)
            || s.masters().empty() || external[0].samples[0] != external[1].samples[0]) return false;
        // Large: the run's rate-raising target. Small: the source's own rate.
        auto large = request; large.id = 12;
        if (! cancelled (s, large, f) || ! select (sourceRate, 13)) return false;
        auto small = request; small.id = 14; small.ready.deliveryRateHz = sourceRate;
        small.source = s.source().hash; small.revision = s.revision();
        const auto price = s.check (small);
        if (price.rejection != Rejection::None || price.bytes == 0 || price.largestBlockBytes == 0) return false;
        Answer rejected;
        const auto before = std::uint64_t (s.liveBytes());
        if (s.setCapacity ({ double (before + price.bytes - 1u), double (price.largestBlockBytes) }) != Status::Ok) return false;
        const auto heapSpent = declared::spend ([&] { rejected = s.apply (small); });
        if (rejected.rejection != Rejection::Memory || heapSpent.requests != 0) return false;
        if (s.setCapacity ({ double (before + price.bytes), double (price.largestBlockBytes - 1u) }) != Status::Ok) return false;
        const auto blockSpent = declared::spend ([&] { rejected = s.apply (small); });
        if (rejected.rejection != Rejection::Memory || blockSpent.requests != 0) return false;
        if (s.setCapacity ({ double (before + price.bytes), double (price.largestBlockBytes) }) != Status::Ok
            || ! master (s, small, f, external, false)) return false;
        if (s.setCapacity ({}) != Status::Ok || ! select (deliveryRate, 18)) return false;
        large.id = 15;
        if (! master (s, large, f, external, false)) return false;
        left[0] += 0.25f;
        const command::Load next { 16, { planes, channels, frames, sourceRate },
            { "replacement.wav", sourceRate, true, 24 } };
        if (! measure (s, next, f) || s.masters().size() != 0 || s.pendingMaster().master != 0
            || ! external[0].samples || ! external[1].samples || ! select (deliveryRate, 19)) return false;
        const auto warmLive = std::uint64_t (s.liveBytes());
        for (unsigned cycle = 0; cycle < 3; ++cycle)
        {
            auto warm = request; warm.id = 30u + cycle * 2u;
            if (! master (s, warm, f, external, true)) return false;
            const auto held = s.masters().back().id;
            if (s.apply (command::Forget { warm.id + 1u, held }).rejection != Rejection::None
                || s.liveBytes() > double (warmLive + 4096u)) return false;
        }
        if (external.size() != 5 || ! external[0].samples || ! external[1].samples) return false;
    }
    // A bad replacement is refused without destroying the transferable source or allocating.
    float invalid = std::numeric_limits<float>::quiet_NaN();
    const float* invalidPlanes[] { &invalid, &invalid };
    Answer refused;
    const auto rejectionSpent = declared::spend ([&]
    {
        refused = s.apply (command::Load { 17, { invalidPlanes, channels, 1, sourceRate }, {} });
    });
    if (refused.rejection != Rejection::NotFinite || rejectionSpent.requests != 0) return false;
    std::printf ("memory-gate rate=%u->%u channels=%u seconds=%u source=%llu output=%llu peak=%llu largest=%llu declared=%llu allocated=%llu external=%llu\n",
        sourceRate, deliveryRate, channels, seconds,
        static_cast<unsigned long long> (f.source), static_cast<unsigned long long> (f.output),
        static_cast<unsigned long long> (f.peak), static_cast<unsigned long long> (f.largest),
        static_cast<unsigned long long> (f.declared), static_cast<unsigned long long> (f.allocated),
        static_cast<unsigned long long> (f.external));
    return true;
}
}

int main (int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::strcmp (argv[1], "--long") != 0)) return 2;
    Figures a, b, c, d, e, f, g;
    ok (run (48000, 48000, 1, 2, a), "mono full lifecycle stays inside declared memory");
    ok (run (44100, 48000, 2, 2, b), "44.1 to 48 kHz full lifecycle stays inside declared memory");
    ok (run (22050, 48000, 2, 2, c, true), "22.05 to 48 kHz reuse, refusal, cancellation and replacement stay declared");
    ok (run (48000, 44100, 2, 2, e), "48 to 44.1 kHz full lifecycle stays inside declared memory");
    ok (run (16000, 48000, 2, 2, f), "16 to 48 kHz full lifecycle stays inside declared memory");
    ok (run (96000, 44100, 1, 1, g), "96 to 44.1 kHz (source above output) full lifecycle stays inside declared memory");
    if (argc == 2) ok (run (96000, 44100, 2, 60, d), "long 96 to 44.1 kHz full lifecycle stays inside declared memory");
    return felitronics::test::report();
}
