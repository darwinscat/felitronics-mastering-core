// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include "Advance.h"
#include "Driver.h"
#include "SourceMeasurements.h"
#include "QueryState.h"
#include "FpEnvironmentControl.h"
#include "JsonNumber.h"
#include "SnapshotStorage.h"
#include "TextNumber.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>
#include <felitronics/eq/EqBand.h>
#include <algorithm>
#include <bit>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
using detail::Driver;
using text::FactId;
// A usable loudness and true peak whose loudness need on the default target is at most 3 dB: no needles to measure.
constexpr MeasurementValue kEventReadyNumbers[] {
    { "integratedLufs", -18.0, MeasurementReason::None, 0 },
    { "truePeakDb", -3.0, MeasurementReason::None, 0 }
};
// Test-only corruption reaches the guards without depending on real driver failures.
struct felitronics::session::detail::Inspector
{
    static void state (Session& s, State state) { s.state_ = state; }
    static void measurementUnit (Session& s, std::uint32_t unit) { s.sourceMeasurements_->cursor = unit; s.sourceMeasurements_->stage = 99; s.measurementUnit_ = 2; }
    static void skipWaveform (Session& s) { s.waveform_->finished = true; }
    static void noMasterRoom (Session& s) { s.masterRoom_ = s.masterCount_; }
    static void unplace (Session& s) { s.devicesPlaced_ = false; }
    static void mandatory (Session& s)
    {
        auto& result = s.measurementResults_[std::size_t (Analyzer::Loudness)];
        result.status = MeasurementStatus::Ready;
        result.reason = MeasurementReason::None;
        result.numbers = kEventReadyNumbers;
        // What a finished loudness does (Driver::retain): the needles for the project's ceiling, and the plan again.
        s.requestNeedles();
        s.replan();
    }
    static std::uint64_t sequence (const Session& s) { return s.sequence_; }
    static void fillBatch (Session& s, std::size_t count) { for (std::size_t i = 0; i < count; ++i) s.emit ({}); }
};
namespace budget = felitronics::session::testing;
namespace declared = felitronics::declared;
namespace
{
void measureReady (Session& session, bool firstOnly = false)
{
    budget::measure (session, firstOnly);
    detail::Inspector::mandatory (session);
}
struct Audio
{
    float samples[48000] {};
    Audio() { for (unsigned i = 0; i < 48000; ++i) samples[i] = (i % 48 < 24 ? .25f : -.25f); }
    const float* planes[1] { samples };
    command::Load load() const { return { 1, { planes, 1, 48000, 48000 }, { "source.wav", 48000, true, 24 } }; }
};
std::unique_ptr<Session> fresh()
{
    Created made;
    const auto need = Session::createBytes();
    const auto spent = declared::spend ([&] { made = Session::create(); });
    ok (made.status == Status::Ok && declared::covers (need, spent) && need == std::uint64_t (spent.bytes), "create declares its exact demand");
    return std::move (made.session);
}
Answer apply (Session& s, const Request& r)
{
    const auto need = s.check (r);
    Answer answer;
    const auto spent = declared::spend ([&] { answer = s.apply (r); });
    ok (declared::covers (need.bytes, spent)
        && (std::holds_alternative<command::ImportProject> (r) || std::holds_alternative<command::Load> (r)
            || std::holds_alternative<command::Master> (r) || need.bytes == std::uint64_t (spent.bytes)), "command demand covers events and import validation");
    return answer;
}
Stepped step (Session& s, std::uint32_t units)
{
    Stepped result;
    const auto demand = s.measurementStorage ({ nullptr, s.source().channels, s.source().frames, s.source().sampleRate });
    const auto spent = declared::spend ([&] { result = s.step (units); });
    ok (declared::covers (std::uint64_t (demand.workspaceBytes + demand.resultBytes + demand.allocatorBytes + s.liveBytes()), spent), "pump stays within the declared preparation and output demand");
    return result;
}
Snapshot snapshot (const Session& s)
{
    Snapshot out;
    const auto need = s.snapshotBytes();
    const auto spent = declared::spend ([&] { out = s.snapshot(); });
    ok (need == std::uint64_t (spent.bytes) && need == Snapshot::storageFor (out.view()), "snapshot and its storage demand use the same complete view");
    return out;
}
std::string encoded (const SnapshotView& view)
{
    CodecNeed need;
    const auto checked = declared::spend ([&] { need = Codec::encodedBytes (view); });
    ok (checked.bytes == 0 && need.status == CodecStatus::Ok, "encoding size query allocates nothing");
    std::string text (std::size_t (need.bytes), '\0');
    CodecStatus status {};
    const auto spent = declared::spend ([&] { status = Codec::encode (view, text); });
    ok (status == CodecStatus::Ok && spent.bytes == 0, "codec writes into exactly the declared caller storage");
    return text;
}
std::uint64_t digest (std::uint64_t hash, std::uint64_t n)
{
    for (unsigned i = 0; i < 8; ++i) hash = (hash ^ ((n >> (i * 8)) & 255)) * 0x100000001B3ull;
    return hash;
}
// THE MASTER'S MEASURED NUMBERS — the facts a master's completion publishes from its rendered PCM: the landing (11, 23,
// 88–92), the hints (12–17), the crest (18–22), the cost (24–28, 93–97), the stages (44, 45) and the medium (76–79,
// 82–87). That PCM comes from DSP outside the det-math zone (felitronics-core's limiter and saturator call libm), so
// its last bits are the platform's — tools/contract/run.mjs compares such numbers within a tolerance for the same
// reason. A cross-platform pin takes each Value argument of these facts as the catalogue prints it: the core's own
// digits (gridDigits) at the argument's declared precision. Every other argument, and every non-Value argument of
// these, is hashed bit for bit. (80 and 81 sit in the range but speak of the source and of edits, not of the master.)
constexpr FactId kMastersMeasurement[] {
    FactId::MasterLandingMiss, FactId::MasterHintSubBass, FactId::MasterHintPeaks, FactId::MasterHintDark,
    FactId::MasterHintDemand, FactId::MasterHintGainRange, FactId::MasterHintTruePeak, FactId::MasterCrestSourceRate,
    FactId::MasterCrestPending, FactId::MasterCrestUnavailable, FactId::MasterReportUnavailable,
    FactId::MasterCrestDelivered, FactId::MasterLandingAbove, FactId::MasterCostShape, FactId::MasterCostCrest,
    FactId::MasterCostPumping, FactId::MasterCostK2Deferred, FactId::MasterCostUnavailable, FactId::MasterGlue,
    FactId::MasterSaturation, FactId::MasterVinylReady, FactId::MasterVinylDeparts, FactId::MasterVinylChecked,
    FactId::MasterVinylUncheckable, FactId::MasterVinylNoFold, FactId::MasterVinylFoldDeparts,
    FactId::MasterVinylNoHighPass, FactId::MasterVinylHighPassDeparts, FactId::MasterVinylCeilingDeparts,
    FactId::MasterVinylNeedlesDeparts, FactId::MasterLandingSolved, FactId::MasterLandingUnreachable,
    FactId::MasterLandingPassLimit, FactId::MasterLandingBetween, FactId::MasterLandingFailed, FactId::MasterCostSection,
    FactId::MasterCostSections, FactId::MasterCostLimiter, FactId::MasterCostActive, FactId::MasterCostBands };
bool fromMastersMeasurement (FactId id) noexcept
{ return std::find (std::begin (kMastersMeasurement), std::end (kMastersMeasurement), id) != std::end (kMastersMeasurement); }
std::uint64_t printed (std::uint64_t hash, const text::Arg& arg)
{
    text::detail::Digits digits;
    if (! text::detail::gridDigits (arg.number, arg.precision, digits)) return digest (hash, std::bit_cast<std::uint64_t> (arg.number));
    hash = digest (hash, digits.negative ? 1u : 0u);
    for (const char c : digits.integer()) hash = digest (hash, static_cast<unsigned char> (c));
    hash = digest (hash, std::uint64_t ('.'));
    for (const char c : digits.fraction()) hash = digest (hash, static_cast<unsigned char> (c));
    return hash;
}
std::uint64_t eventsHash (const std::vector<Notification>& events)
{
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& e : events)
    {
        hash = digest (hash, e.seq); hash = digest (hash, e.jobId); hash = digest (hash, std::uint8_t (e.kind));
        if (e.kind == EventKind::Phase)
        {
            hash = digest (hash, std::uint8_t (e.payload.phase.name)); hash = digest (hash, std::bit_cast<std::uint64_t> (e.payload.phase.fraction));
            hash = digest (hash, e.payload.phase.weightsVersion); hash = digest (hash, e.payload.phase.pass); hash = digest (hash, e.payload.phase.totalPasses);
            hash = digest (hash, e.payload.phase.completedUnits); hash = digest (hash, e.payload.phase.totalUnits);
        }
        if (e.kind == EventKind::Fact)
        {
            const auto fact = e.payload.fact.view();
            const bool master = fromMastersMeasurement (fact.id);
            hash = digest (hash, std::uint16_t (fact.id)); hash = digest (hash, fact.argCount);
            for (unsigned i = 0; i < fact.argCount; ++i)
            {
                const auto& arg = fact.args[i];
                hash = digest (hash, std::uint8_t (arg.kind)); hash = digest (hash, std::uint8_t (arg.unit));
                hash = digest (hash, arg.precision); hash = digest (hash, std::uint8_t (arg.sign));
                hash = digest (hash, std::uint8_t (arg.bound)); hash = digest (hash, std::uint16_t (arg.termId));
                hash = master && arg.kind == text::ArgKind::Value ? printed (hash, arg) : digest (hash, std::bit_cast<std::uint64_t> (arg.number));
                hash = digest (hash, std::uint64_t (arg.integer)); hash = digest (hash, arg.userText.size());
                for (const char c : arg.userText) hash = digest (hash, static_cast<unsigned char> (c));
            }
        }
        if (e.kind == EventKind::Done) hash = digest (hash, e.payload.done.masterId);
        if (e.kind == EventKind::Damage)
        {
            hash = digest (hash, e.payload.damage.masterId); hash = digest (hash, std::uint8_t (e.payload.damage.status));
            hash = digest (hash, std::uint8_t (e.payload.damage.reason));
        }
        if (e.kind == EventKind::Rejected) { hash = digest (hash, e.payload.rejected.commandId); hash = digest (hash, std::uint8_t (e.payload.rejected.code)); }
    }
    return hash;
}
// The events of every job but a master's, renumbered. A master's job is known by what it publishes: every phase of it —
// the wait for the measurement included — counts the landing's passes (totalPasses), which no measurement's or
// needles' phase does, and a finished master names itself. A master's damage, graded after it by a job of its own, is
// the master's work too: that job is known by its damage event.
std::vector<Notification> withoutMaster (const std::vector<Notification>& events)
{
    std::vector<std::uint64_t> masters;
    for (const auto& e : events)
    {
        const bool landing = e.kind == EventKind::Phase && e.payload.phase.totalPasses != 0;
        const bool finished = e.kind == EventKind::Done && e.payload.done.masterId != 0;
        const bool damage = e.kind == EventKind::Damage;
        if ((landing || finished || damage) && std::find (masters.begin(), masters.end(), e.jobId) == masters.end())
            masters.push_back (e.jobId);
    }
    std::vector<Notification> kept;
    std::uint64_t dropped = 0;
    for (auto e : events)
    {
        if (std::find (masters.begin(), masters.end(), e.jobId) != masters.end()) { ++dropped; continue; }
        e.seq -= dropped;
        kept.push_back (e);
    }
    return kept;
}

std::vector<Notification> scenario (std::uint32_t chunk, bool cancel)
{
    Audio audio; auto s = fresh(); std::vector<Notification> events;
    const auto collect = [&] { for (const auto& e : s->events()) events.push_back (e); };
    ok (apply (*s, audio.load()).job == 1, "load issues the measurement identity");
    ok (s->source().hash != 0, "event fixture PCM hash is pinned with its config version");
    ok (snapshot (*s).view().measurementProgress.totalUnits == 10, "measurement work estimate is available before the first step");
    ok (step (*s, 0).state == StepState::More && s->events().empty(), "zero units only polls");
    while (s->state() == State::Loaded) { (void) step (*s, 1); collect(); }
    ok (s->state() == State::Measured1 && events.back().payload.fact.view().id == FactId::Measurement1, "phase one fact is available on the step that establishes it");
    while (! snapshot (*s).view().mandatoryMeasurementsReady && s->measurementJob() != 0)
    { (void) step (*s, 1); collect(); }
    if (! snapshot (*s).view().mandatoryMeasurementsReady) detail::Inspector::mandatory (*s);
    auto saved = snapshot (*s);
    const auto job = apply (*s, command::Master { 2 }).job;
    ok (snapshot (*s).view().masterProgress.totalUnits == 12 && snapshot (*s).view().masterProgress.totalPasses == 12,
        "the master's progress bound is available before the first step");
    if (cancel)
    {
        (void) step (*s, 1); collect();
        ok (apply (*s, command::Cancel { 3, job }).rejection == Rejection::None, "cancel accepts the running job"); collect();
        ok (apply (*s, command::Master { 4 }).job == 3, "a cancelled session starts another master");
        const auto before = encoded (snapshot (*s).view());
        ok (! Driver::mastered (*s, job), "late completion A cannot finish master B");
        ok (before == encoded (snapshot (*s).view()), "stale completion changed no snapshot field");
    }
    while (step (*s, chunk).state == StepState::More) collect();
    collect();
    ok (s->state() == State::Measured2 && s->masters().size() == 1 && ! s->mastering(), "both measurements and one master finish");
    ok (saved.view().state == State::Measured1 && saved.view().masters.empty() && saved.view().source.name == "source.wav", "retained snapshot does not change as its session advances");
    (void) apply (*s, command::Cancel { 99, job }); collect();
    ok (events.back().kind == EventKind::Rejected && events.back().payload.rejected.commandId == 99, "rejection delta names its command and code");
    for (std::size_t i = 0; i < events.size(); ++i) ok (events[i].seq == i + 1, "sequence counts inputs, without a clock");
    return events;
}
void pump()
{
    const auto one = scenario (1, false), bulk = scenario (16, false), again = scenario (1, false);
    const auto cancelled = scenario (1, true), cancelledAgain = scenario (16, true);
    ok (eventsHash (one) == eventsHash (bulk) && eventsHash (one) == eventsHash (again), "complete event sequence is invariant across runs and pump slicing");
    ok (eventsHash (cancelled) == eventsHash (cancelledAgain), "cancelled scenario sequence is invariant across runs and slicing");
    ok (one.size() > 22 && cancelled.size() > one.size(), "measurement publishes live work and cancellation adds events");
    // THE PINS. The phases carry the config's version (weightsVersion), so a new config moves the whole-stream pins below,
    // and a master the session decides is rendered in the scenario, so its passes, cost and facts move with its sound. Of
    // the master's facts, those of its measurement (kMastersMeasurement) are hashed as printed, every other argument bit
    // for bit. Two pins hold everything else apart from both: with the config's version put back to the one
    // previousVersion restates, and with the landing's two keys taken out, every job's events but a master's are pinned.
    // THE MAX MODES (v0.15.0) came after everything below: the two max targets' rows and the engine's [landing.max]
    // table. Each restatement takes them out first; a manual master (the scenario's) never reads them.
    const auto withoutMax = [] (std::string& targets, std::string& engine)
    {
        // v0.18.0: target classes and already-mastered delivery ceilings. Remove them when restating the older pins.
        for (const std::string_view classification : { ", class = \"specification\"", ", class = \"streaming\"", ", class = \"other\"" })
            for (auto at = targets.find (classification); at != std::string::npos; at = targets.find (classification))
                targets.erase (at, classification.size());
        if (const auto at = engine.find ("\n[masteredDelivery]\n"); at != std::string::npos)
        {
            const auto next = engine.find ("\n[", at + 1);
            engine.erase (at, (next == std::string::npos ? engine.size() : next) - at);
        }
        for (const std::string_view key : { "\nlimiterSlopeBelow = ", "\nlimiterSlopeSpacingDb = ", "\nalreadyMastered = " })
            if (const auto at = engine.find (key); at != std::string::npos)
                engine.erase (at, engine.find ('\n', at + 1) - at);
        // The unread compressor mix was removed (owner, 05.10); restore it for the older config fingerprints alone.
        if (const auto at = engine.find ("\n[compressor]\n"); at != std::string::npos)
            engine.insert (at + 14, "mix = 1\n");
        // ...and the glue's mix (v0.17.0): [glue] mixDomain, mix, mixRange, mixStep — the saturation's keys of the same
        // names stay.
        if (const auto glue = engine.find ("\n[glue]\n"); glue != std::string::npos)
            for (auto at = engine.find ("\nmix", glue); at != std::string::npos && at < engine.find ("\n[", glue + 1);
                 at = engine.find ("\nmix", glue))
                engine.erase (at, engine.find ('\n', at + 1) - at);
        for (const std::string_view key : { "\nmaxClean ", "\nmaxDense " })
            if (const auto at = targets.find (key); at != std::string::npos)
                targets.erase (at, targets.find ('\n', at + 1) - at);
        for (auto at = engine.find ("\n[landing.max"); at != std::string::npos; at = engine.find ("\n[landing.max"))
        {
            const auto next = engine.find ("\n[", at + 1);
            engine.erase (at, (next == std::string::npos ? engine.size() : next) - at);
        }
    };
    const auto previousVersion = [&withoutMax] (std::vector<Notification> events)
    {
        // The canonical document without its [notes] table: the config before them.
        auto targets = config::Config::text (config::Document::Targets);
        const auto from = targets.rfind ("\n[notes]\n");
        const auto to = from == std::string::npos ? from : targets.find ("\n[", from + 1);
        if (from != std::string::npos) targets.erase (from, (to == std::string::npos ? targets.size() : to) - from);
        // ...and without the three broadcast targets (atsc, arib, op59, v0.13.0), which came after.
        for (const std::string_view key : { "\natsc ", "\narib ", "\nop59 " })
            if (const auto at = targets.find (key); at != std::string::npos)
                targets.erase (at, targets.find ('\n', at + 1) - at);
        // ...and the engine with the observation rows before the owner's table.
        auto engine = config::Config::text (config::Document::Engine);
        const std::pair<std::string_view, std::string_view> rows[] = {
            { "from = 0.001, fullAt = 0.01, warningFrom = 0.01, errorFrom = 0.1 }", "from = 0.001, fullAt = 0.01 }" },
            { "depthBits = 24, fromBitsShort = 1, fullAtBitsShort = 8, errorFromBitsShort = 8 }", "fromBits = 1, fullAtBits = 8 }" },
            { "fromPowerDb = -60, fullAtPowerDb = -40, warningFromSeverity = 0.5 }", "fullAtPowerAgainstProgramme = 0.0001 }" },
            { "fullAtDropDb = 40, fromFractionBelowNyquist = 0.12,", "fullAtDropDb = 60, fromFractionBelowNyquist = 0.2," },
            { "high = 0.05, warningFrom = 0.05 }", "high = 0.05 }" },
            { "sideFractionAtLeast = 0.06, fullAt = 0.3 }", "sideFractionAtLeast = 0.06 }" },
            { "rawSideFractionAbove = 0.5, fullAtLowCorrelation = -0.5 }", "rawSideFractionAbove = 0.5 }" },
            { "plrBelowDb = 10.5, fullAtPlrDb = 7 }", "plrBelowDb = 8 }" },
            { "dcOffset = \"note\"", "dcOffset = \"warning\"" }, { "dualMono = \"warning\"", "dualMono = \"note\"" },
            { "spectralWall = \"warning\"", "spectralWall = \"note\"" }, { "lowestLowBand = \"reading\"", "lowestLowBand = \"note\"" },
            { "polarity = \"error\"", "polarity = \"warning\"" }, { "alreadyLimited = \"warning\"", "alreadyLimited = \"note\"" },
            { "shape = \"tape\"", "shape = \"tanh\"" },
            { "mixDomain = [0, 1]\nshape", "mixDomain = [0, 1]\noutputDomain = [-6, 0]\nshape" },
            { "mixStep = 0.05\nautoComp", "mixStep = 0.05\noutputDb = 0\noutputRange = [-6, 0]\noutputStep = 0.1\nautoComp" },
            { "{ key = \"bass4\", hz = 41.2 }", "{ key = \"bass4\", hz = 41 }" },
            { "{ key = \"bass5\", hz = 30.87 }", "{ key = \"bass5\", hz = 31 }" },
            { "{ key = \"sub808\", hz = 28 }", "{ key = \"sub808\", hz = 23 }" },
            { "hzMax = 80\nmachineTopHz = 50\n", "hzMax = 50\n" },
            { "frequencyStep = 5\n", "frequencyStep = 1\n" }, { "manualStepDb = 0.1\n", "manualStepDb = 0.5\n" },
            { "manualMaxDb = 6\n", "manualMaxDb = 3\n" } };
        for (const auto& [now, then] : rows)
            if (const auto at = engine.find (now); at != std::string::npos) engine.replace (at, now.size(), then);
        // ...and without the landing's level on the source's gate and the limiter's budget, and the damage's table
        // ([cost.damage]), which came after (v0.14.0).
        for (const std::string_view key : { "\nonSourceGate = ", "\nlimiterBudget = " })
            if (const auto at = engine.find (key); at != std::string::npos)
                engine.erase (at, engine.find ('\n', at + 1) - at);
        if (const auto at = engine.find ("\n[cost.damage]\n"); at != std::string::npos)
            engine.erase (at, engine.find ("\n[", at + 1) - at);
        // ...and without the EQ bands' tables, which came after.
        for (auto at = engine.find ("\n[bands"); at != std::string::npos; at = engine.find ("\n[bands"))
        {
            const auto next = engine.find ("\n[", at + 1);
            engine.erase (at, (next == std::string::npos ? engine.size() : next) - at);
        }
        withoutMax (targets, engine);
        const auto before = config::Config::versionsOf (targets, engine);
        for (auto& e : events)
            if (e.kind == EventKind::Phase && e.payload.phase.weightsVersion == config::Config::versions().all && before)
                e.payload.phase.weightsVersion = before->all;
        return events;
    };
    {
        // A master waiting for the measurement publishes the analyzers' phase, counting its passes: a master's job.
        std::vector<Notification> waiting (2);
        waiting[0].seq = 1; waiting[0].jobId = 7; waiting[0].kind = EventKind::Phase;
        waiting[0].payload.phase.name = PhaseName::Analyzers; waiting[0].payload.phase.totalPasses = 12;
        waiting[1].seq = 2; waiting[1].jobId = 1; waiting[1].kind = EventKind::Phase;
        waiting[1].payload.phase.name = PhaseName::Analyzers;
        const auto kept = withoutMaster (waiting);
        ok (kept.size() == 1 && kept[0].jobId == 1 && kept[0].seq == 1,
            "a master waiting for the measurement is known as a master's job; the measurement's own phase is kept");
    }
    // previousVersion takes out what moved the config's version alone — the targets' notes and the three broadcast
    // targets, the engine rows it lists, the EQ bands' tables, the landing's two keys, the damage's table — and with that
    // version put back every job's events but a master's (its damage's job among them) are the ones that config gives.
    ok (eventsHash (withoutMaster (previousVersion (one))) == 0xacc7bec22e8a400full
        && eventsHash (withoutMaster (previousVersion (cancelled))) == 0xacc7bec22e8a400full,
        "with the config's version previousVersion restates, every job's events but a master's hold their pin");
    // The landing's two keys ([landing] onSourceGate, limiterBudget) and the damage's table ([cost.damage]) move the
    // master and nothing else: with the config's version they leave put back, every other job's events are the ones the
    // config without them gives (81133bf's).
    const auto withoutKeys = [&withoutMax] (std::vector<Notification> events, bool landing, bool damage, bool loudBudget = false)
    {
        auto engine = config::Config::text (config::Document::Engine);
        auto targets = config::Config::text (config::Document::Targets);
        withoutMax (targets, engine);
        // ...and the loud target's budget at the 10 dB it was before 7.5 (a number only, every master here quieter).
        if (loudBudget)
            if (const auto at = engine.find ("loudDb = 7.5,"); at != std::string::npos) engine.replace (at, 13, "loudDb = 10,");
        if (landing)
            for (const std::string_view key : { "\nonSourceGate = ", "\nlimiterBudget = " })
                if (const auto at = engine.find (key); at != std::string::npos)
                    engine.erase (at, engine.find ('\n', at + 1) - at);
        if (damage)
            if (const auto at = engine.find ("\n[cost.damage]\n"); at != std::string::npos)
                engine.erase (at, engine.find ("\n[", at + 1) - at);
        const auto before = config::Config::versionsOf (targets, engine);
        for (auto& e : events)
            if (e.kind == EventKind::Phase && e.payload.phase.weightsVersion == config::Config::versions().all && before)
                e.payload.phase.weightsVersion = before->all;
        return std::pair { events, before ? before->all : std::uint64_t (0) };
    };
    const auto [oneBefore, versionBefore] = withoutKeys (one, true, true);
    ok (versionBefore == 0x4d1ce39f8531727aull && eventsHash (withoutMaster (oneBefore)) == 0x229c3d9dc4eb6775ull
        && eventsHash (withoutMaster (withoutKeys (cancelled, true, true).first)) == 0x229c3d9dc4eb6775ull,
        "the landing's two keys and the damage's table move the master's events alone: without them the config's version "
        "is 4d1ce39f8531727a, and every other job's events hold");
    // THE DAMAGE (v0.14.0) is a job of its own after the master: its events carry its own id — its phases, its line and
    // the damage events — and the master says two lines more (603-607). Without them, and with the config's version its
    // table leaves put back, every event is the landing fix's alone (d406ca4's pins), bit for bit: the master's job ends
    // where it did.
    const auto withoutDamage = [] (const std::vector<Notification>& events)
    {
        std::vector<JobId> jobs;
        for (const auto& e : events)
            if (e.kind == EventKind::Damage) jobs.push_back (e.jobId);
        std::vector<Notification> kept;
        std::uint64_t dropped = 0;
        for (auto e : events)
        {
            const auto id = e.kind == EventKind::Fact ? unsigned (e.payload.fact.view().id) : 0u;
            if (std::find (jobs.begin(), jobs.end(), e.jobId) != jobs.end() || (id >= 603u && id <= 607u)) { ++dropped; continue; }
            e.seq -= dropped;
            kept.push_back (e);
        }
        return kept;
    };
    const auto oneWithoutDamage = eventsHash (withoutDamage (withoutKeys (one, false, true, true).first));
    const auto cancelledWithoutDamage = eventsHash (withoutDamage (withoutKeys (cancelled, false, true, true).first));
    std::printf ("event fingerprints, without damage: %016llx %016llx\n",
        (unsigned long long) oneWithoutDamage, (unsigned long long) cancelledWithoutDamage);
    ok (oneWithoutDamage == 0x74303a7fc546ffb2ull && cancelledWithoutDamage == 0xccb520e2bcb0df7cull,
        "the damage moves its own job's events, the master's two lines and the config's version alone: without them the "
        "retained-winner and pass-log pins 74303a7fc546ffb2 / ccb520e2bcb0df7c hold");
    char hashes[48];
    std::snprintf (hashes, sizeof hashes, "%016llx / %016llx", (unsigned long long) eventsHash (one), (unsigned long long) eventsHash (cancelled));
    // The max modes (v0.15.0) moved the config's version alone here: with it restated (withoutMax) every pin above holds.
    // The damage's grade is the shell's to ask (command::GradeDamage, v0.16.0): the scenario asks none, so its stream
    // carries no damage job — the pin without the damage above holds, and this one moves by that job alone; the max modes
    // by ear (v0.16.0) move the config's version alone (withoutMax restates it).
    // The glue's mix (v0.17.0) and v0.18's target classes / mastered-delivery ceilings move the config's version alone
    // here (withoutMax restates them too).
    ok (eventsHash (one) == 0x4c0913c1f9d5e8beull && eventsHash (cancelled) == 0xaa97a89f9b57aeeaull,
        "event fixtures pin every active payload field: " + std::string (hashes));
    std::printf ("event fingerprints: %016llx %016llx\n", (unsigned long long) eventsHash (one), (unsigned long long) eventsHash (cancelled));
    std::printf ("event fingerprints, every job but the master's, previous version: %016llx %016llx\n",
        (unsigned long long) eventsHash (withoutMaster (previousVersion (one))),
        (unsigned long long) eventsHash (withoutMaster (previousVersion (cancelled))));
    // The rule's controls, on the landing's achieved level (one digit): moved by its printed step it moves the pin; moved
    // below its precision it does not; the same argument on a fact outside the list is still hashed bit for bit.
    const auto solved = std::find_if (one.begin(), one.end(), [] (const Notification& e)
        { return e.kind == EventKind::Fact && e.payload.fact.view().id == FactId::MasterLandingSolved; });
    ok (solved != one.end() && fromMastersMeasurement (FactId::MasterLandingSolved) && ! fromMastersMeasurement (FactId::WideBass),
        "the scenario's master publishes its landing, a fact of the master's measurement; the wide-bass warning is not one");
    if (solved != one.end())
    {
        const auto index = std::size_t (solved - one.begin());
        const auto changed = [&] (FactId id, double delta)
        {
            auto events = one;
            auto fact = events[index].payload.fact.view();
            fact.id = id;
            fact.args[0].number += delta;
            ok (events[index].payload.fact.assign (fact), "the control's fact is stored");
            return events;
        };
        ok (eventsHash (changed (FactId::MasterLandingSolved, 0.1)) != eventsHash (one),
            "a master's number moved by its printed step (0.1 LUFS at one digit) moves the pin");
        ok (eventsHash (changed (FactId::MasterLandingSolved, 1e-9)) == eventsHash (one),
            "a master's number moved below its printed precision does not: its last bits are the platform's");
        ok (eventsHash (changed (FactId::WideBass, 1e-9)) != eventsHash (changed (FactId::WideBass, 0.0)),
            "the same argument on a fact outside the master's measurement is hashed bit for bit");
    }
    Audio audio; auto s = fresh();
    const auto old = apply (*s, audio.load()).job;
    const auto source = s->source().hash;
    (void) step (*s, 2);
    (void) apply (*s, command::Cancel { 2, old });
    ok (step (*s, 16).state == StepState::Done && s->state() == State::MeasurementStopped && s->source().channels == 1 && s->source().hash == source, "phase-one cancel retains the source");
    ok (! Driver::measured1 (*s, old, source), "cancelled measurement completion is ignored");
    const auto next = apply (*s, audio.load()).job;
    ok (next != old && s->source().hash == source, "even reloading identical samples gets a new job identity");
    ok (! Driver::measured1 (*s, old, source) && ! Driver::measured1 (*s, next, source + 1), "both measurement job and source are checked");
    measureReady (*s, true);
    ok (! Driver::measured2 (*s, old, source), "stale phase two is ignored");
    (void) apply (*s, command::Cancel { 3, next });
    ok (apply (*s, command::Master { 4 }).rejection == Rejection::None, "master remains usable after phase two cancellation");
    for (unsigned i = 0; i < 10000 && s->job() != 0; ++i) (void) step (*s, 16);
    ok (s->masters().size() == 1, "cancel does not destroy the session");
}
void tableBetweenSteps()
{
    Audio audio;
    for (std::size_t col = 0; col < kColumns; ++col)
        for (const auto& row : Table::commands)
        {
            constexpr unsigned placedColumns[] { 2, 3, 4, 5, 7, 8 };
            const auto placed = col < 9 ? col : placedColumns[col - 9];
            auto s = fresh();
            MasterId kept = 0;
            if (placed != 0) (void) apply (*s, audio.load());
            if (placed >= 2 && placed != 6)
            {
                measureReady (*s, placed != 3 && placed != 5);
                kept = apply (*s, command::Master { 10 }).job;
                (void) Driver::mastered (*s, kept);
                if (placed == 4 || placed == 5 || placed == 8) { (void) apply (*s, command::Master { 11 }); (void) step (*s, 1); }
            }
            if (placed >= 6) (void) apply (*s, command::Cancel { 22, s->measurementJob() });
            if (col >= 9) detail::Inspector::unplace (*s);
            (void) apply (*s, command::SetManual { 12, true });
            (void) snapshot (*s);
            const auto before = encoded (snapshot (*s).view());
            const JobId job = s->job() ? s->job() : s->measurementJob();
            HpfFields<Touched> hpf; hpf.fq = 36.0;
            HpfFields<Mark> mask; mask.fq = true;
            const auto project = s->exportProject();
            const Request requests[] = { audio.load(), command::SetTarget { 13, "allStreaming" },
                command::EditTarget { 14, { -13.0, {} } }, command::EditDevice { 15, hpf }, command::RevertEdits { 16, mask },
                command::SetManual { 17, false }, command::Master { 18 }, command::Cancel { 19, job }, command::Forget { 20, kept }, command::ImportProject { 21, project.view() }, command::ContinueMeasurement { 23 },
                command::AdoptMachine { 24 }, command::GradeDamage { 25, kept } };
            static_assert (std::size (requests) == kCommands, "a request of every command");
            ok (std::size_t (s->column()) == col, "pump establishes the table column");
            const auto answer = apply (*s, requests[std::size_t (row.command)]);
            const auto expected = row.command == Command::Cancel && job == 0 ? Rejection::NoJob : row.cell[col];
            // A master this fixture keeps was not delivered by a job: past the table, its damage is not gradable.
            const bool settled = row.command == Command::GradeDamage && expected == Rejection::None
                && answer.rejection == Rejection::DamageSettled;
            ok (answer.rejection == expected || settled, "every command obeys the table and active-job check between pump steps");
            if (answer.rejection != Rejection::None) ok (before == encoded (snapshot (*s).view()), "rejection leaves the entire snapshot intact");
        }
}
void codec()
{
    Audio audio; auto s = fresh();
    (void) apply (*s, audio.load()); measureReady (*s);
    (void) apply (*s, command::SetManual { 2, true });
    HpfFields<Touched> hpf; hpf.fq = 36; hpf.on = true; hpf.slope = 12;
    (void) apply (*s, command::EditDevice { 3, hpf });
    (void) apply (*s, command::Master { 4 }); (void) step (*s, 4);
    auto base = snapshot (*s); SnapshotView v = base.view();
    const std::string special ("silence\0\"\\\n\t", 13);
    v.source.name = special;
    v.revision = std::numeric_limits<std::uint64_t>::max();
    v.sourceBytes = 9007199254740991.0;
    v.integratedLufs = -std::numeric_limits<double>::infinity();
    const ReadingPoint momentary[] { { 0, -std::numeric_limits<double>::infinity() }, { 1, std::numeric_limits<double>::quiet_NaN() }, { 2, -0.0 } };
    const ReadingPoint shortTerm[] { { 3, std::numeric_limits<double>::max() }, { 4, std::numeric_limits<double>::denorm_min() } };
    const ReadingRun runs[] { { 8, 2, std::numeric_limits<double>::infinity() } };
    v.momentary = momentary; v.shortTerm = shortTerm; v.runs = runs;
    Snapshot owned;
    const auto copy = declared::spend ([&] { owned = Snapshot::copy (v); });
    ok (Snapshot::storageFor (v) == std::uint64_t (copy.bytes), "copy declares exact strings, masters and reading arrays");
    const auto text = encoded (owned.view());
    CodecNeed need;
    const auto checked = declared::spend ([&] { need = Codec::decodedBytes (text); });
    ok (need.status == CodecStatus::Ok && checked.bytes == 0 && need.bytes == Snapshot::storageFor (v), "decode validates and declares exact owned storage before work");
    Snapshot restored; CodecStatus status {};
    const auto decoded = declared::spend ([&] { status = Codec::decode (text, restored); });
    ok (status == CodecStatus::Ok && need.bytes == std::uint64_t (decoded.bytes), "decode asks exactly its declared demand");
    ok (encoded (restored.view()) == text, "named fields and both layers round trip, including rows and non-finite values");
    const auto& reasons = restored.view().plan.facts;
    bool sameReasons = v.plan.facts.count != 0 && reasons.count == v.plan.facts.count;
    for (unsigned i = 0; sameReasons && i < reasons.count; ++i)
        sameReasons = reasons.items[i].device == v.plan.facts.items[i].device && reasons.items[i].fact.id == v.plan.facts.items[i].fact.id
            && reasons.items[i].fact.argCount == v.plan.facts.items[i].fact.argCount;
    ok (sameReasons, "the plan's reasons round trip: " + std::to_string (reasons.count) + " facts");
    for (const auto& [from, to] : { std::pair<std::string, std::string> { "\"userText\":\"\"", "\"userText\":\"x\"" },
                                    { "\"FactId\":", "\"FactId\":9" }, { "\"precision\":", "\"precision\":1" } })
    {
        auto input = text;
        const auto at = input.find (from);
        if (at != std::string::npos) input.replace (at, from.size(), to);
        const auto before = encoded (restored.view());
        ok (at != std::string::npos && Codec::decode (input, restored) == CodecStatus::Invalid && before == encoded (restored.view()),
            "a plan's fact with a person's text, an unknown id or a precision out of range refuses whole: " + to);
    }
    ok (std::isnan (restored.view().momentary[1].value) && std::isinf (restored.view().integratedLufs), "NaN gaps and silence are explicit and restored");
    ok (restored.view().source.name == special && restored.view().revision == v.revision, "strings and 64-bit identities are lossless");
    std::string shortBuffer (text.size() - 1, 'x');
    ok (Codec::encode (v, shortBuffer) == CodecStatus::TooSmall && shortBuffer == std::string (text.size() - 1, 'x'), "short output is refused without a partial write");
    const std::string broken[] = { "{}", text + "x", "{\"unknown\":0," + text.substr (1), "{\"job\":0," + text.substr (1), text.substr (0, text.size() - 1) };
    for (const auto& input : broken)
    {
        const auto before = encoded (restored.view());
        const auto spent = declared::spend ([&] { status = Codec::decode (input, restored); });
        ok (status == CodecStatus::Invalid && spent.bytes == 0 && before == encoded (restored.view()), "invalid, missing, duplicate and unknown fields refuse whole before allocation");
    }
    const auto field = text.rfind ("\"sourceBytes\":");
    const auto end = text.find (',', field);
    const auto item = text.substr (field, end - field);
    auto reordered = text.substr (0, field) + text.substr (end + 1);
    reordered.insert (1, item + ',');
    ok (Codec::decode (reordered, restored) == CodecStatus::Ok && encoded (restored.view()) == text,
        "named fields decode independently of object order");
    for (const auto& bad : { "-1", "0.5", "9007199254740992", "\"NaN\"" })
    {
        auto invalid = text;
        const auto value = field + std::string ("\"sourceBytes\":").size();
        invalid.replace (value, end - value, bad);
        ok (Codec::decodedBytes (invalid).status == CodecStatus::Invalid, "decoder rejects invalid byte counts before allocation");
    }
    v.sourceBytes = 9007199254740992.0;
    ok (Codec::encodedBytes (v).status == CodecStatus::Invalid, "byte counters must be strictly below 2^53");
    v.sourceBytes = 0.5;
    ok (Codec::encodedBytes (v).status == CodecStatus::Invalid, "byte counters are whole bytes");
    s.reset(); base = Snapshot {};
    ok (encoded (owned.view()) == text, "snapshot owns its data beyond the session's life");
}
void contracts()
{
    Audio audio;
    // Cancellation at each phase boundary clears only the cancelled work's progress.
    for (unsigned units : { 0u, 4u, 5u, 9u })
    {
        auto s = fresh();
        const auto job = apply (*s, audio.load()).job;
        (void) step (*s, units);
        const auto progress = s->snapshot().view().measurementProgress;
        const auto revision = s->revision();
        const auto seq = detail::Inspector::sequence (*s);
        ok (apply (*s, command::Cancel { 2, job }).rejection == Rejection::None, "measurement cancellation accepts every live phase");
        const auto v = snapshot (*s);
        ok (s->state() == State::MeasurementStopped && s->revision() == revision + 1,
            "cancellation retains the last measurement state");
        ok (v.view().measurementJob == 0 && v.view().measurementProgress.completedUnits == progress.completedUnits
            && v.view().measurementProgress.totalUnits == progress.totalUnits && v.view().measurementProgress.weightsVersion != 0,
            "measurement cancellation retains its progress");
        ok (s->events().size() == 1 && s->events()[0].seq == seq + 1
            && s->events()[0].payload.fact.view().id == FactId::MeasurementStopped, "cancel publishes the stopped measurement fact");
        const auto before = encoded (v.view());
        ok (apply (*s, command::Cancel { 3, job }).rejection == Rejection::NoJob
            && before == encoded (snapshot (*s).view()) && s->events()[0].seq == seq + 2,
            "rejection changes no snapshot field but publishes and advances seq");
        if (units < 5)
        {
            ok (s->source().frames != 0 && s->source().hash != 0, "phase-one cancellation retains the audio identity");
            (void) apply (*s, audio.load());
            measureReady (*s);
            ok (s->state() == State::Measured2, "the shell can reload the same audio after phase-one cancellation");
        }
    }
    for (unsigned units : { 5u, 10u })
    {
        auto s = fresh(); (void) apply (*s, audio.load()); measureReady (*s, units == 5);
        const auto state = s->state(); const auto measurement = s->measurementJob();
        const auto job = apply (*s, command::Master { 2 }).job; (void) step (*s, 2);
        (void) apply (*s, command::Cancel { 3, job });
        const auto v = snapshot (*s);
        ok (s->state() == state && s->measurementJob() == measurement && ! s->mastering()
            && v.view().masterProgress.completedUnits == 0 && v.view().masterProgress.totalUnits == 0
            && v.view().masterProgress.weightsVersion == 0, "master cancellation keeps the measured state and resets progress");
    }
    // Each Driver failure and an exhausted/corrupt measurement cursor must emit Contract and disarm the job.
    for (unsigned fault = 0; fault < 4; ++fault)
    {
        auto s = fresh(); (void) apply (*s, audio.load());
        detail::Inspector::skipWaveform (*s);
        if (fault == 0) { detail::Inspector::measurementUnit (*s, 4); detail::Inspector::state (*s, State::Measured1); }
        if (fault == 1) { detail::Inspector::measurementUnit (*s, 9); detail::Inspector::state (*s, State::Measured2); }
        if (fault == 2 || fault == 3) detail::Inspector::measurementUnit (*s, fault == 2 ? 10u : std::numeric_limits<std::uint32_t>::max());
        (void) step (*s, 1);
        const auto& error = s->events().back();
        ok (error.kind == EventKind::Error && error.payload.error.code == ErrorCode::Contract
            && error.payload.error.fact.view().id == FactId::SessionContract && error.payload.error.recover == Recover::None,
            "failed driver or out-of-table cursor publishes Contract");
        ok (s->measurementJob() == 0 && s->job() == 0 && step (*s, 16).state == StepState::Done,
            "contract failure drops the job instead of continuing beyond the table");
    }
    for (const auto id : { FactId::Measurement1, FactId::Measurement2, FactId::MasterPass, FactId::MasterReady, FactId::Cancelled,
                          FactId::SessionTrap, FactId::SessionContract, FactId::SessionRefusal, FactId::SessionMemory,
                          FactId::SessionPoisoned, FactId::SessionStale, FactId::RejectedInvalidUtf8 })
    {
        const auto fact = id == FactId::MasterPass ? text::Fact::of (id, text::Arg::count (3)) : text::Fact::of (id);
        for (const auto lang : { text::Lang::Ru, text::Lang::En })
        {
            const auto rendered = text::Text::text (fact, lang);
            ok (! rendered.empty() && rendered != text::Text::key (id) && rendered.find ('{') == std::string::npos,
                "every event and UTF-8 refusal fact renders a whole message in both catalog languages");
        }
    }
    ok (text::Text::text (text::Fact::of (FactId::MasterPass, text::Arg::count (3)), text::Lang::En) == "Master · pass 3",
        "the human pass label carries no budget");
    Notification saved;
    {
        Notification event; event.kind = EventKind::Fact;
        std::string first = "source.wav", second = "target";
        const auto fact = text::Fact::of (FactId::Value, text::Arg::text (first), text::Arg::text (second));
        bool assigned = false;
        const auto spent = declared::spend ([&] { assigned = event.payload.fact.assign (fact); });
        saved = event;
        first.assign (first.size(), 'x'); second.clear();
        ok (assigned && spent.bytes == 0 && saved.payload.fact.view().args[0].userText == "source.wav",
            "event user text is owned without an allocation, independent of the producer");
    }
    Notification moved = std::move (saved); saved = {};
    ok (moved.payload.fact.view().args[0].userText == "source.wav" && moved.payload.fact.view().args[1].userText == "target",
        "copied and moved notifications bind views to their own bytes after the original dies");
    ok (moved.payload.fact.assign (moved.payload.fact.view()), "assigning an owned fact's own view is safe");
    const std::string tooLong (OwnedFact::kTextCapacity + 1, 'x');
    ok (! moved.payload.fact.assign (text::Fact::of (FactId::Value, text::Arg::text (tooLong)))
        && moved.payload.fact.view().args[0].userText == "source.wav", "over-capacity user text refuses whole without truncation");
    const std::string exact (OwnedFact::kTextCapacity, 'y');
    ok (moved.payload.fact.assign (text::Fact::of (FactId::Value, text::Arg::text (exact)))
        && moved.payload.fact.view().args[0].userText == exact, "the full declared user-text capacity is usable");
    auto s = fresh();
    for (const std::string invalid : { std::string ("\x80"), std::string ("\xC0\xAF"), std::string ("\xE0\x80\x80"),
        std::string ("\xED\xA0\x80"), std::string ("\xF4\x90\x80\x80"), std::string ("\xF0\x9F"), std::string ("\xC2x") })
    {
        auto request = audio.load(); request.meta.name = invalid;
        const auto need = s->check (request); const auto before = encoded (snapshot (*s).view());
        ok (need.rejection == Rejection::InvalidUtf8 && need.bytes == 0, "invalid UTF-8 load names are refused at check");
        const auto answer = apply (*s, request);
        ok (answer.rejection == Rejection::InvalidUtf8 && before == encoded (snapshot (*s).view()), "invalid UTF-8 refusal leaves the snapshot unchanged");
        const auto fact = text::Text::rejected (answer, request);
        ok (fact && unsigned (fact->id) == 100 + unsigned (answer.rejection), "rejected event text uses 100 plus the appended code");
    }
    const std::string valid ("\0\xC2\xA2\xE2\x82\xAC\xF0\x9F\x8E\xB5", 10);
    auto request = audio.load(); request.meta.name = valid;
    ok (apply (*s, request).rejection == Rejection::None, "valid UTF-8 at every width, including NUL, is kept");
    const auto json = encoded (snapshot (*s).view()); Snapshot restored;
    ok (Codec::decode (json, restored) == CodecStatus::Ok && restored.view().source.name == valid, "UTF-8 names survive the JSON round trip");
}

void numbers()
{
    bool same = true;
    std::uint64_t seed = 0xD1342543DE82EF95ull;
    for (unsigned i = 0; i < 2500; ++i)
    {
        seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
        const auto bits = seed * 0x2545F4914F6CDD1Dull;
        const auto d = std::bit_cast<double> (bits);
        if (! std::isfinite (d)) continue;
        char buffer[detail::JsonNumber::capacity];
        const auto n = detail::JsonNumber::write (d, buffer);
        buffer[n] = '\0';
        double read = 0;
        const double independentlyRead = std::strtod (buffer, nullptr); // test oracle only; never in session
        same = same && detail::JsonNumber::read ({ buffer, n }, read) && std::bit_cast<std::uint64_t> (read) == bits
                    && std::bit_cast<std::uint64_t> (independentlyRead) == bits;
    }
    ok (same, "exact decimal codec round trips deterministic binary64 samples across exponents");
    double d = 0;
    ok (detail::JsonNumber::read ("0.1", d) && std::bit_cast<std::uint64_t> (d) == 0x3FB999999999999Aull, "decimal input rounds once, correctly");
    ok (detail::JsonNumber::read ("1.00000000000000011102230246251565404236316680908203125", d) && std::bit_cast<std::uint64_t> (d) == 0x3FF0000000000000ull, "halfway decimal rounds to even");
}
void eqSnapshot()
{
    namespace eq = felitronics::eq;
    auto created = Session::create(); auto& s = *created.session;
    ok (s.snapshot().view().eqCurve.empty(), "empty session has no EQ curve");
    float sample[4] {}; const float* pcm[] { sample, sample };
    (void) s.apply (command::Load { 1, { pcm, 2, 4, 48000 }, {} });
    ok (s.snapshot().view().eqCurve.empty(), "unplaced devices have no EQ curve");
    measureReady (s);
    const auto cfg = config::Config::load();
    const auto& engine = cfg.config.engine;
    std::uint64_t curveHash = 0xCBF29CE484222325ull;
    for (const auto rate : { 8000u, 44100u, 48000u, 192000u })
    {
        (void) s.apply (command::Load { 1, { pcm, 2, 4, rate }, {} });
        measureReady (s);
        for (int slope = 6; slope <= 96; slope += 6)
        {
            HpfFields<Touched> hp; hp.on = true; hp.fq = 36.25; hp.slope = slope;
            TiltFields<Touched> tilt; tilt.on = true; tilt.db = -1.25;
            LowFields<Touched> low; low.on = true; low.db = slope % 12 ? -5.25 : 5.25;
            ok (s.apply (command::EditDevice { 2, hp }).rejection == Rejection::None
                && s.apply (command::EditDevice { 3, tilt }).rejection == Rejection::None
                && s.apply (command::EditDevice { 4, low }).rejection == Rejection::None, "all EQ tasks accept edits while hidden");
            const auto snap = s.snapshot(); const auto& v = snap.view();
            ok (v.eqCurve.size() == kEqCurvePoints && v.handFieldCount == 7, "summed curve and touched-field count are published");
            eq::BandParams h, t, l;
            h.on = t.on = l.on = true;
            h.type = eq::FilterType::HighPass; h.lanes[0].freq = 36.25; h.lanes[0].slope = slope;
            t.type = eq::FilterType::Tilt; t.lanes[0].freq = engine.tilt.freqHz; t.lanes[0].gainDb = -1.25; t.lanes[0].slope = 6;
            l.type = eq::FilterType::LowShelf; l.lanes[0].freq = engine.low.freqHz; l.lanes[0].Q = engine.low.q; l.lanes[0].gainDb = *low.db;
            bool agrees = true, grid = true; double previous = 0;
            for (const auto& p : v.eqCurve)
            {
                const double w = 2 * eq::kPi * p.hz / rate;
                const double expected = 20 * std::log10 (std::abs (eq::bandResponse (h, rate, w)))
                                      + 20 * std::log10 (std::abs (eq::bandResponse (t, rate, w)))
                                      + 20 * std::log10 (std::abs (eq::bandResponse (l, rate, w)));
                agrees = agrees && std::isfinite (p.db) && std::abs (p.db - expected) < 0.002;
                grid = grid && p.hz > previous && p.hz <= double (rate) * 0.5;
                previous = p.hz;
                curveHash = digest (digest (curveHash, std::bit_cast<std::uint64_t> (p.hz)), std::bit_cast<std::uint64_t> (p.db));
            }
            ok (agrees && grid, "summed response matches core high-pass + tilt + low, every slope/rate");
            (void) s.apply (command::SetManual { 5, true });
            const auto shown = s.snapshot();
            bool unchanged = true;
            for (std::size_t i = 0; i < v.eqCurve.size(); ++i)
                unchanged = unchanged && std::bit_cast<std::uint64_t> (v.eqCurve[i].db) == std::bit_cast<std::uint64_t> (shown.view().eqCurve[i].db);
            ok (unchanged, "panel visibility leaves the sounding curve unchanged");
            (void) s.apply (command::SetManual { 6, false });
        }
    }
    std::printf ("EQ response fingerprint: %016llx\n", static_cast<unsigned long long> (curveHash));
    HpfFields<Touched> hp; hp.on = false;
    TiltFields<Touched> tilt; tilt.on = false;
    LowFields<Touched> low; low.on = false;
    (void) s.apply (command::EditDevice { 7, hp });
    (void) s.apply (command::EditDevice { 8, tilt });
    const auto before = s.snapshot();
    (void) s.apply (command::EditDevice { 9, low });
    const auto flat = s.snapshot(); bool zero = true, previousHasLow = false;
    for (const auto& p : flat.view().eqCurve) zero = zero && std::abs (p.db) < 1e-12;
    for (const auto& p : before.view().eqCurve) previousHasLow = previousHasLow || std::abs (p.db) > 0.1;
    ok (zero && previousHasLow, "ticks bypass their bands; the previous curve owns its points");
    (void) s.apply (command::Master { 10 });
    ok (s.jobRecipe().project.devices.low.hand.db == 5.25 && ! s.jobRecipe().project.manual,
        "master recipe keeps hidden hand edits");
}

void environment()
{
    Audio audio; auto s = fresh(); (void) apply (*s, audio.load());
    const auto before = encoded (snapshot (*s).view());
    const auto env = budget::saveFpEnvironment();
    const bool changed = budget::setFlushToZero();
    if (changed)
    {
        const auto result = s->step (1);
        const auto need = Codec::decodedBytes (before);
        budget::restoreFpEnvironment (env);
        ok (s->events().size() == 1 && s->events()[0].kind == EventKind::Error
            && s->events()[0].payload.error.fact.view().id == FactId::SessionRefusal
            && s->events()[0].payload.error.code == ErrorCode::Refusal
            && s->events()[0].payload.error.recover == Recover::Continue, "FP refusal publishes resumable recovery");
        ok (result.refused && result.units == 0 && need.status == CodecStatus::FloatingPointEnvironment, "pump and codec refuse hostile floating-point environment");
        ok (before == encoded (snapshot (*s).view()), "FP refusal preserves all snapshot state");
        while (s->measurementJob() != 0) (void) s->step (16);
        ok (s->state() == State::Measured2, "restoring the environment resumes the refused job");
    }
    else budget::restoreFpEnvironment (env);
    auto empty = fresh();
    const auto seq = detail::Inspector::sequence (*empty);
    if (budget::setFlushToZero())
    {
        const auto result = empty->step (0);
        budget::restoreFpEnvironment (env);
        ok (! result.refused && result.state == StepState::Done && empty->events().empty()
            && detail::Inspector::sequence (*empty) == seq, "Empty poll publishes nothing even under a hostile environment");
    }
    else budget::restoreFpEnvironment (env);
}
}
int main (int argc, char** argv)
{
    if (argc == 2 && std::string_view (argv[1]) == "--snapshot-limit")
    {
        const auto bytes = detail::snapshotTextBytes (UINT32_MAX, std::uint32_t (1));
        std::puts ("snapshot allocation limit reached"); std::fflush (stdout);
        (void) detail::snapshotAllocationSize<std::uint32_t> (bytes);
        return 0; // Reaching this line means the allocation guard failed.
    }
    if (argc == 2 && std::string_view (argv[1]) == "--emit-limit")
    {
#if defined (NDEBUG)
        return 77;
#else
        auto s = Session::create();
        detail::Inspector::fillBatch (*s.session, kEventBatch);
        if (s.session->events().size() != kEventBatch) return 2;
        std::puts ("batch capacity filled"); std::fflush (stdout);
        detail::Inspector::fillBatch (*s.session, 1);
        return 0; // Reaching this line means the debug bound failed.
#endif
    }
    const auto max = std::numeric_limits<std::uint32_t>::max();
    ok (detail::snapshotTextBytes (max, max) == 8589934590ull, "combined text widens each 32-bit size before adding");
    ok (detail::snapshotStorage (max, max, max, max, max, max) == 25769803770ull,
        "snapshot demand widens all six 32-bit terms before adding");
    ok (detail::snapshotAllocationSize<std::uint32_t> (max) == max,
        "the largest representable allocation size is accepted without allocating");
    pump(); tableBetweenSteps(); codec(); numbers(); eqSnapshot(); environment(); contracts();
    return felitronics::test::report();
}
