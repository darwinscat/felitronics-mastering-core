// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include "Driver.h"
#include "Damage.h"
#include "MasterJob.h"
#include "Rules.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace declared = felitronics::declared;
namespace felitronics::session::detail
{
struct Inspector
{
    static bool noMasterOwners (const Session& s) noexcept
    {
        return ! s.masterJob_ && ! s.masterAudio_.samples && ! s.masterRows_ && ! s.masters_
            && s.masterRoom_ == 0 && s.masterCount_ == 0 && s.masterJobBytes_ == 0
            && ! s.lateMasterJob_ && s.lateMasterId_ == 0 && s.lateMasterJobBytes_ == 0
            && ! s.damageJob_ && s.damageJobId_ == 0 && s.damageJobBytes_ == 0;
    }
    static void lastJob (Session& s, JobId last) noexcept { s.lastJob_ = last; }
    static std::uint64_t damageBytes (const Session& s) noexcept { return s.damageJobBytes_; }
    // A walk that fails midway (a refusal of the chains, the meters or PEAQ — a contract fault no input reaches).
    static std::uint32_t windows (const Damage& d) noexcept { return d.windows_; }
    // The master's job, for a test that plants its floor pass's request.
    static MasterJob* job (Session& s) noexcept { return s.masterJob_.get(); }
    static void fail (Damage& d, MeasurementReason why) noexcept { d.fail (why); }
};
}

// THE BUDGET'S PROOF, read off the pass log: the lowest drive of every render marked over the budget, above the ceiling
// or not, stands above the render delivered (the log's last) by 0.25 dB at most.
bool budgetProven (const LandingSummary& l)
{
    if (l.log.empty()) return false;
    const double delivered = l.log.back().gainDb - l.log.back().ceilingDbTp;
    double lowest = std::numeric_limits<double>::infinity();
    for (const auto& pass : l.log)
        if (pass.overBudget) lowest = std::fmin (lowest, pass.gainDb - pass.ceilingDbTp);
    return lowest > delivered && lowest - delivered <= 0.25;
}

// THE DAMAGE'S RESAMPLER (src/Damage.h): a source at 44.1 or 96 kHz reaches PEAQ's 48 kHz through a kernel of
// deterministic math, so its bits are pinned here — this suite runs natively and as wasm (tools/wasm/build.sh), and both
// must give the pinned digest; the exact count comes out, whatever the blocks.
void damageResampler()
{
    using felitronics::session::detail::DamageResampler;
    for (const auto [rate, pinned] : { std::pair<std::uint32_t, std::uint64_t> { 44100u, 0xc9374b7a9f34ac0bull }, { 96000u, 0x159a0496d7b104ddull } })
    {
        const auto shape = DamageResampler::shapeFor (rate, 32);
        DamageResampler r;
        const long long in = 3 * (long long) rate / 10, out = DamageResampler::outFrames (shape, in);
        std::vector<float> a ((std::size_t) in), b ((std::size_t) in), ya ((std::size_t) out + 64u), yb ((std::size_t) out + 64u);
        for (long long i = 0; i < in; ++i)
        {
            a[(std::size_t) i] = float (int ((i * 17) % 251) - 125) / 512.0f;
            b[(std::size_t) i] = float (int ((i * 29 + 3) % 113) - 56) / 256.0f;
        }
        bool ready = shape.ok && r.prepare (shape, 2);
        long long done = 0, read = 0;
        while (ready && done < out)
        {
            const int n = int (std::min<long long> (997, in - read));
            const float* src[2] { a.data() + read, b.data() + read };
            float* dst[2] { ya.data() + done, yb.data() + done };
            done += n > 0 ? r.push (src, n, dst, out - done) : r.push (nullptr, 997, dst, out - done);
            read += n;
        }
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (long long i = 0; i < out; ++i)
            for (const float v : { ya[(std::size_t) i], yb[(std::size_t) i] })
                for (unsigned k = 0; k < 4; ++k) h = (h ^ ((std::bit_cast<std::uint32_t> (v) >> (8u * k)) & 0xFFu)) * 0x100000001B3ull;
        char digest[17];
        std::snprintf (digest, sizeof digest, "%016llx", (unsigned long long) h);
        ok (ready && done == out && h == pinned, "the damage's resampler from " + std::to_string (rate)
            + " Hz gives the exact count and the pinned bits, native and wasm alike (digest " + digest + ")");
    }
}

// THE DAMAGE'S JOB WITH NO ID LEFT: the master takes the last id there is; delivered as ever, its damage is graded only
// when the shell asks (no grade starts by itself: no Damage event, the report Pending), and a grade asked with no id left
// is refused (NoJobId), the report left as it was.
void damageWithoutAnId()
{
    constexpr std::uint32_t rate = 48000;
    std::vector<float> left (rate), right (rate);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f;
        right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f;
    }
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create();
    auto& s = *made.session;
    (void) s.apply (command::Load { 1, { planes, 2, left.size(), rate }, { "last.wav", rate, true, 24 } });
    while (s.step (16).state == StepState::More) {}
    detail::Inspector::lastJob (s, std::numeric_limits<JobId>::max() - 1u);
    command::Master ready { 2 };
    ready.ready.version = 1;
    ready.ready.topology.eq = ready.ready.topology.compressor = ready.ready.topology.dither = false;
    ready.source = s.source().hash; ready.revision = s.revision();
    const auto started = s.apply (ready);
    bool damageEvent = false;
    bool more = true;
    for (unsigned i = 0; i < 40000 && more; ++i)
    {
        more = s.step (1).state == StepState::More;
        for (const auto& e : s.events()) damageEvent = damageEvent || e.kind == EventKind::Damage;
    }
    const auto& d = s.masters().empty() ? MasterDamage {} : s.masters()[0].report->damage;
    const bool pending = d.status == MeasurementStatus::Pending;
    const auto graded = s.masters().empty() ? Answer {} : s.apply (command::GradeDamage { 3, s.masters()[0].id });
    ok (started.job == std::numeric_limits<JobId>::max() && s.masters().size() == 1 && s.pendingMaster().master == started.job
        && s.damageJob() == 0 && s.damageJobs().empty() && ! damageEvent && pending
        && graded.rejection == Rejection::NoJobId && s.masters()[0].report->damage.status == MeasurementStatus::Pending,
        "a master with the last job id is delivered, no grade by itself; a grade asked with no id left is refused NoJobId");
}

// A short programme of struck tones — band-limited, dynamic, its peaks a few dB under `level` · 2 — at `rate`, planar.
std::vector<float> struck (std::uint32_t rate, double seconds, double level = 0.5)
{
    const std::size_t frames = std::size_t (double (rate) * seconds);
    std::vector<float> out (2u * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = double (i) / double (rate), beat = t - 0.5 * std::floor (t / 0.5), env = std::exp (-6.0 * beat) * level;
        const double w = 6.283185307179586 * t;
        const double x = env * (std::sin (220.0 * w) + 0.5 * std::sin (1100.0 * w) + 0.3 * std::sin (3300.0 * w)
                                + 0.2 * std::sin (9900.0 * w));
        out[i] = float (x);
        out[frames + i] = float (0.9 * x + 0.05 * std::sin (440.0 * w) * env);
    }
    return out;
}
std::unique_ptr<Session> measuredSession (const std::vector<float>& pcm, std::uint32_t rate)
{
    const std::size_t frames = pcm.size() / 2u;
    const float* planes[] { pcm.data(), pcm.data() + frames };
    auto made = Session::create();
    if (made.status != Status::Ok
        || made.session->apply (command::Load { 1, { planes, 2, frames, rate }, { "damage.wav", rate, true, 24 } }).rejection
            != Rejection::None) return nullptr;
    while (made.session->step (16).state == StepState::More) {}
    return std::move (made.session);
}
command::Master readyMaster (const Session& s, CommandId id)
{
    command::Master m { id };
    m.ready.version = 1;
    m.ready.topology.eq = m.ready.topology.compressor = m.ready.topology.dither = false;
    m.source = s.source().hash; m.revision = s.revision();
    return m;
}

// THE REFERENCE IS THE WHOLE referenceBelowDb LOWER, WHEREVER THE MASTER'S INPUT GAIN STANDS. A limiter-only chain that
// takes a few dB off the peaks of a hot programme (its peaks above full scale), once as input 0 dB and pre-limiter +1 dB,
// once as input -59 dB and pre-limiter +60 dB: the same signal reaches the limiter. The second's reference cannot go
// 60 dB below -59 dB by the input gain alone (its range stops at -60 dB): the rest is a scale of its input, so both
// references stay clear of the ceiling and both masters grade alike — where the knob alone lowered it by 1 dB, its
// limiter cut nearly the same peaks as the master's.
void damageReferenceBelowTheKnob()
{
    using detail::Damage;
    const auto pcm = struck (48000, 4.0, 1.5);
    const std::uint64_t frames = pcm.size() / 2u;
    const float* planes[] { pcm.data(), pcm.data() + frames };
    felitronics::mastering::MasteringChainConfig topology;
    topology.eq = topology.compressor = topology.clipper = topology.dither = false;
    topology.limiter = true;
    const auto grade = [&] (double input, double preLimiter)
    {
        auto plan = Damage::plan (48000, frames, 2, topology);
        felitronics::mastering::MasteringChainParams winning;
        winning.inputGainDb = input; winning.preLimiterGainDb = preLimiter;
        felitronics::mastering::MasteringChain master;
        Damage damage;
        MasterDamage out;
        if (! plan.ok || ! damage.begin (plan, master, winning, planes, frames, 2)) return out;
        for (unsigned i = 0; i < 100000 && ! damage.step (1024); ++i) {}
        damage.publish (out);
        return out;
    };
    const auto near = grade (0.0, 1.0), far = grade (-59.0, 60.0);
    const auto odg = [] (const MasterDamage& d) { return d.worstOdg.value_or (0.0); };
    char said[96];
    std::snprintf (said, sizeof said, "ODG %.3f and %.3f, grades %u and %u", odg (near), odg (far), near.grade, far.grade);
    ok (near.status == MeasurementStatus::Ready && far.status == MeasurementStatus::Ready && near.grade < 5u
        && far.grade == near.grade && std::fabs (odg (far) - odg (near)) < 0.1,
        std::string ("a reference the input gain cannot take 60 dB down is lowered the rest by its input: the same grade (") + said + ")");
}

// A WALK THAT FAILS GRADES NOTHING: failed after three windows closed, its report is Unavailable with the
// reason, and no count of windows — not a partial tally beside a status that says nothing was graded.
void damageFailedMidway()
{
    using detail::Damage;
    const auto pcm = struck (48000, 30.0, 1.5);
    const std::uint64_t frames = pcm.size() / 2u;
    const float* planes[] { pcm.data(), pcm.data() + frames };
    felitronics::mastering::MasteringChainConfig topology;
    topology.eq = topology.compressor = topology.clipper = topology.dither = false;
    auto plan = Damage::plan (48000, frames, 2, topology);
    felitronics::mastering::MasteringChainParams winning;
    winning.preLimiterGainDb = 1.0;
    felitronics::mastering::MasteringChain master;
    Damage damage;
    MasterDamage out;
    bool done = ! plan.ok || ! damage.begin (plan, master, winning, planes, frames, 2);
    for (unsigned i = 0; i < 100000 && ! done && detail::Inspector::windows (damage) < 3u; ++i) done = damage.step (1024);
    const auto closed = detail::Inspector::windows (damage);
    detail::Inspector::fail (damage, MeasurementReason::NonFinite);
    for (unsigned i = 0; i < 100000 && ! done; ++i) done = damage.step (1024);
    // Into a report that held a grade before: publish clears every number of it, not only what a fresh one lacks.
    out.grade = 3; out.worstOdg = -2.0; out.worstDi = 1.0; out.worstFromSeconds = 5.0; out.audibleShare = 0.5;
    out.referenceGainDb = -1.0;
    damage.publish (out);
    ok (closed == 3u && done && out.status == MeasurementStatus::Unavailable && out.reason == MeasurementReason::NonFinite
        && out.verdict == DamageVerdict::NotRun && out.windows == 0 && out.audibleWindows == 0 && out.ungradedWindows == 0
        && out.grade == 0 && ! out.worstOdg && ! out.worstDi && ! out.worstFromSeconds && ! out.audibleShare
        && ! out.referenceGainDb,
        "a damage walk failed after " + std::to_string (closed) + " windows grades nothing: Unavailable, NonFinite, windows "
        + std::to_string (out.windows) + ", heard " + std::to_string (out.audibleWindows));
}

// THE DAMAGE'S JOB HOLDS NO MORE THAN IT DECLARED: every byte its job asks for — made in the unit that delivers the
// master, its walks begun at its first step — within DamageJob::bytes, the bytes it adds to liveBytes(), at 48 kHz and
// through the resampler from 44.1 kHz.
void damageMemoryIsDeclared()
{
    for (const std::uint32_t rate : { 48000u, 44100u })
    {
        const auto pcm = struck (rate, 3.0);
        auto sp = measuredSession (pcm, rate);
        if (! sp) { ok (false, "a session for the damage's memory"); continue; }
        auto& s = *sp;
        if (s.apply (readyMaster (s, 2)).rejection != Rejection::None) { ok (false, "its master is taken"); continue; }
        for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) (void) s.step (1);
        if (s.masters().empty() || s.apply (command::GradeDamage { 3, s.masters()[0].id }).rejection != Rejection::None)
        { ok (false, "its damage grade is asked"); continue; }
        std::uint64_t declaredBytes = 0, asked = 0;
        bool started = false;
        for (unsigned i = 0; i < 400000 && ! s.damageJobs().empty(); ++i)
        {
            const auto spent = declared::spend ([&] { (void) s.step (1); });
            if (! started && s.damageJob() != 0) { started = true; declaredBytes = detail::Inspector::damageBytes (s); }
            if (started) asked += std::uint64_t (std::max (0LL, spent.bytes));
        }
        ok (started && s.damageJob() == 0 && asked > 0 && asked <= declaredBytes,
            "the damage's job at " + std::to_string (rate) + " Hz asks for " + std::to_string (asked) + " bytes of the "
            + std::to_string (declaredBytes) + " it declared");
    }
}

// A MASTER ASKED WHILE A DAMAGE IS GRADED is priced without it: the new master parks it — its walks freed before the
// master allocates, the grade waiting its turn again — so the room that job holds is the new master's. At the ceiling of exactly what the master needs then it is taken, and stays under
// it; a byte lower it is refused, naming that need.
void masterAtTheCeilingWithADamage()
{
    const auto pcm = struck (48000, 3.0);
    auto sp = measuredSession (pcm, 48000);
    if (! sp) { ok (false, "a session for the ceiling"); return; }
    auto& s = *sp;
    (void) s.apply (readyMaster (s, 2));
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) (void) s.step (1);
    (void) s.apply (command::GradeDamage { 3, s.masters()[0].id });
    // The late impact joins first; then the damage enters its first walk and owns its admitted room.
    for (unsigned i = 0; i < 400000 && s.damageJob() == 0; ++i) (void) s.step (1);
    const bool released = s.releaseMaster (s.pendingMaster()) == MasterTransferStatus::Ok;
    const auto request = readyMaster (s, 4);
    const auto unlimited = s.check (request);
    const auto damage = detail::Inspector::damageBytes (s);
    const auto live = std::uint64_t (s.liveBytes());
    const auto need = live - damage + unlimited.bytes;
    (void) s.setCapacity ({ double (need - 1u), double (unlimited.largestBlockBytes) });
    const auto under = s.check (request);
    (void) s.setCapacity ({ double (need), double (unlimited.largestBlockBytes) });
    const auto at = s.check (request);
    Answer taken;
    // Gross: the counter adds every request and subtracts no release, so the damage's freed job hides none of these.
    const auto spent = declared::spend ([&] { taken = s.apply (request); });
    ok (released && s.damageJob() == 0 && damage > 0 && unlimited.rejection == Rejection::None
        && under.rejection == Rejection::Memory && under.needBytes == double (need)
        && at.rejection == Rejection::None && taken.rejection == Rejection::None
        && declared::covers (unlimited.bytes, spent) && s.liveBytes() <= double (need),
        "a master asked while a damage is graded is priced without that damage's job: taken at the ceiling of "
        + std::to_string (need) + " bytes (the damage's " + std::to_string (damage) + " freed first), refused a byte under");
    (void) s.setCapacity ({});
}

// THE PROGRAMME REPORT HAS ENDED WHENEVER A MASTER STARTS: the master's loudness range reads the input's from it, so a
// master that started before it would leave its range's change Pending for good. A master asked earlier is queued — never
// refused for its timing — and its turn comes after the first measurement, which ends after the programme report: on a
// fresh load, after a measurement stopped between the loudness and the report and continued, and after the same source
// loaded again.
void programmeBeforeAnyMaster()
{
    const auto pcm = struck (48000, 1.0);
    const std::size_t frames = pcm.size() / 2u;
    const float* planes[] { pcm.data(), pcm.data() + frames };
    for (const int path : { 0, 1, 2 })
    {
        auto made = Session::create();
        auto& s = *made.session;
        (void) s.apply (command::Load { 1, { planes, 2, frames, 48000 }, { "programme.wav", 48000, true, 24 } });
        const auto status = [&] (Analyzer a) { return s.snapshot().view().measurements[std::size_t (a)].status; };
        bool between = path == 0;
        if (path != 0)
        {
            for (unsigned i = 0; i < 100000 && ! between && s.measurementJob() != 0; ++i)
            {
                (void) s.step (1);
                between = status (Analyzer::Loudness) == MeasurementStatus::Ready && status (Analyzer::Programme) == MeasurementStatus::Pending;
            }
            (void) s.apply (command::Cancel { 2, s.measurementJob() });
            if (path == 1) (void) s.apply (command::ContinueMeasurement { 3 });
            else (void) s.apply (command::Load { 3, { planes, 2, frames, 48000 }, { "programme.wav", 48000, true, 24 } });
        }
        const auto asked = s.apply (readyMaster (s, 9));
        const bool queued = asked.rejection == Rejection::None && asked.job != 0 && s.job() == 0
            && status (Analyzer::Programme) == MeasurementStatus::Pending;
        bool started = false, early = false;
        for (unsigned i = 0; i < 400000 && ! started; ++i)
        {
            (void) s.step (1);
            if (s.job() == asked.job) { started = true; early = status (Analyzer::Programme) != MeasurementStatus::Ready; }
        }
        ok (between && queued && started && ! early,
            "path " + std::to_string (path) + ": a master asked before the programme report is queued, and starts only once "
            "the report has ended");
    }
}

// THE DAMAGE GRADES, ASKED BY THE SHELL (command::GradeDamage): no grade without the command; each its own job, its
// damage events Pending then the result, a row of the snapshot's damageJobs; one at a time, in the order asked. A person
// deleting a master (forget) ends its grade at once — running at any point of its walks, or waiting — with one last
// event (MasterForgotten), its walks' bytes back, and nothing of it after; a cancel the same with Cancelled; a new source
// ends them all; the queue moves on. Each case is recorded event by event.
struct GradeRun
{
    std::unique_ptr<Session> session;
    std::vector<Notification> events;           // every event of every apply and step, in order
};
GradeRun gradeSession (double seconds)
{
    GradeRun r;
    const auto pcm = struck (48000, seconds);
    r.session = measuredSession (pcm, 48000);
    return r;
}
void record (GradeRun& r) { for (const auto& e : r.session->events()) r.events.push_back (e); }
Answer act (GradeRun& r, const Request& request) { const auto a = r.session->apply (request); record (r); return a; }
// A master delivered, its PCM released (the next master is asked with none pending): its id.
MasterId deliver (GradeRun& r, CommandId id)
{
    auto& s = *r.session;
    if (s.pendingMaster().master != 0) (void) s.releaseMaster (s.pendingMaster());
    if (act (r, readyMaster (s, id)).rejection != Rejection::None) return 0;
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) { (void) s.step (1); record (r); }
    return s.masters().empty() ? 0u : s.masters().back().id;
}
// Steps one unit at a time until `until` holds (or nothing is left to do).
template <typename Until> bool stepUntil (GradeRun& r, Until&& until)
{
    for (unsigned i = 0; i < 400000; ++i)
    {
        if (until()) return true;
        if (r.session->step (1).state == StepState::Done) { record (r); return until(); }
        record (r);
    }
    return false;
}
bool runOut (GradeRun& r) { return stepUntil (r, [] { return false; }), true; }

// DELIVERY ENDS BEFORE ITS IMPACT JOIN. PCM is transferable while the report says Pending; the later crest fact follows
// MasterReady. A cancel addresses that join by the delivered job id, a new master settles it Cancelled, and forgetting
// its master releases it without leaving work behind.
void lateMasterCrestLifecycle()
{
    {
        auto r = gradeSession (1.5);
        auto& s = *r.session;
        const auto master = deliver (r, 2);
        const auto token = s.pendingMaster();
        const auto shape = s.masterAudioShape (token);
        std::vector<float> pcm (std::size_t (shape.frames * shape.channels));
        const bool readyPending = master != 0 && token.master == master && ! pcm.empty()
            && s.copyMaster (token, pcm) == MasterTransferStatus::Ok && s.masters().back().report
            && s.masters().back().report->crest.status == MeasurementStatus::Pending;
        (void) runOut (r);
        std::size_t readyAt = r.events.size(), crestAt = r.events.size();
        for (std::size_t i = 0; i < r.events.size(); ++i)
            if (r.events[i].jobId == master && r.events[i].kind == EventKind::Fact)
            {
                const auto fact = r.events[i].payload.fact.view().id;
                if (fact == text::FactId::MasterReady) readyAt = std::min (readyAt, i);
                if (fact == text::FactId::MasterCrestDelivered || fact == text::FactId::MasterCrestSourceRate)
                    crestAt = std::min (crestAt, i);
            }
        ok (readyPending && s.masters().back().report->crest.status == MeasurementStatus::Ready
            && readyAt < crestAt && s.masterAudioBytes (token) == pcm.size() * sizeof (float),
            "master PCM and report are ready with crest Pending; its impact joins later, after MasterReady");
    }
    {
        auto r = gradeSession (1.5); auto& s = *r.session;
        const auto master = deliver (r, 2);
        const auto stopped = act (r, command::Cancel { 3, master });
        ok (stopped.rejection == Rejection::None && s.masters().back().report->crest.status == MeasurementStatus::Cancelled
            && s.pendingMaster().master == master,
            "cancel by the delivered job id stops only its pending impact and leaves its PCM transferable");
    }
    {
        auto r = gradeSession (1.5); auto& s = *r.session;
        const auto first = deliver (r, 2);
        const bool released = s.releaseMaster (s.pendingMaster()) == MasterTransferStatus::Ok;
        const auto next = act (r, readyMaster (s, 3));
        ok (released && next.rejection == Rejection::None && s.masters().front().id == first
            && s.masters().front().report->crest.status == MeasurementStatus::Cancelled && s.job() == next.job,
            "a new master settles the older pending impact Cancelled and starts independently (rejection "
            + std::to_string (unsigned (next.rejection)) + ", status "
            + std::to_string (unsigned (s.masters().front().report->crest.status)) + ", job "
            + std::to_string (s.job()) + "/" + std::to_string (next.job) + ")");
        (void) act (r, command::Cancel { 4, next.job });
    }
    {
        auto r = gradeSession (1.5); auto& s = *r.session;
        const auto master = deliver (r, 2);
        const auto forgotten = act (r, command::Forget { 3, master });
        ok (forgotten.rejection == Rejection::None && s.masters().empty() && s.step (1).state == StepState::Done,
            "forget releases a pending impact with its retained master and leaves no orphan work");
    }
}

void manualBudgetResolution()
{
    constexpr std::uint32_t rate = 48000;
    constexpr std::size_t frames = 4u * rate;
    std::vector<float> pcm (2u * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        double x = .16 * felitronics::core::det::sin (6.283185307179586 * 117.0 * double (i) / rate)
                 + .03 * felitronics::core::det::sin (6.283185307179586 * 3061.0 * double (i) / rate);
        if (i % 4096u == 0) x += .7;
        pcm[i] = pcm[frames + i] = float (x);
    }
    GradeRun r; r.session = measuredSession (pcm, rate);
    if (! r.session) { ok (false, "a session for the manual budget-resolution regression"); return; }
    // The fixture names its target: a manual loudness goal on All streaming, not whatever a new session starts on.
    command::EditTarget edit { 3, { -10.0, -1.0 } };
    if (act (r, command::SetTarget { 2, "allStreaming" }).rejection != Rejection::None
        || act (r, edit).rejection != Rejection::None)
    { ok (false, "the manual target for the budget-resolution regression"); return; }
    auto request = readyMaster (*r.session, 4);
    request.budgetResolutionDb = .05;
    if (act (r, request).rejection != Rejection::None)
    { ok (false, "the manual master accepts its budget resolution"); return; }
    for (unsigned i = 0; i < 400000 && r.session->job() != 0; ++i) { (void) r.session->step (1); record (r); }
    const auto& masters = r.session->masters();
    const LandingSummary* landing = masters.empty() || ! masters.back().landing ? nullptr : &*masters.back().landing;
    double lowestOver = std::numeric_limits<double>::infinity();
    double delivered = std::numeric_limits<double>::quiet_NaN();
    if (landing && ! landing->log.empty())
    {
        delivered = -std::numeric_limits<double>::infinity();
        for (const auto& pass : landing->log)
            if (pass.overBudget) lowestOver = std::fmin (lowestOver, pass.gainDb - pass.ceilingDbTp);
            else if (pass.ceilingSafe) delivered = std::fmax (delivered, pass.gainDb - pass.ceilingDbTp);
    }
    std::printf ("        manual 0.05 proof: over %.5f, delivered %.5f, passes %u\n",
        lowestOver, delivered, landing ? landing->passes : 0u);
    ok (landing && landing->deliverable && std::isfinite (lowestOver) && lowestOver > delivered
        && lowestOver - delivered <= .05 + 1.0e-9,
        "a manual master proves its limiter budget to the requested 0.05 dB");
}
// The point of a walk: the running grade's damage walk (PEAQ) past `fraction` of the programme; 0: its first unit.
bool atPoint (GradeRun& r, double fraction)
{
    auto& s = *r.session;
    if (fraction <= 0.0) return stepUntil (r, [&] { return s.damageJob() != 0; });
    return stepUntil (r, [&] { return s.damageJob() != 0 && s.damageJobs()[0].progress.name == PhaseName::Damage
                                      && s.damageJobs()[0].progress.stepFraction
                                      && *s.damageJobs()[0].progress.stepFraction > fraction; });
}
std::size_t countOf (const std::vector<Notification>& events, JobId job, std::size_t from = 0)
{
    std::size_t n = 0;
    for (std::size_t i = from; i < events.size(); ++i) n += events[i].jobId == job ? 1u : 0u;
    return n;
}
// A job's last event, a copy: the record grows (and moves) with every apply and step after it.
std::optional<Notification> lastOf (const std::vector<Notification>& events, JobId job)
{
    for (std::size_t i = events.size(); i-- > 0;) if (events[i].jobId == job) return events[i];
    return std::nullopt;
}

void damageGrades()
{
    // NO GRADE WITHOUT THE COMMAND: a master delivered and the session run out — no damage event, no row, Pending.
    {
        auto r = gradeSession (3.0);
        const auto master = r.session ? deliver (r, 2) : 0u;
        (void) runOut (r);
        bool damage = false;
        for (const auto& e : r.events) damage = damage || e.kind == EventKind::Damage;
        ok (master != 0 && ! damage && r.session->damageJobs().empty() && r.session->damageJob() == 0
            && r.session->masters()[0].report->damage.status == MeasurementStatus::Pending,
            "no grade without the command: the master delivered, no damage event, no row, its damage Pending");
    }
    // ASKED, GRADED: the answer's job; announced Pending; Waiting, then Running in the snapshot; to the end — its phases,
    // its line, and its Damage event, Ready, the job's last word; then no row and nothing of it.
    {
        auto r = gradeSession (3.0);
        auto& s = *r.session;
        const auto master = deliver (r, 2);
        const auto from = r.events.size();
        const auto asked = act (r, command::GradeDamage { 3, master });
        const auto job = asked.job;
        const auto view = s.snapshot();
        const bool waiting = view.view().damageJobs.size() == 1 && view.view().damageJobs[0].job == job
            && view.view().damageJobs[0].masterId == master && view.view().damageJobs[0].state == DamageJobState::Waiting;
        const bool announced = r.events.size() == from + 1 && r.events[from].kind == EventKind::Damage
            && r.events[from].jobId == job && r.events[from].payload.damage.status == MeasurementStatus::Pending;
        const bool running = atPoint (r, 0.0) && s.snapshot().view().damageJobs[0].state == DamageJobState::Running
            && s.snapshot().view().damageJob == job;
        (void) runOut (r);
        const auto last = lastOf (r.events, job);
        ok (asked.rejection == Rejection::None && job > master && waiting && announced && running && last
            && last->kind == EventKind::Damage && last->payload.damage.status == MeasurementStatus::Ready
            && s.masters()[0].report->damage.status == MeasurementStatus::Ready && s.damageJobs().empty(),
            "gradeDamage after a master: its own job, announced Pending, Waiting then Running in the snapshot, graded "
            "Ready by its last event, then no row");
        // THE ORDINARY REFUSALS: a settled grade, a master not kept, a grade asked twice, a cancel of a finished job.
        const auto settled = act (r, command::GradeDamage { 4, master });
        const auto unknown = act (r, command::GradeDamage { 5, master + 100u });
        const auto other = deliver (r, 6);
        const auto first = act (r, command::GradeDamage { 7, other });
        const auto twice = act (r, command::GradeDamage { 8, other });
        const auto finished = act (r, command::Cancel { 9, job });
        ok (settled.rejection == Rejection::DamageSettled && unknown.rejection == Rejection::UnknownMaster
            && first.rejection == Rejection::None && twice.rejection == Rejection::DamageQueued
            && finished.rejection == Rejection::UnknownJob,
            "gradeDamage is refused for a settled damage, a master not kept and a grade already asked; a finished grade's "
            "job is no job to cancel");
    }
    // TWO GRADES QUEUED: run one at a time, in the order asked — the second waits, then runs; both Ready, in order.
    {
        auto r = gradeSession (3.0);
        auto& s = *r.session;
        const auto a = deliver (r, 2), b = deliver (r, 3);
        const auto ja = act (r, command::GradeDamage { 4, a }).job, jb = act (r, command::GradeDamage { 5, b }).job;
        const bool queued = s.damageJobs().size() == 2 && s.damageJobs()[0].job == ja && s.damageJobs()[1].job == jb
            && s.damageJobs()[1].state == DamageJobState::Waiting;
        const bool oneAtATime = atPoint (r, 0.5) && s.damageJob() == ja && s.damageJobs()[1].state == DamageJobState::Waiting
            && countOf (r.events, jb) == 1u;
        (void) runOut (r);
        std::vector<JobId> ready;
        for (const auto& e : r.events)
            if (e.kind == EventKind::Damage && e.payload.damage.status == MeasurementStatus::Ready) ready.push_back (e.jobId);
        ok (queued && oneAtATime && ready.size() == 2 && ready[0] == ja && ready[1] == jb
            && s.masters()[0].report->damage.status == MeasurementStatus::Ready
            && s.masters()[1].report->damage.status == MeasurementStatus::Ready,
            "two grades queued run one at a time in the order asked: the second waits (its announcement alone), both Ready");
    }
    // FORGET OR CANCEL OF THE RUNNING GRADE, at its first unit, midway and in its last window: its walks go in the
    // command — the command's releasedBytes are their bytes, and the live bytes are a twin's that never graded — one
    // last event says why (forget: MasterForgotten, no line; cancel: its fact, its line, Cancelled), and nothing of it
    // comes after; after a cancel the same master is graded again, cleanly, under a fresh job.
    for (const bool forget : { true, false })
        for (const double point : { 0.0, 0.5, 0.97 })
        {
            const std::string what = std::string (forget ? "forget" : "cancel") + " at " + std::to_string (point);
            auto r = gradeSession (6.0), twin = gradeSession (6.0);
            auto& s = *r.session;
            const auto master = deliver (r, 2), twinMaster = deliver (twin, 2);
            const auto job = act (r, command::GradeDamage { 3, master }).job;
            if (! atPoint (r, point)) { ok (false, what + ": the walk reached its point"); continue; }
            const auto held = detail::Inspector::damageBytes (s);
            const Request stop = forget ? Request { command::Forget { 4, master } } : Request { command::Cancel { 4, job } };
            const auto checked = s.check (stop);
            const auto from = r.events.size();
            const auto answer = act (r, stop);
            const auto said = countOf (r.events, job, from);
            const auto last = lastOf (r.events, job);
            const bool lastWord = last && last->kind == EventKind::Damage
                && last->payload.damage.status == MeasurementStatus::Cancelled
                && last->payload.damage.reason == (forget ? MeasurementReason::MasterForgotten : MeasurementReason::Cancelled);
            const auto afterStop = r.events.size();
            (void) runOut (r);
            if (forget && twin.session) (void) act (twin, command::Forget { 4, twinMaster });
            const bool baseline = ! forget || (twin.session && s.liveBytes() == twin.session->liveBytes());
            ok (job != 0 && held > 0 && answer.rejection == Rejection::None && checked.releasedBytes == held
                && said == (forget ? 1u : 3u) && lastWord && countOf (r.events, job, afterStop) == 0u
                && s.damageJob() == 0 && s.damageJobs().empty() && baseline,
                what + ": the running grade ends in the command, its " + std::to_string (held)
                + " bytes released, one last event, nothing after" + (forget ? ", memory back to the baseline" : ""));
            if (! forget)
            {
                const auto again = act (r, command::GradeDamage { 5, master });
                (void) runOut (r);
                ok (again.rejection == Rejection::None && again.job > job && s.masters()[0].report->damage.status == MeasurementStatus::Ready,
                    what + ": after the cancel the same master is graded again, under a fresh job, Ready");
            }
        }
    // FORGET OR CANCEL OF A WAITING GRADE: it leaves the queue with one last event and no work; the running one finishes.
    for (const bool forget : { true, false })
    {
        auto r = gradeSession (3.0);
        auto& s = *r.session;
        const auto a = deliver (r, 2), b = deliver (r, 3);
        const auto ja = act (r, command::GradeDamage { 4, a }).job, jb = act (r, command::GradeDamage { 5, b }).job;
        (void) atPoint (r, 0.3);
        const Request stop = forget ? Request { command::Forget { 6, b } } : Request { command::Cancel { 6, jb } };
        const auto checked = s.check (stop);
        const auto answer = act (r, stop);
        const bool gone = s.damageJobs().size() == 1 && s.damageJobs()[0].job == ja;
        (void) runOut (r);
        bool worked = false;
        for (const auto& e : r.events) worked = worked || (e.jobId == jb && e.kind == EventKind::Phase);
        const auto last = lastOf (r.events, jb);
        ok (answer.rejection == Rejection::None && checked.releasedBytes == 0 && gone && ! worked && last
            && last->kind == EventKind::Damage && last->payload.damage.status == MeasurementStatus::Cancelled
            && countOf (r.events, jb) == (forget ? 2u : 4u) && s.masters()[0].report->damage.status == MeasurementStatus::Ready,
            std::string (forget ? "forget" : "cancel") + " of a waiting grade: it leaves the queue with one last event and "
            "no work; the running one finishes Ready");
    }
    // FORGET OF THE RUNNING GRADE WHILE ANOTHER WAITS: the waiting one then runs and finishes; a master with no grade
    // forgotten meanwhile changes nothing for it; then a new master is graded cleanly under fresh ids.
    {
        auto r = gradeSession (3.0);
        auto& s = *r.session;
        const auto a = deliver (r, 2), b = deliver (r, 3), c = deliver (r, 4);
        const auto ja = act (r, command::GradeDamage { 5, a }).job, jb = act (r, command::GradeDamage { 6, b }).job;
        (void) atPoint (r, 0.5);
        (void) act (r, command::Forget { 7, a });
        const bool next = s.damageJobs().size() == 1 && s.damageJobs()[0].job == jb;
        const auto beforeC = r.events.size();
        (void) atPoint (r, 0.4);
        (void) act (r, command::Forget { 8, c });
        const bool untouched = countOf (r.events, jb, beforeC) > 0 && s.damageJob() == jb;
        (void) runOut (r);
        const auto lastB = lastOf (r.events, jb);
        const auto d = deliver (r, 9);
        const auto jd = act (r, command::GradeDamage { 10, d }).job;
        (void) runOut (r);
        const auto lastD = lastOf (r.events, jd);
        ok (ja != 0 && next && untouched && lastB && lastB->payload.damage.status == MeasurementStatus::Ready
            && countOf (r.events, ja, beforeC) == 0u && d > jb && jd > d && lastD
            && lastD->payload.damage.status == MeasurementStatus::Ready && lastD->payload.damage.masterId == d,
            "forget of the running grade while another waits: the next runs and finishes Ready, a master with no grade "
            "forgotten meanwhile changes nothing, and a new master is graded cleanly under fresh ids");
    }
    // THE ROOM AT ITS TURN: a capacity with no room for the walks when the grade's turn comes — refused there, said
    // (Memory, its line), Unavailable; with room again the same master may be asked once more, and is graded.
    {
        auto r = gradeSession (3.0);
        auto& s = *r.session;
        const auto master = deliver (r, 2);
        const auto job = act (r, command::GradeDamage { 3, master }).job;
        // Delivery now leaves only its impact join. Let that release its workspace while this grade is still Waiting,
        // so the deliberately exact ceiling below tests the damage's own turn rather than the earlier join.
        const auto withImpact = s.liveBytes();
        for (unsigned i = 0; i < 400000 && s.damageJob() == 0 && s.liveBytes() >= withImpact; ++i) (void) s.step (1);
        (void) s.setCapacity ({ s.liveBytes(), 9007199254740991.0 });
        (void) stepUntil (r, [&] { return s.damageJobs().empty(); });
        const auto last = lastOf (r.events, job);
        bool line = false;
        for (const auto& e : r.events)
            if (e.jobId == job && e.kind == EventKind::Fact && e.payload.fact.view().id == text::FactId::MasterDamageUnmeasured)
                line = e.payload.fact.view().args[0].termId == text::Term::ReasonMemory;
        const auto& d = s.masters()[0].report->damage;
        const bool refused = last && last->kind == EventKind::Damage && last->payload.damage.status == MeasurementStatus::Unavailable
            && last->payload.damage.reason == MeasurementReason::Memory && line && d.status == MeasurementStatus::Unavailable
            && d.reason == MeasurementReason::Memory && countOf (r.events, job) == 3u;
        (void) s.setCapacity ({});
        const auto again = act (r, command::GradeDamage { 4, master });
        (void) runOut (r);
        ok (job != 0 && refused && again.rejection == Rejection::None && s.masters()[0].report->damage.status == MeasurementStatus::Ready,
            "a grade whose turn finds no room is refused there, said with Memory and its line; with room again it is "
            "asked once more and graded");
    }
    // A NEW SOURCE ends every grade (MasterForgotten): each grade's last word in the load's own batch, in queue order,
    // stamped with the source it belonged to and ahead of anything of the new one — whatever comes next before a step: a
    // cancel of the new source's measurement, another source. With the queue full (kMaxDamageGrades; one grade more is
    // refused DamageQueueFull) no batch outgrows its bound, and nothing of those grades comes after.
    for (const int next : { 0, 1, 2 })   // after the load: steps; a cancel of its measurement, then steps; another load
        for (const std::size_t many : { std::size_t (2), kMaxDamageGrades })
        {
            const std::string name = std::to_string (many) + " grades, then " + (next == 0 ? "steps" : next == 1 ? "a cancel" : "another load");
            auto r = gradeSession (many > 2 ? 1.0 : 3.0);
            auto& s = *r.session;
            // A master graded to its end first: its damage settled.
            const auto settledMaster = deliver (r, 50);
            (void) act (r, command::GradeDamage { 51, settledMaster });
            (void) stepUntil (r, [&] { return s.damageJobs().empty(); });
            std::vector<JobId> asked;
            for (std::size_t k = 0; k < many; ++k)
            {
                const auto master = deliver (r, CommandId (100 + 2 * k));
                asked.push_back (act (r, command::GradeDamage { CommandId (101 + 2 * k), master }).job);
            }
            const auto full = many == kMaxDamageGrades ? act (r, command::GradeDamage { 999, deliver (r, 998) }).rejection : Rejection::DamageQueueFull;
            // With the queue full, the permanent refusals still come first: a settled damage, a master not kept, a grade
            // already asked — DamageQueueFull, the one that passes, last.
            const bool order = many != kMaxDamageGrades
                || (act (r, command::GradeDamage { 997, settledMaster }).rejection == Rejection::DamageSettled
                    && act (r, command::GradeDamage { 996, settledMaster + 10000u }).rejection == Rejection::UnknownMaster
                    && act (r, command::GradeDamage { 995, s.damageJobs()[1].masterId }).rejection == Rejection::DamageQueued);
            (void) atPoint (r, many > 2 ? 0.0 : 0.5);
            const auto oldSource = s.source().hash, oldRevision = s.revision();
            const auto pcm = struck (48000, 2.0, 0.3), other = struck (48000, 2.5, 0.2);
            const float* planes[] { pcm.data(), pcm.data() + pcm.size() / 2u };
            const float* otherPlanes[] { other.data(), other.data() + other.size() / 2u };
            const auto from = r.events.size();
            std::size_t largest = 0;
            const auto take = [&] (const Answer& a) { largest = std::max (largest, s.events().size()); record (r); return a; };
            const auto loaded = take (s.apply (command::Load { 9000, { planes, 2, pcm.size() / 2u, 48000 }, { "b.wav", 48000, true, 24 } }));
            const auto loadBatchEnd = r.events.size();
            const auto newSource = s.source().hash;
            if (next == 1) (void) take (s.apply (command::Cancel { 9001, s.measurementJob() }));
            if (next == 2) (void) take (s.apply (command::Load { 9002, { otherPlanes, 2, other.size() / 2u, 48000 }, { "c.wav", 48000, true, 24 } }));
            for (unsigned i = 0; i < 400000 && s.step (1).state == StepState::More; ++i)
            {
                largest = std::max (largest, s.events().size());
                record (r);
            }
            std::vector<JobId> ended;
            bool stamped = true, quiet = true, first = true, seenNew = false;
            for (std::size_t i = from; i < r.events.size(); ++i)
            {
                const auto& e = r.events[i];
                const bool old = std::find (asked.begin(), asked.end(), e.jobId) != asked.end();
                if (! old) { seenNew = seenNew || e.source != oldSource; continue; }
                if (e.kind == EventKind::Damage && e.payload.damage.reason == MeasurementReason::MasterForgotten
                    && e.payload.damage.status == MeasurementStatus::Cancelled) ended.push_back (e.jobId);
                else quiet = false;
                stamped = stamped && e.source == oldSource && e.revision == oldRevision && i < loadBatchEnd;
                first = first && ! seenNew;
            }
            ok (loaded.rejection == Rejection::None && full == Rejection::DamageQueueFull && order && newSource != oldSource
                    && ended == asked && quiet && stamped && first && largest <= kEventBatch && s.damageJobs().empty(),
                name + ": each grade's last word in the load's own batch, in queue order, stamped with its own source and "
                "revision and ahead of the new one, nothing else of them after, no batch past its bound (largest "
                + std::to_string (largest) + " of " + std::to_string (kEventBatch) + ")");
        }
}

// A MAX MASTER IS A LANDING AND A DELIVERY (owner, 04.10: no guard in the loop): delivered with its damage Pending and no
// grade of its own — no damage event, no row — until the shell asks one (command::GradeDamage), which grades it as any
// master's; what ended it is the landing's alone (Budget, SearchCeiling, Passes, TruePeak, OverBudget; no step back);
// and the budgets are the config's: clean 0.5 dB, dense 1.75 dB (owner, 04.10, by ear).
void maxMasterLanding()
{
    const auto engine = detail::rules().engine;
    ok (detail::maxBudgetDb (engine, LoudnessMode::MaxClean) == 0.5 && detail::maxBudgetDb (engine, LoudnessMode::MaxDense) == 1.75
            && detail::maxBudgetDb (engine, LoudnessMode::MaxExtreme) == 3.0 && detail::maxBudgetDb (engine, LoudnessMode::MaxNuke) == 7.0
            && std::isnan (detail::maxBudgetDb (engine, LoudnessMode::Manual))
            && unsigned (LoudnessMode::MaxExtreme) == 3u && unsigned (LoudnessMode::MaxNuke) == 4u,
        "the max modes' budgets read from the config: clean 0.5 dB, dense 1.75 dB, extreme 3 dB (enum value 3), nuke 7 dB (enum value 4), none for the manual mode");
    for (const LoudnessMode mode : { LoudnessMode::MaxClean, LoudnessMode::MaxDense, LoudnessMode::MaxExtreme, LoudnessMode::MaxNuke })
    {
        const std::string name = mode == LoudnessMode::MaxClean ? "maxClean" : mode == LoudnessMode::MaxDense ? "maxDense"
                               : mode == LoudnessMode::MaxExtreme ? "maxExtreme" : "maxNuke";
        auto r = gradeSession (3.0);
        if (! r.session) { ok (false, name + ": a session"); continue; }
        auto& s = *r.session;
        command::EditTarget edit { 2, {} };
        edit.fields.loudnessMode = mode;
        (void) act (r, edit);
        const auto master = deliver (r, 3);
        (void) runOut (r);
        bool damageEvent = false;
        for (const auto& e : r.events) damageEvent = damageEvent || e.kind == EventKind::Damage;
        const std::optional<MasterReport> report = s.masters().empty() ? std::optional<MasterReport> {} : s.masters().back().report;
        const auto stop = report ? report->maxStop : MaxStop::None;
        const bool landingStop = stop == MaxStop::Budget || stop == MaxStop::SearchCeiling || stop == MaxStop::Passes
            || stop == MaxStop::TruePeak || stop == MaxStop::OverBudget;
        const double budget = mode == LoudnessMode::MaxClean ? 0.5 : mode == LoudnessMode::MaxDense ? 1.75
                            : mode == LoudnessMode::MaxExtreme ? 3.0 : 7.0;
        const bool kept = report && report->cost && report->cost->limiterP95Db.value
            && (stop != MaxStop::Budget || *report->cost->limiterP95Db.value <= budget + 1e-9);
        ok (master != 0 && report && report->loudnessMode == mode && landingStop && report->guardSteps == 0u && kept
                && report->damage.status == MeasurementStatus::Pending && ! damageEvent && s.damageJobs().empty(),
            name + ": a landing and a delivery — stop " + std::to_string (unsigned (stop)) + " from the landing, no step "
            "back, the budget held, its damage Pending and no grade of its own (achieved "
            + std::to_string (report ? report->achievedLufs.value_or (0.0) : 0.0) + " LUFS)");
        const auto asked = act (r, command::GradeDamage { 4, master });
        (void) runOut (r);
        const auto last = lastOf (r.events, asked.job);
        ok (asked.rejection == Rejection::None && last && last->kind == EventKind::Damage
                && last->payload.damage.status == MeasurementStatus::Ready
                && s.masters().back().report->damage.status == MeasurementStatus::Ready,
            name + ": its damage graded as any master's, when the shell asks");
    }
}

// [landing.max] <mode> cleaner: every max mode of the shipped config leaves it out, so a wish of theirs lands its zones
// at the loudness the master without them reached (MasterPlan::clean); a mode written cleaner = false (WaterfallTests
// plants it) lands its zones on its budget in one landing.
void maxCleanerSwitch()
{
    const auto engine = detail::rules().engine;
    ok (detail::maxCleaner (engine, LoudnessMode::MaxClean) && detail::maxCleaner (engine, LoudnessMode::MaxDense)
            && detail::maxCleaner (engine, LoudnessMode::MaxExtreme) && detail::maxCleaner (engine, LoudnessMode::MaxNuke)
            && ! detail::maxCleaner (engine, LoudnessMode::Manual),
        "[landing.max] cleaner: every max mode lands cleaner (absent: true), the manual mode never");
    for (const LoudnessMode mode : { LoudnessMode::MaxExtreme, LoudnessMode::MaxNuke })
    {
        const std::string name = mode == LoudnessMode::MaxExtreme ? "maxExtreme" : "maxNuke";
        auto r = gradeSession (3.0);
        if (! r.session) { ok (false, name + ": a session"); continue; }
        auto& s = *r.session;
        command::EditTarget edit { 2, {} };
        edit.fields.loudnessMode = mode;
        GlueFields<Touched> glue; glue.share = 0.1;
        SaturationFields<Touched> saturation; saturation.share = 0.4;
        LimiterFields<Touched> limiter; limiter.cutShare = 0.1;
        const bool edited = act (r, edit).rejection == Rejection::None
            && act (r, command::EditDevice { 3, glue }).rejection == Rejection::None
            && act (r, command::EditDevice { 4, saturation }).rejection == Rejection::None
            && act (r, command::EditDevice { 5, limiter }).rejection == Rejection::None;
        const auto planned = detail::MasterJob::plan (s, command::Master { 6 }, s.project());
        const bool cleaner = true;
        ok (edited && planned.rejection == Rejection::None && planned.loudnessMode == mode && planned.request.waterfall()
                && planned.clean == cleaner && planned.request.limiterGr.limitDb == detail::maxBudgetDb (engine, mode),
            name + ": a wish of shares " + (cleaner ? "lands first without the wishes (clean)" : "lands on the budget at once (not clean)"));
    }
}

// THE FLOOR (owner, 04.10: "always pulled up to −14"): a mix so dense a limiter budget of 0.5 dB holds its landing far
// under −14 LUFS — clicks every 10 ms over a quiet bed — is landed again on [landing.max] floorLufs and delivered there,
// on the maxClean target and with max clean by hand on appleMusic (−16): MaxStop::Floor, its verdict the mode and the
// level alone (616), its numbers in a line for the log (617: the budget, the floor, the P95 it took, above the budget).
// The struck programme, which the budget lands above −14, is not touched (maxMasterLanding: Budget).
std::vector<float> clicks (std::uint32_t rate, double seconds)
{
    const std::size_t frames = std::size_t (double (rate) * seconds);
    std::vector<float> out (2u * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double bed = 0.1 * std::sin (6.283185307179586 * 220.0 * double (i) / double (rate));
        const double click = (i % (rate / 100u)) < 8u ? 0.9 : 0.0;
        out[i] = float (bed + click);
        out[frames + i] = float (0.9 * bed + click);
    }
    return out;
}
void maxFloor()
{
    for (const bool byHand : { false, true })
    {
        const std::string name = byHand ? "max clean by hand on appleMusic (−16)" : "the maxClean target";
        GradeRun r;
        const auto pcm = clicks (48000, 3.0);
        r.session = measuredSession (pcm, 48000);
        if (! r.session) { ok (false, name + ": a session"); continue; }
        auto& s = *r.session;
        if (byHand)
        {
            (void) act (r, command::SetTarget { 2, "appleMusic" });
            command::EditTarget edit { 3, {} };
            edit.fields.loudnessMode = LoudnessMode::MaxClean;
            (void) act (r, edit);
        }
        else (void) act (r, command::SetTarget { 2, "maxClean" });
        const auto from = r.events.size();
        const auto master = deliver (r, 4);
        const std::optional<MasterReport> report = s.masters().empty() ? std::optional<MasterReport> {} : s.masters().back().report;
        // Copies, and where they stood: the record moves as it grows.
        std::optional<Notification> verdict, detail;
        std::size_t verdictAt = 0, detailAt = 0;
        for (std::size_t i = from; i < r.events.size(); ++i)
            if (r.events[i].jobId == master && r.events[i].kind == EventKind::Fact)
            {
                const auto id = r.events[i].payload.fact.view().id;
                if (id == text::FactId::MasterMaxFloor) { verdict = r.events[i]; verdictAt = i; }
                if (id == text::FactId::MasterMaxFloorDetail) { detail = r.events[i]; detailAt = i; }
            }
        const auto said = verdict ? text::Text::text (verdict->payload.fact.view(), text::Lang::En) : std::string {};
        const auto logged = detail ? text::Text::text (detail->payload.fact.view(), text::Lang::En) : std::string {};
        // On the floor within the landing's own tolerance ([landing] toleranceLu), as every landing counts a level.
        const double tolerance = detail::rules().engine.find ("landing").find ("toleranceLu").decimal()->toDouble() + 1e-9;
        const bool atFloor = report && report->achievedLufs && std::fabs (*report->achievedLufs + 14.0) <= tolerance;
        const bool overBudget = detail && detail->payload.fact.view().args[3].number > 0.5
            && detail->payload.fact.view().args[1].number < -14.0 - tolerance;
        ok (master != 0 && report && report->maxStop == MaxStop::Floor && atFloor && verdict && verdict->payload.fact.view().argCount == 2
                && said.starts_with ("Clean: ") && said.find ("budget") == std::string::npos && detail && verdictAt < detailAt
                && overBudget && logged.find ("0.5 dB") != std::string::npos && logged.find ("−14.0 LUFS") != std::string::npos
                && logged.find ("held") == std::string::npos,
            name + ": a dense mix is pulled up to −14 LUFS (" + std::to_string (report ? report->achievedLufs.value_or (0.0) : 0.0)
            + "), said as the mode and the level — \"" + said + "\" — its numbers for the log: \"" + logged + "\"");
    }
    // A FLOOR OUT OF REACH says what held it: the floor pass planted to a single pass ends short of −14 — Passes, not
    // Floor — with no floor line and no log line of it.
    {
        GradeRun r;
        const auto pcm = clicks (48000, 3.0);
        r.session = measuredSession (pcm, 48000);
        auto& s = *r.session;
        (void) act (r, command::SetTarget { 2, "maxClean" });
        const auto job = act (r, readyMaster (s, 3)).job;
        for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
        {
            if (auto* j = detail::Inspector::job (s)) j->floorRequest.maxPasses = 1;
            (void) s.step (1); record (r);
        }
        bool floorLine = false;
        for (const auto& e : r.events)
            if (e.jobId == job && e.kind == EventKind::Fact)
                floorLine = floorLine || e.payload.fact.view().id == text::FactId::MasterMaxFloor
                         || e.payload.fact.view().id == text::FactId::MasterMaxFloorDetail;
        const std::optional<MasterReport> report = s.masters().empty() ? std::optional<MasterReport> {} : s.masters().back().report;
        ok (report && report->maxStop == MaxStop::Passes && report->achievedLufs && *report->achievedLufs < -14.1 && ! floorLine,
            "a floor pass that cannot reach −14 says what held it (Passes), never Floor, and no floor line (stop "
            + std::to_string (report ? unsigned (report->maxStop) : 99u) + ")");
    }
    // ITS LOG LINE CLAIMS NO BUDGET: where the first landing stopped and the P95 taken beside the mode's budget — above it
    // printed above it, within it as it is (a first landing held by its passes may need less than the budget).
    {
        MasterReport floored;
        floored.status = MeasurementStatus::Ready; floored.deliverable = true; floored.achievedLufs = -14.02;
        floored.loudnessMode = LoudnessMode::MaxClean; floored.maxStop = MaxStop::Floor;
        const auto above = MasterReportText::maxFloorDetail (floored, 0.5, 0.51, -14.0, -15.27);
        const auto within = MasterReportText::maxFloorDetail (floored, 0.5, 0.3, -14.0, -14.6);
        MasterReport passes = floored; passes.maxStop = MaxStop::Passes;
        const auto en = above ? text::Text::text (*above, text::Lang::En) : std::string {};
        const auto en2 = within ? text::Text::text (*within, text::Lang::En) : std::string {};
        const auto ru = above ? text::Text::text (*above, text::Lang::Ru) : std::string {};
        ok (en == "Clean: the first landing stopped at −15.3\u00A0LUFS, under −14.0\u00A0LUFS; brought up to −14.0\u00A0LUFS, "
                  "the limiter takes off 0.6\u00A0dB (P95) against the mode's budget of 0.5\u00A0dB."
                && ru.starts_with ("Чисто: первая посадка остановилась на ")
                && en2.find ("takes off 0.3\u00A0dB (P95)") != std::string::npos
                && ! MasterReportText::maxFloorDetail (passes, 0.5, 0.51, -14.0, -15.27),
            "the floor's log line says where the first landing stopped and what the limiter took beside the budget, claiming "
            "no budget held it: \"" + en + "\" / \"" + en2 + "\"");
    }
}

// WHAT ENDED A MAX MODE, rule by rule (detail::maxStopOf), from the landing alone: above the ceiling first; a landing
// that delivered its gentlest render over the budget carries the same status and binding as one the budget held — told
// apart by the delivered render's own excess, said over the budget; then the search's ceiling, the budget, the passes.
void maxStopRules()
{
    using felitronics::mastering::MasteringSolveStatus; using felitronics::mastering::MasteringConstraint;
    detail::MaxStopInputs held;
    held.status = MasteringSolveStatus::TargetUnreachable; held.binding = MasteringConstraint::LimiterGainReduction;
    auto over = held; over.overBudget = true;
    auto above = over; above.peaksAboveCeiling = true;
    auto solved = held; solved.status = MasteringSolveStatus::Solved;
    auto passes = held; passes.status = MasteringSolveStatus::PassLimit;
    ok (detail::maxStopOf (held) == MaxStop::Budget && detail::maxStopOf (over) == MaxStop::OverBudget
            && detail::maxStopOf (above) == MaxStop::TruePeak && detail::maxStopOf (solved) == MaxStop::SearchCeiling
            && detail::maxStopOf (passes) == MaxStop::Passes,
        "a max mode's stop, from the landing: over the budget is not held by it; the ceiling first");
}

// THE BUDGET'S SEARCH SETTLES IN FEW PASSES (v0.16.1): a max master's tight budget (clean 0.5 dB, dense 1.75 dB) found
// by aiming at where the limiter's statistic crosses it — not by a step back of three times the excess and a climb back —
// on a struck programme and on clicks over a bed; each still stopped by its budget, proven.
void maxSearchPasses()
{
    std::string seen;
    bool fewEnough = true, logsWhole = true, progressScaled = true;
    for (const bool dense : { false, true })
        for (const LoudnessMode mode : { LoudnessMode::MaxClean, LoudnessMode::MaxDense })
        {
            GradeRun r;
            const auto pcm = dense ? clicks (48000, 3.0) : struck (48000, 3.0);
            r.session = measuredSession (pcm, 48000);
            if (! r.session) { fewEnough = false; continue; }
            command::EditTarget edit { 2, {} };
            edit.fields.loudnessMode = mode;
            (void) act (r, edit);
            // The budget's passes are the first landing's: a master pulled up to its floor records both landings.
            std::uint32_t budgetPasses = 0;
            if (act (r, readyMaster (*r.session, 3)).rejection == Rejection::None)
                for (unsigned i = 0; i < 400000 && r.session->job() != 0; ++i)
                {
                    if (const auto* j = detail::Inspector::job (*r.session); j && j->floorPass) budgetPasses = j->floorFirstPasses;
                    (void) r.session->step (1); record (r);
                }
            const auto& s = *r.session;
            const auto& kept = s.masters().back();
            const auto passes = kept.landing ? kept.landing->passes : 99u;
            const auto stop = kept.report ? kept.report->maxStop : MaxStop::None;
            if (stop != MaxStop::Floor) budgetPasses = passes;
            seen += std::string (dense ? "clicks " : "struck ") + (mode == LoudnessMode::MaxClean ? "clean " : "dense ")
                  + std::to_string (budgetPasses) + " of " + std::to_string (passes) + " (stop " + std::to_string (unsigned (stop)) + "); ";
            fewEnough = fewEnough && budgetPasses <= 6u && (stop == MaxStop::Budget || stop == MaxStop::Floor)
                && (stop != MaxStop::Floor || (budgetPasses > 0 && passes > budgetPasses));
            bool rowValues = true;
            if (! kept.landing) logsWhole = false;
            else for (const auto& pass : kept.landing->log)
            {
                rowValues = rowValues && pass.limiterP95Db.has_value()
                    && unsigned (pass.reason) <= unsigned (LandingPassReason::DeliverWinner);
            }
            logsWhole = logsWhole && rowValues;
            double previous = 0.0, beforeFinal = 0.0;
            for (const auto& event : r.events)
                if (event.kind == EventKind::Phase)
                {
                    const auto& phase = event.payload.phase;
                    if (phase.name == PhaseName::Pass)
                    {
                        progressScaled = progressScaled && phase.fraction >= previous && phase.fraction < 1.0;
                        previous = beforeFinal = phase.fraction;
                    }
                    if (phase.name == PhaseName::Final)
                        progressScaled = progressScaled && phase.fraction == 1.0 && 1.0 - beforeFinal <= 0.2000000001;
                }
        }
    std::printf ("        max search passes: %s\n", seen.c_str());
    ok (fewEnough, "a max master's budget settles in six passes at most, proven: " + seen);
    ok (logsWhole, "every retained full-file pass says its active P95 and reason");
    ok (progressScaled, "max progress follows its expected full renders monotonically and jumps at most 0.2 at done");
}

// THE GLUE AT MIX 1 IS v0.16.0's DOWNWARD GLUE, TO THE BIT (v0.17.0). The pinned digest is v0.16.0's master on cd of the
// glue tests' mix (GlueSaturationTests.cpp's Mix: a 12 s, 48 kHz kick with a needle and a pad that swells at 6 s, on the
// core's det::sin/det::exp2), untouched: the machine's 2.6 dB glue, written at [compressor] mix = 1. The same master now,
// its glue's mix set to 1, must give it, natively and as wasm; at the default 0.4 it is another.
std::uint64_t glueMasterDigest (bool fullMix)
{
    namespace det = felitronics::core::det;
    constexpr double kPi = 3.141592653589793;
    constexpr unsigned rate = 48000, frames = rate * 12;
    std::vector<float> left (frames), right (frames);
    for (unsigned i = 0; i < frames; ++i)
    {
        const double t = double (i) / rate, beat = double (i % (rate / 2)) / rate;
        const double kick = 0.30 * det::exp2 (-beat / 0.04) * det::sin (2 * kPi * 60.0 * beat)
                          + (beat < 0.004 ? 0.10 * (double ((i * 2654435761u) >> 16 & 0xffffu) / 32768.0 - 1.0) : 0.0);
        const double swell = t < 6.0 ? 0.6 : 1.0;
        const double pad = swell * (0.05 * det::sin (2 * kPi * 220.0 * t) + 0.04 * det::sin (2 * kPi * 331.0 * t));
        left[i] = float (kick + pad);
        right[i] = float (kick + swell * (0.05 * det::sin (2 * kPi * 220.0 * t + 0.4) + 0.04 * det::sin (2 * kPi * 331.0 * t + 1.1)));
    }
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create();
    if (made.status != Status::Ok) return 0;
    auto& s = *made.session;
    if (s.apply (command::SetTarget { 1, "cd" }).rejection != Rejection::None
        || s.apply (command::Load { 2, { planes, 2, frames, rate }, { "mix.wav", rate, true, 24 } }).rejection != Rejection::None) return 0;
    for (unsigned i = 0; i < 4000000 && (s.measurementJob() || s.needlesJob()); ++i) (void) s.step (16);
    GlueFields<Touched> all; all.mix = 1.0;
    if (fullMix && s.apply (command::EditDevice { 3, all }).rejection != Rejection::None) return 0;
    const auto view = s.snapshot();
    if (view.view().plan.glue.state != GlueState::Active || s.apply (command::Master { 4 }).rejection != Rejection::None) return 0;
    for (unsigned i = 0; i < 4000000 && s.job() != 0; ++i) (void) s.step (16);
    const auto token = s.pendingMaster();
    const auto shape = s.masterAudioShape (token);
    std::vector<float> out (std::size_t (shape.frames * shape.channels));
    if (out.empty() || s.copyMaster (token, out) != MasterTransferStatus::Ok) return 0;
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const float v : out)
        for (unsigned k = 0; k < 4; ++k) h = (h ^ ((std::bit_cast<std::uint32_t> (v) >> (8u * k)) & 0xFFu)) * 0x100000001B3ull;
    return h;
}

void glueAtFullMix()
{
    constexpr std::uint64_t kDownwardGlue = 0x80741a2cc3a4130aull;
    const std::uint64_t full = glueMasterDigest (true), parallel = glueMasterDigest (false);
    char digest[17];
    std::snprintf (digest, sizeof digest, "%016llx", (unsigned long long) full);
    ok (full == kDownwardGlue, std::string ("the glue at mix 1: v0.16.0's downward glue to the bit, ") + digest);
    ok (parallel != 0 && parallel != kDownwardGlue, "the glue at its default 0.4: another master");
}

// THE FLOOR LANDING CONTINUES THE MASTER'S RECORD: the first landing's passes stay in the master's pass log ahead of the
// floor landing's, with their reasons; the pass number never starts again and the bar keeps moving through the floor.
void maxFloorRecord()
{
    GradeRun r;
    const auto pcm = clicks (48000, 3.0);
    r.session = measuredSession (pcm, 48000);
    if (! r.session) { ok (false, "the floor record: a session"); return; }
    auto& s = *r.session;
    (void) act (r, command::SetTarget { 2, "maxClean" });
    const auto job = act (r, readyMaster (s, 3)).job;
    std::uint32_t lastPass = 0, passFacts = 0, floorFacts = 0;
    double lastFraction = 0.0, atSwitch = -1.0, floorSecond = -1.0;
    bool monotone = true, switched = false;
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
    {
        (void) s.step (1);
        const auto* j = detail::Inspector::job (s);
        const bool floorNow = j && j->floorPass;
        if (floorNow && ! switched) { switched = true; atSwitch = lastFraction; }
        for (const auto& e : s.events())
        {
            if (e.jobId != job) continue;
            if (e.kind == EventKind::Phase && (e.payload.phase.name == PhaseName::Pass || e.payload.phase.name == PhaseName::Final))
            {
                monotone = monotone && e.payload.phase.pass >= lastPass && e.payload.phase.fraction >= lastFraction;
                lastPass = e.payload.phase.pass; lastFraction = e.payload.phase.fraction;
            }
            if (e.kind == EventKind::Fact && e.payload.fact.view().id == text::FactId::MasterPass)
            {
                ++passFacts;
                // The floor landing's second pass begins: its first has rendered, and the bar has moved with it.
                if (floorNow && ++floorFacts == 2) floorSecond = lastFraction;
            }
        }
        record (r);
    }
    const auto first = passFacts - floorFacts;
    const auto masters = s.masters();
    const std::optional<LandingSummary> kept = masters.empty() ? std::optional<LandingSummary> {} : masters.back().landing;
    const auto* landing = kept ? &*kept : nullptr;
    bool underFloor = false;
    for (std::size_t i = 0; landing && i < first && i < landing->log.size(); ++i)
        underFloor = underFloor || landing->log[i].achievedLufs < -14.1;
    ok (switched && first > 0 && floorFacts > 0 && landing && landing->passes == passFacts && landing->log.size() == passFacts
            && landing->log[0].reason == LandingPassReason::AimAtTarget && underFloor
            && landing->log[first].reason == LandingPassReason::AimAtTarget,
        "a max master pulled up to its floor keeps both landings in its record: " + std::to_string (first) + " + "
            + std::to_string (floorFacts) + " passes, recorded " + std::to_string (landing ? landing->passes : 0u));
    ok (monotone, "its pass number and its bar never go back across the floor landing");
    ok (switched && floorSecond > atSwitch, "its bar moves with the floor landing's first render ("
        + std::to_string (atSwitch) + " -> " + std::to_string (floorSecond) + ")");
}
int main()
{
    damageResampler();
    manualBudgetResolution();
    glueAtFullMix();
    maxSearchPasses();
    damageGrades();
    lateMasterCrestLifecycle();
    maxMasterLanding();
    maxCleanerSwitch();
    maxFloor(); maxFloorRecord();
    maxStopRules();
    damageWithoutAnId();
    damageReferenceBelowTheKnob();
    damageFailedMidway();
    damageMemoryIsDeclared();
    masterAtTheCeilingWithADamage();
    programmeBeforeAnyMaster();
    constexpr std::uint32_t rate = 48000;
    std::vector<float> left (2u * rate), right (2u * rate);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f;
        right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f;
    }
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create();
    ok (made.status == Status::Ok, "session created");
    auto& s = *made.session;
    const auto loaded = s.apply (command::Load { 1, { planes, 2, left.size(), rate }, { "test.wav", rate, true, 24 } });
    ok (loaded.rejection == Rejection::None, "source loaded");
    for (unsigned i = 0; i < 20000 && (s.state() == State::Loaded || ! s.snapshot().view().mandatoryMeasurementsReady); ++i)
        (void) s.step (16);
    ok (s.snapshot().view().mandatoryMeasurementsReady, "mandatory LUFS and true peak are ready");
    command::Master ready { 2 };
    ready.ready.version = 1;
    ready.ready.topology.eq = false;
    ready.ready.topology.compressor = false;
    ready.ready.topology.dither = false;
    ready.source = s.source().hash; ready.revision = s.revision();
    const auto declared = s.check (ready);
    Answer started;
    const auto spent = declared::spend ([&] { started = s.apply (ready); });
    ok (declared.rejection == Rejection::None && started.rejection == Rejection::None
        && declared::covers (declared.bytes, spent), "ready master preflight covers preparation");
    const auto recipe = s.jobRecipe();
    const auto target = s.apply (command::SetTarget { 3, "club" });
    ok (target.rejection == Rejection::None && s.jobRecipe().readyHash == recipe.readyHash
        && s.jobRecipe().project.target == recipe.project.target, "project edits do not change the running recipe");
    std::uint64_t largestStep = 0;
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i)
    {
        const auto work = declared::spend ([&] { (void) s.step (1); });
        largestStep = std::max (largestStep, std::uint64_t (work.bytes));
    }
    ok (s.job() == 0 && s.masters().size() == 1, "real search finished and retained one master");
    if (s.masters().empty()) return felitronics::test::report();
    ok (largestStep <= declared.bytes, "later solver allocations fit the command declaration");
    const auto& kept = s.masters().back();
    ok (kept.recipe.readyHash == recipe.readyHash && kept.landing && kept.landing->passes <= 12,
        "completed metadata keeps the ready recipe and one pass budget");
    ok (kept.landing && kept.landing->deliverable, "safe result retains transferable PCM");
    const auto token = s.pendingMaster();
    const auto shape = s.masterAudioShape (token);
    ok (token.source == s.source().hash && token.job == kept.id && token.master == kept.id
        && shape.frames == left.size() && shape.channels == 2 && shape.sampleRate == rate,
        "transfer identity and delivered shape are published");
    std::vector<float> copy (std::size_t (shape.frames * shape.channels));
    ok (s.copyMaster (token, copy) == MasterTransferStatus::Ok
        && s.copyMaster ({ token.source + 1u, token.revision, token.job, token.master }, copy) == MasterTransferStatus::Stale,
        "copy validates all transfer identity fields");
    float invalidSample = std::numeric_limits<float>::quiet_NaN();
    const float* invalidPlanes[] { &invalidSample, &invalidSample };
    Answer invalidLoad;
    const auto invalidSpent = declared::spend ([&] { invalidLoad = s.apply (command::Load {
        4, { invalidPlanes, 2, 1, rate }, { "invalid.wav", rate, true, 24 } }); });
    ok (invalidLoad.rejection == Rejection::NotFinite && invalidSpent.requests == 0
        && s.pendingMaster().master == token.master && s.masterAudioBytes (token) == copy.size() * sizeof (float),
        "rejected source replacement retains the finished transfer without allocating");

    std::vector<float> otherLeft = left;
    otherLeft[0] += 0.125f;
    const float* otherPlanes[] { otherLeft.data(), right.data() };
    auto otherCreated = Session::create();
    auto& other = *otherCreated.session;
    ok (other.apply (command::Load { 1, { otherPlanes, 2, otherLeft.size(), rate },
        { "other.wav", rate, true, 24 } }).rejection == Rejection::None, "another session loads an independent source");
    for (unsigned i = 0; i < 20000 && (other.state() == State::Loaded
         || ! other.snapshot().view().mandatoryMeasurementsReady); ++i) (void) other.step (16);
    auto otherReady = ready; otherReady.id = 2; otherReady.source = other.source().hash;
    otherReady.revision = other.revision();
    const auto otherStart = other.apply (otherReady);
    ok (otherStart.rejection == Rejection::None && other.apply (command::Cancel { 3, otherStart.job }).rejection == Rejection::None
        && other.job() == 0 && ! other.masterWavPlan (other.pendingMaster())
        && s.masterAudioBytes (token) != 0, "cancel in another session leaves this completed PCM intact and no false file");
    auto invalidReady = otherReady; invalidReady.id = 4; invalidReady.revision = other.revision();
    invalidReady.ready.params.inputGainDb = std::numeric_limits<double>::quiet_NaN();
    Answer invalidMaster;
    const auto refusalSpent = declared::spend ([&] { invalidMaster = other.apply (invalidReady); });
    ok (invalidMaster.rejection == Rejection::NotFinite && refusalSpent.requests == 0
        && other.job() == 0 && ! other.masterWavPlan (other.pendingMaster()),
        "invalid ready request refuses before allocation and creates no downloadable master");

    ok (s.releaseMaster (token) == MasterTransferStatus::Ok
        && s.releaseMaster (token) == MasterTransferStatus::Unknown
        && s.snapshot().view().canMaster, "release is explicit and permits the next master");
    const auto olderMasterId = kept.id;
    auto next = ready; next.id = 5; next.revision = s.revision();
    const auto again = s.apply (next);
    const auto forgottenWhileRunning = s.apply (command::Forget { 51, olderMasterId });
    ok (again.rejection == Rejection::None && forgottenWhileRunning.rejection == Rejection::None
        && s.job() == again.job && s.masters().empty(),
        "forgetting an older master leaves the active render intact");
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i) (void) s.step (16);
    ok (s.masters().size() == 1 && s.masters().front().landing
        && s.pendingMaster().master == again.job,
        "forgetting an older master preserves active rows through completion");
    const auto secondToken = s.pendingMaster();
    MasterAudio moved;
    ok (again.rejection == Rejection::None && secondToken.master != 0
        && s.takeMaster (secondToken, moved) == MasterTransferStatus::Ok
        && moved.samples && moved.frames == left.size() && s.pendingMaster().master == 0,
        "native take moves owned PCM without a full copy");
    next.id = 6; next.revision = s.revision();
    const auto cancelled = s.apply (next);
    ok (cancelled.rejection == Rejection::None
        && s.apply (command::Cancel { 7, cancelled.job }).rejection == Rejection::None
        && s.masters().size() == 1, "cancel discards only unfinished work and retains completed metadata");
    next.id = 8; next.revision = s.revision();
    const auto replacing = s.apply (next);
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i) (void) s.step (16);
    const auto oldToken = s.pendingMaster();
    ok (replacing.rejection == Rejection::None && oldToken.master != 0, "a later master is independently transferable");
    const auto replaced = s.apply (command::Load { 9, { otherPlanes, 2, otherLeft.size(), rate },
        { "other.wav", rate, true, 24 } });
    ok (replaced.rejection == Rejection::None && s.masterAudioBytes (oldToken) == 0
        && s.copyMaster (oldToken, copy) != MasterTransferStatus::Ok && s.masters().empty(),
        "new source fences the old transfer and drops its PCM while keeping the moved caller buffer");
    auto unavailableCreated = Session::create();
    auto& unavailable = *unavailableCreated.session;
    const float shortSamples[] { 0.0f, 0.25f, -0.25f, 0.0f };
    const float* shortPlanes[] { shortSamples, shortSamples };
    const auto shortLoad = unavailable.apply (command::Load { 1, { shortPlanes, 2, 4, rate }, {} });
    ok (shortLoad.rejection == Rejection::None
        && detail::Driver::measured1 (unavailable, shortLoad.job, unavailable.source().hash),
        "unavailable mandatory reading fixture reaches a measured state");
    auto impossible = ready; impossible.source = unavailable.source().hash;
    impossible.revision = unavailable.revision();
    const auto refused = unavailable.apply (impossible);
    ok (refused.rejection == Rejection::MandatoryUnavailable && unavailable.pendingMaster().master == 0
        && ! unavailable.masterWavPlan (unavailable.pendingMaster()),
        "unavailable mandatory LUFS or true peak forbids ready PCM");
    for (unsigned i = 0; i < 20000 && (s.state() == State::Loaded
         || ! s.snapshot().view().mandatoryMeasurementsReady); ++i) (void) s.step (16);
    const auto demanding = s.apply (command::EditTarget { 10, { -5.0, -6.0 } });
    auto missReady = ready; missReady.id = 11; missReady.source = s.source().hash;
    missReady.revision = s.revision();
    const auto missStart = s.apply (missReady);
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i) (void) s.step (16);
    ok (s.masters().size() == 1, "demanding target publishes one retained result");
    if (s.masters().empty()) return felitronics::test::report();
    const auto& miss = s.masters().back();
    // The loud target's budget, as the config states it: −5 LUFS is above the middle step's −8.
    const auto rules = felitronics::session::config::Config::load();
    const auto& landingRules = rules.config.engine.landing;
    const double loudBudget = landingRules.middleLufs.max < -5.0 ? landingRules.loudBudgetDb : std::numeric_limits<double>::quiet_NaN();
    ok (demanding.rejection == Rejection::None && missStart.rejection == Rejection::None
        && miss.landing && miss.landing->status == LandingStatus::TargetUnreachable
        && miss.landing->binding == LandingConstraint::None && miss.landing->limiterWall
        && miss.landing->limiterSlope && *miss.landing->limiterSlope < .2
        && miss.landing->deliverable && ! miss.landing->peaksAboveCeiling && miss.landing->passes < 12
        && miss.report && miss.report->cost && miss.report->cost->limiterP95Db.value && *miss.report->cost->limiterP95Db.value <= loudBudget
        && miss.report && ! miss.report->peaksAboveCeiling && ! MasterReportText::peaksAboveCeiling (*miss.report)
        && miss.landing->truePeakDbTp && *miss.landing->truePeakDbTp <= -6.0
        && s.pendingMaster().master == miss.id && s.masterWavPlan (s.pendingMaster()),
        "an unreachable loudness goal stops at the limiter wall inside the 7.5 dB budget, and retains ceiling-safe PCM");
    {
        // −6 LUFS under −6 dBTP: the slope stops this landing before the old pass limit; the budget is not its cause.
        auto ceilingMade = Session::create();
        auto& c = *ceilingMade.session;
        (void) c.apply (command::Load { 1, { planes, 2, left.size(), rate }, { "test.wav", rate, true, 24 } });
        for (unsigned i = 0; i < 20000 && (c.state() == State::Loaded || ! c.snapshot().view().mandatoryMeasurementsReady); ++i)
            (void) c.step (16);
        const auto named = c.apply (command::SetTarget { 2, "allStreaming" });
        const auto edited = c.apply (command::EditTarget { 3, { -6.0, -6.0 } });
        auto ceilingReady = ready; ceilingReady.id = 4; ceilingReady.source = c.source().hash; ceilingReady.revision = c.revision();
        const auto ceilingStart = c.apply (ceilingReady);
        for (unsigned i = 0; i < 40000 && c.job() != 0; ++i) (void) c.step (16);
        const auto masters = c.masters();
        const bool landed = ! masters.empty() && masters.back().landing;
        const LandingSummary l = landed ? *masters.back().landing : LandingSummary {};
        std::printf ("    −6 LUFS under −6 dBTP: status %d binding %d, %.3f LUFS, %u passes\n", landed ? (int) l.status : -1,
                     landed ? (int) l.binding : -1, landed && l.achievedLufs ? *l.achievedLufs : 0.0, landed ? l.passes : 0u);
        ok (named.rejection == Rejection::None && edited.rejection == Rejection::None && ceilingStart.rejection == Rejection::None && landed && l.deliverable
            && l.status == LandingStatus::TargetUnreachable && l.limiterWall
            && l.binding == LandingConstraint::None && ! budgetProven (l),
            "−6 LUFS under −6 dBTP reaches the limiter wall without naming the budget");
    }
    const auto missToken = s.pendingMaster();
    const auto filePlan = s.masterWavPlan (missToken);
    std::vector<std::uint8_t> file (std::size_t (filePlan.bytes), 0u);
    bool fileCopied = bool (filePlan);
    for (std::size_t at = 0; fileCopied && at < file.size(); at += 4093u)
        fileCopied &= s.copyMasterWav (missToken, at,
            { file.data() + at, std::min<std::size_t> (4093u, file.size() - at) }) == MasterTransferStatus::Ok;
    ok (fileCopied && filePlan.bits == 24 && file.size() == 44u + filePlan.dataBytes + (filePlan.dataBytes & 1u),
        "safe miss yields one complete WAV through bounded copies");
    if (fileCopied && filePlan.bits == 24)
    {
        std::vector<float> decoded (std::size_t (filePlan.frames * filePlan.channels));
        for (std::size_t frame = 0; frame < filePlan.frames; ++frame)
            for (std::size_t channel = 0; channel < filePlan.channels; ++channel)
            {
                const auto at = 44u + (frame * filePlan.channels + channel) * 3u;
                const auto raw = std::uint32_t (file[at]) | (std::uint32_t (file[at + 1]) << 8)
                    | (std::uint32_t (file[at + 2]) << 16);
                const auto code = std::int32_t (raw) - ((raw & 0x800000u) ? 0x1000000 : 0);
                decoded[channel * filePlan.frames + frame] = float (double (code) / 8388608.0);
            }
        felitronics::analysis::ReferenceTruePeakMeter meter;
        bool measured = meter.prepare (filePlan.rate, 1024, int (filePlan.channels));
        for (std::size_t at = 0; measured && at < filePlan.frames; at += 1024u)
        {
            const float* block[] { decoded.data() + at, decoded.data() + filePlan.frames + at };
            measured &= meter.process (block, int (filePlan.channels),
                int (std::min<std::size_t> (1024u, filePlan.frames - at)));
        }
        if (measured) meter.drain();
        ok (measured && miss.report && meter.truePeakDb() <= miss.report->ceilingDbTp,
            "decoded safe-miss WAV respects the reference true-peak ceiling");
    }
    const auto sidecarToken = s.pendingMaster();
    const auto sidecarBefore = s.liveBytes();
    MeasuredSource replacementFacts { "sidecar.wav", s.source().hash + 1u, left.size(), rate, 2, rate, 24,
        true, -18.0, -2.0 };
    const auto sidecarDemand = s.loadMeasuredStorage (replacementFacts);
    Answer sidecarLoaded;
    const auto sidecarSpent = declared::spend ([&] { sidecarLoaded = s.loadMeasured (12, replacementFacts); });
    ok (sidecarDemand.rejection == Rejection::None && sidecarLoaded.rejection == Rejection::None
        && declared::covers (sidecarDemand.bytes, sidecarSpent)
        && s.pendingMaster().master == 0 && s.masterAudioBytes (sidecarToken) == 0
        && s.masters().empty() && detail::Inspector::noMasterOwners (s)
        && ! s.snapshot().view().canMaster && s.snapshot().view().pendingMasterBytes == 0.0
        && s.liveBytes() < sidecarBefore - double (left.size() * 2u * sizeof (float)),
        "sidecar replacement releases the prior PCM, token, rows and job owners");
    std::vector<float> transient (2u * rate, 0.003f);
    transient.back() = 1.0f;
    const float* transientPlanes[] { transient.data(), transient.data() };
    auto unsafeCreated = Session::create();
    auto& unsafe = *unsafeCreated.session;
    ok (unsafe.apply (command::Load { 1, { transientPlanes, 2, transient.size(), rate }, {} }).rejection == Rejection::None,
        "transient source loads for true-peak refusal");
    for (unsigned i = 0; i < 20000 && (unsafe.state() == State::Loaded
         || ! unsafe.snapshot().view().mandatoryMeasurementsReady); ++i) (void) unsafe.step (16);
    const auto sourceFacts = unsafe.snapshot();
    double inputLufs = std::numeric_limits<double>::quiet_NaN();
    for (const auto& number : sourceFacts.view().measurements[std::size_t (Analyzer::Loudness)].numbers)
        if (number.name == "integratedLufs" && number.value) inputLufs = *number.value;
    const auto unsafeTarget = unsafe.apply (command::EditTarget { 2, { -14.0, -6.0 } });
    auto unsafeReady = ready; unsafeReady.id = 3; unsafeReady.source = unsafe.source().hash;
    unsafeReady.revision = unsafe.revision(); unsafeReady.ready.topology.limiter = false;
    unsafeReady.ready.params.inputGainDb = 59.0 - (-18.0 - inputLufs);
    const auto unsafeStart = unsafe.apply (unsafeReady);
    for (unsigned i = 0; i < 40000 && unsafe.job() != 0; ++i) (void) unsafe.step (16);
    // No render under the ceiling (owner, 01.10): the file is still delivered — the render that overshoots the ceiling
    // least, the gentlest measured — and its true peak above the ceiling is what the master says.
    const bool unsafeKept = unsafeTarget.rejection == Rejection::None && unsafeStart.rejection == Rejection::None
        && unsafe.masters().size() == 1 && unsafe.masters().back().landing && unsafe.masters().back().report;
    ok (unsafeKept && unsafe.masters().back().landing->deliverable && unsafe.masters().back().report->deliverable
        && unsafe.pendingMaster().master == unsafe.masters().back().id && unsafe.masterWavPlan (unsafe.pendingMaster())
        && unsafe.masters().back().report->truePeakDbTp
        && *unsafe.masters().back().report->truePeakDbTp > unsafe.masters().back().report->ceilingDbTp,
        "no render under the ceiling still yields transferable PCM, its true peak above the ceiling");
    if (unsafeKept && unsafe.masters().back().landing->truePeakDbTp)
    {
        const auto masters = unsafe.masters();
        const auto& kept = *masters.back().landing;
        bool allAbove = ! kept.log.empty(), gentlest = true;
        for (const auto& pass : kept.log)
        {
            allAbove = allAbove && ! pass.ceilingSafe;
            gentlest = gentlest && *kept.truePeakDbTp <= pass.truePeakDbTp;
        }
        ok (allAbove && gentlest, "the delivered render is the gentlest measured: no pass overshoots the ceiling less");
    }
    else ok (false, "PRECONDITION: the unsafe master keeps a measured true peak");
    // ...marked: the landing and the report carry it, the report's two numbers say it, fact 98 names both; the miss's
    // line (which says the true peak held) is not said of it, and a safe master is not marked.
    if (unsafeKept)
    {
        const auto masters = unsafe.masters();
        const auto& report = *masters.back().report;
        const auto above = MasterReportText::peaksAboveCeiling (report);
        ok (masters.back().landing->peaksAboveCeiling && report.peaksAboveCeiling && ! report.peakSafe
            && above && above->id == text::FactId::MasterPeaksAboveCeiling && above->argCount == 2
            && report.truePeakDbTp && above->args[0].number == *report.truePeakDbTp
            && above->args[1].number == report.ceilingDbTp && ! MasterReportText::miss (report),
            "the master above the ceiling is marked: the landing, the report, fact 98 with its true peak and ceiling");
    }
    // ...and the verdict says why (slice 5): the target out of reach, the true-peak ceiling holding it — fact 89 with its
    // limit — where it used to say nothing at all (Unavailable).
    if (! unsafe.masters().empty() && unsafe.masters().back().landing && unsafe.masters().back().report)
    {
        const auto masters = unsafe.masters();
        const auto& lost = *masters.back().landing;
        const auto verdict = MasterReportText::landing (*masters.back().report, lost, 0.1);
        ok (lost.status == LandingStatus::TargetUnreachable && lost.binding == LandingConstraint::TruePeakCeiling
            && verdict && verdict->id == text::FactId::MasterLandingUnreachable && verdict->argCount == 2
            && verdict->args[1].termId == text::Term::LandingLimitTruePeak,
            "no render under the ceiling: unreachable, the true-peak ceiling named, fact 89");
    }
    else ok (false, "PRECONDITION: the unsafe master keeps its landing and report");
    std::printf ("wav-outcomes=cancel:false,refusal:false,unavailable:false,miss:true,unsafe:true\n");
    return felitronics::test::report();
}
