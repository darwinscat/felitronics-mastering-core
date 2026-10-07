// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "../../tests/DeclaredBudget.h"
#include "../../modules/session/src/JsonCodec.h"
#include "../../modules/session/src/QueryState.h"
#include "../../modules/session/src/MeasurementPlan.h"
#include <felitronics/session/Wire.h>
#include <felitronics/session/Config.h>
#include <felitronics/core/DetMath.h>
#include "fc_session_abi.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace declared = felitronics::declared;
struct felitronics::session::detail::Inspector
{
    static std::uint64_t frames (const Session& s) { return s.waveform_->index.framesSeen(); }
    static std::uint64_t cacheBytes (const Session& s) { return s.queryCache_ ? s.queryCache_->bytes() : 0; }
    static std::uint64_t key (const Session& s) { return s.measurementKey_; }
    static const MeasurementResult& measurement (const Session& s, Analyzer id) { return s.measurementResults_[std::size_t (id)]; }
    static void installMaster (Session& s, const Kept& kept)
    {
        s.masters_.reset (new Kept[1]);
        s.masters_[0] = kept; s.masterCount_ = s.masterRoom_ = 1;
    }
};
std::uint64_t digest = 0xCBF29CE484222325ull;
void hash (const QueryView& v)
{
    for (double value : v.values)
    {
        const auto bits = std::bit_cast<std::uint64_t> (value);
        for (unsigned i = 0; i < 8; ++i) digest = (digest ^ std::uint8_t (bits >> (i * 8u))) * 0x100000001B3ull;
    }
    digest = (digest ^ std::uint64_t (v.status)) * 0x100000001B3ull;
}
bool same (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
bool identical (std::span<const double> a, std::span<const double> b)
{ return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size_bytes()) == 0); }
std::string json (const MeasurementQuery& q)
{
    detail::Writer count; count.value (q); std::string text (std::size_t (count.size), '\0');
    detail::Writer write; write.output = text.data(); write.value (q); return text;
}
void finish (Session& s)
{
    unsigned calls = 0;
    while (s.step (16).state != StepState::Done && ++calls < 20000) {}
    ok (calls < 20000, "bounded jobs finish");
}
QueryResult ask (Session& s, const MeasurementQuery& q)
{
    QueryResult answer;
    const auto need = s.queryStorage (q);
    const auto spent = declared::spend ([&] { answer = s.query (q); });
    ok (declared::covers (need.bytes, spent), declared::describe (need.bytes, spent).c_str());
    hash (answer.view()); return answer;
}
void source (std::vector<float>& left, std::vector<float>& right)
{
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = float (std::clamp (0.9 * felitronics::core::det::sin (double (i) * 0.031), -0.4, 0.4));
        right[i] = float (std::clamp (0.9 * felitronics::core::det::sin (double (i) * 0.021 + 0.2), -0.4, 0.4));
    }
    left[0] = 1.0f; left[1] = 1.0f; right[right.size() - 1] = -1.0f; right[right.size() - 2] = -1.0f;
}
void nativeQueries()
{
    constexpr std::uint64_t frames = 192013;
    std::vector<float> left (frames), right (frames); source (left, right);
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create(); auto& s = *made.session;
    const command::Load load { 1, { planes, 2, frames, 48000 }, {} };
    const auto allocation = s.check (load);
    const auto spent = declared::spend ([&] { ok (s.apply (load).rejection == Rejection::None, "load"); });
    ok (declared::covers (allocation.bytes, spent), "load is declared including waveform controls");
    MeasurementQuery q; q.audioId = s.source().hash; q.toFrame = frames; q.columns = 17; q.requestId = 9007199254740993ull;
    ok (s.queryStorage (q).bytes == 0, "unbuilt waveform demand is allocation free");
    auto pending = ask (s, q); ok (pending.view().status == QueryStatus::Pending && pending.view().values.empty(), "overview explicitly pending before construction");
    // Readings now precede waveform construction. Advance to the first indexed block without assuming how many
    // measurement units came before it.
    for (unsigned calls = 0; detail::Inspector::frames (s) == 0 && calls < 20000; ++calls) (void) s.step (1);
    const auto prefix = detail::Inspector::frames (s);
    q.toFrame = prefix;
    auto prefixAnswer = ask (s, q);
    ok (prefixAnswer.view().status == QueryStatus::Ready && prefixAnswer.view().stored > 0
        && prefixAnswer.view().stored <= 68, "ready prefix is queryable between pump steps");
    const auto id = s.measurementJob();
    ok (s.apply (command::Cancel { 2, id }).rejection == Rejection::None, "cancel retains index");
    q.toFrame = frames;
    auto stopped = ask (s, q); ok (stopped.view().status == QueryStatus::Cancelled, "unfinished range names cancellation");
    ok (s.apply (command::ContinueMeasurement { 3 }).rejection == Rejection::None, "continue index");
    for (unsigned calls = 0; detail::Inspector::frames (s) == prefix && calls < 20000; ++calls) (void) s.step (1);
    ok (detail::Inspector::frames (s) == prefix + 1024u, "continue reads only unfinished frames");
    while (detail::Inspector::frames (s) != frames) (void) s.step (1);
    ok (s.state() == State::Loaded, "overview completes before later measurements");
    auto overview = ask (s, q);
    ok (overview.view().status == QueryStatus::Ready && overview.view().complete, "whole overview ready");
    const auto revision = s.revision(); const auto events = s.events().size();
    auto q2 = q; q2.requestId = std::numeric_limits<std::uint64_t>::max();
    auto repeated = ask (s, q2);
    ok (repeated.view().cacheHit && repeated.view().pcmFramesRead == 0 && repeated.view().request.requestId == q2.requestId, "request identity is echoed without duplicating demand");
    ok (identical (overview.view().values, repeated.view().values) && s.revision() == revision && s.events().size() == events, "query leaves jobs, revision and event batch intact");
    const auto key = detail::Inspector::key (s);
    ok (s.apply (command::SetTarget { 4, "cd" }).rejection == Rejection::None, "target change");
    auto target = ask (s, q);
    ok (target.view().cacheHit && detail::Inspector::key (s) == key && target.view().revision == s.revision(), "target reuses source query with current revision");
    auto zoom = q; zoom.fromFrame = 13; zoom.toFrame = 1027; zoom.columns = 11;
    auto z = ask (s, zoom); ok (! z.view().cacheHit && same (z.view().values[0], 13) && same (z.view().values[z.view().values.size() - 12], 1027), "zoom has exact requested edges");
    auto sample = q; sample.fromFrame = frames - 1; sample.columns = 2048;
    auto one = ask (s, sample);
    ok (one.view().stored == 4 && same (one.view().values[16], -1) && same (one.view().values[17], -1), "individual final sample preserves channel and polarity");
    ok (same (overview.view().values[0], 0) && overview.view().request.requestId == q.requestId, "evicted response stays owned");
    auto changed = zoom; ++changed.columns; ok (! ask (s, changed).view().cacheHit, "column count is in cache key");
    changed = zoom; ++changed.fromFrame; ok (! ask (s, changed).view().cacheHit, "first edge is in cache key");
    changed = zoom; --changed.toFrame; ok (! ask (s, changed).view().cacheHit, "last edge is in cache key");
    changed = zoom; changed.fromHz = 21; ok (! ask (s, changed).view().cacheHit, "explicit grid input is in cache key");
    ok (detail::Inspector::cacheBytes (s) <= sizeof (detail::QueryCache) + 2u * (kQueryValues * 8u + 128u), "two bounded cache entries");
    const auto invalid = [&] (MeasurementQuery bad, QueryStatus status)
    { const auto a = ask (s, bad); ok (a.view().status == status && a.view().values.empty(), "range/source/column refusal is explicit and allocation free"); };
    changed = q; changed.fromFrame = frames; invalid (changed, QueryStatus::Empty);
    changed = q; ++changed.toFrame; invalid (changed, QueryStatus::InvalidRange);
    changed = q; changed.fromFrame = frames + 1; invalid (changed, QueryStatus::InvalidRange);
    changed = q; changed.columns = 0; invalid (changed, QueryStatus::ColumnLimit);
    changed = q; changed.columns = kQueryColumns + 1; invalid (changed, QueryStatus::ColumnLimit);
    changed = q; ++changed.audioId; invalid (changed, QueryStatus::StaleSource);
    changed = q; changed.kind = QueryKind (255); invalid (changed, QueryStatus::Contract);
    const auto capacity = s.capabilities();
    auto miss = q; miss.columns = 31;
    const auto need = s.queryStorage (miss);
    ok (s.setCapacity ({ s.liveBytes() + double (need.bytes) - 1, capacity.largestFreeBlockBytes }) == Status::Ok, "capacity boundary");
    ok (ask (s, miss).view().status == QueryStatus::Memory, "query refuses before allocation");
    ok (s.setCapacity ({ capacity.heapCeilingBytes, capacity.largestFreeBlockBytes }) == Status::Ok, "restore capacity");
    finish (s);
    const auto snapshot = s.snapshot();
    for (const auto kind : { QueryKind::LowSpectrum, QueryKind::LowSide, QueryKind::Momentary, QueryKind::ShortTerm, QueryKind::Clipping, QueryKind::Stereo })
    {
        auto rq = q; rq.kind = kind; rq.columns = 19;
        auto result = ask (s, rq);
        ok (result.view().status == QueryStatus::Ready && ! result.view().values.empty(), "ready measurement rows are queryable");
        ok (result.view().pcmFramesRead == 0, "retained measurement query reads no PCM");
        ok (ask (s, rq).view().cacheHit, "all query kinds use bounded cache");
        if (kind == QueryKind::LowSpectrum || kind == QueryKind::LowSide)
        {
            const auto& low = snapshot.view().measurements[std::size_t (Analyzer::LowEnd)];
            for (const auto& a : low.arrays) if (a.name == "bands")
            {
                const auto j = a.stored / 2; const auto* row = a.values.data() + std::size_t (j * 16u);
                rq.fromHz = rq.toHz = row[1]; rq.columns = 1;
                auto band = ask (s, rq);
                const double expected = kind == QueryKind::LowSpectrum ? row[7] : row[5] / (row[4] + row[5]);
                ok (std::fabs (band.view().values[1] - expected) < 1e-12, "frequency grid reads retained spectrum and Side evidence");
                if (kind == QueryKind::LowSpectrum)
                {
                    auto whole = rq; whole.spectrum = SpectrumQuantity::Energy;
                    const auto e = ask (s, whole);
                    ok (e.view().request.spectrum == SpectrumQuantity::Energy && same (e.view().values[1], row[6]) && ! same (row[6], row[7]),
                        "asked for energy, LowSpectrum answers the band's whole energy, not its density");
                }
                rq.crossoverHz = 150;
                ok (! ask (s, rq).view().cacheHit, "120 and 150 Hz do not collide");
                // A band narrower than a Hann main lobe (owner, 06.10 and 07.10): its value as measured, no TooShort — the
                // unresolved flag stays on the bands array, and no decision reads these queries.
                const auto* first = a.values.data();
                auto lowest = q; lowest.kind = kind; lowest.fromHz = lowest.toHz = first[1]; lowest.columns = 1;
                const auto unresolved = ask (s, lowest);
                const double want = kind == QueryKind::LowSpectrum ? first[7] : first[5] / (first[4] + first[5]);
                const auto& got = unresolved.view().values;
                ok (first[15] < 0.5 && got.size() == 3 && same (got[2], double (MeasurementReason::None)) && std::isfinite (got[1])
                    && std::fabs (got[1] - want) <= 1e-9 * std::fabs (want),
                    std::string (kind == QueryKind::LowSpectrum ? "LowSpectrum" : "LowSide") + " at the unresolved first band ("
                    + std::to_string (first[1]) + " Hz): its measured value");
            }
        }
        if (kind == QueryKind::Clipping)
        {
            rq.columns = 1;
            const auto clipped = ask (s, rq);
            ok (clipped.view().stored == 1 && clipped.view().total > 1 && ! clipped.view().complete, "clipping truncation preserves counts");
        }
    }
    // THE DITHER'S FLOOR (owner, 07.10) through the session: the plan's dither at the target's rate (cd: 16 bits at
    // 44.1 kHz, TPDF, Weighted), per bin of the source's forensics analysis at 48 kHz — the rows detail::ditherFloor gives.
    {
        const auto& dither = snapshot.view().plan.dither;
        auto dq = q; dq.kind = QueryKind::DitherFloor; dq.fromHz = 20; dq.toHz = 22050; dq.columns = 64;
        const auto floor = ask (s, dq);
        QueryView v; v.request = dq;
        std::vector<double> want (64u * 3u);
        detail::ditherFloor (v, want.data(), dither, 48000, 44100);
        ok (dither.bits == 16 && dither.on && dither.shaping == DitherShaping::Weighted && floor.view().status == QueryStatus::Ready
            && floor.view().stored == 64 && floor.view().stride == 3 && identical (floor.view().values, want)
            && floor.view().pcmFramesRead == 0,
            "DitherFloor: the cd delivery's floor, at its rate, named at the source's — answered without a measurement");
        auto past = dq; past.fromHz = past.toHz = 22050.5; past.columns = 1;
        const auto above = ask (s, past);
        ok (above.view().status == QueryStatus::Ready && same (above.view().values[2], double (MeasurementReason::Unsupported)),
            "DitherFloor above the delivery's 22.05 kHz Nyquist, inside the source's: Unsupported");
        const auto refused = [&] (MeasurementQuery bad, QueryStatus status)
        { const auto a = ask (s, bad); return a.view().status == status && a.view().values.empty(); };
        auto zero = dq; zero.fromHz = 0; auto part = dq; part.fromFrame = 1; auto reversed = dq; reversed.fromHz = 30000; reversed.toHz = 20;
        auto none = dq; none.columns = 0;
        ok (refused (zero, QueryStatus::InvalidRange) && refused (part, QueryStatus::InvalidRange) && refused (reversed, QueryStatus::InvalidRange)
            && refused (none, QueryStatus::ColumnLimit), "DitherFloor refuses a grid from 0 Hz, part of the source, a reversed grid, no columns");
    }
    const auto summary = s.summary();
    ok (! summary.view().measurementRowsIncluded && snapshot.view().measurementRowsIncluded, "summary explicitly omits large rows");
    for (const auto& r : summary.view().measurements) ok (r.arrays.empty(), "summary retains scalar status without row copies");
    ok (s.summaryBytes() < s.snapshotBytes() && Wire::summaryBytes (s).rowBytes < Wire::snapshotBytes (s).rowBytes, "frequent summaries avoid whole-row transport");
    LandingTraceBucket limiterRows[] { { 0.0, 2.0, 1.0, 8, 0 }, { 0.0, 1.0, 0.5, 8, 0 } };
    LandingTraceBucket clipRows[] { { 0.0, 0.5, 0.25, 8, 0 }, { 0.0, 0.0, 0.0, 8, 0 } };
    LandingTrace layer; layer.toFrame = 4; layer.sampleRateHz = 48000; layer.columns = 2;
    layer.complete = layer.valid = true; layer.samples = 16; layer.rows = limiterRows;
    LandingSummary landing; landing.limiterTrace = layer; layer.rows = clipRows; landing.peakClipTrace = layer;
    Kept master; master.id = 42; master.recipe.source = s.source().hash; master.landing = landing;
    detail::Inspector::installMaster (s, master);
    MeasurementQuery gr; gr.kind = QueryKind::LimiterGr; gr.audioId = s.source().hash;
    gr.masterId = 42; gr.toFrame = 4; gr.columns = 1;
    auto limiter = ask (s, gr);
    ok (limiter.view().status == QueryStatus::Ready && limiter.view().stride == 7
        && limiter.view().total == 2 && limiter.view().stored == 1 && ! limiter.view().complete
        && same (limiter.view().values[0], 0.0) && same (limiter.view().values[1], 4.0)
        && same (limiter.view().values[3], 2.0) && same (limiter.view().values[4], 0.75),
        "master limiter query returns bounded delivered-grid columns and explicit reduction");
    gr.kind = QueryKind::PeakClipGr; gr.columns = 2;
    auto clipped = ask (s, gr);
    ok (clipped.view().status == QueryStatus::Ready && clipped.view().complete
        && same (clipped.view().values[3], 0.5) && same (clipped.view().values[10], 0.0),
        "K13 query uses the same grid with its own reductions");
    gr.fromFrame = 1; gr.toFrame = 3;
    auto zoomGr = ask (s, gr);
    ok (zoomGr.view().status == QueryStatus::Ready && zoomGr.view().total == 2
        && zoomGr.view().stored == 2 && zoomGr.view().complete
        && same (zoomGr.view().values[0], 0.0) && same (zoomGr.view().values[1], 2.0)
        && same (zoomGr.view().values[7], 2.0) && same (zoomGr.view().values[8], 4.0),
        "zoom selects intersecting K13 buckets and reports their retained bounds");
    gr.columns = 1;
    auto reducedZoom = ask (s, gr);
    ok (reducedZoom.view().status == QueryStatus::Ready && reducedZoom.view().total == 2
        && reducedZoom.view().stored == 1 && ! reducedZoom.view().complete
        && same (reducedZoom.view().values[0], 0.0) && same (reducedZoom.view().values[1], 4.0)
        && same (reducedZoom.view().values[4], 0.125),
        "zoom groups selected buckets with their original bounds and weighted mean");
    gr.fromFrame = 0; gr.toFrame = 2;
    auto firstOnly = ask (s, gr);
    ok (firstOnly.view().status == QueryStatus::Ready && firstOnly.view().total == 1
        && firstOnly.view().stored == 1 && firstOnly.view().complete
        && same (firstOnly.view().values[0], 0.0) && same (firstOnly.view().values[1], 2.0),
        "one-sided zoom returns only the intersecting retained bucket");
    gr.fromFrame = 2; gr.toFrame = 2;
    ok (ask (s, gr).view().status == QueryStatus::Empty, "empty master zoom is explicit");
    gr.fromFrame = 3; gr.toFrame = 5;
    ok (ask (s, gr).view().status == QueryStatus::InvalidRange, "master zoom beyond the delivered programme refuses");
    gr.fromFrame = 0; gr.toFrame = 4;
    gr.columns = 2;
    auto staleGr = gr; staleGr.audioId ^= 1u;
    ok (ask (s, staleGr).view().status == QueryStatus::StaleSource,
        "master trace refuses a mismatched source identity");
    master.landing->peakClipTrace->complete = false;
    detail::Inspector::installMaster (s, master);
    auto unfinished = ask (s, gr);
    ok (unfinished.view().status == QueryStatus::Pending && unfinished.view().values.empty()
        && ! unfinished.view().complete, "unfinished K13 trace reports pending without rows");
    master.landing.reset(); detail::Inspector::installMaster (s, master);
    ok (ask (s, gr).view().status == QueryStatus::Unavailable && same (limiter.view().values[3], 2.0),
        "cancelled or replaced master exposes no stale trace; previous response remains owned");
    left[8] *= 0.75f;
    ok (s.apply (load).rejection == Rejection::None, "replacement source");
    ok (ask (s, q).view().status == QueryStatus::StaleSource && same (overview.view().values[0], 0), "old response survives source replacement and new request rejects old audio ID");
}
void abiQueries()
{
    constexpr std::uint64_t frames = 48013;
    std::vector<float> left (frames), right (frames); source (left, right);
    const float* planes[] { left.data(), right.data() };
    auto native = Session::create();
    ok (native.session->apply (command::Load { 1, { planes, 2, frames, 48000 }, {} }).rejection == Rejection::None, "native fixture load");
    fc_session_capabilities caps { sizeof (caps), 1073741824, 96000, 255, 1073741824, 0 };
    const auto version = config::Config::versions().all; fc_session handle = 0;
    ok (fc_session_create (&caps, std::uint32_t (version), std::uint32_t (version >> 32u), &handle) == FC_SESSION_OK, "C fixture create");
    std::vector<char> answer (kAnswerBytes); std::uint32_t written = 0;
    constexpr char meta[] = R"({"name":"","fileRate":0,"bitDepth":0,"rateKnown":false})";
    ok (fc_session_load (handle, 1, 0, planes, 2, std::uint32_t (frames), 48000, meta, sizeof (meta) - 1, answer.data(), std::uint32_t (answer.size()), &written) == FC_SESSION_OK, "C fixture load");
    ok (std::string_view (answer.data(), written).find ("accepted") != std::string_view::npos, "C load accepted domain answer");
    finish (*native.session);
    std::uint32_t state = FC_SESSION_MORE;
    for (unsigned i = 0; i < 20000 && state == FC_SESSION_MORE; ++i) ok (fc_session_step (handle, 16, &state) == FC_SESSION_OK, "C fixture step");
    ok (state == FC_SESSION_DONE, "C fixture completes");
    for (unsigned kind = 0; kind <= unsigned (QueryKind::Stereo); ++kind)
    {
        MeasurementQuery q; q.kind = QueryKind (kind); q.audioId = native.session->source().hash; q.toFrame = frames; q.columns = 7; q.requestId = 9007199254740993ull + kind;
        const auto request = json (q);
        for (unsigned repetition = 0; repetition < 2; ++repetition)
        {
            fc_session_sizes size { sizeof (size), 0, 0 }, actual { sizeof (actual), 777, 888 };
            fc_session_storage demand { sizeof (demand), 0, 0, 0, 0, 0 };
            ok (fc_session_query_bytes (handle, request.data(), std::uint32_t (request.size()), &demand) == FC_SESSION_OK && demand.rejection == 0, "C query demand");
            ok (fc_session_query_size (handle, request.data(), std::uint32_t (request.size()), &size) == FC_SESSION_OK, "C query buffer bounds");
            std::vector<char> text (size.jsonBytes, '!'); std::vector<double> rows (size.rowBytes / 8u, -777);
            fc_session_status shortStatus {};
            const auto rejected = declared::spend ([&]
            { shortStatus = fc_session_query_copy (handle, request.data(), std::uint32_t (request.size()), text.data(), 1, rows.data(), size.rowBytes, &actual); });
            ok (shortStatus == FC_SESSION_ERR_TOO_SMALL, "short query buffer");
            ok (rejected.bytes == 0 && text[0] == '!' && (rows.empty() || same (rows[0], -777)) && actual.jsonBytes == 777, "entry refusal allocates and writes nothing");
            const auto spent = declared::spend ([&]
            { ok (fc_session_query_copy (handle, request.data(), std::uint32_t (request.size()), text.data(), size.jsonBytes, rows.data(), size.rowBytes, &actual) == FC_SESSION_OK, "C query copy"); });
            ok (declared::covers (std::uint64_t (demand.bytes), spent), "C query declaration covers counters");
            const auto value = native.session->query (q);
            const auto exact = Wire::queryBytes (value.view());
            std::vector<char> expected (exact.jsonBytes); std::vector<double> expectedRows (exact.rowBytes / 8u);
            ok (Wire::query (value.view(), expected, expectedRows) == CodecStatus::Ok, "named response codec");
            ok (actual.jsonBytes == expected.size() && actual.rowBytes == expectedRows.size() * 8u
                && std::equal (expected.begin(), expected.end(), text.begin()) && identical ({ rows.data(), actual.rowBytes / 8u }, expectedRows), "native/C response metadata and arrays agree exactly");
            hash (value.view());
            if (repetition == 0)
            {
                std::printf ("query-fixture %s\n", std::string (text.data(), actual.jsonBytes).c_str());
                std::printf ("query-rows ");
                const auto* bytes = reinterpret_cast<const unsigned char*> (rows.data());
                for (std::uint32_t i = 0; i < actual.rowBytes; ++i) std::printf ("%02x", unsigned (bytes[i]));
                std::printf ("\n");
            }
        }
    }
    MeasurementQuery absentMaster; absentMaster.kind = QueryKind::PeakClipGr;
    absentMaster.audioId = native.session->source().hash;
    absentMaster.masterId = 42; absentMaster.toFrame = 4; absentMaster.columns = 2;
    const auto masterRequest = json (absentMaster);
    fc_session_sizes masterSize { sizeof (masterSize), 0, 0 }, masterWritten { sizeof (masterWritten), 0, 0 };
    ok (fc_session_query_size (handle, masterRequest.data(), std::uint32_t (masterRequest.size()), &masterSize) == FC_SESSION_OK
        && masterSize.rowBytes == 0, "C master trace query declares zero rows when no master is retained");
    std::vector<char> masterAnswer (masterSize.jsonBytes);
    ok (fc_session_query_copy (handle, masterRequest.data(), std::uint32_t (masterRequest.size()),
        masterAnswer.data(), masterSize.jsonBytes, nullptr, 0, &masterWritten) == FC_SESSION_OK
        && masterWritten.rowBytes == 0
        && std::string_view (masterAnswer.data(), masterWritten.jsonBytes).find ("\"status\":2") != std::string_view::npos,
        "C master trace query returns the explicit unavailable status");
    fc_session_sizes size { sizeof (size), 0, 0 };
    ok (fc_session_summary_size (handle, &size) == FC_SESSION_OK, "C summary bounds");
    std::vector<char> text (size.jsonBytes); std::vector<double> rows (size.rowBytes / 8u);
    ok (fc_session_summary_copy (handle, text.data(), size.jsonBytes, rows.data(), size.rowBytes) == FC_SESSION_OK, "C summary copy");
    ok (std::string_view (text.data(), text.size()).find ("\"measurementRowsIncluded\":false") != std::string_view::npos, "summary advertises omitted rows");
    MeasurementQuery outside; outside.audioId = native.session->source().hash; outside.toFrame = frames + 1u;
    const auto outOfRange = json (outside);
    fc_session_storage semanticDemand { sizeof (semanticDemand), 0, 0, 0, 0, 0 };
    fc_session_sizes semanticSize { sizeof (semanticSize), 0, 0 }, semanticWritten { sizeof (semanticWritten), 0, 0 };
    ok (fc_session_query_bytes (handle, outOfRange.data(), std::uint32_t (outOfRange.size()), &semanticDemand) == FC_SESSION_OK
        && semanticDemand.rejection == 0 && same (semanticDemand.bytes, 0.0), "invalid range has an allocation-free C preflight");
    ok (fc_session_query_size (handle, outOfRange.data(), std::uint32_t (outOfRange.size()), &semanticSize) == FC_SESSION_OK
        && semanticSize.rowBytes == 0, "invalid range needs no row buffer");
    std::vector<char> semanticJson (semanticSize.jsonBytes);
    ok (fc_session_query_copy (handle, outOfRange.data(), std::uint32_t (outOfRange.size()), semanticJson.data(), semanticSize.jsonBytes,
        nullptr, 0, &semanticWritten) == FC_SESSION_OK
        && std::string_view (semanticJson.data(), semanticWritten.jsonBytes).find ("\"status\":4") != std::string_view::npos,
        "invalid range returns its explicit status through the C boundary");
    const char invalid[] = "{}";
    ok (fc_session_query_size (handle, invalid, 2, &size) == FC_SESSION_ERR_CONTRACT, "malformed query JSON");
    ok (fc_session_query_copy (handle, invalid, 2, text.data(), size.jsonBytes, reinterpret_cast<double*> (text.data()), 8, &size) == FC_SESSION_ERR_OVERLAP, "overlapping outputs refused before parsing");
    ok (fc_session_query_size (handle, invalid, 2, nullptr) == FC_SESSION_ERR_NULL, "null output before handle/input");
    ok (fc_session_destroy (handle) == FC_SESSION_OK, "C fixture destroy");
}
struct CQuery
{
    std::string metadata;
    std::vector<double> rows;
};
CQuery queryC (fc_session handle, const MeasurementQuery& q)
{
    const auto request = json (q);
    fc_session_storage demand { sizeof (demand), 0, 0, 0, 0, 0 };
    fc_session_sizes size { sizeof (size), 0, 0 }, actual { sizeof (actual), 0, 0 };
    ok (fc_session_query_bytes (handle, request.data(), std::uint32_t (request.size()), &demand) == FC_SESSION_OK, "regression C demand");
    ok (fc_session_query_size (handle, request.data(), std::uint32_t (request.size()), &size) == FC_SESSION_OK, "regression C size");
    std::vector<char> text (size.jsonBytes);
    CQuery out; out.rows.resize (size.rowBytes / sizeof (double));
    const auto spent = declared::spend ([&]
    { ok (fc_session_query_copy (handle, request.data(), std::uint32_t (request.size()), text.data(), size.jsonBytes,
        out.rows.data(), size.rowBytes, &actual) == FC_SESSION_OK, "regression C copy"); });
    ok (declared::covers (std::uint64_t (demand.bytes), spent), "regression C allocation declared");
    out.metadata.assign (text.data(), actual.jsonBytes);
    out.rows.resize (actual.rowBytes / sizeof (double));
    return out;
}
void reviewRegressions()
{
    constexpr std::uint64_t frames = 48000;
    std::vector<float> left (frames), right (frames); source (left, right);
    left[3] = right[3] = 0.0f;
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create(); auto& native = *made.session;
    ok (native.apply (command::Load { 1, { planes, 2, frames, 48000 }, {} }).rejection == Rejection::None, "regression native load");
    fc_session_capabilities caps { sizeof (caps), 1073741824, 96000, 255, 1073741824, 0 };
    const auto version = config::Config::versions().all; fc_session handle = 0;
    ok (fc_session_create (&caps, std::uint32_t (version), std::uint32_t (version >> 32u), &handle) == FC_SESSION_OK, "regression C create");
    std::vector<char> answer (kAnswerBytes); std::uint32_t written = 0;
    constexpr char meta[] = R"({"name":"","fileRate":0,"bitDepth":0,"rateKnown":false})";
    ok (fc_session_load (handle, 1, 0, planes, 2, std::uint32_t (frames), 48000, meta, sizeof (meta) - 1,
        answer.data(), std::uint32_t (answer.size()), &written) == FC_SESSION_OK, "regression C load");
    MeasurementQuery clips; clips.kind = QueryKind::Clipping; clips.audioId = native.source().hash;
    clips.toFrame = frames; clips.columns = 8;
    std::uint32_t state = FC_SESSION_MORE;
    bool drained = false;
    for (unsigned i = 0; i < 200 && ! drained; ++i)
    {
        const auto& result = detail::Inspector::measurement (native, Analyzer::Clipping);
        if (! result.arrays.empty() && result.arrays[0].total >= 4 && result.arrays[0].stored == 0)
        {
            const auto priorFrame = result.arrays[0].grid.framesRead;
            const auto before = ask (native, clips); const auto beforeC = queryC (handle, clips);
            ok (before.view().stored == 0 && beforeC.rows.empty(), "clip cache primed before drain");
            (void) native.step (1);
            ok (fc_session_step (handle, 1, &state) == FC_SESSION_OK, "regression C drain step");
            const auto& afterResult = detail::Inspector::measurement (native, Analyzer::Clipping);
            drained = afterResult.arrays[0].grid.framesRead == priorFrame && afterResult.arrays[0].stored >= 4;
            const auto after = ask (native, clips); const auto afterC = queryC (handle, clips);
            ok (drained && after.view().stored >= 4 && ! after.view().cacheHit
                && afterC.rows.size() >= 24 && afterC.metadata.find ("\"cacheHit\":false") != std::string::npos,
                "drain-only publication invalidates native and C clip cache");
            break;
        }
        (void) native.step (1);
        ok (fc_session_step (handle, 1, &state) == FC_SESSION_OK, "regression C stream step");
    }
    ok (drained, "four clip runs drain without advancing frames");
    finish (native);
    for (unsigned i = 0; i < 20000 && state == FC_SESSION_MORE; ++i)
        ok (fc_session_step (handle, 16, &state) == FC_SESSION_OK, "regression C completion step");
    ok (state == FC_SESSION_DONE, "regression C completion");
    MeasurementQuery stereo; stereo.kind = QueryKind::Stereo; stereo.audioId = native.source().hash;
    stereo.fromFrame = 3; stereo.toFrame = 4; stereo.columns = 7;
    const auto one = ask (native, stereo); const auto oneC = queryC (handle, stereo);
    ok (one.view().stored == 1 && one.view().values.size() == 6 && same (one.view().values[0], 0)
        && same (one.view().values[1], 40) && one.view().values[4] > 0
        && identical (one.view().values, oneC.rows), "stereo row reports its retained source interval");
    stereo.toFrame = 5;
    const auto adjacent = ask (native, stereo); const auto adjacentC = queryC (handle, stereo);
    ok (adjacent.view().total == 1 && adjacent.view().stored == 1 && adjacent.view().complete
        && identical (adjacent.view().values, adjacentC.rows), "two samples in one retained stereo column are not duplicated");
    for (const auto kind : { QueryKind::Momentary, QueryKind::ShortTerm })
    {
        MeasurementQuery loud; loud.kind = kind; loud.audioId = native.source().hash;
        loud.toFrame = frames; loud.columns = 10;
        const auto whole = ask (native, loud); const auto wholeC = queryC (handle, loud);
        ok (whole.view().total == 10 && whole.view().stored == 10 && whole.view().complete
            && same (whole.view().values[27], double (frames)) && identical (whole.view().values, wholeC.rows),
            "whole-source loudness includes its terminal window-end reading");
        loud.fromFrame = frames - 1;
        const auto tail = ask (native, loud); const auto tailC = queryC (handle, loud);
        ok (tail.view().stored == 1 && tail.view().complete && same (tail.view().values[0], double (frames))
            && identical (tail.view().values, tailC.rows), "last frame can query terminal loudness reading");
    }
    ok (fc_session_destroy (handle) == FC_SESSION_OK, "regression C destroy");
}
// THE DITHER'S FLOOR (owner, 07.10), the rows alone: the quantiser's white noise — TPDF ±1 LSB with its rounding, LSB²/4;
// rounding alone, LSB²/12 — through the dither's NTF, the power one bin of a 16384-point Hann analysis reads.
void ditherFloorRows()
{
    felitronics::test::group ("DitherFloor: the delivery's noise floor per bin, in the forensics meanPower convention");
    const auto rows = [] (const DitherFinding& d, double from, double to, std::uint32_t columns, std::uint32_t source = 48000,
                          std::uint32_t delivery = 48000)
    {
        QueryView v; v.request.kind = QueryKind::DitherFloor; v.request.fromHz = from; v.request.toHz = to; v.request.columns = columns;
        std::vector<double> out (std::size_t (columns) * 3u);
        detail::ditherFloor (v, out.data(), d, source, delivery);
        return out;
    };
    const auto db = [] (double power) { return 10.0 * std::log10 (power); };
    DitherFinding tpdf; tpdf.bits = 16; tpdf.on = true; tpdf.shaping = DitherShaping::None;
    const auto flat = rows (tpdf, 20.0, 20000.0, 5);
    bool even = true;
    for (std::size_t i = 0; i < 5; ++i) even = even && same (flat[3 * i + 2], double (MeasurementReason::None)) && same (flat[3 * i + 1], flat[1]);
    ok (even && std::fabs (flat[1] - db (1.0 / 4294967296.0 / 16384.0)) < 1e-9 && std::fabs (flat[1] + 138.47) < 0.005
        && same (flat[0], 20.0) && std::fabs (flat[12] - 20000.0) < 1e-9,
        "16-bit TPDF at 48 kHz, no shaping: flat at −138.47 dB per bin on a log grid 20 Hz … 20 kHz (" + std::to_string (flat[1]) + ")");
    DitherFinding weighted = tpdf; weighted.shaping = DitherShaping::Weighted;
    const auto nyquist = rows (weighted, 24000.0, 24000.0, 1), k1 = rows (weighted, 1000.0, 1000.0, 1);
    ok (std::fabs (nyquist[1] - (flat[1] + db (9.0))) < 1e-9 && std::fabs (nyquist[1] + 128.9) < 0.05 && std::fabs (k1[1] + 162.0) < 0.05,
        "16-bit Weighted: −128.9 dB at Nyquist (|NTF|² = 9), −162.0 at 1 kHz (" + std::to_string (nyquist[1]) + ", "
        + std::to_string (k1[1]) + ")");
    DitherFinding rounding; rounding.bits = 24; rounding.on = false; rounding.shaping = DitherShaping::Weighted;
    const auto r24 = rows (rounding, 1000.0, 1000.0, 1);
    ok (std::fabs (r24[1] - db (1.0 / 70368744177664.0 / 12.0 / 16384.0)) < 1e-9 && std::fabs (r24[1] + 191.4) < 0.05,
        "24-bit rounding, no dither and so no shaping: −191.4 dB per bin (" + std::to_string (r24[1]) + ")");
    const auto rated = rows (tpdf, 1000.0, 1000.0, 1, 44100, 48000);
    ok (std::fabs (rated[1] - (flat[1] + db (44100.0 / 48000.0))) < 1e-9, "named at the source's rate: a 44.1 kHz source's bin, a 48 kHz delivery");
    DitherFinding wide; wide.bits = 32;
    const auto above = rows (weighted, 24000.5, 24000.5, 1), none = rows (wide, 1000.0, 1000.0, 1);
    ok (same (above[2], double (MeasurementReason::Unsupported)) && std::isnan (above[1]) && same (none[2], double (MeasurementReason::NoSignal))
        && std::isnan (none[1]), "above the delivery's Nyquist: Unsupported; 32 bits, no quantiser: NoSignal");
    DitherFinding psycho = tpdf; psycho.shaping = DitherShaping::Psychoacoustic;
    const auto p = rows (psycho, 3000.0, 3000.0, 1);
    ok (same (p[2], double (MeasurementReason::None)) && p[1] < flat[1], "Psychoacoustic: below the flat floor where the ear is keenest, 3 kHz");
}
int main()
{
    ditherFloorRows(); nativeQueries(); abiQueries(); reviewRegressions();
    std::printf ("measurement-query-digest=%016llx\n", static_cast<unsigned long long> (digest));
    return felitronics::test::report();
}
