// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE COMMANDS (Commands.h): check() runs the checks in the order Commands.h declares — the floating-point environment,
// then the table, then the rest — and says what the request would ask the heap for; apply() runs check() and, only when
// it passed, does the work. A rejection publishes an event and advances seq, but changes no state: check() is const, and the work
// starts after it. The session's own transitions (src/Driver.h) are here too, beside the table they read.

#include "BuildGuards.h"

#include "Devices.h"
#include "Driver.h"
#include "Grid.h"
#include "Rules.h"
#include "BuildContract.h"
#include "ProjectIO.h"
#include "Utf8.h"

#include <felitronics/session/Commands.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <limits>
#include <memory>
#include <utility>
#include <variant>

namespace felitronics::session
{
namespace
{
using detail::FieldRule;
using detail::Rules;

static_assert (std::variant_size_v<Request> == kCommands, "one Request alternative per command");
static_assert (std::variant_size_v<DeviceEdit> == 8 && std::variant_size_v<DeviceMask> == 8, "one alternative per device");
static_assert (std::is_same_v<std::variant_alternative_t<std::size_t (Command::Forget), Request>, command::Forget>
               && std::is_same_v<std::variant_alternative_t<std::size_t (Command::Load), Request>, command::Load>,
               "Request's alternatives are in the order of Command");
static_assert (std::is_same_v<std::variant_alternative_t<std::size_t (Device::LowShelf), DeviceEdit>, LowShelfFields<Touched>>
               && std::is_same_v<std::variant_alternative_t<std::size_t (Device::Hpf), DeviceMask>, HpfFields<Mark>>,
               "a device edit's alternatives are in the order of Device");

// The table's rows are in the order of their commands and events, so a row is found by index.
constexpr bool rowsInOrder()
{
    for (std::size_t i = 0; i < kCommands; ++i)
        if (std::size_t (Table::commands[i].command) != i) return false;
    for (std::size_t i = 0; i < kEvents; ++i)
        if (std::size_t (Table::events[i].event) != i) return false;
    return true;
}
static_assert (rowsInOrder(), "Table::commands and Table::events list their rows in the order of Command and Event");

// `new T[n]` asks for exactly n × sizeof (T) where T has a trivial destructor: no array cookie, on any ABI. The demands
// check() states count on it.
static_assert (std::is_trivially_destructible_v<Kept> && std::is_trivially_destructible_v<float>,
               "the session's owned arrays carry no cookie");

// f(the active alternative) — without std::visit, whose path for a variant left valueless by an exception throws. With
// exceptions off no variant here is ever valueless, so the last alternative is the active one when no other is.
template <std::size_t I = 0, class V, class F> decltype (auto) onActive (V& v, F&& f)
{
    if constexpr (I + 1 < std::variant_size_v<std::remove_const_t<V>>)
    {
        if (auto* p = std::get_if<I> (&v)) return f (*p);
        return onActive<I + 1> (v, std::forward<F> (f));
    }
    else
        return f (*std::get_if<I> (&v));
}

CommandId idOf (const Request& request) noexcept
{
    return onActive (request, [] (const auto& r) { return r.id; });
}

bool finite (double x) noexcept { return std::isfinite (x); }
bool finite (float x) noexcept { return (std::bit_cast<std::uint32_t> (x) & 0x7F800000u) != 0x7F800000u; }

// A knob's value: finite and within its domain, in that order.
Rejection knobCheck (const detail::Knob& knob, double x, std::uint32_t rate) noexcept
{
    if (! finite (x)) return Rejection::NotFinite;
    return knob.accepts (x, rate) ? Rejection::None : Rejection::OutOfDomain;
}

// A touched field's value, by its rule.
template <class T> Rejection fieldCheck (const Rules& rules, const FieldRule& rule, const std::optional<T>& v, std::uint32_t rate) noexcept
{
    if (! v) return Rejection::None;
    if constexpr (std::is_same_v<T, bool>)
        return Rejection::None;
    else if constexpr (std::is_same_v<T, double>)
        return knobCheck (rule.knob, *v, rate);
    else if constexpr (std::is_same_v<T, std::int32_t>)
        return rules.slope (*v) ? Rejection::None : Rejection::NotOneOf;
    else
    {
        static_assert (std::is_same_v<T, Needles>);
        return std::uint8_t (*v) <= std::uint8_t (Needles::Off) ? Rejection::None : Rejection::NotOneOf;
    }
}

Checked storage (std::uint64_t bytes, std::uint64_t block) noexcept { return { Rejection::None, kNoField, bytes, 0, block }; }

Checked rejected (Rejection r, std::uint8_t field = kNoField) noexcept { return { r, field, 0 }; }

// A device edit's touched fields, in the order written; an empty edit is accepted.
template <class Edit> Checked editCheck (const Rules& rules, const Edit& edit, std::uint32_t rate) noexcept
{
    Checked out;
    detail::DeviceOf<Edit>::each (rules, [&] (std::uint8_t i, const FieldRule& rule, const auto& v)
    {
        if (out.rejection == Rejection::None)
            if (const Rejection r = fieldCheck (rules, rule, v, rate); r != Rejection::None) out = rejected (r, i);
    }, edit);
    return out;
}

template <class Mask> bool anyMarked (const Rules& rules, const Mask& mask) noexcept
{
    bool any = false;
    detail::DeviceOf<Mask>::each (rules, [&] (std::uint8_t, const FieldRule&, bool marked) { any = any || marked; }, mask);
    return any;
}

// THE HASH OF A SOURCE — 64-bit FNV-1a over its rate, its channels, its frames and then every sample's 32 bits, channel
// after channel, each value little-endian: a number of the source, the same on every row.
struct Fnv
{
    std::uint64_t h = 0xCBF29CE484222325ull;
    void byte (std::uint8_t b) noexcept { h = (h ^ b) * 0x100000001B3ull; }
    void u32 (std::uint32_t v) noexcept { for (int i = 0; i < 4; ++i) byte (std::uint8_t (v >> (8 * i))); }
    void u64 (std::uint64_t v) noexcept { for (int i = 0; i < 8; ++i) byte (std::uint8_t (v >> (8 * i))); }
};

// A person's layers, taken back: all of them, or those of the devices the target and the source do not offer.
void clearHands (Devices& devices) noexcept
{
    detail::eachDevice (devices, [] (Device, auto& layers) { layers.hand = {}; });
}
void clearHandsNotOffered (const Rules& rules, std::uint16_t row, std::uint32_t channels, Devices& devices) noexcept
{
    detail::eachDevice (devices, [&] (Device d, auto& layers)
    {
        if (! detail::offered (rules, row, channels, d)) layers.hand = {};
    });
}

std::size_t index (Command c) noexcept { return std::size_t (c); }
std::size_t index (Column c) noexcept { return std::size_t (c); }
std::size_t index (Event e) noexcept { return std::size_t (e); }
} // namespace

//==============================================================================
// check — the order of Commands.h, and the bytes of the work

Checked Session::storageFor (const Request& request) const noexcept
{
    // 1. ENTRY
    if (checkFloatingPointEnvironment() != Status::Ok) return rejected (Rejection::FloatingPointEnvironment);
    // 2. STATE
    const auto which = Command (request.index());
    if (const Rejection r = Table::commands[index (which)].cell[index (column())]; r != Rejection::None) return rejected (r);

    // The config, read in place after the build gate established its contract.
    const Rules rules = detail::rules();

    if (const auto* imported = std::get_if<command::ImportProject> (&request))
    {
        if (imported->bytes.size() > kMaxProjectText) return rejected (Rejection::ProjectTooLarge);
        const auto bytes = detail::importBytes (imported->bytes.size());
        return storage (bytes, bytes);
    }
    if (const auto* load = std::get_if<command::Load> (&request))
    {
        if (lastJob_ == std::numeric_limits<JobId>::max()) return rejected (Rejection::NoJobId);
        if (! detail::validUtf8 (load->meta.name)) return rejected (Rejection::InvalidUtf8);
        // 6. AUDIO
        const Pcm& pcm = load->pcm;
        if (pcm.channelCount < 1 || pcm.channelCount > 2) return rejected (Rejection::BadChannels);
        if (pcm.sampleRate < kMinSampleRate) return rejected (Rejection::BadRate);
        if (pcm.sampleRate > capabilities_.maxRateHz) return rejected (Rejection::RateAboveLimit);
        if (pcm.frames == 0) return rejected (Rejection::NoAudio);
        if (pcm.frames > std::numeric_limits<std::size_t>::max() / sizeof (float) / pcm.channelCount
            || pcm.frames > 9007199254740991ull / sizeof (float) / pcm.channelCount)
            return rejected (Rejection::TooLong);
        const auto audio = std::uint64_t (pcm.channelCount) * pcm.frames * sizeof (float);
        const auto name = std::uint64_t (load->meta.name.size());
        if (name > 9007199254740991ull - audio) return rejected (Rejection::TooLong);
        return storage (audio + name, std::max (audio, name));
    }
    if (const auto* set = std::get_if<command::SetTarget> (&request))
    {
        // 4. NAMES, 5. FIELDS
        if (! rules.find (set->target)) return rejected (Rejection::UnknownTarget);
        if (set->onEdits != OnEdits::Reset && set->onEdits != OnEdits::Keep) return rejected (Rejection::NotOneOf);
        return {};
    }
    if (const auto* edit = std::get_if<command::EditTarget> (&request))
    {
        // 5. FIELDS
        const TargetFields<Touched>& f = edit->fields;
        if (f.lufs)
            if (const Rejection r = knobCheck (rules.lufs, *f.lufs, source_.sampleRate); r != Rejection::None) return rejected (r, 0);
        if (f.tp)
            if (const Rejection r = knobCheck (rules.tp, *f.tp, source_.sampleRate); r != Rejection::None) return rejected (r, 1);
        return {};
    }
    if (const auto* edit = std::get_if<command::EditDevice> (&request))
    {
        // 3. MANUAL, 4. NAMES, 5. FIELDS
        if (! project_.manual) return rejected (Rejection::ManualOff);
        if ((capabilities_.offeredDevices & (1u << edit->fields.index())) == 0
            || ! detail::offered (rules, project_.target, source_.channels, Device (edit->fields.index())))
            return rejected (Rejection::NotOffered);
        return onActive (edit->fields, [&] (const auto& fields) { return editCheck (rules, fields, source_.sampleRate); });
    }
    if (const auto* revert = std::get_if<command::RevertEdits> (&request))
    {
        if (! project_.manual) return rejected (Rejection::ManualOff);
        if ((capabilities_.offeredDevices & (1u << revert->fields.index())) == 0
            || ! detail::offered (rules, project_.target, source_.channels, Device (revert->fields.index())))
            return rejected (Rejection::NotOffered);
        return {};
    }
    if (std::get_if<command::Master> (&request) != nullptr)
    {
        // 4. NAMES: an id for the job, never 0 and never one issued before.
        if (lastJob_ == std::numeric_limits<JobId>::max()) return rejected (Rejection::NoJobId);
        // THE WORK'S BYTES: room for one more master kept — its recipe is kept when it is done, and that must not ask
        // the heap at the end of a render — as one exact reserve, unless the room is there already.
        const bool room = masterRoom_ > masterCount_;
        const auto bytes = room ? 0 : std::uint64_t (masterCount_ + 1) * sizeof (Kept);
        return storage (bytes, bytes);
    }
    if (const auto* cancel = std::get_if<command::Cancel> (&request))
        return job_ == 0 && measurementJob_ == 0 ? rejected (Rejection::NoJob)
             : cancel->job != 0 && (cancel->job == job_ || cancel->job == measurementJob_)
                 ? Checked {} : rejected (Rejection::UnknownJob);
    if (const auto* forget = std::get_if<command::Forget> (&request))
    {
        for (const Kept& k : masters())
            if (k.id == forget->master) return {};
        return rejected (Rejection::UnknownMaster);
    }
    return {};   // SetManual: nothing past the table
}

//==============================================================================
// apply — check(), then the work

Checked Session::check (const Request& request) const noexcept
{
    const auto priced = storageFor (request);
    if (priced.rejection != Rejection::None) return priced;
    const auto checked = demand (priced);
    if (checked.rejection != Rejection::None) return checked;
    if (const auto* load = std::get_if<command::Load> (&request))
    {
        const auto& pcm = load->pcm;
        if (! pcm.channels) return rejected (Rejection::NoAudio);
        for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
        {
            if (! pcm.channels[c]) return rejected (Rejection::NoAudio);
            for (std::size_t i = 0; i < std::size_t (pcm.frames); ++i)
                if (! finite (pcm.channels[c][i])) return rejected (Rejection::NotFinite);
        }
    }
    return checked;
}

Answer Session::rejectProtocol (CommandId id) noexcept
{
    Answer answer;
    answer.command = id;
    answer.rejection = Rejection::Contract;
    return reject (answer);
}
Answer Session::reject (Answer answer) noexcept
{
    eventCount_ = 0;
    answer.revision = revision_;
    if (answer.rejection == Rejection::Memory)
    {
        Notification error;
        error.kind = EventKind::Error;
        error.payload.error.code = ErrorCode::Memory;
        error.payload.error.needBytes = answer.needBytes;
        (void) error.payload.error.fact.assign (text::Fact::of (text::FactId::SessionMemory));
        emit (error);
    }
    Notification event;
    event.kind = EventKind::Rejected;
    event.payload.rejected = { answer.command, answer.rejection };
    emit (event);
    return answer;
}

Answer Session::apply (const Request& request) noexcept
{
    eventCount_ = 0;
    Answer answer;
    answer.command = idOf (request);
    const Checked checked = check (request);
    answer.rejection = checked.rejection;
    answer.field = checked.field;
    answer.needBytes = checked.needBytes;
    detail::ImportedProject imported;
    if (checked.rejection == Rejection::None)
        if (const auto* input = std::get_if<command::ImportProject> (&request))
        {
            imported = detail::readProject (input->bytes, source_.channels, capabilities_.offeredDevices, source_.sampleRate);
            answer = imported.answer;
            answer.command = input->id;
        }
    if (answer.rejection != Rejection::None) return reject (answer);

    const Rules rules = detail::rules();
    bool empty = false;
    if (const auto* edit = std::get_if<command::EditTarget> (&request)) empty = ! edit->fields.lufs && ! edit->fields.tp;
    if (const auto* edit = std::get_if<command::EditDevice> (&request))
        empty = onActive (edit->fields, [&] (const auto& fields)
        {
            bool any = false;
            detail::DeviceOf<std::remove_cvref_t<decltype (fields)>>::each (rules,
                [&] (std::uint8_t, const FieldRule&, const auto& v) { any = any || v.has_value(); }, fields);
            return ! any;
        });
    if (const auto* revert = std::get_if<command::RevertEdits> (&request))
        empty = onActive (revert->fields, [&] (const auto& mask) { return ! anyMarked (rules, mask); });
    if (empty) { answer.revision = revision_; return answer; }
    if (const auto* load = std::get_if<command::Load> (&request))
    {
        const Pcm& pcm = load->pcm;
        // The name first, into an array of its own: it may be the name this session holds (a shell that loads again
        // under source().name), which the disarm below frees.
        const std::string_view given = load->meta.name;
        std::unique_ptr<char[]> name (given.empty() ? nullptr : new char[given.size()]);
        std::copy (given.begin(), given.end(), name.get());
        // DISARM: nothing of the old source stays beside the new — the master being made stops, the masters and the
        // measurements go, the manual mode is switched off and a person's device edits go with it (the mode does not
        // outlive the file: its edits were decisions about the old source), and the old samples are freed before the
        // new are asked for.
        mastering_ = false;
        job_ = 0;
        jobRecipe_ = {};
        masters_.reset();
        masterCount_ = 0;
        masterRoom_ = 0;
        samples_.reset();
        project_.core = version();
        differenceCount_ = 0;
        project_.manual = false;
        project_.devices = {};                                      // unplaced: both layers, until the new source is measured
        // WRITE — one array of exactly the samples, then a copy into it.
        const auto frames = std::size_t (pcm.frames);   // fits: check() refused a source past what size_t counts
        std::unique_ptr<float[]> samples (new float[frames * pcm.channelCount]);
        Fnv hash;
        hash.u32 (pcm.sampleRate);
        hash.u32 (pcm.channelCount);
        hash.u64 (pcm.frames);
        for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
        {
            std::copy (pcm.channels[c], pcm.channels[c] + frames, samples.get() + std::size_t (c) * frames);
            for (std::size_t i = 0; i < frames; ++i) hash.u32 (std::bit_cast<std::uint32_t> (pcm.channels[c][i]));
        }
        samples_ = std::move (samples);
        name_ = std::move (name);
        source_ = { pcm.channelCount, pcm.sampleRate, pcm.frames, hash.h, load->meta.fileRate, load->meta.rateKnown,
                    load->meta.bitDepth, std::string_view (name_.get(), given.size()) };
        state_ = State::Loaded;
        measurementJob_ = ++lastJob_;
        measurementUnit_ = masterUnit_ = 0;
        measurementProgress_ = { PhaseName::Stream, 0.0, config::Config::versions().all, 0, 0, 0, 10 };
        masterProgress_ = {};
        answer.job = measurementJob_;
    }
    else if (const auto* set = std::get_if<command::SetTarget> (&request))
    {
        const std::uint16_t row = *rules.find (set->target);
        project_.target = row;
        project_.core = version();
        differenceCount_ = 0;
        project_.targetEdit = {};                                   // the new target's numbers, silently
        if (set->onEdits == OnEdits::Reset) clearHands (project_.devices);
        else clearHandsNotOffered (rules, row, source_.channels, project_.devices);
        // Placed devices are placed again for the new target; unplaced ones wait for the first measurement's end.
        if (placed()) detail::placeMachine (rules, row, source_.channels, project_.devices, capabilities_.offeredDevices);
    }
    else if (const auto* edit = std::get_if<command::EditTarget> (&request))
    {
        if (edit->fields.lufs) project_.targetEdit.lufs = detail::kept (*edit->fields.lufs);
        if (edit->fields.tp) project_.targetEdit.tp = detail::kept (*edit->fields.tp);
    }
    else if (const auto* device = std::get_if<command::EditDevice> (&request))
    {
        onActive (device->fields, [&] (const auto& fields)
        {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (fields)>>;
            Of::each (rules, [] (std::uint8_t, const FieldRule&, auto& hand, const auto& v)
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype (v)>, std::optional<double>>)
                {
                    if (v) hand = detail::kept (*v);
                }
                else if (v) hand = v;
            }, Of::layers (project_.devices).hand, fields);
        });
    }
    else if (const auto* revert = std::get_if<command::RevertEdits> (&request))
    {
        onActive (revert->fields, [&] (const auto& mask)
        {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (mask)>>;
            Of::each (rules, [] (std::uint8_t, const FieldRule&, auto& hand, bool marked)
            {
                if (marked) hand.reset();
            }, Of::layers (project_.devices).hand, mask);
        });
    }
    else if (const auto* manual = std::get_if<command::SetManual> (&request))
    {
        project_.manual = manual->on;
        if (! manual->on) clearHands (project_.devices);           // a person's edits go; the machine's stay
    }
    else if (std::get_if<command::Master> (&request) != nullptr)
    {
        if (masterRoom_ == masterCount_)                            // the room check() counted, as one exact array
        {
            std::unique_ptr<Kept[]> room (new Kept[masterCount_ + 1]);
            std::copy (masters_.get(), masters_.get() + masterCount_, room.get());
            masters_ = std::move (room);
            masterRoom_ = masterCount_ + 1;
        }
        job_ = ++lastJob_;
        jobRecipe_ = { project_, source_.hash, config::Config::versions().sound };
        mastering_ = true;
        masterUnit_ = 0;
        const auto passes = std::uint32_t (*rules.engine.find ("progress").find ("master").find ("expectedPasses").integer());
        masterProgress_ = { PhaseName::Pass, 0.0, config::Config::versions().all, 0, passes, 0, passes + 1 };
        answer.job = job_;
    }
    else if (const auto* cancel = std::get_if<command::Cancel> (&request))
    {
        dropJob (cancel->job);
        Notification event;
        event.jobId = cancel->job;
        event.kind = EventKind::Fact;
        (void) event.payload.fact.assign (text::Fact::of (text::FactId::Cancelled));
        emit (event);
    }
    else if (const auto* forget = std::get_if<command::Forget> (&request))
    {
        Kept* const first = masters_.get();
        Kept* const last = first + masterCount_;
        Kept* const it = std::find_if (first, last, [&] (const Kept& k) { return k.id == forget->master; });
        std::copy (it + 1, last, it);                               // the room stays
        --masterCount_;
    }
    else if (std::holds_alternative<command::ImportProject> (request))
    {
        project_ = imported.project;
        differenceCount_ = imported.differenceCount;
        std::copy_n (imported.differences, differenceCount_, differences_);
        if (imported.convertedDefaults)
        {
            Notification event;
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::DefaultsConverted,
                text::Arg::text ({ imported.originalDefaults, sizeof (imported.originalDefaults) })));
            emit (event);
        }
        if (imported.foreignCore)
        {
            Notification event;
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::MachineDifferences,
                text::Arg::count (std::int64_t (differenceCount_))));
            emit (event);
        }
    }
    answer.revision = ++revision_;
    return answer;
}

//==============================================================================
// THE SESSION'S OWN TRANSITIONS (src/Driver.h)

namespace detail
{
bool Driver::allowed (const Session& session, Event event) noexcept
{
    return Table::events[index (event)].cell[index (session.column())];
}

bool Driver::measured1 (Session& session, JobId job, std::uint64_t source) noexcept
{
    if (job == 0 || job != session.measurementJob_ || source != session.source_.hash) return false;
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Measured1)) return false;
    session.state_ = State::Measured1;
    session.measurementUnit_ = 5;
    detail::placeMachine (detail::rules(), session.project_.target, session.source_.channels, session.project_.devices, session.capabilities_.offeredDevices);
    ++session.revision_;
    return true;
}

bool Driver::measured2 (Session& session, JobId job, std::uint64_t source) noexcept
{
    if (job == 0 || job != session.measurementJob_ || source != session.source_.hash) return false;
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Measured2)) return false;
    session.state_ = State::Measured2;
    session.measurementJob_ = 0;
    ++session.revision_;
    return true;
}

bool Driver::mastered (Session& session, JobId job) noexcept
{
    if (job == 0 || job != session.job_) return false;
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Mastered)) return false;
    detail::debugBound (session.masterRoom_ > session.masterCount_);
    session.masters_[session.masterCount_++] = { session.job_, session.jobRecipe_ };   // into the room master() took
    session.mastering_ = false;
    session.job_ = 0;
    session.jobRecipe_ = {};
    ++session.revision_;
    return true;
}
} // namespace detail

} // namespace felitronics::session
