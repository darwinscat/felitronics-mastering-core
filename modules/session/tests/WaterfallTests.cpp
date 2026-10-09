// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE WATERFALL AND WHAT CAME WITH IT, theme by theme: the three share fields, the master's waterfall report, a zone at
// 0 % out of the chain, the saturation's drive ceiling, a glue, a saturation and a cut held by their step, a zone met on the steering's total, the
// steering's convergence, cleaner not louder and its switch, the two clippers, Maximum · nuke, the queue's snapshot at the command, a queue waiting for the
// take — and a master with no wish, to the bit, as the candidate before the waterfall made it (commit 808c058) on every
// target.

#include "MasterJob.h"
#include "Devices.h"
#include "Rules.h"
#include "embedded/not-cleaner.h"     // the shipped engine, extreme written cleaner = false (CMakeLists.txt)
#include "embedded/place-start.h"     // ... [limiter.peakClipper] place = "start"
#include "embedded/place-limiter.h"   // ... place = "limiter"
#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/core/DetMath.h>
#include <felitronics_test.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;

// The suites' seam into the session (Session.h befriends it; the library defines none): its master job, as the last
// master left it.
struct felitronics::session::detail::Inspector
{
    static const MasterJob* masterJob (const Session& s) noexcept { return s.masterJob_.get(); }
};

namespace
{
// A master with no wish, to the bit: a fixture of six seconds (a kick, a click on its attack and a pad that swells, made
// with the det functions, so every platform makes the same file), mastered on `target` by the session (version 0, the
// machine's devices, no edit), its delivered PCM hashed (FNV-1a over the float bits). 0 where a step fails.
std::uint64_t noWishDigest (std::string_view target)
{
    namespace det = felitronics::core::det;
    constexpr double kPi = 3.141592653589793;
    constexpr unsigned rate = 48000, frames = rate * 6;
    std::vector<float> left (frames), right (frames);
    for (unsigned i = 0; i < frames; ++i)
    {
        const double t = double (i) / rate, beat = double (i % (rate / 2)) / rate;
        const double kick = 0.45 * det::exp2 (-beat / 0.04) * det::sin (2 * kPi * 60.0 * beat)
                          + (beat < 0.004 ? 0.15 * (double ((i * 2654435761u) >> 16 & 0xffffu) / 32768.0 - 1.0) : 0.0);
        const double swell = t < 3.0 ? 0.6 : 1.0;
        const double pad = swell * (0.08 * det::sin (2 * kPi * 220.0 * t) + 0.05 * det::sin (2 * kPi * 3310.0 * t));
        left[i] = float (kick + pad);
        right[i] = float (kick + swell * (0.08 * det::sin (2 * kPi * 220.0 * t + 0.4) + 0.05 * det::sin (2 * kPi * 3310.0 * t + 1.1)));
    }
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create();
    if (made.status != Status::Ok) return 0;
    auto& s = *made.session;
    if (s.apply (command::SetTarget { 1, target }).rejection != Rejection::None
        || s.apply (command::Load { 2, { planes, 2, frames, rate }, { "mix.wav", rate, true, 24 } }).rejection != Rejection::None) return 0;
    for (unsigned i = 0; i < 4000000 && (s.measurementJob() || s.needlesJob()); ++i) (void) s.step (16);
    if (s.apply (command::Master { 3 }).rejection != Rejection::None) return 0;
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

// The same fixture's six seconds, planar (left, then right), for the sessions below.
std::vector<float> fixture()
{
    namespace det = felitronics::core::det;
    constexpr double kPi = 3.141592653589793;
    constexpr unsigned rate = 48000, frames = rate * 6;
    std::vector<float> out (2u * frames);
    for (unsigned i = 0; i < frames; ++i)
    {
        const double t = double (i) / rate, beat = double (i % (rate / 2)) / rate;
        const double kick = 0.45 * det::exp2 (-beat / 0.04) * det::sin (2 * kPi * 60.0 * beat)
                          + (beat < 0.004 ? 0.15 * (double ((i * 2654435761u) >> 16 & 0xffffu) / 32768.0 - 1.0) : 0.0);
        const double swell = t < 3.0 ? 0.6 : 1.0;
        out[i] = float (kick + swell * (0.08 * det::sin (2 * kPi * 220.0 * t) + 0.05 * det::sin (2 * kPi * 3310.0 * t)));
        out[frames + i] = float (kick + swell * (0.08 * det::sin (2 * kPi * 220.0 * t + 0.4) + 0.05 * det::sin (2 * kPi * 3310.0 * t + 1.1)));
    }
    return out;
}

// A session on `target` with the fixture loaded; measured unless `measure` is false.
std::unique_ptr<Session> loaded (const std::vector<float>& pcm, std::string_view target, bool measure = true)
{
    const std::size_t frames = pcm.size() / 2u;
    const float* planes[] { pcm.data(), pcm.data() + frames };
    auto made = Session::create();
    if (made.status != Status::Ok || made.session->apply (command::SetTarget { 1, target }).rejection != Rejection::None
        || made.session->apply (command::Load { 2, { planes, 2, frames, 48000 }, { "mix.wav", 48000, true, 24 } }).rejection
            != Rejection::None) return nullptr;
    if (measure)
        for (unsigned i = 0; i < 4000000 && (made.session->measurementJob() || made.session->needlesJob()); ++i) (void) made.session->step (16);
    return std::move (made.session);
}

// Runs every job out, releasing each delivered master so the next queued one starts.
void runOut (Session& s)
{
    for (unsigned i = 0; i < 4000000; ++i)
    {
        if (s.pendingMaster().master != 0) (void) s.releaseMaster (s.pendingMaster());
        if (s.step (64).state == StepState::Done && s.pendingMaster().master == 0 && s.job() == 0) return;
    }
}

// A master asked now, run out: its kept entry (nullptr where it was refused or did not finish).
const Kept* master (Session& s, CommandId id)
{
    const std::size_t before = s.masters().size();
    if (s.pendingMaster().master != 0) (void) s.releaseMaster (s.pendingMaster());
    if (s.apply (command::Master { id }).rejection != Rejection::None) return nullptr;
    runOut (s);
    return s.masters().size() > before && s.masters().back().report ? &s.masters().back() : nullptr;
}

double number (felitronics::toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto n = v.integer()) return double (*n);
    return std::numeric_limits<double>::quiet_NaN();
}

std::string num (double x) { char b[32]; std::snprintf (b, sizeof b, "%.3f", x); return b; }

bool editShares (Session& s, CommandId id, std::optional<double> glue, std::optional<double> saturation, std::optional<double> cut)
{
    bool fine = true;
    if (glue) { GlueFields<Touched> f; f.share = *glue; fine = fine && s.apply (command::EditDevice { id, f }).rejection == Rejection::None; }
    if (saturation) { SaturationFields<Touched> f; f.share = *saturation; fine = fine && s.apply (command::EditDevice { CommandId (id + 1), f }).rejection == Rejection::None; }
    if (cut) { LimiterFields<Touched> f; f.cutShare = *cut; fine = fine && s.apply (command::EditDevice { CommandId (id + 2), f }).rejection == Rejection::None; }
    return fine;
}

bool sounds (const detail::MasterPlan& p, bool glue)
{
    const auto& t = p.ready.topology;
    const auto& q = p.ready.params;
    return glue ? t.compressor && ! q.bypassCompressor : t.clipper && ! q.bypassClipper;
}

// The PCM `s` delivers for a master asked now (id 90), run until no job is left: FNV-1a over the float bits, 0 where a
// step fails.
std::uint64_t deliveredDigest (Session& s)
{
    if (s.apply (command::Master { 90 }).rejection != Rejection::None) return 0;
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

//==============================================================================
// THE SHARE FIELDS: a person's alone — the machine's layer never holds one, and a share is the only field its edit
// touches; on the mix's domain 0…1 (a value off it refused OutOfDomain, a non-number NotFinite, whole: the revision and
// the project stay); a revert takes back the one share masked; and a person's own field wins over the steering (a drive
// by hand is never raised to the ceiling).
void shareFields()
{
    felitronics::test::group ("the share fields: a person's alone, their domain, their revert, a person's field first");
    const auto pcm = fixture();
    auto made = loaded (pcm, "spotify");
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    Session& s = *made;
    const auto& d = s.project().devices;
    ok (d.glue.machine.share == 0.0 && d.saturation.machine.share == 0.0 && d.limiter.machine.cutShare == 0.0
            && ! d.glue.hand.share && ! d.saturation.hand.share && ! d.limiter.hand.cutShare,
        "placed: no share in the machine's layer, none in a person's");
    ok (! detail::MasterJob::plan (s, command::Master { 90 }, s.project()).request.waterfall(), "no share asked: no waterfall");
    for (const double bad : { -0.05, 1.05 })
    {
        const auto rev = s.revision();
        GlueFields<Touched> g; g.share = bad;
        SaturationFields<Touched> t; t.share = bad;
        LimiterFields<Touched> l; l.cutShare = bad;
        const auto a = s.apply (command::EditDevice { 10, g }), b = s.apply (command::EditDevice { 11, t }), c = s.apply (command::EditDevice { 12, l });
        ok (a.rejection == Rejection::OutOfDomain && b.rejection == Rejection::OutOfDomain && c.rejection == Rejection::OutOfDomain
                && s.revision() == rev && ! d.glue.hand.share && ! d.saturation.hand.share && ! d.limiter.hand.cutShare,
            "a share of " + num (bad) + " refused OutOfDomain on all three, whole");
    }
    {
        const auto rev = s.revision();
        GlueFields<Touched> g; g.share = std::numeric_limits<double>::quiet_NaN();
        ok (s.apply (command::EditDevice { 13, g }).rejection == Rejection::NotFinite && s.revision() == rev && ! d.glue.hand.share,
            "a share that is no number refused NotFinite, whole");
    }
    ok (editShares (s, 20, 0.0, 1.0, 0.5), "0 and 1 are on the domain, as 0.5");
    ok (editShares (s, 30, 0.3, 0.2, 0.25), "PRECONDITION: three shares asked");
    const auto& g = d.glue.hand;
    ok (g.share == 0.3 && ! g.on && ! g.upToDb && ! g.mix && ! g.thresholdDb && ! g.ratio && ! g.kneeDb && ! g.attackMs && ! g.releaseMs
            && d.saturation.hand.share == 0.2 && ! d.saturation.hand.drive && ! d.saturation.hand.mix
            && d.limiter.hand.cutShare == 0.25 && ! d.limiter.hand.needles && ! d.limiter.hand.needlesDb,
        "a share's edit touches its share alone");
    auto planned = detail::MasterJob::plan (s, command::Master { 91 }, s.project());
    ok (planned.request.waterfall() && planned.request.waterfallGlueShare == 0.3 && planned.request.waterfallSaturationShare == 0.2
            && planned.request.waterfallCutShare == 0.25 && std::isfinite (planned.request.waterfallSaturationDriveMaxDb),
        "the plan carries the three shares, and the drive's ceiling where no person set the drive");
    SaturationFields<Touched> drive; drive.drive = 3.0;
    ok (s.apply (command::EditDevice { 40, drive }).rejection == Rejection::None
            && std::isnan (detail::MasterJob::plan (s, command::Master { 92 }, s.project()).request.waterfallSaturationDriveMaxDb),
        "a person's drive wins: the waterfall never raises it");
    SaturationFields<Mark> mask; mask.share = true;
    ok (s.apply (command::RevertEdits { 41, mask }).rejection == Rejection::None && ! d.saturation.hand.share
            && d.saturation.hand.drive == 3.0 && d.glue.hand.share == 0.3 && d.limiter.hand.cutShare == 0.25,
        "a revert of the saturation's share takes back that share alone");
    GlueFields<Mark> glueMask; glueMask.share = true;
    LimiterFields<Mark> cutMask; cutMask.cutShare = true;
    ok (s.apply (command::RevertEdits { 42, glueMask }).rejection == Rejection::None
            && s.apply (command::RevertEdits { 43, cutMask }).rejection == Rejection::None
            && ! d.glue.hand.share && ! d.limiter.hand.cutShare
            && ! detail::MasterJob::plan (s, command::Master { 93 }, s.project()).request.waterfall(),
        "all three taken back: no waterfall");
}

//==============================================================================
// ONE READER PER KEY: [limiter.peakClipper] place and [saturation] steerDriveMaxDb are read by the master's plan alone —
// the schema checks them and the typed config holds no copy; and a share's rule is its own (0…1, no step, so the page's
// 0.001 moves pass), not the glue mix's knob read from [glue] — a glue mix bounded otherwise leaves the shares as they are.
template <class C> constexpr bool holdsPlace = requires (C c) { c.place; };
template <class C> constexpr bool holdsDriveCeiling = requires (C c) { c.steerDriveMaxDb; };

void oneReaderPerKey()
{
    felitronics::test::group ("one reader per key: place and steerDriveMaxDb in the plan alone, a share's rule its own");
    constexpr bool placeCopied = holdsPlace<config::PeakClipper>, ceilingCopied = holdsDriveCeiling<config::Saturation>;
    ok (! placeCopied && ! ceilingCopied, std::string ("the typed config holds no place (") + (placeCopied ? "holds" : "none")
                                              + ") and no steerDriveMaxDb (" + (ceilingCopied ? "holds" : "none") + ")");
    auto r = detail::rules();
    r.glueMix.maximum = { 5, 1, false };   // a glue mix up to 0.5
    detail::FieldRule glueShare {}, saturationShare {}, cutShare {};
    detail::DeviceOf<GlueFields<Touched>>::each (r, [&] (unsigned i, const detail::FieldRule& rule, auto&&...) { if (i == 8) glueShare = rule; });
    detail::DeviceOf<SaturationFields<Touched>>::each (r, [&] (unsigned i, const detail::FieldRule& rule, auto&&...) { if (i == 5) saturationShare = rule; });
    detail::DeviceOf<LimiterFields<Touched>>::each (r, [&] (unsigned i, const detail::FieldRule& rule, auto&&...) { if (i == 5) cutShare = rule; });
    bool fine = ! r.glueMix.accepts (0.8, 48000);
    for (const auto* rule : { &glueShare, &saturationShare, &cutShare })
        fine = fine && rule->kind == detail::FieldRule::Kind::Knob && rule->knob.accepts (0.0, 48000) && rule->knob.accepts (0.8, 48000)
            && rule->knob.accepts (0.123, 48000) && rule->knob.accepts (1.0, 48000) && ! rule->knob.accepts (1.001, 48000)
            && ! rule->knob.accepts (-0.001, 48000) && rule->knob.step.mantissa == 0;
    ok (fine, "a glue mix bounded at 0.5: the three shares still take 0, 0.123, 0.8 and 1, refuse 1.001 and −0.001, with no step");
}

//==============================================================================
// THE WATERFALL REPORT on the fixture: each zone's asked share (the person's, scaled to a total of 1 where the three ask
// more), the share it reached (its dB over the total), and its stop by the report's rule — Reached within 0.1 dB of its
// want or 3 % of the total, else the setting that stopped it; the limiter takes the rest. A master with no wish has no
// waterfall.
void waterfallReport()
{
    felitronics::test::group ("the waterfall report: asked, reached and the stop of each zone");
    const auto pcm = fixture();
    for (const bool over : { false, true })
    {
        auto made = loaded (pcm, "spotify");
        if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
        Session& s = *made;
        if (! over)
        {
            const Kept* plain = master (s, 5);
            ok (plain && ! plain->report->waterfall, "a master with no wish: no waterfall in its report");
        }
        const double g = over ? 0.6 : 0.2, t = over ? 0.6 : 0.3, c = over ? 0.3 : 0.2, sum = g + t + c, scale = sum > 1.0 ? 1.0 / sum : 1.0;
        const Kept* k = editShares (s, 10, g, t, c) ? master (s, 20) : nullptr;
        const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
        if (! w) { ok (false, "a master with three shares reports its waterfall"); continue; }
        const double total = w->totalDb.value_or (0.0);
        const auto close = [] (std::optional<double> a, double b) { return a && std::fabs (*a - b) < 1e-9; };
        ok (close (w->glue.asked, g * scale) && close (w->saturation.asked, t * scale) && close (w->cut.asked, c * scale)
                && close (w->limiter.asked, std::fmax (0.0, 1.0 - sum * scale)) && w->limiter.stop == WaterfallStop::Rest,
            std::string (over ? "asking 150 %: " : "asking 70 %: ") + "the asked shares " + num (*w->glue.asked) + ", "
                + num (*w->saturation.asked) + ", " + num (*w->cut.asked) + ", the limiter the rest " + num (*w->limiter.asked));
        double reachedSum = 0.0;
        bool rule = total > 0.0;
        for (const auto* z : { &w->glue, &w->saturation, &w->cut })
        {
            if (! z->reached || ! z->db || ! z->asked) { rule = false; continue; }
            reachedSum += *z->reached;
            const bool within = std::fabs (*z->asked * total - *z->db) <= std::fmax (0.1, 0.03 * total);
            rule = rule && close (z->reached, *z->db / total) && (z->stop == WaterfallStop::Reached) == (within && z->setting.has_value());
        }
        reachedSum += w->limiter.reached.value_or (0.0);
        ok (rule && std::fabs (reachedSum - 1.0) < 1e-6,
            std::string (over ? "asking 150 %: " : "asking 70 %: ") + "each zone reached its dB over the total " + num (total)
                + " dB, the four sum to 1, and Reached is said exactly within the tolerance — glue " + num (w->glue.reached.value_or (-1))
                + " (stop " + std::to_string (unsigned (w->glue.stop)) + "), saturation " + num (w->saturation.reached.value_or (-1))
                + " (" + std::to_string (unsigned (w->saturation.stop)) + "), cut " + num (w->cut.reached.value_or (-1))
                + " (" + std::to_string (unsigned (w->cut.stop)) + ")");
    }
}

//==============================================================================
// A ZONE AT 0 % LEAVES ITS STAGE OUT OF THE CHAIN: a glue or a saturation share of 0 does not sound, where a share above
// it ticks the stage; its zone reports Reached at 0 dB.
void zeroZone()
{
    felitronics::test::group ("a zone at 0 % leaves its stage out of the chain");
    const auto pcm = fixture();
    auto made = loaded (pcm, "spotify");
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    Session& s = *made;
    ok (editShares (s, 10, 0.3, 0.3, std::nullopt), "PRECONDITION: glue and saturation asked 30 %");
    const auto on = detail::MasterJob::plan (s, command::Master { 90 }, s.project());
    ok (sounds (on, true) && sounds (on, false), "asked 30 %: the glue and the saturation sound");
    ok (editShares (s, 20, 0.0, 0.0, std::nullopt), "PRECONDITION: both asked 0 %");
    const auto off = detail::MasterJob::plan (s, command::Master { 91 }, s.project());
    ok (off.rejection == Rejection::None && ! sounds (off, true) && ! sounds (off, false), "asked 0 %: neither is in the chain");
    const Kept* k = master (s, 30);
    const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
    ok (w && w->glue.stop == WaterfallStop::Reached && w->saturation.stop == WaterfallStop::Reached
            && w->glue.db.value_or (1.0) <= 0.05 && w->saturation.db.value_or (1.0) <= 0.05 && ! w->glue.setting && ! w->saturation.setting,
        "their zones reached 0 % at no dB, with no setting");
}

//==============================================================================
// THE DRIVE'S CEILING: a saturation asked more of the peak work than its mix at 1 takes has its drive raised, up to
// [saturation] steerDriveMaxDb on the knob, and stops there: DriveAtCeiling, its drive the ceiling (maxNuke, asked all
// of it; maxExtreme's large wishes are saturationAtLargeShares).
void driveCeiling()
{
    felitronics::test::group ("the saturation's drive raised up to [saturation] steerDriveMaxDb, DriveAtCeiling there");
    const double ceiling = number (detail::rules().engine.find ("saturation").find ("steerDriveMaxDb"));
    const auto pcm = fixture();
    for (const std::string_view target : { "maxNuke" })
    {
    auto made = loaded (pcm, target);
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    Session& s = *made;
    const Kept* k = editShares (s, 10, std::nullopt, 1.0, std::nullopt) ? master (s, 20) : nullptr;
    const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
    ok (w && w->saturation.stop == WaterfallStop::DriveAtCeiling && w->saturation.drive
            && std::fabs (*w->saturation.drive - ceiling) <= 0.1 && w->saturation.setting && *w->saturation.setting >= 0.999,
        std::string (target) + ", asked 100 %: the mix at 1, the drive " + num (w && w->saturation.drive ? *w->saturation.drive : -1.0) + " dB at the ceiling "
            + num (ceiling) + " dB, stop " + std::to_string (w ? unsigned (w->saturation.stop) : 99u) + ", mix " + num (w && w->saturation.setting ? *w->saturation.setting : -1.0));
    // The as-worked export names that stop as the codec does (its table once held one name fewer than the stops).
    const std::string worked = k ? std::string (s.exportWorked (k->id).view()) : std::string();
    ok (worked.find ("saturationStop = \"driveAtCeiling\"") != std::string_view::npos,
        std::string (target) + ": the as-worked export says saturationStop = \"driveAtCeiling\"");
    }
}

//==============================================================================
// THE CEILING BY MEASUREMENT (engine.toml, [saturation] steerDriveMaxDb): maxNuke at the page's shares (0.10 / 0.40 /
// 0.10) on the fixture wants more saturation than its mix at 1 and a drive of 10 dB take — the steering raises the drive
// past 10 dB, to no more than the ceiling.
void nukeDrivePastTen()
{
    felitronics::test::group ("maxNuke at the page's shares: the saturation's drive past 10 dB, up to the ceiling");
    const double ceiling = number (detail::rules().engine.find ("saturation").find ("steerDriveMaxDb"));
    const auto pcm = fixture();
    auto made = loaded (pcm, "maxNuke");
    const Kept* k = made && editShares (*made, 10, 0.10, 0.40, 0.10) ? master (*made, 20) : nullptr;
    const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
    const double drive = w ? w->saturation.drive.value_or (-1.0) : -1.0;
    ok (w && drive > 10.05 && drive <= ceiling + 0.05,
        "the drive " + num (drive) + " dB (the ceiling " + num (ceiling) + " dB), the saturation reached "
            + num (w ? w->saturation.reached.value_or (-1.0) : -1.0) + " of its 0.40");
}

//==============================================================================
// A LARGE SATURATION SHARE TAKES ITS SHARE: on maxDense and maxExtreme a saturation asked 30 % to all of the peak work
// lands on its share of the total the delivered render reports — within the steering's tolerance (0.1 dB, or 3 % of the
// total where that is more) and 0.1 dB more, the limiter's take the steering read on its last pass near the target
// against the landing's own last pass — or, where its mix at 1 and its drive at [saturation] steerDriveMaxDb take less,
// stops there: DriveAtCeiling. The steering once read the zones' work as added on top of the limiter's: a wish past half
// of the total overshot by more than it missed on every move, and shares 0.6–0.9 took all of the work (before that, none).
void saturationAtLargeShares()
{
    felitronics::test::group ("a large saturation share takes its share of the peak work, or stops at the drive's ceiling");
    const double ceiling = number (detail::rules().engine.find ("saturation").find ("steerDriveMaxDb"));
    const auto pcm = fixture();
    for (const std::string_view target : { "maxDense", "maxExtreme" })
        for (const double share : { 0.3, 0.6, 0.8, 0.9, 1.0 })
        {
            auto made = loaded (pcm, target);
            const Kept* k = made && editShares (*made, 10, std::nullopt, share, std::nullopt) ? master (*made, 20) : nullptr;
            const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
            const auto& z = w ? w->saturation : MasterWaterfallZone {};
            const double total = w ? w->totalDb.value_or (0.0) : 0.0, db = z.db.value_or (-1.0);
            const double mix = z.setting.value_or (-1.0), drive = z.drive.value_or (-1.0);
            const bool reached = total > 0.0 && std::fabs (share * total - db) <= std::fmax (0.1, 0.03 * total) + 0.1;
            const bool atCeiling = mix >= 0.999 && std::fabs (drive - ceiling) <= 0.1 && db < share * total
                                && z.stop == WaterfallStop::DriveAtCeiling;
            ok (w && (reached || atCeiling),
                std::string (target) + ", saturation asked " + num (share) + ": reached " + num (z.reached.value_or (-1.0)) + " (" + num (db)
                    + " dB of " + num (total) + "), mix " + num (mix) + ", drive " + num (drive) + " dB, stop "
                    + std::to_string (unsigned (z.stop)) + ", " + std::to_string (w ? w->extraPasses : 0u) + " moves");
        }
}

//==============================================================================
// A WHOLE SUM KEEPS ITS RATIOS: shares 0.5 / 0.4 / 0.1 (glue, saturation, cut — the limiter asked nothing) and half of
// each, 0.25 / 0.2 / 0.05, on maxDense, maxExtreme and maxNuke: in both, every zone lands on one common fraction of its
// asked share (the reached sum over the asked sum), within the steering's tolerance (0.1 dB, or 3 % of the total where
// that is more). Where the whole sum cannot be met (the glue's mix at 1, the saturation's drive at its ceiling), every
// zone comes down by the same factor; each held at its own most, the zones of such a sum once all ran to their maxima
// and the cut took twice its part of what was reached.
void wholeSumKeepsRatios()
{
    felitronics::test::group ("shares summing to the whole keep their ratios where the sum cannot be met");
    const auto pcm = fixture();
    for (const std::string_view target : { "maxDense", "maxExtreme", "maxNuke" })
        for (const double k : { 1.0, 0.5 })
        {
            const double asked[] { 0.5 * k, 0.4 * k, 0.1 * k };
            auto made = loaded (pcm, target);
            const Kept* m = made && editShares (*made, 10, asked[0], asked[1], asked[2]) ? master (*made, 20) : nullptr;
            const auto* w = m && m->report->waterfall ? &*m->report->waterfall : nullptr;
            const MasterWaterfallZone none {};
            const MasterWaterfallZone* zones[] { w ? &w->glue : &none, w ? &w->saturation : &none, w ? &w->cut : &none };
            const double total = w ? w->totalDb.value_or (0.0) : 0.0;
            double reachedSum = 0.0;
            for (const auto* z : zones) reachedSum += z->reached.value_or (0.0);
            const double fraction = reachedSum / (k * 1.0);
            bool kept = total > 0.0;
            std::string line;
            for (unsigned i = 0; i < 3; ++i)
            {
                const double db = zones[i]->db.value_or (-1.0);
                kept = kept && std::fabs (fraction * asked[i] * total - db) <= std::fmax (0.1, 0.03 * total);
                line += (i ? ", " : "") + num (zones[i]->reached.value_or (-1.0)) + " (" + num (db) + " dB)";
            }
            ok (w && kept, std::string (target) + ", asked " + num (asked[0]) + "/" + num (asked[1]) + "/" + num (asked[2]) + ": reached "
                    + line + " of " + num (total) + " dB, the common fraction " + num (fraction));
        }
}

//==============================================================================
// A GLUE HELD BY ITS STEP SAYS SO: the glue's threshold by hand at −40 dB and ratio 8, asked 0.7 on maxDense — the
// steering's second look finds the glue short of its share by more than its tolerance, but the mix that meets it lies
// within 0.02 of the one in force (a mix moves only by more), so nothing moves and the steering ends after one move:
// MixStep, not Passes. At −30 dB the same share runs out of the steering's four moves: Passes there.
void glueUnderStep()
{
    felitronics::test::group ("a glue the steering holds under a mix's step stops on MixStep; one out of moves on Passes");
    const auto pcm = fixture();
    struct Row { double thresholdDb; WaterfallStop stop; bool outOfMoves; };
    for (const Row row : { Row { -40.0, WaterfallStop::MixStep, false }, Row { -30.0, WaterfallStop::Passes, true } })
    {
        auto made = loaded (pcm, "maxDense");
        GlueFields<Touched> g; g.thresholdDb = row.thresholdDb; g.ratio = 8.0;
        const bool hand = made && made->apply (command::EditDevice { 5, g }).rejection == Rejection::None;
        const Kept* k = hand && editShares (*made, 10, 0.7, std::nullopt, std::nullopt) ? master (*made, 20) : nullptr;
        const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
        const auto& z = w ? w->glue : MasterWaterfallZone {};
        const double mix = z.setting.value_or (-1.0), reached = z.reached.value_or (-1.0);
        const std::uint32_t moves = w ? w->extraPasses : 0u;
        ok (w && z.stop == row.stop && mix > 0.001 && mix < 0.999 && reached >= 0.0 && reached < 0.7 && (moves >= 4u) == row.outOfMoves,
            "threshold " + num (row.thresholdDb) + " dB, glue asked 0.700: reached " + num (reached) + ", mix " + num (mix) + ", "
                + std::to_string (moves) + " moves, stop " + std::to_string (unsigned (z.stop)) + " (want "
                + std::to_string (unsigned (row.stop)) + ")");
    }
}

//==============================================================================
// A SATURATION HELD BY ITS STEP SAYS SO: a Cubic saturation driven by hand at 12 dB (the steering never moves a person's
// drive) takes its loud places steeply at a small mix — a mix's step of 0.02 there is more than half a dB. Asked 0.06 or
// 0.10 alone on maxDense, the steering's first move lands the mix near the want; its next look finds the take off by more
// than its tolerance, but the mix that meets it lies within 0.02 of the one in force, so nothing moves: MixStep, not
// Passes. Asked 0.20, the second move lands it: Reached.
void saturationUnderStep()
{
    felitronics::test::group ("a saturation's mix the steering holds under its step stops on MixStep");
    const auto pcm = fixture();
    struct Row { double share; WaterfallStop stop; };
    for (const Row row : { Row { 0.06, WaterfallStop::MixStep }, Row { 0.10, WaterfallStop::MixStep }, Row { 0.20, WaterfallStop::Reached } })
    {
        auto made = loaded (pcm, "maxDense");
        SaturationFields<Touched> f; f.drive = 12.0; f.type = SaturationType::Cubic;
        const bool hand = made && made->apply (command::EditDevice { 5, f }).rejection == Rejection::None;
        const Kept* k = hand && editShares (*made, 10, std::nullopt, row.share, std::nullopt) ? master (*made, 20) : nullptr;
        const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
        const auto& z = w ? w->saturation : MasterWaterfallZone {};
        const double mix = z.setting.value_or (-1.0);
        const std::uint32_t moves = w ? w->extraPasses : 0u;
        ok (w && z.stop == row.stop && mix > 0.001 && mix < 0.999 && moves < 4u,
            "Cubic at 12 dB by hand, saturation asked " + num (row.share) + ": reached " + num (z.reached.value_or (-1.0)) + " ("
                + num (z.db.value_or (-1.0)) + " dB of " + num (w ? w->totalDb.value_or (0.0) : 0.0) + "), mix " + num (mix) + ", "
                + std::to_string (moves) + " moves, stop " + std::to_string (unsigned (z.stop)) + " (want "
                + std::to_string (unsigned (row.stop)) + ")");
    }
}

//==============================================================================
// A CUT HELD BY ITS STEP SAYS SO: the cut alone on maxClean, asked 0.10 or 0.15 — the clipper's take moves in steps of
// 0.1 dB (a P95 over what it clipped), so after the first move it still reads 0.2 dB, off its want by more than the
// tolerance; the cut that meets it (the cut scaled by want / take) lies within 0.1 dB of the one in force, and a cut moves
// only by more: CutStep, not Passes.
void cutUnderStep()
{
    felitronics::test::group ("a clipper's cut the steering holds under its step stops on CutStep");
    const auto pcm = fixture();
    for (const double share : { 0.10, 0.15 })
    {
        auto made = loaded (pcm, "maxClean");
        const Kept* k = made && editShares (*made, 10, std::nullopt, std::nullopt, share) ? master (*made, 20) : nullptr;
        const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
        const auto& z = w ? w->cut : MasterWaterfallZone {};
        const double cut = z.setting.value_or (-1.0);
        const std::uint32_t moves = w ? w->extraPasses : 0u;
        ok (w && z.stop == WaterfallStop::CutStep && cut > 0.01 && moves < 4u,
            "maxClean, cut asked " + num (share) + ": reached " + num (z.reached.value_or (-1.0)) + " (" + num (z.db.value_or (-1.0))
                + " dB of " + num (w ? w->totalDb.value_or (0.0) : 0.0) + "), cut " + num (cut) + " dB, " + std::to_string (moves)
                + " moves, stop " + std::to_string (unsigned (z.stop)) + " (want " + std::to_string (unsigned (WaterfallStop::CutStep)) + ")");
    }
}

//==============================================================================
// A ZONE MET ON THE STEERING'S TOTAL SAYS SO: the glue alone on maxClean (0.15, 0.30, 0.50), maxDense (0.40) and
// maxExtreme (0.10), the cut alone on maxDense (0.25), and the saturation on maxDense at 0.25 beside the page's glue and
// cut (0.05 each) — the steering's last look finds the zone within its tolerance of its share of the total it foresees at
// the target and moves nothing more, with moves left; the delivered render's total comes out elsewhere, and the zone's
// share of it misses the asked one by more than the tolerance: TotalMoved, not Passes.
void metOnTheSteeringsTotal()
{
    felitronics::test::group ("a zone met on the total the steering foresaw stops on TotalMoved, not Passes");
    const auto pcm = fixture();
    struct Row { std::string_view target; int zone; std::optional<double> glue, saturation, cut; };
    const Row rows[] {
        { "maxClean", 0, 0.15, std::nullopt, std::nullopt }, { "maxClean", 0, 0.30, std::nullopt, std::nullopt },
        { "maxClean", 0, 0.50, std::nullopt, std::nullopt }, { "maxDense", 0, 0.40, std::nullopt, std::nullopt },
        { "maxExtreme", 0, 0.10, std::nullopt, std::nullopt }, { "maxDense", 2, std::nullopt, std::nullopt, 0.25 },
        { "maxDense", 1, 0.05, 0.25, 0.05 },
    };
    for (const auto& row : rows)
    {
        auto made = loaded (pcm, row.target);
        const Kept* k = made && editShares (*made, 10, row.glue, row.saturation, row.cut) ? master (*made, 20) : nullptr;
        const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
        const MasterWaterfallZone none {};
        const auto& z = ! w ? none : row.zone == 0 ? w->glue : row.zone == 1 ? w->saturation : w->cut;
        const double total = w ? w->totalDb.value_or (0.0) : 0.0, asked = z.asked.value_or (-1.0), db = z.db.value_or (-1.0);
        const std::uint32_t moves = w ? w->extraPasses : 0u;
        const bool missed = total > 0.0 && std::fabs (asked * total - db) > std::fmax (0.1, 0.03 * total);
        ok (w && z.stop == WaterfallStop::TotalMoved && missed && moves < 4u,
            std::string (row.target) + ", " + (row.zone == 0 ? "glue" : row.zone == 1 ? "saturation" : "cut") + " asked " + num (asked)
                + ": reached " + num (z.reached.value_or (-1.0)) + " (" + num (db) + " dB of " + num (total) + "), " + std::to_string (moves)
                + " moves, stop " + std::to_string (unsigned (z.stop)) + " (want " + std::to_string (unsigned (WaterfallStop::TotalMoved)) + ")");
    }
}

//==============================================================================
// CONVERGENCE: the steering moves the stages at most three times on the fixture (extraPasses; with a cut wish the start
// clipper's first pass, which measures the peak its threshold is cut from, is one of them), and the landing's own passes
// after them are no more than the no-wish landing's and two. A max mode's cleaner landing holds the master without the
// wishes first — that landing is the no-wish one — so its passes are counted off before the zones' landing is compared.
void convergence()
{
    felitronics::test::group ("the waterfall converges: passes within the no-wish landing's + 2");
    const auto pcm = fixture();
    for (const std::string_view target : { "spotify", "maxClean" })
    {
        auto made = loaded (pcm, target);
        if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
        Session& s = *made;
        const Kept* plain = master (s, 5);
        const std::uint32_t alone = plain && plain->landing ? plain->landing->passes : 0u;
        const Kept* k = editShares (s, 10, 0.2, 0.3, 0.2) ? master (s, 20) : nullptr;
        const std::uint32_t passes = k && k->landing ? k->landing->passes : 999u;
        const std::uint32_t extra = k && k->report->waterfall ? k->report->waterfall->extraPasses : 999u;
        const std::uint32_t phaseA = target == "maxClean" ? alone : 0u;
        ok (alone > 0 && extra <= 3u && passes >= extra + phaseA && passes - extra - phaseA <= alone + 2u,
            std::string (target) + ": the no-wish landing " + std::to_string (alone) + " passes, with three shares "
                + std::to_string (passes) + " in all, " + std::to_string (extra) + " of them the steering's moves"
                + (phaseA ? ", " + std::to_string (phaseA) + " the landing without the wishes" : std::string()));
    }
}

//==============================================================================
// CLEANER, NOT LOUDER: a max mode with a wish lands the master without the wishes first on its budget, then the zones
// at that loudness — the delivered master within toleranceLu of it. With `cleaner = false` written for the mode (a test
// config), the master's plan lands the zones on the mode's budget in one landing; the key's one reader says so.
void cleaner()
{
    felitronics::test::group ("cleaner, not louder: phase B at phase A's loudness; cleaner = false lands on the budget");
    const double tolerance = number (detail::rules().engine.find ("landing").find ("toleranceLu"));
    const auto pcm = fixture();
    {
        auto made = loaded (pcm, "maxExtreme");
        if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
        Session& s = *made;
        const Kept* k = editShares (s, 10, 0.2, 0.3, 0.2) ? master (s, 20) : nullptr;
        const auto* w = k && k->report->waterfall ? &*k->report->waterfall : nullptr;
        const double a = w && w->aloneLufs ? *w->aloneLufs : -99.0, b = k ? k->report->achievedLufs.value_or (0.0) : 0.0;
        ok (w && w->aloneLufs && w->limiterAloneDb && std::fabs (b - a) <= tolerance + 1e-9,
            "maxExtreme with three shares: alone " + num (a) + " LUFS, with the zones " + num (b) + " LUFS (toleranceLu " + num (tolerance) + ")");
        // The report describes the landing that delivered: its target phase A's loudness, its miss from there.
        const double target = k ? k->report->targetLufs : 0.0, miss = k ? k->report->missLu.value_or (-99.0) : -99.0;
        ok (k && target == a && std::fabs (miss - (b - a)) < 1e-9 && k->report->targetMet,
            "its report: target " + num (target) + " LUFS (phase A's " + num (a) + "), miss " + num (miss) + " LU, met "
                + (k && k->report->targetMet ? "yes" : "no"));
    }
    {
        // What ended the delivered landing is said where it did not land; phase A's stop only where it landed.
        using felitronics::mastering::MasteringSolveStatus; using felitronics::mastering::MasteringConstraint;
        const detail::MaxStopInputs landed { false, false, false, MasteringSolveStatus::Solved, MasteringConstraint::None };
        const detail::MaxStopInputs peaks { true, false, false, MasteringSolveStatus::Solved, MasteringConstraint::None };
        const detail::MaxStopInputs passes { false, false, false, MasteringSolveStatus::PassLimit, MasteringConstraint::None };
        ok (detail::cleanStopOf (MaxStop::Budget, false, landed) == MaxStop::Budget
                && detail::cleanStopOf (MaxStop::Budget, false, peaks) == MaxStop::TruePeak
                && detail::cleanStopOf (MaxStop::Budget, false, passes) == MaxStop::Passes
                && detail::cleanStopOf (MaxStop::Budget, true, landed) == MaxStop::Floor
                && detail::cleanStopOf (MaxStop::Budget, true, peaks) == MaxStop::TruePeak,
            "phase B landed: phase A's Budget; above the ceiling: TruePeak; out of passes: Passes; on the floor: Floor ("
                + std::to_string (unsigned (detail::cleanStopOf (MaxStop::Budget, false, peaks))) + ", "
                + std::to_string (unsigned (detail::cleanStopOf (MaxStop::Budget, false, passes))) + ")");
    }
    const auto shipped = detail::rules();
    const auto planted = detail::readRules (shipped.targets, felitronics::session::test::embedded::notCleaner.root(), shipped.geometry);
    ok (! detail::maxCleaner (planted.engine, LoudnessMode::MaxExtreme) && detail::maxCleaner (planted.engine, LoudnessMode::MaxClean)
            && detail::maxCleaner (planted.engine, LoudnessMode::MaxDense) && detail::maxCleaner (planted.engine, LoudnessMode::MaxNuke)
            && detail::maxCleaner (shipped.engine, LoudnessMode::MaxExtreme),
        "the switch's reader: extreme written false reads false, the modes without the key true");
    auto made = loaded (pcm, "maxExtreme");
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    Session& s = *made;
    ok (editShares (s, 10, 0.2, 0.3, 0.2), "PRECONDITION: three shares");
    const auto clean = detail::MasterJob::plan (s, command::Master { 90 }, s.project());
    const auto notClean = detail::MasterJob::plan (s, command::Master { 91 }, s.project(), planted);
    ok (clean.rejection == Rejection::None && clean.clean && notClean.rejection == Rejection::None && ! notClean.clean
            && notClean.request.waterfall() && notClean.request.limiterGr.limitDb == 3.0 && clean.request.limiterGr.limitDb == 3.0,
        "the shipped config lands it cleaner; extreme's cleaner = false lands the zones on its 3 dB budget in one landing");
}

//==============================================================================
// TWO CLIPPERS: the start clipper only with a cut wish and a sounding glue or saturation; [limiter.peakClipper] place
// honoured — both (shipped): the start clipper and the limiter's; start: the start one alone; limiter: the limiter's alone.
void twoClippers()
{
    felitronics::test::group ("two clippers: the start clipper with a cut wish where the glue or the saturation sounds; place honoured");
    const auto pcm = fixture();
    auto made = loaded (pcm, "spotify");
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    Session& s = *made;
    const auto start = [] (const detail::MasterPlan& p) { return p.ready.topology.startClipper && ! p.ready.params.bypassStartClipper; };
    GlueFields<Touched> glueOff; glueOff.on = false;
    SaturationFields<Touched> satOff; satOff.on = false;
    ok (s.apply (command::EditDevice { 10, glueOff }).rejection == Rejection::None
            && s.apply (command::EditDevice { 11, satOff }).rejection == Rejection::None && editShares (s, 12, std::nullopt, std::nullopt, 0.3),
        "PRECONDITION: the glue and the saturation off by hand, a cut asked");
    const auto quiet = detail::MasterJob::plan (s, command::Master { 90 }, s.project());
    ok (! sounds (quiet, true) && ! sounds (quiet, false) && ! start (quiet) && quiet.ready.params.limiter.peakClip,
        "a cut wish with neither sounding: no start clipper, the limiter's takes the cut");
    GlueFields<Touched> glueOn; glueOn.on = true;
    ok (s.apply (command::EditDevice { 20, glueOn }).rejection == Rejection::None, "PRECONDITION: the glue on");
    const auto both = detail::MasterJob::plan (s, command::Master { 91 }, s.project());
    ok (sounds (both, true) && start (both) && both.ready.params.limiter.peakClip, "a cut wish with the glue sounding, place both: both clippers");
    const auto shipped = detail::rules();
    const auto atStart = detail::MasterJob::plan (s, command::Master { 92 }, s.project(),
        detail::readRules (shipped.targets, felitronics::session::test::embedded::placeStart.root(), shipped.geometry));
    const auto atLimiter = detail::MasterJob::plan (s, command::Master { 93 }, s.project(),
        detail::readRules (shipped.targets, felitronics::session::test::embedded::placeLimiter.root(), shipped.geometry));
    ok (start (atStart) && ! atStart.ready.params.limiter.peakClip, "place start: the start clipper alone");
    ok (! start (atLimiter) && atLimiter.ready.params.limiter.peakClip, "place limiter: the limiter's clipper alone");
    LimiterFields<Mark> cutMask; cutMask.cutShare = true;
    ok (s.apply (command::RevertEdits { 30, cutMask }).rejection == Rejection::None
            && ! start (detail::MasterJob::plan (s, command::Master { 94 }, s.project())),
        "the glue sounding with no cut wish: no start clipper");
}

//==============================================================================
// THE NEEDLES' THRESHOLD AFTER THE LAST START CUT: with two clippers ("both"), the limiter's own clipper is set to take
// [limiter.peakClipper]'s cut off the loudest peak at its input, worked out from that peak as measured. A move of the
// start clipper's cut moves that peak too, and nothing foresees by how much: the steering measures it again on the pass
// after its last such move. On maxDense with the page's default shares and on maxClean with 0.2 / 0.3 / 0.2 — both
// moving the start cut — the delivered render's needles take their cut to 0.01 dB: its peak at the limiter's input less
// the threshold the clipper was given.
void needlesAfterStartCut()
{
    felitronics::test::group ("the limiter's clipper takes its configured cut after the start clipper's last move");
    struct Row { std::string_view target; double glue, saturation, cut; };
    const auto pcm = fixture();
    for (const Row row : { Row { "maxDense", 0.05, 0.20, 0.05 }, Row { "maxClean", 0.2, 0.3, 0.2 } })
    {
        auto made = loaded (pcm, row.target);
        if (! made || ! editShares (*made, 10, row.glue, row.saturation, row.cut)) { ok (false, "PRECONDITION: a measured session with three shares"); continue; }
        Session& s = *made;
        const auto plan = detail::MasterJob::plan (s, command::Master { 90 }, s.project());
        const double startCut = plan.ready.params.startClipCutDb;
        // The job is gone once its master is kept: what it held is read on its last step.
        felitronics::mastering::MasteringChainParams steered {};
        felitronics::mastering::LoudnessSolution best {};
        double peakAtZero = std::numeric_limits<double>::quiet_NaN();
        const bool asked = s.apply (command::Master { 90 }).rejection == Rejection::None;
        for (unsigned i = 0; asked && i < 4000000 && s.job() != 0; ++i)
        {
            (void) s.step (16);
            if (const auto* job = detail::Inspector::masterJob (s))
                { steered = job->search.currentParams(); best = job->result(); peakAtZero = job->search.peakClipPeakDb(); }
        }
        const bool moved = plan.ready.topology.startClipper && std::isfinite (steered.startClipCutDb) && std::fabs (steered.startClipCutDb - startCut) > 0.1;
        const double cut = best.measured.limiterMaxReconstructedPeakDb - (peakAtZero + best.preLimiterGainDb) + steered.peakClipCutDb;
        ok (moved && std::isfinite (cut) && std::fabs (cut - steered.peakClipCutDb) <= 0.01,
            std::string (row.target) + ": the start cut " + num (startCut) + " → " + num (steered.startClipCutDb)
                + " dB, the needles took " + num (cut) + " dB of their " + num (steered.peakClipCutDb));
    }
}

//==============================================================================
// MAXIMUM · NUKE: the fourth max mode (enum value 4), its target row maxNuke (−7 LUFS nominal, −1 dBTP, the mode), its
// limiter budget read from the config (7 dB), and a master planned on the row lands in it.
void nuke()
{
    felitronics::test::group ("Maximum · nuke: enum 4, its target row, its budget from the config");
    const auto r = detail::rules();
    const auto row = r.find ("maxNuke");
    const double budget = number (r.engine.find ("landing").find ("max").find ("nuke").find ("budgetDb"));
    ok (row && r.row (*row).loudnessMode == LoudnessMode::MaxNuke && r.row (*row).lufs.toDouble() == -7.0 && r.row (*row).tp.toDouble() == -1.0
            && unsigned (LoudnessMode::MaxNuke) == 4u && budget == 7.0 && detail::maxBudgetDb (r.engine, LoudnessMode::MaxNuke) == budget,
        "the row maxNuke: −7 LUFS, −1 dBTP, mode 4, budget 7 dB from [landing.max] nuke");
    const auto pcm = fixture();
    auto made = loaded (pcm, "maxNuke");
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    const auto planned = detail::MasterJob::plan (*made, command::Master { 90 }, made->project());
    ok (planned.rejection == Rejection::None && planned.loudnessMode == LoudnessMode::MaxNuke && planned.request.limiterGr.limitDb == budget,
        "a master on the row lands in nuke on its budget");
}

//==============================================================================
// THE SNAPSHOT AT THE COMMAND: two masters asked while the file is still measured, the target changed between them,
// are queued and each lands on the target it was asked with — the queue holds the project as it was at the command.
void snapshotAtTheCommand()
{
    felitronics::test::group ("the queue's snapshot at the command: two queued masters keep their own targets");
    const auto pcm = fixture();
    auto made = loaded (pcm, "spotify", false);
    if (! made) { ok (false, "PRECONDITION: a loaded session"); return; }
    Session& s = *made;
    const auto first = s.apply (command::Master { 10 });
    const auto moved = s.apply (command::SetTarget { 11, "appleMusic" });
    const auto second = s.apply (command::Master { 12 });
    ok (s.measurementJob() != 0 && first.rejection == Rejection::None && moved.rejection == Rejection::None
            && second.rejection == Rejection::None && first.job != 0 && second.job != 0 && first.job != second.job,
        "PRECONDITION: both queued while the file is measured");
    runOut (s);
    const auto r = detail::rules();
    const auto ms = s.masters();
    ok (ms.size() == 2 && ms[0].report && ms[1].report && ms[0].recipe.project.target == *r.find ("spotify")
            && ms[1].recipe.project.target == *r.find ("appleMusic") && ms[0].report->targetLufs == -14.0 && ms[1].report->targetLufs == -16.0,
        "the first on spotify (−14 LUFS), the second on appleMusic (−16 LUFS): "
            + (ms.size() == 2 && ms[0].report && ms[1].report ? num (ms[0].report->targetLufs) + ", " + num (ms[1].report->targetLufs) : std::string ("missing")));
}

//==============================================================================
// THE QUEUE AT THE FIRST ANALYSIS: right after a load a master is taken (queued), and canMaster says so; a master queued
// then is forgotten as any queued one is; and a measurement stopped before its first readings never leaves a master
// waiting for them — asked after the stop it is refused as the table says (NotMeasured), queued before it, it ends
// Failed with that code once the pump runs.
std::size_t queuedRows (const Session& s)
{
    const auto snap = s.snapshot();
    const auto& v = snap.view();
    std::size_t n = 0;
    for (std::size_t i = 0; i < v.masterJobs.count; ++i) n += v.masterJobs.items[i].state == MasterJobState::Queued ? 1u : 0u;
    return n;
}

void queueAtTheFirstAnalysis()
{
    felitronics::test::group ("the queue at the first analysis: canMaster, forget, and a stopped measurement never leaves a master waiting");
    const auto pcm = fixture();
    {
        auto made = loaded (pcm, "spotify", false);
        if (! made) { ok (false, "PRECONDITION: a loaded session"); return; }
        Session& s = *made;
        const bool takes = s.storageFor (command::Master { 3 }).rejection == Rejection::None;
        ok (takes && s.snapshot().view().canMaster, std::string ("right after the load a master is taken (queued), and canMaster says so: ")
                                                 + (takes ? "taken" : "refused") + ", canMaster " + (s.snapshot().view().canMaster ? "true" : "false"));
        const auto asked = s.apply (command::Master { 3 });
        ok (asked.rejection == Rejection::None && queuedRows (s) == 1, "PRECONDITION: a master queued during the first analysis");
        const auto forgot = s.apply (command::Forget { 4, MasterId (asked.job) });
        ok (forgot.rejection == Rejection::None && queuedRows (s) == 0,
            "forgetting it during the first analysis takes it out of the queue (rejection " + std::to_string (unsigned (forgot.rejection)) + ")");
    }
    for (const bool queuedFirst : { false, true })
    {
        auto made = loaded (pcm, "spotify", false);
        if (! made) { ok (false, "PRECONDITION: a loaded session"); return; }
        Session& s = *made;
        const auto first = queuedFirst ? s.apply (command::Master { 3 }) : Answer {};
        const bool stopped = s.apply (command::Cancel { 4, s.measurementJob() }).rejection == Rejection::None;
        const auto later = queuedFirst ? Answer {} : s.apply (command::Master { 5 });
        for (unsigned i = 0; i < 100000 && s.step (64).state != StepState::Done; ++i) {}
        const auto snap = s.snapshot();
        const auto& v = snap.view();
        bool failed = false;
        for (std::size_t i = 0; i < v.masterJobs.count; ++i)
            failed = failed || (v.masterJobs.items[i].job == first.job && v.masterJobs.items[i].state == MasterJobState::Failed);
        const bool settled = queuedFirst ? first.rejection == Rejection::None && failed : later.rejection == Rejection::NotMeasured;
        ok (stopped && settled && queuedRows (s) == 0,
            std::string (queuedFirst ? "queued, then the measurement stopped: the master ends Failed" : "the measurement stopped, then a master: refused NotMeasured")
                + " — " + std::to_string (queuedRows (s)) + " left queued, rejection "
                + std::to_string (unsigned (queuedFirst ? first.rejection : later.rejection)));
    }
}

//==============================================================================
// A QUEUE WAITING FOR THE TAKE IS NOT WORK: a master delivered and a second one queued behind it wait for the first one's
// PCM to be taken or released, which no step does — so `step` says Done (the pump contract: More while a step has work,
// Done when none does), with no unit spent and nothing published, however often it is called. The release makes the
// queued master ready: the next step starts it, and it is delivered.
void queueWaitingForTheTake()
{
    felitronics::test::group ("a queue waiting only for the take: step says Done; the release lets the next step start it");
    const auto pcm = fixture();
    auto made = loaded (pcm, "spotify");
    if (! made) { ok (false, "PRECONDITION: a measured session"); return; }
    Session& s = *made;
    const auto first = s.apply (command::Master { 3 });
    const auto second = s.apply (command::Master { 4 });
    for (unsigned i = 0; i < 4000000 && s.pendingMaster().master == 0; ++i) (void) s.step (16);
    Stepped st {};
    for (unsigned i = 0; i < 4000000; ++i) if ((st = s.step (16)).units == 0) break;
    ok (first.rejection == Rejection::None && second.rejection == Rejection::None && s.pendingMaster().job == first.job
            && queuedRows (s) == 1 && s.job() == 0,
        "PRECONDITION: the first master delivered, the second queued behind it, no job running");
    bool quiet = true;
    for (unsigned i = 0; i < 3; ++i)
    {
        const std::uint64_t revision = s.revision();
        st = s.step (16);
        quiet = quiet && st.state == StepState::Done && st.units == 0 && s.events().empty() && s.revision() == revision;
    }
    ok (quiet, std::string ("waiting for the take, step says ") + (st.state == StepState::Done ? "Done" : "More")
                   + " with " + std::to_string (st.units) + " units, " + std::to_string (s.events().size()) + " events");
    const bool released = s.releaseMaster (s.pendingMaster()) == MasterTransferStatus::Ok;
    st = s.step (16);
    const bool started = st.state == StepState::More && st.units > 0;
    for (unsigned i = 0; i < 4000000 && s.pendingMaster().master == 0; ++i) (void) s.step (16);
    ok (released && started && s.pendingMaster().job == second.job && queuedRows (s) == 0,
        std::string ("released: the next step ") + (started ? "starts the queued master" : "does not start it")
            + ", which is delivered");
}

//==============================================================================
// A MASTER WITH NO WISH IS THE CANDIDATE BEFORE THE WATERFALL, TO THE BIT: the digests below are commit 808c058's — the
// same noWishDigest, built from that commit's tree — on every target it had (maxNuke is new).
void noWishAsBefore()
{
    felitronics::test::group ("a master with no wish: 808c058's PCM to the bit, on every target");
    struct Row { std::string_view target; std::uint64_t digest; };
    static constexpr Row before[] {
        { "allStreaming", 0xe827cbdcbe481ba0ull },
        { "cdDynamic", 0x7aeaea5d052b49a5ull },
        { "club", 0xc469f78842c41d39ull },
        { "lp", 0x887fc572199b8b8cull },
        { "spotify", 0xe827cbdcbe481ba0ull },
        { "spotifyLoud", 0xe381e06fc3aa38baull },
        { "appleMusic", 0x03ad9388f6752a91ull },
        { "youtube", 0xe827cbdcbe481ba0ull },
        { "youtubeMusic", 0x682a2191780f269full },
        { "amazon", 0xe827cbdcbe481ba0ull },
        { "tidal", 0xe827cbdcbe481ba0ull },
        { "deezer", 0x753a2c3a01fe34d0ull },
        { "soundcloud", 0xe827cbdcbe481ba0ull },
        { "td1008", 0x03ad9388f6752a91ull },
        { "ebu", 0xf4d08c143dfc9dffull },
        { "distrokid", 0xe827cbdcbe481ba0ull },
        { "cdbaby", 0xe827cbdcbe481ba0ull },
        { "tunecore", 0xe827cbdcbe481ba0ull },
        { "amuse", 0xe827cbdcbe481ba0ull },
        { "feiyr", 0xe827cbdcbe481ba0ull },
        { "routenote", 0xe827cbdcbe481ba0ull },
        { "horusmusic", 0xe827cbdcbe481ba0ull },
        { "dittomusic", 0xe827cbdcbe481ba0ull },
        { "cd", 0x344dabac76d00aebull },
        { "bandcamp", 0x9e0e346579ea04abull },
        { "atsc", 0x24beb33c9219f56cull },
        { "arib", 0x24beb33c9219f56cull },
        { "op59", 0x24beb33c9219f56cull },
        { "maxClean", 0x216bcd36484ca254ull },
        { "maxDense", 0xe3284c961c48a9d2ull },
        { "maxExtreme", 0xb5e3282b4669f475ull },
    };
    for (const auto& row : before)
    {
        const std::uint64_t now = noWishDigest (row.target);
        char digest[17];
        std::snprintf (digest, sizeof digest, "%016llx", (unsigned long long) now);
        ok (now == row.digest, std::string (row.target) + ": " + digest);
    }
}

//==============================================================================
// THE PAGE'S DEFAULT SHARES SOUND AS THEY DID: each max mode with the shares the page starts it on (glue, saturation,
// cut), mastered on the fixture — the digests below were taken when the steering's total became the peak work the zones
// and the limiter share at the target (after v0.21.0). A change that moves one of them changes the sound people hear.
// The steering decides through the platform's libm (a known limit of v0.21.0), so a steered master's bits are the
// platform's: the digests hold on Apple arm64, where they were taken; elsewhere each master must still be delivered, and
// Linux is held by the release's golden check of 360 masters on gcc.
void defaultSharesAsBefore()
{
    felitronics::test::group ("the max modes with the page's default shares: their PCM to the bit");
    struct Row { std::string_view target; double glue, saturation, cut; std::uint64_t digest; };
    static constexpr Row before[] {
        { "maxClean", 0.02, 0.10, 0.01, 0xde899fc5d14b4cceull },
        { "maxDense", 0.05, 0.20, 0.05, 0x92746a94b60fe6d1ull },
        { "maxExtreme", 0.15, 0.30, 0.05, 0x3e99bfba8cecc362ull },
        { "maxNuke", 0.10, 0.40, 0.10, 0xb433bd4d97bccae5ull },
    };
    const auto pcm = fixture();
    for (const auto& row : before)
    {
        auto made = loaded (pcm, row.target);
        const std::uint64_t now = made && editShares (*made, 10, row.glue, row.saturation, row.cut) ? deliveredDigest (*made) : 0u;
        char digest[17];
        std::snprintf (digest, sizeof digest, "%016llx", (unsigned long long) now);
#if defined(__APPLE__) && defined(__aarch64__)
        const bool same = now == row.digest;
#else
        const bool same = true;
#endif
        ok (now != 0u && same, std::string (row.target) + " " + num (row.glue) + "/" + num (row.saturation) + "/" + num (row.cut) + ": " + digest);
    }
}

} // namespace

int main()
{
    shareFields();
    oneReaderPerKey();
    waterfallReport();
    zeroZone();
    driveCeiling();
    nukeDrivePastTen();
    saturationAtLargeShares();
    wholeSumKeepsRatios();
    glueUnderStep();
    saturationUnderStep();
    cutUnderStep();
    metOnTheSteeringsTotal();
    convergence();
    cleaner();
    twoClippers();
    needlesAfterStartCut();
    nuke();
    snapshotAtTheCommand();
    queueAtTheFirstAnalysis();
    queueWaitingForTheTake();
    noWishAsBefore();
    defaultSharesAsBefore();
    return felitronics::test::report();
}
