// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "../../modules/session/tests/DeclaredBudget.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>
#include <felitronics/session/Snapshot.h>
#include <bit>
#include <cstdio>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;
namespace
{
std::uint64_t sourceHash (const Pcm& pcm)
{
    std::uint64_t h = 0xCBF29CE484222325ull;
    const auto byte = [&] (std::uint8_t b) { h = (h ^ b) * 0x100000001B3ull; };
    const auto u32 = [&] (std::uint32_t n) { for (unsigned i = 0; i < 4; ++i) byte (std::uint8_t (n >> (8 * i))); };
    const auto u64 = [&] (std::uint64_t n) { for (unsigned i = 0; i < 8; ++i) byte (std::uint8_t (n >> (8 * i))); };
    u32 (pcm.sampleRate); u32 (pcm.channelCount); u64 (pcm.frames);
    for (unsigned c = 0; c < pcm.channelCount; ++c)
        for (std::size_t i = 0; i < pcm.frames; ++i) u32 (std::bit_cast<std::uint32_t> (pcm.channels[c][i]));
    return h;
}
}
int main()
{
    constexpr std::uint32_t frames = 192000;
    std::vector<float> left (frames), right (frames);
    for (unsigned i = 0; i < frames; ++i)
    { left[i] = float (int (i % 128) - 64) / 256.0f; right[i] = float (int ((i + 7) % 128) - 64) / 256.0f; }
    const float* planes[] { left.data(), right.data() };
    const Pcm pcm { planes, 2, frames, 48000 };
    const Capabilities caps { 134217728, 96000, 255, 134217728 };
    auto made = Session::create (caps, config::Config::versions().all);
    ok (made.status == Status::Ok && made.session, "sidecar session created");
    auto& s = *made.session;
    MeasuredSource facts { "synthetic", sourceHash (pcm), frames, 48000, 2, 48000, 16, true, -14.0, -0.5 };
    const auto declared = s.loadMeasuredStorage (facts);
    ok (declared.rejection == Rejection::None, "sidecar admitted before work");
    Answer loaded;
    const auto loadSpent = budget::spend ([&] { loaded = s.loadMeasured (1, facts); });
    ok (loaded.rejection == Rejection::None && budget::covers (declared.bytes, loadSpent), "sidecar fact allocation declared");
    auto snap = s.snapshot();
    auto view = snap.view();
    ok (view.measurementsFromSidecar && view.sourceMissingAudio && view.sourceBytes == 0 && view.mandatoryMeasurementsReady,
        "sidecar origin and absent PCM visible with ready mandatory facts");
    ok (s.setCapacity ({ 0, 0 }) == Status::Ok, "sidecar capacity lowered");
    const auto deniedLoad = s.loadMeasuredStorage (facts);
    const auto loadRefusal = budget::spend ([&] { loaded = s.loadMeasured (7, facts); });
    ok (deniedLoad.rejection == Rejection::Memory && loaded.rejection == Rejection::Memory
        && loadRefusal.bytes == 0 && s.snapshot().view().sourceMissingAudio,
        "sidecar memory refusal preserves source before allocation");
    const auto deniedAttach = s.attachAudioStorage (pcm);
    const auto attachRefusal = budget::spend ([&] { loaded = s.attachAudio (8, pcm); });
    ok (deniedAttach.rejection == Rejection::Memory && loaded.rejection == Rejection::Memory
        && attachRefusal.bytes == 0 && s.snapshot().view().sourceMissingAudio,
        "attachment memory refusal precedes PCM scan and allocation");
    ok (s.setCapacity ({ 134217728, 134217728 }) == Status::Ok, "sidecar capacity restored");
    Answer masterAnswer;
    const auto masterDenied = budget::spend ([&] { masterAnswer = s.apply (command::Master { 2 }); });
    ok (masterAnswer.rejection == Rejection::NoAudio, "master waits for PCM");
    ok (masterDenied.bytes == 0, "missing PCM refusal allocates nothing");
    const auto before = s.source().hash;
    left[0] = 0.125f;
    Answer wrongAnswer;
    const auto wrong = budget::spend ([&] { wrongAnswer = s.attachAudio (3, pcm); });
    ok (wrongAnswer.rejection == Rejection::Contract, "wrong source refused");
    snap = s.snapshot();
    ok (wrong.bytes == 0 && s.source().hash == before && snap.view().sourceMissingAudio,
        "hash refusal preserves sidecar and allocates nothing");
    left[0] = -0.25f;
    const auto attachDemand = s.attachAudioStorage (pcm);
    ok (attachDemand.rejection == Rejection::None, "attachment admitted before PCM scan");
    Answer attached;
    const auto attachSpent = budget::spend ([&] { attached = s.attachAudio (4, pcm); });
    snap = s.snapshot();
    ok (attached.rejection == Rejection::None && attached.job != 0 && snap.view().measurementsFromSidecar,
        "attachment preserves fact origin and schedules index");
    std::uint64_t allSpent = std::uint64_t (attachSpent.bytes);
    for (unsigned i = 0; i < 10000; ++i)
    {
        Stepped step;
        const auto spent = budget::spend ([&] { step = s.step (16); });
        allSpent += std::uint64_t (spent.bytes);
        if (step.state == StepState::Done) break;
        ok (i < 9999, "sidecar waveform finishes in bounded steps");
    }
    snap = s.snapshot(); view = snap.view();
    ok (budget::covers (attachDemand.bytes, { 0, static_cast<long long> (allSpent) }), "attachment plus index allocations declared");
    ok (! view.sourceMissingAudio && view.sourceBytes == double (frames * 2u * sizeof (float))
        && view.measurements[std::size_t (Analyzer::Waveform)].status == MeasurementStatus::Ready
        && view.measurements[std::size_t (Analyzer::Stereo)].reason == MeasurementReason::NotImplemented,
        "attachment indexes only missing waveform evidence");
    ok (view.measurements[0].numbers[0].value == facts.integratedLufs
        && view.measurements[0].numbers[1].value == facts.truePeakDb, "attachment retains ready facts exactly");
    facts.integratedLufs.reset();
    const auto missing = s.loadMeasuredStorage (facts);
    ok (missing.rejection == Rejection::None, "missing mandatory sidecar value is a visible result");
    const auto missingSpent = budget::spend ([&] { loaded = s.loadMeasured (5, facts); });
    snap = s.snapshot();
    ok (budget::covers (missing.bytes, missingSpent) && loaded.rejection == Rejection::None
        && s.state() == State::Loaded && ! snap.view().mandatoryMeasurementsReady
        && s.apply (command::Master { 6 }).rejection == Rejection::NotMeasured,
        "missing LUFS never readies a master");
    std::printf ("sidecar budget %llu / %llu bytes; source %llu\n",
                 static_cast<unsigned long long> (attachDemand.bytes), static_cast<unsigned long long> (allSpent),
                 static_cast<unsigned long long> (before));
    return felitronics::test::report();
}
