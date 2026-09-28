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
    ok (! snapshot.view().devicesPlaced, "snapshot explicitly separates device placement from measurements");
    ok (s.state() == (frames >= rate * 4 / 10 ? State::Measured1 : State::Loaded), "first-phase readiness follows usable loudness and true peak; background does not invent Measured2");
    ok (s.check (command::EditDevice { 2, HpfFields<Touched> {} }).rejection == Rejection::NotPlaced, "real measurements do not substitute defaults for device placement");
    if (s.state() == State::Measured1) ok (s.check (command::Master { 3 }).rejection == Rejection::None, "Master remains available after optional outcomes");
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
int main()
{
    fixture (2, 48000, 192000, 317, true);
    fixture (1, 48000, 192000, 1024, false);
    fixture (2, 8000, 800, 1, false);
    fixture (2, 8000, 8000, 319, false);
    optionalFailure(); missingMandatory(); driftingHum(); truncatedLists(); finiteSourceOverflow();
    ok (! detail::SourceWarnings::wideBass (.059999999) && detail::SourceWarnings::wideBass (.06), "wide bass includes exactly six percent");
    ok (detail::SourceWarnings::quiet (-40) == text::FactId::Value && detail::SourceWarnings::quiet (-40.0001) == text::FactId::SourceQuiet
        && detail::SourceWarnings::quiet (-55) == text::FactId::SourceQuiet && detail::SourceWarnings::quiet (-55.0001) == text::FactId::SourceGainOnly,
        "quiet-input warning and fallback preserve both strict boundaries");
    std::printf ("source-results-digest=%016llx\n", static_cast<unsigned long long> (digest));
    return felitronics::test::report();
}
