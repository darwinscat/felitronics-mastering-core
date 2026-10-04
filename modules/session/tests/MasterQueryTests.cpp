// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// WHAT A SHELL READS OF A MASTER WITHOUT DECIDING ANYTHING. The landing's miss and its hints published as facts keyed to
// the master; the master's own loudness curves and its waveform in the source's four axes, each held to the SAME
// instrument run over the delivered audio as a source; the low-end spectrum as density (as it was) or as energy; and the
// lean summary — every master's scalars, pass log and sections, its heavy rows left to a MasterReport query that gives
// one master whole, to the bit of the full snapshot — with its bytes measured and its memory declared.

#include "../../../tests/DeclaredBudget.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <felitronics/session/Text.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include "PreviousSnapshotBytes.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace felitronics::session;
using namespace felitronics::session::previous;
using felitronics::test::ok;
namespace declared = felitronics::declared;

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
struct Mastered { MasterId id = 0; std::vector<float> audio; MasterAudioShape shape {}; std::vector<Notification> facts; Checked declared {}; declared::Spent spent {}; };
// glue: the compressor in the chain and compressing (a low threshold, a firm ratio); otherwise out of it. shaper: the
// soft clipper (the saturation) in the chain with those parameters; otherwise out of it.
using ShaperParams = decltype (felitronics::mastering::MasteringChainParams::clipper);
Mastered master (Session& s, CommandId id, bool glue = false, const ShaperParams* shaper = nullptr, bool limiter = true)
{
    command::Master request { id };
    request.ready.version = 1;
    request.ready.topology.eq = request.ready.topology.compressor = request.ready.topology.clipper = request.ready.topology.dither = false;
    request.ready.topology.limiter = limiter;
    if (glue)
    {
        request.ready.topology.compressor = true; request.ready.params.bypassCompressor = false;
        request.ready.params.compressor.thresholdDb = -30.0; request.ready.params.compressor.ratio = 4.0;
    }
    if (shaper)
    {
        request.ready.topology.clipper = true; request.ready.params.bypassClipper = false;
        request.ready.params.clipper = *shaper;
    }
    request.source = s.source().hash; request.revision = s.revision();
    Mastered out;
    out.declared = s.check (request);
    Answer started;
    out.spent = declared::spend ([&] { started = s.apply (request); });
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
    const auto spent = declared::spend ([&] { out = s.query (q); });
    covered = covered && declared::covers (need.bytes, spent) && (spent.requests == 0 || need.largestBlockBytes != 0);
    return out;
}

//==============================================================================

void theLandingsFacts()
{
    felitronics::test::group ("(c) the landing's miss and its verdict are facts the core publishes, keyed to the master");
    const Audio audio (5);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate); auto& s = *sp;
    ok (s.apply (command::EditTarget { 3, { -5.0, -6.0 } }).rejection == Rejection::None, "PRECONDITION: a target this mix cannot reach: −5 LUFS under −6 dBTP");
    const auto made = master (s, 4);
    const auto view = s.snapshot();
    const auto& report = *kept (view.view(), made.id)->report;
    const auto miss = MasterReportText::miss (report);
    ok (miss.has_value(), "PRECONDITION: the master missed");
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
    // Held by the limiter's budget (owner, 04.10): nothing in the mix is blamed — no hint is stated or published.
    bool hinted = report.firstHint || report.secondHint;
    for (const auto& e : made.facts)
    {
        const auto id = std::uint16_t (e.payload.fact.view().id);
        hinted = hinted || (e.jobId == made.id && id >= 12 && id <= 17);
    }
    ok (! hinted, "and no hint blames the mix");
    std::size_t before = 0, first = made.facts.size();
    for (std::size_t i = 0; i < made.facts.size(); ++i)
    {
        const auto id = made.facts[i].payload.fact.view().id;
        if (miss && id == miss->id) first = std::min (first, i);
        if (id == text::FactId::MasterCostShape && first == made.facts.size()) ++before;
    }
    ok (before == 0, "the landing's line comes ahead of the cost's");

    // The tolerance is the shipped engine's landing.toleranceLu (LandingPlanTests pins it).
    constexpr double kTolerance = 0.1;
    // The verdict: held by the limiter's budget, the target, the level landed and the budget — 7.5 dB for a target louder
    // than −8 LUFS (engine.toml [landing] limiterBudget), the level landed no louder than the target.
    const auto& summary = *kept (view.view(), made.id)->landing;
    const auto both = [] (const text::Fact& f) { return text::Text::text (f, text::Lang::Ru) + " / " + text::Text::text (f, text::Lang::En); };
    std::optional<text::Fact> verdict;
    for (const auto& e : made.facts)
        if (e.jobId == made.id && e.payload.fact.view().id == text::FactId::MasterLandingBudget) verdict = e.payload.fact.view();
    double lowestOver = std::numeric_limits<double>::infinity();
    for (const auto& pass : summary.log)
        if (pass.overBudget) lowestOver = std::fmin (lowestOver, pass.gainDb - pass.ceilingDbTp);
    const double delivered = summary.log.empty() ? 0.0 : summary.log.back().gainDb - summary.log.back().ceilingDbTp;
    ok (! summary.log.empty() && lowestOver > delivered && lowestOver - delivered <= 0.25,
        "the budget is named on its proof: the lowest render the pass log marks over it stands within 0.25 dB above the one "
        "delivered");
    ok (summary.status == LandingStatus::TargetUnreachable && summary.binding == LandingConstraint::LimiterGainReduction
        && verdict && verdict->argCount == 3 && same (verdict->args[0].number, -5.0) && verdict->args[1].number <= -5.0
        && same (verdict->args[2].number, 7.5) && verdict->args[2].unit == text::Unit::Db && verdict->args[2].precision == 1
        && both (*verdict).find ('{') == std::string::npos,
        "the miss's verdict is published with the master, with its numbers: " + (verdict ? both (*verdict) : std::string {}));

    // The search's own miss keeps its hints: without the limiter no budget can hold the landing, and −5 LUFS under −6 dBTP
    // is missed on the true peak — the mix's reasons are said beside it.
    auto unlimitedSession = loaded (audio.planes, 2, audio.frames, audio.rate);
    ok (unlimitedSession->apply (command::EditTarget { 3, { -5.0, -6.0 } }).rejection == Rejection::None,
        "PRECONDITION: the same target, for a chain without the limiter");
    const auto unlimited = master (*unlimitedSession, 4, false, nullptr, false);
    const auto unlimitedView = unlimitedSession->snapshot();
    const auto& unlimitedReport = *kept (unlimitedView.view(), unlimited.id)->report;
    bool unlimitedHints = (bool) unlimitedReport.firstHint;
    std::string unlimitedSaid;
    for (const auto* hint : { &unlimitedReport.firstHint, &unlimitedReport.secondHint })
        if (*hint)
        {
            const auto fact = MasterReportText::hint (**hint);
            bool out = false;
            for (const auto& e : unlimited.facts)
                out = out || (fact && e.jobId == unlimited.id && e.payload.fact.view().id == fact->id);
            unlimitedHints = unlimitedHints && fact && out;
            if (fact) unlimitedSaid += text::Text::text (*fact, text::Lang::Ru) + " ";
        }
    ok (kept (unlimitedView.view(), unlimited.id)->landing->binding != LandingConstraint::LimiterGainReduction
        && ! unlimitedReport.targetMet && unlimitedHints,
        "a miss the budget did not hold publishes its hints: " + unlimitedSaid);

    auto met = loaded (audio.planes, 2, audio.frames, audio.rate);
    const auto landed = master (*met, 3);
    bool silent = true;
    for (const auto& e : landed.facts)
    {
        const auto id = std::uint16_t (e.payload.fact.view().id);
        silent = silent && id != 11 && id != 23 && ! (id >= 12 && id <= 17);
    }
    const auto metView = met->snapshot();
    const auto& metReport = *kept (metView.view(), landed.id)->report;
    ok (metReport.targetMet && silent, "a master that lands says neither");
    const auto in = [] (const Mastered& m, const text::Fact& fact)
    {
        unsigned n = 0;
        for (const auto& e : m.facts)
        {
            const auto& f = e.payload.fact.view();
            bool equal = e.jobId == m.id && f.id == fact.id && f.argCount == fact.argCount;
            for (std::size_t i = 0; equal && i < f.argCount; ++i)
                equal = same (f.args[i].number, fact.args[i].number) && f.args[i].unit == fact.args[i].unit;
            n += equal ? 1u : 0u;
        }
        return n;
    };
    // The level on the source's gate and the file's reading, said where they part by more than the tolerance — either way.
    {
        LandingMeasure apart;
        const double file = metReport.achievedLufs.value_or (0.0);
        bool both = true;
        for (const double gate : { file - 0.5, file + 0.5 })
        {
            apart.gateLufs = gate; apart.landedLufs = std::fmax (gate, file);
            const auto said = MasterReportText::gate (metReport, apart, kTolerance);
            both = both && said && said->id == text::FactId::MasterLandingGate && same (said->args[0].number, gate)
                && same (said->args[1].number, file);
        }
        apart.gateLufs = file + 0.05;
        ok (metReport.achievedLufs && both && ! MasterReportText::gate (metReport, apart, kTolerance),
            "the gate's level and the file's are both said when they part by more than the tolerance, the file louder or quieter");
    }
    const auto solved = MasterReportText::landing (metReport, *kept (metView.view(), landed.id)->landing, kTolerance);
    ok (solved && solved->id == text::FactId::MasterLandingSolved && metReport.achievedLufs
        && same (solved->args[0].number, *metReport.achievedLufs) && same (solved->args[1].number, metReport.targetLufs)
        && same (solved->args[2].number, kTolerance) && in (landed, *solved) == 1,
        "a master that lands says so with its numbers — achieved, target, tolerance: " + (solved ? both (*solved) : std::string {}));

    // The crest joined inside the job (the source measured whole before the master): its line comes with the report,
    // once, from MasterReportText::crest.
    unsigned crestLines = 0;
    for (const auto& e : landed.facts)
    {
        const auto id = e.payload.fact.view().id;
        crestLines += e.jobId == landed.id && (id == text::FactId::MasterCrestSourceRate || id == text::FactId::MasterCrestPending
            || id == text::FactId::MasterCrestUnavailable || id == text::FactId::MasterCrestDelivered) ? 1u : 0u;
    }
    const auto crestLine = MasterReportText::crest (metReport.crest);
    ok (metReport.crest.status == MeasurementStatus::Ready && crestLines == 1 && in (landed, crestLine) == 1,
        "a crest ready before the master ends: its line comes with the report, once — " + both (crestLine));
}

void theVerdictPerStatus()
{
    felitronics::test::group ("(c2) the landing's verdict: one fact per status, with its numbers");
    MasterReport r;
    r.status = MeasurementStatus::Ready; r.targetLufs = -14.0; r.achievedLufs = -14.04; r.missLu = -0.04;
    const auto whole = [] (const text::Fact& f)
    {
        const auto a = text::Text::text (f, text::Lang::Ru), b = text::Text::text (f, text::Lang::En);
        return a.find ('{') == std::string::npos && b.find ('{') == std::string::npos && a.size() > 20 && b.size() > 20
            && a != text::Text::key (f.id) && b != text::Text::key (f.id);
    };
    const auto at = [] (LandingStatus status)
    {
        LandingSummary s; s.status = status;
        if (status == LandingStatus::TargetBetweenAchievable) { s.belowLufs = -14.46; s.aboveLufs = -13.52; }
        return s;
    };
    const auto solved = MasterReportText::landing (r, at (LandingStatus::Solved), 0.1);
    ok (solved && solved->id == text::FactId::MasterLandingSolved && solved->argCount == 3
        && text::Text::text (*solved, text::Lang::Ru).find ("(допуск ±0,1") != std::string::npos
        && same (solved->args[0].number, -14.04) && solved->args[0].unit == text::Unit::Lufs
        && same (solved->args[1].number, -14.0) && solved->args[1].unit == text::Unit::Lufs
        && same (solved->args[2].number, 0.1) && solved->args[2].unit == text::Unit::Lu && whole (*solved),
        "solved: the achieved number against the target and the tolerance — " + (solved ? text::Text::text (*solved, text::Lang::Ru) : std::string {}));
    const auto pass = MasterReportText::landing (r, at (LandingStatus::PassLimit), 0.1);
    ok (pass && pass->id == text::FactId::MasterLandingPassLimit && pass->argCount == 1 && same (pass->args[0].number, 0.1)
        && pass->args[0].unit == text::Unit::Lu && whole (*pass), "pass limit: against the tolerance");
    const auto between = MasterReportText::landing (r, at (LandingStatus::TargetBetweenAchievable), 0.1);
    LandingSummary bare; bare.status = LandingStatus::TargetBetweenAchievable;
    ok (between && between->id == text::FactId::MasterLandingBetween && between->argCount == 2
        && same (between->args[0].number, -14.46) && same (between->args[1].number, -13.52) && whole (*between)
        && text::Text::text (*between, text::Lang::Ru).find ("ближайшие уровни \xE2\x88\x92" "14,5") != std::string::npos
        && ! MasterReportText::landing (r, bare, 0.1),
        "between: the two nearest levels, the summary's — and no line without them: "
            + (between ? text::Text::text (*between, text::Lang::Ru) : std::string {}));
    // Unreachable: the limit the solver named, carried by the summary — each its own words; none named, the general line.
    bool named = true; std::string said; std::string previous;
    for (const auto& [binding, term] : { std::pair { LandingConstraint::TruePeakCeiling, text::Term::LandingLimitTruePeak },
                                         std::pair { LandingConstraint::LimiterGainReduction, text::Term::LandingLimitLimiter },
                                         std::pair { LandingConstraint::PeakToLoudness, text::Term::LandingLimitPlr },
                                         std::pair { LandingConstraint::LoudnessRange, text::Term::LandingLimitLra },
                                         std::pair { LandingConstraint::GainRange, text::Term::LandingLimitGain },
                                         std::pair { LandingConstraint::None, text::Term::LandingLimitNone } })
    {
        auto s = at (LandingStatus::TargetUnreachable); s.binding = binding;
        const auto f = MasterReportText::landing (r, s, 0.1);
        const auto line = f ? text::Text::text (*f, text::Lang::Ru) : std::string {};
        named = named && f && f->id == text::FactId::MasterLandingUnreachable && f->argCount == 2 && same (f->args[0].number, 0.1)
            && f->args[1].kind == text::ArgKind::Term && f->args[1].termId == term && whole (*f) && line != previous;
        previous = line; said += "\n        " + line;
    }
    ok (named, "unreachable: the limit that held it, each its own line" + said);
    const auto failed = MasterReportText::landing (MasterReport {}, at (LandingStatus::TechnicalFailure), 0.1);
    ok (failed && failed->id == text::FactId::MasterLandingFailed && failed->argCount == 0 && whole (*failed),
        "a technical failure says so: " + (failed ? text::Text::text (*failed, text::Lang::Ru) : std::string {}));
    MasterReport unmeasured; unmeasured.targetLufs = -14.0;
    ok (! MasterReportText::landing (r, at (LandingStatus::Unavailable), 0.1) && ! MasterReportText::landing (r, at (LandingStatus::Cancelled), 0.1)
        && ! MasterReportText::landing (unmeasured, at (LandingStatus::Solved), 0.1),
        "an unavailable or cancelled landing says no verdict, nor a solved one without a measured loudness");
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

void theMastersLoudnessOnTheSourceGrid()
{
    felitronics::test::group ("(d2) a converted master's curves in the source's frames: the source's request with a master id lies under it");
    // 44.1 kHz in, 48 kHz delivered (youtube): the delivered frames are not the source's.
    const Audio audio (8, 44100);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate, false, "youtube"); auto& s = *sp;
    const auto made = master (s, 3);
    ok (made.shape.sampleRate == 48000 && made.shape.frames == 384000u, "PRECONDITION: delivered at 48 kHz, 384000 frames");
    const float* planes[] { made.audio.data(), made.audio.data() + made.shape.frames };
    auto op = loaded (planes, made.shape.channels, made.shape.frames, made.shape.sampleRate); auto& oracle = *op;
    bool covered = true;
    for (const auto kind : { QueryKind::Momentary, QueryKind::ShortTerm })
    {
        const char* name = kind == QueryKind::Momentary ? "momentary" : "short-term";
        const auto mine = answered (s, ask (kind, s, 0, audio.frames, 2048, made.id), covered);
        const auto source = s.query (ask (kind, s, 0, audio.frames, 2048));
        const auto theirs = oracle.query (ask (kind, oracle, 0, made.shape.frames, 2048));
        const auto& v = mine.view(); const auto& o = theirs.view(); const auto& src = source.view();
        bool frames = v.stored == src.stored && v.stored > 0, readings = v.stored <= o.stored;
        for (std::uint64_t i = 0; frames && i < v.stored; ++i)
            frames = same (v.values[std::size_t (3u * i)], src.values[std::size_t (3u * i)]);
        for (std::uint64_t i = 0; readings && i < v.stored; ++i)
            readings = same (v.values[std::size_t (3u * i + 1u)], o.values[std::size_t (3u * i + 1u)])
                && same (v.values[std::size_t (3u * i + 2u)], o.values[std::size_t (3u * i + 2u)])
                && same (v.values[std::size_t (3u * i)] * 48000.0, o.values[std::size_t (3u * i)] * 44100.0);
        ok (v.status == QueryStatus::Ready && v.complete && v.sampleRate == 44100 && frames,
            std::string (name) + ": " + std::to_string (v.stored) + " rows named by the source's frames, row for row the source's own rows, the rate the source's");
        ok (readings, std::string (name) + ": each reading the delivered audio's own measured as a source, renamed into the source's frame of the same moment");
        const auto part = answered (s, ask (kind, s, 44100, 220500, 16, made.id), covered);
        const auto partSource = s.query (ask (kind, s, 44100, 220500, 16));
        bool partFrames = part.view().stored == 16 && part.view().total == partSource.view().total && partSource.view().stored == 16;
        for (std::uint64_t i = 0; partFrames && i < 16; ++i)
            partFrames = same (part.view().values[std::size_t (3u * i)], partSource.view().values[std::size_t (3u * i)]);
        ok (part.view().status == QueryStatus::Ready && partFrames, std::string (name) + ": a part of it, thinned to 16 columns, at the source's own picks");
    }
    ok (covered, "each answer inside its declared memory");
    ok (s.query (ask (QueryKind::ShortTerm, s, 0, audio.frames + 1u, 64, made.id)).view().status == QueryStatus::InvalidRange,
        "past the source's end is refused, though the delivered audio is longer");
}

// The glue's gain reduction, per bucket on the limiter's grid, from the delivered render.
void theGluesGainReduction()
{
    felitronics::test::group ("(d3) GlueGr: the compressor's gain reduction over time, on LimiterGr's grid");
    const Audio audio (8);
    auto sp = loaded (audio.planes, 2, audio.frames, audio.rate); auto& s = *sp;
    const auto glued = master (s, 3, true);
    const auto plain = master (s, 4);
    ok (declared::covers (glued.declared.bytes, glued.spent), "the glued master's retained trace inside its declared memory");
    bool covered = true;
    const auto gr = answered (s, ask (QueryKind::GlueGr, s, 0, glued.shape.frames, 64, glued.id), covered);
    const auto lim = s.query (ask (QueryKind::LimiterGr, s, 0, glued.shape.frames, 64, glued.id));
    const auto& v = gr.view(); const auto& l = lim.view();
    bool grid = v.stored == l.stored && v.total == l.total && v.stored == 64;
    double largest = 0;
    for (std::uint64_t i = 0; grid && i < v.stored; ++i)
    {
        grid = same (v.values[std::size_t (7u * i)], l.values[std::size_t (7u * i)])
            && same (v.values[std::size_t (7u * i + 1u)], l.values[std::size_t (7u * i + 1u)]);
        largest = std::max (largest, v.values[std::size_t (7u * i + 3u)]);
    }
    ok (v.status == QueryStatus::Ready && v.stride == 7 && v.reason == MeasurementReason::None && v.measurementKey == glued.id
        && v.sampleRate == glued.shape.sampleRate && grid, "64 columns, bucket for bucket the limiter's bounds");
    const auto report = s.query (ask (QueryKind::MasterReport, s, 0, 0, 1, glued.id));
    const auto& cost = report.view().master->report->cost;
    ok (cost && cost->glueMaxDb.value && largest > 0 && same (largest, *cost->glueMaxDb.value),
        "its largest bucket is the report's glueMaxDb, the same tap of the same render");
    const auto zoom = answered (s, ask (QueryKind::GlueGr, s, 96000, 192000, 8, glued.id), covered);
    const auto zoomLim = s.query (ask (QueryKind::LimiterGr, s, 96000, 192000, 8, glued.id));
    ok (zoom.view().status == QueryStatus::Ready && zoom.view().total == zoomLim.view().total && zoom.view().stored == 8,
        "a zoom selects the same buckets the limiter's does");
    ok (covered, "each answer inside its declared memory");
    const auto none = s.query (ask (QueryKind::GlueGr, s, 0, plain.shape.frames, 64, plain.id));
    ok (none.view().status == QueryStatus::Unavailable && none.view().reason == MeasurementReason::NoSignal && none.view().values.empty(),
        "a master without a compressing glue: Unavailable, NoSignal, no rows");
    ok (s.query (ask (QueryKind::GlueGr, s, 0, glued.shape.frames, 64, glued.id + 9u)).view().reason == MeasurementReason::Unsupported,
        "no such master: Unavailable, Unsupported, as every master kind");
}

// What the saturation took off the peaks, per bucket on the limiter's grid, from the delivered render: a 1 kHz sine,
// -60 dBFS for two seconds, then -6 dBFS for two, through a tanh at 18 dB of drive, half drive compensation, 80 % wet,
// 2 dB down at its output.
void theSaturationsShave()
{
    felitronics::test::group ("(d4) SaturationShave: what the saturation took off the peaks, on LimiterGr's grid");
    namespace det = felitronics::core::det;
    constexpr unsigned rate = 48000, frames = 4 * rate;
    constexpr double quiet = 0.001, hot = 0.5, driveDb = 18.0, wet = 0.8;
    std::vector<float> tone (frames);
    // 48 samples a period, a sample on every crest: the input's peak in a quantum is the amplitude itself.
    for (unsigned i = 0; i < frames; ++i) tone[i] = float ((i < frames / 2 ? quiet : hot) * det::sin (2 * kPi * 1000.0 * i / rate));
    const float* planes[] { tone.data(), tone.data() };
    auto sp = loaded (planes, 2, frames, rate); auto& s = *sp;
    ShaperParams tanh {};
    tanh.shape = felitronics::saturation::WaveShaper::Shape::Tanh;
    tanh.driveDb = float (driveDb); tanh.mix = float (wet); tanh.outputDb = -2.0f; tanh.autoComp = 0.5f;
    ShaperParams tape = tanh;
    tape.shape = felitronics::saturation::WaveShaper::Shape::Tape;
    const auto shaped = master (s, 3, false, &tanh);
    const auto taped = master (s, 4, false, &tape);
    const auto plain = master (s, 5);
    ok (declared::covers (shaped.declared.bytes, shaped.spent), "the shaped master's retained trace inside its declared memory");
    bool covered = true;
    constexpr std::uint32_t columns = 64;
    const auto shave = answered (s, ask (QueryKind::SaturationShave, s, 0, shaped.shape.frames, columns, shaped.id), covered);
    const auto lim = s.query (ask (QueryKind::LimiterGr, s, 0, shaped.shape.frames, columns, shaped.id));
    const auto& v = shave.view(); const auto& l = lim.view();
    bool grid = v.stored == l.stored && v.total == l.total && v.stored == columns;
    for (std::uint64_t i = 0; grid && i < v.stored; ++i)
        grid = same (v.values[std::size_t (7u * i)], l.values[std::size_t (7u * i)])
            && same (v.values[std::size_t (7u * i + 1u)], l.values[std::size_t (7u * i + 1u)]);
    ok (v.status == QueryStatus::Ready && v.stride == 7 && v.reason == MeasurementReason::None && v.measurementKey == shaped.id
        && v.sampleRate == shaped.shape.sampleRate && grid, "64 columns, bucket for bucket the limiter's bounds");
    // THE CURVATURE'S LOSS, by hand from the parameters: the shape is tanh(k·x)/tanh(k) with k = 10^(drive/20) − 1, its
    // slope at zero k/tanh(k), the drive compensation that slope^−0.5. A sound too small to bend gets
    // (1 − wet) + wet·comp·slope of itself, the crest A gets (1 − wet)·A + wet·comp·tanh(k·A)/tanh(k) (the trim on both
    // sides). A is the hot amplitude brought to the input's reference loudness first ([input] referenceLufs, -18), as
    // every stage hears the source.
    const double k = std::pow (10.0, driveDb / 20.0) - 1.0, slope = k / std::tanh (k), comp = 1.0 / std::sqrt (slope);
    const double crest = hot * std::pow (10.0, (-18.0 - s.snapshot().view().integratedLufs) / 20.0);
    const double expected = 20.0 * std::log10 (((1.0 - wet) + wet * comp * slope) * crest
                                               / ((1.0 - wet) * crest + wet * comp * std::tanh (k * crest) / std::tanh (k)));
    // Buckets wholly inside each half; the two at the step and the first are left out (a quantum straddles the step).
    const auto range = [] (const QueryView& r, std::uint64_t from, std::uint64_t to, std::size_t column, bool largest)
    {
        if (r.values.size() < std::size_t (7u * to)) return std::numeric_limits<double>::quiet_NaN();   // no rows: fails
        double x = largest ? 0.0 : 1e9;
        for (std::uint64_t i = from; i < to; ++i)
        {
            const double y = r.values[std::size_t (7u * i + column)];
            x = largest ? std::max (x, y) : std::min (x, y);
        }
        return x;
    };
    const double hotLeast = range (v, columns / 2 + 1, columns, 2, false), hotMost = range (v, columns / 2 + 1, columns, 3, true);
    const double quietMost = range (v, 1, columns / 2 - 1, 3, true);
    std::printf ("    tanh: crest %.4f, expected %.4f dB, hot buckets %.4f..%.4f dB; below the knee at most %.6f dB\n",
                 crest, expected, hotLeast, hotMost, quietMost);
    ok (expected > 1.0 && std::fabs (hotLeast - expected) < 0.01 && std::fabs (hotMost - expected) < 0.01,
        "a hot sine through a driven tanh: every hot bucket within 0.01 dB of the curvature's loss");
    ok (quietMost < 0.01, "the same sine 54 dB lower, under the knee: under 0.01 dB — the trim and the dry share are no shave");
    const auto report = s.query (ask (QueryKind::MasterReport, s, 0, 0, 1, shaped.id));
    const auto& cost = report.view().master->report->cost;
    const double largest = range (v, 0, columns, 3, true);
    ok (cost && cost->saturationCutMaxDb.value && std::fabs (largest - *cost->saturationCutMaxDb.value) < 1e-4,
        "its largest bucket is the report's saturationCutMaxDb, the same quanta of the same render");
    const auto tapeShave = answered (s, ask (QueryKind::SaturationShave, s, 0, taped.shape.frames, columns, taped.id), covered);
    const double tapeQuiet = range (tapeShave.view(), 1, columns / 2 - 1, 3, true);
    const double tapeHot = range (tapeShave.view(), columns / 2 + 1, columns, 2, false);
    std::printf ("    tape: hot buckets at least %.4f dB; below the knee at most %.6f dB\n", tapeHot, tapeQuiet);
    ok (tapeShave.view().status == QueryStatus::Ready && tapeHot > 0.5 && tapeQuiet < 0.01,
        "tape, its emphasis filters around the bend: the hot half shaved, the quiet half under 0.01 dB");
    const auto zoom = answered (s, ask (QueryKind::SaturationShave, s, 96000, 192000, 8, shaped.id), covered);
    const auto zoomLim = s.query (ask (QueryKind::LimiterGr, s, 96000, 192000, 8, shaped.id));
    ok (zoom.view().status == QueryStatus::Ready && zoom.view().total == zoomLim.view().total && zoom.view().stored == 8,
        "a zoom selects the same buckets the limiter's does");
    ok (covered, "each answer inside its declared memory");
    const auto none = s.query (ask (QueryKind::SaturationShave, s, 0, plain.shape.frames, columns, plain.id));
    ok (none.view().status == QueryStatus::Unavailable && none.view().reason == MeasurementReason::NoSignal && none.view().values.empty(),
        "a master without the saturation: Unavailable, NoSignal, no rows");
    ok (s.query (ask (QueryKind::SaturationShave, s, 0, shaped.shape.frames, columns, shaped.id + 9u)).view().reason
        == MeasurementReason::Unsupported, "no such master: Unavailable, Unsupported, as every master kind");
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
    const auto spent = declared::spend ([&] { chunk = s.masterWaveformChunk (chunkQuery, head); });
    const auto chunkOracle = oracle.query (ask (QueryKind::Waveform, oracle, 0, 4096, 16));
    bool zoom = need.status == QueryStatus::Ready && chunk.view().status == QueryStatus::Ready && chunk.view().stored == 64 && chunk.view().stride == kWaveformStride
        && declared::covers (need.bytes, spent);
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
        declared = declared && declared::covers (a.declared.bytes, a.spent) && declared::covers (b.declared.bytes, b.spent) && b.spent.bytes > a.spent.bytes;
        const auto before = previousSummary (*full), now = summaryOf (*full);
        identical = identical && sameBytes (before, now);
        const auto spent = declared::spend ([&] { (void) Wire::summaryBytes (*lean); });
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
        const auto asked = declared::spend ([&] { (void) plain->apply (command::Master { 5 }); });
        const auto spent = declared::spend ([&] { started = thin->apply (command::Master { 5 }); });
        ok (a.rejection == Rejection::None && b.rejection == Rejection::None && b.bytes == a.bytes + sizeof (Kept) && a.bytes >= sizeof (Kept)
            && started.rejection == Rejection::None && declared::covers (a.bytes, asked) && declared::covers (b.bytes, spent)
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
    const auto spent = declared::spend ([&] { (void) lean->query (report); });
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

// THE SNAPSHOT SIZED IN ONE PASS (slice 5): the same sizes and statuses as the previous function, copied
// (tests/PreviousSnapshotBytes.h). Its cost is felitronics_session_snapshot_sizing's, a target of its own: a clock is
// not for a unit built with the session's own flags.
void theSnapshotSizedInOnePass()
{
    const Audio audio (4);
    auto s = loaded (audio.planes, 2, audio.frames, audio.rate);
    (void) master (*s, 3);
    const auto full = s->snapshot();
    const auto summary = s->summary();
    bool same = sameNeed (Wire::snapshotBytes (full.view()), previousSnapshotBytes (full.view()))
             && sameNeed (Wire::snapshotBytes (summary.view()), previousSnapshotBytes (summary.view()))
             && sameNeed (Wire::snapshotBytes (*s), previousSnapshotBytes (full.view()))
             && Wire::snapshotBytes (full.view()).status == CodecStatus::Ok && ! full.view().masters.empty();
    ok (same, "a measured and mastered session, whole and summed up: the same sizes as the previous path");

    // What only the text pass caught is still caught: each view below is refused by both, the same way.
    const Wide wide (64);
    const MachineDifference stray[] { { Device (200), 0, 1.0, 2.0 } };
    auto badDevice = wide.view; badDevice.machineDifferences = stray;
    auto badText = wide.view; badText.target = std::string_view ("\xff\xfe", 2);
    auto badBytes = wide.view; badBytes.measurementStorage.sourceBytes = std::numeric_limits<double>::quiet_NaN();
    auto badIndex = wide.view;
    std::vector<ReadingPoint> far (wide.points); far[3].index = 9007199254740992ull;
    badIndex.momentary = far;
    auto badState = wide.view; badState.state = State (99);
    bool refused = true;
    for (const auto* v : { &badDevice, &badText, &badBytes, &badIndex, &badState })
    {
        const auto a = Wire::snapshotBytes (*v), b = previousSnapshotBytes (*v);
        refused = refused && sameNeed (a, b) && a.status == CodecStatus::Invalid;
    }
    ok (refused && sameNeed (Wire::snapshotBytes (wide.view), previousSnapshotBytes (wide.view)),
        "a stray device in a machine difference, text that is not UTF-8, a byte count that is not a number, an index past 2^53, "
        "a state out of range: Invalid, as before");
}
int main()
{
    std::printf ("felitronics::session — a master read by queries, and the lean summary\n");
    theLandingsFacts();
    theVerdictPerStatus();
    theSpectrumsQuantity();
    theMastersLoudness();
    theMastersLoudnessOnTheSourceGrid();
    theGluesGainReduction();
    theSaturationsShave();
    theMastersAxes();
    theLeanSummary();
    theSnapshotSizedInOnePass();
    return felitronics::test::report();
}
