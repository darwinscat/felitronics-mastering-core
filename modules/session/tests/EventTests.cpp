// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "DeclaredBudget.h"
#include "Driver.h"
#include "FpEnvironmentControl.h"
#include "JsonNumber.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>
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
    ok (budget::covers (need.bytes, spent) && need.bytes == std::uint64_t (spent.bytes), "command demand is exact, with events included");
    return answer;
}
Stepped step (Session& s, std::uint32_t units)
{
    Stepped result;
    const auto spent = budget::spend ([&] { result = s.step (units); });
    ok (spent.bytes == 0 && budget::covers (Session::stepBytes(), spent), "pump allocates nothing");
    return result;
}
Snapshot snapshot (const Session& s)
{
    Snapshot out;
    const auto need = s.snapshotBytes();
    const auto spent = budget::spend ([&] { out = s.snapshot(); });
    ok (need == std::uint64_t (spent.bytes), "snapshot declares exact owned storage");
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
            hash = digest (hash, std::uint8_t (e.payload.fact.id)); hash = digest (hash, e.payload.fact.count);
            for (unsigned i = 0; i < e.payload.fact.count; ++i)
            {
                hash = digest (hash, std::uint8_t (e.payload.fact.args[i].kind));
                hash = digest (hash, std::bit_cast<std::uint64_t> (e.payload.fact.args[i].value));
                hash = digest (hash, e.payload.fact.args[i].count); hash = digest (hash, e.payload.fact.args[i].enumId);
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
    for (unsigned left = 5; left;)
    {
        const unsigned n = left < chunk ? left : chunk;
        left -= step (*s, n).units; collect();
    }
    ok (s->state() == State::Measured1 && events.back().payload.fact.id == FactId::Measurement1, "phase one fact is available on the step that establishes it");
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
    ok (one.size() == 22 && cancelled.size() == 25, "fixed event counts for the two scenarios");
    ok (eventsHash (one) == 0x82ea9ee80d5e4d21ull && eventsHash (cancelled) == 0xcb2185b270ed0182ull, "event fixtures pin every active payload field");
    std::printf ("event fingerprints: %016llx %016llx\n", (unsigned long long) eventsHash (one), (unsigned long long) eventsHash (cancelled));
    Audio audio; auto s = fresh();
    const auto old = apply (*s, audio.load()).job;
    const auto source = s->source().hash;
    (void) step (*s, 2);
    (void) apply (*s, command::Cancel { 2, old });
    ok (step (*s, 16).state == StepState::Done && s->state() == State::Loaded, "measurement cancel stops work and keeps the source");
    ok (! Driver::measured1 (*s, old, source), "cancelled measurement completion is ignored");
    const auto next = apply (*s, audio.load()).job;
    ok (next != old && s->source().hash == source, "even reloading identical samples gets a new job identity");
    ok (! Driver::measured1 (*s, old, source) && ! Driver::measured1 (*s, next, source + 1), "both measurement job and source are checked");
    (void) step (*s, 5);
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
            if (col >= 2)
            {
                (void) step (*s, (col == 3 || col == 5) ? 10 : 5);
                kept = apply (*s, command::Master { 10 }).job;
                (void) step (*s, 4);
                if (col >= 4) { (void) apply (*s, command::Master { 11 }); (void) step (*s, 1); }
            }
            (void) apply (*s, command::SetManual { 12, true });
            (void) snapshot (*s);
            const auto before = encoded (snapshot (*s).view());
            const JobId job = s->job() ? s->job() : s->measurementJob();
            HpfFields<Touched> hpf; hpf.fq = 36.0;
            HpfFields<Mark> mask; mask.fq = true;
            const Request requests[] = { audio.load(), command::SetTarget { 13, "allStreaming", OnEdits::Keep },
                command::EditTarget { 14, { -13.0, {} } }, command::EditDevice { 15, hpf }, command::RevertEdits { 16, mask },
                command::SetManual { 17, false }, command::Master { 18 }, command::Cancel { 19, job }, command::Forget { 20, kept } };
            ok (std::size_t (s->column()) == col, "pump establishes the table column");
            const auto answer = apply (*s, requests[std::size_t (row.command)]);
            ok (answer.rejection == row.cell[col], "every command obeys the table between pump steps");
            if (answer.rejection != Rejection::None) ok (before == encoded (snapshot (*s).view()), "rejection leaves the entire snapshot intact");
        }
}
void codec()
{
    Audio audio; auto s = fresh();
    (void) apply (*s, audio.load()); (void) step (*s, 10);
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
    const auto field = text.find ("\"sourceBytes\":");
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
            && s->events()[0].payload.error.fact.id == FactId::FloatingPointEnvironment
            && s->events()[0].payload.error.recover == Recover::None, "FP refusal publishes a self-contained contract error");
        ok (result.refused && result.units == 0 && need.status == CodecStatus::FloatingPointEnvironment, "pump and codec refuse hostile floating-point environment");
        ok (before == encoded (snapshot (*s).view()), "FP refusal preserves all snapshot state");
    }
    else budget::restoreFpEnvironment (env);
}
}
int main()
{
    pump(); tableBetweenSteps(); codec(); numbers(); environment();
    return felitronics::test::report();
}
