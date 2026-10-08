// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE STATES AND THE COMMANDS of felitronics::session (Commands.h, Session.h), held by a running program. Pinned:
//   * THE TABLE, CELL BY CELL: every command in every column — a command its cell takes is accepted and moves the revision
//     by one; one its cell refuses is rejected with exactly that cell's code, and the session — its state, revision,
//     project, source, job and masters — is as it was, with nothing asked of the heap. The session's own transitions
//     likewise, against Table::events.
//   * EVERY REJECTION CHANGES NOTHING: every code a command can be rejected with is produced here, each time with the
//     whole session compared before and after, and the suite fails if a code was never produced.
//   * THE ORDER OF THE CHECKS: a request wrong in several ways gets the first check's answer.
//   * THE KNOBS: every knob of every device and both target numbers accepted at both ends of the travel and one step in,
//     and rejected past either end, between two steps, and as NaN or infinity — on the field the answer names; the
//     slopes and the needles' modes.
//   * PLACEMENT: device edits refused before the first measurement ends, taken after it; the manual mode opens the panel.
//   * A CHANGE OF TARGET: its numbers replaced silently; every device edit reset; the machine's layer placed again.
//   * THE MANUAL MODE SWITCHED OFF hides the panel and keeps a person's device edits.
//   * MEMORY IS DECLARED BEFORE THE WORK (law 11d): for every command, check() says what apply() will ask the heap for,
//     and the allocation counter says it asked exactly that; check() itself, a rejection and every transition ask nothing.
//   * A MASTER KEEPS ITS RECIPE while the project moves on; a load disarms whatever ran on the old source.
//   * THE NUMBERS THE COMMANDS READ IN PLACE are the schema's, number by number, and the reading says when it is not.

#include "../../../tests/DeclaredBudget.h"   // installs the allocation counter: EVERY form of `new`, over-aligned included
#include "FpEnvironmentControl.h"
#include <felitronics/session/Snapshot.h>

#include <felitronics_test.h>
#include <felitronics/core/Config.h>
#include <felitronics/session/Commands.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>
#include <felitronics/toml/Embedded.h>

#include "Devices.h"
#include "Planner.h"
#include "Needles.h"
#include "Driver.h"
#include "Grid.h"
#include "Rules.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

using namespace felitronics::session;

// A usable loudness and true peak whose loudness need on the default target is at most 3 dB: the needles are not
// measured, so the plan ends with them.
constexpr MeasurementValue kReadyMasterNumbers[] {
    { "integratedLufs", -18.0, MeasurementReason::None, 0 },
    { "truePeakDb", -3.0, MeasurementReason::None, 0 }
};

// The state suite's own seam into a session (Session.h names it a friend; the library defines none): a session whose
// jobs have spent their ids, which only billions of masters would reach otherwise.
struct felitronics::session::detail::Inspector
{
    static void lastJob (Session& s, JobId last) noexcept { s.lastJob_ = last; }
    static void unplace (Session& s) noexcept { s.devicesPlaced_ = false; }
    static void mandatory (Session& s) noexcept
    {
        auto& result = s.measurementResults_[std::size_t (Analyzer::Loudness)];
        result.status = MeasurementStatus::Ready;
        result.numbers = kReadyMasterNumbers;
        // The low-end runs the high-pass and mono bass read end without a value — the placed columns stand for a plan
        // with nothing left to wait for of the source's runs (the needles and the tempo are endWithoutValue's).
        for (const auto a : { Analyzer::LowEnd, Analyzer::LowEnd150 })
        { s.measurementResults_[std::size_t (a)].status = MeasurementStatus::Unavailable; s.measurementResults_[std::size_t (a)].reason = MeasurementReason::NoSignal; }
        // What a finished loudness does (Driver::retain): the needles for the project's ceiling, and the plan again.
        s.requestNeedles();
        s.replan();
    }
    // The measurements a plan waits for end without a value: the tempo, and the needles job at its ceiling.
    static void endWithoutValue (Session& s) noexcept
    {
        auto& tempo = s.measurementResults_[std::size_t (Analyzer::Tempo)];
        tempo.status = MeasurementStatus::Unavailable; tempo.reason = MeasurementReason::NoSignal;
        if (s.needlesJob_ != 0)
        {
            s.needlesJob_ = 0; s.needlesWork_.reset();
            auto& needles = s.measurementResults_[std::size_t (Analyzer::Excursions)];
            needles.status = MeasurementStatus::Unavailable; needles.reason = MeasurementReason::NoSignal;
        }
        s.replan();
    }
};
using felitronics::test::ok;
using felitronics::session::detail::Driver;
namespace declared = felitronics::declared;
namespace fpenv = felitronics::session::testing;
namespace config = felitronics::session::config;

namespace
{
constexpr const char* kCommandNames[] = { "load", "setTarget", "editTarget", "editDevice", "revertEdits", "setManual",
                                          "master", "cancel", "forget", "importProject", "continueMeasurement", "adoptMachine",
                                          "gradeDamage" };
static_assert (std::size (kCommandNames) == kCommands, "a name for every command");
constexpr const char* kColumnNames[] = { "Empty", "Loaded", "Measured1", "Measured2", "Mastering1", "Mastering2", "Stopped", "StoppedMeasured", "MasteringStopped",
    "Measured1Unplaced", "Measured2Unplaced", "Mastering1Unplaced", "Mastering2Unplaced", "StoppedMeasuredUnplaced", "MasteringStoppedUnplaced" };
constexpr const char* kEventNames[] = { "Measured1", "Measured2", "Mastered" };
constexpr const char* kRejectionNames[] = { "None", "FloatingPointEnvironment", "NoSource", "NotPlaced", "NotMeasured",
    "Busy", "NoJob", "NoMaster", "UnknownTarget", "NotOffered", "UnknownJob", "UnknownMaster",
    "NotFinite", "NotOneOf", "OutOfDomain", "BadChannels", "BadRate", "NoAudio", "TooLong", "NoJobId", "InvalidUtf8" };
constexpr auto kLastRejection = Rejection::InvalidUtf8;
static_assert (std::size (kRejectionNames) == std::size_t (kLastRejection) + 1, "a name for every rejection");

std::string nameOf (Rejection r)
{
    return std::size_t (r) < std::size (kRejectionNames) ? kRejectionNames[std::size_t (r)] : "?";
}

std::set<Rejection>& covered()
{
    static std::set<Rejection> s;
    return s;
}
bool g_environmentReachable = false;   // this row can set a rounding mode, so the entry check was driven

//==============================================================================
// AUDIO — deterministic, planar, owned here; the session copies it.

struct Audio
{
    std::vector<std::vector<float>> planes;
    std::vector<const float*> pointers;
    Pcm pcm {};
};

Audio makeAudio (std::uint32_t channels, std::uint64_t frames, std::uint32_t rate = 48000, std::uint64_t seed = 0)
{
    Audio a;
    a.planes.resize (channels);
    for (std::uint32_t c = 0; c < channels; ++c)
    {
        a.planes[c].resize (frames);
        for (std::uint64_t i = 0; i < frames; ++i)
            a.planes[c][i] = float (int ((i * 7 + c * 13 + seed * 5) % 200) - 100) / 128.0f;
    }
    for (auto& p : a.planes) a.pointers.push_back (p.data());
    a.pcm = { a.pointers.data(), channels, frames, rate };
    return a;
}

//==============================================================================
// WHAT THE SESSION HOLDS, compared whole — every field of every device's two layers, by bits.

template <class T> bool same (const T& a, const T& b) { return a == b; }
bool same (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
template <class T> bool same (const std::optional<T>& a, const std::optional<T>& b)
{
    return a.has_value() == b.has_value() && (! a || same (*a, *b));
}

template <class F> bool sameFields (const F& a, const F& b)
{
    bool eq = true;
    detail::DeviceOf<F>::each (detail::rules(), [&] (std::uint8_t, const detail::FieldRule&, const auto& x, const auto& y)
    {
        eq = eq && same (x, y);
    }, a, b);
    return eq;
}
template <class L> bool sameLayers (const L& a, const L& b) { return sameFields (a.machine, b.machine) && sameFields (a.hand, b.hand); }

bool sameMachine (const Devices& a, const Devices& b)
{
    return sameFields (a.hpf.machine, b.hpf.machine) && sameFields (a.monoBass.machine, b.monoBass.machine)
        && sameFields (a.glue.machine, b.glue.machine) && sameFields (a.saturation.machine, b.saturation.machine)
        && sameFields (a.tilt.machine, b.tilt.machine) && sameFields (a.limiter.machine, b.limiter.machine)
        && sameFields (a.dither.machine, b.dither.machine) && sameFields (a.low.machine, b.low.machine);
}

bool handsEmpty (const Devices& d)
{
    bool empty = true;
    detail::eachDevice (d, [&] (Device, const auto& layers)
    {
        using Hand = std::remove_cvref_t<decltype (layers.hand)>;
        empty = empty && sameFields (layers.hand, Hand {});
    });
    return empty;
}

bool sameProject (const Project& a, const Project& b)
{
    return a.target == b.target && same (a.targetEdit.lufs, b.targetEdit.lufs) && same (a.targetEdit.tp, b.targetEdit.tp)
        && a.manual == b.manual && sameLayers (a.devices.hpf, b.devices.hpf)
        && sameLayers (a.devices.monoBass, b.devices.monoBass) && sameLayers (a.devices.glue, b.devices.glue)
        && sameLayers (a.devices.saturation, b.devices.saturation) && sameLayers (a.devices.tilt, b.devices.tilt)
        && sameLayers (a.devices.limiter, b.devices.limiter) && sameLayers (a.devices.dither, b.devices.dither)
        && sameLayers (a.devices.low, b.devices.low);
}

bool sameRecipe (const Recipe& a, const Recipe& b)
{
    return sameProject (a.project, b.project) && a.source == b.source && a.sound == b.sound;
}

struct Seen
{
    State state {};
    bool mastering = false;
    Column column {};
    std::uint64_t revision = 0;
    Project project {};
    Source source {};
    std::string name;
    JobId job = 0;
    Recipe jobRecipe {};
    std::vector<Kept> masters;
};

Seen seen (const Session& s)
{
    Seen x;
    x.state = s.state();
    x.mastering = s.mastering();
    x.column = s.column();
    x.revision = s.revision();
    x.project = s.project();
    x.source = s.source();
    x.name = std::string (s.source().name);
    x.job = s.job();
    x.jobRecipe = s.jobRecipe();
    x.masters.assign (s.masters().begin(), s.masters().end());
    return x;
}

bool sameSeen (const Seen& a, const Seen& b)
{
    bool eq = a.state == b.state && a.mastering == b.mastering && a.column == b.column && a.revision == b.revision
           && sameProject (a.project, b.project) && a.source.channels == b.source.channels
           && a.source.sampleRate == b.source.sampleRate && a.source.frames == b.source.frames
           && a.source.hash == b.source.hash && a.source.fileRate == b.source.fileRate
           && a.source.rateKnown == b.source.rateKnown && a.source.bitDepth == b.source.bitDepth && a.name == b.name
           && a.job == b.job && sameRecipe (a.jobRecipe, b.jobRecipe) && a.masters.size() == b.masters.size();
    for (std::size_t i = 0; eq && i < a.masters.size(); ++i)
        eq = a.masters[i].id == b.masters[i].id && sameRecipe (a.masters[i].recipe, b.masters[i].recipe);
    return eq;
}

//==============================================================================
// SESSIONS AND REQUESTS

std::unique_ptr<Session> fresh()
{
    auto c = Session::create();
    ok (c.status == Status::Ok && c.session != nullptr, "PRECONDITION: a session");
    return std::move (c.session);
}

bool accepted (Session& s, const Request& r) { return s.apply (r).rejection == Rejection::None; }

command::Load loadOf (const Audio& a, CommandId id = 1, std::string_view name = "mix.wav")
{
    return { id, a.pcm, { name, a.pcm.sampleRate, true, 24 } };
}

template <class F> command::EditDevice editOf (const F& fields, CommandId id = 1) { return { id, DeviceEdit (fields) }; }
template <class F> command::RevertEdits revertOf (const F& mask, CommandId id = 1) { return { id, DeviceMask (mask) }; }

HpfFields<Touched> hpfFq (double fq) { HpfFields<Touched> f; f.fq = fq; return f; }

// A session standing in `column`: a stereo source where the column has one, one master kept where masters can be, a
// master being made in the Mastering columns — and the manual mode on (unless asked otherwise), switched on last: a load
// switches it off.
struct Situation
{
    std::unique_ptr<Session> s;
    Audio audio;
    JobId job = 0;
    MasterId kept = 0;
    ProjectText projectText;
};

Situation situation (Column column, bool manual = true, const char* target = nullptr, std::uint32_t channels = 2)
{
    if (column >= Column::Measured1Unplaced)
    {
        constexpr Column placed[] { Column::Measured1, Column::Measured2, Column::Mastering1, Column::Mastering2, Column::StoppedMeasured, Column::MasteringStopped };
        auto x = situation (placed[std::size_t (column) - std::size_t (Column::Measured1Unplaced)], manual, target, channels);
        detail::Inspector::unplace (*x.s);
        ok (x.s->column() == column, "unplaced situation selects its explicit table column");
        return x;
    }
    Situation x { fresh(), makeAudio (channels, 4800) };
    Session& s = *x.s;
    bool good = true;
    if (target != nullptr) good = good && accepted (s, command::SetTarget { 101, target });
    if (column != Column::Empty)
    {
        good = good && accepted (s, loadOf (x.audio, 102));
        if (column != Column::Loaded && column != Column::Stopped)
        {
            good = good && Driver::measured1 (s, s.measurementJob(), s.source().hash);
            detail::Inspector::mandatory (s);
            if (column == Column::Measured2 || column == Column::Mastering2)
                good = good && Driver::measured2 (s, s.measurementJob(), s.source().hash);
            const Answer m = s.apply (command::Master { 103 });
            good = good && m.rejection == Rejection::None && Driver::mastered (s, s.job());
            x.kept = m.job;
            if (column == Column::Mastering1 || column == Column::Mastering2 || column == Column::MasteringStopped)
            {
                const Answer j = s.apply (command::Master { 104 });
                good = good && j.rejection == Rejection::None;
                x.job = j.job;
            }
        }
    }
    if (column == Column::Stopped || column == Column::StoppedMeasured || column == Column::MasteringStopped)
        good = good && accepted (s, command::Cancel { 105, s.measurementJob() });
    if (manual) good = good && accepted (s, command::SetManual { 100, true });
    ok (good && s.column() == column, std::string ("PRECONDITION: the session stands in ") + kColumnNames[std::size_t (column)]);
    x.projectText = s.exportProject();
    return x;
}

// A request of each command that passes every check after the table in any column the table lets it into.
Request validRequest (Command c, const Situation& x, CommandId id)
{
    switch (c)
    {
        case Command::Load:        return command::Load { id, x.audio.pcm, { "other.wav", 44100, true, 16 } };
        case Command::SetTarget:   return command::SetTarget { id, "lp" };
        case Command::EditTarget:  { command::EditTarget e { id, {} }; e.fields.lufs = -13.0; return e; }
        case Command::EditDevice:  return editOf (hpfFq (36.0), id);
        case Command::RevertEdits: { HpfFields<Mark> m {}; m.fq = true; return revertOf (m, id); }
        case Command::SetManual:   return command::SetManual { id, true };
        case Command::Master:      return command::Master { id };
        case Command::Cancel:      return command::Cancel { id, x.job != 0 ? x.job : x.s->measurementJob() };
        case Command::Forget:      return command::Forget { id, x.kept };
        case Command::ContinueMeasurement: return command::ContinueMeasurement { id };
        case Command::ImportProject: return command::ImportProject { id, x.projectText.view() };
        case Command::AdoptMachine: return command::AdoptMachine { id };
        case Command::GradeDamage: return command::GradeDamage { id, x.kept };
    }
    return command::Master { id };
}

// A request that must be rejected with `want`, on `field`, having changed nothing and asked the heap for nothing — and
// check() must say so first.
void rejectedWhole (Session& s, const Request& r, Rejection want, std::uint8_t field, const std::string& what)
{
    const Seen before = seen (s);
    const Checked checked = s.check (r);
    Answer a;
    const declared::Spent spent = declared::spend ([&] { a = s.apply (r); });
    ok (a.rejection == want, what + ": rejected as " + nameOf (want) + " (got " + nameOf (a.rejection) + ")");
    ok (a.field == field, what + ": on field " + std::to_string (field) + " (got " + std::to_string (a.field) + ")");
    ok (checked.rejection == want && checked.field == field && checked.bytes == 0,
        what + ": and check() said so first, with no bytes");
    ok (a.revision == before.revision && sameSeen (before, seen (s)), what + ": and nothing changed, the revision included");
    ok (spent.requests == 0, what + ": and nothing was asked of the heap");
    covered().insert (want);
}

//==============================================================================

void theDefaultsAtCreate()
{
    felitronics::test::group ("create: Empty, revision 0, the default target, the manual mode off, the devices unplaced");
    auto s = fresh();
    const auto loaded = config::Config::load();
    ok (s->state() == State::Empty && ! s->mastering() && s->column() == Column::Empty && s->revision() == 0,
        "Empty, no master being made, revision 0");
    ok (s->targetName() == loaded.config.targets.defaultTarget, "on the config's default target (" + std::string (s->targetName()) + ")");
    ok (! s->project().manual && handsEmpty (s->project().devices) && ! s->project().targetEdit.lufs && ! s->project().targetEdit.tp,
        "the manual mode off, no edit of a person's anywhere");
    ok (s->source().channels == 0 && s->job() == 0 && s->masters().empty(), "no source, no job, no master");
    ok (sameMachine (s->project().devices, Devices {}), "the devices unplaced: the machine's layer at its types' zeros");
    Audio a = makeAudio (2, 480);
    ok (accepted (*s, loadOf (a)) && sameMachine (s->project().devices, Devices {}), "and still after a load");
    ok (accepted (*s, command::SetTarget { 2, "lp" }) && sameMachine (s->project().devices, Devices {})
            && accepted (*s, command::SetTarget { 3, "allStreaming" }),
        "and after a change of target while the first measurement runs");
    ok (Driver::measured1 (*s, s->measurementJob(), s->source().hash), "the first measurement ends");
    const auto& d = s->project().devices;
    // allStreaming: the 32 Hz floor (no low end measured here), 24 dB/oct, mono bass at 120 Hz, a peak clipper, 24-bit delivery.
    ok (same (d.hpf.machine.fq, 32.0) && d.hpf.machine.slope == 24 && d.hpf.machine.on, "the high-pass: 32 Hz, 24 dB/oct, on");
    ok (same (d.monoBass.machine.fq, 120.0) && same (d.monoBass.machine.width, 0.0), "mono bass: 120 Hz, width 0");
    ok (d.limiter.machine.needles == Needles::Auto && same (d.limiter.machine.needlesDb, 1.5), "the needles: auto, 1.5 dB");
    ok (! d.dither.machine.on && ! d.low.machine.on, "no dither at 24 bits, no low shelf off vinyl");
    ok (same (d.glue.machine.upToDb, 0.0) && same (d.tilt.machine.db, 0.0), "no glue by default, a flat tilt");
}

void everyCellOfTheTable()
{
    felitronics::test::group ("the table, cell by cell: taken where its cell says None, rejected whole with its cell's code elsewhere");
    int cells = 0;
    for (std::size_t c = 0; c < kCommands; ++c)
        for (std::size_t col = 0; col < kColumns; ++col)
        {
            Situation x = situation (Column (col));
            Session& s = *x.s;
            const Request r = validRequest (Command (c), x, 1000 + c);
            // Measured2 and the stopped columns admit needles cancellation (needles may outlive a stopped measurement);
            // this fixture has no active job.
            const Rejection cell = Command (c) == Command::Cancel && (Column (col) == Column::Measured2 || Column (col) == Column::Stopped
                || Column (col) == Column::StoppedMeasured
                || Column (col) == Column::Measured2Unplaced || Column (col) == Column::StoppedMeasuredUnplaced)
                ? Rejection::NoJob : Table::commands[c].cell[col];
            const std::string what = std::string (kCommandNames[c]) + " in " + kColumnNames[col];
            // A master this fixture keeps was not delivered by a job: past the table, its damage is not gradable.
            if (cell == Rejection::None && Command (c) == Command::GradeDamage)
                rejectedWhole (s, r, Rejection::DamageSettled, kNoField, what);
            else if (cell == Rejection::None)
            {
                // adoptMachine with no difference to take is accepted as an empty edit is: without a revision.
                const std::uint64_t moves = Command (c) == Command::AdoptMachine && s.snapshot().view().machineDifferences.empty() ? 0 : 1;
                const std::uint64_t before = s.revision();
                const Answer a = s.apply (r);
                ok (a.rejection == Rejection::None && a.revision == before + moves && s.revision() == before + moves
                        && a.command == 1000 + c,
                    what + ": accepted, the revision moved by " + std::to_string (moves) + ", the id given back (" + nameOf (a.rejection) + ")");
            }
            // THE MASTERS' QUEUE: a master is never refused for its timing. Where its cell says it cannot start now — before
            // the first measurement ended or with it stopped, before the devices are placed, while another is made — it is
            // taken with a job of its own and listed Queued, the job running (if any) untouched.
            else if (Command (c) == Command::Master && Column (col) != Column::Empty)
            {
                const JobId running = s.job();
                const Answer a = s.apply (r);
                bool listed = false;
                const auto jobs = s.snapshot().view().masterJobs;
                for (const auto& row : std::span (jobs.items.data(), jobs.count))
                    listed = listed || (row.job == a.job && row.state == MasterJobState::Queued);
                ok (a.rejection == Rejection::None && a.job != 0 && a.job != running && s.job() == running && listed
                        && a.command == 1000 + c,
                    what + ": queued, where its cell says " + nameOf (cell) + " (" + nameOf (a.rejection) + ")");
            }
            else
                rejectedWhole (s, r, cell, kNoField, what);
            ++cells;
        }
    ok (cells == int (kCommands * kColumns), std::to_string (cells) + " cells");
}

void everyCellOfTheEvents()
{
    felitronics::test::group ("the session's own transitions, cell by cell against Table::events");
    for (std::size_t e = 0; e < kEvents; ++e)
        for (std::size_t col = 0; col < kColumns; ++col)
        {
            Situation x = situation (Column (col));
            Session& s = *x.s;
            const Seen before = seen (s);
            bool done = false;
            const declared::Spent spent = declared::spend ([&]
            {
                done = e == 0 ? Driver::measured1 (s, s.measurementJob(), s.source().hash)
                     : e == 1 ? Driver::measured2 (s, s.measurementJob(), s.source().hash)
                              : Driver::mastered (s, s.job());
            });
            const bool cell = Table::events[e].cell[col];
            const std::string what = std::string (kEventNames[e]) + " in " + kColumnNames[col];
            ok (done == cell && spent.requests == 0, what + (cell ? ": happens" : ": does not happen") + ", asking the heap for nothing");
            if (! done) { ok (sameSeen (before, seen (s)), what + ": nothing changed"); continue; }
            ok (s.revision() == before.revision + 1, what + ": the revision moved by one");
            if (e == 0) ok (s.state() == State::Measured1, what + ": Measured1");
            if (e == 1) ok (s.state() == State::Measured2 && s.mastering() == before.mastering, what + ": Measured2, the overlay as it was");
            if (e == 2)
                ok (! s.mastering() && s.job() == 0 && s.masters().size() == before.masters.size() + 1
                        && s.masters().back().id == before.job && sameRecipe (s.masters().back().recipe, before.jobRecipe)
                        && s.state() == before.state,
                    what + ": the master kept under its job's id with its recipe, the overlay ended");
        }
}

void placement()
{
    felitronics::test::group ("device edits wait for the devices to be placed");
    Situation x = situation (Column::Loaded);
    Session& s = *x.s;
    rejectedWhole (s, editOf (hpfFq (36.0)), Rejection::NotPlaced, kNoField, "an edit while the first measurement runs");
    HpfFields<Mark> fq {};
    fq.fq = true;
    rejectedWhole (s, revertOf (fq), Rejection::NotPlaced, kNoField, "a revert while the first measurement runs");
    ok (sameMachine (s.project().devices, Devices {}), "the devices unplaced while the first measurement runs");
    ok (Driver::measured1 (s, s.measurementJob(), s.source().hash) && s.state() == State::Measured1, "the first measurement ends: Measured1");
    Devices expected {};
    detail::PlanInputs in;
    in.rules = detail::rules(); in.row = s.project().target; in.channels = s.source().channels; in.sampleRate = s.source().sampleRate;
    detail::placeMachine (in, expected);
    ok (sameMachine (expected, s.project().devices) && ! sameMachine (Devices {}, s.project().devices),
        "the devices placed then, on the config's numbers for the target and the source — and nothing placed them before");
    const Answer a = s.apply (editOf (hpfFq (36.0)));
    ok (a.rejection == Rejection::None && s.project().devices.hpf.hand.fq && same (*s.project().devices.hpf.hand.fq, 36.0),
        "after it an edit is taken, into a person's layer");
    ok (same (s.project().devices.hpf.machine.fq, 32.0), "and the machine's layer is as it was");
    ok (accepted (s, revertOf (fq)) && ! s.project().devices.hpf.hand.fq, "a revert takes it back");

    Situation y = situation (Column::Measured1, false);
    ok (accepted (*y.s, editOf (hpfFq (36.0))), "an edit with the panel hidden");
    ok (accepted (*y.s, revertOf (fq)), "a revert with the panel hidden");
}

void aChangeOfTarget()
{
    felitronics::test::group ("setTarget: the target's numbers replaced silently; every device edit reset");
    Situation x = situation (Column::Measured1);
    Session& s = *x.s;
    command::EditTarget lufs { 1, {} };
    lufs.fields.lufs = -13.5;             // a loudness need of 2.5 dB: no needles to measure, the plan stays Ready
    MonoBassFields<Touched> width {};
    width.width = 0.5;
    ok (accepted (s, lufs) && accepted (s, editOf (hpfFq (36.0))) && accepted (s, editOf (width)), "PRECONDITION: three edits");

    ok (accepted (s, command::SetTarget { 2, "lp" }) && s.targetName() == "lp", "setTarget lp");
    const Project& p = s.project();
    ok (! p.targetEdit.lufs && ! p.targetEdit.tp, "the target's numbers are lp's: the edit of its loudness is gone");
    ok (handsEmpty (p.devices) && s.snapshot().view().handFieldCount == 0,
        "every device edit is reset, including the snapshot count");
    ok (same (p.devices.hpf.machine.fq, 32.0) && p.devices.hpf.machine.slope == 12 && same (p.devices.monoBass.machine.fq, 150.0)
            && p.devices.limiter.machine.needles == Needles::Off && p.devices.low.machine.on
            && same (p.devices.low.machine.db, 0.5) && ! p.devices.dither.machine.on,
        "the machine's layer placed again for vinyl: 32 Hz at 12 dB/oct, mono bass at 150 Hz, no needles, the +0.5 dB shelf");

    LowFields<Touched> shelf {};
    shelf.db = 1.0;
    ok (accepted (s, editOf (shelf)), "the low shelf is offered on lp, and edited");
    ok (accepted (s, command::SetTarget { 3, "allStreaming" }), "setTarget allStreaming");
    ok (handsEmpty (s.project().devices), "low edits reset across targets");
    ok (accepted (s, editOf (shelf)), "low is offered on streaming");

    ok (accepted (s, command::SetTarget { 4, "cd" }), "setTarget cd");
    ok (handsEmpty (s.project().devices), "a person's device edits are gone");
    ok (s.project().devices.dither.machine.on && same (s.project().devices.glue.machine.upToDb, 2.6),
        "and the machine dithers cd's 16 bits and glues it up to 2.6 dB");
    DitherFields<Touched> dither {};
    dither.on = false;
    ok (s.snapshot().view().plan.status == PlanStatus::Pending && ! s.snapshot().view().plan.readOnly && accepted (s, editOf (dither)),
        "cd's glue reads the tempo and its peak clipper the needles: the plan waits for them, the panel takes edits meanwhile (owner, 02.10)");
    detail::Inspector::endWithoutValue (s);
    ok (accepted (s, editOf (dither)), "the dither is offered at 16 bits");
    rejectedWhole (s, command::SetTarget { 5, "nowhere" }, Rejection::UnknownTarget, kNoField, "an unknown target");
    ok (accepted (s, command::SetTarget { 7, "allStreaming" }) && ! s.project().devices.dither.hand.on,
        "the dither's false edit resets where dither is not offered (24 bits)");
    rejectedWhole (s, editOf (dither), Rejection::NotOffered, kNoField, "the dither at 24 bits");
}

void theManualModeOff()
{
    felitronics::test::group ("setManual(false) hides the panel and preserves device edits");
    Situation x = situation (Column::Measured2);
    Session& s = *x.s;
    command::EditTarget tp { 1, {} };
    tp.fields.tp = -2.0;
    TiltFields<Touched> tilt {};
    tilt.db = 1.5;
    ok (accepted (s, tp) && accepted (s, editOf (hpfFq (40.0))) && accepted (s, editOf (tilt)), "PRECONDITION: edits");
    const Project before = s.project();
    ok (accepted (s, command::SetManual { 2, false }), "the manual mode off");
    ok (! handsEmpty (s.project().devices), "device edits remain while hidden");
    ok (sameMachine (before.devices, s.project().devices), "the machine's layer stays as it was");
    ok (s.project().targetEdit.tp && same (*s.project().targetEdit.tp, -2.0), "and the edit of the target's ceiling stays");
    ok (accepted (s, editOf (tilt)), "an edit after hiding");
    ok (accepted (s, command::SetManual { 3, true }) && ! handsEmpty (s.project().devices), "on again: the panel opens with its edits");
}

// Every knob of `F`'s device: accepted at both ends and one step in, rejected past the ends, between two steps and as
// NaN or infinity — each on its own field.
template <class F> void knobsOf (Session& s, const std::string& device)
{
    const detail::Rules rules = detail::rules();
    detail::DeviceOf<F>::each (rules, [&] (std::uint8_t i, const detail::FieldRule& rule, const auto&)
    {
        if (rule.kind != detail::FieldRule::Kind::Knob) return;
        auto edit = [&] (double v)
        {
            F f {};
            detail::DeviceOf<F>::each (rules, [&] (std::uint8_t j, const detail::FieldRule&, auto& field)
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype (field)>, std::optional<double>>)
                    if (j == i) field = v;
            }, f);
            return editOf (f);
        };
        const auto at = [&] (std::int64_t steps)   // from + steps · step, as a decimal
        {
            const int scale = std::max (int (rule.knob.from.scale), int (rule.knob.step.scale));
            std::int64_t from = 0, step = 0;
            detail::scaleUp (rule.knob.from.mantissa, scale - rule.knob.from.scale, from);
            detail::scaleUp (rule.knob.step.mantissa, scale - rule.knob.step.scale, step);
            return detail::Decimal { from + steps * step, std::uint8_t (scale), false }.toDouble();
        };
        const auto between = [&]   // half a step in: one more decimal place
        {
            const int scale = std::max (int (rule.knob.from.scale), int (rule.knob.step.scale)) + 1;
            std::int64_t from = 0, step = 0;
            detail::scaleUp (rule.knob.from.mantissa, scale - rule.knob.from.scale, from);
            detail::scaleUp (rule.knob.step.mantissa, scale - rule.knob.step.scale, step);
            return detail::Decimal { from + step / 2, std::uint8_t (scale), false }.toDouble();
        };
        const std::string what = device + " field " + std::to_string (i);
        for (const double v : { rule.knob.from.toDouble(), rule.knob.to.toDouble(), at (1) })
        {
            const Answer a = s.apply (edit (v));
            ok (a.rejection == Rejection::None, what + " = " + std::to_string (v) + ": taken (" + nameOf (a.rejection) + ")");
        }
        const bool nyquist = rule.knob.domain == detail::Knob::Domain::SourceNyquist;
        // A step past the domain's end, or a tenth for a stepless knob (owner, 07.10: step 0).
        const double past = rule.knob.step.mantissa != 0 ? rule.knob.step.toDouble() : 0.1;
        const double lower = nyquist ? 0 : rule.knob.minimum.toDouble() - past;
        const double upper = nyquist ? double (s.source().sampleRate) / 2 : rule.knob.maximum.toDouble() + past;
        rejectedWhole (s, edit (lower), Rejection::OutOfDomain, i, what + " below domain");
        rejectedWhole (s, edit (upper), Rejection::OutOfDomain, i, what + " above domain");
        if (! nyquist)
            ok (accepted (s, edit (rule.knob.minimum.toDouble())) && accepted (s, edit (rule.knob.maximum.toDouble())), what + " domain endpoints");
        else ok (accepted (s, edit (0.001)) && accepted (s, edit (upper - 0.5)), what + " open Nyquist domain");
        ok (accepted (s, edit (between())), what + " half a slider step is accepted");
        rejectedWhole (s, edit (std::numeric_limits<double>::quiet_NaN()), Rejection::NotFinite, i, what + " NaN");
        rejectedWhole (s, edit (std::numeric_limits<double>::infinity()), Rejection::NotFinite, i, what + " +inf");
        rejectedWhole (s, edit (-std::numeric_limits<double>::infinity()), Rejection::NotFinite, i, what + " -inf");
    }, F {});
}

void negativeZero()
{
    felitronics::test::group ("-0 is written as +0: one value on every knob, and a recipe is compared by its bits");
    Situation x = situation (Column::Measured1);
    Situation y = situation (Column::Measured1);
    TiltFields<Touched> minus {}, plus {};
    minus.db = -0.0;
    plus.db = 0.0;
    ok (accepted (*x.s, editOf (minus)) && accepted (*y.s, editOf (plus)), "tilt edited to -0 in one session, to +0 in the other");
    ok (std::bit_cast<std::uint64_t> (*x.s->project().devices.tilt.hand.db) == 0, "-0 is kept as +0");
    ok (sameProject (x.s->project(), y.s->project()), "and the two projects are one, bit for bit");
    const Answer a = x.s->apply (command::Master { 1 });
    const Answer b = y.s->apply (command::Master { 1 });
    ok (a.rejection == Rejection::None && b.rejection == Rejection::None && sameRecipe (x.s->jobRecipe(), y.s->jobRecipe()),
        "so the two masters' recipes are equal");
}

void theKnobs()
{
    felitronics::test::group ("every knob: its travel and its step, on the field the answer names");
    Situation x = situation (Column::Measured1, true, "lp");   // vinyl: the low shelf is offered too
    Session& s = *x.s;
    knobsOf<HpfFields<Touched>> (s, "hpf");
    knobsOf<MonoBassFields<Touched>> (s, "monoBass");
    knobsOf<GlueFields<Touched>> (s, "glue");
    knobsOf<SaturationFields<Touched>> (s, "saturation");
    knobsOf<TiltFields<Touched>> (s, "tilt");
    knobsOf<LimiterFields<Touched>> (s, "limiter");
    knobsOf<LowFields<Touched>> (s, "low");

    // A value that is the correct double of a decimal on the grid is taken, however the shell computed it.
    ok (accepted (s, editOf (hpfFq (36.0))), "hpf 36 Hz");
    MonoBassFields<Touched> w {};
    w.width = 0.15;
    ok (accepted (s, editOf (w)), "mono bass width 0.15 — 0.15 has no double; its nearest double is taken as 0.15");
    volatile double step = 0.05;   // computed at run time, as a shell computes it
    w.width = step * 3.0;          // 0.15000000000000002: the double of no decimal of nine places or fewer
    ok (accepted (s, editOf (w)), "0.05 times 3 is accepted without slider quantization");

    for (const std::int32_t slope : { 6, 12, 18, 24, 36, 48, 96 })
    {
        HpfFields<Touched> f {};
        f.slope = slope;
        ok (accepted (s, editOf (f)), "hpf slope " + std::to_string (slope) + " taken");
    }
    for (const std::int32_t slope : { 0, 7, 95, -24, 102 })
    {
        HpfFields<Touched> f {};
        f.slope = slope;
        rejectedWhole (s, editOf (f), Rejection::NotOneOf, 2, "hpf slope " + std::to_string (slope));
    }
    for (const Needles n : { Needles::Auto, Needles::Manual, Needles::Off })
    {
        LimiterFields<Touched> f {};
        f.needles = n;
        ok (accepted (s, editOf (f)), "needles mode " + std::to_string (int (n)) + " taken");
    }
    LimiterFields<Touched> bad {};
    bad.needles = Needles (3);
    rejectedWhole (s, editOf (bad), Rejection::NotOneOf, 0, "a needles mode past Off");

    // The target's two numbers.
    const detail::Rules rules = detail::rules();
    for (std::uint8_t i = 0; i < 2; ++i)
    {
        const detail::Knob& k = i == 0 ? rules.lufs : rules.tp;
        const std::string what = i == 0 ? "target lufs" : "target tp";
        auto edit = [&] (double v)
        {
            command::EditTarget e { 1, {} };
            (i == 0 ? e.fields.lufs : e.fields.tp) = v;
            return e;
        };
        ok (accepted (s, edit (k.from.toDouble())) && accepted (s, edit (k.to.toDouble())), what + ": both ends taken");
        if (i == 0)
            for (double v : { -1000.0, 99.0, std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min() })
                ok (accepted (s, edit (v)), "any finite LUFS accepted");
        else
        {
            rejectedWhole (s, edit (-6.01), Rejection::OutOfDomain, i, "TP below domain");
            rejectedWhole (s, edit (0), Rejection::OutOfDomain, i, "TP above domain");
            ok (accepted (s, edit (-6)), "TP domain extends beyond travel");
        }
        ok (accepted (s, edit (k.from.toDouble() + k.step.toDouble() / 2)), what + " half a slider step is accepted");
        rejectedWhole (s, edit (std::numeric_limits<double>::quiet_NaN()), Rejection::NotFinite, i, what + " NaN");
    }
    ok (accepted (s, [] { command::EditTarget e { 1, {} }; e.fields.lufs = -13.4; e.fields.tp = -1.2; return e; }())
            && same (*s.project().targetEdit.lufs, -13.4) && same (*s.project().targetEdit.tp, -1.2),
        "both numbers in one edit, as given");
}

void theOrderOfTheChecks()
{
    felitronics::test::group ("the checks run in the declared order: the first that fails is the answer");
    {
        Situation x = situation (Column::Loaded, false);
        LowFields<Touched> f {};
        f.db = 99.0;
        rejectedWhole (*x.s, editOf (f), Rejection::NotPlaced, kNoField,
                       "STATE before NAMES and FIELDS: not placed, panel hidden, out of domain");
    }
    {
        Situation x = situation (Column::Measured1, false);
        LowFields<Touched> f {};
        f.db = 99.0;
        rejectedWhole (*x.s, editOf (f), Rejection::OutOfDomain, 1, "hidden panel still checks the domain");
    }
    Situation x = situation (Column::Measured1);
    Session& s = *x.s;
    {
        LimiterFields<Touched> f {};
        f.needlesDb = 99.0;
        rejectedWhole (s, editOf (f), Rejection::OutOfDomain, 1, "FIELDS checks domain");
        rejectedWhole (s, editOf (DitherFields<Touched> {}), Rejection::NotOffered, kNoField, "NAMES before an empty edit");
    }
    {
        SaturationFields<Touched> f {};
        f.drive = 99.0;
        f.mix = std::numeric_limits<double>::quiet_NaN();
        rejectedWhole (s, editOf (f), Rejection::OutOfDomain, 1, "FIELDS in the order written: the drive before the mix");
        const auto revision = s.revision();
        for (const Request request : { Request (editOf (SaturationFields<Touched> {})), Request (revertOf (SaturationFields<Mark> {})),
                                       Request (command::EditTarget { 1, {} }) })
        {
            const auto answer = s.apply (request);
            ok (answer.rejection == Rejection::None && answer.revision == revision && s.revision() == revision && s.events().empty(),
                "empty edit/revert accepted with unchanged revision and empty batch");
        }
    }
    ok (accepted (s, editOf (hpfFq (50.5))), "HPF frequency beyond travel and off step is accepted");
    {
        command::EditTarget e { 1, {} };
        e.fields.lufs = -40.05;
        e.fields.tp = std::numeric_limits<double>::quiet_NaN();
        rejectedWhole (s, e, Rejection::NotFinite, 1, "finite LUFS passes before non-finite TP");
    }
    {
        Audio three = makeAudio (2, 16);
        command::Load bad = loadOf (three);
        bad.pcm.channelCount = 3;
        bad.pcm.sampleRate = 100;
        rejectedWhole (s, bad, Rejection::BadChannels, kNoField, "AUDIO: channels before the rate");
        bad.pcm.channelCount = 2;
        bad.pcm.frames = 0;
        rejectedWhole (s, bad, Rejection::BadRate, kNoField, "AUDIO: the rate before the frames");
        bad.pcm.sampleRate = 48000;
        rejectedWhole (s, bad, Rejection::NoAudio, kNoField, "AUDIO: no frames");
    }
    {
        Situation m = situation (Column::Mastering1);
        const auto saved = fpenv::saveFpEnvironment();
        if (fpenv::setRounding (fpenv::kRoundUpward))
        {
            const Seen before = seen (*m.s);
            const Answer a = m.s->apply (command::Master { 1 });
            const bool driven = Driver::mastered (*m.s, m.s->job());
            fpenv::restoreFpEnvironment (saved);
            ok (a.rejection == Rejection::FloatingPointEnvironment && ! driven && sameSeen (before, seen (*m.s)),
                "ENTRY before STATE: a master while mastering, on a thread rounding upward, is refused for the thread; "
                "the transitions refuse it too, and nothing changed");
            covered().insert (Rejection::FloatingPointEnvironment);
            g_environmentReachable = true;
        }
        else
        {
            fpenv::restoreFpEnvironment (saved);
            std::printf ("    this row cannot round upward — the entry check is not reachable here\n");
        }
    }
    rejectedWhole (s, command::SetTarget { 1, "nowhere" }, Rejection::UnknownTarget, kNoField,
                   "an unknown target is rejected without changing the session");
}

void theOtherRejections()
{
    felitronics::test::group ("the rejections the table and the names give, each changing nothing");
    {
        Situation x = situation (Column::Measured1, true, nullptr, 1);
        MonoBassFields<Touched> f {};
        f.fq = 100.0;
        rejectedWhole (*x.s, editOf (f), Rejection::NotOffered, kNoField, "mono bass on a mono source");
    }
    {
        Situation x = situation (Column::Mastering2);
        rejectedWhole (*x.s, command::Cancel { 1, x.job + 7 }, Rejection::UnknownJob, kNoField, "cancel of another job");
        rejectedWhole (*x.s, command::Forget { 1, 999 }, Rejection::UnknownMaster, kNoField, "forget of a master never made");
        ok (accepted (*x.s, command::Cancel { 2, x.job }) && ! x.s->mastering() && x.s->job() == 0
                && x.s->state() == State::Measured2,
            "cancel of the job: the overlay ends, Measured2 stays");
        ok (accepted (*x.s, command::Forget { 3, x.kept }) && x.s->masters().empty(), "forget of the master kept");
        rejectedWhole (*x.s, command::Forget { 4, x.kept }, Rejection::UnknownMaster, kNoField, "forget of it again");
    }
    {
        auto s = fresh();
        Audio a = makeAudio (2, 64);
        command::Load l = loadOf (a);
        l.pcm.channelCount = 0;
        rejectedWhole (*s, l, Rejection::BadChannels, kNoField, "no channels");
        l.pcm.channelCount = 2;
        l.pcm.sampleRate = kMinSampleRate - 1;
        rejectedWhole (*s, l, Rejection::BadRate, kNoField, "7999 Hz");
        l.pcm.sampleRate = 44;
        rejectedWhole (*s, l, Rejection::BadRate, kNoField, "a rate in kilohertz");
        l.pcm.sampleRate = kMinSampleRate;
        l.pcm.channels = nullptr;
        rejectedWhole (*s, l, Rejection::NoAudio, kNoField, "no data");
        const float* holes[2] = { a.pointers[0], nullptr };
        l.pcm.channels = holes;
        rejectedWhole (*s, l, Rejection::NoAudio, kNoField, "a channel with no data");
        l.pcm.channels = a.pointers.data();
        l.pcm.frames = std::numeric_limits<std::uint64_t>::max() / 2;
        rejectedWhole (*s, l, Rejection::TooLong, kNoField, "more samples than memory can address");
        l.pcm.frames = 64;
        a.planes[1][40] = std::numeric_limits<float>::infinity();
        rejectedWhole (*s, l, Rejection::NotFinite, kNoField, "an infinite sample");
        a.planes[1][40] = std::numeric_limits<float>::quiet_NaN();
        rejectedWhole (*s, l, Rejection::NotFinite, kNoField, "a NaN sample");
        a.planes[1][40] = 0.0f;
        ok (accepted (*s, l) && s->state() == State::Loaded && s->source().sampleRate == kMinSampleRate,
            "and at 8000 Hz with every sample finite, taken");
    }
}

// THE MASTERS' QUEUE HAS ITS REFUSALS — never a master's timing: a full queue refuses the next master Busy, and a source
// whose first measurement ended without its mandatory readings (silence) refuses it NotMeasured, its turn never coming.
// Each refused whole.
void theQueueHasItsRefusals()
{
    felitronics::test::group ("the masters' queue: Busy when full, NotMeasured on a source with no mandatory readings");
    {
        Situation x = situation (Column::Loaded);
        Session& s = *x.s;
        bool queued = true;
        for (std::size_t i = 0; i < kMaxQueuedMasters; ++i)
        {
            const Answer a = s.apply (validRequest (Command::Master, x, CommandId (2000 + i)));
            queued = queued && a.rejection == Rejection::None && a.job != 0;
        }
        ok (queued && s.job() == 0, "PRECONDITION: " + std::to_string (kMaxQueuedMasters) + " masters queued before the first measurement ended");
        rejectedWhole (s, validRequest (Command::Master, x, 2100), Rejection::Busy, kNoField, "a master past a full queue");
    }
    {
        auto s = fresh();
        Audio a = makeAudio (2, 48000);
        for (auto& p : a.planes) std::fill (p.begin(), p.end(), 0.0f);
        ok (accepted (*s, loadOf (a)), "PRECONDITION: a second of silence loaded");
        for (unsigned i = 0; i < 100000 && s->measurementJob() != 0; ++i) (void) s->step (16);
        rejectedWhole (*s, command::Master { 3 }, Rejection::NotMeasured, kNoField,
                       "a master on silence, measured with no mandatory readings");
    }
}

void everyRejectionWasProduced()
{
    felitronics::test::group ("every rejection code was produced above, each with nothing changed");
    for (std::size_t r = 1; r <= std::size_t (kLastRejection); ++r)
    {
        const auto code = Rejection (r);
        const bool reachable = code != Rejection::FloatingPointEnvironment || g_environmentReachable;
        if (! reachable) { std::printf ("    %s: not reachable on this row\n", kRejectionNames[r]); continue; }
        ok (covered().count (code) == 1, std::string (kRejectionNames[r]) + " produced");
    }
}

void memoryIsDeclared()
{
    felitronics::test::group ("memory is declared before the work: check() covers what apply() asks the heap for, every command");
    // ...and in as many requests as the work has buffers: a load its samples and its name, a master its room — a
    // debugging standard library that gives a container a proxy of its own would show here as a request too many.
    struct Case { Command command; Column column; long long requests; };
    const Case cases[] = { { Command::Load, Column::Measured2, -1 }, { Command::SetTarget, Column::Measured1, 0 },
                           { Command::EditTarget, Column::Measured1, 0 }, { Command::EditDevice, Column::Measured1, 0 },
                           { Command::RevertEdits, Column::Measured1, 0 }, { Command::SetManual, Column::Measured1, 0 },
                           { Command::Master, Column::Measured1, -1 }, { Command::Cancel, Column::Mastering1, 0 },
                           { Command::Forget, Column::Measured2, 0 }, { Command::ImportProject, Column::Measured2, -1 } };
    for (const Case& c : cases)
    {
        Situation x = situation (c.column);
        Session& s = *x.s;
        const Request r = validRequest (c.command, x, 1);
        Checked declared;
        const declared::Spent checking = declared::spend ([&] { declared = s.check (r); });
        Answer a;
        const declared::Spent spent = declared::spend ([&] { a = s.apply (r); });
        const std::string what = kCommandNames[std::size_t (c.command)];
        ok (checking.requests == 0, what + ": check() asks the heap for nothing");
        ok (a.rejection == Rejection::None, what + ": PRECONDITION: accepted");
        ok (declared::covers (declared.bytes, spent), what + ": declared >= requested — " + declared::describe (declared.bytes, spent));
        if (c.requests >= 0)
        {
            ok (spent.bytes == (long long) declared.bytes, what + ": and exactly the declaration");
            ok (spent.requests == c.requests, what + ": in " + std::to_string (c.requests) + " request(s) (got "
                                                  + std::to_string (spent.requests) + ")");
        }
    }
    // A master the session decides declares its whole job before anything is done — the kept slot, its retained rows
    // and the render — as a master with a ready chain does.
    {
        Situation x = situation (Column::Measured1);
        Audio mono = makeAudio (1, 1000);
        const command::Load l = loadOf (mono, 1, "a name longer than any small-string buffer.wav");
        ok (x.s->check (l).bytes > 1000 * sizeof (float) + l.meta.name.size(), "a load declares samples, name and the complete measurement");
        const auto first = x.s->check (command::Master { 1 });
        ok (first.rejection == Rejection::None && first.bytes > 2 * sizeof (Kept), "a decided master declares its job, not only its kept slot");
        Answer asked;
        const declared::Spent spent = declared::spend ([&] { asked = x.s->apply (command::Master { 2 }); });
        ok (asked.rejection == Rejection::None && declared::covers (first.bytes, spent) && spent.requests > 0,
            "and what it asks the heap for is inside the declaration — " + declared::describe (first.bytes, spent));
        ok (accepted (*x.s, command::Cancel { 3, x.s->job() }), "a master asked and cancelled");
        const auto declared = x.s->check (command::Master { 4 });
        ok (declared.rejection == Rejection::None && declared.bytes < first.bytes, "the next one reuses the kept room and declares the rest");
        const declared::Spent again = declared::spend ([&] { (void) x.s->apply (command::Master { 5 }); });
        const declared::Spent done = declared::spend ([&] { (void) Driver::mastered (*x.s, x.s->job()); });
        ok (declared::covers (declared.bytes, again) && done.requests == 0 && x.s->masters().size() == 2,
            "the repeated preparation is covered and its ending asks nothing");
    }
    const declared::Spent reading = declared::spend ([] { (void) detail::rules(); });
    ok (reading.requests == 0, "reading the config in place asks for nothing");
}

void aMasterKeepsItsRecipe()
{
    felitronics::test::group ("a master renders its recipe: the project as it was when asked, while the project moves on");
    Situation x = situation (Column::Measured1);
    Session& s = *x.s;
    ok (accepted (s, editOf (hpfFq (36.0))), "PRECONDITION: hpf at 36 Hz");
    const Answer m = s.apply (command::Master { 1 });
    ok (m.rejection == Rejection::None && m.job == s.job() && m.job == x.kept + 1, "a master: a job, numbered after the last");
    ok (sameProject (s.jobRecipe().project, s.project()) && s.jobRecipe().source == s.source().hash
            && s.jobRecipe().sound == config::Config::versions().sound,
        "its recipe: the project, the source's hash, the config's sound version");
    ok (accepted (s, editOf (hpfFq (40.0))) && accepted (s, command::SetTarget { 2, "club" }),
        "the project moves on while it is made");
    ok (same (*s.jobRecipe().project.devices.hpf.hand.fq, 36.0) && s.jobRecipe().project.target != s.project().target,
        "and the recipe does not");
    ok (Driver::mastered (s, s.job()) && s.masters().back().id == m.job && same (*s.masters().back().recipe.project.devices.hpf.hand.fq, 36.0),
        "kept under its job's id, with the recipe it was asked with");
}

void jobIds()
{
    felitronics::test::group ("job ids: never 0, never twice in a session's life — the last one issued, a master is rejected");
    Situation x = situation (Column::Measured1);
    Session& s = *x.s;
    const JobId before = x.kept;
    ok (accepted (s, loadOf (x.audio, 1)) && s.state() == State::Measured1, "identical source keeps the completed first phase");
    const Answer again = s.apply (command::Master { 2 });
    ok (again.rejection == Rejection::None && again.job > before + 1,
        "the next job after a load is numbered on from the last, not again from 1 (" + std::to_string (again.job) + ")");
    ok (accepted (s, command::Cancel { 3, again.job }), "cancelled");
    detail::Inspector::lastJob (s, std::numeric_limits<JobId>::max() - 1);
    const Answer last = s.apply (command::Master { 4 });
    ok (last.rejection == Rejection::None && last.job == std::numeric_limits<JobId>::max(), "the last id is issued");
    ok (accepted (s, command::Cancel { 5, last.job }), "cancelled");
    rejectedWhole (s, command::Master { 6 }, Rejection::NoJobId, kNoField, "a master after the last id");
    auto fresh = Session::create();
    command::Load invalid; invalid.meta.name = "\x80";
    rejectedWhole (*fresh.session, invalid, Rejection::InvalidUtf8, kNoField, "invalid UTF-8 name before audio checks");
    ok (s.job() == 0 && ! s.mastering(), "and no job runs — none numbered 0");
}

void aLoadDisarms()
{
    felitronics::test::group ("load: disarm, then write — nothing of the old source stays beside the new");
    Situation x = situation (Column::Mastering2);
    Session& s = *x.s;
    command::EditTarget e { 1, {} };
    e.fields.lufs = -11.0;
    ok (accepted (s, e) && accepted (s, command::SetTarget { 2, "lp" }) && accepted (s, e)
            && accepted (s, editOf (hpfFq (44.0))),
        "PRECONDITION: a target, an edit of it, a device edit, a master kept and one being made");
    const std::uint64_t oldHash = s.source().hash;
    Audio mono = makeAudio (1, 3000, 44100, 3);
    command::Load l = loadOf (mono, 3, "second.wav");
    l.meta.bitDepth = 16;
    ok (accepted (s, l), "a new source loaded while a master is made");
    ok (s.state() == State::Loaded && ! s.mastering() && s.job() == 0 && s.masters().empty(),
        "Loaded: the master being made stopped, the masters gone");
    ok (handsEmpty (s.project().devices), "a person's device edits gone: they were about the old source");
    ok (sameMachine (s.project().devices, Devices {}), "and the devices unplaced until the new source is measured");
    ok (! s.project().manual, "the manual mode is off: it does not outlive the file");
    ok (s.targetName() == "lp" && s.project().targetEdit.lufs && same (*s.project().targetEdit.lufs, -11.0),
        "the target and its edited number stay");
    ok (s.source().channels == 1 && s.source().frames == 3000 && s.source().sampleRate == 44100 && s.source().bitDepth == 16
            && s.source().name == "second.wav" && s.source().hash != oldHash,
        "the new source, its name and its hash");
    // A load under the name the session holds: the name is copied before the old one is freed.
    command::Load again = loadOf (mono, 4, s.source().name);
    ok (accepted (s, again) && s.source().name == "second.wav", "a load under source().name keeps the name");
}

void theSourceHash()
{
    felitronics::test::group ("the source's hash: every sample's bits, the same on every row");
    const float left[] = { 0.5f, -0.25f, 1.0f };
    const float right[] = { 0.0f, -0.0f, 0.125f };
    const float* planes[] = { left, right };
    auto s = fresh();
    ok (accepted (*s, command::Load { 1, { planes, 2, 3, 48000 }, { "", 0, false, 0 } }), "PRECONDITION: loaded");
    // FNV-1a 64 over 48000, 2 (u32 LE), 3 (u64 LE), then the six samples' bits (u32 LE), left then right.
    ok (s->source().hash == 0xCE46A0EF4AE0A82Dull, "pinned: " + std::to_string (s->source().hash));
    const float flipped[] = { 0.0f, 0.0f, 0.125f };   // −0 read as +0 is another source
    const float* planes2[] = { left, flipped };
    auto t = fresh();
    ok (accepted (*t, command::Load { 1, { planes2, 2, 3, 48000 }, { "", 0, false, 0 } }) && t->source().hash != s->source().hash,
        "one sample's sign of zero moves it");
}

void theRulesAreTheSchemas()
{
    felitronics::test::group ("the numbers the commands read in place are the schema's, number by number");
    const detail::Rules r = detail::rules();
    const auto loaded = config::Config::load();
    const config::Config& c = loaded.config;
    ok (loaded.ok(), "PRECONDITION: both readings complete");
    const auto is = [] (const detail::Decimal& d, double x) { return same (d.toDouble(), x); };
    const auto knob = [&] (const detail::Knob& k, double from, double to, double step) { return is (k.from, from) && is (k.to, to) && is (k.step, step); };
    ok (knob (r.lufs, c.targets.editLufs.from, c.targets.editLufs.to, c.targets.editLufs.step)
            && knob (r.tp, c.targets.editTp.from, c.targets.editTp.to, c.targets.editTp.step), "[edit] lufs, tp");
    const auto& e = c.engine;
    ok (knob (r.hpfFq, e.hpf.hzMin, e.hpf.hzMax, e.hpf.hzStep) && same (e.hpf.hzStep, 0.0), "[hpf]: from hzMin to hzMax, stepless (owner, 06.10)");
    for (std::int32_t slope = -6; slope <= 96; ++slope)
        if (r.slope (slope) != (slope >= 6 && slope <= 96 && slope % 6 == 0))
            ok (false, "[hpf] slopes: " + std::to_string (slope));
    ok (knob (r.monoBassFq, e.monoBass.frequencyRange.min, e.monoBass.frequencyRange.max, e.monoBass.frequencyStep)
            && knob (r.monoBassWidth, e.monoBass.lowWidthRange.min, e.monoBass.lowWidthRange.max, e.monoBass.lowWidthStep)
            && is (r.monoBassWidthDefault, e.monoBass.lowWidth), "[monoBass]");
    ok (knob (r.glue, e.glue.knobMinDb, e.glue.knobMaxDb, e.glue.knobStepDb) && is (r.glueDefault, e.glue.defaultUpToDb),
        "[glue]: the knob, up to N dB, and its default");
    const auto& sat = e.saturation;
    ok (knob (r.drive, sat.driveRange.min, sat.driveRange.max, sat.driveStep) && knob (r.mix, sat.mixRange.min, sat.mixRange.max, sat.mixStep)
            && is (r.driveDefault, sat.driveDb) && is (r.mixDefault, sat.mix), "[saturation]");
    ok (knob (r.tilt, e.tilt.hard.min, e.tilt.hard.max, e.tilt.step)
            && knob (r.low, e.low.hard.min, e.low.hard.max, e.low.step), "[tilt], [low]");
    const auto& pc = e.limiter.peakClipper;
    ok (knob (r.needles, pc.manualMinDb, pc.manualMaxDb, pc.manualStepDb) && is (r.needlesDefault, pc.betweenCutDb), "[limiter.peakClipper]");
    ok (r.eq == e.stages.eq && r.monoBass == e.stages.monoBass && r.compressor == e.stages.compressor
            && r.clipper == e.stages.clipper && r.dither == e.stages.dither && r.ditherUpToBits == e.dither.onUpToBits,
        "[stages], [dither]");
    ok (r.rows == c.targets.targets.size() && r.row (r.defaultRow).key == c.targets.defaultTarget, "[targets]: the rows and the default");
    bool rows = true;
    for (std::uint16_t i = 0; i < r.rows; ++i)
    {
        const detail::TargetRow row = r.row (i);
        const config::Target& t = c.targets.targets[i];
        std::optional<double> glue;
        for (const auto& g : e.glue.byTarget)
            if (g.target == t.key) glue = g.upToDb;
        const bool eq = row.key == t.key && is (row.lufs, t.lufs) && is (row.tp, t.tp) && is (row.monoBass, t.monoBass)
                     && is (row.hpfFloor, t.hpfFloor) && row.hpfSlope == t.hpfSlopeDbPerOct && row.bitDepth == t.bitDepth
                     && row.noClipper == t.noClipper && row.lowDb.has_value() == t.lowDb.has_value()
                     && (! row.lowDb || is (*row.lowDb, *t.lowDb)) && row.glue.has_value() == glue.has_value()
                     && (! row.glue || is (*row.glue, *glue)) && r.find (t.key) == i;
        if (! eq) ok (false, "[targets] row " + t.key);
        rows = rows && eq;
    }
    ok (rows, "every row of [targets], field by field, and found by its key");
    ok (! r.find ("nowhere") && ! r.find (""), "a key no row has is found nowhere");


}

void theMachinePlacesWhatAPersonCouldSet()
{
    felitronics::test::group ("every value the machine places is one a person could set: on its knob's travel and step");
    const detail::Rules r = detail::rules();
    int placed = 0;
    for (std::uint16_t row = 0; row < r.rows; ++row)
        for (const std::uint32_t channels : { 1u, 2u })
        {
            Devices d {};
            detail::PlanInputs in;
            in.rules = r; in.row = row; in.channels = channels; in.sampleRate = 48000;
            detail::placeMachine (in, d);
            const std::string where = std::string (r.row (row).key) + (channels == 1 ? " (mono)" : " (stereo)");
            detail::eachDevice (d, [&] (Device device, const auto& layers)
            {
                using Fields = std::remove_cvref_t<decltype (layers.machine)>;
                detail::DeviceOf<Fields>::each (r, [&] (std::uint8_t i, const detail::FieldRule& rule, const auto& v)
                {
                    using T = std::remove_cvref_t<decltype (v)>;
                    bool good = true;
                    if constexpr (std::is_same_v<T, double>)
                    {
                        const auto x = detail::decimalOf (v);
                        // The glue's five (fields 3…7) are the law's numbers at the amount, not a typed value: inside
                        // their domain, at any precision.
                        const bool law = device == Device::Glue && i >= 3;
                        good = (law ? rule.knob.accepts (v, 48000) : x && detail::compare (*x, rule.knob.from) >= 0 && detail::compare (*x, rule.knob.to) <= 0
                            && (rule.knob.step.mantissa == 0 || detail::onGrid (*x, rule.knob.from, rule.knob.step)))
                            && std::bit_cast<std::uint64_t> (v) != std::bit_cast<std::uint64_t> (-0.0);
                    }
                    else if constexpr (std::is_same_v<T, std::int32_t>)
                        good = rule.kind == detail::FieldRule::Kind::Oversampling ? r.oversampling (v) : r.slope (v);
                    else if constexpr (std::is_same_v<T, Needles>)
                        good = std::uint8_t (v) <= std::uint8_t (Needles::Off);
                    if (! good)
                        ok (false, where + ": device " + std::to_string (int (device)) + " field " + std::to_string (i)
                                   + " placed off its knob");
                    ++placed;
                }, layers.machine);
            });
        }
    ok (placed > 0, std::to_string (placed) + " placed values, every one on its knob (+0, never -0)");
}

void everyFieldIsWalked()
{
    felitronics::test::group ("the code walks every field of every device (src/Devices.h against Project.h)");
    const detail::Rules r = detail::rules();
    auto count = [&] (const auto& mask)
    {
        std::size_t n = 0, next = 0;
        detail::DeviceOf<std::remove_cvref_t<decltype (mask)>>::each (r, [&] (std::uint8_t i, const detail::FieldRule&, bool)
        {
            ok (i >= next, "the fields are walked in the order written, by ascending id");
            next = std::size_t (i) + 1; ++n;
        }, mask);
        // A mask is one bool per field and nothing else, so its size counts the fields Project.h writes.
        return n == sizeof (mask);
    };
    ok (count (HpfFields<Mark> {}) && count (MonoBassFields<Mark> {}) && count (GlueFields<Mark> {})
            && count (SaturationFields<Mark> {}) && count (TiltFields<Mark> {}) && count (LimiterFields<Mark> {})
            && count (DitherFields<Mark> {}) && count (LowFields<Mark> {}),
        "every device: as many fields walked as its mask holds");
    ok (kMinSampleRate == std::uint32_t (felitronics::core::kMinSampleRate), "the lowest rate a load takes is felitronics-core's");
}
} // namespace

int main()
{
    std::printf ("felitronics session states and commands tests\n");
    theRulesAreTheSchemas();
    theMachinePlacesWhatAPersonCouldSet();
    everyFieldIsWalked();
    theDefaultsAtCreate();
    everyCellOfTheTable();
    everyCellOfTheEvents();
    placement();
    aChangeOfTarget();
    theManualModeOff();
    theKnobs();
    negativeZero();
    theOrderOfTheChecks();
    theOtherRejections();
    memoryIsDeclared();
    aMasterKeepsItsRecipe();
    jobIds();
    aLoadDisarms();
    theSourceHash();
    theQueueHasItsRefusals();
    everyRejectionWasProduced();
    return felitronics::test::report();
}
