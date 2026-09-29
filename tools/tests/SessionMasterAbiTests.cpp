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
#include <string>
#include <string_view>
#include <tuple>
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
    request.ceilingMarginDb = 0.15;
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
bool setTarget (fc_session handle, std::uint32_t id, const char* target)
{
    char json[128];
    const int n = std::snprintf (json, sizeof (json), R"({"kind":"setTarget","commandId":"%u","target":"%s"})", id, target);
    char answer[FC_SESSION_ANSWER_BYTES] {}; std::uint32_t written = 0;
    return n > 0 && fc_session_command (handle, json, std::uint32_t (n), answer, sizeof (answer), &written) == FC_SESSION_OK
        && std::string_view (answer, written).find ("\"accepted\"") != std::string_view::npos;
}
std::string eventsJson (fc_session handle)
{
    fc_session_sizes n { sizeof (fc_session_sizes) };
    if (fc_session_events_size (handle, &n) != FC_SESSION_OK) return {};
    std::string json (n.jsonBytes, '?'); std::vector<double> rows (n.rowBytes / 8u + 1u);
    if (fc_session_events_copy (handle, json.data(), n.jsonBytes, rows.data(), n.rowBytes) != FC_SESSION_OK) return {};
    return json;
}
// THE DELIVERY FORMAT IS THE TARGET'S, end to end through the C facade: dither.bits 0 or the target's own depth
// masters, and the WAV header carries exactly that depth.
bool formatThroughFacade (const char* target, std::int32_t bits, std::uint32_t targetBits)
{
    const auto handle = create();
    load (handle, 1);
    auto* s = contractSession (handle);
    bool good = setTarget (handle, 2, target);
    fc_master_config config {}; fc_master_params params {};
    ready (config, params);
    params.dither.bits = bits;
    const auto source = s->source().hash, revision = s->revision();
    char answer[FC_SESSION_ANSWER_BYTES] {}; std::uint32_t written = 0;
    good = good && fc_session_master (handle, 3, 0, std::uint32_t (source), std::uint32_t (source >> 32),
        std::uint32_t (revision), std::uint32_t (revision >> 32), &config, &params,
        answer, sizeof (answer), &written) == FC_SESSION_OK
        && std::string_view (answer, written).find ("\"accepted\"") != std::string_view::npos;
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
        const auto frames = s->masterAudioShape (s->pendingMaster()).frames;
        good = fc_session_master_wav_size (handle, &t, &bytes, &actualBits) == FC_SESSION_OK
            && actualBits == targetBits && bytes == double (44u + frames * 2u * targetBits / 8u);
        if (good)
        {
            std::uint8_t header[44] {}; std::uint32_t count = 0;
            good = fc_session_master_wav_copy (handle, &t, 0, 0, header, sizeof (header), &count)
                == FC_SESSION_OK && count == sizeof (header)
                && header[20] == 1 && header[21] == 0 && header[34] == targetBits && header[35] == 0;
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
            if (good) std::printf ("session-master-%s-dither%d-wav=%016llx\n", target, bits,
                static_cast<unsigned long long> (digest));
        }
    }
    good = fc_session_destroy (handle) == FC_SESSION_OK && good;
    return good;
}
// ...and any other depth is an OPEN refusal, before any allocation: the rejection DeliveryFormat (34) in the storage
// record and in the answer, and a fact (134) naming the target's depth in the events.
bool refusedThroughFacade (fc_session handle, std::uint32_t id, const char* target, std::int32_t bits,
                           std::uint32_t targetBits, std::string& why)
{
    auto* s = contractSession (handle);
    if (! setTarget (handle, id, target)) { why = "set target"; return false; }
    fc_master_config config {}; fc_master_params params {};
    ready (config, params);
    params.dither.bits = bits;
    const auto source = s->source().hash, revision = s->revision();
    fc_session_storage storage { sizeof (fc_session_storage) };
    fc_session_status priced = FC_SESSION_ERR_CONTRACT, started = FC_SESSION_ERR_CONTRACT;
    char answer[FC_SESSION_ANSWER_BYTES] {}; std::uint32_t written = 0;
    const auto spent = budget::spend ([&] {
        priced = fc_session_master_bytes (handle, std::uint32_t (source), std::uint32_t (source >> 32),
            std::uint32_t (revision), std::uint32_t (revision >> 32), &config, &params, &storage);
        started = fc_session_master (handle, id + 1u, 0, std::uint32_t (source), std::uint32_t (source >> 32),
            std::uint32_t (revision), std::uint32_t (revision >> 32), &config, &params,
            answer, sizeof (answer), &written);
    });
    const std::string_view reply (answer, written);
    const auto events = eventsJson (handle);
    const std::string depth = "\"integer\":\"" + std::to_string (targetBits) + "\"";
    why = std::string (target) + " dither.bits " + std::to_string (bits) + ": priced " + std::to_string (int (priced))
        + " rejection " + std::to_string (storage.rejection) + ", started " + std::to_string (int (started))
        + " " + std::string (reply) + ", " + std::to_string (spent.requests) + " allocations";
    return priced == FC_SESSION_OK && storage.rejection == 34u && storage.bytes == 0
        && started == FC_SESSION_OK && reply.find ("\"rejected\"") != std::string_view::npos
        && reply.find ("\"code\":34") != std::string_view::npos
        && spent.requests == 0 && s->job() == 0 && s->revision() == revision
        && events.find ("\"FactId\":134") != std::string::npos && events.find (depth) != std::string::npos;
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
            // EVERY OTHER OUTPUT IS FENCED AGAINST THE RETAINED PCM TOO: a copy onto itself, a shape, a view and a
            // waveform chunk written into the samples they describe.
            auto* retained = const_cast<float*> (view);
            ok (fc_session_master_audio_copy (handle, &t, retained, samples) == FC_SESSION_ERR_OVERLAP
                && fc_session_master_audio_copy (handle, &t, retained + 3, samples - 3u) == FC_SESSION_ERR_OVERLAP
                && std::memcmp (view, copied.data(), bytes) == 0,
                "PCM copy cannot write into the retained PCM it copies");
            double shapeBytes = -1.0; std::uint32_t shapeFrames = 7, shapeChannels = 7, shapeRate = 7;
            ok (fc_session_master_audio_size (handle, &t, reinterpret_cast<double*> (retained), &shapeFrames,
                    &shapeChannels, &shapeRate) == FC_SESSION_ERR_OVERLAP
                && fc_session_master_audio_size (handle, &t, &shapeBytes, reinterpret_cast<std::uint32_t*> (retained + 1),
                    &shapeChannels, &shapeRate) == FC_SESSION_ERR_OVERLAP
                && fc_session_master_audio_size (handle, &t, &shapeBytes, &shapeFrames,
                    reinterpret_cast<std::uint32_t*> (retained + 2), &shapeRate) == FC_SESSION_ERR_OVERLAP
                && fc_session_master_audio_size (handle, &t, &shapeBytes, &shapeFrames, &shapeChannels,
                    reinterpret_cast<std::uint32_t*> (retained + 3)) == FC_SESSION_ERR_OVERLAP
                && shapeBytes == -1.0 && shapeFrames == 7 && shapeChannels == 7 && shapeRate == 7
                && std::memcmp (view, saved, sizeof (saved)) == 0,
                "PCM shape cannot write any of its four numbers into the retained PCM");
            const float* viewed = nullptr; std::uint32_t viewedSamples = 7;
            ok (fc_session_master_audio_view (handle, &t, reinterpret_cast<const float**> (retained + 4), &viewedSamples)
                    == FC_SESSION_ERR_OVERLAP
                && fc_session_master_audio_view (handle, &t, &viewed, reinterpret_cast<std::uint32_t*> (retained + 1))
                    == FC_SESSION_ERR_OVERLAP
                && viewed == nullptr && viewedSamples == 7 && std::memcmp (view, saved, sizeof (saved)) == 0,
                "a scoped view cannot write its address or length into the retained PCM");
            char waveQuery[512];
            const auto waveLength = std::snprintf (waveQuery, sizeof (waveQuery),
                "{\"kind\":9,\"audioId\":\"%llu\",\"fromFrame\":\"100\",\"toFrame\":\"200\","
                "\"columns\":10,\"requestId\":\"4\",\"crossoverHz\":120,\"fromHz\":20,\"toHz\":250,"
                "\"masterId\":%u}", static_cast<unsigned long long> (source), session->masters().front().id);
            fc_session_sizes waveSizes { sizeof (fc_session_sizes) };
            const float* wavePcm[2] { copied.data() + 100, copied.data() + frames + 100 };
            const auto waveSized = fc_session_master_waveform_chunk_size (handle, waveQuery, std::uint32_t (waveLength),
                2, 100, rate, &waveSizes);
            std::vector<char> waveJson (waveSizes.jsonBytes + 1u);
            std::vector<double> waveRows (waveSizes.rowBytes / sizeof (double) + 1u);
            fc_session_sizes waveWritten { sizeof (fc_session_sizes) };
            const auto viewBefore = std::vector<float> (view, view + 64);
            ok (waveSized == FC_SESSION_OK
                && fc_session_master_waveform_chunk_copy (handle, waveQuery, std::uint32_t (waveLength), wavePcm, 2, 100,
                    rate, reinterpret_cast<char*> (retained), waveSizes.jsonBytes, waveRows.data(), waveSizes.rowBytes,
                    &waveWritten) == FC_SESSION_ERR_OVERLAP
                && fc_session_master_waveform_chunk_copy (handle, waveQuery, std::uint32_t (waveLength), wavePcm, 2, 100,
                    rate, waveJson.data(), waveSizes.jsonBytes, reinterpret_cast<double*> (retained), waveSizes.rowBytes,
                    &waveWritten) == FC_SESSION_ERR_OVERLAP
                && fc_session_master_waveform_chunk_copy (handle, waveQuery, std::uint32_t (waveLength), wavePcm, 2, 100,
                    rate, waveJson.data(), waveSizes.jsonBytes, waveRows.data(), waveSizes.rowBytes,
                    reinterpret_cast<fc_session_sizes*> (retained + 8)) != FC_SESSION_OK
                && std::memcmp (view, viewBefore.data(), viewBefore.size() * sizeof (float)) == 0,
                "a waveform chunk cannot write its JSON, rows or sizes into the retained PCM");
            const float* retainedPcm[2] { view + 100, view + frames + 100 };
            ok (fc_session_master_waveform_chunk_copy (handle, waveQuery, std::uint32_t (waveLength), retainedPcm, 2, 100,
                    rate, waveJson.data(), waveSizes.jsonBytes, waveRows.data(), waveSizes.rowBytes, &waveWritten)
                    == FC_SESSION_OK,
                "the retained PCM stays a valid waveform chunk INPUT");
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
    ok (formatThroughFacade ("cd", 0, 16), "cd: dither.bits 0 delivers the target's PCM16 through the C facade");
    ok (formatThroughFacade ("cd", 16, 16), "cd: dither.bits 16 restates the target's PCM16");
    ok (formatThroughFacade ("cdDynamic", 0, 16), "cdDynamic: dither.bits 0 delivers the target's PCM16");
    ok (formatThroughFacade ("allStreaming", 0, 24), "allStreaming: dither.bits 0 delivers the target's PCM24");
    ok (formatThroughFacade ("spotify", 24, 24), "spotify: dither.bits 24 restates the target's PCM24");
    {
        const auto refusing = create();
        load (refusing, 1);
        std::uint32_t id = 10;
        for (const auto& [target, bits, depth] : { std::tuple<const char*, std::int32_t, std::uint32_t>
                 { "allStreaming", 16, 24 }, { "cd", 24, 16 }, { "allStreaming", 32, 24 }, { "allStreaming", 20, 24 },
                 { "cd", -1, 16 }, { "cd", 272, 16 } })
        {
            std::string why;
            ok (refusedThroughFacade (refusing, id, target, bits, depth, why),
                "a depth other than the target's is an open refusal before any allocation, naming the target's (" + why + ")");
            id += 10;
        }
        ok (fc_session_destroy (refusing) == FC_SESSION_OK, "C session releases all storage after the refusals");
    }
    return felitronics::test::report();
}
