// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE SCENARIO, END TO END (docs/DECIDE-COVERAGE.md, gaps A1–A3). One synthetic source, independent sessions: load →
// measure → plan → a person's hand (tilt +1.25 dB, low shelf +0.75 dB, a louder target) → master → export → import into
// another session → master. The two masters are one master: the same snapshot JSON, recipe, facts, defaults and sound
// versions, PCM and WAV bytes. Then, on the same file with a machine opinion of its own: the import keeps the
// file's machine layer; AdoptMachine and a master give the fresh session's master; step budgets of 1, 7 and a large one
// give the same bytes; a cancelled waiting master and a stale needles job publish nothing; import → master → release,
// repeated, with refusals between, leaves the session's live bytes where the first cycle left them.
//
// PARITY. The scenario prints its input, its versions and four digests (plan, facts, PCM, WAV) on `scenario-…` lines;
// tools/wasm/scenario-parity.mjs holds the native lines and checks the wasm run against them.

#include <felitronics/session/Session.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Text.h>
#include <felitronics/session/Config.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;

namespace
{
constexpr double kPi = 3.141592653589793;
constexpr std::uint32_t kLarge = 1u << 30;   // any budget above kStepUnits: a call takes kStepUnits units at most

// A kick with a needle on its attack, a pad that swells at 6 s: the source every session of the scenario loads. Built
// with the core's own sine and exp2, so native and wasm load the same bits.
struct Mix
{
    static constexpr unsigned rate = 48000;
    unsigned frames;
    std::vector<float> left, right;
    const float* planes[2] { nullptr, nullptr };
    explicit Mix (unsigned seconds = 10) : frames (rate * seconds), left (frames), right (frames)
    {
        namespace det = felitronics::core::det;
        for (unsigned i = 0; i < frames; ++i)
        {
            const double t = double (i) / rate, beat = double (i % (rate / 2)) / rate;
            const double kick = 0.22 * det::exp2 (-beat / 0.04) * det::sin (2 * kPi * 60.0 * beat);
            const double needle = beat < 0.0005 ? 0.35 * det::sin (2 * kPi * 3000.0 * beat + 0.5 * kPi) : 0.0;
            const double swell = t < 6.0 ? 0.6 : 1.0;
            left[i] = float (kick + needle + swell * (0.05 * det::sin (2 * kPi * 220.0 * t) + 0.04 * det::sin (2 * kPi * 331.0 * t)));
            right[i] = float (kick + needle + swell * (0.05 * det::sin (2 * kPi * 220.0 * t + 0.4) + 0.04 * det::sin (2 * kPi * 331.0 * t + 1.1)));
        }
        planes[0] = left.data(); planes[1] = right.data();
    }
    Mix (const Mix&) = delete;
    command::Load load (CommandId id) const { return { id, { planes, 2, frames, rate }, { "mix.wav", rate, true, 24 } }; }
};

struct Fnv
{
    std::uint64_t value = 0xCBF29CE484222325ull;
    void byte (std::uint8_t b) { value = (value ^ b) * 0x100000001B3ull; }
    void text (std::string_view s) { for (const char c : s) byte (std::uint8_t (c)); }
    void word (std::uint64_t bits, unsigned bytes) { for (unsigned i = 0; i < bytes; ++i) byte (std::uint8_t (bits >> (8 * i))); }
};

// What the pump published: every event's job, and each fact in both languages.
struct Seen
{
    std::vector<JobId> jobs;
    std::vector<std::string> facts;
    bool from (JobId job) const { return std::find (jobs.begin(), jobs.end(), job) != jobs.end(); }
};
void take (const Session& s, Seen& seen)
{
    for (const auto& e : s.events())
    {
        seen.jobs.push_back (e.jobId);
        if (e.kind != EventKind::Fact) continue;
        const auto fact = e.payload.fact.view();
        seen.facts.push_back (text::Text::text (fact, text::Lang::Ru) + " | " + text::Text::text (fact, text::Lang::En));
    }
}
bool pump (Session& s, std::uint32_t budget, Seen& seen)
{
    for (unsigned i = 0; i < 50000000u; ++i)
    {
        if (s.measurementJob() == 0 && s.needlesJob() == 0 && s.job() == 0 && s.damageJobs().empty()) return true;
        (void) s.step (budget);
        take (s, seen);
    }
    return false;
}
bool pump (Session& s, std::uint32_t budget) { Seen seen; return pump (s, budget, seen); }

std::unique_ptr<Session> measured (const Mix& mix, std::uint32_t budget)
{
    auto s = Session::create().session;
    ok (s->apply (command::SetTarget { 1, "allStreaming" }).rejection == Rejection::None
        && s->apply (mix.load (2)).rejection == Rejection::None, "PRECONDITION: the mix loads on allStreaming");
    ok (pump (*s, budget) && s->state() == State::Measured2 && s->snapshot().view().plan.status == PlanStatus::Ready,
        "PRECONDITION: measured at a budget of " + std::to_string (std::min (budget, kStepUnits)) + ", the plan ready");
    return s;
}

// A master to its end, as a shell takes it: the facts its job published, its PCM, its WAV in 64 KiB slices, its kept
// recipe. The master is then released.
struct Made
{
    bool made = false;
    std::vector<std::string> facts;
    std::vector<float> pcm;
    std::vector<std::uint8_t> wav;
    std::uint32_t rate = 0;
    Kept kept {};
};
Made master (Session& s, CommandId id, std::uint32_t budget)
{
    Made out;
    if (s.apply (command::Master { id }).rejection != Rejection::None) return out;
    Seen seen;
    if (! pump (s, budget, seen) || s.masters().empty()) return out;
    // The damage grade, asked as the page asks it once the master is delivered (command::GradeDamage, v0.16.0), and
    // graded to its end: its line among the master's facts, as it always was.
    if (s.masters().back().report && s.masters().back().report->damage.status == MeasurementStatus::Pending)
    {
        if (s.apply (command::GradeDamage { id + 500u, s.masters().back().id }).rejection != Rejection::None) return out;
        take (s, seen);
        if (! pump (s, budget, seen)) return out;
    }
    out.facts = std::move (seen.facts);
    out.kept = s.masters().back();
    const auto token = s.pendingMaster();
    const auto shape = s.masterAudioShape (token);
    const auto wav = s.masterWavPlan (token);
    out.pcm.resize (std::size_t (shape.frames * shape.channels));
    out.wav.resize (std::size_t (wav.bytes));
    out.rate = wav.rate;
    bool copied = token.master == out.kept.id && ! out.pcm.empty() && bool (wav) && s.copyMaster (token, out.pcm) == MasterTransferStatus::Ok;
    for (std::size_t at = 0; copied && at < out.wav.size(); at += 65536u)
        copied = s.copyMasterWav (token, at, { out.wav.data() + at, std::min<std::size_t> (65536u, out.wav.size() - at) }) == MasterTransferStatus::Ok;
    out.made = copied && s.releaseMaster (token) == MasterTransferStatus::Ok && s.pendingMaster().master == 0;
    return out;
}
bool sameSound (const Made& a, const Made& b)
{
    if (! a.made || ! b.made || a.rate != b.rate || a.pcm.size() != b.pcm.size() || a.wav != b.wav) return false;
    for (std::size_t i = 0; i < a.pcm.size(); ++i)
        if (std::bit_cast<std::uint32_t> (a.pcm[i]) != std::bit_cast<std::uint32_t> (b.pcm[i])) return false;
    return true;
}
bool sameRecipe (const Recipe& a, const Recipe& b)
{
    return a.source == b.source && a.sound == b.sound && a.readyHash == b.readyHash && a.deliveryRateHz == b.deliveryRateHz
        && a.readyVersion == b.readyVersion && a.project.target == b.project.target;
}

// The whole snapshot as the wire carries it, but for the revision and the kept masters' ids (counts of commands and of
// jobs, which the paths differ in) and the plan's fromFile (true after an import by definition; asserted on its own).
std::string json (const Session& s)
{
    const auto snapshot = s.snapshot();
    SnapshotView v = snapshot.view();
    std::vector<Kept> kept (v.masters.begin(), v.masters.end());
    for (auto& k : kept) k.id = 0;
    v.masters = kept;
    v.revision = 0;
    v.plan.fromFile = false;
    const auto need = Codec::encodedBytes (v);
    std::string out (std::size_t (need.bytes), '\0');
    return need.status == CodecStatus::Ok && Codec::encode (v, out) == CodecStatus::Ok ? out : std::string {};
}
std::string exported (const Session& s) { return std::string (s.exportProject().view()); }
// The value of the first line `key = "value"` of a project file, and the file with that value replaced.
std::string header (std::string_view file, std::string_view key)
{
    const auto at = file.find (std::string (key) + " = \"");
    if (at == std::string_view::npos) return {};
    const auto from = at + key.size() + 4;
    return std::string (file.substr (from, file.find ('"', from) - from));
}
std::string withHeader (std::string file, std::string_view key, std::string_view value)
{
    const auto at = file.find (std::string (key) + " = \"");
    if (at == std::string::npos) return {};
    const auto from = at + key.size() + 4;
    return file.replace (from, file.find ('"', from) - from, value);
}
std::string version (Version v) { return std::to_string (v.major) + '.' + std::to_string (v.minor) + '.' + std::to_string (v.patch); }

// What the first two sessions settled: the project file, the first master, the imported session's snapshot JSON and
// the ceiling its needles were measured at.
struct Reference
{
    std::string file;
    Made first;
    std::string snapshot;
    std::optional<double> needlesCeilingDb;
};

Reference theScenario()
{
    felitronics::test::group ("the scenario: load, measure, plan, hand, master, export, import into another session, master — one master");
    const Mix mix;

    // THE FIRST SESSION.
    auto ap = measured (mix, kLarge); auto& a = *ap;
    TiltFields<Touched> tilt; tilt.db = 1.25;
    LowFields<Touched> low; low.db = 0.75;
    command::EditTarget louder { 5, {} }; louder.fields.lufs = -13.0;
    ok (a.apply (command::EditDevice { 3, tilt }).rejection == Rejection::None && a.apply (command::EditDevice { 4, low }).rejection == Rejection::None
        && a.apply (louder).rejection == Rejection::None && pump (a, kLarge) && a.snapshot().view().plan.status == PlanStatus::Ready,
        "the hand: tilt +1.25 dB, the low shelf +0.75 dB, the target at −13 LUFS");
    const auto first = master (a, 6, kLarge);
    ok (first.made && ! first.facts.empty() && first.rate == Mix::rate, "the first master is made, with its report's facts");
    const auto file = exported (a);
    ok (header (file, "core").empty() && ! header (file, "defaults").empty(),
        "the file stamps the defaults (" + header (file, "defaults") + ") and no core");

    // THE SECOND SESSION: its own load and measurement, the file, a master.
    auto bp = measured (mix, kLarge); auto& b = *bp;
    ok (b.apply (command::ImportProject { 3, file }).rejection == Rejection::None && pump (b, kLarge), "the file imports into the second session");
    ok (b.snapshot().view().plan.fromFile && ! a.snapshot().view().plan.fromFile && b.snapshot().view().machineDifferences.empty(),
        "its machine layer is the file's, and the file's is what this core decides: no difference");
    ok (b.project().devices.tilt.hand.db && std::bit_cast<std::uint64_t> (*b.project().devices.tilt.hand.db) == std::bit_cast<std::uint64_t> (1.25),
        "the 1.25 dB arrives exactly");
    ok (exported (b) == file, "export → import → export is byte-identical");
    const auto second = master (b, 4, kLarge);
    ok (sameSound (first, second), "the second master is the first: PCM and WAV bytes");
    ok (sameRecipe (first.kept.recipe, second.kept.recipe) && first.kept.recipe.sound == config::Config::versions().sound,
        "the same recipe, at the config's sound version");
    ok (first.facts == second.facts, "the same facts (" + std::to_string (first.facts.size()) + ")");
    const auto snapshot = json (b);
    ok (json (a) == snapshot && ! snapshot.empty(), "the same snapshot JSON, the kept master's report in it");
    ok (header (exported (b), "defaults") == header (file, "defaults"), "the same defaults in the second session's file");

    // PARITY LINES (tools/wasm/scenario-parity.mjs). The plan is the project file and the ready recipe.
    Fnv plan, facts, pcm, wav;
    plan.text (file); plan.word (first.kept.recipe.readyHash, 8);
    for (const auto& f : first.facts) { facts.text (f); facts.byte ('\n'); }
    for (const float x : first.pcm) pcm.word (std::bit_cast<std::uint32_t> (x), 4);
    for (const auto x : first.wav) wav.byte (x);
    const auto versions = config::Config::versions();
    std::printf ("scenario-input source=%016llx frames=%u rate=%u\n", static_cast<unsigned long long> (a.source().hash), mix.frames, Mix::rate);
    std::printf ("scenario-versions core=%s felitronics-core=%s defaults=%s sound=%016llx config=%016llx\n",
        version (Session::version()).c_str(), version (Session::coreVersion()).c_str(), header (file, "defaults").c_str(),
        static_cast<unsigned long long> (versions.sound), static_cast<unsigned long long> (versions.all));
    std::printf ("scenario-parity plan=%016llx facts=%016llx pcm=%016llx wav=%016llx\n", static_cast<unsigned long long> (plan.value),
        static_cast<unsigned long long> (facts.value), static_cast<unsigned long long> (pcm.value), static_cast<unsigned long long> (wav.value));

    // A3: import → master → release → forget, again and again, with a refused import, master and forget between: the
    // live bytes stay where the first cycle left them.
    CommandId id = 10;
    ok (b.apply (command::Forget { id++, second.kept.id }).rejection == Rejection::None && b.masters().empty(),
        "PRECONDITION: the second session's master is forgotten, the first cycle ends");
    const double settled = b.liveBytes();
    bool flat = true, refused = true, same = true;
    for (int cycle = 2; cycle <= 5; ++cycle)
    {
        refused = refused && b.apply (command::ImportProject { id++, withHeader (file, "name", "nowhere") }).rejection != Rejection::None;
        refused = refused && b.apply (command::Master { id++, {}, b.source().hash + 1u, b.revision() }).rejection != Rejection::None;
        refused = refused && b.apply (command::Forget { id++, 1000 }).rejection != Rejection::None;
        flat = flat && b.liveBytes() == settled;
        same = same && b.apply (command::ImportProject { id++, file }).rejection == Rejection::None && pump (b, kLarge);
        const auto again = master (b, id++, kLarge);
        const bool forgotten = b.apply (command::Forget { id++, again.kept.id }).rejection == Rejection::None && b.masters().empty();
        same = same && sameSound (first, again) && forgotten;
        flat = flat && b.liveBytes() == settled;
    }
    ok (refused && same, "four more cycles: each refusal refused, each master the first one");
    ok (flat, "and the live bytes flat after the first cycle: " + std::to_string (settled) + " → " + std::to_string (b.liveBytes()));
    return { file, first, snapshot, a.snapshot().view().needlesCeilingDb };
}

// The file's machine layer: a high-pass from 36 Hz where this planner places it at its 32 Hz floor.
void theFilesMachine (const Reference& ref)
{
    felitronics::test::group ("the file's machine layer: kept by an import; adoptMachine gives the fresh session's master");
    const Mix mix;
    const auto altered = ref.file + "\n[hpf]\nfq.machine = 36\n";
    auto sp = measured (mix, kLarge); auto& other = *sp;
    const bool taken = other.apply (command::ImportProject { 3, altered }).rejection == Rejection::None && pump (other, kLarge);
    {
        const auto v = other.snapshot();
        const auto& differences = v.view().machineDifferences;
        ok (taken && other.project().devices.hpf.machine.fq == 36.0 && v.view().plan.fromFile && differences.size() == 1
            && differences[0].device == Device::Hpf && differences[0].fileValue == 36.0 && differences[0].coreValue == 32.0,
            "the file's 36 Hz kept, this planner's 32 Hz shown beside it");
    }
    const auto fileMaster = master (other, 4, kLarge);
    ok (! sameSound (fileMaster, ref.first), "the file's machine sounds: its master is not the planner's");
    ok (other.apply (command::AdoptMachine { 5 }).rejection == Rejection::None && other.project().devices.hpf.machine.fq == 32.0
        && other.snapshot().view().machineDifferences.empty() && other.project().devices.tilt.hand.db
        && *other.project().devices.tilt.hand.db == 1.25, "adoptMachine takes the planner's 32 Hz and keeps the person's 1.25 dB");
    const auto adopted = master (other, 6, kLarge);
    ok (sameSound (adopted, ref.first) && adopted.kept.recipe.readyHash == ref.first.kept.recipe.readyHash && adopted.facts == ref.first.facts,
        "its master sounds as the fresh session's: PCM, WAV, the ready recipe and the facts");
    ok (sameRecipe (adopted.kept.recipe, ref.first.kept.recipe) && exported (other) == ref.file,
        "and it is the fresh session's master, and the fresh session's file");
}

// Pump slicing: budgets of 1 and 7 units a call give the large budget's session and master.
void theSlicing (const Reference& ref)
{
    felitronics::test::group ("the scenario sliced: step budgets of 1, 7 and a large one give the same snapshot and the same master");
    const Mix mix;
    for (const std::uint32_t budget : { 1u, 7u })
    {
        auto sp = measured (mix, budget); auto& s = *sp;
        ok (s.apply (command::ImportProject { 3, ref.file }).rejection == Rejection::None && pump (s, budget), "the file imports");
        const auto made = master (s, 4, budget);
        ok (sameSound (made, ref.first) && sameRecipe (made.kept.recipe, ref.first.kept.recipe) && made.facts == ref.first.facts
            && json (s) == ref.snapshot, "a budget of " + std::to_string (budget) + ": the same master and the same snapshot JSON");
    }
}

// Work that stops publishes nothing: a master waiting for the measurement, cancelled; the needles of a target left
// before they end.
void nothingStale (const Reference& ref)
{
    felitronics::test::group ("a cancelled waiting master and a stale needles job publish nothing; the scenario's master follows unchanged");
    const Mix mix;
    auto s = Session::create().session;
    ok (s->apply (command::SetTarget { 1, "cd" }).rejection == Rejection::None && s->apply (mix.load (2)).rejection == Rejection::None,
        "PRECONDITION: the mix loads on cd, whose glue reads the tempo");
    for (unsigned i = 0; i < 1000000u && s->state() == State::Loaded; ++i) (void) s->step (1);
    const auto waiting = s->apply (command::Master { 3 });
    ok (waiting.rejection == Rejection::None && s->job() == waiting.job && s->state() == State::Measured1
        && s->snapshot().view().plan.awaited == Analyzer::Tempo, "PRECONDITION: a master waits for the tempo");
    ok (s->apply (command::Cancel { 4, waiting.job }).rejection == Rejection::None && s->job() == 0 && s->masters().empty()
        && s->pendingMaster().master == 0, "cancelled: no job, nothing kept, nothing to copy");
    Seen after;
    ok (pump (*s, kLarge, after) && s->state() == State::Measured2 && ! after.from (waiting.job) && s->masters().empty(),
        "the measurement ends, and the cancelled master published nothing more");
    ok (s->apply (command::SetTarget { 5, "club" }).rejection == Rejection::None && s->needlesJob() != 0, "PRECONDITION: club's needles run");
    const auto stale = s->needlesJob();
    (void) s->step (kLarge);
    ok (s->apply (command::SetTarget { 6, "allStreaming" }).rejection == Rejection::None && s->needlesJob() != stale
        && s->apply (command::Cancel { 7, stale }).rejection == Rejection::UnknownJob,
        "the project leaves club before they end: its needles are stale, not even cancellable");
    Seen later;
    ok (pump (*s, kLarge, later) && ! later.from (stale), "and they publish nothing");
    ok (s->apply (command::ImportProject { 8, ref.file }).rejection == Rejection::None && pump (*s, kLarge) && s->snapshot().view().needlesCeilingDb
        && ref.needlesCeilingDb && *s->snapshot().view().needlesCeilingDb == *ref.needlesCeilingDb,
        "the file imports; the needles are measured at its ceiling, as in the scenario");
    const auto made = master (*s, 9, kLarge);
    ok (sameSound (made, ref.first) && sameRecipe (made.kept.recipe, ref.first.kept.recipe) && made.facts == ref.first.facts,
        "the master after both is the scenario's: PCM, WAV, recipe and facts");
    ok (json (*s) == ref.snapshot, "and the snapshot JSON is the scenario's");
}
} // namespace

int main()
{
    const auto reference = theScenario();
    theFilesMachine (reference);
    theSlicing (reference);
    nothingStale (reference);
    return felitronics::test::report();
}
