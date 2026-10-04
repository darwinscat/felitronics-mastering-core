// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include "Driver.h"
#include "Damage.h"
#include "MasterJob.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
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
            && ! s.damageJob_ && s.damageJobId_ == 0 && s.damageJobBytes_ == 0;
    }
    static void lastJob (Session& s, JobId last) noexcept { s.lastJob_ = last; }
    static std::uint64_t damageBytes (const Session& s) noexcept { return s.damageJobBytes_; }
    // A walk that fails midway (a refusal of the chains, the meters or PEAQ — a contract fault no input reaches).
    static std::uint32_t windows (const Damage& d) noexcept { return d.windows_; }
    static void fail (Damage& d, MeasurementReason why) noexcept { d.fail (why); }
    // The master's job, for a test that plants a guard's numbers or fails its walk midway.
    static MasterJob* job (Session& s) noexcept { return s.masterJob_.get(); }
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

// THE DAMAGE'S JOB WITH NO ID LEFT: the master takes the last id there is; delivered as ever, its damage is not graded —
// Unavailable, NoJobId — and its line says so, with no job and no Damage event.
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
    bool line = false, damageEvent = false;
    bool more = true;
    for (unsigned i = 0; i < 40000 && more; ++i)
    {
        more = s.step (1).state == StepState::More;
        for (const auto& e : s.events())
        {
            damageEvent = damageEvent || e.kind == EventKind::Damage;
            if (e.kind == EventKind::Fact && e.payload.fact.view().id == text::FactId::MasterDamageUnmeasured)
                line = e.payload.fact.view().args[0].termId == text::Term::ReasonNoJobId;
        }
    }
    const auto& d = s.masters().empty() ? MasterDamage {} : s.masters()[0].report->damage;
    ok (started.job == std::numeric_limits<JobId>::max() && s.masters().size() == 1 && s.pendingMaster().master == started.job
        && s.damageJob() == 0 && ! damageEvent && line
        && d.status == MeasurementStatus::Unavailable && d.reason == MeasurementReason::NoJobId,
        "a master with the last job id is delivered; its damage has no job to grade it: Unavailable, NoJobId, said so");
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
        std::uint64_t declaredBytes = 0, asked = 0;
        bool started = false;
        for (unsigned i = 0; i < 400000 && (s.job() != 0 || s.damageJob() != 0); ++i)
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

// A MASTER ASKED WHILE A DAMAGE IS GRADED is priced without it: the new master stops it before allocating, so the room
// that job holds is the new master's. At the ceiling of exactly what the master needs then it is taken, and stays under
// it; a byte lower it is refused, naming that need.
void masterAtTheCeilingWithADamage()
{
    const auto pcm = struck (48000, 3.0);
    auto sp = measuredSession (pcm, 48000);
    if (! sp) { ok (false, "a session for the ceiling"); return; }
    auto& s = *sp;
    (void) s.apply (readyMaster (s, 2));
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i) (void) s.step (1);
    // Into the damage's first walk: its chains, meters and PEAQ allocated.
    for (unsigned i = 0; i < 64 && s.damageJob() != 0; ++i) (void) s.step (1);
    const bool released = s.releaseMaster (s.pendingMaster()) == MasterTransferStatus::Ok;
    const auto request = readyMaster (s, 3);
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

// THE PROGRAMME REPORT HAS ENDED WHENEVER A MASTER IS TAKEN: the master's loudness range reads the input's from it, so a
// master that could be asked before it would leave its range's change Pending for good. The table never lets one: the
// first measurement — which a master waits for — ends after the programme report, on a fresh load, after a measurement
// stopped between the loudness and the report and continued, and after the same source loaded again.
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
        unsigned taken = 0, early = 0;
        for (unsigned i = 0; i < 200000; ++i)
        {
            if (s.check (readyMaster (s, 9)).rejection == Rejection::None)
            { ++taken; early += status (Analyzer::Programme) == MeasurementStatus::Pending ? 1u : 0u; }
            if (s.step (1).state == StepState::Done) break;
        }
        ok (between && taken > 0 && early == 0,
            "path " + std::to_string (path) + ": a master is taken " + std::to_string (taken) + " times and never before the "
            "programme report has ended");
    }
}

// A MAX MASTER (v0.15.0) grades its landing's render with PEAQ before the file is delivered: that guard runs inside the
// master's own declaration (its walks priced there as the damage's job, the master's buffers alive beside them), no
// damage job follows the master, and the report carries the mode, what stopped it and the guard's grade. A step back
// frees its walk before the next is made, so a guard that took none has asked for no more than the declaration.
void maxMasterGuarded()
{
    std::uint32_t stepsBack = 0;
    for (const LoudnessMode mode : { LoudnessMode::MaxClean, LoudnessMode::MaxDense })
    {
        const std::string name = mode == LoudnessMode::MaxClean ? "maxClean" : "maxDense";
        const auto pcm = struck (48000, 3.0);
        auto sp = measuredSession (pcm, 48000);
        if (! sp) { ok (false, "a session for " + name); continue; }
        auto& s = *sp;
        command::EditTarget edit { 2, {} };
        edit.fields.loudnessMode = mode;
        const bool edited = s.apply (edit).rejection == Rejection::None && s.snapshot().view().loudnessMode == mode;
        const auto request = readyMaster (s, 3);
        const auto quoted = s.check (request);
        declared::LifecycleBudget demand { quoted.bytes, quoted.largestBlockBytes + 4096u };
        Answer taken;
        bool within = demand.charge (declared::spend ([&] { taken = s.apply (request); }));
        bool damageSeen = false;
        for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
        {
            within = demand.charge (declared::spend ([&] { (void) s.step (1); })) && within;
            damageSeen = damageSeen || s.damageJob() != 0;
        }
        if (s.masters().empty()) { ok (false, name + ": a master is kept"); continue; }
        const std::optional<MasterReport> kept = s.masters().back().report;
        if (! kept) { ok (false, name + ": the master carries its report"); continue; }
        const MasterReport& r = *kept;
        ok (edited && quoted.rejection == Rejection::None && taken.rejection == Rejection::None && ! damageSeen
                && s.damageJob() == 0 && r.loudnessMode == mode && r.maxStop != MaxStop::None
                && r.damage.status == MeasurementStatus::Ready && r.damage.grade >= 1u && within,
            name + ": the guard grades the render before the file, inside the master's declaration (" + std::to_string (demand.allocated)
            + " of " + std::to_string (quoted.bytes) + " bytes, " + std::to_string (r.guardSteps) + " steps back, stop "
            + std::to_string (unsigned (r.maxStop)) + "), and no damage job follows");
        // THE GUARD'S VERDICT ([landing.max] floorOdg: clean -0.5, dense -1.5; guardSteps 3): a guard that stepped back
        // and stopped delivers a render whose worst window is above the floor; one that ran out of steps delivers the
        // gentlest it graded, under the floor, and says so.
        const double floor = mode == LoudnessMode::MaxClean ? -0.5 : -1.5;
        const bool verdict = r.maxStop == MaxStop::Guard ? r.guardSteps >= 1u && r.damage.worstOdg && *r.damage.worstOdg > floor
                           : r.maxStop == MaxStop::GuardUnmet ? r.guardSteps == 3u && r.damage.worstOdg && *r.damage.worstOdg <= floor
                           : r.guardSteps == 0u && (! r.damage.worstOdg || *r.damage.worstOdg > floor);
        ok (verdict, name + ": the guard's stop agrees with its grade against the floor (worst ODG "
            + std::to_string (r.damage.worstOdg.value_or (0.0)) + ", " + std::to_string (r.guardSteps) + " steps)");
        stepsBack += r.guardSteps;
        // Its verdict names the mode and what ended it, whole, in both languages.
        const auto said = MasterReportText::max (r, mode == LoudnessMode::MaxClean ? 3.0 : 7.0, std::numeric_limits<double>::quiet_NaN());
        const auto wanted = r.maxStop == MaxStop::Guard ? text::FactId::MasterMaxGuard
                          : r.maxStop == MaxStop::GuardUnmet ? text::FactId::MasterMaxGuardUnmet
                          : r.maxStop == MaxStop::Budget ? text::FactId::MasterMaxBudget : text::FactId::MasterMaxPasses;
        ok (said && said->id == wanted && text::Text::complete (*said)
                && text::Text::text (*said, text::Lang::En).starts_with (mode == LoudnessMode::MaxClean ? "Maximum · clean: " : "Maximum · dense: ")
                && text::Text::text (*said, text::Lang::Ru).starts_with (mode == LoudnessMode::MaxClean ? "Максимум · чисто: " : "Максимум · плотно: "),
            name + ": the verdict names the mode and its stop: " + (said ? text::Text::text (*said, text::Lang::En) : std::string ("none")));
    }
    ok (stepsBack > 0u, "PRECONDITION: the struck programme steps a guard back (the guard's path is exercised)");
    // The budget's verdict, word for word: the mode, the file's level and the mode's budget, printed whole.
    MasterReport held;
    held.status = MeasurementStatus::Ready; held.deliverable = true; held.achievedLufs = -9.94;
    held.loudnessMode = LoudnessMode::MaxClean; held.maxStop = MaxStop::Budget;
    const auto budget = MasterReportText::max (held, 3.0, std::numeric_limits<double>::quiet_NaN());
    MasterReport manual = held;
    manual.loudnessMode = LoudnessMode::Manual;
    ok (budget && text::Text::text (*budget, text::Lang::Ru)
                == "Максимум · чисто: −9,9\u00A0LUFS — дальше лимитеру пришлось бы срезать больше 3\u00A0дБ (P95), это предел режима."
            && text::Text::text (*budget, text::Lang::En)
                == "Maximum · clean: −9.9\u00A0LUFS — further, the limiter would have to take off more than 3\u00A0dB (P95), the mode's limit."
            && ! MasterReportText::max (manual, 3.0, std::numeric_limits<double>::quiet_NaN()),
        "a max master held by its budget says the mode, the level and the budget; a manual one has no max verdict: "
            + (budget ? text::Text::text (*budget, text::Lang::Ru) : std::string ("none")));
    // Over the budget (fact 615): the delivered reduction reads above the budget, however close above it — rounded up to
    // a tenth, at least a tenth above the budget's tenths: 3.01 over 3 prints 3.1, 7.26 over 7.25 prints 7.3.
    MasterReport broke = held;
    broke.maxStop = MaxStop::OverBudget;
    const auto over301 = MasterReportText::max (broke, 3.0, 3.01), over726 = MasterReportText::max (broke, 7.25, 7.26);
    const auto overEdge = MasterReportText::max (broke, 3.0, std::nextafter (3.0, 4.0));
    const auto ru301 = over301 ? text::Text::text (*over301, text::Lang::Ru) : std::string {};
    const auto en726 = over726 ? text::Text::text (*over726, text::Lang::En) : std::string {};
    const auto ruEdge = overEdge ? text::Text::text (*overEdge, text::Lang::Ru) : std::string {};
    ok (ru301.find ("срезает 3,1\u00A0дБ") != std::string::npos && en726.find ("takes off 7.3\u00A0dB") != std::string::npos
            && en726.find ("budget of 7.25\u00A0dB") != std::string::npos && ruEdge.find ("срезает 3,1\u00A0дБ") != std::string::npos,
        "a max master over its budget says a reduction above the budget: " + ru301 + " / " + en726 + " / " + ruEdge);
}

// A MAX MASTER, ITS JOB PLANTED OR ITS SOURCE ODD: each step, the report and its verdict, until the job ends.
struct MaxRun
{
    std::optional<MasterReport> report;
    std::optional<text::Fact> verdict;
    bool damageJob = false;
};
template <typename Plant>
MaxRun runMax (Session& s, LoudnessMode mode, Plant&& plant)
{
    MaxRun run;
    command::EditTarget edit { 2, {} };
    edit.fields.loudnessMode = mode;
    if (s.apply (edit).rejection != Rejection::None || s.apply (readyMaster (s, 3)).rejection != Rejection::None) return run;
    for (unsigned i = 0; i < 400000 && s.job() != 0; ++i)
    {
        if (auto* job = detail::Inspector::job (s)) plant (*job);
        (void) s.step (1);
        run.damageJob = run.damageJob || s.damageJob() != 0;
    }
    if (s.masters().empty()) return run;
    run.report = s.masters().back().report;
    if (run.report) run.verdict = MasterReportText::max (*run.report, mode == LoudnessMode::MaxClean ? 3.0 : 7.0, 4.25);
    return run;
}

// THE DAMAGE THE GUARD COULD NOT GRADE is said unchecked, never passed (MaxStop::Unguarded, fact 614): a source whose
// rate the damage's resampler has no kernel for (8001 Hz: no damage plan, no walk), and a walk that fails midway (its
// grade Unavailable). Nothing heard — a render graded with no window audible — stays a pass; the budget still holds.
void maxMasterUngraded()
{
    {
        const auto pcm = struck (8001, 3.0);
        auto sp = measuredSession (pcm, 8001);
        const auto run = sp ? runMax (*sp, LoudnessMode::MaxClean, [] (detail::MasterJob&) {}) : MaxRun {};
        ok (run.report && run.report->maxStop == MaxStop::Unguarded && run.report->guardSteps == 0u
                && run.report->damage.status == MeasurementStatus::Unavailable && ! run.report->damage.worstOdg
                && ! run.damageJob && run.verdict && run.verdict->id == text::FactId::MasterMaxUnguarded
                && text::Text::text (*run.verdict, text::Lang::En).find ("could not be checked") != std::string::npos,
            "at 8001 Hz, with no damage plan, a max master says its damage unchecked: stop "
                + std::to_string (run.report ? unsigned (run.report->maxStop) : 99u));
    }
    {
        const auto pcm = struck (48000, 3.0);
        auto sp = measuredSession (pcm, 48000);
        bool failed = false;
        const auto run = sp ? runMax (*sp, LoudnessMode::MaxClean, [&failed] (detail::MasterJob& job)
        {
            if (! failed && job.stage == detail::MasterJob::Stage::Guard && job.guard && job.guard->begun && ! job.guard->refused)
            { detail::Inspector::fail (job.guard->damage, MeasurementReason::NonFinite); failed = true; }
        }) : MaxRun {};
        ok (failed && run.report && run.report->maxStop == MaxStop::Unguarded && run.report->guardSteps == 0u
                && run.report->damage.status == MeasurementStatus::Unavailable
                && run.report->damage.reason == MeasurementReason::NonFinite && ! run.damageJob
                && run.verdict && run.verdict->id == text::FactId::MasterMaxUnguarded,
            "a guard's walk failed midway is no pass: the damage unchecked, stop "
                + std::to_string (run.report ? unsigned (run.report->maxStop) : 99u));
    }
}

// THE GUARD'S STEP RENDER PROVES THE MODE'S BUDGET: planted so every step goes LOUDER (a floor no render passes, a step
// of −0.5 dB), the guard steps three times and the render delivered takes more than the 3 dB the mode allows — it is
// delivered as over the budget (MaxStop::OverBudget, fact 615, the pass marked), never as the guard's verdict.
void maxStepRenderKeepsTheBudget()
{
    const auto pcm = struck (48000, 3.0);
    auto sp = measuredSession (pcm, 48000);
    const auto run = sp ? runMax (*sp, LoudnessMode::MaxClean, [] (detail::MasterJob& job)
    {
        job.guardFloorOdg = 1.0;
        job.guardStepDb = -0.5;
    }) : MaxRun {};
    const auto kept = sp && ! sp->masters().empty() ? sp->masters().back().landing : std::optional<LandingSummary> {};
    const bool marked = kept && ! kept->log.empty() && kept->log.back().overBudget;
    ok (run.report && run.report->maxStop == MaxStop::OverBudget && run.report->guardSteps == 3u && marked
            && run.verdict && run.verdict->id == text::FactId::MasterMaxOverBudget,
        "a step back that breaks the budget is delivered over the budget: stop "
            + std::to_string (run.report ? unsigned (run.report->maxStop) : 99u) + ", steps "
            + std::to_string (run.report ? run.report->guardSteps : 0u) + ", pass marked " + (marked ? "yes" : "no"));
}

// WHAT ENDED A MAX MODE, rule by rule (detail::maxStopOf): a landing that delivered its gentlest render over the budget
// carries the same status and binding as one the budget held (TargetUnreachable, LimiterGainReduction) — it is told
// apart by the delivered render's own excess, and said over the budget, never held by it.
void maxStopRules()
{
    using felitronics::mastering::MasteringSolveStatus; using felitronics::mastering::MasteringConstraint;
    detail::MaxStopInputs held;
    held.guardGraded = true; held.guardPassed = true;
    held.firstStatus = MasteringSolveStatus::TargetUnreachable; held.firstBinding = MasteringConstraint::LimiterGainReduction;
    auto over = held; over.overBudget = true;
    auto ungraded = held; ungraded.guardGraded = false; ungraded.guardPassed = false;
    auto unmet = held; unmet.guardPassed = false; unmet.guardTaken = 3;
    auto stepped = held; stepped.guardTaken = 1;
    auto above = over; above.peaksAboveCeiling = true;
    auto solved = held; solved.firstStatus = MasteringSolveStatus::Solved;
    auto passes = held; passes.firstStatus = MasteringSolveStatus::PassLimit;
    ok (detail::maxStopOf (held) == MaxStop::Budget && detail::maxStopOf (over) == MaxStop::OverBudget
            && detail::maxStopOf (ungraded) == MaxStop::Unguarded && detail::maxStopOf (unmet) == MaxStop::GuardUnmet
            && detail::maxStopOf (stepped) == MaxStop::Guard && detail::maxStopOf (above) == MaxStop::TruePeak
            && detail::maxStopOf (solved) == MaxStop::SearchCeiling && detail::maxStopOf (passes) == MaxStop::Passes,
        "a max mode's stop: over the budget is not held by it; ungraded is not a pass; the ceiling first");
}

int main()
{
    damageResampler();
    maxMasterGuarded();
    maxMasterUngraded();
    maxStepRenderKeepsTheBudget();
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
    char budgetSaid[64];
    std::snprintf (budgetSaid, sizeof budgetSaid, "%g dB", loudBudget);
    ok (demanding.rejection == Rejection::None && missStart.rejection == Rejection::None
        && miss.landing && miss.landing->status == LandingStatus::TargetUnreachable
        && miss.landing->binding == LandingConstraint::LimiterGainReduction
        && miss.landing->deliverable && ! miss.landing->peaksAboveCeiling && miss.landing->passes < 12
        && budgetProven (*miss.landing)
        && miss.report && miss.report->cost && miss.report->cost->limiterP95Db.value && *miss.report->cost->limiterP95Db.value <= loudBudget
        && miss.report && ! miss.report->peaksAboveCeiling && ! MasterReportText::peaksAboveCeiling (*miss.report)
        && miss.landing->truePeakDbTp && *miss.landing->truePeakDbTp <= -6.0
        && s.pendingMaster().master == miss.id && s.masterWavPlan (s.pendingMaster()),
        "an unreachable loudness goal stops at the loud target's limiter budget of " + std::string (budgetSaid) + ", named on its "
        "proof — a render marked over it within 0.25 dB above the one delivered — its delivered P95 inside it, and retains "
        "the best ceiling-safe PCM");
    {
        // −6 LUFS under −6 dBTP: the ceiling holds this landing, not the budget — whatever passes went over it.
        auto ceilingMade = Session::create();
        auto& c = *ceilingMade.session;
        (void) c.apply (command::Load { 1, { planes, 2, left.size(), rate }, { "test.wav", rate, true, 24 } });
        for (unsigned i = 0; i < 20000 && (c.state() == State::Loaded || ! c.snapshot().view().mandatoryMeasurementsReady); ++i)
            (void) c.step (16);
        const auto edited = c.apply (command::EditTarget { 2, { -6.0, -6.0 } });
        auto ceilingReady = ready; ceilingReady.id = 3; ceilingReady.source = c.source().hash; ceilingReady.revision = c.revision();
        const auto ceilingStart = c.apply (ceilingReady);
        for (unsigned i = 0; i < 40000 && c.job() != 0; ++i) (void) c.step (16);
        const auto masters = c.masters();
        const bool landed = ! masters.empty() && masters.back().landing;
        const LandingSummary l = landed ? *masters.back().landing : LandingSummary {};
        std::printf ("    −6 LUFS under −6 dBTP: status %d binding %d, %.3f LUFS, %u passes\n", landed ? (int) l.status : -1,
                     landed ? (int) l.binding : -1, landed && l.achievedLufs ? *l.achievedLufs : 0.0, landed ? l.passes : 0u);
        ok (edited.rejection == Rejection::None && ceilingStart.rejection == Rejection::None && landed && l.deliverable
            && l.status == LandingStatus::PassLimit && l.binding != LandingConstraint::LimiterGainReduction && ! budgetProven (l),
            "−6 LUFS under −6 dBTP is held by the ceiling: no render over the budget stands just above the one delivered, "
            "and the budget is not named");
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
