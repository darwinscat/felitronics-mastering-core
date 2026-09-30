// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "DeclaredBudget.h"
#include "SourceMeasurements.h"
#include "MeasurementWorkspace.h"
#include "LiveMeasurements.h"
#include "Driver.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <felitronics/analysis/BandCrestResult.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <limits>
#include <vector>

using namespace felitronics::session;
using namespace felitronics::analysis;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;
struct felitronics::session::detail::Inspector
{
    static const SourceMeasurements& run (const Session& s) { return *s.sourceMeasurements_; }
    static const MeasurementWorkspace& workspace (const Session& s) { return *s.measurementWorkspace_; }
    static void missingMandatory (Session& s, bool peak)
    {
        auto& r = s.measurementResults_[0];
        auto& live = *s.liveMeasurements_;
        for (auto& n : live.numbers[0]) if (n.name == (peak ? "truePeakDb" : "integratedLufs"))
        { n.value.reset(); n.reason = MeasurementReason::NoSignal; }
        r.numbers = { live.numbers[0], live.numberCount[0] };
    }
    static unsigned live (const Session& s) { return s.liveMeasurements_->stage; }
    static void place (Session& s) { s.devicesPlaced_ = true; }
    static void tempoOutcome (Session& s, MeasurementStatus status, MeasurementReason reason,
                              std::span<const MeasurementValue> numbers)
    {
        auto& result = s.measurementResults_[std::size_t (Analyzer::Tempo)];
        result.status = status; result.reason = reason; result.numbers = numbers;
    }
};
std::uint64_t digest = 0xCBF29CE484222325ull;
bool same (double a, double b)
{
    const auto bits = std::bit_cast<std::uint64_t> (a);
    for (unsigned i = 0; i < 8; ++i) digest = (digest ^ std::uint8_t (bits >> (i * 8))) * 0x100000001B3ull;
    return bits == std::bit_cast<std::uint64_t> (b);
}
const MeasurementArray& array (const MeasurementResult& r, std::string_view name)
{
    for (const auto& a : r.arrays) if (a.name == name) return a;
    std::fprintf (stderr, "missing array %.*s\n", int (name.size()), name.data()); std::abort();
}
const MeasurementValue& value (const MeasurementResult& r, std::string_view name)
{
    for (const auto& v : r.numbers) if (v.name == name) return v;
    std::fprintf (stderr, "missing value %.*s\n", int (name.size()), name.data()); std::abort();
}
void drive (Session& s, bool cancel = false)
{
    unsigned previous = 99, stage = 99;
    while (s.measurementJob() || s.needlesJob())
    {
        if (cancel && s.measurementJob() && detail::Inspector::live (s) == 9)
        {
            const auto& run = detail::Inspector::run (s);
            if (run.cursor != previous || run.stage != stage)
            {
                previous = run.cursor; stage = run.stage;
                const auto frames = run.frames, source = s.source().hash;
                const auto before = s.snapshot();
                const auto stopped = budget::spend ([&] { (void) s.apply (command::Cancel { 7, s.measurementJob() }); });
                ok (stopped.bytes == 0 && s.source().hash == source && run.frames == frames, "cancel retains PCM, results and the exact unfinished position without allocation");
                const auto resumed = budget::spend ([&] { (void) s.apply (command::ContinueMeasurement { 8 }); });
                ok (resumed.bytes == 0 && run.frames == frames, "continue allocates nothing and resumes the saved stage");
                const auto after = s.snapshot();
                for (unsigned i = 0; i < kAnalyzers; ++i)
                    if (before.view().measurements[i].status == MeasurementStatus::Ready)
                        ok (after.view().measurements[i].framesRead == before.view().measurements[i].framesRead, "ready analyzers are never restarted");
            }
        }
        const auto& run = detail::Inspector::run (s);
        const bool preparation = s.needlesJob() == 0 && detail::Inspector::live (s) == 9 && run.stage == 0;
        const auto resident = s.liveBytes();
        const auto spent = budget::spend ([&] { (void) s.step (1); });
        if (preparation && spent.bytes != 0)
            ok (s.liveBytes() - resident >= double (spent.bytes), "each optional preparation retains allocator allowance, including MSVC Debug");
        for (const auto& e : s.events()) if (e.kind == EventKind::Fact && e.payload.fact.view().id == text::FactId::AnalyzerStatus)
            for (const auto lang : { text::Lang::Ru, text::Lang::En })
            {
                const auto rendered = text::Text::text (e.payload.fact.view(), lang);
                ok (rendered.size() > 16 && rendered.find ('{') == rendered.npos, "typed analyzer outcome renders a complete ru/en status");
            }
    }
}
void fixture (unsigned channels, unsigned rate, unsigned frames, int piece, bool cancel)
{
    std::vector<float> left (frames), right (frames);
    for (unsigned i = 0; i < frames; ++i)
    {
        const double t = double (i) / rate;
        const double mid = .08 * felitronics::core::det::sin (t * 6.283185307179586 * 73)
                         + .001 * felitronics::core::det::sin (t * 6.283185307179586 * (50 + .05 * t));
        const double side = .04 * felitronics::core::det::sin (t * 6.283185307179586 * 13);
        const double burst = i % (rate / 2) < rate / 20 ? .08 * felitronics::core::det::sin (t * 6.283185307179586 * 6500) : 0;
        left[i] = float (mid + side + burst); right[i] = float (mid - side);
    }
    const float* planes[] { left.data(), right.data() };
    const Pcm pcm { planes, channels, frames, rate };
    auto made = Session::create(); auto& s = *made.session;
    ok (s.apply (command::Load { 1, pcm, {} }).rejection == Rejection::None, "source load accepted");
    const auto oldJob = s.measurementJob();
    drive (s, cancel);
    auto snapshot = s.snapshot();
    ok (OwnedMeasurements::valid (snapshot.view().measurements), "all source results satisfy the wire contract");
    for (const auto& r : snapshot.view().measurements)
    {
        for (const auto& n : r.numbers) if (n.value) (void) same (*n.value, *n.value);
        for (const auto& a : r.arrays) for (const auto v : a.values) (void) same (v, v);
    }
    ok (s.state() == (frames >= rate * 4 / 10 ? State::Measured2 : State::Loaded), "phase two needs usable loudness and true peak plus terminal optional statuses");
    const bool measured = s.state() == State::Measured2;
    ok (snapshot.view().devicesPlaced == measured
        && snapshot.view().plan.status == (measured ? PlanStatus::Ready : PlanStatus::Unavailable),
        "the planner places the devices when the first measurement ends; without a usable loudness there is no plan");
    ok (s.check (command::EditDevice { 2, HpfFields<Touched> {} }).rejection == (measured ? Rejection::None : Rejection::NotPlaced),
        "device edits are taken once the plan is ready, and not before");
    if (s.state() == State::Measured2) ok (s.check (command::Master { 3 }).rejection == Rejection::None, "Master remains available after optional outcomes");
    else ok (s.check (command::Master { 3 }).rejection == Rejection::NotMeasured, "missing mandatory readings block Master");
    const auto plan = detail::MeasurementPlan::storageFor (pcm, detail::MeasurementPlan::parametersFor (pcm));
    for (const auto id : detail::SourceMeasurements::order)
    {
        const auto& result = snapshot.view().measurements[std::size_t (id)];
        if (! plan.analyzers[std::size_t (id)].available)
        { ok (result.status == MeasurementStatus::Unavailable && result.reason == MeasurementReason::Unsupported, "impossible optional band has a visible reason"); continue; }
        detail::MeasurementWorkspace w;
        ok (w.prepare (id, pcm, plan), "direct analyzer prepared with identical source geometry");
        for (unsigned start = 0; start < frames;)
        {
            const auto n = std::min (unsigned (piece), frames - start); const float* p[] { left.data() + start, right.data() + start };
            if (w.lowEnd) (void) w.lowEnd->process (p, int (channels), int (n));
            if (w.lowEnd150) (void) w.lowEnd150->process (p, int (channels), int (n));
            if (w.infraLow) (void) w.infraLow->process (p, int (channels), int (n));
            if (w.forensics) (void) w.forensics->process (p, int (channels), int (n));
            if (w.stereo) (void) w.stereo->process (p, int (channels), int (n));
            if (w.crest) (void) w.crest->process (p, int (channels), int (n));
            if (w.hum) (void) w.hum->process (p, int (channels), int (n));
            if (w.bursts) (void) w.bursts->process (p, int (channels), int (n));
            if (w.tempo) (void) w.tempo->process (p, int (channels), int (n));
            start += n;
        }
        auto* low = w.lowEnd ? w.lowEnd.get() : w.lowEnd150 ? w.lowEnd150.get() : w.infraLow.get();
        if (low)
        {
            (void) low->finish();
            ok (same (*value (result, "lowMidEnergy").value, low->lowMidEnergy()) && same (*value (result, "lowSideEnergy").value, low->lowSideEnergy()), "real LR4 energies equal the direct analyzer, including Side below 20 Hz");
            const auto& rows = array (result, "blocks");
            ok (rows.stored == std::uint64_t (low->storedBlockCount()) && rows.complete == low->blocksComplete(), "block counts and completeness survive release");
            for (unsigned i = 0; i < rows.stored; ++i)
            {
                const auto b = low->block (i); const auto* r = rows.values.data() + i * 8;
                ok (same (r[4], b.midEnergy) && same (r[5], b.sideEnergy) && r[0] == double (b.start), "every low-end block matches direct output");
            }
            const auto& bands = array (result, "bands");
            for (int i = 0; i < low->bandCount(); ++i)
            {
                const auto b = low->band (i); const auto* r = bands.values.data() + i * 16;
                ok (same (r[4], b.midEnergy) && same (r[5], b.sideEnergy) && same (r[12], low->duty (i)) && same (r[14], low->marginWhenOnDb (i)), "every spectral band retains energy, duty and margin");
            }
        }
        if (w.forensics)
        {
            auto& a = *w.forensics; (void) a.finish(); const auto& powers = array (result, "meanPower");
            for (int c = 0; c < int (channels); ++c)
            {
                for (int b = 0; b < a.bins(); ++b) ok (same (powers.values[std::size_t (b) * channels + unsigned (c)], a.meanPower (c, b)), "forensics spectrum matches per channel");
                const auto& histogram = array (result, "gridExponentHistogram");
                for (int b = 0; b < a.gridExponentBuckets(); ++b) ok (histogram.values[std::size_t (b) * channels + unsigned (c)] == double (a.gridExponentHistogram (c)[b]), "exact and robust grid evidence survives release");
            }
        }
        if (w.stereo)
        {
            const auto& a = *w.stereo; const auto& rows = array (result, "columns");
            for (int i = 0; i < a.columns(); ++i) ok (same (rows.values[std::size_t (i) * 3], a.width()[std::size_t (i)]) && same (rows.values[std::size_t (i) * 3 + 1], a.correlation()[std::size_t (i)]) && same (rows.values[std::size_t (i) * 3 + 2], a.rms()[std::size_t (i)]), "all stereo columns match direct output");
        }
        if (w.crest)
        {
            auto& a = *w.crest; (void) a.finish(); const auto view = MeasurementCrest::view (result);
            ok (view.blockCount() == std::size_t (a.blockCount()) && view.frames == frames, "detached crest has full block geometry");
            const auto floor = std::max (-70., a.programmeMeanSquareDb() - 42.);
            ok (same (view.activityFloorDb, floor), "mask uses the analyzer's own block power, never report power or LUFS");
            const auto threshold = felitronics::core::det::pow10 (floor / 10);
            const auto share = felitronics::core::det::pow10 (-4);
            for (std::size_t i = 0; i < view.blockCount(); ++i) for (unsigned b = 0; b < 5; ++b)
            {
                ok (same (view.blockPeakLin (i, b), a.blockPeakLin (static_cast<long long> (i), int (b))) && same (view.blockMeanSq (i, b), a.blockMeanSq (static_cast<long long> (i), int (b))), "all five crest peak and mean-square pairs equal direct output");
                const auto os = a.blockOversampledMeanSq (static_cast<long long> (i));
                const bool active = a.blockMeanSq (static_cast<long long> (i), 4) >= threshold && (b == 4 || (os > 0 && a.blockMeanSq (static_cast<long long> (i), int (b)) >= share * os));
                ok (view.blockActive (i, b) == active, "every owned source mask cell agrees without a second PCM pass");
            }
            ok (! detail::Inspector::workspace (s).crest, "source crest is readable after workspace release");
        }
        if (w.hum)
        {
            auto& a = *w.hum; (void) a.finish(); const auto& rows = array (result, "candidates");
            for (int c = 0; c < int (channels); ++c) for (int k = 0; k < 2; ++k)
            { const auto v = a.candidate (c, k); const auto* row = rows.values.data() + (c * 2 + k) * 36;
              ok (same (row[4], v.fundamentalHz) && same (row[13], v.stretchSpreadHz) && same (row[14], v.frameSpreadHz) && row[17] == double (v.passed), "both hum hypotheses preserve drift and confidence evidence"); }
        }
        if (w.bursts)
        {
            auto& a = *w.bursts; (void) a.finish();
            ok (*value (result, "sideAbsent").value == double (channels == 1), "mono explicitly reports absent Side");
            for (int axis = 0; axis < 2; ++axis)
            {
                const auto& rows = array (result, axis == 0 ? "midEvents" : "sideEvents");
                ok (rows.total == std::uint64_t (a.axis (axis).eventCount()) && rows.complete == a.axis (axis).eventsComplete(), "burst total/stored/completeness are honest");
                for (unsigned i = 0; i < rows.stored; ++i)
                { const auto v = a.axis (axis).event (i); const auto cross = a.crossAt (axis, i); const auto* r = rows.values.data() + i * 16;
                  ok (same (r[3], v.peakPower) && same (r[7], v.energy) && same (r[12], cross.power) && r[0] == double (v.start), "burst coordinates, dose and other-axis evidence match direct output"); }
            }
        }
        if (w.tempo)
        {
            auto& a = *w.tempo; (void) a.finish();
            ok (! detail::Inspector::workspace (s).tempo, "tempo scratch is released after owned publication");
            ok (result.status == (a.determined() ? MeasurementStatus::Ready : MeasurementStatus::Unavailable),
                "an absent tempo carries a terminal status");
            const auto& candidates = array (result, "candidates");
            const auto& curve = array (result, "curve");
            ok (candidates.stored == std::uint64_t (a.candidateCount()) && curve.stored == std::uint64_t (a.pointCount()),
                "tempo candidates and curve survive scratch release");
            if (a.determined())
            {
                ok (same (*value (result, "headlineBpm").value, a.headline().bpm)
                    && same (*value (result, "wholeTrackBpm").value, a.wholeTrack().bpm)
                    && same (*value (result, "headlineConfidence").value, a.headline().confidence),
                    "headline and whole-track tempo equal the shared detector");
                for (std::uint64_t i = 0; i < candidates.stored; ++i)
                {
                    const auto c = a.candidate (int (i));
                    ok (same (candidates.values[std::size_t (i * 2)], c.bpm)
                        && same (candidates.values[std::size_t (i * 2 + 1)], c.score), "candidate BPM and score match");
                }
                for (std::uint64_t i = 0; i < curve.stored; ++i)
                {
                    const auto p = a.point (std::int64_t (i)); const auto raw = a.rawPoint (std::int64_t (i));
                    const auto* row = curve.values.data() + std::size_t (i * 6);
                    ok (same (row[0], p.t) && same (row[1], p.bpm) && same (row[2], p.conf)
                        && same (row[3], double (p.hasBpm)) && same (row[4], raw.bpm) && same (row[5], raw.conf),
                        "smoothed and raw tempo curve preserve gaps and exact values");
                }
            }
            else ok (! value (result, "headlineBpm").value && value (result, "headlineBpm").reason == result.reason,
                "missing tempo is not a measured zero");
            const auto choice = snapshot.view().tempoChoice;
            ok (choice.ready && (choice.measured ? result.status == MeasurementStatus::Ready && same (choice.bpm, a.headline().bpm)
                : same (choice.bpm, 120.0)), "device tempo is decided only from high confidence or the configured fallback");
        }
    }
    const auto key = snapshot.view().measurements[3].key;
    (void) s.apply (command::SetTarget { 2, "club" });
    ok (s.measurementJob() == 0 && detail::Inspector::run (s).finished, "target changes never restart source analysis");
    left[0] += .01f;
    (void) s.apply (command::Load { 3, pcm, {} });
    MeasurementResult stale = snapshot.view().measurements[3];
    ok (! detail::Driver::retain (s, oldJob, stale) && s.snapshot().view().measurements[3].key != key, "replaced source rejects a late completion");
    made.session.reset();
    ok (OwnedMeasurements::valid (snapshot.view().measurements), "all retained results outlive Session and its analyzers");
    const auto wire = Wire::snapshotBytes (snapshot.view());
    std::vector<char> json (wire.jsonBytes); std::vector<double> rows (wire.rowBytes / 8);
    ok (Wire::snapshot (snapshot.view(), json, rows) == CodecStatus::Ok, "owned source and background rows cross the single codec");
}
void optionalFailure()
{
    std::vector<float> pcm (48000, .1f); const float* planes[] { pcm.data(), pcm.data() };
    auto made = Session::create(); auto& s = *made.session;
    (void) s.apply (command::Load { 1, { planes, 2, pcm.size(), 48000 }, {} });
    while (detail::Inspector::live (s) < 9) (void) s.step (1);
    while (! detail::Inspector::run (s).initialWarnings) (void) s.step (1);
    (void) s.setCapacity ({ s.liveBytes(), 0 });
    while (detail::Inspector::run (s).cursor == 0) (void) s.step (1);
    (void) s.setCapacity ({});
    while (s.state() == State::Loaded) (void) s.step (1);
    (void) s.setCapacity ({ s.liveBytes(), 0 });
    while (detail::Inspector::run (s).cursor == 5) (void) s.step (1);
    (void) s.setCapacity ({}); drive (s);
    const auto view = s.snapshot();
    ok (view.view().measurements[3].reason == MeasurementReason::Memory && view.view().measurements[9].reason == MeasurementReason::Memory,
        "optional source and background refusals retain their own visible memory reasons");
    ok (view.view().measurements[10].reason != MeasurementReason::Memory && s.check (command::Master { 9 }).rejection == Rejection::None,
        "one failed optional instrument does not block the others or Master");

    auto second = Session::create(); auto& withoutTempo = *second.session;
    (void) withoutTempo.apply (command::Load { 1, { planes, 2, pcm.size(), 48000 }, {} });
    while (detail::Inspector::live (withoutTempo) < 9 || detail::Inspector::run (withoutTempo).cursor != 8)
        (void) withoutTempo.step (1);
    ok (! withoutTempo.snapshot().view().tempoChoice.ready, "no early 120 BPM default is presented as a decision");
    (void) withoutTempo.setCapacity ({ withoutTempo.liveBytes(), 0 });
    while (detail::Inspector::run (withoutTempo).cursor == 8) (void) withoutTempo.step (1);
    (void) withoutTempo.setCapacity ({}); drive (withoutTempo);
    const auto terminal = withoutTempo.snapshot();
    const auto& tempo = terminal.view().measurements[std::size_t (Analyzer::Tempo)];
    ok (tempo.status == MeasurementStatus::Unavailable && tempo.reason == MeasurementReason::Memory
        && terminal.view().tempoChoice.ready && ! terminal.view().tempoChoice.measured
        && same (terminal.view().tempoChoice.bpm, 120.0) && terminal.view().tempoChoice.reason == MeasurementReason::Memory
        && terminal.view().state == State::Measured2,
        "optional tempo memory failure has a visible cause, a terminal fallback and no effect on mandatory readiness");
}
void missingMandatory()
{
    std::vector<float> pcm (48000); for (unsigned i = 0; i < pcm.size(); ++i) pcm[i] = i % 48 < 24 ? .25f : -.25f;
    const float* planes[] { pcm.data(), pcm.data() };
    for (const bool peak : { false, true })
    {
        auto made = Session::create(); auto& s = *made.session;
        (void) s.apply (command::Load { 1, { planes, 2, pcm.size(), 48000 }, {} });
        while (detail::Inspector::live (s) < 9) (void) s.step (1);
        detail::Inspector::missingMandatory (s, peak); drive (s);
        ok (s.state() == State::Loaded && ! s.snapshot().view().mandatoryMeasurementsReady
            && s.check (command::Master { 2 }).rejection == Rejection::NotMeasured,
            "either missing mandatory quantity alone prevents readiness and Master");
    }
}
void driftingHum()
{
    const unsigned rate = 48000, frames = 20 * rate;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i)
    {
        const double t = double (i) / rate;
        pcm[i] = float (.001 * felitronics::core::det::sin (6.283185307179586 * t * (49.5 + .03 * t))
            + (t > 8 && t < 11 ? .1 * felitronics::core::det::sin (6.283185307179586 * t * 700) : 0));
    }
    const float* planes[] { pcm.data() }; const Pcm audio { planes, 1, frames, rate };
    auto made = Session::create(); auto& s = *made.session;
    (void) s.apply (command::Load { 1, audio, {} }); drive (s);
    const auto snapshot = s.snapshot(); const auto& result = snapshot.view().measurements[std::size_t (Analyzer::Hum)];
    HumDetector a; const auto parameters = detail::MeasurementPlan::parametersFor (audio);
    a.setParams (parameters.hum); (void) a.prepare (rate, 1024, 1);
    for (unsigned at = 0; at < frames;)
    { const auto n = std::min (frames - at, 719u); const float* p[] { pcm.data() + at }; (void) a.process (p, 1, int (n)); at += n; }
    (void) a.finish(); const auto& rows = array (result, "candidates");
    ok (a.candidate (0, 0).frameObservations > 0 && a.candidate (0, 0).frameSpreadHz > .5, "the drift fixture supplies changing measured mains evidence");
    ok (same (rows.values[14], a.candidate (0, 0).frameSpreadHz)
        && *value (result, "reason[0]").value == double (a.report (0).reason), "session retains hum drift and the native refusal reason");
}
void truncatedLists()
{
    LowEnd low; LowEndParams lowParams; lowParams.maxBlocks = 2; lowParams.fftOrder = 11;
    low.setParams (lowParams); (void) low.prepare (48000, 1024, 1);
    StereoBandBursts bursts; StereoBandBursts::Params burstParams; burstParams.maxEvents = 1;
    bursts.setParams (burstParams); (void) bursts.prepare (48000, 1);
    std::vector<float> pcm (4 * 48000);
    for (unsigned i = 0; i < pcm.size(); ++i)
        pcm[i] = float ((i % 19200 < 1920 ? .2 : .00001) * felitronics::core::det::sin (double (i) * 1.0471975511965976));
    for (unsigned at = 0; at < pcm.size();)
    { const auto n = std::min (unsigned (pcm.size()) - at, 1024u); const float* p[] { pcm.data() + at }; (void) low.process (p, 1, int (n)); (void) bursts.process (p, 1, int (n)); at += n; }
    (void) low.finish(); (void) bursts.finish();
    detail::MeasurementStore lowResult, burstResult;
    lowResult.capacity = 8192; lowResult.rows.reset (new double[8192]);
    burstResult.capacity = 8192; burstResult.rows.reset (new double[8192]);
    detail::SourceResults::lowEnd (lowResult, low, .1, 2);
    detail::SourceResults::bursts (burstResult, bursts);
    ok (lowResult.arrays[0].stored == 2 && lowResult.arrays[0].total > 2 && ! lowResult.arrays[0].complete,
        "the low-end adapter cannot silently drop a long file's blocks");
    ok (bursts.axis (0).eventCount() > 1 && burstResult.arrays[0].stored == 1 && ! burstResult.arrays[0].complete
        && burstResult.arrays[0].total == std::uint64_t (bursts.axis (0).eventCount()),
        "overflowed burst lists retain the true total, stored count and explicit incompleteness");
}
void finiteSourceOverflow()
{
    std::vector<float> pcm (64, std::numeric_limits<float>::max());
    const float* planes[] { pcm.data(), pcm.data() };
    auto made = Session::create(); auto& s = *made.session;
    ok (s.apply (command::Load { 1, { planes, 2, pcm.size(), 48000 }, {} }).rejection == Rejection::None,
        "finite input is accepted even when a later Mid/Side operation overflows");
    drive (s);
    const auto snapshot = s.snapshot();
    for (const auto id : { Analyzer::LowEnd, Analyzer::LowEnd150, Analyzer::InfraLow })
        ok (snapshot.view().measurements[std::size_t (id)].reason == MeasurementReason::NonFinite,
            "an unusable Mid/Side pair reports numerical failure rather than silence");
}
void cachedSourceAndCommands()
{
    std::vector<float> pcm (48000);
    for (unsigned i = 0; i < pcm.size(); ++i) pcm[i] = float (int (i % 97) - 48) / 32768;
    pcm[480] = 32767.0f / 32768;
    const float* planes[] { pcm.data(), pcm.data() };
    auto made = Session::create(); auto& s = *made.session;
    command::Load load { 1, { planes, 2, pcm.size(), 48000 }, { {}, 48000, true, 16 } };
    ok (s.apply (load).rejection == Rejection::None, "load quantized source with 16-bit metadata");
    drive (s);
    const auto before = s.snapshot();
    ok (before.view().mandatoryMeasurementsReady && before.view().devicesPlaced
        && before.view().measurements[std::size_t (Analyzer::Excursions)].status == MeasurementStatus::Ready,
        "real measurement reaches mandatory readiness, completes the needles the limiter reads, and places the devices");
    const auto forensics = std::size_t (Analyzer::Forensics);
    ok (*value (before.view().measurements[forensics], "grid.alwaysZeroLowBits[0]").value == 0,
        "16-bit PCM initially has no unused low bits");
    for (const Request request : { Request (command::EditDevice { 7, HpfFields<Touched> {} }),
                                  Request (command::RevertEdits { 8, HpfFields<Mark> {} }) })
    {
        const auto cell = Table::commands[request.index()].cell[std::size_t (s.column())];
        ok (cell == Rejection::None && s.check (request).rejection == cell && s.apply (request).rejection == cell,
            "the public table takes device operations after a real measurement placed the devices");
    }
    load.meta.bitDepth = 24;
    const auto spent = budget::spend ([&] { (void) s.apply (load); });
    ok (spent.bytes == 0, "cached metadata refresh and needles scheduling allocate nothing");
    const auto after = s.snapshot();
    ok (s.measurementJob() == 0 && s.needlesJob() != 0,
        "cached reload schedules replacement needles from mandatory readiness");
    ok (*value (after.view().measurements[forensics], "grid.alwaysZeroLowBits[0]").value == 8,
        "cached 24-bit metadata refreshes unused bits from the retained PCM grid");
    bool unusedBitsFact = false;
    for (const auto& e : s.events())
        if (e.kind == EventKind::Fact && e.payload.fact.view().id == text::FactId::SourceUnusedBits)
            unusedBitsFact = e.payload.fact.view().args[0].integer == 8;
    ok (! unusedBitsFact, "metadata refresh changes retained evidence without emitting a late finding");
    // The same PCM under another depth is another forensics result: a key of its own (the PCM key stays for the
    // rest), and a Measurement event that says so, under the reload's revision.
    const auto forensicsEvent = [&] (std::uint64_t key)
    {
        unsigned count = 0;
        for (const auto& e : s.events())
            if (e.kind == EventKind::Measurement && e.payload.measurement.analyzer == Analyzer::Forensics)
                count += e.payload.measurement.key == key && e.payload.measurement.status == MeasurementStatus::Ready
                      && e.payload.measurement.revision == s.revision() && e.revision == s.revision() ? 1u : 100u;
        return count;
    };
    const auto refreshedKey = after.view().measurements[forensics].key;
    ok (detail::Inspector::run (s).finished && refreshedKey != before.view().measurements[forensics].key
        && after.view().measurements[0].key == before.view().measurements[0].key,
        "a changed bit depth moves the forensics key alone and keeps the completed source analysis");
    ok (forensicsEvent (refreshedKey) == 1, "the cached reload publishes the changed forensics result once, under its new key");
    {
        auto fresh = Session::create();
        ok (fresh.session->apply (load).rejection == Rejection::None
            && fresh.session->snapshot().view().measurements[forensics].key == refreshedKey,
            "the key is a function of the inputs: a fresh 24-bit load of the same PCM names the same result");
    }
    drive (s);
    ok (s.snapshot().view().measurements[std::size_t (Analyzer::Excursions)].status == MeasurementStatus::Ready,
        "replacement needles finish instead of remaining orphaned Pending");
    for (const unsigned depth : { 32u, 0u, 16u, 24u, 24u })
    {
        const auto previous = s.snapshot().view().measurements[forensics].key;
        const bool changed = depth != load.meta.bitDepth;
        load.meta.bitDepth = depth;
        const auto refreshed = budget::spend ([&] { (void) s.apply (load); });
        const auto current = s.snapshot();
        for (const auto name : { "grid.alwaysZeroLowBits[0]", "grid.alwaysZeroLowBits[1]" })
        {
            const auto& bits = value (current.view().measurements[forensics], name);
            ok (depth ? bits.value && *bits.value == double (depth - 16) : ! bits.value && bits.reason == MeasurementReason::Unsupported,
                "metadata refresh supports both channels and known/unknown depth transitions");
        }
        const auto key = current.view().measurements[forensics].key;
        ok (changed ? key != previous && forensicsEvent (key) == 1 : key == previous && forensicsEvent (key) == 0,
            "each depth change is a new key and one event; an unchanged depth is the same result, unannounced");
        ok (key == (depth == 16u ? before.view().measurements[forensics].key : depth == 24u ? refreshedKey : key),
            "returning to a depth returns to its key");
        ok (refreshed.bytes == 0 && detail::Inspector::run (s).finished, "every cached refresh avoids allocation and source DSP");
        drive (s);
    }
}
void unplacedCommandsDuringWork()
{
    // cd's glue reads the tempo, which the source's second phase measures: after the first measurement the plan waits
    // for it, and the table's Unplaced columns take no device operation.
    constexpr unsigned rate = 48000, frames = rate * 12;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i) pcm[i] = i % 24000 < 300 ? .2f : 0.0f;
    const float* planes[] { pcm.data(), pcm.data() };
    auto made = Session::create(); auto& s = *made.session;
    (void) s.apply (command::SetTarget { 1, "cd" });
    (void) s.apply (command::Load { 2, { planes, 2, pcm.size(), rate }, {} });
    while (s.state() == State::Loaded && s.measurementJob()) (void) s.step (1);
    const auto refused = [&] (Column column)
    {
        ok (s.column() == column, "the real pump exposes the plan in its table column");
        for (const Request request : { Request (command::EditDevice { 2, HpfFields<Touched> {} }),
                                      Request (command::RevertEdits { 3, HpfFields<Mark> {} }),
                                      Request (command::ImportProject { 4, {} }),
                                      Request (command::AdoptMachine { 5 }) })
        {
            const auto cell = Table::commands[request.index()].cell[std::size_t (column)];
            const auto result = s.apply (request);
            ok (cell == Rejection::NotPlaced && result.rejection == cell, "all unplaced command refusals come from the published table");
        }
    };
    ok (s.snapshot().view().plan.status == PlanStatus::Pending && s.snapshot().view().plan.awaited == Analyzer::Tempo
        && s.snapshot().view().plan.awaitedBy == Device::Glue, "the plan waits for the tempo the glue reads");
    refused (Column::Measured1Unplaced);
    const auto master = s.apply (command::Master { 5 });
    ok (master.rejection == Rejection::None, "with the panel hidden a master is taken while the plan waits");
    refused (Column::Mastering1Unplaced);
    (void) s.apply (command::Cancel { 6, master.job });
    refused (Column::Measured1Unplaced);
    (void) s.apply (command::Cancel { 7, s.measurementJob() });
    ok (s.snapshot().view().plan.status == PlanStatus::Stopped, "a stopped tempo stops the plan, which says so");
    refused (Column::StoppedMeasuredUnplaced);
    ok (s.apply (command::ContinueMeasurement { 8 }).rejection == Rejection::None, "unplaced stopped measurement can continue");
    refused (Column::Measured1Unplaced);
    drive (s);
    ok (s.column() == Column::Measured2 && s.snapshot().view().plan.status == PlanStatus::Ready,
        "once the tempo has ended the devices are placed");
}
void tempoFinishCanStopAndContinue()
{
    const unsigned rate = 48000, frames = rate * 12;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i)
        pcm[i] = i % 24000 < 300 ? .2f : 0.0f;
    const float* planes[] { pcm.data() };
    const Pcm audio { planes, 1, frames, rate };
    auto made = Session::create(); auto& s = *made.session;
    (void) s.apply (command::Load { 1, audio, {} });
    while (detail::Inspector::live (s) < 9 || detail::Inspector::run (s).cursor != 8
           || detail::Inspector::run (s).stage != 2) (void) s.step (1);
    ok (! s.snapshot().view().tempoChoice.ready && s.snapshot().view().state == State::Measured1,
        "phase one does not claim a usable tempo while finish is pending");
    (void) s.step (1); (void) s.step (1); (void) s.step (1);
    const auto stage = detail::Inspector::workspace (s).tempo->finishStage();
    const auto source = s.source().hash;
    const auto prior = s.snapshot();
    const auto stopped = budget::spend ([&] { (void) s.apply (command::Cancel { 2, s.measurementJob() }); });
    const auto paused = s.snapshot();
    ok (stopped.bytes == 0 && s.source().hash == source && paused.view().state == State::MeasurementStopped
        && paused.view().canContinueMeasurement && ! paused.view().tempoChoice.ready,
        "cancel during finish keeps PCM, unfinished tempo and a visible resume action");
    MeasurementQuery q; q.audioId = source; q.toFrame = frames; q.columns = 64;
    const auto zoom = s.query (q);
    ok (zoom.view().status == QueryStatus::Ready && zoom.view().stored == 64u * 4u,
        "waveform zoom is served while tempo finish is stopped");
    const auto resumed = budget::spend ([&] { (void) s.apply (command::ContinueMeasurement { 3 }); });
    ok (resumed.bytes == 0 && detail::Inspector::workspace (s).tempo->finishStage() == stage
        && detail::Inspector::run (s).cursor == 8 && prior.view().measurements[std::size_t (Analyzer::Crest)].key
            == s.snapshot().view().measurements[std::size_t (Analyzer::Crest)].key,
        "continue resumes the exact finish stage without restarting completed analyzers");
    drive (s);
    auto plain = Session::create(); auto& reference = *plain.session;
    (void) reference.apply (command::Load { 1, audio, {} }); drive (reference);
    const auto a = s.snapshot(), b = reference.snapshot();
    const auto& actual = a.view().measurements[std::size_t (Analyzer::Tempo)];
    const auto& expected = b.view().measurements[std::size_t (Analyzer::Tempo)];
    ok (a.view().state == State::Measured2 && actual.status == expected.status
        && actual.reason == expected.reason && actual.numbers.size() == expected.numbers.size()
        && actual.arrays.size() == expected.arrays.size(), "resumed finish reaches the same terminal phase");
    for (std::size_t i = 0; i < actual.numbers.size(); ++i)
        ok (actual.numbers[i].name == expected.numbers[i].name
            && actual.numbers[i].reason == expected.numbers[i].reason
            && actual.numbers[i].value.has_value() == expected.numbers[i].value.has_value()
            && (! actual.numbers[i].value || same (*actual.numbers[i].value, *expected.numbers[i].value)),
            "resumed tempo scalars equal uninterrupted results bit for bit");
    for (std::size_t i = 0; i < actual.arrays.size(); ++i)
    {
        const auto& x = actual.arrays[i]; const auto& y = expected.arrays[i];
        ok (x.name == y.name && x.total == y.total && x.stored == y.stored && x.values.size() == y.values.size(),
            "resumed tempo array shape matches uninterrupted results");
        for (std::size_t j = 0; j < x.values.size(); ++j)
            ok (same (x.values[j], y.values[j]), "resumed tempo curve and candidates match bit for bit");
    }
}
void masterWaitsForTempo()
{
    constexpr unsigned rate = 48000, frames = rate * 12;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i) pcm[i] = i % 24000 < 300 ? .2f : 0.0f;
    const float* planes[] { pcm.data() };
    auto made = Session::create(); auto& s = *made.session;
    ok (s.apply (command::Load { 1, { planes, 1, frames, rate }, {} }).rejection == Rejection::None, "tempo wait fixture loads");
    ok (s.apply (command::SetTarget { 2, "cd" }).rejection == Rejection::None, "CD target selected");
    while (s.state() == State::Loaded) (void) s.step (16);
    ok (s.state() == State::Measured1 && ! s.snapshot().view().tempoChoice.ready, "CD master starts with pending tempo");
    const auto first = s.apply (command::Master { 3 });
    ok (first.rejection == Rejection::None && first.job != 0, "CD master accepted during phase two");
    (void) s.step (1);
    auto waiting = s.snapshot();
    ok (s.job() == first.job && s.masters().empty() && ! waiting.view().tempoChoice.ready
        && waiting.view().masterProgress.pass == 0 && waiting.view().masterProgress.name == PhaseName::Analyzers,
        "CD master waits for terminal tempo before any render pass");
    for (unsigned i = 0; i < 1000 && detail::Inspector::run (s).cursor < 8 && s.job() == first.job; ++i)
        (void) s.step (1);
    ok (detail::Inspector::run (s).cursor == 8 && s.measurementJob() != 0
        && s.snapshot().view().measurements[std::size_t (Analyzer::Hum)].status == MeasurementStatus::Pending,
        "waiting master schedules tempo before unrelated optionals after the in-flight analyzer");
    ok (s.apply (command::Cancel { 4, first.job }).rejection == Rejection::None && s.job() == 0 && s.masters().empty(),
        "waiting master cancels without keeping a master");
    const auto measurement = s.measurementJob();
    ok (s.apply (command::Cancel { 5, measurement }).rejection == Rejection::None
        && s.state() == State::MeasurementStopped, "unfinished tempo can be stopped after master cancellation");
    const auto second = s.apply (command::Master { 6 });
    ok (second.rejection == Rejection::None && second.job != 0, "master is accepted with stopped phase two");
    (void) s.step (1);
    waiting = s.snapshot();
    ok (s.job() == second.job && s.measurementJob() != 0 && ! waiting.view().tempoChoice.ready
        && waiting.view().masterProgress.name == PhaseName::Analyzers,
        "master restarts the unfinished tempo measurement inside its waiting job");
    bool endedAfterTempo = true;
    for (unsigned i = 0; i < 200000 && s.job() != 0; ++i)
    {
        (void) s.step (16);
        if (s.job() == 0 && ! s.snapshot().view().tempoChoice.ready) endedAfterTempo = false;
    }
    ok (endedAfterTempo && s.job() == 0 && s.masters().size() == 1
        && s.snapshot().view().tempoChoice.ready && s.masters()[0].id == second.job,
        "CD master completes only after the measured or terminal fallback tempo");
    drive (s);
    ok (s.state() == State::Measured2, "other optionals continue after the CD master");
}
void masterTempoDependencyPolicy()
{
    constexpr unsigned rate = 48000, frames = rate * 12;
    std::vector<float> pcm (frames);
    for (unsigned i = 0; i < frames; ++i) pcm[i] = i % 24000 < 300 ? .2f : 0.0f;
    const float* planes[] { pcm.data() };
    const auto start = [&] (Session& s)
    {
        ok (s.apply (command::Load { 1, { planes, 1, frames, rate }, {} }).rejection == Rejection::None,
            "dependency fixture loads");
        ok (s.apply (command::SetTarget { 2, "club" }).rejection == Rejection::None,
            "non-CD target selected");
        while (s.state() == State::Loaded) (void) s.step (16);
        ok (s.state() == State::Measured1 && ! s.snapshot().view().tempoChoice.ready,
            "dependency fixture enters phase two with pending tempo");
    };
    {
        auto made = Session::create(); auto& s = *made.session; start (s);
        const auto master = s.apply (command::Master { 3 });
        (void) s.step (1);
        ok (master.rejection == Rejection::None && s.snapshot().view().masterProgress.pass == 1
            && ! s.snapshot().view().tempoChoice.ready,
            "non-CD target without active glue starts rendering without tempo");
    }
    {
        auto made = Session::create(); auto& s = *made.session; start (s);
        detail::Inspector::place (s);
        GlueFields<Touched> glue; glue.on = true; glue.upToDb = 1.0;
        ok (s.apply (command::EditDevice { 3, glue }).rejection == Rejection::None,
            "glue can be enabled for a non-CD target");
        const auto master = s.apply (command::Master { 4 });
        (void) s.step (1);
        ok (master.rejection == Rejection::None && s.job() == master.job
            && s.snapshot().view().masterProgress.name == PhaseName::Analyzers
            && s.snapshot().view().masterProgress.pass == 0,
            "enabled glue waits inside the master for tempo");
        const auto measurement = s.measurementJob();
        ok (s.apply (command::Cancel { 5, measurement }).rejection == Rejection::None
            && s.measurementJob() == 0 && s.job() == 0 && s.masters().empty(),
            "cancelling a waiting master's measurement also stops that master");
    }
}
void directTempoParity (const std::vector<float>& pcm, unsigned rate, bool varying, bool gaps)
{
    const float* planes[] { pcm.data() };
    const Pcm audio { planes, 1, pcm.size(), rate };
    auto made = Session::create(); auto& s = *made.session;
    ok (s.apply (command::Load { 1, audio, {} }).rejection == Rejection::None, "long tempo parity fixture loads");
    drive (s);
    const auto snapshot = s.snapshot();
    const auto& result = snapshot.view().measurements[std::size_t (Analyzer::Tempo)];
    const auto plan = detail::MeasurementPlan::storageFor (audio, detail::MeasurementPlan::parametersFor (audio));
    detail::MeasurementWorkspace direct;
    ok (direct.prepare (Analyzer::Tempo, audio, plan), "direct tempo analyzer prepares with session parameters");
    for (std::size_t start = 0; start < pcm.size();)
    {
        const auto n = std::min<std::size_t> (317, pcm.size() - start);
        const float* part[] { pcm.data() + start };
        ok (direct.tempo->process (part, 1, int (n)), "direct tempo processes independent chunking");
        start += n;
    }
    auto& a = *direct.tempo; ok (a.finish() && a.determined() && result.status == MeasurementStatus::Ready,
        "long tempo fixture reaches a direct and session report");
    const auto scalar = [&] (std::string_view name, double expected)
    { const auto& got = value (result, name); ok (got.value && same (*got.value, expected), "every retained tempo headline field matches direct bits"); };
    const auto headline = [&] (std::string_view prefix, const felitronics::tempo::TempoHeadline& h)
    {
        const std::string pre (prefix);
        scalar (pre + "Bpm", h.bpm); scalar (pre + "Confidence", h.confidence); scalar (pre + "Label", double (h.label));
        scalar (pre + "BeatPeriodSec", h.beatPeriodSec); scalar (pre + "BeatOffsetSec", h.beatOffsetSec);
        scalar (pre + "AlternativeCount", double (h.altCount));
        for (int i = 0; i < h.altCount; ++i) scalar (pre + "Alternative[" + std::to_string (i) + "]", h.alts[i]);
    };
    headline ("headline", a.headline()); headline ("wholeTrack", a.wholeTrack());
    scalar ("varies", a.varies() ? 1.0 : 0.0); scalar ("hasRange", a.hasRange() ? 1.0 : 0.0);
    if (a.hasRange()) { scalar ("rangeLowBpm", a.rangeLow()); scalar ("rangeHighBpm", a.rangeHigh()); }
    scalar ("anchorBpm", a.anchorBpm()); scalar ("anchorConfidence", a.anchorConfidence()); scalar ("anchorLag", a.anchorLag());
    scalar ("candidateCount", double (a.candidateCount())); scalar ("curvePoints", double (a.pointCount()));
    const auto& candidates = array (result, "candidates"); const auto& curve = array (result, "curve");
    ok (curve.stored == std::uint64_t (a.pointCount()) && curve.stored > 0
        && candidates.stored == std::uint64_t (a.candidateCount()), "long source retains every direct tempo row");
    ok (candidates.grid.firstFrame == 0 && candidates.grid.stepFrames == 0
        && candidates.grid.framesRead == 0 && candidates.grid.sampleRate == 0,
        "BPM candidates have no false time grid");
    const auto centre = std::uint64_t (a.windowFrames()) * felitronics::tempo::TempoDetector::kHop / 2u;
    const auto stride = std::uint64_t (a.hopFrames()) * felitronics::tempo::TempoDetector::kHop;
    ok (curve.grid.firstFrame == centre && curve.grid.stepFrames == stride
        && curve.grid.framesRead == pcm.size() && curve.grid.sampleRate == rate,
        "curve grid names actual window centres in source frames");
    bool sawGap = false, sawBpm = false;
    for (std::uint64_t i = 0; i < curve.stored; ++i)
    {
        const auto p = a.point (std::int64_t (i)), raw = a.rawPoint (std::int64_t (i));
        const auto* row = curve.values.data() + std::size_t (i * 6u);
        ok (same (row[0], p.t) && same (row[1], p.bpm) && same (row[2], p.conf)
            && same (row[3], double (p.hasBpm)) && same (row[4], raw.bpm) && same (row[5], raw.conf),
            "every long session curve point and gap matches direct bits");
        ok (std::fabs (double (centre + i * stride) / double (rate) - p.t) <= 0.051,
            "every curve grid coordinate agrees with its rounded displayed time");
        sawGap = sawGap || ! p.hasBpm; sawBpm = sawBpm || p.hasBpm;
    }
    for (std::uint64_t i = 0; i < candidates.stored; ++i)
    {
        const auto c = a.candidate (int (i));
        ok (same (candidates.values[std::size_t (i * 2u)], c.bpm)
            && same (candidates.values[std::size_t (i * 2u + 1u)], c.score), "every candidate matches direct bits");
    }
    if (varying) ok (a.varies() && sawBpm, "variable tempo produces measured changing windows");
    if (gaps) ok (sawGap, "a long session curve retains missing-tempo windows");
}
void longTempoParity()
{
    constexpr unsigned rate = 44100;
    std::vector<float> changing (rate * 32u);
    const auto clicks = [&] (unsigned from, unsigned to, unsigned period)
    {
        for (unsigned at = from; at < to; at += period)
            for (unsigned i = 0; i < 220 && at + i < to; ++i)
                changing[at + i] = float ((1.0 - double (i) / 220.0) * (i % 2 ? -1.0 : 1.0));
    };
    clicks (0, rate * 16u, 26460); clicks (rate * 16u, rate * 32u, 17640);
    directTempoParity (changing, rate, true, false);
    std::vector<float> lonely (rate * 8u);
    for (unsigned i = 0; i < 220; ++i) lonely[rate * 3u + i] = float ((1.0 - double (i) / 220.0) * (i % 2 ? -1.0 : 1.0));
    directTempoParity (lonely, rate, false, true);
}
void deviceTempoPolicy()
{
    auto made = Session::create(); auto& s = *made.session;
    MeasurementValue values[] { { "headlineBpm", 127.3, MeasurementReason::None, 0 },
                                { "headlineLabel", double (felitronics::tempo::ConfidenceLabel::High), MeasurementReason::None, 0 } };
    detail::Inspector::tempoOutcome (s, MeasurementStatus::Pending, MeasurementReason::Pending, values);
    ok (! s.snapshot().view().tempoChoice.ready, "pending tempo never masquerades as a finished fallback");
    detail::Inspector::tempoOutcome (s, MeasurementStatus::Ready, MeasurementReason::None, values);
    const auto measured = s.snapshot().view().tempoChoice;
    ok (measured.ready && measured.measured && same (measured.bpm, 127.3), "high confidence uses the measured BPM");
    values[1].value = double (felitronics::tempo::ConfidenceLabel::Medium);
    const auto unsure = s.snapshot().view().tempoChoice;
    ok (unsure.ready && ! unsure.measured && same (unsure.bpm, 120.0)
        && same (*values[0].value, 127.3), "medium confidence keeps the measured BPM separate from the device fallback");
    detail::Inspector::tempoOutcome (s, MeasurementStatus::Unavailable, MeasurementReason::Memory, values);
    const auto unavailable = s.snapshot().view().tempoChoice;
    ok (unavailable.ready && ! unavailable.measured && same (unavailable.bpm, 120.0)
        && unavailable.reason == MeasurementReason::Memory, "unavailable tempo uses the fallback with its cause");
}
int main()
{
    fixture (2, 48000, 192000, 317, true);
    fixture (1, 48000, 192000, 1024, false);
    fixture (2, 8000, 800, 1, false);
    fixture (2, 8000, 8000, 319, false);
    optionalFailure(); missingMandatory(); driftingHum(); truncatedLists(); finiteSourceOverflow(); cachedSourceAndCommands(); unplacedCommandsDuringWork();
    tempoFinishCanStopAndContinue(); masterWaitsForTempo(); masterTempoDependencyPolicy(); longTempoParity(); deviceTempoPolicy();
    ok (! detail::SourceWarnings::wideBass (.059999999) && detail::SourceWarnings::wideBass (.06), "wide bass includes exactly six percent");
    ok (detail::SourceWarnings::quiet (-40) == text::FactId::Value && detail::SourceWarnings::quiet (-40.0001) == text::FactId::SourceQuiet
        && detail::SourceWarnings::quiet (-55) == text::FactId::SourceQuiet && detail::SourceWarnings::quiet (-55.0001) == text::FactId::SourceGainOnly,
        "quiet-input warning and fallback preserve both strict boundaries");
    std::printf ("source-results-digest=%016llx\n", static_cast<unsigned long long> (digest));
    return felitronics::test::report();
}
