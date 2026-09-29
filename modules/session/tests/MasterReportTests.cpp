// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "DeclaredBudget.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/analysis/BandCrestResult.h>
#include <felitronics/analysis/ProgrammeReport.h>
#include <felitronics/mastering/OfflineRenderer.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;

namespace
{
struct Case
{
    std::vector<float> left, right;
    std::unique_ptr<session::Session> session;
    std::uint32_t rate = 0, delivery = 0;
    session::command::Master request {};
    session::MasterToken token {};
    std::vector<float> delivered;
};
bool run (Case& c, std::uint32_t sourceRate, std::uint32_t deliveryRate, bool processed,
          bool waitForCrest = true, bool demanding = false)
{
    c.rate = sourceRate; c.delivery = deliveryRate;
    const std::size_t frames = std::size_t (sourceRate) * 4u;
    c.left.resize (frames); c.right.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        c.left[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f;
        c.right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f;
    }
    if (! demanding) c.right.back() -= 0.15f;
    const float* planes[2] { c.left.data(), c.right.data() };
    auto made = session::Session::create();
    if (made.status != session::Status::Ok) return false;
    c.session = std::move (made.session);
    auto& s = *c.session;
    if (s.apply (session::command::Load { 1, { planes, 2, frames, sourceRate },
        { "report.wav", sourceRate, true, 24 } }).rejection != session::Rejection::None) return false;
    for (unsigned i = 0; i < 200000 && (waitForCrest ? s.state() != session::State::Measured2
        : s.state() == session::State::Loaded || ! s.snapshot().view().mandatoryMeasurementsReady); ++i)
        (void) s.step (16);
    if (! s.snapshot().view().mandatoryMeasurementsReady
        || (waitForCrest && s.snapshot().view().measurements[std::size_t (session::Analyzer::Crest)].status
            != session::MeasurementStatus::Ready)) return false;
    if (demanding && s.apply (session::command::EditTarget { 9, { -5.0, -6.0 } }).rejection
        != session::Rejection::None) return false;
    c.request = session::command::Master { 2 };
    c.request.ready.version = 1;
    c.request.ready.deliveryRateHz = deliveryRate;
    c.request.ready.topology.eq = processed;
    c.request.ready.topology.compressor = processed;
    c.request.ready.topology.clipper = processed;
    c.request.ready.topology.limiter = processed || demanding;
    c.request.ready.topology.dither = false;
    if (processed)
    {
        c.request.ready.params.eqBands[0].on = true;
        c.request.ready.params.eqBands[0].lanes[0].freq = 200;
        c.request.ready.params.eqBands[0].lanes[0].gainDb = 1.5;
        c.request.ready.params.compressor.thresholdDb = -30;
        c.request.ready.params.compressor.ratio = 2.5;
        c.request.ready.params.clipper.mix = 0.5f;
    }
    c.request.source = s.source().hash; c.request.revision = s.revision();
    const auto checked = s.check (c.request);
    if (checked.rejection != session::Rejection::None) return false;
    session::Answer start;
    const auto spent = budget::spend ([&] { start = s.apply (c.request); });
    if (start.rejection != session::Rejection::None || ! budget::covers (checked.bytes, spent)) return false;
    std::uint64_t largestStep = 0;
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
    {
        const auto stepSpent = budget::spend ([&] { (void) s.step (i % 3u == 0 ? 1u : i % 3u == 1 ? 7u : 73u); });
        largestStep = std::max (largestStep, std::uint64_t (stepSpent.bytes));
    }
    if (s.job() != 0 || largestStep > checked.bytes || s.masters().size() != 1
        || ! s.masters()[0].report || ! s.masters()[0].landing) return false;
    c.token = s.pendingMaster();
    const auto shape = s.masterAudioShape (c.token);
    if (c.token.master == 0 || shape.sampleRate != deliveryRate) return false;
    c.delivered.resize (std::size_t (shape.frames * shape.channels));
    return s.copyMaster (c.token, c.delivered) == session::MasterTransferStatus::Ok;
}
bool referenceCrest (const Case& c)
{
    const auto& master = c.session->masters()[0];
    const auto& crest = master.report->crest;
    if (crest.status != session::MeasurementStatus::Ready || ! crest.complete
        || crest.sourceRateCheck != (c.rate != c.delivery)
        || master.report->checkPasses != (c.rate != c.delivery ? 1u : 0u)
        || crest.sampleRateHz != c.rate || crest.rows.size() != crest.blocks * 10u
        || crest.sourceMask.size() != crest.blocks * 5u) return false;
    const auto sourceSnapshot = c.session->snapshot();
    const auto& sourceResult = sourceSnapshot.view().measurements[std::size_t (session::Analyzer::Crest)];
    const auto source = session::MeasurementCrest::view (sourceResult);
    if (source.blockCount() != crest.blocks || source.hopSamples != crest.hopFrames
        || source.mask.size() != crest.sourceMask.size()
        || std::memcmp (source.mask.data(), crest.sourceMask.data(), source.mask.size_bytes()) != 0) return false;
    if (c.rate == c.delivery) return true;
    const auto& last = master.landing->log.back();
    auto topology = c.request.ready.topology;
    auto params = c.request.ready.params;
    params.preLimiterGainDb = last.gainDb;
    params.limiter.ceilingDbTp = last.ceilingDbTp;
    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    if (! chain.prepare (double (c.rate), 2, topology) || ! renderer.prepare (2, 1024)) return false;
    chain.setParams (params);
    std::vector<float> oracle (c.left.size() * 2u);
    const float* in[2] { c.left.data(), c.right.data() };
    float* out[2] { oracle.data(), oracle.data() + c.left.size() };
    if (! renderer.render (chain, in, out, 2, int (c.left.size()))) return false;
    analysis::BandCrest direct;
    direct.setParams (source.parameters);
    if (! direct.prepare (double (c.rate), 2, (long long) c.left.size())) return false;
    const float* measured[2] { out[0], out[1] };
    for (std::size_t i = 0; i < c.left.size(); i += 73u)
    {
        const auto n = int (std::min<std::size_t> (73u, c.left.size() - i));
        const float* piece[2] { measured[0] + i, measured[1] + i };
        if (! direct.process (piece, 2, n)) return false;
    }
    direct.finish();
    if (! direct.valid() || std::uint64_t (direct.blockCount()) != crest.blocks) return false;
    for (std::size_t row = 0; row < crest.blocks; ++row)
        for (int band = 0; band < 5; ++band)
        {
            const auto a = std::bit_cast<std::uint64_t> (crest.rows[row * 10u + std::size_t (band * 2)]);
            const auto b = std::bit_cast<std::uint64_t> (direct.blockPeakLin ((long long) row, band));
            const auto x = std::bit_cast<std::uint64_t> (crest.rows[row * 10u + std::size_t (band * 2 + 1)]);
            const auto y = std::bit_cast<std::uint64_t> (direct.blockMeanSq ((long long) row, band));
            if (a != b || x != y) return false;
        }
    return true;
}
bool directMeter (const Case& c)
{
    const auto& report = *c.session->masters()[0].report;
    const auto& shape = c.session->masterAudioShape (c.token);
    analysis::ProgrammeReport meter;
    analysis::ProgrammeReportParams params;
    params.maxDurationSec = double (shape.frames) / double (shape.sampleRate) + 1.0;
    meter.setParams (params);
    if (! meter.prepare (double (shape.sampleRate), 1024, int (shape.channels))) return false;
    for (std::size_t i = 0; i < shape.frames; i += 73u)
    {
        const auto n = int (std::min<std::size_t> (73u, std::size_t (shape.frames) - i));
        const float* piece[2] { c.delivered.data() + i,
            c.delivered.data() + shape.frames + i };
        if (! meter.process (piece, 2, n)) return false;
    }
    meter.finish();
    const auto& direct = meter.report();
    double sourceLufs = std::numeric_limits<double>::quiet_NaN();
    const auto source = c.session->snapshot();
    for (const auto& value : source.view().measurements[std::size_t (session::Analyzer::Loudness)].numbers)
        if (value.name == "integratedLufs" && value.value) sourceLufs = *value.value;
    return report.status == session::MeasurementStatus::Ready && report.peakSafe
        && report.achievedLufs && report.truePeakDbTp && report.missLu && report.gainFromSourceDb
        && direct.integratedLufs.valid && direct.truePeakDbtp.valid
        && std::isfinite (sourceLufs)
        && std::fabs (*report.achievedLufs - direct.integratedLufs.value) < 0.05
        && std::fabs (*report.truePeakDbTp - direct.truePeakDbtp.value) < 0.05
        && std::fabs (*report.gainFromSourceDb - (direct.integratedLufs.value - sourceLufs)) < 0.05
        && std::fabs (*report.missLu - (*report.achievedLufs - report.targetLufs)) < 1e-12
        && report.plrDb && direct.plrDb.valid
        && std::fabs (*report.plrDb - direct.plrDb.value) < 0.05
        && (! report.lraLu || (direct.lraLu.valid
            && std::fabs (*report.lraLu - direct.lraLu.value) < 0.05));
}
}

int main()
{
    Case gain;
    ok (run (gain, 48000, 48000, false), "gain-only master and declared budget complete");
    if (gain.session && ! gain.session->masters().empty())
    {
        ok (directMeter (gain), "delivered LUFS and reference true peak match a direct programme report");
        ok (referenceCrest (gain), "source and master crest have the same explicit grid and source mask");
        const auto& crest = gain.session->masters()[0].report->crest;
        const auto sourceSnapshot = gain.session->snapshot();
        const auto source = session::MeasurementCrest::view (
            sourceSnapshot.view().measurements[std::size_t (session::Analyzer::Crest)]);
        double largest = 0;
        for (std::size_t i = 0; i < crest.blocks; ++i)
        {
            const double p = source.blockPeakLin (i, 4), m = source.blockMeanSq (i, 4);
            const double q = crest.rows[i * 10u + 8u], n = crest.rows[i * 10u + 9u];
            if (p > 0 && m > 0 && q > 0 && n > 0 && crest.sourceMask[i * 5u + 4u] > 0)
                largest = std::max (largest, std::fabs ((q * q / n) / (p * p / m) - 1.0));
        }
        ok (largest < 0.01, "gain-only crest comparison does not invent impact loss");
        const auto snapshotBytes = session::Snapshot::storageFor (sourceSnapshot.view());
        session::Snapshot copy;
        const auto spent = budget::spend ([&] { copy = gain.session->snapshot(); });
        ok (budget::covers (snapshotBytes, spent)
            && copy.view().masters[0].report->crest.rows.size() == crest.rows.size(),
            "owned snapshot retains compact master crest inside declared memory");
        const auto needed = session::Codec::encodedBytes (copy.view());
        std::vector<char> json (std::size_t (needed.bytes));
        session::Snapshot decoded;
        const auto decodedNeed = needed.status == session::CodecStatus::Ok
            && session::Codec::encode (copy.view(), json) == session::CodecStatus::Ok
            ? session::Codec::decodedBytes ({ json.data(), json.size() }) : session::CodecNeed {};
        session::CodecStatus status = session::CodecStatus::Invalid;
        const auto decodedSpent = budget::spend ([&] {
            status = session::Codec::decode ({ json.data(), json.size() }, decoded);
        });
        ok (status == session::CodecStatus::Ok && budget::covers (decodedNeed.bytes, decodedSpent)
            && decoded.view().masters[0].report->crest.rows.size() == crest.rows.size()
            && decoded.view().masters[0].report->deliverable == gain.session->masters()[0].report->deliverable
            && decoded.view().masters[0].report->targetMet == gain.session->masters()[0].report->targetMet
            && decoded.view().masters[0].report->checkPasses == gain.session->masters()[0].report->checkPasses,
            "one generated codec round-trips the owned report within its declared demand");
        std::string malformed (json.data(), json.size());
        const auto key = malformed.find ("\"blocks\":\"");
        if (key != std::string::npos)
        {
            const auto first = key + sizeof ("\"blocks\":\"") - 1u;
            const auto last = malformed.find ('"', first);
            malformed.replace (first, last - first, std::to_string (crest.blocks + 1u));
        }
        session::Snapshot invalid;
        session::CodecStatus invalidStatus = session::CodecStatus::Ok;
        const auto invalidSpent = budget::spend ([&] {
            invalidStatus = session::Codec::decode (malformed, invalid);
        });
        ok (key != std::string::npos && invalidStatus == session::CodecStatus::Invalid
            && invalidSpent.requests == 0
            && session::Codec::decodedBytes (malformed).status == session::CodecStatus::Invalid,
            "an inconsistent crest row total is rejected before decode allocates");
        std::string incoherent (json.data(), json.size());
        const auto met = gain.session->masters()[0].report->targetMet;
        const auto field = std::string ("\"targetMet\":") + (met ? "true" : "false");
        const auto at = incoherent.find (field);
        if (at != std::string::npos)
            incoherent.replace (at, field.size(), std::string ("\"targetMet\":") + (met ? "false" : "true"));
        const auto mismatchSpent = budget::spend ([&] {
            invalidStatus = session::Codec::decode (incoherent, invalid);
        });
        ok (at != std::string::npos && invalidStatus == session::CodecStatus::Invalid
            && mismatchSpent.requests == 0,
            "a report that disagrees with its landing status is rejected before allocation");
        const auto altered = source;
        auto grid = altered; grid.sampleRate += 1.0;
        ok (session::MasterCrestGrid::compatible (crest, altered)
            && ! session::MasterCrestGrid::compatible (crest, grid),
            "crest pairing checks sample rate rather than row count");
        grid = altered; grid.hopSamples += 1;
        ok (! session::MasterCrestGrid::compatible (crest, grid),
            "crest pairing checks hop size");
        grid = altered; grid.parameters.bandEdgeHz[1] += 1.0;
        ok (! session::MasterCrestGrid::compatible (crest, grid),
            "crest pairing checks all band corners");
        grid = altered; grid.droppedHops = 1;
        ok (! session::MasterCrestGrid::compatible (crest, grid),
            "crest pairing refuses incomplete source rows");
    }
    Case late;
    ok (run (late, 48000, 48000, false, false),
        "master can finish while the optional source crest is still pending");
    if (late.session && ! late.session->masters().empty())
    {
        ok (late.session->masters()[0].report->crest.status == session::MeasurementStatus::Pending
            && late.session->pendingMaster().master != 0,
            "pending crest is distinct from an unavailable master");
        const auto measuring = late.session->measurementJob();
        if (measuring != 0) (void) late.session->apply (session::command::Cancel { 3, measuring });
        ok (late.session->masters()[0].report->crest.status == session::MeasurementStatus::Unavailable
            && late.session->masters()[0].report->crest.reason == session::MeasurementReason::Cancelled
            && late.session->pendingMaster().master != 0,
            "final source crest refusal keeps safe PCM while settling the missing comparison");
    }
    Case lateReady;
    ok (run (lateReady, 48000, 48000, false, false),
        "a second master can finish before the source crest without waiting for it");
    if (lateReady.session && ! lateReady.session->masters().empty())
    {
        ok (lateReady.session->masters()[0].report->crest.status == session::MeasurementStatus::Pending,
            "the unfinished source comparison stays pending");
        bool crestPublished = false;
        for (unsigned i = 0; i < 200000 && ! crestPublished; ++i)
        {
            (void) lateReady.session->step (1);
            for (const auto& event : lateReady.session->events())
                if (event.kind == session::EventKind::Measurement
                    && event.payload.measurement.analyzer == session::Analyzer::Crest
                    && event.payload.measurement.status == session::MeasurementStatus::Ready)
                    crestPublished = true;
        }
        const auto beforeJoin = lateReady.session->revision();
        ok (crestPublished
            && lateReady.session->masters()[0].report->crest.status == session::MeasurementStatus::Pending,
            "source completion leaves a distinct bounded master crest join to pump");
        bool joinAllocated = false, joinOverran = false;
        for (unsigned i = 0; i < 200000
            && lateReady.session->masters()[0].report->crest.status == session::MeasurementStatus::Pending; ++i)
        {
            session::Stepped joined;
            const auto spent = budget::spend ([&] { joined = lateReady.session->step (1); });
            joinAllocated = joinAllocated || spent.requests != 0;
            joinOverran = joinOverran || joined.units > 1;
        }
        ok (lateReady.session->masters()[0].report->crest.status == session::MeasurementStatus::Ready
            && referenceCrest (lateReady) && lateReady.session->pendingMaster().master != 0
            && lateReady.session->revision() > beforeJoin && ! joinAllocated && ! joinOverran,
            "late source crest joins the retained master in bounded row batches and keeps the PCM");
    }
    Case compact;
    ok (run (compact, 48000, 48000, false, false),
        "two retained masters can wait for one late source crest");
    if (compact.session && compact.session->masters().size() == 1)
    {
        auto& s = *compact.session;
        const auto firstId = s.masters()[0].id;
        const auto released = s.releaseMaster (compact.token);
        compact.request.id = 3; compact.request.revision = s.revision();
        const auto second = s.apply (compact.request);
        for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) (void) s.step (73);
        ok (released == session::MasterTransferStatus::Ok
            && second.rejection == session::Rejection::None && s.masters().size() == 2
            && s.masters()[0].report->crest.status == session::MeasurementStatus::Pending
            && s.masters()[1].report->crest.status == session::MeasurementStatus::Pending,
            "both masters retain independent rows while the source crest is pending");
        bool published = false;
        for (unsigned i = 0; i < 200000 && ! published; ++i)
        {
            (void) s.step (1);
            for (const auto& event : s.events())
                if (event.kind == session::EventKind::Measurement
                    && event.payload.measurement.analyzer == session::Analyzer::Crest
                    && event.payload.measurement.status == session::MeasurementStatus::Ready)
                    published = true;
        }
        if (published) (void) s.step (1);
        const auto forgotten = s.apply (session::command::Forget { 4, firstId });
        for (unsigned i = 0; i < 200000 && s.masters().size() == 1
            && s.masters()[0].report->crest.status == session::MeasurementStatus::Pending; ++i)
            (void) s.step (1);
        ok (published && forgotten.rejection == session::Rejection::None
            && s.masters().size() == 1 && s.masters()[0].id == second.job
            && s.masters()[0].report->crest.status == session::MeasurementStatus::Ready
            && s.masters()[0].report->crest.sourceMask.size()
                == s.masters()[0].report->crest.blocks * 5u,
            "forgetting a partially joined older master preserves the later row and completes its mask");
    }
    Case up, down;
    ok (run (up, 44100, 48000, true) && referenceCrest (up) && directMeter (up),
        "SRC up meters delivered PCM and measures one source-rate check against whole renderer");
    ok (run (down, 48000, 44100, true) && referenceCrest (down) && directMeter (down),
        "SRC down meters delivered PCM and measures one source-rate check against whole renderer");
    Case repeat;
    ok (run (repeat, 44100, 48000, true) && up.session && ! up.session->masters().empty()
        && repeat.delivered == up.delivered
        && repeat.session->masters()[0].report->crest.rows.size() == up.session->masters()[0].report->crest.rows.size()
        && std::memcmp (repeat.session->masters()[0].report->crest.rows.data(),
            up.session->masters()[0].report->crest.rows.data(),
            up.session->masters()[0].report->crest.rows.size_bytes()) == 0,
        "a second session repeats the delivered PCM and source-rate crest exactly");
    Case missed;
    ok (run (missed, 48000, 48000, false, true, true),
        "a demanding target retains a checked ceiling-safe result after the pass budget");
    if (missed.session && ! missed.session->masters().empty())
    {
        const auto& kept = missed.session->masters()[0];
        const auto& report = *kept.report;
        const auto missFact = session::MasterReportText::miss (report);
        ok (kept.landing->status == session::LandingStatus::PassLimit && ! report.targetMet
            && report.peakSafe && report.firstHint && missFact
            && ! session::text::Text::text (*missFact, session::text::Lang::Ru).empty()
            && ! session::text::Text::text (*missFact, session::text::Lang::En).empty()
            && session::MasterReportText::hint (*report.firstHint),
            "ordinary loudness miss carries a number and a measured mix hint in ru and en");
    }
    std::vector<float> mono (48000u);
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = float (int (i % 197u) - 98) / 8192.0f;
    const float* monoPlanes[1] { mono.data() };
    auto shortMade = session::Session::create();
    auto& shortSession = *shortMade.session;
    const auto shortLoad = shortSession.apply (session::command::Load { 1,
        { monoPlanes, 1, mono.size(), 48000 }, { "mono.wav", 48000, true, 24 } });
    for (unsigned i = 0; i < 100000 && shortSession.state() != session::State::Measured2; ++i)
        (void) shortSession.step (16);
    session::command::Master monoReady { 2 };
    monoReady.ready.version = 1;
    monoReady.ready.topology.eq = monoReady.ready.topology.compressor = false;
    monoReady.ready.topology.clipper = monoReady.ready.topology.dither = false;
    monoReady.ready.topology.limiter = false;
    monoReady.source = shortSession.source().hash; monoReady.revision = shortSession.revision();
    const auto monoStart = shortSession.apply (monoReady);
    for (unsigned i = 0; i < 100000 && shortSession.job() != 0; ++i) (void) shortSession.step (16);
    ok (shortLoad.rejection == session::Rejection::None && monoStart.rejection == session::Rejection::None
        && shortSession.masters().size() == 1 && shortSession.masters()[0].report
        && shortSession.masters()[0].report->lraLu == std::nullopt
        && shortSession.masters()[0].report->lraReason == session::MeasurementReason::TooShort
        && shortSession.masters()[0].report->plrDb
        && shortSession.masters()[0].report->plrReason == session::MeasurementReason::None,
        "short mono master reports unavailable LRA by reason, without inventing zero");
    std::fill (mono.begin(), mono.end(), 0.0f);
    auto silentMade = session::Session::create();
    auto& silent = *silentMade.session;
    const auto silenceLoad = silent.apply (session::command::Load { 1,
        { monoPlanes, 1, mono.size(), 48000 }, { "silence.wav", 48000, true, 24 } });
    for (unsigned i = 0; i < 100000 && silent.state() == session::State::Loaded; ++i) (void) silent.step (16);
    monoReady.source = silent.source().hash; monoReady.revision = silent.revision();
    const auto silenceAnswer = silent.apply (monoReady);
    const auto silenceSnapshot = silent.snapshot();
    ok (silenceLoad.rejection == session::Rejection::None
        && silenceAnswer.rejection == session::Rejection::NotMeasured
        && silenceSnapshot.view().measurements[0].status == session::MeasurementStatus::Unavailable
        && silenceSnapshot.view().measurements[0].reason == session::MeasurementReason::NoSignal
        && silent.pendingMaster().master == 0,
        "silent source gives an explicit mandatory-measurement refusal and no file");
    std::uint64_t digest = 0xCBF29CE484222325ull;
    const auto add = [&] (std::uint64_t bits)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            digest = (digest ^ std::uint8_t (bits >> shift)) * 0x100000001B3ull;
    };
    int parityCase = 0;
    for (const Case* c : { &gain, &up, &down }) if (c->session && ! c->session->masters().empty())
    {
        const auto& r = *c->session->masters()[0].report;
        double pcmAbs = 0, crestAbs = 0;
        for (float sample : c->delivered) pcmAbs += std::fabs (double (sample));
        for (double row : r.crest.rows) crestAbs += std::fabs (row);
        std::printf ("master-report-parity %d %.12f %.12f %.12f %.12f %.12f\n",
            parityCase, *r.achievedLufs, *r.truePeakDbTp, *r.missLu, pcmAbs, crestAbs);
        add (std::bit_cast<std::uint64_t> (*r.achievedLufs));
        add (std::bit_cast<std::uint64_t> (*r.truePeakDbTp));
        add (std::bit_cast<std::uint64_t> (*r.missLu));
        for (float sample : c->delivered) add (std::bit_cast<std::uint32_t> (sample));
        for (double row : r.crest.rows) add (std::bit_cast<std::uint64_t> (row));
        for (double row : r.crest.sourceMask) add (std::bit_cast<std::uint64_t> (row));
        if (parityCase == 0)
            std::printf ("master-report-gain-digest=%016llx\n", static_cast<unsigned long long> (digest));
        ++parityCase;
    }
    ok (parityCase == 3, "all native/wasm parity cases completed");
    return felitronics::test::report();
}
