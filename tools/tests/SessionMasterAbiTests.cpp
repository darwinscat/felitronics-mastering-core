// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "fc_session_abi.h"
#include "../../modules/session/tests/DeclaredBudget.h"
#include <alloc_counter.h>
#include <felitronics_test.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/mastering/LandingSearch.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace felitronics::session;
namespace mastering = felitronics::mastering;
using felitronics::test::ok;
namespace alloc = felitronics::test::alloc;
namespace budget = felitronics::session::testing;

// The facade's test seam gives this test the independently typed session state.
Session* contractSession (fc_session) noexcept;

namespace
{
constexpr std::uint32_t rate = 48000;
constexpr std::uint32_t frames = 2 * rate;
constexpr char meta[] = R"({"name":"ready.wav","fileRate":48000,"bitDepth":24,"rateKnown":true})";

fc_session create()
{
    const auto version = config::Config::versions().all;
    const fc_session_capabilities caps { sizeof (fc_session_capabilities), 256.0 * 1024.0 * 1024.0,
        rate, FC_SESSION_DEVICES_ALL, 256.0 * 1024.0 * 1024.0 };
    fc_session handle = 0;
    ok (fc_session_create (&caps, std::uint32_t (version), std::uint32_t (version >> 32), &handle) == FC_SESSION_OK,
        "C facade creates a session");
    return handle;
}
void load (fc_session handle, std::uint32_t id)
{
    std::vector<float> left (frames), right (frames);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f;
        right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f;
    }
    const float* planes[] { left.data(), right.data() };
    char answer[FC_SESSION_ANSWER_BYTES]; std::uint32_t written = 0;
    ok (fc_session_load (handle, id, 0, planes, 2, frames, rate, meta, sizeof (meta) - 1,
                         answer, sizeof (answer), &written) == FC_SESSION_OK, "C facade loads source");
    auto* session = contractSession (handle);
    for (unsigned i = 0; i < 20000 && (session->state() == State::Loaded
                                     || ! session->snapshot().view().mandatoryMeasurementsReady); ++i)
    {
        std::uint32_t step = 0;
        ok (fc_session_step (handle, 16, &step) == FC_SESSION_OK, "C facade advances measurement");
    }
    ok (session->snapshot().view().mandatoryMeasurementsReady, "C facade source is measurable");
}
void ready (fc_master_config& config, fc_master_params& params)
{
    config = {}; params = {};
    config.header = { FC_MASTER_ABI_VERSION, sizeof (config) };
    config.sampleRate = rate; config.channels = 2; config.internalBlock = 256;
    config.oversampleFactor = 4; config.tapsPerPhase = 64; config.limiter = 1;
    config.limiterLookaheadMs = 1.0;
    params.header = { FC_MASTER_ABI_VERSION, sizeof (params) };
    params.compressorMix = 1.0;
}
bool directDacd2b2 (const Session& session, std::vector<float>& pcm,
                    mastering::LoudnessSolution& result)
{
    std::vector<float> source (frames * 2u);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        source[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f;
        source[frames + i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f;
    }
    const float* input[2] { source.data(), source.data() + frames };
    pcm.resize (source.size());
    float* output[2] { pcm.data(), pcm.data() + frames };
    mastering::MasteringChainConfig topology;
    topology.eq = topology.compressor = topology.dither = false;
    topology.compressorLookaheadMs = 0.0;
    topology.limiterLookaheadMs = 1.0;
    mastering::MasteringChainParams params;
    params.limiter.ceilingDbTp = -1.15;
    params.limiter.releaseMs = 0.0;
    params.limiter.slowReleaseMs = 0.0;
    params.limiter.overCeilingDb = 0.0;
    mastering::LoudnessRequest request;
    request.targetLufs = -14.0; request.maxTruePeakDbTp = -1.0;
    request.toleranceLu = 0.1; request.truePeakAimDb = 0.05;
    request.maxPasses = 12; request.initialGainDb = 4.0;
    request.normalizationGainDb = -18.0 - session.snapshot().view().integratedLufs;
    request.productLanding = true;
    request.grTraceBuckets = mastering::GainReductionTrace::kDefaultBuckets;
    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    if (! chain.prepare (rate, 2, topology) || ! renderer.prepare (2, 1024)
        || ! solver.prepare (rate, 2, 1024, topology.internalBlock, topology.oversampleFactor)) return false;
    chain.setParams (params);
    mastering::LandingSearch search (solver);
    if (! search.begin (chain, renderer, params, input, frames, rate, output, 2, frames, request)) return false;
    mastering::StepResult state = mastering::StepResult::More;
    for (unsigned i = 0; state == mastering::StepResult::More && i < 200000; ++i) state = search.step (73);
    result = search.result();
    return state == mastering::StepResult::Done;
}
fc_session_master_token token (const MasterToken& t)
{
    return { sizeof (fc_session_master_token), std::uint32_t (t.source), std::uint32_t (t.source >> 32),
        std::uint32_t (t.revision), std::uint32_t (t.revision >> 32), t.job, t.master };
}
}

int main()
{
    const auto handle = create();
    load (handle, 1);
    auto* session = contractSession (handle);
    fc_master_config topology {}; fc_master_params params {};
    ready (topology, params);
    fc_session_storage storage { sizeof (fc_session_storage) };
    const auto source = session->source().hash, revision = session->revision();
    const auto count = alloc::count.load();
    const auto stale = fc_session_master_bytes (handle, std::uint32_t (source + 1), std::uint32_t ((source + 1) >> 32),
        std::uint32_t (revision), std::uint32_t (revision >> 32), &topology, &params, &storage);
    const bool refusedWithoutAllocation = stale == FC_SESSION_ERR_STALE && alloc::count.load() == count;
    ok (refusedWithoutAllocation, "stale source is refused before allocation");
    const auto priced = fc_session_master_bytes (handle, std::uint32_t (source), std::uint32_t (source >> 32),
        std::uint32_t (revision), std::uint32_t (revision >> 32), &topology, &params, &storage);
    ok (priced == FC_SESSION_OK && storage.rejection == 0 && storage.bytes > 0, "ready request has a preflight demand");
    alignas (8) char aliased[FC_SESSION_ANSWER_BYTES] {};
    fc_session_status overlapStatus = FC_SESSION_ERR_CONTRACT;
    const auto overlapSpent = budget::spend ([&] {
        overlapStatus = fc_session_master (handle, 77, 0, std::uint32_t (source), std::uint32_t (source >> 32),
            std::uint32_t (revision), std::uint32_t (revision >> 32), &topology, &params,
            aliased, sizeof (aliased), reinterpret_cast<std::uint32_t*> (aliased));
    });
    ok (overlapStatus == FC_SESSION_ERR_OVERLAP && overlapSpent.requests == 0 && session->job() == 0,
        "overlapping answer and written are rejected before a master starts");
    char answer[FC_SESSION_ANSWER_BYTES]; std::uint32_t written = 0;
    fc_session_status started = FC_SESSION_ERR_CONTRACT;
    const auto spent = budget::spend ([&] { started = fc_session_master (handle, 2, 0, std::uint32_t (source), std::uint32_t (source >> 32),
        std::uint32_t (revision), std::uint32_t (revision >> 32), &topology, &params,
        answer, sizeof (answer), &written); });
    ok (started == FC_SESSION_OK && session->job() != 0 && budget::covers (std::uint64_t (storage.bytes), spent),
        "ready C command starts the real job within its declared demand");
    ok (fc_session_master (handle, 3, 0, std::uint32_t (source), std::uint32_t (source >> 32),
        std::uint32_t (revision), std::uint32_t (revision >> 32), &topology, &params,
        answer, sizeof (answer), &written) == FC_SESSION_ERR_STALE, "stale revision cannot start a second job");
    for (unsigned i = 0; i < 40000 && session->job() != 0; ++i)
    {
        std::uint32_t step = 0;
        if (fc_session_step (handle, 16, &step) != FC_SESSION_OK) break;
    }
    ok (session->job() == 0 && session->masters().size() == 1 && session->pendingMaster().master != 0,
        "C job completes with transferable PCM");
    if (session->pendingMaster().master != 0)
    {
        auto t = token (session->pendingMaster());
        double bytes = 0; std::uint32_t outFrames = 0, channels = 0, outRate = 0;
        ok (fc_session_master_audio_size (handle, &t, &bytes, &outFrames, &channels, &outRate) == FC_SESSION_OK
            && bytes == double (frames * 2u * sizeof (float)) && outFrames == frames
            && channels == 2 && outRate == rate, "transfer publishes planar shape");
        std::vector<float> copied (frames * 2u);
        ok (fc_session_master_audio_copy (handle, &t, copied.data(), std::uint32_t (copied.size())) == FC_SESSION_OK,
            "C facade copies PCM into caller storage");
        std::uint64_t digest = 0xcbf29ce484222325ull;
        for (float sample : copied)
        {
            const auto bits = std::bit_cast<std::uint32_t> (sample);
            for (unsigned shift = 0; shift < 32; shift += 8)
                digest = (digest ^ std::uint8_t (bits >> shift)) * 0x100000001b3ull;
        }
        std::printf ("session-master-audio=%016llx\n", static_cast<unsigned long long> (digest));
        std::vector<float> previous;
        mastering::LoudnessSolution prior;
        const bool oracle = directDacd2b2 (*session, previous, prior);
        const auto retained = session->masters();
        const auto& published = *retained.front().landing;
        ok (oracle && previous.size() == copied.size()
            && std::memcmp (previous.data(), copied.data(), bytes) == 0
            && published.achievedLufs && std::bit_cast<std::uint64_t> (*published.achievedLufs)
                == std::bit_cast<std::uint64_t> (prior.achievedLufs)
            && published.truePeakDbTp && std::bit_cast<std::uint64_t> (*published.truePeakDbTp)
                == std::bit_cast<std::uint64_t> (prior.measured.truePeakDbTp)
            && published.passes == std::uint32_t (prior.passes),
            "C bridge matches the dacd2b2 direct PCM, LUFS, true peak and passes");
        const float* view = nullptr; std::uint32_t samples = 0;
        ok (fc_session_master_audio_view (handle, &t, &view, &samples) == FC_SESSION_OK
            && samples == copied.size() && std::memcmp (view, copied.data(), bytes) == 0,
            "scoped wasm view is bit identical to copy");
        t.job += 1;
        ok (fc_session_master_audio_copy (handle, &t, copied.data(), std::uint32_t (copied.size())) == FC_SESSION_ERR_STALE,
            "mismatched job cannot copy PCM");
        t.job -= 1;
        ok (fc_session_master_audio_release (handle, &t) == FC_SESSION_OK
            && fc_session_master_audio_release (handle, &t) == FC_SESSION_ERR_STALE,
            "release is explicit and idempotently refused");
    }
    ok (fc_session_destroy (handle) == FC_SESSION_OK, "C session releases all storage");
    return felitronics::test::report();
}
