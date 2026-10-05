// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Config.h>
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
#include <span>
#include <tuple>
#include <utility>
#include <vector>

using namespace felitronics;
using felitronics::test::ok;
namespace declared = felitronics::declared;

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
          bool waitForCrest = true, bool demanding = false, unsigned seconds = 4,
          unsigned silentSeconds = 0, const char* target = nullptr, float divisor = 4096.0f,
          std::vector<session::Notification>* events = nullptr, bool fine = false, bool tones = false,
          bool idleLimiter = false, bool grade = true)
{
    c.rate = sourceRate; c.delivery = deliveryRate;
    const std::size_t frames = std::size_t (sourceRate) * seconds;
    c.left.resize (frames); c.right.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        c.left[i] = float (int ((i * 17u) % 251u) - 125) / divisor;
        c.right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / divisor;
        if (tones)
        {
            // Band-limited and dynamic, for PEAQ: four partials up to 9.9 kHz (its bandwidth reads above 8.1 kHz, and
            // nothing reaches 21.6 kHz), struck every half second and decaying — the glue and the limiter have work.
            const double t = double (i) / double (sourceRate), beat = t - 0.5 * std::floor (t / 0.5);
            const double env = std::exp (-6.0 * beat) * 0.5;
            const double tw = 6.283185307179586 * t;
            const double x = env * (std::sin (220.0 * tw) + 0.5 * std::sin (1100.0 * tw) + 0.3 * std::sin (3300.0 * tw)
                                    + 0.2 * std::sin (9900.0 * tw));
            c.left[i] = float (x);
            c.right[i] = float (0.9 * x + 0.05 * std::sin (440.0 * tw) * env);
        }
        if (i < std::size_t (sourceRate) * silentSeconds) c.left[i] = c.right[i] = 0.0f;
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
    // A target well under the programme: the landing lowers it, and the limiter never reaches its ceiling.
    if (idleLimiter && s.apply (session::command::EditTarget { 9, { -24.0, -1.0 } }).rejection
        != session::Rejection::None) return false;
    // The delivery rate is the target's: a changed rate names a target that delivers it (youtube at 48 kHz with the
    // allStreaming numbers, cdDynamic at 44.1 kHz), and the request restates that rate.
    if (! target && deliveryRate != 0 && deliveryRate != sourceRate)
        target = deliveryRate == 48000 ? "youtube" : deliveryRate == 44100 ? "cdDynamic" : nullptr;
    if (! target && deliveryRate != 0 && deliveryRate != sourceRate) return false;
    if (target && s.apply (session::command::SetTarget { 8, target }).rejection
        != session::Rejection::None) return false;
    c.request = session::command::Master { 2 };
    c.request.ready.version = 1;
    c.request.ready.deliveryRateHz = deliveryRate;
    c.request.ready.topology.eq = processed;
    c.request.ready.topology.compressor = processed;
    c.request.ready.topology.clipper = processed;
    c.request.ready.topology.limiter = processed || demanding || idleLimiter;
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
    const auto spent = declared::spend ([&] { start = s.apply (c.request); });
    if (start.rejection != session::Rejection::None || ! declared::covers (checked.bytes, spent)) return false;
    std::uint64_t largestStep = 0;
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
    {
        const auto stepSpent = declared::spend ([&] { (void) s.step (fine ? 1u : i % 3u == 0 ? 1u : i % 3u == 1 ? 7u : 73u); });
        largestStep = std::max (largestStep, std::uint64_t (stepSpent.bytes));
        if (events) for (const auto& e : s.events()) events->push_back (e);
    }
    if (s.job() != 0 || s.masters().size() != 1 || ! s.masters()[0].report || ! s.masters()[0].landing) return false;
    // The damage's own job, asked as a shell asks it once the master is delivered (command::GradeDamage) and behind the
    // source's measurement: graded here where that has ended (a case that leaves the measurement running keeps its
    // state, and `grade` false leaves it to the case), inside the master's admitted demand too.
    if (s.masters()[0].report->damage.status == session::MeasurementStatus::Pending)
    {
        if (s.apply (session::command::GradeDamage { c.request.id + 1000u, s.masters()[0].id }).rejection != session::Rejection::None)
            return false;
        if (events) for (const auto& e : s.events()) events->push_back (e);
    }
    for (unsigned i = 0; grade && i < 400000 && ! s.damageJobs().empty() && s.measurementJob() == 0; ++i)
    {
        const auto stepSpent = declared::spend ([&] { (void) s.step (fine ? 1u : i % 3u == 0 ? 1u : i % 3u == 1 ? 7u : 73u); });
        largestStep = std::max (largestStep, std::uint64_t (stepSpent.bytes));
        if (events) for (const auto& e : s.events()) events->push_back (e);
    }
    if ((grade && ! s.damageJobs().empty() && s.measurementJob() == 0) || largestStep > checked.bytes) return false;
    c.token = s.pendingMaster();
    const auto shape = s.masterAudioShape (c.token);
    if (c.token.master == 0 || (deliveryRate != 0 && shape.sampleRate != deliveryRate)) return false;
    c.delivery = shape.sampleRate;   // a request of 0 takes the target's rate
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
    double sourceLufs = std::numeric_limits<double>::quiet_NaN();
    for (const auto& value : sourceSnapshot.view().measurements[std::size_t (session::Analyzer::Loudness)].numbers)
        if (value.name == "integratedLufs" && value.value) sourceLufs = *value.value;
    const auto config = session::config::Config::load();
    if (! config.ok() || ! std::isfinite (sourceLufs)) return false;
    params.inputGainDb += config.config.engine.input.referenceLufs - sourceLufs;
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
bool crestK1Ready (const Case& c)
{
    if (! c.session || c.session->masters().size() != 1 || ! c.session->masters()[0].report
        || ! c.session->masters()[0].report->cost) return false;
    const auto& k1 = c.session->masters()[0].report->cost->crestFullDb;
    return k1.reason == session::MeasurementReason::None && k1.value.has_value();
}
bool lateCrestAfterRelease (Case& c)
{
    auto& s = *c.session;
    if (s.masters().size() != 1 || ! s.masters()[0].report
        || s.masters()[0].report->crest.status != session::MeasurementStatus::Pending
        || s.releaseMaster (c.token) != session::MasterTransferStatus::Ok) return false;
    for (unsigned i = 0; i < 200000 && s.measurementJob() != 0; ++i) (void) s.step (73);
    for (unsigned i = 0; i < 200000 && ! crestK1Ready (c);
         ++i) (void) s.step (1);
    return s.measurementJob() == 0 && s.pendingMaster().master == 0
        && referenceCrest (c) && crestK1Ready (c);
}
// A cancelled source measurement resumed before the next step: the join the cancel armed runs while the source crest
// is Pending again. It must leave the master Pending and join on the resumed measurement's own result.
bool resumedBeforeJoin (Case& c)
{
    auto& s = *c.session;
    const auto job = s.measurementJob();
    if (job == 0 || s.masters().size() != 1 || ! s.masters()[0].report
        || s.masters()[0].report->crest.status != session::MeasurementStatus::Pending
        || s.apply (session::command::Cancel { 30, job }).rejection != session::Rejection::None
        || s.apply (session::command::ContinueMeasurement { 31 }).rejection != session::Rejection::None
        || s.measurementJob() == 0) return false;
    for (unsigned i = 0; i < 400000 && s.step (73).state == session::StepState::More; ++i) {}
    return s.measurementJob() == 0 && s.masters()[0].report->crest.status == session::MeasurementStatus::Ready
        && crestK1Ready (c);
}
// A crest joined inside the master job, then a join re-armed by cancelling the still running source measurement: the
// master is complete, so the join publishes no second fact and moves no revision.
bool joinedOnce (std::string& why)
{
    constexpr std::uint32_t rate = 48000;
    const std::size_t frames = std::size_t (rate) * 4u;
    std::vector<float> left (frames), right (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 512.0f;
        right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 512.0f;
    }
    const float* planes[2] { left.data(), right.data() };
    auto made = session::Session::create();
    if (made.status != session::Status::Ok) { why = "create"; return false; }
    auto& s = *made.session;
    if (s.apply (session::command::Load { 1, { planes, 2, frames, rate }, { "joined.wav", rate, true, 24 } }).rejection
        != session::Rejection::None) { why = "load"; return false; }
    const auto crest = [&] { return s.snapshot().view().measurements[std::size_t (session::Analyzer::Crest)].status; };
    for (unsigned i = 0; i < 200000 && s.measurementJob() != 0
         && (crest() != session::MeasurementStatus::Ready || ! s.snapshot().view().mandatoryMeasurementsReady); ++i)
        (void) s.step (1);
    const auto measuring = s.measurementJob();
    if (measuring == 0 || crest() != session::MeasurementStatus::Ready)
    { why = "PRECONDITION: the source crest is ready while its measurement job still runs"; return false; }
    session::command::Master request { 2 };
    request.ready.version = 1;
    request.ready.topology.eq = request.ready.topology.compressor = request.ready.topology.clipper = false;
    request.ready.topology.dither = false;
    request.source = s.source().hash; request.revision = s.revision();
    if (s.apply (request).rejection != session::Rejection::None) { why = "master"; return false; }
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) (void) s.step (73);
    if (s.job() != 0 || s.masters().size() != 1 || ! s.masters()[0].report
        || s.masters()[0].report->crest.status != session::MeasurementStatus::Ready || s.measurementJob() != measuring)
    { why = "PRECONDITION: the crest joined inside the job while the source measurement still runs"; return false; }
    const auto master = s.masters()[0].id;
    const auto k1 = s.masters()[0].report->cost ? s.masters()[0].report->cost->crestFullDb.value : std::nullopt;
    if (s.apply (session::command::Cancel { 3, measuring }).rejection != session::Rejection::None)
    { why = "cancel"; return false; }
    // The master's damage is graded only when the shell asks (command::GradeDamage): none runs, so what follows is the
    // join alone.
    if (s.damageJob() != 0 || ! s.damageJobs().empty()) { why = "a grade nobody asked for"; return false; }
    const auto revision = s.revision();
    unsigned facts = 0;
    for (unsigned i = 0; i < 1000 && s.step (1).state == session::StepState::More; ++i)
        for (const auto& event : s.events())
            facts += event.kind == session::EventKind::Fact && event.jobId == master ? 1u : 0u;
    for (const auto& event : s.events()) facts += event.kind == session::EventKind::Fact && event.jobId == master ? 1u : 0u;
    const auto k1After = s.masters()[0].report->cost ? s.masters()[0].report->cost->crestFullDb.value : std::nullopt;
    why = std::to_string (facts) + " facts, revision " + std::to_string (revision) + " -> " + std::to_string (s.revision());
    return facts == 0 && s.revision() == revision && k1 == k1After;
}
// The crest still pending when the master ends: the report says no crest line, and the late join says it once — its
// final word, never "pending".
bool lateCrestSaidOnce (std::string& why)
{
    constexpr std::uint32_t rate = 48000;
    const std::size_t frames = std::size_t (rate) * 4u;
    std::vector<float> left (frames), right (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 512.0f;
        right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 512.0f;
    }
    const float* planes[2] { left.data(), right.data() };
    auto made = session::Session::create();
    if (made.status != session::Status::Ok) { why = "create"; return false; }
    auto& s = *made.session;
    if (s.apply (session::command::Load { 1, { planes, 2, frames, rate }, { "late.wav", rate, true, 24 } }).rejection
        != session::Rejection::None) { why = "load"; return false; }
    for (unsigned i = 0; i < 200000 && (s.state() == session::State::Loaded || ! s.snapshot().view().mandatoryMeasurementsReady); ++i)
        (void) s.step (16);
    session::command::Master request { 2 };
    request.ready.version = 1;
    request.ready.topology.eq = request.ready.topology.compressor = request.ready.topology.clipper = false;
    request.ready.topology.dither = false;
    request.source = s.source().hash; request.revision = s.revision();
    if (s.apply (request).rejection != session::Rejection::None) { why = "master"; return false; }
    std::vector<std::pair<std::uint64_t, session::text::FactId>> facts;
    const auto collect = [&]
    {
        for (const auto& e : s.events())
            if (e.kind == session::EventKind::Fact) facts.push_back ({ e.jobId, e.payload.fact.view().id });
    };
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) { (void) s.step (73); collect(); }
    if (s.job() != 0 || s.masters().size() != 1 || ! s.masters()[0].report
        || s.masters()[0].report->crest.status != session::MeasurementStatus::Pending)
    { why = "PRECONDITION: the master ends while the source crest is pending"; return false; }
    for (unsigned i = 0; i < 400000 && s.masters()[0].report->crest.status == session::MeasurementStatus::Pending; ++i)
    { (void) s.step (16); collect(); }
    for (unsigned i = 0; i < 64; ++i) { (void) s.step (16); collect(); }
    using session::text::FactId;
    unsigned lines = 0; FactId last = FactId::Value;
    for (const auto& [job, id] : facts)
        if (job == s.masters()[0].id && (id == FactId::MasterCrestSourceRate || id == FactId::MasterCrestPending
            || id == FactId::MasterCrestUnavailable || id == FactId::MasterCrestDelivered)) { ++lines; last = id; }
    why = std::to_string (lines) + " crest lines, the last " + std::to_string (unsigned (last));
    return lines == 1 && last != FactId::MasterCrestPending
        && s.masters()[0].report->crest.status == session::MeasurementStatus::Ready;
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
bool memoryLifecycle (Case& c)
{
    auto& s = *c.session;
    for (unsigned i = 0; i < 200000 && s.measurementJob() != 0; ++i) (void) s.step (73);
    if (s.measurementJob() != 0) return false;
    if (s.releaseMaster (c.token) != session::MasterTransferStatus::Ok) return false;
    const auto exercise = [&] (const char* target, session::CommandId id, bool cancel,
                               bool testRefusal) -> bool
    {
        // The previous master's damage ends first: this lifecycle is the master's own (a master priced while a damage
        // runs is MasterJobTests' masterAtTheCeilingWithADamage).
        for (unsigned i = 0; i < 400000 && s.damageJob() != 0; ++i) (void) s.step (73);
        if (s.damageJob() != 0) return false;
        if (s.apply (session::command::SetTarget { id + 200u, target }).rejection != session::Rejection::None)
            return false;
        auto request = c.request;
        request.id = id;
        request.ready.deliveryRateHz = 0;
        request.source = s.source().hash;
        request.revision = s.revision();
        const auto declared = s.check (request);
        if (declared.rejection != session::Rejection::None || declared.bytes == 0
            || declared.largestBlockBytes == 0) return false;
        const auto initial = std::uint64_t (s.liveBytes());
        const auto ceiling = initial + declared.bytes;
        if (testRefusal)
        {
            const auto before = s.revision();
            if (s.setCapacity ({ double (ceiling - 1u), double (ceiling) }) != session::Status::Ok) return false;
            session::Answer refused;
            const auto spent = declared::spend ([&] { refused = s.apply (request); });
            if (refused.rejection != session::Rejection::Memory || spent.requests != 0
                || s.revision() != before || s.liveBytes() != double (initial)) return false;
            if (s.setCapacity ({ 9007199254740991.0, double (declared.largestBlockBytes - 1u) })
                != session::Status::Ok) return false;
            const auto blockSpent = declared::spend ([&] { refused = s.apply (request); });
            if (refused.rejection != session::Rejection::Memory || blockSpent.requests != 0
                || s.revision() != before) return false;
            if (s.setCapacity ({}) != session::Status::Ok) return false;
        }
        std::uint64_t peakLive = initial;
        declared::LifecycleBudget allocation { declared.bytes, declared.largestBlockBytes };
        bool covered = true;
        const auto charge = [&] (auto&& work)
        {
            const auto before = std::uint64_t (s.liveBytes());
            const auto spent = declared::spend (work);
            const auto after = std::uint64_t (s.liveBytes());
            if (spent.bytes < 0) { covered = false; return; }
            peakLive = std::max ({ peakLive, before, after });
            covered = covered && allocation.charge (spent) && peakLive <= ceiling;
        };
        session::Answer started;
        charge ([&] { started = s.apply (request); });
        if (started.rejection != session::Rejection::None || ! covered) return false;
        for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
        {
            charge ([&] { (void) s.step (cancel ? 1u : 73u); });
            if (! covered) return false;
            if (cancel && i == 20u)
            {
                charge ([&] { (void) s.apply (session::command::Cancel { id + 100u, started.job }); });
                break;
            }
        }
        if (s.job() != 0 || ! covered || peakLive > ceiling) return false;
        if (! cancel)
        {
            const auto token = s.pendingMaster();
            if (token.master == 0 || s.releaseMaster (token) != session::MasterTransferStatus::Ok) return false;
        }
        return true;
    };
    // A rate-converting target (SRC, source-rate crest pass) cancelled, a rate-keeping one, then the converting one
    // again with its refusals.
    return exercise ("cdDynamic", 20, true, false)
        && exercise ("allStreaming", 21, false, false)
        && exercise ("cdDynamic", 22, false, true);
}
}

// THE COST'S LINES AND THE MASTER'S READINGS, on a report built by hand: each line where its numbers were measured and
// nothing where one is missing; each reading with its unit and precision, rendered as the catalogue says.
void costLinesAndReadings()
{
    using session::MasterReportText; using session::text::FactId; using session::text::Lang;
    const auto ru = [] (const session::text::Fact& f) { return session::text::Text::text (f, Lang::Ru); };
    const auto en = [] (const session::text::Fact& f) { return session::text::Text::text (f, Lang::En); };
    session::MasterCost cost;
    ok (! MasterReportText::section (cost) && ! MasterReportText::sections (cost) && ! MasterReportText::limiter (cost)
        && ! MasterReportText::active (cost) && ! MasterReportText::bands (cost),
        "nothing measured: none of the cost's new lines");
    const session::MasterSection rows[] { { 0, 144000, -20.0, -14.0, 0.4, true }, { 144000, 336000, -18.0, -13.26, -1.26, true },
                                          { 336000, 384000, -60.0, -60.0, 0.0, false } };
    cost.sections = rows; cost.worstSectionIndex = 1u; cost.sourceRateHz = 48000;
    cost.largestSectionShiftLu.value = -1.26;
    cost.limiterP50Db.value = 0.84; cost.limiterP95Db.value = 2.36;
    cost.limiterActiveShare.value = 0.254; cost.activeWindowShare.value = 0.9;
    cost.crestLowDb.value = 0.52; cost.crestLowMidDb.value = 1.04; cost.crestHighMidDb.value = 1.56; cost.crestHighDb.value = 2.08;
    const auto section = MasterReportText::section (cost), sections = MasterReportText::sections (cost);
    const auto limiter = MasterReportText::limiter (cost), active = MasterReportText::active (cost), bands = MasterReportText::bands (cost);
    ok (section && section->id == FactId::MasterCostSection
        && ru (*section) == "Наибольший сдвиг секции: −1,3 LU на отрезке 3 с–7 с."
        && en (*section) == "The largest section shift is −1.3 LU, at 3 s–7 s.",
        "the worst section: its shift, and where it lies in the source, in seconds — " + (section ? ru (*section) : std::string()));
    ok (sections && sections->id == FactId::MasterCostSections && en (*sections) == "Sections compared: 2."
        && ru (*sections) == "Сопоставлено секций: 2.", "the sections compared: those with a comparison, not every section");
    ok (limiter && ru (*limiter) == "Срез лимитера в активных окнах: медиана 0,8 дБ, P95 2,4 дБ."
        && en (*limiter) == "Limiter reduction over the active windows: median 0.8 dB, P95 2.4 dB.",
        "the limiter's median and P95 over the active windows — " + (limiter ? ru (*limiter) : std::string()));
    ok (active && ru (*active) == "Лимитер работает в 25 % окон; активных окон — 90 %."
        && en (*active) == "The limiter works in 25% of the windows; 90% of the windows are active.",
        "the shares, in whole percent — " + (active ? ru (*active) : std::string()));
    ok (bands && ru (*bands) == "Потеря удара по полосам: низ 0,5 дБ, нижняя середина 1,0 дБ, верхняя середина 1,6 дБ, верх 2,1 дБ."
        && en (*bands) == "Impact loss by band: low 0.5 dB, low-mid 1.0 dB, high-mid 1.6 dB, high 2.1 dB.",
        "the impact loss of the four bands below the full band — " + (bands ? ru (*bands) : std::string()));
    auto partial = cost;
    partial.limiterP95Db.value.reset(); partial.activeWindowShare.value.reset(); partial.crestHighDb.value.reset();
    partial.largestSectionShiftLu.value.reset();
    ok (! MasterReportText::limiter (partial) && ! MasterReportText::active (partial) && ! MasterReportText::bands (partial)
        && ! MasterReportText::section (partial) && MasterReportText::sections (partial),
        "a number not measured takes its line away, and only its own");

    session::MasterReport report;
    report.achievedLufs = -14.04; report.truePeakDbTp = -1.26; report.targetLufs = -14.0; report.ceilingDbTp = -1.0;
    report.gainFromSourceDb = 3.46; report.checkPasses = 1;
    const auto readings = MasterReportText::readings (report, 4);
    const session::ReadingKind kinds[] { session::ReadingKind::Integrated, session::ReadingKind::TruePeak, session::ReadingKind::Target,
        session::ReadingKind::Ceiling, session::ReadingKind::Gain, session::ReadingKind::Passes, session::ReadingKind::CheckPasses };
    bool inOrder = readings.count == std::size (kinds);
    for (std::size_t i = 0; inOrder && i < readings.count; ++i)
        inOrder = readings.items[i].kind == kinds[i] && readings.items[i].fact.id == FactId::Value && readings.items[i].fact.argCount == 1;
    ok (inOrder, "the master's readings in the order of ReadingKind; LRA and PLR not measured have none");
    if (inOrder)
    {
        const auto& r = readings.items;
        ok (ru (r[0].fact) == "−14,0 LUFS" && en (r[0].fact) == "−14.0 LUFS" && ru (r[1].fact) == "−1,3 dBTP"
            && ru (r[3].fact) == "−1,0 dBTP" && ru (r[4].fact) == "+3,5 дБ" && en (r[4].fact) == "+3.5 dB"
            && en (r[5].fact) == "4" && en (r[6].fact) == "1",
            "each reading as the catalogue writes it: the decimal comma in Russian, the gain signed, the passes whole — "
            + ru (r[0].fact) + " · " + ru (r[4].fact));
    }
}

// THE DAMAGE (MasterReport.damage): PEAQ of the master against the same chain with its dynamics at rest, in windows; the
// loudness range input against master; and the current walk's share of the file in every master phase that walks.
std::vector<session::text::FactId> factsOf (const std::vector<session::Notification>& events)
{
    std::vector<session::text::FactId> out;
    for (const auto& e : events)
        if (e.kind == session::EventKind::Fact) out.push_back (e.payload.fact.view().id);
    return out;
}
bool has (const std::vector<session::text::FactId>& facts, session::text::FactId id)
{ return std::find (facts.begin(), facts.end(), id) != facts.end(); }
void damage()
{
    using session::text::FactId;
    // A master whose chain does nothing (no EQ, glue, saturation, limiter or dither): the reference is the master to the
    // rounding of a gain — PEAQ's Transparent, grade 5, nothing heard; the line says so.
    {
        Case c; std::vector<session::Notification> events;
        const bool made = run (c, 48000, 48000, false, true, false, 4, 0, nullptr, 4096.0f, &events, false, true);
        ok (made, "a master of a chain that does nothing is delivered");
        if (made)
        {
            const auto& d = c.session->masters()[0].report->damage;
            ok (d.status == session::MeasurementStatus::Ready && d.verdict == session::DamageVerdict::Transparent
                && d.grade == 5 && d.worstOdg && *d.worstOdg == 0.0 && d.windows == 1 && d.audibleWindows == 0
                && d.audibleShare && *d.audibleShare == 0.0 && d.worstFromSeconds && *d.worstFromSeconds == 0.0
                && d.windowSeconds == 10.0 && d.hopSeconds == 5.0,
                "a chain at rest is Transparent: grade 5, ODG 0, no window heard (one 10 s window over 4 s)");
            ok (has (factsOf (events), FactId::MasterDamageInaudible) && ! has (factsOf (events), FactId::MasterDamage),
                "a damage nobody hears is said as such");
        }
    }
    // The limiter in the chain (its oversampled true-peak path, its lookahead) at rest: a target far under the programme.
    // The reference takes the same path, so the two are equal to a gain's rounding — Transparent, not PEAQ's -2 on the
    // structure a path of other filters and delays would leave.
    {
        Case c; std::vector<session::Notification> events;
        const bool made = run (c, 48000, 48000, false, true, false, 4, 0, nullptr, 4096.0f, &events, false, true, true);
        ok (made, "a master with its limiter at rest is delivered");
        if (made)
        {
            const auto& d = c.session->masters()[0].report->damage;
            ok (d.status == session::MeasurementStatus::Ready && d.verdict == session::DamageVerdict::Transparent && d.grade == 5,
                "a limiter that never reaches its ceiling leaves the master Transparent against its reference");
        }
    }
    // A master that works — glue at -30 dB 2.5:1, the soft clipper half wet, the limiter landing it: graded, the worst
    // window named with its grade on the BS.1116 scale. The numbers are this source's, pinned (native and wasm alike).
    {
        Case c; std::vector<session::Notification> events;
        const bool made = run (c, 48000, 48000, true, true, true, 4, 0, nullptr, 4096.0f, &events, false, true);
        ok (made, "a processed master is delivered");
        if (made)
        {
            const auto& d = c.session->masters()[0].report->damage;
            ok (d.status == session::MeasurementStatus::Ready && d.verdict == session::DamageVerdict::Graded && d.worstOdg
                && *d.worstOdg < 0.0 && d.grade >= 1 && d.grade <= 5 && d.worstDi && d.windows == 1,
                "a master whose dynamics work is graded");
            const double odg = d.worstOdg.value_or (0.0);
            const unsigned grade = odg >= -0.5 ? 5u : odg >= -1.5 ? 4u : odg >= -2.5 ? 3u : odg >= -3.5 ? 2u : 1u;
            ok (d.grade == grade && d.audibleWindows == (grade < 5u ? 1u : 0u),
                "the grade is the BS.1116 scale's for the worst window's ODG (edges -0.5, -1.5, -2.5, -3.5)");
            const auto facts = factsOf (events);
            ok (grade < 5u ? has (facts, FactId::MasterDamage) : has (facts, FactId::MasterDamageInaudible),
                "the damage's line comes after the master, from the damage's own job");
            // THE GRADE OF THIS SOURCE, pinned. The walks gave the same inside the master's job (186f9c5) until the
            // reference went the whole referenceBelowDb lower where the input gain's range stops short: this master's
            // input gain is under 0 dB, so 186f9c5 lowered its reference by less (ODG -3.8489970, DI -3.4359317, gain
            // -50.0476 dB there). The limiter's budget (v0.14.0's landing) then holds this demanding master short of its
            // target: its limiter takes about 13 dB, no longer 52 (ODG -3.8374910, DI -3.3489147, gain -52.5884 dB
            // before it); at the loud target's 7.5 dB (v0.14.1, it was 10) about 7.5 (ODG -3.8521412, DI -3.4609980,
            // gain -12.8745 dB at 10). Its soft clipper is a stage on libm, so ODG and DI agree between native and wasm only
            // to PEAQ's spread (docs/SESSION.md: ODG by 2.4e-4, DI by 1.9e-3 here), the reference's gain to the chains'
            // (1e-5 dB); the rest exactly.
            ok (d.grade == 1 && d.windows == 1 && d.audibleWindows == 1 && d.ungradedWindows == 0
                && d.worstFromSeconds == 0.0 && d.audibleShare == 1.0
                && std::fabs (odg - -3.8399599584809163) <= 5e-4 && std::fabs (d.worstDi.value_or (0.0) - -3.3669999975374258) <= 5e-3
                && d.referenceGainDb && std::fabs (*d.referenceGainDb - -11.972735801256562) <= 1e-5
                && d.sourceLraLu == 0.0 && d.masterLraLu == 0.0 && d.lraChangeLu == 0.0 && ! d.lraChangePercent
                && d.lraReason == session::MeasurementReason::NoSignal && d.reason == session::MeasurementReason::None,
                "the damage of this source, its reference the whole referenceBelowDb lower: "
                "ODG " + std::to_string (odg) + ", DI " + std::to_string (d.worstDi.value_or (0.0)) + ", gain "
                + std::to_string (d.referenceGainDb.value_or (0.0)) + " dB");
        }
    }
    // THE LINE SAYS WHAT WAS GRADED: windows PEAQ could not grade (silence, nothing it reads) are no part of the verdict,
    // so a line never stretches it to the whole track — 30 s, five windows, two of them silent: "nothing heard" is said
    // of the three graded, and both languages name the three of five; a heard grade names its share of the graded ones.
    {
        using session::text::Lang;
        session::MasterDamage d;
        d.status = session::MeasurementStatus::Ready; d.reason = session::MeasurementReason::None;
        d.verdict = session::DamageVerdict::Transparent; d.grade = 5; d.worstOdg = 0.0; d.worstFromSeconds = 0.0;
        d.windows = 3; d.audibleWindows = 0; d.ungradedWindows = 2; d.audibleShare = 0.0;
        const auto quiet = session::MasterReportText::damage (d);
        const auto ru = session::text::Text::text (quiet, Lang::Ru), en = session::text::Text::text (quiet, Lang::En);
        d.verdict = session::DamageVerdict::Graded; d.grade = 3; d.worstOdg = -2.0; d.worstFromSeconds = 15.0;
        d.audibleWindows = 1; d.audibleShare = 1.0 / 3.0;
        const auto heard = session::MasterReportText::damage (d);
        const auto heardRu = session::text::Text::text (heard, Lang::Ru), heardEn = session::text::Text::text (heard, Lang::En);
        ok (quiet.id == FactId::MasterDamageInaudible && ru.find ("3 из 5") != std::string::npos && en.find ("3 of 5") != std::string::npos
            && ru.find ("трек") == std::string::npos && en.find ("track") == std::string::npos
            && heard.id == FactId::MasterDamage && heardRu.find ("3 из 5") != std::string::npos
            && heardEn.find ("3 of 5") != std::string::npos && heardRu.find ("с отметки 15") != std::string::npos,
            "the damage's line names the windows graded of all and never the whole track: «" + ru + "» / «" + en + "»; «"
            + heardRu + "» / «" + heardEn + "»");
    }
    // The loudness range of a programme too short to have one: the line says why, and no number is made up.
    {
        Case c; std::vector<session::Notification> events;
        const bool made = run (c, 48000, 48000, true, false, false, 2, 0, nullptr, 4096.0f, &events);
        ok (made, "a 2 s master is delivered");
        if (made)
        {
            const auto& d = c.session->masters()[0].report->damage;
            ok (! d.masterLraLu && ! d.lraChangeLu && ! d.lraChangePercent && d.lraReason == session::MeasurementReason::TooShort,
                "a programme too short for a loudness range: its change is absent, the reason TooShort");
            ok (has (factsOf (events), FactId::MasterLraUnmeasured), "the loudness range's line says why it was not measured");
            const auto said = session::MasterReportText::lra (d);
            ok (said.id == FactId::MasterLraUnmeasured && said.args[0].termId == session::text::Term::ReasonTooShort,
                "...with the reason as its term");
        }
    }
    // The current walk's share of the file, in every phase of a master that walks: 0..1, counting up within a walk; the
    // bookkeeping between two walks has none, so a new walk follows a step without one and starts from near 0; the
    // damage's two walks are phases of their own; the end and the waits have none.
    {
        Case c; std::vector<session::Notification> events;
        const bool made = run (c, 48000, 48000, true, true, false, 4, 0, nullptr, 4096.0f, &events, true);
        ok (made, "a master stepped one unit at a time is delivered");
        bool inRange = true, rising = true, reference = false, graded = false, finalHasNone = true, passes = false;
        double last = -1.0;
        for (const auto& e : events)
        {
            if (e.kind != session::EventKind::Phase) continue;
            const auto& p = e.payload.phase;
            // The master's phases: the measurement's and the needles' (Stream, Report, Analyzers) are not walked here.
            if (p.name == session::PhaseName::Stream || p.name == session::PhaseName::Report
                || p.name == session::PhaseName::Analyzers) continue;
            if (p.name == session::PhaseName::Final) { finalHasNone = finalHasNone && ! p.stepFraction; last = -1.0; continue; }
            if (! p.stepFraction) { last = -1.0; continue; }
            const double f = *p.stepFraction;
            inRange = inRange && f >= 0.0 && f <= 1.0;
            // Within a walk it never goes back; a new walk comes after a step that walks nothing, from its beginning.
            rising = rising && (last < 0.0 ? f < 0.05 : f >= last);
            last = f;
            reference = reference || (p.name == session::PhaseName::Reference && f > 0.9);
            graded = graded || (p.name == session::PhaseName::Damage && f > 0.9);
            passes = passes || (p.name == session::PhaseName::Pass && f > 0.9);
        }
        ok (made && inRange && rising && reference && graded && passes && finalHasNone,
            "stepFraction walks 0..1 within each walk, Reference and Damage are phases with their own walk, Final has none");
    }
}

// THE DAMAGE AFTER THE MASTER: the master is delivered when its own work ends, its report says the damage is Pending, and
// a job of its own grades it — announced by the Damage event right after Done, ended by it with the result. During its
// walks a new master is taken (the grade is parked and graded after: never ended by it), a cancel stops the damage alone,
// a forget stops it with its master; each says so.
std::size_t firstWalk (const std::vector<session::Notification>& events, session::PhaseName name)
{
    for (std::size_t i = 0; i < events.size(); ++i)
        if (events[i].kind == session::EventKind::Phase && events[i].payload.phase.name == name) return i;
    return events.size();
}
// Steps one unit at a time until the damage's second walk (PEAQ) has fed `past` of the programme: on a 16 s programme
// 0.7 is 11.2 s in, past the end of the first 10 s window, closed and graded — the walk midway, a window behind it.
session::JobId stepIntoGrading (session::Session& s, std::vector<session::Notification>& events, double past = 0.7)
{
    for (unsigned i = 0; i < 400000; ++i)
    {
        if (s.step (1).state == session::StepState::Done) return 0;
        for (const auto& e : s.events())
        {
            events.push_back (e);
            if (e.kind == session::EventKind::Phase && e.payload.phase.name == session::PhaseName::Damage
                && e.payload.phase.stepFraction && *e.payload.phase.stepFraction > past) return e.jobId;
        }
    }
    return 0;
}
// The damage job's events in a batch, in order: its kinds and, for a fact, its id.
std::vector<std::pair<session::EventKind, unsigned>> jobEvents (std::span<const session::Notification> batch, session::JobId job)
{
    std::vector<std::pair<session::EventKind, unsigned>> out;
    for (const auto& e : batch)
        if (e.jobId == job)
            out.emplace_back (e.kind, e.kind == session::EventKind::Fact ? unsigned (e.payload.fact.view().id) : 0u);
    return out;
}
void damageAfterMaster()
{
    using session::text::FactId;
    using session::EventKind;
    using session::MeasurementReason;
    using session::MeasurementStatus;
    // A master can finish while its source's optional analyzers still run. The grade then waits for those measurements,
    // and changes to waiting for its turn when they end (or are cancelled), before its first walk starts.
    {
        Case c;
        const bool made = run (c, 48000, 48000, false, false, false, 4, 0, nullptr, 4096.0f, nullptr, false, true, false, false);
        ok (made, "a master is available before its source analyzers finish");
        if (made)
        {
            auto& s = *c.session;
            ok (! s.damageJobs().empty() && s.measurementJob() != 0
                && s.damageJobs()[0].waitReason == session::DamageWaitReason::SourceMeasurement,
                "a grade held by source measurement says why it waits");
            (void) s.apply (session::command::Cancel { 81, s.measurementJob() });
            ok (s.damageJobs()[0].waitReason == session::DamageWaitReason::Queue,
                "after source measurement stops the grade waits only for its queue turn");
            for (unsigned i = 0; i < 400000 && s.damageJob() == 0; ++i) (void) s.step (1);
            ok (s.damageJob() != 0 && ! s.damageJobs()[0].waitReason, "a running grade has no wait reason");
        }
    }
    // Delivered first: at the master's Done the report says Pending, the master's PCM is there, and no walk of the damage
    // has run; then its job announces itself, walks, and ends with the grade.
    {
        Case c; std::vector<session::Notification> events;
        const bool made = run (c, 48000, 48000, true, true, true, 4, 0, nullptr, 4096.0f, &events, false, true, false, false);
        ok (made, "a processed master is delivered with its damage still to grade");
        if (made)
        {
            auto& s = *c.session;
            const auto master = s.masters()[0].id;
            const auto& d = s.masters()[0].report->damage;
            std::size_t done = events.size();
            for (std::size_t i = 0; i < events.size(); ++i)
                if (events[i].kind == EventKind::Done) done = i;
            ok (done < events.size() && d.status == MeasurementStatus::Pending && d.reason == MeasurementReason::Pending
                && firstWalk (events, session::PhaseName::Reference) == events.size()
                && firstWalk (events, session::PhaseName::Damage) == events.size() && ! c.delivered.empty(),
                "the master is delivered before its damage is graded: Done, the PCM, the report Pending, no walk of it yet");
            bool pendingLine = false;
            for (const auto& e : events)
                if (e.kind == EventKind::Fact && e.jobId == master && e.payload.fact.view().id == FactId::MasterDamageUnmeasured)
                    pendingLine = e.payload.fact.view().args[0].termId == session::text::Term::ReasonPending;
            // The grade the shell asked after Done (command::GradeDamage) waits its turn: its own job, announced Pending.
            const auto job = s.damageJobs().empty() ? 0u : s.damageJobs()[0].job;
            const auto view = s.snapshot();
            std::size_t announced = events.size();
            for (std::size_t i = 0; i < events.size() && announced == events.size(); ++i)
                if (events[i].jobId == job) announced = i;
            ok (pendingLine && job != 0 && job != master && done < announced && announced < events.size()
                && events[announced].kind == EventKind::Damage && events[announced].payload.damage.masterId == master
                && events[announced].payload.damage.status == MeasurementStatus::Pending
                && view.view().damageJob == 0 && view.view().damageJobs.size() == 1 && view.view().damageJobs[0].job == job
                && view.view().damageJobs[0].masterId == master && view.view().damageJobs[0].state == session::DamageJobState::Waiting
                && view.view().masters[0].report->damage.status == MeasurementStatus::Pending,
                "the master's damage line says Pending; the grade asked after Done is announced by its own job's Damage event "
                "and waits its turn in the snapshot's damageJobs");
            // The needles the demanding target asked for run first: the damage waits behind every other work.
            std::vector<session::Notification> after;
            bool behind = true;
            for (unsigned i = 0; i < 400000 && ! s.damageJobs().empty(); ++i)
            {
                (void) s.step (7);
                for (const auto& e : s.events())
                {
                    if (e.jobId == job) after.push_back (e);
                    else behind = behind && after.size() <= 1u;
                }
            }
            bool walked = false, graded = false;
            for (const auto& e : after)
                if (e.kind == EventKind::Phase && e.payload.phase.stepFraction)
                {
                    walked = walked || e.payload.phase.name == session::PhaseName::Reference;
                    graded = graded || e.payload.phase.name == session::PhaseName::Damage;
                }
            const auto n = after.size();
            ok (s.damageJob() == 0 && behind && walked && graded && n >= 3 && after[n - 1].kind == EventKind::Damage
                && after[n - 1].payload.damage.masterId == master && after[n - 1].payload.damage.status == MeasurementStatus::Ready
                && after[n - 2].kind == EventKind::Fact && after[n - 2].payload.fact.view().id == FactId::MasterDamage
                && after[n - 3].kind == EventKind::Phase && after[n - 3].payload.phase.name == session::PhaseName::Final
                && d.status == MeasurementStatus::Ready && d.grade == 1 && s.step (1).state == session::StepState::Done,
                "the damage's job walks its two phases, then says the grade and ends with the Damage event, Ready");
        }
    }
    // A NEW MASTER NEVER ENDS A GRADE (owner, 04.10: "it is a process of its own"). Asked midway through the damage's
    // PEAQ walk, a window graded behind it: taken at once; the running grade is parked — its walks freed, its progress
    // back at its start, no line and no Damage event — and waits first. Both grades then run to their end, in the order
    // their masters were delivered: the first master's grade Ready before the second's.
    {
        Case c;
        const bool made = run (c, 48000, 48000, true, true, true, 16, 0, nullptr, 4096.0f, nullptr, false, true, false, false);
        ok (made, "a master is delivered, its damage pending");
        if (made)
        {
            auto& s = *c.session;
            std::vector<session::Notification> events;
            const auto grading = stepIntoGrading (s, events);
            const auto first = s.masters()[0].id;
            // The shell takes the PCM as it comes: the next master is asked with none pending.
            const bool released = s.releaseMaster (c.token) == session::MasterTransferStatus::Ok;
            auto request = c.request;
            request.id = 30; request.source = s.source().hash; request.revision = s.revision();
            const auto checked = s.check (request);
            session::Answer answer;
            const auto spent = declared::spend ([&] { answer = s.apply (request); });
            bool ended = false, reset = false;
            for (const auto& e : s.events())
                if (e.jobId == grading)
                {
                    ended = ended || e.kind == EventKind::Damage || e.kind == EventKind::Fact;
                    reset = reset || (e.kind == EventKind::Phase && e.payload.phase.completedUnits == 0
                                      && e.payload.phase.name == session::PhaseName::Reference);
                }
            const auto queued = s.damageJobs();
            const bool parked = queued.size() == 1 && queued[0].job == grading && queued[0].masterId == first
                && queued[0].state == session::DamageJobState::Waiting && queued[0].progress.completedUnits == 0;
            ok (grading != 0 && released && answer.rejection == session::Rejection::None && ! ended && reset && parked
                && s.damageJob() == 0 && s.masters()[0].report->damage.status == MeasurementStatus::Pending
                && checked.releasedBytes > 0 && declared::covers (checked.bytes, spent),
                "a master asked during the damage's walks is taken; that grade is parked — freed, waiting first, no line, "
                "no Damage event — never Superseded");
            // The second master delivered, the shell asks its grade too; then to the end: each grade's Damage event, in
            // order.
            std::vector<session::Notification> after;
            for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
            {
                (void) s.step (73);
                for (const auto& e : s.events()) after.push_back (e);
            }
            const bool asked = s.masters().size() == 2
                && s.apply (session::command::GradeDamage { 31, s.masters()[1].id }).rejection == session::Rejection::None;
            for (const auto& e : s.events()) after.push_back (e);
            for (unsigned i = 0; i < 400000 && s.step (73).state == session::StepState::More; ++i)
                for (const auto& e : s.events()) after.push_back (e);
            for (const auto& e : s.events()) after.push_back (e);
            std::vector<std::pair<session::JobId, MeasurementStatus>> closes;
            for (const auto& e : after)
                if (e.kind == EventKind::Damage) closes.emplace_back (e.jobId, e.payload.damage.status);
            ok (asked && s.masters().size() == 2 && s.masters()[0].report->damage.status == MeasurementStatus::Ready
                && s.masters()[1].report->damage.status == MeasurementStatus::Ready
                && closes.size() == 3 && closes[0].second == MeasurementStatus::Pending && closes[0].first > grading
                && closes[1].first == grading && closes[1].second == MeasurementStatus::Ready
                && closes[2].first == closes[0].first && closes[2].second == MeasurementStatus::Ready
                && s.damageJobs().empty(),
                "both grades arrive, in the order asked: the second announced Pending, the first graded Ready, then the "
                "second Ready");
        }
    }
    // Cancelled by its id midway through its PEAQ walk: the master stays whole, its PCM still there; the damage says
    // Cancelled — the cancel's fact, its line, then the damage event, which closes the job.
    {
        Case c;
        const bool made = run (c, 48000, 48000, true, true, true, 16, 0, nullptr, 4096.0f, nullptr, false, true, false, false);
        if (made)
        {
            auto& s = *c.session;
            std::vector<session::Notification> events;
            const auto grading = stepIntoGrading (s, events);
            const auto answer = s.apply (session::command::Cancel { 31, grading });
            bool said = false;
            for (const auto& e : s.events())
                if (e.kind == EventKind::Damage)
                    said = e.payload.damage.status == MeasurementStatus::Cancelled && e.payload.damage.reason == MeasurementReason::Cancelled;
            const auto order = jobEvents (s.events(), grading);
            const bool closed = order.size() == 3 && order[0].second == unsigned (FactId::Cancelled)
                && order[1].second == unsigned (FactId::MasterDamageUnmeasured) && order[2].first == EventKind::Damage;
            std::vector<float> copy (c.delivered.size());
            bool quiet = true, more = true;
            for (unsigned i = 0; i < 400000 && more; ++i)
            {
                more = s.step (73).state == session::StepState::More;
                for (const auto& e : s.events()) quiet = quiet && e.jobId != grading;
            }
            ok (grading != 0 && answer.rejection == session::Rejection::None && said && closed && s.damageJob() == 0
                && s.masters().size() == 1 && s.masters()[0].report->damage.status == MeasurementStatus::Cancelled
                && s.copyMaster (c.token, copy) == session::MasterTransferStatus::Ok && copy == c.delivered && quiet,
                "cancelling the damage's job midway stops the damage alone: the master and its PCM stay, the report says "
                "Cancelled, and the damage event is the job's last word");
        }
        else ok (false, "a master is delivered for the cancel");
    }
    // Forgotten midway through its damage's PEAQ walk: the job stops with it, without a line for a master no longer kept.
    {
        Case c;
        const bool made = run (c, 48000, 48000, true, true, true, 16, 0, nullptr, 4096.0f, nullptr, false, true, false, false);
        if (made)
        {
            auto& s = *c.session;
            std::vector<session::Notification> events;
            const auto grading = stepIntoGrading (s, events);
            const auto master = s.masters()[0].id, job = s.damageJob();
            const auto answer = s.apply (session::command::Forget { 32, master });
            bool line = false, said = false;
            for (const auto& e : s.events())
            {
                line = line || e.kind == EventKind::Fact;
                said = said || (e.kind == EventKind::Damage && e.jobId == job && e.payload.damage.masterId == master
                                && e.payload.damage.status == MeasurementStatus::Cancelled);
            }
            bool quiet = true, more = true;
            for (unsigned i = 0; i < 400000 && more; ++i)
            {
                more = s.step (73).state == session::StepState::More;
                for (const auto& e : s.events()) quiet = quiet && e.jobId != job;
            }
            ok (job != 0 && grading == job && answer.rejection == session::Rejection::None && said && ! line && s.damageJob() == 0
                && s.masters().empty() && quiet,
                "forgetting a master whose damage is graded stops that job, with its event and no line");
        }
        else ok (false, "a master is delivered for the forget");
    }
}

int main()
{
    damage();
    damageAfterMaster();
    Case normalFirst, normalLate, fractionalFirst, fractionalLate;
    ok (run (normalFirst, 48000, 48000, false, true, false, 4, 0, nullptr, 512.0f)
        && referenceCrest (normalFirst) && crestK1Ready (normalFirst),
        "normal-level source-first crest retains the configured floor and K1 comparison");
    if (normalFirst.session && ! normalFirst.session->masters().empty())
    {
        const auto snapshot = normalFirst.session->snapshot();
        const auto source = session::MeasurementCrest::view (
            snapshot.view().measurements[std::size_t (session::Analyzer::Crest)]);
        ok (source.activityFloorDb > source.parameters.programmeFloorDb + 1.0
            && source.parameters.programmeFloorDb == -70.0,
            "configured crest floor survives separately from the effective activity threshold");
        // The master's readings come with its report, as MasterReportText states them from the report and the landing's
        // passes; they cross the wire whole, and an unknown kind is refused.
        const auto& kept = normalFirst.session->masters()[0];
        const auto again = session::MasterReportText::readings (*kept.report, kept.landing->passes);
        bool same = kept.report->readings.count == again.count && again.count >= 6;
        for (std::size_t i = 0; same && i < again.count; ++i)
            same = kept.report->readings.items[i].kind == again.items[i].kind
                && std::bit_cast<std::uint64_t> (kept.report->readings.items[i].fact.args[0].number)
                   == std::bit_cast<std::uint64_t> (again.items[i].fact.args[0].number);
        const auto& first = kept.report->readings.items[0];
        ok (same && first.kind == session::ReadingKind::Integrated && kept.report->achievedLufs
            && std::bit_cast<std::uint64_t> (first.fact.args[0].number) == std::bit_cast<std::uint64_t> (*kept.report->achievedLufs)
            && first.fact.args[0].unit == session::text::Unit::Lufs && first.fact.args[0].precision == 1,
            "the master's readings come with its report: the achieved loudness first, LUFS to a tenth");
        std::string json (std::size_t (session::Codec::encodedBytes (snapshot.view()).bytes), '\0');
        session::Snapshot restored;
        const bool coded = session::Codec::encode (snapshot.view(), json) == session::CodecStatus::Ok
            && session::Codec::decode (json, restored) == session::CodecStatus::Ok && ! restored.view().masters.empty()
            && restored.view().masters[0].report;
        bool carried = coded && restored.view().masters[0].report->readings.count == kept.report->readings.count;
        for (std::size_t i = 0; carried && i < kept.report->readings.count; ++i)
            carried = restored.view().masters[0].report->readings.items[i].kind == kept.report->readings.items[i].kind
                && session::text::Text::text (restored.view().masters[0].report->readings.items[i].fact, session::text::Lang::Ru)
                   == session::text::Text::text (kept.report->readings.items[i].fact, session::text::Lang::Ru);
        const auto at = json.find ("\"readings\":[{\"fact\":{\"FactId\":");
        ok (carried && at != std::string::npos, "the master's readings cross the wire whole");
        if (at != std::string::npos)
        {
            const auto kindAt = json.find ("]},\"kind\":", at) + 10, kindEnd = json.find ('}', kindAt);
            auto badKind = json; badKind.replace (kindAt, kindEnd - kindAt, "31");
            session::Snapshot refused;
            ok (session::Codec::decode (badKind, refused) == session::CodecStatus::Invalid, "a master's unknown reading kind (31) is refused");
        }
    }
    ok (run (normalLate, 48000, 48000, false, false, false, 4, 0, nullptr, 512.0f)
        && lateCrestAfterRelease (normalLate),
        "normal-level late crest joins after PCM release with configured floor");
    const bool fractionalFirstRan = run (fractionalFirst, 22050, 48000, false, true, false, 8);
    ok (fractionalFirstRan && referenceCrest (fractionalFirst) && crestK1Ready (fractionalFirst),
        "22.05 kHz source-first crest uses the 2210-sample hop and configured duration");
    if (fractionalFirstRan)
    {
        const auto snapshot = fractionalFirst.session->snapshot();
        const auto source = session::MeasurementCrest::view (
            snapshot.view().measurements[std::size_t (session::Analyzer::Crest)]);
        ok (source.hopSamples == 2210 && source.parameters.hopMs == 100.0
            && double (source.hopSamples) * 1000.0 / source.sampleRate > 100.2,
            "configured hop duration survives separately from the 22.05 kHz sample grid");
    }
    ok (run (fractionalLate, 22050, 48000, false, false, false, 8)
        && lateCrestAfterRelease (fractionalLate),
        "22.05 kHz late crest joins after PCM release on the same sample grid");
    declared::LifecycleBudget shortDeclaration { 96, 96 };
    ok (declared::covers (96, { 1, 64 }) && shortDeclaration.charge ({ 1, 64 })
        && ! shortDeclaration.charge ({ 1, 64 }),
        "lifecycle control rejects aggregate demand that separate call checks miss");
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
        const auto spent = declared::spend ([&] { copy = gain.session->snapshot(); });
        ok (declared::covers (snapshotBytes, spent)
            && copy.view().masters[0].report->crest.rows.size() == crest.rows.size()
            && copy.view().masters[0].report->cost
            && ! copy.view().masters[0].report->cost->waveform.empty(),
            "owned snapshot retains compact cost and waveform rows inside declared memory");
        const auto needed = session::Codec::encodedBytes (copy.view());
        std::vector<char> json (std::size_t (needed.bytes));
        session::Snapshot decoded;
        const auto decodedNeed = needed.status == session::CodecStatus::Ok
            && session::Codec::encode (copy.view(), json) == session::CodecStatus::Ok
            ? session::Codec::decodedBytes ({ json.data(), json.size() }) : session::CodecNeed {};
        session::CodecStatus status = session::CodecStatus::Invalid;
        const auto decodedSpent = declared::spend ([&] {
            status = session::Codec::decode ({ json.data(), json.size() }, decoded);
        });
        ok (status == session::CodecStatus::Ok && declared::covers (decodedNeed.bytes, decodedSpent)
            && decoded.view().masters[0].report->crest.rows.size() == crest.rows.size()
            && decoded.view().masters[0].report->deliverable == gain.session->masters()[0].report->deliverable
            && decoded.view().masters[0].report->cost
            && decoded.view().masters[0].report->cost->waveform.size()
                == gain.session->masters()[0].report->cost->waveform.size()
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
        const auto invalidSpent = declared::spend ([&] {
            invalidStatus = session::Codec::decode (malformed, invalid);
        });
        ok (key != std::string::npos && invalidStatus == session::CodecStatus::Invalid
            && invalidSpent.requests == 0
            && session::Codec::decodedBytes (malformed).status == session::CodecStatus::Invalid,
            "an inconsistent crest row total is rejected before decode allocates");
        std::string shiftedWave (json.data(), json.size());
        constexpr std::string_view waveStart = "\"waveform\":[{";
        const auto waveAt = shiftedWave.find (waveStart);
        constexpr std::string_view waveFrom = "\"fromFrame\":\"0\"";
        const auto waveFromAt = waveAt == std::string::npos ? waveAt
            : shiftedWave.find (waveFrom, waveAt + waveStart.size());
        if (waveFromAt != std::string::npos)
            shiftedWave[waveFromAt + waveFrom.size() - 2u] = '1';
        const auto waveSpent = declared::spend ([&] {
            invalidStatus = session::Codec::decode (shiftedWave, invalid);
        });
        ok (waveAt != std::string::npos && waveFromAt != std::string::npos
            && invalidStatus == session::CodecStatus::Invalid
            && waveSpent.requests == 0
            && session::Codec::decodedBytes (shiftedWave).status == session::CodecStatus::Invalid,
            "a shifted compact waveform is rejected during the allocation-free parse");
        std::string incoherent (json.data(), json.size());
        const auto met = gain.session->masters()[0].report->targetMet;
        const auto field = std::string ("\"targetMet\":") + (met ? "true" : "false");
        const auto at = incoherent.find (field);
        if (at != std::string::npos)
            incoherent.replace (at, field.size(), std::string ("\"targetMet\":") + (met ? "false" : "true"));
        const auto mismatchSpent = declared::spend ([&] {
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
        grid = altered; grid.parameters.programmeFloorDb += 1.0;
        ok (! session::MasterCrestGrid::compatible (crest, grid, altered.parameters),
            "late crest pairing checks the source programme gate");
        grid = altered; grid.parameters.bandShareFloorDb += 1.0;
        ok (! session::MasterCrestGrid::compatible (crest, grid, altered.parameters),
            "late crest pairing checks the source band gate");
        grid = altered; grid.parameters.hopMs += 0.01;
        ok (! session::MasterCrestGrid::compatible (crest, grid, altered.parameters),
            "late crest pairing checks the configured hop duration");
        grid = altered; grid.droppedHops = 1;
        ok (! session::MasterCrestGrid::compatible (crest, grid),
            "crest pairing refuses incomplete source rows");
        const auto& cost = *gain.session->masters()[0].report->cost;
        ok (cost.k2Reason == session::MeasurementReason::NotImplemented
            && cost.crestFullDb.value && *cost.crestFullDb.value < .05
            && cost.shapeP95Lu.value && *cost.shapeP95Lu.value < .1
            && cost.sections.size() <= 2 && cost.largestSectionShiftLu.value
            && ! cost.worstSectionIndex,
            "gain-only master has measured near-zero impact and shape while K2 remains unmeasured");
        session::MeasurementQuery waveform;
        waveform.kind = session::QueryKind::MasterWaveform;
        waveform.audioId = gain.session->source().hash;
        waveform.masterId = gain.session->masters()[0].id;
        waveform.fromFrame = 100; waveform.toFrame = 200; waveform.columns = 10;
        const auto compact = gain.session->query (waveform);
        ok (compact.view().status == session::QueryStatus::Ready
            && compact.view().pcmFramesRead == 0 && compact.view().stored > 0
            && compact.view().stored <= 20
            && compact.view().values[0] <= 100 && compact.view().values[1] >= 100,
            "retained master waveform answers zoom with actual compact bucket bounds");
    }
    Case late;
    ok (run (late, 48000, 48000, false, false),
        "master can finish while the optional source crest is still pending");
    if (late.session && ! late.session->masters().empty())
    {
        const auto masterId = late.session->masters()[0].id;
        ok (late.session->masters()[0].report->crest.status == session::MeasurementStatus::Pending
            && late.session->pendingMaster().master != 0,
            "pending crest is distinct from an unavailable master");
        const auto measuring = late.session->measurementJob();
        if (measuring != 0) (void) late.session->apply (session::command::Cancel { 3, measuring });
        bool unavailableEvent = false;
        for (unsigned i = 0; i < 8 && ! unavailableEvent; ++i)
        {
            for (const auto& event : late.session->events())
                if (event.jobId == masterId && event.kind == session::EventKind::Fact
                    && event.payload.fact.view().id == session::text::FactId::MasterCrestUnavailable)
                    unavailableEvent = true;
            if (! unavailableEvent) (void) late.session->step (1);
        }
        ok (late.session->masters()[0].report->crest.status == session::MeasurementStatus::Unavailable
            && late.session->masters()[0].report->crest.reason == session::MeasurementReason::Cancelled
            && late.session->pendingMaster().master != 0 && unavailableEvent,
            "final source crest refusal emits a master-keyed update and keeps safe PCM");
        const auto& cost = *late.session->masters()[0].report->cost;
        ok (cost.crestLowDb.reason == session::MeasurementReason::Cancelled
            && cost.crestLowMidDb.reason == session::MeasurementReason::Cancelled
            && cost.crestHighMidDb.reason == session::MeasurementReason::Cancelled
            && cost.crestHighDb.reason == session::MeasurementReason::Cancelled
            && cost.crestFullDb.reason == session::MeasurementReason::Cancelled,
            "late source cancellation settles all pending crest cost bands");
    }
    Case lateReady;
    ok (run (lateReady, 48000, 48000, false, false),
        "a second master can finish before the source crest without waiting for it");
    if (lateReady.session && ! lateReady.session->masters().empty())
    {
        ok (lateReady.session->masters()[0].report->crest.status == session::MeasurementStatus::Pending,
            "the unfinished source comparison stays pending");
        const auto achievedBefore = *lateReady.session->masters()[0].report->achievedLufs;
        const auto peakBefore = *lateReady.session->masters()[0].report->truePeakDbTp;
        const auto passBefore = lateReady.session->masters()[0].landing->passes;
        const auto released = lateReady.session->releaseMaster (lateReady.token);
        ok (released == session::MasterTransferStatus::Ok
            && lateReady.session->pendingMaster().master == 0
            && lateReady.session->masterAudioShape (lateReady.token).frames == 0,
            "late crest can join after the delivered PCM has been released");
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
        bool joinAllocated = false, joinOverran = false, joinedEvent = false;
        for (unsigned i = 0; i < 200000 && ! joinedEvent; ++i)
        {
            session::Stepped joined;
            const auto spent = declared::spend ([&] { joined = lateReady.session->step (1); });
            joinAllocated = joinAllocated || spent.requests != 0;
            joinOverran = joinOverran || joined.units > 1;
            for (const auto& event : lateReady.session->events())
                if (event.jobId == lateReady.session->masters()[0].id
                    && event.kind == session::EventKind::Fact
                    && event.payload.fact.view().id == session::text::FactId::MasterCrestDelivered)
                    joinedEvent = true;
        }
        ok (lateReady.session->masters()[0].report->crest.status == session::MeasurementStatus::Ready
            && referenceCrest (lateReady) && lateReady.session->pendingMaster().master == 0
            && lateReady.session->revision() > beforeJoin && ! joinAllocated && ! joinOverran && joinedEvent,
            "late source crest emits a master-keyed update in bounded row batches after PCM release");
        ok (std::bit_cast<std::uint64_t> (*lateReady.session->masters()[0].report->achievedLufs)
                == std::bit_cast<std::uint64_t> (achievedBefore)
            && std::bit_cast<std::uint64_t> (*lateReady.session->masters()[0].report->truePeakDbTp)
                == std::bit_cast<std::uint64_t> (peakBefore)
            && lateReady.session->masters()[0].landing->passes == passBefore,
            "the late join leaves delivered LUFS, true peak and landing passes unchanged");
        std::uint64_t lateDigest = 0xcbf29ce484222325ull;
        const auto addLate = [&] (std::uint64_t value) noexcept
        {
            for (unsigned byte = 0; byte < 8; ++byte)
                lateDigest = (lateDigest ^ std::uint8_t (value >> (byte * 8u))) * 0x100000001b3ull;
        };
        const auto& settled = lateReady.session->masters()[0].report->crest;
        addLate (std::uint64_t (settled.status));
        addLate (settled.sampleRateHz);
        addLate (settled.blocks);
        for (const double value : settled.rows) addLate (std::bit_cast<std::uint64_t> (value));
        for (const double value : settled.sourceMask) addLate (std::bit_cast<std::uint64_t> (value));
        std::printf ("master-late-crest-digest=%016llx\n", (unsigned long long) lateDigest);
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
    Case cd;
    ok (run (cd, 48000, 44100, true, true, false, 4, 0, "cdDynamic")
        && referenceCrest (cd) && cd.session->masters()[0].report->checkPasses == 1,
        "CD rate conversion retains one source-rate crest pass outside the landing budget");
    Case differentTargets;
    ok (run (differentTargets, 44100, 48000, false),
        "a rate-changed non-CD target completes its source-rate check");
    if (differentTargets.session && differentTargets.session->masters().size() == 1)
    {
        auto& s = *differentTargets.session;
        const auto first = s.masters()[0].id;
        const auto firstRateCheck = s.masters()[0].report->checkPasses;
        const auto firstPasses = s.masters()[0].landing->passes;
        const auto released = s.releaseMaster (differentTargets.token);
        // The other 48 kHz target, held to this material's loudness by an edit: another recipe, the same rate change.
        const auto changed = s.apply (session::command::SetTarget { 80, "youtubeMusic" });
        const auto edited = s.apply (session::command::EditTarget { 83, { -14.0, -1.0 } });
        auto request = differentTargets.request;
        request.id = 81; request.source = s.source().hash; request.revision = s.revision();
        const auto next = s.apply (request);
        for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) (void) s.step (73);
        ok (released == session::MasterTransferStatus::Ok && changed.rejection == session::Rejection::None
            && edited.rejection == session::Rejection::None
            && next.rejection == session::Rejection::None && s.masters().size() == 2
            && s.masters()[0].id == first && s.masters()[1].id == next.job
            && s.masters()[0].recipe.source == s.masters()[1].recipe.source
            && s.masters()[0].recipe.project.target != s.masters()[1].recipe.project.target
            && firstRateCheck == 1 && s.masters()[0].report->checkPasses == 1
            && s.masters()[1].report->checkPasses == 1
            && s.masters()[0].landing->passes == firstPasses
            && s.masters()[1].report->crest.sourceRateCheck,
            "two different target recipes each retain exactly one source-rate check outside landing");
        differentTargets.left[0] += .01f;
        const float* newPlanes[2] { differentTargets.left.data(), differentTargets.right.data() };
        const auto replacement = s.apply (session::command::Load { 82,
            { newPlanes, 2, differentTargets.left.size(), 44100 },
            { "replacement.wav", 44100, true, 24 } });
        bool staleFact = false;
        for (unsigned i = 0; i < 200000 && s.measurementJob() != 0; ++i)
        {
            (void) s.step (73);
            for (const auto& event : s.events())
                if (event.jobId == first || event.jobId == next.job) staleFact = true;
        }
        ok (replacement.rejection == session::Rejection::None && s.masters().empty()
            && s.source().hash != differentTargets.request.source && ! staleFact,
            "replacing the source removes both recipes and cannot publish a stale crest join");
    }
    Case silentPrefix;
    ok (run (silentPrefix, 48000, 48000, true, true, false, 8, 3),
        "a master with a long digital-silence prefix completes");
    if (silentPrefix.session && ! silentPrefix.session->masters().empty())
    {
        const auto& cost = *silentPrefix.session->masters()[0].report->cost;
        const auto& shape = silentPrefix.session->masterAudioShape (silentPrefix.token);
        ok (cost.activeWindowShare.value && *cost.activeWindowShare.value < .9
            && cost.activeWindowShare.compared > 60,
            "valid silent momentary windows count as inactive");
        ok (cost.limiterActiveShare.value && cost.limiterActiveShare.compared > 0
            && cost.limiterActiveShare.compared < shape.frames * 4u,
            "limiter activity uses input-gated windows and their accepted frame count");
    }
    Case repeat;
    ok (run (repeat, 44100, 48000, true) && up.session && ! up.session->masters().empty()
        && repeat.delivered == up.delivered
        && repeat.session->masters()[0].report->crest.rows.size() == up.session->masters()[0].report->crest.rows.size()
        && std::memcmp (repeat.session->masters()[0].report->crest.rows.data(),
            up.session->masters()[0].report->crest.rows.data(),
            up.session->masters()[0].report->crest.rows.size_bytes()) == 0,
        "a second session repeats the delivered PCM and source-rate crest exactly");
    Case missed;
    std::vector<session::Notification> wallEvents;
    ok (run (missed, 48000, 48000, false, true, true, 4, 0, nullptr, 4096.0f, &wallEvents),
        "a demanding target retains a checked ceiling-safe result at the limiter wall");
    if (missed.session && ! missed.session->masters().empty())
    {
        const auto& kept = missed.session->masters()[0];
        const auto& report = *kept.report;
        const auto verdict = session::MasterReportText::landing (report, *kept.landing, .1);
        bool plain = false, detail = false, mixHint = false;
        for (const auto& event : wallEvents) if (event.kind == session::EventKind::Fact)
        {
            const auto fact = event.payload.fact.view();
            plain = plain || fact.id == session::text::FactId::MasterLandingWall;
            detail = detail || fact.id == session::text::FactId::MasterLandingWallDetail;
            mixHint = mixHint || fact.id == session::text::FactId::MasterLandingMiss;
        }
        std::printf ("    wall report: status %u, wall %d, target %d, safe %d, hints %d/%d, verdict %u, events %d/%d/%d\n",
            unsigned (kept.landing->status), kept.landing->limiterWall, report.targetMet, report.peakSafe,
            bool (report.firstHint), bool (report.secondHint), verdict ? unsigned (verdict->id) : 0, plain, detail, mixHint);
        ok (kept.landing->status == session::LandingStatus::TargetUnreachable && kept.landing->limiterWall
            && ! report.targetMet && report.peakSafe && ! report.firstHint && ! report.secondHint
            && verdict && verdict->id == session::text::FactId::MasterLandingWall
            && ! session::text::Text::text (*verdict, session::text::Lang::Ru).empty()
            && ! session::text::Text::text (*verdict, session::text::Lang::En).empty()
            && plain && detail && ! mixHint,
            "the limiter wall says why in ru/en, logs its numbers and blames no mix problem");
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
        if (r.cost && r.cost->crestFullDb.value && r.cost->shapeP95Lu.value
            && r.cost->pumpingRmsDb.value && r.cost->activeWindowShare.value)
            std::printf ("master-cost-parity %d %.12f %.12f %.12f %.12f\n",
                parityCase, *r.cost->crestFullDb.value, *r.cost->shapeP95Lu.value,
                *r.cost->pumpingRmsDb.value, *r.cost->activeWindowShare.value);
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
    ok (memoryLifecycle (gain), "declared peak and largest block cover large, cancel, small, growth and refusal");
    Case longTrace;
    ok (run (longTrace, 48000, 48000, false, true, false, 16)
        && longTrace.session && ! longTrace.session->masters().empty()
        && longTrace.session->masters()[0].landing->limiterTrace
        && longTrace.session->masters()[0].landing->limiterTrace->columns > 1000
        && memoryLifecycle (longTrace),
        "long trace lifecycle bounds cumulative allocations through cancellation and growth");
    if (gain.session && ! gain.session->masters().empty())
    {
        session::MeasurementQuery waveform;
        waveform.kind = session::QueryKind::MasterWaveform;
        waveform.audioId = gain.session->source().hash;
        waveform.masterId = gain.session->masters()[0].id;
        waveform.fromFrame = 100; waveform.toFrame = 200; waveform.columns = 10;
        const float* piece[2] { gain.delivered.data() + 100,
            gain.delivered.data() + gain.delivered.size() / 2u + 100 };
        const session::Pcm chunk { piece, 2, 100, 48000 };
        const auto chunkNeed = gain.session->masterWaveformChunkStorage (waveform, chunk);
        const auto exact = gain.session->masterWaveformChunk (waveform, chunk);
        ok (chunkNeed.status == session::QueryStatus::Ready
            && exact.view().status == session::QueryStatus::Ready
            && exact.view().pcmFramesRead == 100 && exact.view().stored == 20
            && exact.view().values[0] == 100 && exact.view().values[1] == 110,
            "explicit external PCM chunk supports exact deep zoom after heap release");
        const session::Pcm wrongChannels { piece, 1, 100, 48000 };
        ok (gain.session->masterWaveformChunkStorage (waveform, wrongChannels).status
                == session::QueryStatus::Contract,
            "external chunk must match the delivered master's channel count");
    }
    // THE WAV TAKES THE FROZEN TARGET'S BIT DEPTH: the completion hands the recipe to the kept master before the file
    // is planned, so the depth is the job's own and not a default recipe's first row.
    // ...and its rate: cd and cdDynamic deliver 44.1 kHz from a 48 kHz source, with the source-rate crest pass that
    // any rate change takes (decision 2.3); a target of sampleRate 0 keeps the source's rate and takes no such pass.
    for (const auto& [target, bits, rate] : { std::tuple<const char*, std::uint32_t, std::uint32_t> { "cd", 16u, 44100u },
                                              { "cdDynamic", 16u, 44100u }, { "spotify", 24u, 48000u } })
    {
        Case delivered;
        const bool ran = run (delivered, 48000, 0, false, true, false, 4, 0, target, 512.0f);
        const auto plan = ran ? delivered.session->masterWavPlan (delivered.token) : session::WavPlan {};
        const auto& report = ran ? delivered.session->masters()[0].report : std::optional<session::MasterReport> {};
        ok (ran && plan && plan.bits == bits && plan.rate == rate && report
            && report->checkPasses == (rate != 48000u ? 1u : 0u) && report->crest.sourceRateCheck == (rate != 48000u),
            std::string (target) + ": the WAV is " + std::to_string (bits) + "-bit PCM at " + std::to_string (rate)
            + " Hz, the target's format, with " + (rate != 48000u ? "one" : "no") + " source-rate crest pass (got "
            + std::to_string (plan.bits) + " bits at " + std::to_string (plan.rate) + " Hz)");
    }
    Case resumed;
    ok (run (resumed, 48000, 48000, false, false, false, 4, 0, nullptr, 512.0f) && resumedBeforeJoin (resumed),
        "a source measurement cancelled and resumed before the join runs leaves the late crest pending, then joins");
    std::string once;
    ok (joinedOnce (once), "a crest joined inside the job is not joined or published again (" + once + ")");
    std::string lateOnce;
    ok (lateCrestSaidOnce (lateOnce), "a crest pending when the master ends is said once, by the late join (" + lateOnce + ")");
    costLinesAndReadings();
    return felitronics::test::report();
}
