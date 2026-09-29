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
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
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
    std::uint64_t inputDigest = 0xcbf29ce484222325ull;
    for (const auto* plane : { left.data(), right.data() })
        for (std::size_t i = 0; i < frames; ++i)
        {
            const auto bits = std::bit_cast<std::uint32_t> (plane[i]);
            for (unsigned shift = 0; shift < 32; shift += 8)
                inputDigest = (inputDigest ^ std::uint8_t (bits >> shift)) * 0x100000001b3ull;
        }
    std::printf ("session-master-input=%016llx\n", static_cast<unsigned long long> (inputDigest));
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
bool currentDirect (const Session& session, std::vector<float>& pcm,
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
    request.pcmBits = 24;
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
bool formatThroughFacade (std::uint32_t bits)
{
    const auto handle = create();
    load (handle, bits);
    auto* s = contractSession (handle);
    fc_master_config config {}; fc_master_params params {};
    ready (config, params);
    params.dither.bits = std::int32_t (bits);
    const auto source = s->source().hash, revision = s->revision();
    char answer[FC_SESSION_ANSWER_BYTES] {}; std::uint32_t written = 0;
    bool good = fc_session_master (handle, bits, 0, std::uint32_t (source), std::uint32_t (source >> 32),
        std::uint32_t (revision), std::uint32_t (revision >> 32), &config, &params,
        answer, sizeof (answer), &written) == FC_SESSION_OK;
    for (unsigned i = 0; good && i < 40000 && s->job() != 0; ++i)
    {
        std::uint32_t spent = 0;
        good = fc_session_step (handle, 16, &spent) == FC_SESSION_OK;
    }
    good = good && s->job() == 0 && s->pendingMaster().master != 0;
    if (good)
    {
        const auto t = token (s->pendingMaster());
        double bytes = 0; std::uint32_t actualBits = 0;
        good = fc_session_master_wav_size (handle, &t, &bytes, &actualBits) == FC_SESSION_OK
            && actualBits == bits && bytes == double (44u + frames * 2u * bits / 8u);
        if (good)
        {
            std::uint8_t header[44] {}; std::uint32_t count = 0;
            good = fc_session_master_wav_copy (handle, &t, 0, 0, header, sizeof (header), &count)
                == FC_SESSION_OK && count == sizeof (header)
                && header[20] == (bits == 32 ? 3 : 1) && header[34] == bits;
        }
        if (good)
        {
            std::uint64_t digest = 0xcbf29ce484222325ull;
            std::uint8_t chunk[997] {};
            for (std::uint32_t at = 0; good && double (at) < bytes; at += sizeof (chunk))
            {
                const auto n = std::uint32_t (std::min<double> (sizeof (chunk), bytes - at));
                std::uint32_t count = 0;
                good = fc_session_master_wav_copy (handle, &t, at, 0, chunk, n, &count) == FC_SESSION_OK
                    && count == n;
                for (std::uint32_t i = 0; i < count; ++i)
                    digest = (digest ^ chunk[i]) * 0x100000001b3ull;
            }
            if (good) std::printf ("session-master-format%u-wav=%016llx\n", bits,
                static_cast<unsigned long long> (digest));
        }
    }
    good = fc_session_destroy (handle) == FC_SESSION_OK && good;
    return good;
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
        double wavBytes = 0; std::uint32_t wavBits = 0;
        const auto wavSizeAllocations = alloc::count.load();
        const auto wavStatus = fc_session_master_wav_size (handle, &t, &wavBytes, &wavBits);
        const bool wavSizedWithoutAllocation = alloc::count.load() == wavSizeAllocations;
        ok (wavStatus == FC_SESSION_OK
            && wavBits == 24 && wavBytes == double (44u + frames * 2u * 3u),
            "safe master publishes an exact allocation-free WAV size");
        ok (wavSizedWithoutAllocation, "WAV sizing allocates nothing");
        if (wavBytes < 44 || wavBytes > 1000000) return felitronics::test::report();
        std::vector<std::uint8_t> wav (std::size_t (wavBytes), 0u);
        bool copiedWav = true;
        const auto wavSliceAllocations = alloc::count.load();
        for (std::size_t at = 0; at < wav.size(); at += 997u)
        {
            std::uint32_t count = 0;
            const auto n = std::uint32_t (std::min<std::size_t> (997u, wav.size() - at));
            copiedWav &= fc_session_master_wav_copy (handle, &t, std::uint32_t (at), std::uint32_t (at >> 32),
                wav.data() + at, n, &count) == FC_SESSION_OK && count == n;
        }
        const bool slicesWithoutAllocation = alloc::count.load() == wavSliceAllocations;
        ok (slicesWithoutAllocation, "WAV slicing allocates no hidden full-song copy");
        std::uint8_t first[59] {}; std::uint32_t firstWritten = 0;
        ok (copiedWav && fc_session_master_wav_copy (handle, &t, 0, 0, first, sizeof (first), &firstWritten) == FC_SESSION_OK
            && firstWritten == sizeof (first) && std::memcmp (first, wav.data(), sizeof (first)) == 0,
            "bounded irregular slices and repeated bytes agree without rendering");
        const auto read32 = [&] (std::size_t at) noexcept -> std::uint32_t
        { return std::uint32_t (wav[at]) | (std::uint32_t (wav[at + 1]) << 8)
               | (std::uint32_t (wav[at + 2]) << 16) | (std::uint32_t (wav[at + 3]) << 24); };
        ok (std::memcmp (wav.data(), "RIFF", 4) == 0 && std::memcmp (wav.data() + 8, "WAVEfmt ", 8) == 0
            && read32 (4) + 8u == wav.size() && read32 (24) == rate
            && wav[34] == 24 && read32 (40) == frames * 2u * 3u,
            "independent WAV reader validates the delivered header");
        std::vector<float> decoded (frames * 2u);
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < 2; ++c)
            {
                const auto at = 44u + (i * 2u + c) * 3u;
                const auto raw = std::uint32_t (wav[at]) | (std::uint32_t (wav[at + 1]) << 8)
                    | (std::uint32_t (wav[at + 2]) << 16);
                const auto code = std::int32_t (raw) - ((raw & 0x800000u) ? 0x1000000 : 0);
                decoded[c * frames + i] = float (double (code) / 8388608.0);
            }
        ok (decoded == copied, "downloaded WAV samples equal the measured and listened master PCM");
        felitronics::analysis::ReferenceTruePeakMeter filePeak;
        const float* decodedPlanes[] { decoded.data(), decoded.data() + frames };
        const bool peakPrepared = filePeak.prepare (rate, 1024, 2);
        bool peakRead = peakPrepared;
        for (std::size_t at = 0; peakRead && at < frames; at += 1024u)
        {
            const float* block[] { decodedPlanes[0] + at, decodedPlanes[1] + at };
            peakRead &= filePeak.process (block, 2, int (std::min<std::size_t> (1024u, frames - at)));
        }
        if (peakRead) filePeak.drain();
        ok (peakRead && session->masters().front().report
            && filePeak.truePeakDb() <= session->masters().front().report->ceilingDbTp,
            "reference true peak of downloaded, decoded PCM respects the ceiling");
        ok (peakRead && session->masters().front().report->truePeakDbTp
            && std::bit_cast<std::uint64_t> (filePeak.truePeakDb())
                == std::bit_cast<std::uint64_t> (*session->masters().front().report->truePeakDbTp),
            "reported reference true peak is the decoded WAV's reading");
        std::uint64_t wavDigest = 0xcbf29ce484222325ull;
        for (const auto byte : wav) wavDigest = (wavDigest ^ byte) * 0x100000001b3ull;
        std::printf ("session-master-wav=%016llx\n", static_cast<unsigned long long> (wavDigest));
        std::printf ("session-master-wav-header=");
        for (std::size_t i = 0; i < 44; ++i) std::printf ("%02x", unsigned (wav[i]));
        std::printf ("\n");
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
        const bool oracle = currentDirect (*session, previous, prior);
        const auto retained = session->masters();
        const auto& published = *retained.front().landing;
        ok (oracle && previous.size() == copied.size()
            && std::memcmp (previous.data(), copied.data(), bytes) == 0
            && published.achievedLufs && std::bit_cast<std::uint64_t> (*published.achievedLufs)
                == std::bit_cast<std::uint64_t> (prior.achievedLufs)
            && published.truePeakDbTp && std::bit_cast<std::uint64_t> (*published.truePeakDbTp)
                == std::bit_cast<std::uint64_t> (prior.measured.truePeakDbTp)
            && published.passes == std::uint32_t (prior.passes),
            "C bridge matches current direct PCM, LUFS, true peak and passes");
        const float* view = nullptr; std::uint32_t samples = 0;
        ok (fc_session_master_audio_view (handle, &t, &view, &samples) == FC_SESSION_OK
            && samples == copied.size() && std::memcmp (view, copied.data(), bytes) == 0,
            "scoped wasm view is bit identical to copy");
        std::uint32_t overlapWritten = 77;
        ok (fc_session_master_wav_copy (handle, &t, 0, 0,
            reinterpret_cast<std::uint8_t*> (const_cast<float*> (view)), 16, &overlapWritten)
            == FC_SESSION_ERR_OVERLAP && overlapWritten == 77
            && std::memcmp (view, copied.data(), bytes) == 0,
            "WAV output cannot overwrite the retained PCM or its completion count");
        if (view)
        {
            double safeBytes = -1.0;
            std::uint32_t safeBits = 99;
            std::uint8_t saved[16] {};
            std::memcpy (saved, view, sizeof (saved));
            ok (fc_session_master_wav_size (handle, &t,
                    reinterpret_cast<double*> (const_cast<float*> (view)), &safeBits)
                    == FC_SESSION_ERR_OVERLAP && safeBits == 99
                    && std::memcmp (view, saved, sizeof (saved)) == 0,
                "WAV size cannot write its byte count into retained PCM");
            ok (fc_session_master_wav_size (handle, &t, &safeBytes,
                    reinterpret_cast<std::uint32_t*> (const_cast<float*> (view) + 2))
                    == FC_SESSION_ERR_OVERLAP && safeBytes == -1.0
                    && std::memcmp (view, saved, sizeof (saved)) == 0,
                "WAV size cannot write its bit depth into retained PCM");
            std::uint8_t scratch[64] {};
            ok (fc_session_master_wav_copy (handle, &t, 0, 0, scratch, sizeof (scratch),
                    reinterpret_cast<std::uint32_t*> (const_cast<float*> (view) + 3))
                    == FC_SESSION_ERR_OVERLAP && std::memcmp (view, saved, sizeof (saved)) == 0,
                "WAV copy cannot write its completion count into retained PCM");
        }
        t.job += 1;
        ok (fc_session_master_audio_copy (handle, &t, copied.data(), std::uint32_t (copied.size())) == FC_SESSION_ERR_STALE,
            "mismatched job cannot copy PCM");
        t.job -= 1;
        ok (fc_session_master_audio_release (handle, &t) == FC_SESSION_OK
            && fc_session_master_audio_release (handle, &t) == FC_SESSION_ERR_STALE,
            "release is explicit and idempotently refused");
        double staleWav = -1; std::uint32_t staleBits = 99;
        ok (fc_session_master_wav_size (handle, &t, &staleWav, &staleBits) == FC_SESSION_ERR_STALE
            && staleWav == -1 && staleBits == 99 && ! wav.empty(),
            "owned WAV bytes survive PCM release while the session refuses a stale export");
        char query[512];
        const auto queryLength = std::snprintf (query, sizeof (query),
            "{\"kind\":9,\"audioId\":\"%llu\",\"fromFrame\":\"100\",\"toFrame\":\"200\","
            "\"columns\":10,\"requestId\":\"3\",\"crossoverHz\":120,\"fromHz\":20,\"toHz\":250,"
            "\"masterId\":%u}", static_cast<unsigned long long> (source), retained.front().id);
        fc_session_storage chunkStorage { sizeof (fc_session_storage) };
        fc_session_sizes chunkSizes { sizeof (fc_session_sizes) }, chunkWritten { sizeof (fc_session_sizes) };
        const auto inputBytes = std::uint32_t (queryLength);
        const auto pricedChunk = fc_session_master_waveform_chunk_bytes (handle, query, inputBytes,
            2, 100, rate, &chunkStorage);
        const auto sizedChunk = fc_session_master_waveform_chunk_size (handle, query, inputBytes,
            2, 100, rate, &chunkSizes);
        std::vector<char> chunkJson (chunkSizes.jsonBytes);
        std::vector<double> chunkRows (chunkSizes.rowBytes / sizeof (double));
        const float* chunkPcm[2] { copied.data() + 100, copied.data() + frames + 100 };
        fc_session_status chunkStatus = FC_SESSION_ERR_CONTRACT;
        const auto chunkSpent = budget::spend ([&] {
            chunkStatus = fc_session_master_waveform_chunk_copy (handle, query, inputBytes, chunkPcm,
                2, 100, rate, chunkJson.data(), std::uint32_t (chunkJson.size()), chunkRows.data(),
                chunkSizes.rowBytes, &chunkWritten);
        });
        ok (pricedChunk == FC_SESSION_OK && sizedChunk == FC_SESSION_OK && chunkStatus == FC_SESSION_OK
            && chunkStorage.rejection == 0 && budget::covers (std::uint64_t (chunkStorage.bytes), chunkSpent)
            && chunkWritten.rowBytes == 20u * 7u * sizeof (double)
            && chunkRows[0] == 100 && chunkRows[1] == 110,
            "C facade reads an explicit delivered PCM chunk after release within its declared budget");
    }
    ok (fc_session_destroy (handle) == FC_SESSION_OK, "C session releases all storage");
    ok (formatThroughFacade (16), "C facade delivers explicitly requested PCM16");
    ok (formatThroughFacade (24), "C facade delivers explicitly requested PCM24");
    ok (formatThroughFacade (32), "C facade delivers explicitly requested float32");
    return felitronics::test::report();
}
