// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "DeclaredBudget.h"
#include "MeasurementPlan.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;

int main (int argc, char** argv)
{
    const unsigned seconds = argc > 1 ? unsigned (std::strtoul (argv[1], nullptr, 10)) : 4;
    if (seconds != 4 && seconds != 60 && seconds != 600) return 2;
    const unsigned frames = seconds * 48000;
    std::vector<float> pcm (std::size_t (frames) * 2);
    float period[96];
    for (unsigned i = 0; i < 96; ++i)
        period[i] = float (std::clamp (0.9 * felitronics::core::det::sin (double (i) * 0.06544984694978735), -0.4, 0.4));
    for (unsigned i = 0; i < frames; ++i) pcm[i] = pcm[std::size_t (frames) + i] = period[i % 96];
    const float* channels[] { pcm.data(), pcm.data() + frames };
    const Pcm audio { channels, 2, frames, 48000 };
    auto made = Session::create(); auto& s = *made.session;
    const auto price = s.measurementStorage (audio);
    ok (s.setCapacity ({ price.peakBytes, price.largestBlockBytes }) == Status::Ok, "admit exactly the declared heap and largest block");
    const command::Load load { 1, audio, {} };
    const auto loadPrice = s.check (load);
    Answer loaded;
    auto spent = budget::spend ([&] { loaded = s.apply (load); });
    ok (loaded.rejection == Rejection::None && budget::covers (loadPrice.bytes, spent), "load fits its declaration");
    std::uint64_t allocated = std::uint64_t (spent.bytes);
    unsigned calls = 0;
    while (s.measurementJob() != 0 || s.needlesJob() != 0)
    {
        spent = budget::spend ([&] { (void) s.step (16); });
        allocated += std::uint64_t (spent.bytes);
        bool memory = false;
        for (const auto& event : s.events())
            memory = memory || (event.kind == EventKind::Error && event.payload.error.code == ErrorCode::Memory);
        ok (! memory, "the declared peak never refuses a preparation");
        if (memory || ++calls > frames) { ok (false, "measurement terminates within its frame work bound"); return 1; }
    }
    ok (price.peakBytes >= double (allocated), "whole declaration covers load and every execution allocation");
    Snapshot snapshot;
    spent = budget::spend ([&] { snapshot = s.snapshot(); });
    ok (budget::covers (std::uint64_t (price.copyBytes), spent), "detached snapshot fits the copy reserve");
    const auto copied = spent.bytes;
    const auto encoded = Codec::encodedBytes (snapshot.view());
    const auto wire = Wire::snapshotBytes (snapshot.view());
    ok (encoded.status == CodecStatus::Ok && wire.status == CodecStatus::Ok
        && double (std::uint64_t (wire.jsonBytes) + wire.rowBytes) <= price.codecBytes, "transferable rows and exact scalar JSON fit the serialization reserve");
    std::unique_ptr<char[]> wireJson;
    std::unique_ptr<double[]> wireRows;
    spent = budget::spend ([&]
    {
        wireJson.reset (new char[wire.jsonBytes]);
        wireRows.reset (new double[wire.rowBytes / sizeof (double)]);
    });
    ok (budget::covers (std::uint64_t (price.codecBytes), spent)
        && Wire::snapshot (snapshot.view(), { wireJson.get(), wire.jsonBytes }, { wireRows.get(), wire.rowBytes / sizeof (double) }) == CodecStatus::Ok,
        "web serialization writes within its declared buffers");
    std::string json;
    spent = budget::spend ([&] { json.resize (std::size_t (encoded.bytes)); });
    ok (budget::covers (encoded.bytes + 1u + 64u, spent)
        && Codec::encode (snapshot.view(), json) == CodecStatus::Ok, "optional plain JSON export uses its exact size query plus string allocator padding");
    ok (snapshot.view().measurements[2].complete && snapshot.view().measurements[1].total >= snapshot.view().measurements[1].stored,
        "full report and honest clipping counters survive preparation release");
    std::printf ("seconds=%u declared=%.0f allocated=%llu ratio=%.6f copy=%lld json=%llu source=%.0f workspace=%.0f result=%.0f codec=%.0f loadPeak=%.0f workPeak=%.0f\n",
        seconds, price.peakBytes, static_cast<unsigned long long> (allocated), price.peakBytes / double (allocated),
        static_cast<long long> (copied), static_cast<unsigned long long> (encoded.bytes), price.sourceBytes,
        price.workspaceBytes, price.resultBytes, price.codecBytes, price.loadPeakBytes, price.workPeakBytes);
    // Capacity refusal, same-source reuse and replacement are independent of the measurement duration.
    (void) s.setCapacity ({});
    const auto repeated = s.check (load);
    spent = budget::spend ([&] { loaded = s.apply (load); });
    ok (loaded.rejection == Rejection::None && spent.bytes == 0 && budget::covers (repeated.bytes, spent), "same source reuses every allocation");
    const auto replacement = s.measurementStorage (audio);
    ok (replacement.loadPeakBytes < s.liveBytes() + 2 * price.sourceBytes + price.allocatorBytes,
        "replacement does not count old and new PCM copies together");
    (void) s.setCapacity ({ s.liveBytes(), 0 });
    pcm[0] = -0.1f;
    spent = budget::spend ([&] { loaded = s.apply (load); });
    ok (loaded.rejection == Rejection::Memory && spent.bytes == 0, "refused replacement allocates nothing and preserves the old source");
    (void) s.setCapacity ({});
    const auto nextPrice = s.check (load);
    spent = budget::spend ([&] { loaded = s.apply (load); });
    ok (loaded.rejection == Rejection::None && budget::covers (nextPrice.bytes, spent), "new-source replacement fits its declared overlap");
    return felitronics::test::report();
}
