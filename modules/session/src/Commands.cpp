// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE COMMANDS (Commands.h): check() runs the checks in the order Commands.h declares — the table first — and says what
// the request would ask the heap for; apply() runs check() and, only when it passed, does the work. So a rejected request
// has changed nothing by construction: check() is const, and the work starts after it. The session's own transitions
// (src/Driver.h) are here too, beside the table they read.

#include "BuildGuards.h"

#include "Devices.h"
#include "Driver.h"
#include "Grid.h"
#include "Rules.h"

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
#include <utility>
#include <variant>
#include <vector>

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

// A knob's value: finite, on its travel, on its step — in that order.
Rejection knobCheck (const detail::Knob& knob, double x) noexcept
{
    if (! finite (x)) return Rejection::NotFinite;
    const std::optional<detail::Decimal> d = detail::decimalOf (x);
    const bool outside = d ? (detail::compare (*d, knob.from) < 0 || detail::compare (*d, knob.to) > 0)
                           : (x < knob.from.toDouble() || x > knob.to.toDouble());
    if (outside) return Rejection::OutOfTravel;
    if (! d || ! detail::onGrid (*d, knob.from, knob.step)) return Rejection::OffStep;
    return Rejection::None;
}

// A touched field's value, by its rule.
template <class T> Rejection fieldCheck (const Rules& rules, const FieldRule& rule, const std::optional<T>& v) noexcept
{
    if (! v) return Rejection::None;
    if constexpr (std::is_same_v<T, bool>)
        return Rejection::None;
    else if constexpr (std::is_same_v<T, double>)
        return knobCheck (rule.knob, *v);
    else if constexpr (std::is_same_v<T, std::int32_t>)
        return rules.slope (*v) ? Rejection::None : Rejection::NotOneOf;
    else
    {
        static_assert (std::is_same_v<T, Needles>);
        return std::uint8_t (*v) <= std::uint8_t (Needles::Off) ? Rejection::None : Rejection::NotOneOf;
    }
}

Checked rejected (Rejection r, std::uint8_t field = kNoField) noexcept { return { r, field, 0 }; }

// A device edit's fields: at least one touched; then each touched one, in the order written.
template <class Edit> Checked editCheck (const Rules& rules, const Edit& edit) noexcept
{
    bool any = false;
    Checked out;
    detail::DeviceOf<Edit>::each (rules, [&] (std::uint8_t i, const FieldRule& rule, const auto& v)
    {
        any = any || v.has_value();
        if (out.rejection == Rejection::None)
            if (const Rejection r = fieldCheck (rules, rule, v); r != Rejection::None) out = rejected (r, i);
    }, edit);
    if (! any) return rejected (Rejection::NoFields);
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

Checked Session::check (const Request& request) const noexcept
{
    // 1. ENTRY
    if (checkFloatingPointEnvironment() != Status::Ok) return rejected (Rejection::FloatingPointEnvironment);
    // 2. STATE
    const auto which = Command (request.index());
    if (const Rejection r = Table::commands[index (which)].cell[index (column())]; r != Rejection::None) return rejected (r);

    // The config, read in place: complete, or create() would have refused.
    const Rules rules = detail::rules();

    if (const auto* load = std::get_if<command::Load> (&request))
    {
        // 6. AUDIO
        const Pcm& pcm = load->pcm;
        if (pcm.channelCount < 1 || pcm.channelCount > 2) return rejected (Rejection::BadChannels);
        if (pcm.sampleRate < kMinSampleRate) return rejected (Rejection::BadRate);
        if (pcm.frames == 0 || pcm.channels == nullptr) return rejected (Rejection::NoAudio);
        for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
            if (pcm.channels[c] == nullptr) return rejected (Rejection::NoAudio);
        if (pcm.frames > samples_.max_size() / pcm.channelCount) return rejected (Rejection::TooLong);
        const auto frames = std::size_t (pcm.frames);   // fits: the line above
        for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
            for (std::size_t i = 0; i < frames; ++i)
                if (! finite (pcm.channels[c][i])) return rejected (Rejection::NotFinite);
        // THE WORK'S BYTES: the samples, channel after channel, and the name — each one exact request (the load in apply()
        // below: a vector of exactly this many floats, a vector of exactly this many chars).
        return { Rejection::None, kNoField,
                 std::uint64_t (pcm.channelCount) * pcm.frames * sizeof (float) + std::uint64_t (load->meta.name.size()) };
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
        if (! f.lufs && ! f.tp) return rejected (Rejection::NoFields);
        if (f.lufs)
            if (const Rejection r = knobCheck (rules.lufs, *f.lufs); r != Rejection::None) return rejected (r, 0);
        if (f.tp)
            if (const Rejection r = knobCheck (rules.tp, *f.tp); r != Rejection::None) return rejected (r, 1);
        return {};
    }
    if (const auto* edit = std::get_if<command::EditDevice> (&request))
    {
        // 3. MANUAL, 4. NAMES, 5. FIELDS
        if (! project_.manual) return rejected (Rejection::ManualOff);
        if (! detail::offered (rules, project_.target, source_.channels, Device (edit->fields.index())))
            return rejected (Rejection::NotOffered);
        return onActive (edit->fields, [&] (const auto& fields) { return editCheck (rules, fields); });
    }
    if (const auto* revert = std::get_if<command::RevertEdits> (&request))
    {
        if (! project_.manual) return rejected (Rejection::ManualOff);
        if (! detail::offered (rules, project_.target, source_.channels, Device (revert->fields.index())))
            return rejected (Rejection::NotOffered);
        const bool any = onActive (revert->fields, [&] (const auto& mask) { return anyMarked (rules, mask); });
        return any ? Checked {} : rejected (Rejection::NoFields);
    }
    if (std::get_if<command::Master> (&request) != nullptr)
    {
        // THE WORK'S BYTES: room for one more master kept — its recipe is kept when it is done, and that must not ask
        // the heap at the end of a render — as one exact reserve, unless the room is there already.
        const bool room = masters_.capacity() > masters_.size();
        return { Rejection::None, kNoField, room ? 0 : std::uint64_t (masters_.size() + 1) * sizeof (Kept) };
    }
    if (const auto* cancel = std::get_if<command::Cancel> (&request))
        return cancel->job == job_ ? Checked {} : rejected (Rejection::UnknownJob);
    if (const auto* forget = std::get_if<command::Forget> (&request))
    {
        for (const Kept& k : masters_)
            if (k.id == forget->master) return {};
        return rejected (Rejection::UnknownMaster);
    }
    return {};   // SetManual: nothing past the table
}

//==============================================================================
// apply — check(), then the work

Answer Session::apply (const Request& request) noexcept
{
    Answer answer;
    answer.command = idOf (request);
    const Checked checked = check (request);
    if (checked.rejection != Rejection::None)
    {
        answer.rejection = checked.rejection;
        answer.field = checked.field;
        answer.revision = revision_;
        return answer;
    }

    const Rules rules = detail::rules();
    if (const auto* load = std::get_if<command::Load> (&request))
    {
        const Pcm& pcm = load->pcm;
        // The name first, into a vector of its own: it may be the name this session holds (a shell that loads again
        // under source().name), which the disarm below frees.
        std::vector<char> name (load->meta.name.begin(), load->meta.name.end());
        // DISARM: nothing of the old source stays beside the new — the master being made stops, the masters and the
        // measurements go, a person's device edits go (they were decisions about the old source), and the old samples
        // are freed before the new are asked for.
        mastering_ = false;
        job_ = 0;
        jobRecipe_ = {};
        std::vector<Kept>().swap (masters_);
        std::vector<float>().swap (samples_);
        clearHands (project_.devices);
        // WRITE — one vector of exactly the samples, then a copy into it. (Not a reserve and a range insert: MSVC's STL
        // wraps its range insert in a try, which the library's flags refuse.)
        const auto frames = std::size_t (pcm.frames);   // fits: check() refused a source past max_size()
        std::vector<float> samples (frames * pcm.channelCount);
        Fnv hash;
        hash.u32 (pcm.sampleRate);
        hash.u32 (pcm.channelCount);
        hash.u64 (pcm.frames);
        for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
        {
            std::copy (pcm.channels[c], pcm.channels[c] + frames, samples.data() + std::size_t (c) * frames);
            for (std::size_t i = 0; i < frames; ++i) hash.u32 (std::bit_cast<std::uint32_t> (pcm.channels[c][i]));
        }
        samples_ = std::move (samples);
        name_ = std::move (name);
        source_ = { pcm.channelCount, pcm.sampleRate, pcm.frames, hash.h, load->meta.fileRate, load->meta.rateKnown,
                    load->meta.bitDepth, std::string_view (name_.data(), name_.size()) };
        state_ = State::Loaded;
        detail::placeMachine (rules, project_.target, source_.channels, project_.devices);
    }
    else if (const auto* set = std::get_if<command::SetTarget> (&request))
    {
        const std::uint16_t row = *rules.find (set->target);
        project_.target = row;
        project_.targetEdit = {};                                   // the new target's numbers, silently
        if (set->onEdits == OnEdits::Reset) clearHands (project_.devices);
        else clearHandsNotOffered (rules, row, source_.channels, project_.devices);
        detail::placeMachine (rules, row, source_.channels, project_.devices);
    }
    else if (const auto* edit = std::get_if<command::EditTarget> (&request))
    {
        if (edit->fields.lufs) project_.targetEdit.lufs = edit->fields.lufs;
        if (edit->fields.tp) project_.targetEdit.tp = edit->fields.tp;
    }
    else if (const auto* device = std::get_if<command::EditDevice> (&request))
    {
        onActive (device->fields, [&] (const auto& fields)
        {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (fields)>>;
            Of::each (rules, [] (std::uint8_t, const FieldRule&, auto& hand, const auto& v)
            {
                if (v) hand = v;
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
        masters_.reserve (masters_.size() + 1);                    // the room check() counted
        job_ = ++lastJob_;
        jobRecipe_ = { project_, source_.hash, config::Config::versions().sound };
        mastering_ = true;
        answer.job = job_;
    }
    else if (std::get_if<command::Cancel> (&request) != nullptr)
    {
        mastering_ = false;
        job_ = 0;
        jobRecipe_ = {};
    }
    else if (const auto* forget = std::get_if<command::Forget> (&request))
    {
        for (auto it = masters_.begin(); it != masters_.end(); ++it)
            if (it->id == forget->master) { masters_.erase (it); break; }
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

bool Driver::measured1 (Session& session) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Measured1)) return false;
    session.state_ = State::Measured1;
    detail::placeMachine (detail::rules(), session.project_.target, session.source_.channels, session.project_.devices);
    ++session.revision_;
    return true;
}

bool Driver::measured2 (Session& session) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Measured2)) return false;
    session.state_ = State::Measured2;
    ++session.revision_;
    return true;
}

bool Driver::mastered (Session& session) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Mastered)) return false;
    session.masters_.push_back ({ session.job_, session.jobRecipe_ });   // into the room master() took
    session.mastering_ = false;
    session.job_ = 0;
    session.jobRecipe_ = {};
    ++session.revision_;
    return true;
}
} // namespace detail

} // namespace felitronics::session
