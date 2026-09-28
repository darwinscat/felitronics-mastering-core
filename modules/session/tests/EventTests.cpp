// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "DeclaredBudget.h"
#include "Advance.h"
#include "Driver.h"
#include "FpEnvironmentControl.h"
#include "JsonNumber.h"
#include "SnapshotStorage.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>
#include <felitronics/eq/EqBand.h>
#include <bit>
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
// Test-only corruption reaches the guards without depending on real driver failures.
struct felitronics::session::detail::Inspector
{
    static void state (Session& s, State state) { s.state_ = state; }
    static void measurementUnit (Session& s, std::uint32_t unit) { s.measurementUnit_ = unit; }
    static void noMasterRoom (Session& s) { s.masterRoom_ = s.masterCount_; }
    static std::uint64_t sequence (const Session& s) { return s.sequence_; }
    static void fillBatch (Session& s, std::size_t count) { for (std::size_t i = 0; i < count; ++i) s.emit ({}); }
};
namespace budget = felitronics::session::testing;
namespace
{
struct Audio
{
    float samples[4] { 0.0f, 0.25f, -0.25f, 0.0f };
    const float* planes[1] { samples };
    command::Load load() const { return { 1, { planes, 1, 4, 48000 }, { "source.wav", 48000, true, 24 } }; }
};
std::unique_ptr<Session> fresh()
{
    Created made;
    const auto need = Session::createBytes();
    const auto spent = budget::spend ([&] { made = Session::create(); });
    ok (made.status == Status::Ok && budget::covers (need, spent) && need == std::uint64_t (spent.bytes), "create declares its exact demand");
    return std::move (made.session);
}
Answer apply (Session& s, const Request& r)
{
    const auto need = s.check (r);
    Answer answer;
    const auto spent = budget::spend ([&] { answer = s.apply (r); });
    ok (budget::covers (need.bytes, spent)
        && (std::holds_alternative<command::ImportProject> (r) || std::holds_alternative<command::Load> (r) || need.bytes == std::uint64_t (spent.bytes)), "command demand covers events and import validation");
    return answer;
}
Stepped step (Session& s, std::uint32_t units)
{
    Stepped result;
    const auto demand = s.measurementStorage ({ nullptr, s.source().channels, s.source().frames, s.source().sampleRate });
    const auto spent = budget::spend ([&] { result = s.step (units); });
    ok (budget::covers (std::uint64_t (demand.workspaceBytes + demand.resultBytes + demand.allocatorBytes), spent), "pump stays within the declared preparation and output demand");
    return result;
}
Snapshot snapshot (const Session& s)
{
    Snapshot out;
    const auto need = s.snapshotBytes();
    const auto spent = budget::spend ([&] { out = s.snapshot(); });
    ok (need == std::uint64_t (spent.bytes) && need == Snapshot::storageFor (out.view()), "snapshot and its storage demand use the same complete view");
    return out;
}
std::string encoded (const SnapshotView& view)
{
    CodecNeed need;
    const auto checked = budget::spend ([&] { need = Codec::encodedBytes (view); });
    ok (checked.bytes == 0 && need.status == CodecStatus::Ok, "encoding size query allocates nothing");
    std::string text (std::size_t (need.bytes), '\0');
    CodecStatus status {};
    const auto spent = budget::spend ([&] { status = Codec::encode (view, text); });
    ok (status == CodecStatus::Ok && spent.bytes == 0, "codec writes into exactly the declared caller storage");
    return text;
}
std::uint64_t digest (std::uint64_t hash, std::uint64_t n)
{
    for (unsigned i = 0; i < 8; ++i) hash = (hash ^ ((n >> (i * 8)) & 255)) * 0x100000001B3ull;
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
            hash = digest (hash, std::uint16_t (fact.id)); hash = digest (hash, fact.argCount);
            for (unsigned i = 0; i < fact.argCount; ++i)
            {
                const auto& arg = fact.args[i];
                hash = digest (hash, std::uint8_t (arg.kind)); hash = digest (hash, std::uint8_t (arg.unit));
                hash = digest (hash, arg.precision); hash = digest (hash, std::uint8_t (arg.sign));
                hash = digest (hash, std::uint8_t (arg.bound)); hash = digest (hash, std::uint16_t (arg.termId));
                hash = digest (hash, std::bit_cast<std::uint64_t> (arg.number));
                hash = digest (hash, std::uint64_t (arg.integer)); hash = digest (hash, arg.userText.size());
                for (const char c : arg.userText) hash = digest (hash, static_cast<unsigned char> (c));
            }
        }
        if (e.kind == EventKind::Done) hash = digest (hash, e.payload.done.masterId);
        if (e.kind == EventKind::Rejected) { hash = digest (hash, e.payload.rejected.commandId); hash = digest (hash, std::uint8_t (e.payload.rejected.code)); }
    }
    return hash;
}
std::vector<Notification> scenario (std::uint32_t chunk, bool cancel)
{
    Audio audio; auto s = fresh(); std::vector<Notification> events;
    const auto collect = [&] { for (const auto& e : s->events()) events.push_back (e); };
    ok (apply (*s, audio.load()).job == 1, "load issues the measurement identity");
    ok (s->source().hash == 0x0ba6b096abb7c779ull, "event fixture PCM hash is pinned with its config version");
    ok (snapshot (*s).view().measurementProgress.totalUnits == 10, "measurement work estimate is available before the first step");
    ok (step (*s, 0).state == StepState::More && s->events().empty(), "zero units only polls");
    while (s->state() == State::Loaded) { (void) step (*s, 1); collect(); }
    ok (s->state() == State::Measured1 && events.back().payload.fact.view().id == FactId::Measurement1, "phase one fact is available on the step that establishes it");
    auto saved = snapshot (*s);
    const auto job = apply (*s, command::Master { 2 }).job;
    ok (snapshot (*s).view().masterProgress.totalUnits == 4, "master work estimate is available before the first step");
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
    ok (one.size() > 22 && cancelled.size() == one.size() + 3, "measurement publishes live work and cancellation adds three events");
    ok (eventsHash (one) == 0xe48109316b053c14ull && eventsHash (cancelled) == 0x8d46d4281edaa565ull, "event fixtures pin every active payload field");
    std::printf ("event fingerprints: %016llx %016llx\n", (unsigned long long) eventsHash (one), (unsigned long long) eventsHash (cancelled));
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
    budget::measure (*s, true);
    ok (! Driver::measured2 (*s, old, source), "stale phase two is ignored");
    (void) apply (*s, command::Cancel { 3, next });
    ok (apply (*s, command::Master { 4 }).rejection == Rejection::None, "master remains usable after phase two cancellation");
    (void) step (*s, 16);
    ok (s->masters().size() == 1, "cancel does not destroy the session");
}
void tableBetweenSteps()
{
    Audio audio;
    for (std::size_t col = 0; col < kColumns; ++col)
        for (const auto& row : Table::commands)
        {
            auto s = fresh();
            MasterId kept = 0;
            if (col != 0) (void) apply (*s, audio.load());
            if (col >= 2 && col != 6)
            {
                budget::measure (*s, col != 3 && col != 5);
                kept = apply (*s, command::Master { 10 }).job;
                (void) step (*s, 4);
                if (col == 4 || col == 5 || col == 8) { (void) apply (*s, command::Master { 11 }); (void) step (*s, 1); }
            }
            if (col >= 6) (void) apply (*s, command::Cancel { 22, s->measurementJob() });
            (void) apply (*s, command::SetManual { 12, true });
            (void) snapshot (*s);
            const auto before = encoded (snapshot (*s).view());
            const JobId job = s->job() ? s->job() : s->measurementJob();
            HpfFields<Touched> hpf; hpf.fq = 36.0;
            HpfFields<Mark> mask; mask.fq = true;
            const auto project = s->exportProject();
            const Request requests[] = { audio.load(), command::SetTarget { 13, "allStreaming" },
                command::EditTarget { 14, { -13.0, {} } }, command::EditDevice { 15, hpf }, command::RevertEdits { 16, mask },
                command::SetManual { 17, false }, command::Master { 18 }, command::Cancel { 19, job }, command::Forget { 20, kept }, command::ImportProject { 21, project.view() }, command::ContinueMeasurement { 23 } };
            ok (std::size_t (s->column()) == col, "pump establishes the table column");
            const auto answer = apply (*s, requests[std::size_t (row.command)]);
            const auto expected = row.command == Command::Cancel && job == 0 ? Rejection::NoJob : row.cell[col];
            ok (answer.rejection == expected, "every command obeys the table and active-job check between pump steps");
            if (answer.rejection != Rejection::None) ok (before == encoded (snapshot (*s).view()), "rejection leaves the entire snapshot intact");
        }
}
void codec()
{
    Audio audio; auto s = fresh();
    (void) apply (*s, audio.load()); budget::measure (*s);
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
    const auto copy = budget::spend ([&] { owned = Snapshot::copy (v); });
    ok (Snapshot::storageFor (v) == std::uint64_t (copy.bytes), "copy declares exact strings, masters and reading arrays");
    const auto text = encoded (owned.view());
    CodecNeed need;
    const auto checked = budget::spend ([&] { need = Codec::decodedBytes (text); });
    ok (need.status == CodecStatus::Ok && checked.bytes == 0 && need.bytes == Snapshot::storageFor (v), "decode validates and declares exact owned storage before work");
    Snapshot restored; CodecStatus status {};
    const auto decoded = budget::spend ([&] { status = Codec::decode (text, restored); });
    ok (status == CodecStatus::Ok && need.bytes == std::uint64_t (decoded.bytes), "decode asks exactly its declared demand");
    ok (encoded (restored.view()) == text, "named fields and both layers round trip, including rows and non-finite values");
    ok (std::isnan (restored.view().momentary[1].value) && std::isinf (restored.view().integratedLufs), "NaN gaps and silence are explicit and restored");
    ok (restored.view().source.name == special && restored.view().revision == v.revision, "strings and 64-bit identities are lossless");
    std::string shortBuffer (text.size() - 1, 'x');
    ok (Codec::encode (v, shortBuffer) == CodecStatus::TooSmall && shortBuffer == std::string (text.size() - 1, 'x'), "short output is refused without a partial write");
    const std::string broken[] = { "{}", text + "x", "{\"unknown\":0," + text.substr (1), "{\"job\":0," + text.substr (1), text.substr (0, text.size() - 1) };
    for (const auto& input : broken)
    {
        const auto before = encoded (restored.view());
        const auto spent = budget::spend ([&] { status = Codec::decode (input, restored); });
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
            budget::measure (*s);
            ok (s->state() == State::Measured2, "the shell can reload the same audio after phase-one cancellation");
        }
    }
    for (unsigned units : { 5u, 10u })
    {
        auto s = fresh(); (void) apply (*s, audio.load()); budget::measure (*s, units == 5);
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
        const auto spent = budget::spend ([&] { assigned = event.payload.fact.assign (fact); });
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
    budget::measure (s);
    const auto cfg = config::Config::load();
    const auto& engine = cfg.config.engine;
    std::uint64_t curveHash = 0xCBF29CE484222325ull;
    for (const auto rate : { 8000u, 44100u, 48000u, 192000u })
    {
        (void) s.apply (command::Load { 1, { pcm, 2, 4, rate }, {} });
        budget::measure (s);
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
            t.type = eq::FilterType::Tilt; t.lanes[0].freq = engine.tilt.freqHz; t.lanes[0].gainDb = -1.25;
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
        budget::measure (*s);
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
