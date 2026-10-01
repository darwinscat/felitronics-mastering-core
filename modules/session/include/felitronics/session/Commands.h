// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Project.h>
#include <felitronics/mastering/MasteringChain.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

//==============================================================================
// felitronics::session — THE STATES, THE COMMANDS AND WHO MAY DO WHAT, WHEN.
//
// A session is in one of five states, and a master being made is an overlay on the two measured ones:
//
//     Empty ──load──▶ Loaded ──(the first measurement ends)──▶ Measured1 ──(the second ends)──▶ Measured2
//                                                                  └ master ──▶ Mastering ◀── master ┘
//
//   Empty      nothing loaded; a target and the manual mode can be chosen already.
//   Loaded     a source is loaded and its first measurement has not ended. The devices are not placed: their panel is closed.
//   Measured1  the first measurement ended: the planner places the devices (src/Planner.h) and a master can be made.
//              Device edits are taken once what the devices read has ended — the plan Ready (the Unplaced columns).
//   Measured2  the second measurement ended too.
//   MeasurementStopped retains audio, results and cursor; ContinueMeasurement restores the previous phase.
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

enum class State : std::uint8_t { Empty, Loaded, Measured1, Measured2, MeasurementStopped };

// The commands, in the order of the table's rows and of Request's alternatives.
enum class Command : std::uint8_t { Load, SetTarget, EditTarget, EditDevice, RevertEdits, SetManual, Master, Cancel, Forget, ImportProject, ContinueMeasurement, AdoptMachine };
inline constexpr std::size_t kCommands = 12;

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
    NotPlaced,                  // the devices have not been placed yet
    NotMeasured,                // the first measurement has not ended
    Busy,                       // a master is being made
    NoJob,                      // no named measurement or master is running
    NoMaster,                   // no master is kept in this state
    // what the command names
    UnknownTarget,              // no row of [targets] has this name
    NotOffered,                 // this device is excluded by the shell, dither above the bit depth it serves,
                                // or mono bass on a mono source
    UnknownJob,                 // not an active measurement or master
    UnknownMaster,              // no master kept under this id
    // the fields
    NotFinite,                  // a value or a sample that is not a finite number
    NotOneOf,                   // not one of the values this field takes (a slope, a mode)
    OutOfDomain,                // outside the knob's accepted domain
    // the audio (load)
    BadChannels,                // not one or two channels
    BadRate,                    // a sample rate below 8000 Hz (felitronics-core's lowest)
    NoAudio,                    // no frames, or no data
    TooLong,                    // more samples than this machine's memory can address
    // what the command names (continued)
    NoJobId,                    // every job id was issued: load and master cannot start another job
    InvalidUtf8,                // a load's name is not valid UTF-8
    ProjectTooLarge,            // before reading any input
    ProjectSyntax,              // felitronics-toml refused the document
    ProjectMissing,             // a required field is absent
    ProjectType,                // a field has another TOML type
    ProjectUnknownKey,          // an unknown section, knob or author suffix
    UnknownDefaults,            // malformed defaults label, or an older label this core does not carry
    ProjectCore,                // core must be a canonical major.minor.patch version
    NewerDefaults,              // defaults are newer than the current compiled table
    RateAboveLimit,             // above the shell's maxRateHz
    Memory,                     // live bytes plus demand exceeds capacity, or a block cannot fit
    Contract,                   // malformed command or metadata at the protocol boundary
    OutputPending,              // the previous master still owns transferable PCM
    MandatoryUnavailable,       // the source LUFS or true peak is not usable
    DeliveryFormat,             // a master's delivery rate or bits are not the target's, its only format
    PlanPending,                // the panel is open and a measurement the devices read has not ended (PlanView::waiting)
};

using CommandId = std::uint64_t;   // the shell's own number for a request, given back in its answer
using JobId = std::uint32_t;       // a measurement or master job: 1, 2, … in the order asked, never 0 and never twice in a
                                   // session's life (after the last one, neither load nor master starts another job)
using MasterId = std::uint32_t;    // a master kept: the id of the job that made it

inline constexpr std::uint8_t kNoField = 0xFF;

// A DEVICE'S EDIT: which device (the alternative, in the order of Device) and the fields a person touched.
using DeviceEdit = std::variant<HpfFields<Touched>, MonoBassFields<Touched>, GlueFields<Touched>, SaturationFields<Touched>,
                                TiltFields<Touched>, LimiterFields<Touched>, DitherFields<Touched>, LowFields<Touched>,
                                BandsFields<Touched>>;
// ...and the fields a revert takes back.
using DeviceMask = std::variant<HpfFields<Mark>, MonoBassFields<Mark>, GlueFields<Mark>, SaturationFields<Mark>,
                                TiltFields<Mark>, LimiterFields<Mark>, DitherFields<Mark>, LowFields<Mark>,
                                BandsFields<Mark>>;

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
// Replaces the target's numbers, resets every device edit, and places the machine's layer again when ready.
struct SetTarget   { CommandId id = 0; std::string_view target; };
struct EditTarget  { CommandId id = 0; TargetFields<Touched> fields {}; };
struct EditDevice  { CommandId id = 0; DeviceEdit fields {}; };
struct RevertEdits { CommandId id = 0; DeviceMask fields {}; };
struct SetManual   { CommandId id = 0; bool on = false; };
// Version 0: the session decides — the planner's devices. It captures its recipe when it is asked for, and with the
// panel hidden it waits, cancellably, for what those devices read. Version 1 carries the ready topology and parameters
// a shell's Decide supplied; it is a job input, not Project.
struct MasterReady
{
    std::uint32_t version = 0;
    mastering::MasteringChainConfig topology {};
    mastering::MasteringChainParams params {};
    std::uint32_t deliveryRateHz = 0; // 0 or the target's rate (the source's when the target keeps it); else refused
    std::uint8_t deliveryBits = 0;    // 0 or the target's bit depth; any other value is refused
};
struct Master      { CommandId id = 0; MasterReady ready {}; std::uint64_t source = 0, revision = 0; };
struct Cancel      { CommandId id = 0; JobId job = 0; };
struct Forget      { CommandId id = 0; MasterId master = 0; };
struct ContinueMeasurement { CommandId id = 0; };
struct ImportProject { CommandId id = 0; std::string_view bytes; };
// Takes the planner's machine layer for the project as it stands — after an import, the decisions the file's differ
// from (machineDifferences). A person's layer stays. With nothing to take it is accepted without a revision.
struct AdoptMachine { CommandId id = 0; };
} // namespace command

// Any request: the alternative is the command, in the order of Command.
using Request = std::variant<command::Load, command::SetTarget, command::EditTarget, command::EditDevice,
                             command::RevertEdits, command::SetManual, command::Master, command::Cancel, command::Forget, command::ImportProject, command::ContinueMeasurement,
                             command::AdoptMachine>;

// THE ANSWER — whole: accepted with the revision the command made, or rejected with no state change, but an event.
struct Answer
{
    CommandId command = 0;                     // the request's own id
    Rejection rejection = Rejection::None;     // None: accepted
    std::uint64_t revision = 0;                // accepted: the revision it made; rejected: the revision, unchanged
    JobId job = 0;                             // an accepted load or master: the job it started
    double needBytes = 0.0;                    // Memory: live declared bytes plus the refused demand, below 2^53
    ProjectPosition position {};              // import: 1-based Unicode line and column; zero for preflight refusals
    std::optional<Device> device;              // import: absent for a target field
    std::uint8_t field = kNoField;             // rejected on a field: its place among the fields, in the order written
                                               // (the target's: lufs 0, tp 1; a device's: as its Fields lists them)
    std::uint8_t targetBits = 0;               // DeliveryFormat: the target's bit depth and rate (the source's when the
    std::uint32_t targetRate = 0;              // target keeps it), the one format its master takes
};

// WHAT A REQUEST WOULD DO, BEFORE IT IS DONE — Session::check(): the answer it would get, and the bytes it would ask the
// heap for (law 11d). Import checks entry, state and representable demand here. The library counts the text without
// allocation; parsing and schema validation run inside apply(), including document refusals.
struct Checked
{
    Rejection rejection = Rejection::None;
    std::uint8_t field = kNoField;
    std::uint64_t bytes = 0;                   // preflight passed: demand of apply(), including import refusals; otherwise 0
    double needBytes = 0.0;                    // Memory: total declared live bytes and demand
    std::uint64_t largestBlockBytes = 0;       // largest single allocation (import: conservative bound)
};

// A session's situation: measurement state, master overlay and device placement. The measured columns stand for devices
// placed with their plan Ready; the Unplaced ones for the same states before the devices are placed, or while a
// measurement they read still runs (PlanStatus Pending or Stopped) — the panel shows the machine's layer read-only.
enum class Column : std::uint8_t
{
    Empty, Loaded, Measured1, Measured2, Mastering1, Mastering2, Stopped, StoppedMeasured, MasteringStopped,
    Measured1Unplaced, Measured2Unplaced, Mastering1Unplaced, Mastering2Unplaced, StoppedMeasuredUnplaced, MasteringStoppedUnplaced
};
inline constexpr std::size_t kColumns = 15;

//==============================================================================
// THE TABLE — who may do what, when. One row per command: in each column, None where the command is taken, or the
// rejection it gets there. Mastering1 and Mastering2 are a master being made on Measured1 and on Measured2.
//
// THE ORDER OF THE CHECKS, the same for every command — the first that fails is the answer, and nothing has changed:
//   1. ENTRY    the calling thread's floating-point environment (FloatingPointEnvironment)
//   2. STATE    this table: the command's cell in the session's column
//   3. NAMES    what the command names: a target (setTarget), a device offered for this target and source (editDevice,
//               revertEdits), the source's audio (master: NoAudio — a sidecar source before attachAudio), with the panel
//               open a plan whose measurements have ended (a master the session decides: PlanPending), an id left
//               for a new job (load, master: NoJobId), a load's UTF-8 name (InvalidUtf8), the active job (cancel:
//               NoJob when none runs, UnknownJob when another does), a master kept (forget)
//   4. FIELDS   each touched field, in the order written: finite, one of its values, within its domain.
//               Empty edits/reverts are accepted without a revision change; travel and step are slider hints.
//   5. AUDIO    a load's audio: its channels, its rate, its frames, its size, then every sample finite
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
        { Command::Load,        {    None,     None,        None,     None,     None,      None, None, None, None, None, None, None, None, None, None } },
        { Command::SetTarget,   {    None,     None,        None,     None,     None,      None, None, None, None, None, None, None, None, None, None } },
        { Command::EditTarget,  {    None,     None,        None,     None,     None,      None, None, None, None, None, None, None, None, None, None } },
        { Command::EditDevice,  {    NoSource, NotPlaced,   None,     None,     None,      None, NotPlaced, None, None, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced } },
        { Command::RevertEdits, {    NoSource, NotPlaced,   None,     None,     None,      None, NotPlaced, None, None, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced } },
        { Command::SetManual,   {    None,     None,        None,     None,     None,      None, None, None, None, None, None, None, None, None, None } },
        { Command::Master,      {    NoSource, NotMeasured, None,     None,     Busy,      Busy, NotMeasured, None, Busy, None, None, Busy, Busy, None, Busy } },
        { Command::Cancel,      {    NoJob,    None,        None,     None,     None,      None, None, None, None, None, None, None, None, None, None } },
        { Command::Forget,      {    NoSource, NoMaster,    None,     None,     None,      None, NoMaster, None, None, None, None, None, None, None, None } },
        { Command::ImportProject, { NoSource, NotPlaced,   None,     None,     None,      None, NotPlaced, None, None, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced } },
        { Command::ContinueMeasurement, { NoJob, NoJob, NoJob, NoJob, NoJob, NoJob, None, None, None, NoJob, NoJob, NoJob, NoJob, None, None } },
        { Command::AdoptMachine, { NoSource, NotPlaced,   None,     None,     None,      None, NotPlaced, None, None, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced, NotPlaced } },
    };

    // Import: entry, state, size (no input read), then TOML syntax, schema in canonical field order,
    // defaults version, core version, target name, offered-device constraints. The saved machine layer is kept; where the
    // planner would decide otherwise for the file's target on this source is reported, and adoptMachine takes it.
    // The file supplies manual mode; the current mode is not a prerequisite for restoring a project.

    // Cancelling measurement enters MeasurementStopped and retains its source, results and progress.
    // ContinueMeasurement restores the previous measurement phase with a fresh job identity.
    // Cancelling a master ends its overlay and preserves the measurement state.
    // THE SESSION'S OWN TRANSITIONS — where each may happen (true), and what it does:
    //   Measured1  the first measurement ended: Loaded becomes Measured1 and the planner places the devices — the pump
    //              and a fixture's seam alike
    //   Measured2  the second ended: Measured1 becomes Measured2, with a master being made or not
    //   Mastered   the master being made is done: it is kept, and the overlay ends
    struct EventRow
    {
        Event event;
        bool cell[kColumns];
    };

    static constexpr EventRow events[kEvents] = {
        //                          Empty  Loaded Measured1 Measured2 Mastering1 Mastering2
        { Event::Measured1,     {   false, true,  false,    false,    false,     false, false, false, false, false, false, false, false, false, false } },
        { Event::Measured2,     {   false, false, true,     false,    true,      false, false, false, false, true, false, true, false, false, false } },
        { Event::Mastered,      {   false, false, false,    false,    true,      true, false, false, true, false, false, true, true, false, true } },
    };
};

} // namespace felitronics::session
