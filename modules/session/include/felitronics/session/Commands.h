// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Project.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

//==============================================================================
// felitronics::session — THE STATES, THE COMMANDS AND WHO MAY DO WHAT, WHEN.
//
// A session is in one of four states, and a master being made is an overlay on the two measured ones:
//
//     Empty ──load──▶ Loaded ──(the first measurement ends)──▶ Measured1 ──(the second ends)──▶ Measured2
//                                                                  └ master ──▶ Mastering ◀── master ┘
//
//   Empty      nothing loaded; a target and the manual mode can be chosen already.
//   Loaded     a source is loaded and its first measurement has not ended. The devices are not placed: their panel is closed.
//   Measured1  the first measurement ended and the devices are placed: a master can be made, a person may edit them.
//   Measured2  the second measurement ended too.
//   Mastering  a master is being made, on Measured1 or on Measured2 — the second measurement may end meanwhile. It
//              renders what the project was when the master was asked for (its recipe): the project may change
//              meanwhile, and the master does not.
//
// Everything a shell asks is a Request, answered whole by Session::apply(): accepted with the revision it made, or
// rejected with a code and no state change; rejection publishes an event and advances seq. WHO MAY ASK WHAT, IN WHICH STATE, is the table below and nothing else —
// every command consults it right after the floating-point entry check. The endings of the measurements and of a master are not commands: they are the
// session's own transitions (Table::events), driven by the work that ends (the steps that measure and render).
namespace felitronics::session
{

enum class State : std::uint8_t { Empty, Loaded, Measured1, Measured2 };

// The commands, in the order of the table's rows and of Request's alternatives.
enum class Command : std::uint8_t { Load, SetTarget, EditTarget, EditDevice, RevertEdits, SetManual, Master, Cancel, Forget };
inline constexpr std::size_t kCommands = 9;

// The session's own transitions, in the order of the events table's rows.
enum class Event : std::uint8_t { Measured1, Measured2, Mastered };
inline constexpr std::size_t kEvents = 3;

// WHY A COMMAND WAS REJECTED — a code, never a text: what a person reads is written from it by the shell's catalogue.
// The values are stable; a new reason is a new value at the end.
enum class Rejection : std::uint8_t
{
    None = 0,                   // not rejected: accepted
    // entry
    FloatingPointEnvironment,   // the calling thread flushes to zero, reads subnormals as zero or does not round to nearest
    // the state (the table)
    NoSource,                   // nothing is loaded
    NotPlaced,                  // the devices are not placed yet: the first measurement runs
    NotMeasured,                // the first measurement has not ended
    Busy,                       // a master is being made
    NoJob,                      // no named measurement or master is running
    NoMaster,                   // no master is kept in this state
    // the device panel
    ManualOff,                  // the manual mode is off: the device panel is closed
    // what the command names
    UnknownTarget,              // no row of [targets] has this name
    NotOffered,                 // this device is not offered here: the low shelf off a target that carries one, the
                                // dither above the bit depth it serves, mono bass on a mono source
    UnknownJob,                 // not an active measurement or master
    UnknownMaster,              // no master kept under this id
    // the fields
    NoFields,                   // an edit or a revert that touches no field
    NotFinite,                  // a value or a sample that is not a finite number
    NotOneOf,                   // not one of the values this field takes (a slope, a mode)
    OutOfTravel,                // outside the knob's travel
    OffStep,                    // not a whole number of the knob's steps from where its travel starts
    // the audio (load)
    BadChannels,                // not one or two channels
    BadRate,                    // a sample rate below 8000 Hz (felitronics-core's lowest)
    NoAudio,                    // no frames, or no data
    TooLong,                    // more samples than this machine's memory can address
    // what the command names (continued)
    NoJobId,                    // every job id was issued: load and master cannot start another job
    InvalidUtf8,                // a load's name is not valid UTF-8
};

using CommandId = std::uint64_t;   // the shell's own number for a request, given back in its answer
using JobId = std::uint32_t;       // a measurement or master job: 1, 2, … in the order asked, never 0 and never twice in a
                                   // session's life (after the last one, neither load nor master starts another job)
using MasterId = std::uint32_t;    // a master kept: the id of the job that made it

inline constexpr std::uint8_t kNoField = 0xFF;

// A DEVICE'S EDIT: which device (the alternative, in the order of Device) and the fields a person touched.
using DeviceEdit = std::variant<HpfFields<Touched>, MonoBassFields<Touched>, GlueFields<Touched>, SaturationFields<Touched>,
                                TiltFields<Touched>, LimiterFields<Touched>, DitherFields<Touched>, LowShelfFields<Touched>>;
// ...and the fields a revert takes back.
using DeviceMask = std::variant<HpfFields<Mark>, MonoBassFields<Mark>, GlueFields<Mark>, SaturationFields<Mark>,
                                TiltFields<Mark>, LimiterFields<Mark>, DitherFields<Mark>, LowShelfFields<Mark>>;

// Whether setTarget keeps a person's device edits or takes them back. The target's own numbers are replaced either way.
enum class OnEdits : std::uint8_t { Reset, Keep };

// The lowest sample rate a load takes: felitronics-core's (core::kMinSampleRate) — below it the loudness weighting is past
// Nyquist, and what arrives there is a rate in kilohertz or a corrupt header. The highest is the shell's to say.
inline constexpr std::uint32_t kMinSampleRate = 8000;

// THE AUDIO OF A LOAD — planar 32-bit float, one pointer per channel, each `frames` long; the session copies it.
struct Pcm
{
    const float* const* channels = nullptr;
    std::uint32_t channelCount = 0;
    std::uint64_t frames = 0;
    std::uint32_t sampleRate = 0;              // Hz
};

// What a load says about the file the audio came from. The name must be valid UTF-8 and is kept as given.
struct SourceMeta
{
    std::string_view name;
    std::uint32_t fileRate = 0;                // Hz, the file's own rate; meaningful when rateKnown
    bool rateKnown = false;
    std::uint8_t bitDepth = 0;                 // the file's bits per sample; 0 when it has none (a lossy file)
};

// THE REQUESTS — one struct per command, each with the shell's id for it.
namespace command
{
struct Load        { CommandId id = 0; Pcm pcm {}; SourceMeta meta {}; };
struct SetTarget   { CommandId id = 0; std::string_view target; OnEdits onEdits = OnEdits::Reset; };
struct EditTarget  { CommandId id = 0; TargetFields<Touched> fields {}; };
struct EditDevice  { CommandId id = 0; DeviceEdit fields {}; };
struct RevertEdits { CommandId id = 0; DeviceMask fields {}; };
struct SetManual   { CommandId id = 0; bool on = false; };
struct Master      { CommandId id = 0; };
struct Cancel      { CommandId id = 0; JobId job = 0; };
struct Forget      { CommandId id = 0; MasterId master = 0; };
} // namespace command

// Any request: the alternative is the command, in the order of Command.
using Request = std::variant<command::Load, command::SetTarget, command::EditTarget, command::EditDevice,
                             command::RevertEdits, command::SetManual, command::Master, command::Cancel, command::Forget>;

// THE ANSWER — whole: accepted with the revision the command made, or rejected with no state change, but an event.
struct Answer
{
    CommandId command = 0;                     // the request's own id
    Rejection rejection = Rejection::None;     // None: accepted
    std::uint64_t revision = 0;                // accepted: the revision it made; rejected: the revision, unchanged
    JobId job = 0;                             // an accepted load or master: the job it started
    std::uint8_t field = kNoField;             // rejected on a field: its place among the fields, in the order written
                                               // (the target's: lufs 0, tp 1; a device's: as its Fields lists them)
};

// WHAT A REQUEST WOULD DO, BEFORE IT IS DONE — Session::check(): the answer it would get, and the bytes it would ask the
// heap for if it is accepted (law 11d: memory is declared before the work).
struct Checked
{
    Rejection rejection = Rejection::None;
    std::uint8_t field = kNoField;
    std::uint64_t bytes = 0;                   // accepted: what apply() will request from the heap; rejected: 0
};

// A session's situation — its state, and whether a master is being made — as the column of the tables below.
enum class Column : std::uint8_t { Empty, Loaded, Measured1, Measured2, Mastering1, Mastering2 };
inline constexpr std::size_t kColumns = 6;

//==============================================================================
// THE TABLE — who may do what, when. One row per command: in each column, None where the command is taken, or the
// rejection it gets there. Mastering1 and Mastering2 are a master being made on Measured1 and on Measured2.
//
// THE ORDER OF THE CHECKS, the same for every command — the first that fails is the answer, and nothing has changed:
//   1. ENTRY    the calling thread's floating-point environment (FloatingPointEnvironment)
//   2. STATE    this table: the command's cell in the session's column
//   3. MANUAL   the device panel's commands — editDevice, revertEdits — need the manual mode (ManualOff)
//   4. NAMES    what the command names: a target (setTarget), a device offered for this target and source (editDevice,
//               revertEdits), an id left for a new job (load, master: NoJobId), a load's UTF-8 name (InvalidUtf8),
//               the active job (cancel), a master kept (forget)
//   5. FIELDS   an edit or a revert touches a field (NoFields); then field by field, in the order written: finite, one of
//               the field's values, on its travel, on its step
//   6. AUDIO    a load's audio: its channels, its rate, its frames, its size, then every sample finite
// Only a command that passed them all does its work. A load's work starts by DISARMING — whatever runs on the old source
// stops, and the old source, its measurements, its masters and a person's device edits go — and then writes the new one.
struct Table
{
    using enum Rejection;

    struct Row
    {
        Command command;
        Rejection cell[kColumns];
    };

    static constexpr Row commands[kCommands] = {
        //                           Empty     Loaded       Measured1 Measured2 Mastering1 Mastering2
        { Command::Load,        {    None,     None,        None,     None,     None,      None } },
        { Command::SetTarget,   {    None,     None,        None,     None,     None,      None } },
        { Command::EditTarget,  {    None,     None,        None,     None,     None,      None } },
        { Command::EditDevice,  {    NoSource, NotPlaced,   None,     None,     None,      None } },
        { Command::RevertEdits, {    NoSource, NotPlaced,   None,     None,     None,      None } },
        { Command::SetManual,   {    None,     None,        None,     None,     None,      None } },
        { Command::Master,      {    NoSource, NotMeasured, None,     None,     Busy,      Busy } },
        { Command::Cancel,      {    NoJob,    None,        None,     NoJob,    None,      None } },
        { Command::Forget,      {    NoSource, NoMaster,    None,     None,     None,      None } },
    };

    // Cancel in Loaded drops the source and returns Empty; in Measured1 it stops phase two and keeps Measured1.
    // A master cancellation ends the overlay and keeps the measured state. Each cancelled job's progress is reset.
    // Cancel in Measured1 checks for a live measurement; after phase-two cancellation it returns NoJob.
    // THE SESSION'S OWN TRANSITIONS — where each may happen (true), and what it does:
    //   Measured1  the first measurement ended: Loaded becomes Measured1, and the devices are placed
    //   Measured2  the second ended: Measured1 becomes Measured2, with a master being made or not
    //   Mastered   the master being made is done: it is kept, and the overlay ends
    struct EventRow
    {
        Event event;
        bool cell[kColumns];
    };

    static constexpr EventRow events[kEvents] = {
        //                          Empty  Loaded Measured1 Measured2 Mastering1 Mastering2
        { Event::Measured1,     {   false, true,  false,    false,    false,     false } },
        { Event::Measured2,     {   false, false, true,     false,    true,      false } },
        { Event::Mastered,      {   false, false, false,    false,    true,      true  } },
    };
};

} // namespace felitronics::session
