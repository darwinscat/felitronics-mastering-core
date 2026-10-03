// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE COMMANDS (Commands.h): check() runs the checks in the order Commands.h declares — the floating-point environment,
// then the table, then the rest — and says what the request would ask the heap for; apply() runs check() and, only when
// it passed, does the work. A rejection publishes an event and advances seq, but changes no state: check() is const, and the work
// starts after it. The session's own transitions (src/Driver.h) are here too, beside the table they read.

#include "BuildGuards.h"

#include "Devices.h"
#include "Driver.h"
#include "Planner.h"
#include "Grid.h"
#include "Rules.h"
#include "BuildContract.h"
#include "ProjectIO.h"
#include "MeasurementPlan.h"
#include "MeasurementWorkspace.h"
#include "LiveMeasurements.h"
#include "SourceMeasurements.h"
#include "QueryState.h"
#include "MasterJob.h"
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
static_assert (std::variant_size_v<DeviceEdit> == 9 && std::variant_size_v<DeviceMask> == 9, "one alternative per device");
static_assert (std::is_same_v<std::variant_alternative_t<std::size_t (Command::Forget), Request>, command::Forget>
               && std::is_same_v<std::variant_alternative_t<std::size_t (Command::Load), Request>, command::Load>,
               "Request's alternatives are in the order of Command");
static_assert (std::is_same_v<std::variant_alternative_t<std::size_t (Device::Bands), DeviceEdit>, BandsFields<Touched>>
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
    else if constexpr (std::is_same_v<T, SaturationType>)
        return std::uint8_t (*v) <= std::uint8_t (SaturationType::Tape) && detail::handSaturationType (*v)
             ? Rejection::None : Rejection::NotOneOf;
    else
    {
        static_assert (std::is_same_v<T, Needles>);
        return std::uint8_t (*v) <= std::uint8_t (Needles::Off) ? Rejection::None : Rejection::NotOneOf;
    }
}

// THE NUMBERS A FIELD'S REFUSAL IS SAID WITH (Answer::value, low, high): the value the request gave the field the
// check refused, and for OutOfDomain the domain that check read — the same knob, the same source rate.
void refusedNumbers (const Rules& rules, const Request& request, std::uint32_t rate, Answer& answer) noexcept
{
    if (answer.rejection != Rejection::OutOfDomain && answer.rejection != Rejection::NotOneOf) return;
    const auto against = [&] (const detail::Knob& knob, double x) noexcept
    {
        if (answer.rejection != Rejection::OutOfDomain) return;
        switch (knob.domain)
        {
            case detail::Knob::Domain::Bounded: answer.low = knob.minimum.toDouble(); answer.high = knob.maximum.toDouble(); break;
            case detail::Knob::Domain::SourceNyquist: answer.low = 0.0; answer.high = double (rate) * 0.5; break;
            case detail::Knob::Domain::Finite: return;   // a finite number is never out of this domain
        }
        answer.value = x;
    };
    if (const auto* edit = std::get_if<command::EditTarget> (&request))
    {
        if (answer.field == 0 && edit->fields.lufs) against (rules.lufs, *edit->fields.lufs);
        if (answer.field == 1 && edit->fields.tp) against (rules.tp, *edit->fields.tp);
    }
    else if (const auto* device = std::get_if<command::EditDevice> (&request))
        onActive (device->fields, [&] (const auto& fields)
        {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (fields)>>;
            Of::each (rules, [&] (std::uint8_t i, const FieldRule& rule, const auto& v)
            {
                using T = typename std::remove_cvref_t<decltype (v)>::value_type;
                if (i != answer.field || ! v) return;
                if constexpr (std::is_same_v<T, double>) against (rule.knob, *v);
                else if constexpr (std::is_same_v<T, std::int32_t>)
                { if (answer.rejection == Rejection::NotOneOf) answer.value = double (*v); }
            }, fields);
        });
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

// A person's layers, taken back only by an explicit target reset.
void clearHands (Devices& devices) noexcept
{
    detail::eachDevice (devices, [] (Device, auto& layers) { layers.hand = {}; });
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
        // 3. NAMES: a placement with no field still to be measured — the file's own target is asked in apply().
        if (detail::anyPending (plan_.devices)) return rejected (Rejection::PlanPending);
        return detail::importBytes (imported->bytes);
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
        const auto plan = detail::MeasurementPlan::storageFor (pcm, detail::MeasurementPlan::parametersFor (pcm));
        if (plan.rejection != Rejection::None) return rejected (plan.rejection);
        const auto footprint = measurementStorage (pcm);
        // Resident name, detached snapshot name, and its worst-case JSON escaping coexist.
        if (name > 9007199254740991ull / 8u
            || footprint.peakBytes + double (8u * name) >= 9007199254740992.0) return rejected (Rejection::TooLong);
        const auto extra = std::uint64_t (footprint.peakBytes) - std::uint64_t (liveBytes());
        return storage (extra + 8u * name, std::uint64_t (footprint.largestBlockBytes) + 6u * name);
    }
    if (const auto* set = std::get_if<command::SetTarget> (&request))
    {
        // 4. NAMES
        if (! rules.find (set->target)) return rejected (Rejection::UnknownTarget);
        return {};
    }
    if (const auto* edit = std::get_if<command::EditTarget> (&request))
    {
        // 5. FIELDS
        const TargetFields<Touched>& f = edit->fields;
        if (f.lufs && edit->clear.lufs) return rejected (Rejection::Contract, 0);
        if (f.tp && edit->clear.tp) return rejected (Rejection::Contract, 1);
        if (f.lufs)
            if (const Rejection r = knobCheck (rules.lufs, *f.lufs, source_.sampleRate); r != Rejection::None) return rejected (r, 0);
        if (f.tp)
            if (const Rejection r = knobCheck (rules.tp, *f.tp, source_.sampleRate); r != Rejection::None) return rejected (r, 1);
        return {};
    }
    if (const auto* edit = std::get_if<command::EditDevice> (&request))
    {
        // 3. NAMES, 4. FIELDS
        if ((capabilities_.offeredDevices & (1u << edit->fields.index())) == 0
            || ! detail::offered (rules, project_.target, source_.channels, Device (edit->fields.index())))
            return rejected (Rejection::NotOffered);
        return onActive (edit->fields, [&] (const auto& fields) { return editCheck (rules, fields, source_.sampleRate); });
    }
    if (const auto* revert = std::get_if<command::RevertEdits> (&request))
    {
        if ((capabilities_.offeredDevices & (1u << revert->fields.index())) == 0
            || (Device (revert->fields.index()) != Device::Dither
                && ! detail::offered (rules, project_.target, source_.channels, Device (revert->fields.index()))))
            return rejected (Rejection::NotOffered);
        return {};
    }
    if (const auto* master = std::get_if<command::Master> (&request))
    {
        // 3. NAMES: the source's audio (a sidecar source has none before attachAudio), then an id for the job, never 0
        // and never one issued before.
        if (! samples_) return rejected (Rejection::NoAudio);
        // With the panel open a master the session decides waits until what its devices read has ended; with the panel
        // hidden it is taken, and waits for it itself — resuming a stopped measurement takes a job id of its own.
        const bool decided = master->ready.version == 0;
        if (decided && project_.manual && plan_.waiting != 0) return rejected (Rejection::PlanPending);
        const bool resumes = decided && (plan_.waiting & ~detail::bitOf (Analyzer::Excursions)) != 0 && measurementJob_ == 0;
        if (lastJob_ == std::numeric_limits<JobId>::max()
            || (resumes && lastJob_ == std::numeric_limits<JobId>::max() - 1u))
            return rejected (Rejection::NoJobId);
        // The job's demand, whole, before anything is done: a ready chain's, or the chain the project's devices come to
        // as the measurements stand (a waiting master's tempo and needles move parameters, never the chain's geometry).
        const auto plan = detail::MasterJob::plan (*this, *master, project_);
        if (plan.rejection != Rejection::None) return rejected (plan.rejection);
        const bool room = masterRoom_ > masterCount_;
        const auto roomBytes = (room ? 0u : std::uint64_t (masterCount_ + 1) * sizeof (Kept) * (capabilities_.leanSummary ? 2u : 1u))
            + std::uint64_t (std::max (masterRoom_, masterCount_ + 1)) * sizeof (detail::MasterRows) + 4096u;
        if (roomBytes > 9007199254740991ull - plan.bytes) return rejected (Rejection::TooLong);
        return storage (plan.bytes + roomBytes, std::max (plan.largestBlock, roomBytes));
    }
    if (std::holds_alternative<command::ContinueMeasurement> (request))
        return lastJob_ == std::numeric_limits<JobId>::max() ? rejected (Rejection::NoJobId) : Checked {};
    if (const auto* cancel = std::get_if<command::Cancel> (&request))
        return job_ == 0 && measurementJob_ == 0 && needlesJob_ == 0 ? rejected (Rejection::NoJob)
             : cancel->job != 0 && (cancel->job == job_ || cancel->job == measurementJob_ || cancel->job == needlesJob_)
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
    if (answer.rejection == Rejection::DeliveryFormat)
    {
        // The code alone does not say which format the target takes; the shell reads it from this fact.
        Notification fact;
        fact.kind = EventKind::Fact;
        (void) fact.payload.fact.assign (text::Fact::of (text::FactId::RejectedDeliveryFormat,
            text::Arg::count (answer.targetBits), text::Arg::value (double (answer.targetRate), text::Unit::Hz, 0)));
        emit (fact);
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
    refusedNumbers (detail::rules(), request, source_.sampleRate, answer);
    if (checked.rejection == Rejection::DeliveryFormat)
    {
        const auto row = detail::rules().row (project_.target);
        answer.targetBits = std::uint8_t (row.bitDepth);
        answer.targetRate = row.sampleRate == 0 ? source_.sampleRate : std::uint32_t (row.sampleRate);
    }
    detail::ImportedProject imported;
    if (checked.rejection == Rejection::None)
        if (const auto* input = std::get_if<command::ImportProject> (&request))
        {
            imported = detail::readProject (input->bytes, planInputs (project_));
            answer = imported.answer;
            answer.command = input->id;
        }
    if (answer.rejection != Rejection::None) return reject (answer);

    const Rules rules = detail::rules();
    bool empty = false;
    if (const auto* edit = std::get_if<command::EditTarget> (&request))
        empty = ! edit->fields.lufs && ! edit->fields.tp && ! edit->clear.lufs && ! edit->clear.tp;
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
    if (std::holds_alternative<command::AdoptMachine> (request)) empty = differenceCount_ == 0;
    if (empty) { answer.revision = revision_; return answer; }
    if (const auto* load = std::get_if<command::Load> (&request))
    {
        const Pcm& pcm = load->pcm;
        Fnv hash;
        hash.u32 (pcm.sampleRate); hash.u32 (pcm.channelCount); hash.u64 (pcm.frames);
        for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
            for (std::size_t i = 0; i < std::size_t (pcm.frames); ++i) hash.u32 (std::bit_cast<std::uint32_t> (pcm.channels[c][i]));
        const auto plan = detail::MeasurementPlan::storageFor (pcm, detail::MeasurementPlan::parametersFor (pcm));
        const auto key = detail::MeasurementPlan::key (hash.h, plan.parameters, config::Config::versions().all, coreVersion(), version());
        const bool cached = source_.channels != 0 && key == measurementKey_;
        const auto previousState = state_;
        const auto previousPlacement = devicesPlaced_;
        const auto previousUnit = measurementUnit_;
        const auto previousProgress = measurementProgress_;
        const auto previousBitDepth = source_.bitDepth;
        // The name first, into an array of its own: it may be the name this session holds (a shell that loads again
        // under source().name), which the disarm below frees.
        const std::string_view given = load->meta.name;
        std::unique_ptr<char[]> name (given.empty() ? nullptr : new char[given.size()]);
        std::copy (given.begin(), given.end(), name.get());
        // End mastering and source-specific edits. A matching measurement key keeps PCM and results;
        // otherwise release the previous measurement before allocating the new source.
        clearMasters();
        clearNeedles();
        needlesSource_ = 0; needlesKey_ = 0; needlesNeedDb_.reset(); needlesCeilingDb_.reset();
        needlesProgress_ = {}; needlesDemand_ = {};
        if (! cached)
        {
            measurementsFromSidecar_ = false;
            samples_.reset();
            waveform_.reset (new detail::WaveformState);
            queryCache_.reset();
            measurementWorkspace_.reset (new detail::MeasurementWorkspace);
            liveMeasurements_.reset (new detail::LiveMeasurements);
            sourceMeasurements_.reset (new detail::SourceMeasurements);
            measurementOwnedBytes_ = 0;
            for (std::size_t i = 0; i < kAnalyzers; ++i)
            {
                measurementOwners_[i] = {};
                measurementResults_[i] = {};
                auto& r = measurementResults_[i];
                r.analyzer = Analyzer (i);
                r.key = r.analyzer == Analyzer::Forensics ? detail::MeasurementPlan::forensicsKey (key, load->meta.bitDepth) : key;
                if (! plan.analyzers[i].available)
                {
                    r.status = MeasurementStatus::Unavailable;
                    r.reason = r.analyzer == Analyzer::Excursions ? MeasurementReason::Pending : MeasurementReason::Unsupported;
                }
            }
        }
        measurementKey_ = key;
        measurementStorage_ = plan.storage;
        differenceCount_ = 0;
        machineFromFile_ = false;
        project_.manual = false;
        devicesPlaced_ = cached && previousPlacement;
        project_.devices = {};                                      // unplaced: both layers, until the new source is measured
        // WRITE — one array of exactly the samples, then a copy into it.
        const auto frames = std::size_t (pcm.frames);   // fits: check() refused a source past what size_t counts
        if (! cached)
        {
            std::unique_ptr<float[]> samples (new float[frames * pcm.channelCount]);
            for (std::uint32_t c = 0; c < pcm.channelCount; ++c)
                std::copy_n (pcm.channels[c], frames, samples.get() + std::size_t (c) * frames);
            samples_ = std::move (samples);
        }
        name_ = std::move (name);
        source_ = { pcm.channelCount, pcm.sampleRate, pcm.frames, hash.h, load->meta.fileRate, load->meta.rateKnown,
                    load->meta.bitDepth, std::string_view (name_.get(), given.size()) };
        state_ = State::Loaded;
        measurementJob_ = ++lastJob_;
        measurementUnit_ = masterUnit_ = 0;
        measurementProgress_ = { PhaseName::Stream, 0.0, config::Config::versions().all, 0, 0, 0, 10 };
        masterProgress_ = {};
        if (cached)
        {
            state_ = previousState == State::MeasurementStopped ? stoppedState_ : previousState;
            measurementUnit_ = previousUnit;
            measurementProgress_ = previousProgress;
            if (state_ == State::Measured2 || (sourceMeasurements_ && sourceMeasurements_->finished)) measurementJob_ = 0;
            for (auto& r : measurementResults_)
                if (r.analyzer != Analyzer::Excursions && r.status == MeasurementStatus::Cancelled) { r.status = MeasurementStatus::Pending; r.reason = MeasurementReason::Pending; }
            if (devicesPlaced_) place (project_);
            // The PCM's results stay. Forensics also reads the file's bit depth: under another depth its unused low bits
            // are recomputed from the retained grid, and it is another result, under its own key, published as one.
            if (source_.bitDepth != previousBitDepth)
            {
                auto& forensics = measurementResults_[std::size_t (Analyzer::Forensics)];
                forensics.key = detail::MeasurementPlan::forensicsKey (key, source_.bitDepth);
                if (sourceMeasurements_)
                    if (auto& evidence = sourceMeasurements_->results[std::size_t (Analyzer::Forensics)]; evidence)
                        (void) detail::SourceResults::forensicsMetadata (*evidence, source_.bitDepth);
                Notification event;
                event.kind = EventKind::Measurement;
                event.payload.measurement = { forensics.analyzer, forensics.status, forensics.reason, forensics.key, source_.hash,
                                              0, forensics.framesRead, forensics.total, forensics.stored, forensics.complete };
                emit (event);                                   // its revision is the command's, set below
            }
        }
        answer.job = measurementJob_;
        // A reload of the same source asks at once, whatever its readings: the result then says why nothing runs.
        if (cached || mandatoryReady()) requestNeedles();
    }
    else if (const auto* set = std::get_if<command::SetTarget> (&request))
    {
        const std::uint16_t row = *rules.find (set->target);
        project_.target = row;
        differenceCount_ = 0;
        project_.targetEdit = {};                                   // the new target's numbers, silently
        clearHands (project_.devices);
        machineFromFile_ = false;
        // Placed devices are placed again for the new target; unplaced ones wait for the first measurement's end.
        if (devicesPlaced_) place (project_);
    }
    else if (const auto* edit = std::get_if<command::EditTarget> (&request))
    {
        if (edit->fields.lufs) project_.targetEdit.lufs = detail::kept (*edit->fields.lufs);
        if (edit->fields.tp) project_.targetEdit.tp = detail::kept (*edit->fields.tp);
        // A cleared field is the target row's again: the person's number goes, and with it the line a project writes.
        if (edit->clear.lufs) project_.targetEdit.lufs.reset();
        if (edit->clear.tp) project_.targetEdit.tp.reset();
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
    }
    else if (const auto* master = std::get_if<command::Master> (&request))
    {
        const auto plan = detail::MasterJob::plan (*this, *master, project_);
        if (masterRoom_ == masterCount_)                            // the room check() counted, as one exact array
        {
            std::unique_ptr<Kept[]> room (new Kept[masterCount_ + 1]);
            std::copy (masters_.get(), masters_.get() + masterCount_, room.get());
            masters_ = std::move (room);
            masterRoom_ = masterCount_ + 1;
            if (capabilities_.leanSummary) { leanMasters_.reset(); leanMasters_.reset (new Kept[masterRoom_]); }
        }
        {
            std::unique_ptr<detail::MasterRows[]> rowRoom (new detail::MasterRows[masterRoom_]);
            if (masterRows_)
                for (std::size_t i = 0; i < masterCount_; ++i) rowRoom[i] = std::move (masterRows_[i]);
            masterRows_ = std::move (rowRoom);
        }
        job_ = ++lastJob_;
        mastering_ = true;
        masterUnit_ = 0;
        masterSummary_ = {}; masterTraceCursor_ = 0; masterTraceActive_ = false;
        // THE RECIPE is the project as it is now — the target, its numbers, a person's layer — whatever changes while
        // the measurements its devices read end. A master the session decides waits for them (the panel hidden: with
        // it open the command was refused); a ready one renders at once.
        jobWaiting_ = master->ready.version == 0 && plan_.waiting != 0;
        jobMachineFromFile_ = machineFromFile_;
        jobRecipe_ = Recipe { project_, source_.hash, config::Config::versions().sound };
        jobRecipe_.readyVersion = plan.ready.version;
        if (jobWaiting_)
        {
            const auto passes = std::uint32_t (*rules.engine.find ("progress").find ("master").find ("expectedPasses").integer());
            const auto chunks = (source_.frames + 1023u) / 1024u;
            const auto waitUnits = std::uint32_t (std::min<std::uint64_t> (3u * chunks + 64u, 4294967295u));
            masterProgress_ = { PhaseName::Analyzers, 0.0, config::Config::versions().all, 0, passes, 0, waitUnits };
        }
        else startMaster (plan);
        answer.job = job_;
    }
    else if (std::holds_alternative<command::ContinueMeasurement> (request))
    {
        answer.job = detail::Driver::continueMeasurement (*this);
        --revision_; // the command publishes its single revision below
        Notification event; event.jobId = answer.job; event.kind = EventKind::Phase;
        event.payload.phase = measurementProgress_; emit (event);
    }
    else if (const auto* cancel = std::get_if<command::Cancel> (&request))
    {
        const auto progress = jobProgress (cancel->job);
        // The fact names the job that stopped: needles or a master cancelled while the measurement is stopped are
        // cancelled, not a measurement stopped again.
        const bool measurement = cancel->job == measurementJob_;
        dropJob (cancel->job);
        Notification event;
        event.jobId = cancel->job;
        event.kind = EventKind::Fact;
        (void) event.payload.fact.assign (text::Fact::of (measurement ? text::FactId::MeasurementStopped : text::FactId::Cancelled));
        emit (event, progress);
    }
    else if (const auto* forget = std::get_if<command::Forget> (&request))
    {
        if (pendingMaster_.master == forget->master)
        { masterAudio_ = {}; masterAudioBits_ = 0; pendingMaster_ = {}; }
        Kept* const first = masters_.get();
        Kept* const last = first + masterCount_;
        Kept* const it = std::find_if (first, last, [&] (const Kept& k) { return k.id == forget->master; });
        const auto index = std::size_t (it - first);
        std::copy (it + 1, last, it);                               // the room stays
        if (masterRows_)
        {
            for (std::size_t i = index; i + 1 < masterCount_; ++i) masterRows_[i] = std::move (masterRows_[i + 1]);
            if (masterJob_)
                masterRows_[masterCount_ - 1] = std::move (masterRows_[masterCount_]);
            else
                masterRows_[masterCount_ - 1] = {};
        }
        --masterCount_;
        if (crestJoin_ && index < crestJoinIndex_) --crestJoinIndex_;
    }
    else if (std::holds_alternative<command::AdoptMachine> (request))
    {
        // The planner's layer replaces the file's, as at every other placement.
        place (project_);
        machineFromFile_ = false;
        differenceCount_ = 0;
    }
    else if (std::holds_alternative<command::ImportProject> (request))
    {
        project_ = imported.project;
        // The file's machine layer is the file's (an import opens only the current defaults label): the planner's
        // differences are shown beside it, and its opinion replaces the layer only by adoptMachine.
        machineFromFile_ = true;
        differenceCount_ = imported.differenceCount;
        std::copy_n (imported.differences, differenceCount_, differences_);
        if (differenceCount_ != 0)
        {
            Notification event;
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::MachineDifferences,
                text::Arg::count (std::int64_t (differenceCount_))));
            emit (event);
        }
    }
    if (std::holds_alternative<command::SetTarget> (request) || std::holds_alternative<command::EditTarget> (request)
        || std::holds_alternative<command::ImportProject> (request)) requestNeedles();
    replan();
    refreshEqCurve();
    answer.revision = ++revision_;
    for (std::size_t i = 0; i < eventCount_; ++i)
    {
        auto& e = (*events_)[i];
        e.revision = revision_;
        if (e.kind == EventKind::Measurement) e.payload.measurement.revision = revision_;
    }
    return answer;
}

//==============================================================================
// THE MASTER'S JOB — its retained rows and its work, installed for the master being made (job_): at once for a master
// that waits for nothing, when its wait ends for one that did. check() — or the pump, when a wait ends — has stated
// the demand; this asks the heap for it.
void Session::startMaster (const detail::MasterPlan& plan) noexcept
{
    auto& rows = masterRows_[masterCount_];
    rows = {};
    rows.passes.reset (new LandingPass[12]);
    rows.traces.reset (new LandingTraceBucket[std::size_t (2 * plan.traceBuckets)]);
    rows.traceCapacity = std::uint32_t (plan.traceBuckets);
    if (plan.glueTrace) rows.glueRows.reset (new LandingTraceBucket[std::size_t (plan.traceBuckets)]);
    if (plan.saturationTrace) rows.saturationRows.reset (new LandingTraceBucket[std::size_t (plan.traceBuckets)]);
    if (plan.crestCapacity != 0) rows.crest.reset (new double[plan.crestCapacity * 15u]);
    rows.crestCapacity = plan.crestCapacity;
    if (plan.costCapacity != 0)
    {
        rows.costSeries.reset (new double[plan.costCapacity]);
        rows.sections.reset (new MasterSection[plan.costCapacity]);
    }
    if (plan.costScratchCapacity != 0) rows.costScratch.reset (new double[plan.costScratchCapacity]);
    if (plan.waveformCapacity != 0) rows.waveform.reset (new MasterWaveformBucket[plan.waveformCapacity]);
    if (plan.costCapacity != 0) rows.costMomentary.reset (new double[plan.costCapacity]);
    if (plan.axesCapacity != 0) rows.axes.reset (new analysis::WaveformColumn[plan.axesCapacity]);
    rows.axesCapacity = plan.axesCapacity;
    rows.costCapacity = plan.costCapacity;
    rows.costScratchCapacity = plan.costScratchCapacity;
    rows.waveformCapacity = plan.waveformCapacity;
    masterJob_.reset (new detail::MasterJob);
    if (! masterJob_->begin (*this, plan)) detail::storageOverflow();
    // liveBytes() counts the installed row owners separately while this job is active.
    masterJobBytes_ = plan.bytes - plan.retainedRowBytes;
    jobWaiting_ = false;
    jobRecipe_.readyHash = detail::MasterJob::fingerprint (plan.ready);
    jobRecipe_.deliveryRateHz = plan.deliveryRate;
    jobRecipe_.readyVersion = plan.ready.version;
    rows.crestMasterId = job_;
    rows.crestSource = jobRecipe_.source;
    rows.crestSourceKey = measurementKey_;
    rows.crestReadyHash = jobRecipe_.readyHash;
    rows.crestSound = jobRecipe_.sound;
    rows.crestDeliveryRate = jobRecipe_.deliveryRateHz;
    rows.crestParams = plan.crestParams;
    masterUnit_ = 0;
    masterSummary_ = {}; masterTraceCursor_ = 0; masterTraceActive_ = false;
    masterProgress_ = { PhaseName::Pass, 0.0, config::Config::versions().all, 0, 12, 0, 12 };
}

//==============================================================================
// THE SESSION'S OWN TRANSITIONS (src/Driver.h)

namespace detail
{
bool Driver::retain (Session& session, JobId job, const MeasurementResult& result) noexcept
{
    if (result.analyzer == Analyzer::Excursions || job == 0 || job != session.measurementJob_
        || result.framesRead > session.source_.frames
        || Session::checkFloatingPointEnvironment() != Status::Ok || ! OwnedMeasurements::valid ({ &result, 1 })) return false;
    const auto i = std::size_t (result.analyzer);
    // The identity of this analyzer's result for the current source (forensics: its bit depth too).
    if (result.key != session.measurementResults_[i].key) return false;
    if (session.measurementResults_[i].status == MeasurementStatus::Ready) return false;
    const auto bytes = OwnedMeasurements::storageFor ({ &result, 1 });
    const Pcm pcm { nullptr, session.source_.channels, session.source_.frames, session.source_.sampleRate };
    const auto plan = MeasurementPlan::storageFor (pcm, MeasurementPlan::parametersFor (pcm));
    if (bytes > plan.analyzers[i].result || session.demand (storage (bytes, bytes)).rejection != Rejection::None) return false;
    auto copy = OwnedMeasurements::copy ({ &result, 1 });
    session.measurementOwnedBytes_ -= OwnedMeasurements::storageFor (session.measurementOwners_[i].view());
    session.measurementOwnedBytes_ += bytes;
    session.measurementOwners_[i] = std::move (copy);
    session.measurementResults_[i] = session.measurementOwners_[i].view()[0];
    if (result.status != MeasurementStatus::Pending && session.measurementWorkspace_)
        session.measurementWorkspace_->release (result.analyzer);
    ++session.revision_;
    Notification event; event.jobId = job; event.kind = EventKind::Measurement;
    event.payload.measurement = { result.analyzer, result.status, result.reason, result.key, session.source_.hash,
                                  session.revision_, result.framesRead, result.total, result.stored, result.complete };
    session.emit (event);
    if (result.analyzer == Analyzer::Loudness && session.devicesPlaced_) session.requestNeedles();
    if (result.status != MeasurementStatus::Pending) session.placeAgain (result.analyzer);
    session.replan();
    return true;
}
JobId Driver::continueMeasurement (Session& session) noexcept
{
    if (session.source_.channels == 0 || session.measurementJob_ != 0 || session.state_ == State::Measured2
        || session.lastJob_ == std::numeric_limits<JobId>::max() || Session::checkFloatingPointEnvironment() != Status::Ok) return 0;
    if (session.state_ == State::MeasurementStopped) session.state_ = session.stoppedState_;
    session.measurementJob_ = ++session.lastJob_;
    for (auto& r : session.measurementResults_)
        if (r.analyzer != Analyzer::Excursions && r.status == MeasurementStatus::Cancelled) { r.status = MeasurementStatus::Pending; r.reason = MeasurementReason::Pending; }
    ++session.revision_;
    session.replan();
    return session.measurementJob_;
}
bool Driver::allowed (const Session& session, Event event) noexcept
{
    return Table::events[index (event)].cell[index (session.column())];
}

bool Driver::measured1 (Session& session, JobId job, std::uint64_t source) noexcept
{
    if (job == 0 || job != session.measurementJob_ || source != session.source_.hash) return false;
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Measured1)) return false;
    session.state_ = State::Measured1;
    session.devicesPlaced_ = true;
    session.measurementUnit_ = 5;
    session.place (session.project_);
    session.requestNeedles();
    session.replan();
    session.refreshEqCurve();
    ++session.revision_;
    return true;
}

bool Driver::measured2 (Session& session, JobId job, std::uint64_t source) noexcept
{
    if (job == 0 || job != session.measurementJob_ || source != session.source_.hash) return false;
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Measured2)) return false;
    session.state_ = State::Measured2;
    session.measurementJob_ = 0;
    // The second measurement has ended: nothing of the source's job is left to end. The pump arrives here with every
    // result ended; a fixture's seam may not have run an analyzer at all, and a result that can no longer end would be
    // one a plan waits for for ever.
    for (const auto analyzer : detail::SourceMeasurements::order)
        if (auto& r = session.measurementResults_[std::size_t (analyzer)]; r.status == MeasurementStatus::Pending)
        { r.status = MeasurementStatus::Unavailable; r.reason = MeasurementReason::NotImplemented; }
    session.replan();
    ++session.revision_;
    return true;
}

bool Driver::mastered (Session& session, JobId job) noexcept
{
    if (job == 0 || job != session.job_) return false;
    if (Session::checkFloatingPointEnvironment() != Status::Ok || ! allowed (session, Event::Mastered)) return false;
    detail::debugBound (session.masterRoom_ > session.masterCount_);
    session.masters_[session.masterCount_++] = { session.job_, session.jobRecipe_, {}, {} };   // into the room master() took
    // A fixture's seam may end a master whose job never ran: the job goes with it.
    if (session.job_ != 0 && session.masterJob_ && ! session.masterTraceActive_)
    { session.masterJob_.reset(); session.masterJobBytes_ = 0; }
    session.mastering_ = false;
    session.job_ = 0;
    session.jobRecipe_ = {};
    session.jobWaiting_ = false;
    session.replan();
    ++session.revision_;
    return true;
}
} // namespace detail

} // namespace felitronics::session
