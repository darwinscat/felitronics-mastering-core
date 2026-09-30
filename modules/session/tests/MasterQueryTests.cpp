// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// WHAT A SHELL READS OF A MASTER WITHOUT DECIDING ANYTHING. The landing's miss and its hints published as facts keyed to
// the master; the master's own loudness curves and its waveform in the source's four axes, each held to the SAME
// instrument run over the delivered audio as a source; the low-end spectrum as density (as it was) or as energy; and the
// lean summary — every master's scalars, pass log and sections, its heavy rows left to a MasterReport query that gives
// one master whole, to the bit of the full snapshot — with its bytes measured and its memory declared.

#include "DeclaredBudget.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <felitronics/session/Text.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;

namespace
{
constexpr double kPi = 3.141592653589793;
bool same (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
bool close (double a, double b) { return same (a, b) || std::fabs (a - b) <= 1e-9 * std::max (1.0, std::fabs (b)); }
bool sameRows (std::span<const double> a, std::span<const double> b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (! same (a[i], b[i])) return false;
    return true;
}

// A stereo mix with a low end, a side and a top, louder in its second half.
struct Audio
{
    unsigned rate, frames;
    std::vector<float> left, right;
    const float* planes[2] { nullptr, nullptr };
    Audio (unsigned seconds, unsigned sampleRate = 48000) : rate (sampleRate), frames (sampleRate * seconds), left (frames), right (frames)
    {
        namespace det = felitronics::core::det;
        for (unsigned i = 0; i < frames; ++i)
        {
            const double t = double (i) / rate, swell = i < frames / 2 ? 0.5 : 1.0;
            const double beat = double (i % (rate / 2)) / rate;
            const double low = 0.25 * det::exp2 (-beat / 0.05) * det::sin (2 * kPi * 70.0 * beat);
            const double mid = 0.08 * det::sin (2 * kPi * 440.0 * t), side = 0.05 * det::sin (2 * kPi * 1300.0 * t);
            const double top = 0.02 * det::sin (2 * kPi * 6100.0 * t);
            left[i] = float (swell * (low + mid + side + top));
            right[i] = float (swell * (low + mid - side + 0.5 * top));
        }
        planes[0] = left.data(); planes[1] = right.data();
    }
    Audio (const Audio&) = delete;
};

std::unique_ptr<Session> loaded (const float* const* planes, unsigned channels, std::uint64_t frames, unsigned rate, bool lean = false,
                                 const char* target = "allStreaming")
{
    Capabilities caps; caps.leanSummary = lean;
    auto s = Session::create (caps, config::Config::versions().all).session;
    (void) s->apply (command::SetTarget { 1, target });
    ok (s->apply (command::Load { 2, { planes, channels, frames, rate }, { "mix.wav", rate, true, 24 } }).rejection == Rejection::None,
        "PRECONDITION: the source loads");
    for (unsigned i = 0; i < 4000000 && (s->measurementJob() || s->needlesJob()); ++i) (void) s->step (16);
    ok (s->state() == State::Measured2, "PRECONDITION: measured");
    return s;
}

// A master of the session's source through a limiter and nothing else; its delivered audio copied out and released.
struct Mastered { MasterId id = 0; std::vector<float> audio; MasterAudioShape shape {}; std::vector<Notification> facts; Checked declared {}; budget::Spent spent {}; };
Mastered master (Session& s, CommandId id)
{
    command::Master request { id };
    request.ready.version = 1;
    request.ready.topology.eq = request.ready.topology.compressor = request.ready.topology.clipper = request.ready.topology.dither = false;
    request.ready.topology.limiter = true;
    request.source = s.source().hash; request.revision = s.revision();
    Mastered out;
    out.declared = s.check (request);
    Answer started;
    out.spent = budget::spend ([&] { started = s.apply (request); });
    ok (started.rejection == Rejection::None, "PRECONDITION: the master is taken");
    for (unsigned i = 0; i < 4000000 && s.job() != 0; ++i)
    {
        (void) s.step (16);
        for (const auto& e : s.events()) if (e.kind == EventKind::Fact) out.facts.push_back (e);
    }
    const auto token = s.pendingMaster();
    out.id = token.master;
    out.shape = s.masterAudioShape (token);
    out.audio.resize (std::size_t (out.shape.frames * out.shape.channels));
    ok (token.master != 0 && s.copyMaster (token, out.audio) == MasterTransferStatus::Ok
        && s.releaseMaster (token) == MasterTransferStatus::Ok, "PRECONDITION: mastered, its audio copied and released");
    return out;
}
const Kept* kept (const SnapshotView& v, MasterId id)
{
    for (const auto& m : v.masters) if (m.id == id) return &m;
    return nullptr;
}
MeasurementQuery ask (QueryKind kind, const Session& s, std::uint64_t from, std::uint64_t to, std::uint32_t columns, MasterId id = 0)
{
    MeasurementQuery q; q.kind = kind; q.audioId = s.source().hash; q.fromFrame = from; q.toFrame = to; q.columns = columns; q.masterId = id;
    return q;
}
// A query through the declared-memory gate: what it asked the heap for is inside what it declared.
QueryResult answered (Session& s, const MeasurementQuery& q, bool& covered)
{
    const auto need = s.queryStorage (q);
    QueryResult out;
    const auto spent = budget::spend ([&] { out = s.query (q); });
    covered = covered && budget::covers (need.bytes, spent) && (spent.requests == 0 || need.largestBlockBytes != 0);
    return out;
}

//==============================================================================

void theLandingsFacts()
{
    felitronics::test::group ("(c) the landing's miss and its hints are facts the core publishes, keyed to the master");
    const Audio audio (5);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate); auto& s = *sp;
    ok (s.apply (command::EditTarget { 3, { -5.0, -6.0 } }).rejection == Rejection::None, "PRECONDITION: a target this mix cannot reach: −5 LUFS under −6 dBTP");
    const auto made = master (s, 4);
    const auto view = s.snapshot();
    const auto& report = *kept (view.view(), made.id)->report;
    const auto miss = MasterReportText::miss (report);
    ok (miss && report.firstHint, "PRECONDITION: the master missed, with a reason");
    const auto published = [&] (const text::Fact& fact)
    {
        for (const auto& e : made.facts)
        {
            const auto& f = e.payload.fact.view();
            if (e.jobId != made.id || f.id != fact.id || f.argCount != fact.argCount) continue;
            bool equal = true;
            for (std::size_t i = 0; i < f.argCount; ++i) equal = equal && same (f.args[i].number, fact.args[i].number) && f.args[i].unit == fact.args[i].unit;
            if (equal) return true;
        }
        return false;
    };
    const auto said = miss ? text::Text::text (*miss, text::Lang::Ru) + " / " + text::Text::text (*miss, text::Lang::En) : std::string {};
    ok (miss && published (*miss) && (miss->id == text::FactId::MasterLandingMiss || miss->id == text::FactId::MasterLandingAbove)
        && said.find ('{') == std::string::npos, "the miss is published with the master's id, ru and en: " + said);
    bool hints = true; std::string hinted;
    for (const auto* hint : { &report.firstHint, &report.secondHint })
        if (*hint)
        {
            const auto fact = MasterReportText::hint (**hint);
            hints = hints && fact && published (*fact);
            if (fact) hinted += text::Text::text (*fact, text::Lang::Ru) + " ";
        }
    ok (hints && ! hinted.empty(), "and each hint: " + hinted);
    std::size_t before = 0, first = made.facts.size();
    for (std::size_t i = 0; i < made.facts.size(); ++i)
    {
        const auto id = made.facts[i].payload.fact.view().id;
        if (miss && id == miss->id) first = std::min (first, i);
        if (id == text::FactId::MasterCostShape && first == made.facts.size()) ++before;
    }
    ok (before == 0, "the landing's line comes ahead of the cost's");

    auto met = loaded (audio.planes, 2, audio.frames, audio.rate);
    const auto landed = master (*met, 3);
    bool silent = true;
    for (const auto& e : landed.facts)
    {
        const auto id = std::uint16_t (e.payload.fact.view().id);
        silent = silent && id != 11 && id != 23 && ! (id >= 12 && id <= 17);
    }
    ok (kept (met->snapshot().view(), landed.id)->report->targetMet && silent, "a master that lands says neither");
}

void theSpectrumsQuantity()
{
    felitronics::test::group ("(f) LowSpectrum answers density, as it did, or the band's energy when asked");
    const Audio audio (6);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate); auto& s = *sp;
    const auto snapshot = s.snapshot();
    const MeasurementArray* bands = nullptr;
    for (const auto& a : snapshot.view().measurements[std::size_t (Analyzer::LowEnd)].arrays) if (a.name == "bands") bands = &a;
    ok (bands && bands->stored > 4 && bands->columns == 16, "PRECONDITION: the low-end bands are retained");
    bool covered = true, exact = true, apart = false;
    for (std::uint64_t j = 0; bands && j < bands->stored; ++j)
    {
        const auto* row = bands->values.data() + std::size_t (16u * j);
        auto q = ask (QueryKind::LowSpectrum, s, 0, audio.frames, 1); q.fromHz = q.toHz = row[1];
        const auto density = answered (s, q, covered);
        q.spectrum = SpectrumQuantity::Energy;
        const auto energy = answered (s, q, covered);
        exact = exact && density.view().status == QueryStatus::Ready && energy.view().status == QueryStatus::Ready
            && density.view().values.size() == 3 && energy.view().values.size() == 3
            && same (density.view().values[0], row[1]) && same (energy.view().values[0], row[1]);
        // A centre is the end of one stretch between bands and the start of the next: the value is the band's own, to
        // a rounding of the interpolation; where a neighbour is not resolved the point has its reason and no value.
        exact = exact && same (density.view().values[2], energy.view().values[2]);
        if (same (density.view().values[2], double (MeasurementReason::None)))
        {
            exact = exact && close (density.view().values[1], row[7]) && close (energy.view().values[1], row[6]);
            if (row[6] > 0) { exact = exact && close (row[6] / row[7], row[2]); apart = apart || row[2] > 1.5; }
        }
    }
    ok (exact && apart, "at every band's centre: density is the band's column 7, energy its column 6 — the density times the band's width in Hz");
    auto whole = ask (QueryKind::LowSpectrum, s, 0, audio.frames, 64);
    const auto byDefault = answered (s, whole, covered);
    auto named = whole; named.spectrum = SpectrumQuantity::Density;
    auto asEnergy = whole; asEnergy.spectrum = SpectrumQuantity::Energy;
    const auto density = answered (s, named, covered), energy = answered (s, asEnergy, covered), again = answered (s, whole, covered);
    ok (sameRows (byDefault.view().values, density.view().values) && sameRows (again.view().values, density.view().values)
        && ! sameRows (energy.view().values, density.view().values) && energy.view().stored == 64,
        "a request without the field is density; energy is another curve, and the cache keeps the two apart");
    // The tilt a page would see between the two: energy over density is 10·log10(band width in Hz), growing with the band.
    double first = 0, last = 0;
    for (std::uint32_t i = 0; i < 64; ++i)
    {
        const double d = density.view().values[3u * i + 1u], e = energy.view().values[3u * i + 1u];
        if (d > 0 && e > 0) { const double db = 10 * std::log10 (e / d); if (first <= 0) first = db; last = db; }
    }
    std::printf ("    energy over density across the curve: %.2f dB at its low end, %.2f dB at its top\n", first, last);
    ok (covered, "both inside their declared memory");
    auto odd = whole; odd.spectrum = SpectrumQuantity (2);
    ok (s.queryStorage (odd).status == QueryStatus::Contract && s.query (odd).view().status == QueryStatus::Contract, "a quantity that is neither is refused");
    // The wire: a version-1 request has no such field and means density.
    const std::string source = std::to_string (s.source().hash), frames = std::to_string (audio.frames);
    const std::string head = "{\"kind\":1,\"audioId\":\"" + source + "\",\"fromFrame\":\"0\",\"toFrame\":\"" + frames
        + "\",\"columns\":8,\"requestId\":\"5\",\"crossoverHz\":120,\"fromHz\":20,\"toHz\":250";
    MeasurementQuery plain, asked;
    ok (Wire::queryRequest (head + "}", plain) == CodecStatus::Ok && plain.spectrum == SpectrumQuantity::Density
        && Wire::queryRequest (head + ",\"spectrum\":1}", asked) == CodecStatus::Ok && asked.spectrum == SpectrumQuantity::Energy,
        "on the wire a request without \"spectrum\" is density, \"spectrum\":1 energy");
}

void theMastersLoudness()
{
    felitronics::test::group ("(d) a master's momentary and short-term curves: the delivered audio's own, as a source's are");
    const Audio audio (8);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate); auto& s = *sp;
    const auto made = master (s, 3);
    // The delivered audio as a source of its own: the same instrument, the same rows.
    const float* planes[] { made.audio.data(), made.audio.data() + made.shape.frames };
    auto op = loaded (planes, made.shape.channels, made.shape.frames, made.shape.sampleRate); auto& oracle = *op;
    bool covered = true;
    for (const auto kind : { QueryKind::Momentary, QueryKind::ShortTerm })
    {
        const char* name = kind == QueryKind::Momentary ? "momentary" : "short-term";
        const auto mine = answered (s, ask (kind, s, 0, made.shape.frames, 2048, made.id), covered);
        const auto theirs = oracle.query (ask (kind, oracle, 0, made.shape.frames, 2048));
        const auto& v = mine.view();
        std::uint64_t early = 0, measured = 0;
        for (std::uint64_t i = 0; i < v.stored; ++i)
        {
            if (same (v.values[std::size_t (3u * i + 2u)], double (MeasurementReason::TooShort))) ++early;
            if (same (v.values[std::size_t (3u * i + 2u)], double (MeasurementReason::None))) ++measured;
        }
        ok (v.status == QueryStatus::Ready && v.stride == 3 && v.complete && v.stored == made.shape.frames / 4800u && v.sampleRate == made.shape.sampleRate
            && v.measurementKey == made.id && sameRows (v.values, theirs.view().values),
            std::string (name) + ": " + std::to_string (v.stored) + " rows, a reading per 100 ms, bit for bit the delivered audio's own measured as a source");
        ok (early == (kind == QueryKind::Momentary ? 3u : 29u) && measured == v.stored - early,
            std::string (name) + ": the rows before a whole window say so, the rest are readings");
        const auto part = answered (s, ask (kind, s, 48000, 240000, 16, made.id), covered);
        const auto partOracle = oracle.query (ask (kind, oracle, 48000, 240000, 16));
        ok (part.view().status == QueryStatus::Ready && part.view().stored == 16 && ! part.view().complete && part.view().total == 40
            && sameRows (part.view().values, partOracle.view().values), std::string (name) + ": a part of it, thinned to 16 columns, the same way");
    }
    ok (covered, "each answer inside its declared memory");
    auto stale = ask (QueryKind::ShortTerm, s, 0, made.shape.frames, 64, made.id); stale.audioId ^= 1u;
    ok (s.query (stale).view().status == QueryStatus::StaleSource, "another source's id: stale");
    ok (s.query (ask (QueryKind::ShortTerm, s, 0, made.shape.frames, 64, made.id + 7u)).view().status == QueryStatus::Unavailable, "no such master: unavailable");
    ok (s.query (ask (QueryKind::ShortTerm, s, 0, made.shape.frames + 1u, 64, made.id)).view().status == QueryStatus::InvalidRange
        && s.query (ask (QueryKind::ShortTerm, s, 9, 9, 64, made.id)).view().status == QueryStatus::Empty
        && s.query (ask (QueryKind::ShortTerm, s, 0, made.shape.frames, 0, made.id)).view().status == QueryStatus::ColumnLimit,
        "past the master's end is refused, an empty range is empty, no columns is a limit");
    // After a good answer a refused one carries no rows.
    const auto refused = s.query (ask (QueryKind::ShortTerm, s, 0, made.shape.frames + 1u, 64, made.id));
    ok (refused.view().values.empty() && refused.view().stored == 0, "a refusal after a good answer is clean");
    const auto sourceOnly = s.query (ask (QueryKind::ShortTerm, s, 0, audio.frames, 64));
    ok (sourceOnly.view().status == QueryStatus::Ready && sourceOnly.view().measurementKey != made.id, "without a master id the curve is the source's, as before");
}

void theMastersAxes()
{
    felitronics::test::group ("(e) a master's waveform in the source's four axes — Mid and Side, envelope, band energies");
    const Audio audio (8);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate); auto& s = *sp;
    const auto made = master (s, 3);
    const float* planes[] { made.audio.data(), made.audio.data() + made.shape.frames };
    auto op = loaded (planes, made.shape.channels, made.shape.frames, made.shape.sampleRate); auto& oracle = *op;
    bool covered = true;
    const auto all = answered (s, ask (QueryKind::MasterAxes, s, 0, made.shape.frames, 2048, made.id), covered);
    const auto& v = all.view();
    ok (v.status == QueryStatus::Ready && v.stride == kWaveformStride && v.stored == 2048u * 4u && v.complete && v.channels == 2,
        "PRECONDITION: 2048 columns of four axes, rows of 13");
    // Each retained bucket against the source's Waveform over the very same frames of the delivered audio.
    bool exact = true, energies = true, side = false;
    for (const std::uint32_t column : { 0u, 1u, 7u, 500u, 1023u, 1024u, 1999u, 2047u })
        for (unsigned axis = 0; axis < 4; ++axis)
        {
            const auto* row = v.values.data() + std::size_t ((column * 4u + axis) * kWaveformStride);
            const auto theirs = oracle.query (ask (QueryKind::Waveform, oracle, std::uint64_t (row[0]), std::uint64_t (row[1]), 1));
            const auto* want = theirs.view().values.data() + std::size_t (axis * kWaveformStride);
            for (const unsigned field : { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 11u, 12u }) exact = exact && same (row[field], want[field]);
            for (const unsigned field : { 7u, 8u, 9u, 10u }) energies = energies && close (row[field], want[field]);
            side = side || (axis == 3 && row[7] > 1e-4);
        }
    ok (exact, "eight buckets, four axes each: frames, minimum, maximum, peak, envelope, finite count and reason are the source instrument's, to the bit");
    ok (energies && side, "RMS and the three band energies agree to 1e-9 — one running sum against a tree of them — and the Side axis carries signal");
    const auto whole = answered (s, ask (QueryKind::MasterAxes, s, 0, made.shape.frames, 1, made.id), covered);
    const auto wholeOracle = oracle.query (ask (QueryKind::Waveform, oracle, 0, made.shape.frames, 1));
    bool merged = whole.view().stored == 4;
    for (std::size_t i = 0; merged && i < 4u * kWaveformStride; ++i)
    {
        // The envelope of buckets merged is the largest of theirs: each bucket closes its own box at its edges, as an
        // exact column of the source does, so it stands at or above the whole range's and never above the peak.
        const double mine = whole.view().values[i], theirs = wholeOracle.view().values[i];
        merged = i % kWaveformStride == 6 ? mine >= theirs && mine <= whole.view().values[i - 1u] : close (mine, theirs);
    }
    ok (merged, "one column over the whole master is the whole delivered audio's column — its envelope the largest of its buckets'");
    // The L and R rows agree with the rows a snapshot carries (MasterWaveform), which are unchanged.
    const auto flat = answered (s, ask (QueryKind::MasterWaveform, s, 0, made.shape.frames, 2048, made.id), covered);
    bool agrees = flat.view().stride == 7 && flat.view().stored == 4096;
    for (std::uint32_t column = 0; agrees && column < 2048; ++column)
        for (unsigned ch = 0; ch < 2; ++ch)
        {
            const auto* old = flat.view().values.data() + std::size_t ((column * 2u + ch) * 7u);
            const auto* axes = v.values.data() + std::size_t ((column * 4u + ch) * kWaveformStride);
            agrees = agrees && same (old[0], axes[0]) && same (old[1], axes[1]) && same (old[3], axes[3]) && same (old[4], axes[4]) && close (old[5], axes[7]);
        }
    ok (agrees, "MasterWaveform still answers rows of 7, L and R — the same minima, maxima and RMS as the axes' first two");
    ok (covered, "every answer inside its declared memory");

    // Deep zoom on a chunk the caller supplies: from the master's first frame it is the instrument exactly.
    const Pcm head { planes, 2, 4096, made.shape.sampleRate };
    const auto chunkQuery = ask (QueryKind::MasterAxes, s, 0, 4096, 16, made.id);
    const auto need = s.masterWaveformChunkStorage (chunkQuery, head);
    QueryResult chunk;
    const auto spent = budget::spend ([&] { chunk = s.masterWaveformChunk (chunkQuery, head); });
    const auto chunkOracle = oracle.query (ask (QueryKind::Waveform, oracle, 0, 4096, 16));
    bool zoom = need.status == QueryStatus::Ready && chunk.view().status == QueryStatus::Ready && chunk.view().stored == 64 && chunk.view().stride == kWaveformStride
        && budget::covers (need.bytes, spent);
    for (std::size_t i = 0; zoom && i < chunk.view().values.size(); ++i) zoom = close (chunk.view().values[i], chunkOracle.view().values[i]);
    ok (zoom, "a chunk from frame 0 in 16 columns: the source instrument's rows, inside its declared memory");
    const float* later[] { planes[0] + 100000, planes[1] + 100000 };
    const auto laterQuery = ask (QueryKind::MasterAxes, s, 100000, 104096, 8, made.id);
    const auto deep = s.masterWaveformChunk (laterQuery, { later, 2, 4096, made.shape.sampleRate });
    const auto deepOracle = oracle.query (ask (QueryKind::Waveform, oracle, 100000, 104096, 8));
    bool shape = deep.view().status == QueryStatus::Ready && deep.view().stored == 32;
    for (std::size_t i = 0; shape && i < deep.view().values.size(); ++i)
    {
        const auto field = i % kWaveformStride;
        // The band split starts from rest at the chunk's first frame; everything else is exact.
        if (field < 8 || field > 10) shape = close (deep.view().values[i], deepOracle.view().values[i]);
    }
    ok (shape, "a chunk from the middle: frames, extremes, envelope and RMS exact; its band split settles from rest");

    // A mono master has L and Mid; R and Side say Unsupported, as a mono source's do.
    std::vector<float> one (audio.left);
    const float* mono[] { one.data() };
    auto mp = loaded (mono, 1, audio.frames, audio.rate);
    const auto monoMade = master (*mp, 3);
    const auto monoAxes = mp->query (ask (QueryKind::MasterAxes, *mp, 0, monoMade.shape.frames, 4, monoMade.id));
    bool absent = monoAxes.view().status == QueryStatus::Ready && monoAxes.view().stored == 16;
    for (unsigned column = 0; absent && column < 4; ++column)
        for (unsigned axis = 0; axis < 4; ++axis)
            absent = absent && same (monoAxes.view().values[std::size_t ((column * 4u + axis) * kWaveformStride + 12u)],
                double (axis == 1 || axis == 3 ? MeasurementReason::Unsupported : MeasurementReason::None));
    ok (absent, "a mono master: L and Mid measured, R and Side unsupported");
    ok (s.query (ask (QueryKind::MasterAxes, s, 0, made.shape.frames, 64, made.id + 9u)).view().status == QueryStatus::Unavailable
        && s.query (ask (QueryKind::MasterAxes, s, 5, 5, 64, made.id)).view().status == QueryStatus::Empty, "no such master is unavailable; an empty range is empty");
}

// THE PREVIOUS SUMMARY, on the full snapshot's view: the source's rows left out and nothing else — what buildSummary did
// before it knew of lean sessions. Its bytes are the oracle for a session that did not ask to be lean.
struct Transfer { std::vector<char> json; std::vector<double> rows; TransferNeed need {}; };
Transfer previousSummary (const Session& s)
{
    const auto full = s.snapshot();
    auto v = full.view();
    std::vector<MeasurementResult> results (v.measurements.begin(), v.measurements.end());
    for (auto& r : results) r.arrays = {};
    v.measurements = results; v.momentary = {}; v.shortTerm = {}; v.runs = {};
    v.measurementRowsIncluded = false;
    Transfer out; out.need = Wire::snapshotBytes (v);
    out.json.resize (out.need.jsonBytes); out.rows.resize (out.need.rowBytes / sizeof (double));
    felitronics::test::run (Wire::snapshot (v, out.json, out.rows) == CodecStatus::Ok);
    return out;
}
Transfer summaryOf (const Session& s)
{
    Transfer out; out.need = Wire::summaryBytes (s);
    out.json.resize (out.need.jsonBytes); out.rows.resize (out.need.rowBytes / sizeof (double));
    felitronics::test::run (Wire::summary (s, out.json, out.rows) == CodecStatus::Ok);
    return out;
}
bool sameBytes (const Transfer& a, const Transfer& b)
{
    return a.json == b.json && a.rows.size() == b.rows.size()
        && (a.rows.empty() || std::memcmp (a.rows.data(), b.rows.data(), a.rows.size() * sizeof (double)) == 0);
}

void theLeanSummary()
{
    felitronics::test::group ("(b) the lean summary: every master's scalars, its rows by a MasterReport query; the default untouched");
    const Audio audio (12);
    auto full = loaded (audio.planes, 2, audio.frames, audio.rate, false);
    auto lean = loaded (audio.planes, 2, audio.frames, audio.rate, true);
    std::uint32_t sizes[2][2][2] {};   // [masters 1 or 7][default or lean][json, rows]
    bool declared = true, identical = true, stripped = true, scalars = true, noHeap = true;
    std::vector<MasterId> ids;
    for (unsigned n = 1; n <= 7; ++n)
    {
        const auto a = master (*full, 10 + n), b = master (*lean, 10 + n);
        ids.push_back (b.id);
        declared = declared && budget::covers (a.declared.bytes, a.spent) && budget::covers (b.declared.bytes, b.spent) && b.spent.bytes > a.spent.bytes;
        const auto before = previousSummary (*full), now = summaryOf (*full);
        identical = identical && sameBytes (before, now);
        const auto spent = budget::spend ([&] { (void) Wire::summaryBytes (*lean); });
        noHeap = noHeap && spent.requests == 0;
        const auto light = summaryOf (*lean);
        if (n == 1 || n == 7)
        {
            sizes[n == 7][0][0] = now.need.jsonBytes; sizes[n == 7][0][1] = now.need.rowBytes;
            sizes[n == 7][1][0] = light.need.jsonBytes; sizes[n == 7][1][1] = light.need.rowBytes;
        }
        const auto summary = lean->summary(); const auto whole = lean->snapshot();
        stripped = stripped && ! summary.view().masterRowsIncluded && whole.view().masterRowsIncluded && summary.view().masters.size() == n;
        for (std::size_t i = 0; i < summary.view().masters.size(); ++i)
        {
            const auto& l = summary.view().masters[i]; const auto& w = whole.view().masters[i];
            stripped = stripped && l.landing && l.report && l.report->cost && ! l.landing->limiterTrace && ! l.landing->peakClipTrace
                && l.report->crest.rows.empty() && l.report->crest.sourceMask.empty() && l.report->cost->waveform.empty()
                && w.landing->limiterTrace && ! w.report->cost->waveform.empty() && ! w.landing->limiterTrace->rows.empty();
            scalars = scalars && l.id == w.id && l.landing->status == w.landing->status && l.landing->passes == w.landing->passes
                && l.landing->deliverable == w.landing->deliverable && l.landing->log.size() == w.landing->log.size() && l.landing->log.size() == l.landing->passes
                && same (*l.report->achievedLufs, *w.report->achievedLufs) && same (*l.report->truePeakDbTp, *w.report->truePeakDbTp)
                && l.report->crest.blocks == w.report->crest.blocks && l.report->crest.status == w.report->crest.status
                && l.report->cost->sections.size() == w.report->cost->sections.size() && l.report->cost->masterFrames == w.report->cost->masterFrames
                && l.report->cost->limiterP95Db.value.has_value() == w.report->cost->limiterP95Db.value.has_value()
                && l.recipe.readyHash == w.recipe.readyHash;
            for (std::size_t p = 0; p < l.landing->log.size(); ++p) scalars = scalars && same (l.landing->log[p].achievedLufs, w.landing->log[p].achievedLufs);
        }
    }
    ok (identical, "a session that did not ask: its summary is, byte for byte, the previous path's at every count of masters from 1 to 7");
    ok (stripped, "a lean session: its summary says masterRowsIncluded false and carries no trace, no crest rows or mask, no waveform bucket — its snapshot all of them");
    ok (scalars, "and every master keeps its scalars, its pass log and its cost sections");
    ok (noHeap, "sized without touching the heap");
    ok (declared, "the masters' memory is declared in both sessions — the lean one's room for its summary included");
    {
        // The room itself, on a master the session decides (version 0): the same job in both sessions, and, lean, a
        // second kept record for the summary — declared, and asked for.
        const Audio small (3);
        auto plain = loaded (small.planes, 2, small.frames, small.rate, false), thin = loaded (small.planes, 2, small.frames, small.rate, true);
        const auto a = plain->check (command::Master { 5 }), b = thin->check (command::Master { 5 });
        Answer started;
        const auto asked = budget::spend ([&] { (void) plain->apply (command::Master { 5 }); });
        const auto spent = budget::spend ([&] { started = thin->apply (command::Master { 5 }); });
        ok (a.rejection == Rejection::None && b.rejection == Rejection::None && b.bytes == a.bytes + sizeof (Kept) && a.bytes >= sizeof (Kept)
            && started.rejection == Rejection::None && budget::covers (a.bytes, asked) && budget::covers (b.bytes, spent)
            && spent.bytes == asked.bytes + (long long) sizeof (Kept),
            "a lean session declares a second kept record for its summary — " + std::to_string (b.bytes) + " B against "
            + std::to_string (a.bytes) + " — asks for it, and stays inside the declaration");
    }
    std::printf ("    one summary, JSON + rows: 1 master %u + %u B by default, %u + %u B lean; 7 masters %u + %u B by default, %u + %u B lean\n",
        sizes[0][0][0], sizes[0][0][1], sizes[0][1][0], sizes[0][1][1], sizes[1][0][0], sizes[1][0][1], sizes[1][1][0], sizes[1][1][1]);
    const auto perMaster = (sizes[1][1][0] - sizes[0][1][0]) / 6u;
    ok (sizes[0][1][0] < sizes[0][0][0] / 10u && sizes[1][1][0] < sizes[1][0][0] / 10u && sizes[1][1][1] == sizes[0][1][1]
        && sizes[1][0][1] > sizes[0][0][1] && perMaster < 16384u,
        "the lean summary is under a tenth of the default at 1 and at 7 masters, and grows by " + std::to_string (perMaster) + " B a master");

    // A lean summary is a valid snapshot: it encodes and decodes.
    {
        const auto summary = lean->summary();
        const auto need = Codec::encodedBytes (summary.view());
        std::vector<char> json (std::size_t (need.bytes));
        Snapshot back;
        ok (need.status == CodecStatus::Ok && Codec::encode (summary.view(), json) == CodecStatus::Ok
            && Codec::decode ({ json.data(), json.size() }, back) == CodecStatus::Ok && ! back.view().masterRowsIncluded
            && back.view().masters.size() == 7 && back.view().masters[0].report->crest.rows.empty(), "a lean summary encodes and decodes");
        // ...and only because it says what it is: the same record claiming to be whole is refused.
        std::string claimed (json.data(), json.size());
        const auto at = claimed.find ("\"masterRowsIncluded\":false");
        if (at != std::string::npos) claimed.replace (at, 26, "\"masterRowsIncluded\":true");
        Snapshot refusedCopy;
        ok (at != std::string::npos && Codec::decode (claimed, refusedCopy) == CodecStatus::Invalid,
            "the same masters under masterRowsIncluded true are refused: a whole snapshot has its rows");
    }

    // ONE MASTER WHOLE, by query: the record the full snapshot carries, to the bit.
    const auto whole = lean->snapshot();
    bool covered = true, equal = true, owned = true;
    std::uint64_t largestDeclared = 0;
    for (const MasterId id : { ids[0], ids[6], ids[3] })
    {
        auto q = ask (QueryKind::MasterReport, *lean, 0, 0, 0, id);
        const auto need = lean->queryStorage (q);
        largestDeclared = std::max (largestDeclared, need.bytes);
        const auto answer = answered (*lean, q, covered);
        const auto& v = answer.view();
        const auto* want = kept (whole.view(), id);
        equal = equal && v.status == QueryStatus::Ready && v.master && v.values.empty() && v.stride == 0 && v.complete && v.measurementKey == id
            && v.master->id == id && v.master->landing && v.master->report && v.master->report->cost;
        if (! equal) break;
        const auto& m = *v.master;
        const auto sameTrace = [] (const std::optional<LandingTrace>& a, const std::optional<LandingTrace>& b)
        {
            if (a.has_value() != b.has_value()) return false;
            if (! a) return true;
            bool yes = a->columns == b->columns && a->fromFrame == b->fromFrame && a->toFrame == b->toFrame && a->samples == b->samples && a->rows.size() == b->rows.size();
            for (std::size_t i = 0; yes && i < a->rows.size(); ++i)
                yes = same (a->rows[i].minDb, b->rows[i].minDb) && same (a->rows[i].maxDb, b->rows[i].maxDb) && same (a->rows[i].meanDb, b->rows[i].meanDb)
                    && a->rows[i].samples == b->rows[i].samples;
            return yes;
        };
        equal = equal && sameTrace (m.landing->limiterTrace, want->landing->limiterTrace) && sameTrace (m.landing->peakClipTrace, want->landing->peakClipTrace)
            && m.landing->limiterTrace && ! m.landing->limiterTrace->rows.empty()
            && sameRows (m.report->crest.rows, want->report->crest.rows) && sameRows (m.report->crest.sourceMask, want->report->crest.sourceMask)
            && ! m.report->crest.rows.empty() && m.report->cost->waveform.size() == want->report->cost->waveform.size() && ! m.report->cost->waveform.empty()
            && m.report->cost->sections.size() == want->report->cost->sections.size() && m.landing->log.size() == want->landing->log.size()
            && same (*m.report->achievedLufs, *want->report->achievedLufs) && m.recipe.readyHash == want->recipe.readyHash;
        for (std::size_t i = 0; equal && i < m.report->cost->waveform.size(); ++i)
            equal = same (m.report->cost->waveform[i].minimum, want->report->cost->waveform[i].minimum) && same (m.report->cost->waveform[i].rms, want->report->cost->waveform[i].rms)
                && m.report->cost->waveform[i].fromFrame == want->report->cost->waveform[i].fromFrame;
        // The answer owns its rows: nothing of it points into the session's.
        const auto* live = kept ({ .masters = lean->masters() }, id);
        owned = owned && live && m.report->crest.rows.data() != live->report->crest.rows.data()
            && m.landing->limiterTrace->rows.data() != live->landing->limiterTrace->rows.data()
            && m.report->cost->waveform.data() != live->report->cost->waveform.data() && m.landing->log.data() != live->landing->log.data();
    }
    ok (equal, "MasterReport by id: the landing with both traces, the crest rows and mask, the sections and the waveform buckets — the full snapshot's, to the bit");
    ok (owned, "in storage of its own: no row of the answer is the session's");
    ok (covered, "inside its declared memory (" + std::to_string (largestDeclared) + " B declared for the transient copy)");
    std::printf ("    a MasterReport query declares %llu B for its answer\n", (unsigned long long) largestDeclared);

    // On the wire: sized before the work, written within the size.
    {
        const std::string request = "{\"kind\":11,\"audioId\":\"" + std::to_string (lean->source().hash) + "\",\"fromFrame\":\"0\",\"toFrame\":\"0\",\"columns\":0,\"requestId\":\"77\",\"masterId\":"
            + std::to_string (ids[2]) + "}";
        const auto bound = Wire::queryBuffers (*lean, request);
        std::vector<char> json (bound.jsonBytes); std::vector<double> rows (bound.rowBytes / sizeof (double));
        const auto written = Wire::query (*lean, request, json, rows);
        const std::string_view text { json.data(), written.jsonBytes };
        ok (bound.status == CodecStatus::Ok && written.status == CodecStatus::Ok && written.jsonBytes <= bound.jsonBytes && written.rowBytes == bound.rowBytes
            && written.rowBytes != 0 && text.find ("\"master\":{") != std::string_view::npos && text.find ("\"limiterTrace\":{") != std::string_view::npos
            && text.find ("\"requestId\":\"77\"") != std::string_view::npos,
            "on the wire: " + std::to_string (written.jsonBytes) + " B of JSON within the " + std::to_string (bound.jsonBytes) + " B sized beforehand, "
            + std::to_string (written.rowBytes) + " B of rows");
        std::printf ("    on the wire: %u B of JSON (sized beforehand at %u) and %u B of rows\n", written.jsonBytes, bound.jsonBytes, written.rowBytes);
        std::vector<char> small (bound.jsonBytes / 2u);
        ok (Wire::query (*lean, request, small, rows).status == CodecStatus::TooSmall, "a buffer too small is refused, nothing written past it");
    }

    // Refusals, each clean — after good answers.
    auto unknown = ask (QueryKind::MasterReport, *lean, 0, 0, 0, ids[6] + 50u);
    auto stale = ask (QueryKind::MasterReport, *lean, 0, 0, 0, ids[0]); stale.audioId ^= 1u;
    auto anonymous = ask (QueryKind::MasterReport, *lean, 0, 0, 0, 0);
    const auto a = lean->query (unknown), b = lean->query (stale), c = lean->query (anonymous);
    ok (a.view().status == QueryStatus::Unavailable && ! a.view().master && b.view().status == QueryStatus::StaleSource && ! b.view().master
        && c.view().status == QueryStatus::Contract && ! c.view().master, "no such master, another source's id, no id: refused, with no record left over from the last answer");
    const auto report = ask (QueryKind::MasterReport, *lean, 0, 0, 0, ids[0]);
    const auto need = lean->queryStorage (report);
    ok (lean->setCapacity ({ lean->liveBytes() + double (need.bytes) - 1.0, 9007199254740991.0 }) == Status::Ok, "PRECONDITION: a ceiling one byte short");
    const auto spent = budget::spend ([&] { (void) lean->query (report); });
    const auto refused = lean->query (report);
    ok (refused.view().status == QueryStatus::Memory && ! refused.view().master && spent.requests == 0, "under a ceiling one byte short it is refused before any allocation");
    ok (lean->setCapacity ({}) == Status::Ok && lean->query (report).view().status == QueryStatus::Ready, "and answered again with the ceiling lifted");
    // The other master queries work on a lean session as on any.
    ok (lean->query (ask (QueryKind::LimiterGr, *lean, 0, audio.frames, 64, ids[1])).view().status == QueryStatus::Ready
        && lean->query (ask (QueryKind::MasterWaveform, *lean, 0, audio.frames, 64, ids[1])).view().status == QueryStatus::Ready, "the trace and waveform queries answer as before");
    // Forgetting a master leaves the summary whole.
    ok (lean->apply (command::Forget { 90, ids[2] }).rejection == Rejection::None && lean->summary().view().masters.size() == 6
        && lean->query (ask (QueryKind::MasterReport, *lean, 0, 0, 0, ids[2])).view().status == QueryStatus::Unavailable
        && lean->query (ask (QueryKind::MasterReport, *lean, 0, 0, 0, ids[3])).view().status == QueryStatus::Ready, "a forgotten master leaves the summary and the query to the rest");
}
} // namespace

int main()
{
    std::printf ("felitronics::session — a master read by queries, and the lean summary\n");
    theLandingsFacts();
    theSpectrumsQuantity();
    theMastersLoudness();
    theMastersAxes();
    theLeanSummary();
    return felitronics::test::report();
}
