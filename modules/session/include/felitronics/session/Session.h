// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Project.h>
#include <felitronics/session/Events.h>
#include <felitronics/session/Measurements.h>
#include <felitronics/session/Wav.h>
#include <felitronics/session/LandingResult.h>
#include <felitronics/session/Queries.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <cstddef>
#include <span>
#include <string_view>

namespace felitronics::session
{

// Why a call was refused. A refused call did nothing: it allocated nothing, and it created or changed no session.
enum class Status : std::uint8_t
{
    Ok = 0,

    // The calling thread's floating-point environment is not IEEE-754's default: it flushes subnormal results to zero,
    // reads subnormal inputs as zero, or rounds other than to nearest. A host that set FTZ or DAZ for its audio thread
    // and calls the session on it is the usual case. The session's numbers would not be the numbers it computes on any
    // other row, so it refuses rather than answering with them. Nothing in the library changes the environment; the
    // caller restores it (or calls from another thread) and calls again.
    FloatingPointEnvironment = 1,

    ConfigVersion = 2,
    Capabilities = 3,
    Memory = 4,
};

// Shell input; exact byte counts are doubles strictly below 2^53. Device bits follow Device.
struct Capabilities
{
    double heapCeilingBytes = 9007199254740991.0;
    std::uint32_t maxRateHz = 4294967295u;
    std::uint32_t offeredDevices = 255u;
    double largestFreeBlockBytes = 9007199254740991.0;
};

struct Capacity
{
    double heapCeilingBytes = 9007199254740991.0;
    double largestFreeBlockBytes = 9007199254740991.0;
};

struct ProjectText
{
    Rejection rejection = Rejection::None;
    std::unique_ptr<char[]> data;
    std::size_t size = 0;
    [[nodiscard]] std::string_view view() const noexcept;
};

// Summed high-pass + tilt + low response, in Hz and dB.
struct EqPoint { double hz = 0.0, db = 0.0; };
inline constexpr std::size_t kEqCurvePoints = 128;

// THE PLAN OF THE DEVICES (src/Planner.h): where the machine's placement for this source and target stands. The machine's
// layer is written as soon as the first measurement ends (and again on a change of target); what a device then still
// reads — the tempo the glue's release follows, the needles the limiter's peak clipper is classed by — ends later, and
// until it has, the panel shows the machine's layer read-only.
//   None         nothing to plan: no source, or its first measurement has not ended
//   Pending      a measurement the target's devices read is still running
//   Stopped      ...and it was stopped: continueMeasurement resumes it (a master resumes it by itself)
//   Ready        every measurement the devices read has ended — with a value, or with its reason
//   Unavailable  the first measurement ended without a usable loudness or true peak: no plan, and no master
enum class PlanStatus : std::uint8_t { None, Pending, Stopped, Ready, Unavailable };

// WHY THE MACHINE HOLDS A DEVICE BACK: a code, never a text.
//   None        nothing is held back
//   Shell       the shell does not offer the device (Capabilities::offeredDevices)
//   Source      the source cannot take it: mono bass on a mono source
//   Target      the target rules it out: dither above the bit depth it serves, needles where it has no peak clipper
//   Unmeasured  a measurement it reads ended without a value, and it took its safe path
//   Measured    a measurement ruled against it (mono bass: bass in opposite polarity)
//   Quiet       the input is too quiet to measure: below [input] quiet.gainOnlyLufs the machine places no device
enum class HeldBack : std::uint8_t { None, Shell, Source, Target, Unmeasured, Measured, Quiet };

// WHERE THE MACHINE'S HIGH-PASS CUTOFF CAME FROM (owner decisions 3.2–3.4) — always on, the cutoff:
//   Note         the cutoff the sure lowest note allows: the target's noteLossDb at the note, on the chain's response
//   Floor        a sure note above the target's floor whose cutoff is below it: the floor, taking more of the note
//   BelowFloor   a sure note below the target's floor itself (an 808, a sub): the floor, cutting into the note
//   Top          a sure note whose cutoff is above the machine's top (hzMax): the top — the note is higher
//   Unsure       no sure lowest note: the floor
//   Short        a programme shorter than [input] shortSeconds: not searched, the floor
//   Quiet        an input too quiet to measure: the floor
//   Unmeasured   the low end was not measured: the floor
enum class HpfCut : std::uint8_t { Note, Floor, BelowFloor, Top, Unsure, Short, Quiet, Unmeasured };
struct HpfFinding
{
    HpfCut cut = HpfCut::Unmeasured;
    double cutoffHz = 0.0;                     // the machine's cutoff
    std::optional<std::int32_t> noteMidi;      // the sure lowest note, when there is one
    std::optional<double> noteHz;              // ...its band's centre, Hz
    std::optional<double> noteLossDb;          // what the machine's high-pass takes there, dB, positive
};

// WHAT MONO BASS FOUND (owner decision 3.5) — the loss of the low end when it folds to mono, (L+R)/2, below the target's
// crossover, where the bass sounds:
//   On           under [monoBass] loss.warnFromDb: placed
//   Partial      from warnFromDb to offAboveDb, both included: placed, with the number
//   AntiPhase    above offAboveDb: left out — "check the polarity of a channel"; a person may switch it on
//   Unmeasured   the loss could not be weighed (too little bass sounding, or no low-end reading): left out
//   MonoSource   a mono input has no side to fold
//   Quiet        an input too quiet to measure
enum class MonoBassVerdict : std::uint8_t { On, Partial, AntiPhase, Unmeasured, MonoSource, Quiet };
struct MonoBassFinding
{
    MonoBassVerdict verdict = MonoBassVerdict::Unmeasured;
    double crossoverHz = 0.0;                  // the crossover it was weighed at: the target's
    std::optional<double> lossDb;              // the measured loss, dB
    double soundingSeconds = 0.0;              // how long the bass sounds, in the blocks it was weighed over
    // The project's mono bass is on where the machine would leave it out (a person switched it on): the warning beside
    // the tick and in the report.
    bool againstMachine = false;
};

// ONE DEVICE'S PLAN — typed facts about its machine layer: what held it back, what it reads, and where each of its
// machine fields came from. A field is a bit, in the order Project.h writes the device's fields (the order of
// MachineDifference::field): `target` — its target decided it; `measured` — a measurement did; neither — the config's
// default. A person's touched fields are the hand layer's; a machine layer read from a project file is PlanView::fromFile.
struct DevicePlan
{
    HeldBack heldBack = HeldBack::None;
    std::uint32_t needs = 0;               // the analyzers it reads for what the project makes it do, a bit per Analyzer
    std::uint8_t target = 0;
    std::uint8_t measured = 0;
    // The tick as it sounds, and where it came from — so a shell draws it without deciding. The limiter has no tick:
    // always on, the machine's.
    bool on = false;
    TickFrom tick = TickFrom::Machine;
};

// Every device's plan, in the order of Device.
struct DevicePlans
{
    DevicePlan hpf, monoBass, glue, saturation, tilt, limiter, dither, low;
};

// WHAT THE GLUE COMES TO (owner decisions 3.8, 3.8а) — the knob "up to N dB" as the compressor gets it:
//   Out          not in the chain: unticked, or at 0 dB
//   Active       compressing: the static curve takes N dB at the input's short-term P95; the values below are the chain's
//   Unavailable  ticked above 0 dB, but the input has no short-term P95: out of the chain, for the machine and for a
//                person alike — the person's value is kept, no P95 is invented, and a master is made without the glue
enum class GlueState : std::uint8_t { Out, Active, Unavailable };
struct GlueFinding
{
    GlueState state = GlueState::Out;
    double upToDb = 0.0;                       // the knob as it sounds ([glue] whenTicked when ticked on untouched)
    std::optional<double> ratio, thresholdDb, kneeDb, attackMs;   // Active; the threshold in the normalised input's dB
    // ...and once the tempo is decided — the measured one, or [compressor.tempo] bpmWhenUnsure: the tempo, the release
    // it asks for (a beat over the travel's divisor) and the release the compressor gets, inside [compressor.limits].
    std::optional<double> bpm, releaseAskedMs, releaseMs;
    bool releaseClamped = false;               // the release asked for was outside the limits: releaseMs is the limit
    bool tempoMeasured = false;                // the release follows the measured tempo, not the fallback
};

// WHAT THE SATURATION COMES TO (owner decision 3.9): active when ticked above 0 dB; the shaper's drive is the knob's at
// the input's true peak after the normalising gain.
struct SaturationFinding
{
    bool active = false;
    double knobDb = 0.0;
    std::optional<double> driveDb, peakDbTp;
};

struct PlanView
{
    PlanStatus status = PlanStatus::None;
    // The identity of everything the plan was made from — the source and its measurements' keys and outcomes, the
    // target and its edited numbers, a person's layer, the devices offered, the config and core versions. Equal keys,
    // one plan: the planner runs again only when one of them changed.
    std::uint64_t key = 0;
    // The analyzers the project's devices read — the machine's layer with a person's over it — a bit per Analyzer, and
    // those of them that have not ended. With the panel open a master waits for `waiting` to empty
    // (Rejection::PlanPending); with the panel hidden a master is taken and waits for it itself.
    std::uint32_t needs = 0, waiting = 0;
    // What the master button names while it waits: the first waited-for analyzer (tempo before needles), the device that
    // reads it, and how far its measurement is, 0…1.
    std::optional<Analyzer> awaited;
    std::optional<Device> awaitedBy;
    double awaitedFraction = 0.0;
    // The panel takes no device edit: the plan is not Ready (the table's Unplaced columns).
    bool readOnly = true;
    // The machine's layer is an imported project's, kept as the file wrote it; machineDifferences says where the
    // planner would decide otherwise now, and adoptMachine takes its decisions.
    bool fromFile = false;
    DevicePlans devices {};
    HpfFinding hpf {};
    MonoBassFinding monoBass {};
    // The one gain that brings the input to [input] referenceLufs, dB — the system the glue's threshold and the
    // saturation's drive are read in. The landing search applies it, once.
    std::optional<double> inputGainDb;
    GlueFinding glue {};
    SaturationFinding saturation {};
};

// THE FINDINGS AS FACTS — the report's lines, typed: what the high-pass stood on, and, where mono bass has something to
// say (the bass partly or wholly in opposite polarity, or not weighed), that.
struct PlanText
{
    [[nodiscard]] static text::Fact hpf (const HpfFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> monoBass (const MonoBassFinding& finding) noexcept;
    // The glue's refusal and its clamps, each with its reason: unavailable without a P95; the release on the fallback
    // tempo; the release held at a limit.
    [[nodiscard]] static std::optional<text::Fact> glue (const GlueFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> glueTempo (const GlueFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> glueRelease (const GlueFinding& finding) noexcept;
};

class Session;
class Snapshot;
struct SnapshotView;

// What create() gives back: a session, or the reason there is none (`session` null, `status` not Ok).
struct Created
{
    Status status = Status::Ok;
    std::unique_ptr<Session> session;
};

// THE RECIPE OF A MASTER — what a master is made from, captured when it is asked for: the project as it was then (the
// machine's layer included), the source it renders and the config's sound version. Two masters with equal recipes,
// made by one release, are one master.
struct Recipe
{
    Project project {};
    std::uint64_t source = 0;                  // the source's hash (Session::source)
    std::uint64_t sound = 0;                   // config::Config::versions().sound
    std::uint64_t readyHash = 0;              // exact ready topology, parameters and delivery choice
    std::uint32_t deliveryRateHz = 0;
    std::uint32_t readyVersion = 0;
};

// A master kept: its id and its recipe.
struct Kept
{
    MasterId id = 0;
    Recipe recipe {};
    std::optional<LandingSummary> landing;
    std::optional<MasterReport> report;
};

// The source a load gave, as the session holds it. The name and the samples live in the session until a different source is loaded.
struct Source
{
    std::uint32_t channels = 0;                // 0: nothing loaded
    std::uint32_t sampleRate = 0;
    std::uint64_t frames = 0;
    std::uint64_t hash = 0;                    // 64-bit FNV-1a of the rate, the channels, the frames and every sample's bits
    std::uint32_t fileRate = 0;
    bool rateKnown = false;
    std::uint8_t bitDepth = 0;
    std::string_view name;
};

namespace detail { struct Driver; struct Inspector; struct PlanInputs; struct MeasurementWorkspace; struct LiveMeasurements; struct SourceMeasurements; struct NeedlesWork; struct NeedlesResult; struct WaveformState; struct QueryCache; struct MasterJob; struct MasterRows; }

struct MasterToken
{
    std::uint64_t source = 0, revision = 0;
    JobId job = 0;
    MasterId master = 0;
};
enum class MasterTransferStatus : std::uint8_t { Ok, Unknown, Stale, TooSmall, Contract };
struct MasterAudio
{
    std::unique_ptr<float[]> samples;
    std::uint64_t frames = 0;
    std::uint32_t channels = 0, sampleRate = 0;
};
struct MasterAudioShape { std::uint64_t frames = 0; std::uint32_t channels = 0, sampleRate = 0; };

//==============================================================================
// felitronics::session::Session — the mastering session: the object a shell (the web worker through the fcsession
// module, a desktop application linking the library, the native CLI fcore_session) talks to. It holds one project, one
// source and the masters made from it, and it moves between the states of Commands.h only by the commands a shell asks
// (apply) and by its own transitions (the endings of the work). docs/SESSION.md has the laws it is held to and what holds
// each one.
//
// A COMPILED LIBRARY, NOT A HEADER. Everything else in this repository is header-only and compiles under its consumer's
// flags. This does not: `felitronics::session` is a static library whose own sources are compiled with ITS flags (no FP
// contraction, no fast-math, no exceptions, no RTTI — modules/session/CMakeLists.txt). The public headers are the whole
// of what a consumer compiles, which is why they carry no function body and no variable that is not constexpr (the
// session-laws lint refuses both): either would be compiled under the consumer's flags.
//
// WHAT THE LIBRARY'S FLAGS DO NOT REACH. Inline code the session shares with the program — a template or inline function
// from a header both include — is compiled once per translation unit that uses it, each copy under that unit's flags,
// and the linker keeps one copy for the program. So a program that links felitronics::session compiles EVERY translation
// unit with the session's FP flags — no contraction (-ffp-contract=off) and no fast-math — and does no partial linking.
// The wasm modules this repository builds are built whole, with those flags, and are not affected. docs/SESSION.md,
// "What the flags do not reach".
//
// ONE OWNER, ONE THREAD AT A TIME. A Session is created on the heap and owned by the caller's unique_ptr; destroying it
// is the unique_ptr's reset. It is neither copied nor moved — a shell holds it by address (the C ABI's handle table
// does). Calls are made from one thread at a time: two threads calling into the library at once is a data race, and
// nothing in the library detects it.
class Session final
{
public:
    // THE DEMAND OF create(), before it is made (law 11d: memory that cannot be had is fatal, so the demand is published
    // instead). The bytes create() requests from the heap, counted by the same expression that sizes the request, so the
    // two cannot drift. REQUESTED bytes: the allocator's own header and alignment are the caller's margin.
    [[nodiscard]] static std::uint64_t createBytes (const Capabilities& capabilities = {}) noexcept;

    // A new session — Empty, at revision 0, on the config's default target, the manual mode off, the devices unplaced
    // (they are placed when the first measurement ends) — or a refusal before anything is allocated:
    // Status::FloatingPointEnvironment, ConfigVersion, Capabilities or Memory. The embedded config was checked at build time.
    // An accepted create never gives a null session: under -fno-exceptions a heap that cannot serve createBytes() ends
    // the process (natively) or the module (wasm) inside this call, which is what the demand above keeps a shell clear of.
    [[nodiscard]] static Created create() noexcept;
    [[nodiscard]] static Created create (const Capabilities& capabilities, std::uint64_t configVersion) noexcept;
    [[nodiscard]] static Status checkCreate (const Capabilities& capabilities, std::uint64_t configVersion) noexcept;
    [[nodiscard]] double liveBytes() const noexcept;
    [[nodiscard]] const Capabilities& capabilities() const noexcept;
    // Update between calls; a smaller capacity is allowed and the next allocation checks it.
    [[nodiscard]] Status setCapacity (const Capacity& capacity) noexcept;

    // Is the calling thread's floating-point environment IEEE-754's default — no flush-to-zero, no denormals-are-zero,
    // rounding to nearest? Status::Ok if it is, Status::FloatingPointEnvironment if not. Read with ordinary arithmetic;
    // it changes nothing. Every call that computes asks it first: create(), check(), apply(), step(), codec entry points.
    [[nodiscard]] static Status checkFloatingPointEnvironment() noexcept;

    ~Session();

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;
    Session (Session&&) = delete;
    Session& operator= (Session&&) = delete;

    // THIS LIBRARY's release (felitronics-mastering-core) and the felitronics-core it was compiled against — answered by
    // the compiled library, not by this header, so they name the binary that runs, whatever header a consumer included.
    // Both numbers decide results: a recipe replayed on another pair is a different master.
    [[nodiscard]] static Version version() noexcept;
    [[nodiscard]] static Version coreVersion() noexcept;

    //==========================================================================
    // THE COMMANDS (Commands.h)

    // Does the request, whole, or rejects it with no state or revision change. A rejection publishes an event and advances seq.
    // The checks run in the order Commands.h declares: the floating-point environment, then the table, then the rest. Every accepted request moves
    // the revision by one, and so does each of the session's own transitions; a rejected one leaves it as it was.
    [[nodiscard]] Answer apply (const Request& request) noexcept;

    // Preflight and memory demand (law 11d), also run first by apply(). Typed commands validate fully here.
    // Import checks entry and state, then counts the text through the library without allocating.
    // Its parse/schema work and document refusals use that allowance, plus the session's owned storage.
    [[nodiscard]] Checked check (const Request& request) const noexcept;
    // Allocation-free demand before capacity checks; Load needs shape/meta only, never sample pointers.
    [[nodiscard]] Checked storageFor (const Request& request) const noexcept;
    // A sidecar supplies certified scalar facts before its PCM arrives. Attachment
    // verifies the source hash and schedules only the missing waveform index.
    [[nodiscard]] Checked loadMeasuredStorage (const MeasuredSource& facts) const noexcept;
    [[nodiscard]] Answer loadMeasured (CommandId id, const MeasuredSource& facts) noexcept;
    [[nodiscard]] Checked attachAudioStorage (const Pcm& pcm) const noexcept;
    [[nodiscard]] Answer attachAudio (CommandId id, const Pcm& pcm) noexcept;
    [[nodiscard]] MeasurementStorage measurementStorage (const Pcm& pcm) const noexcept;
    // Additional job demand, before preparation; includes the analyzer run list, owned aggregates, copy and codec.
    [[nodiscard]] Checked needlesStorage (double ceilingDb) const noexcept;
    [[nodiscard]] JobId needlesJob() const noexcept;
    [[nodiscard]] QueryDemand queryStorage (const MeasurementQuery& request) const noexcept;
    [[nodiscard]] QueryResult query (const MeasurementQuery& request) noexcept;
    // Explicit deep zoom: the caller supplies at most 65536 delivered PCM frames in planar
    // order, exactly covering request [fromFrame,toFrame). No master PCM is retained or rendered.
    [[nodiscard]] QueryDemand masterWaveformChunkStorage (const MeasurementQuery& request, const Pcm& chunk) const noexcept;
    [[nodiscard]] QueryResult masterWaveformChunk (const MeasurementQuery& request, const Pcm& chunk) noexcept;
    [[nodiscard]] Answer rejectProtocol (CommandId id) noexcept;

    // Export is an owned exact byte allocation, without a terminator. A project is exportable once the machine's layer is
    // written (the first measurement ended), while what its devices read still ends too.
    [[nodiscard]] Checked exportProjectBytes() const noexcept;
    [[nodiscard]] ProjectText exportProject() const noexcept;
    [[nodiscard]] Rejection exportProject (std::span<char> output) const noexcept;
    [[nodiscard]] Answer importProject (CommandId id, std::string_view bytes) noexcept;

    // Each live measurement unit prepares one analyzer, reads at most 1024 source frames, drains new rows,
    // or advances finalization. A call takes at most kStepUnits units;
    // zero polls. With no work, even a hostile FP environment returns Done without an event or a seq change.
    // An FP refusal while work remains publishes Error{Refusal, Continue}; restoring the environment permits resumption.
    // Drain/copy events() after each apply/step, before the next call replaces the batch.
    // The batch lives inside createBytes(). Needles preparation uses its separate needlesStorage demand;
    // loudness/report preparations use the whole measurement demand published before load.
    // stepBytes() is additional transient storage beyond those declared preparations and retained output buffers.
    [[nodiscard]] static std::uint64_t stepBytes() noexcept;
    [[nodiscard]] Stepped step (std::uint32_t budget) noexcept;
    [[nodiscard]] std::span<const Notification> events() const noexcept;
    [[nodiscard]] JobId measurementJob() const noexcept;
    [[nodiscard]] std::uint64_t snapshotBytes() const noexcept;
    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] std::uint64_t summaryBytes() const noexcept;
    [[nodiscard]] Snapshot summary() const noexcept;

    //==========================================================================
    // WHAT THE SESSION HOLDS — read between calls; a reference stays valid until the next call that changes the session.

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool mastering() const noexcept;
    [[nodiscard]] Column column() const noexcept;          // the state and the overlay, as the tables' column
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const Project& project() const noexcept;
    [[nodiscard]] std::string_view targetName() const noexcept;   // the key of the project's target row
    [[nodiscard]] Source source() const noexcept;
    [[nodiscard]] JobId job() const noexcept;               // the master being made; 0 when none is
    [[nodiscard]] const Recipe& jobRecipe() const noexcept; // ...and its recipe (meaningless when job() is 0)
    [[nodiscard]] std::span<const Kept> masters() const noexcept;   // the masters kept, in the order they were made
    [[nodiscard]] MasterToken pendingMaster() const noexcept;
    [[nodiscard]] std::uint64_t masterAudioBytes (MasterToken token) const noexcept;
    [[nodiscard]] MasterAudioShape masterAudioShape (MasterToken token) const noexcept;
    [[nodiscard]] MasterTransferStatus copyMaster (MasterToken token, std::span<float> output) const noexcept;
    [[nodiscard]] std::span<const float> viewMaster (MasterToken token) const noexcept;
    [[nodiscard]] MasterTransferStatus takeMaster (MasterToken token, MasterAudio& output) noexcept;
    [[nodiscard]] MasterTransferStatus releaseMaster (MasterToken token) noexcept;
    [[nodiscard]] WavPlan masterWavPlan (MasterToken token) const noexcept;
    [[nodiscard]] MasterTransferStatus copyMasterWav (MasterToken token, std::uint64_t offset,
                                                     std::span<std::uint8_t> output) const noexcept;

private:
    friend class Wire;
    friend struct detail::Driver;   // the session's own transitions, driven by the work that ends (src/Driver.h)
    friend struct detail::MasterJob;
    // Defined by the state and event suites to reach the last job id, Driver failures and batch bounds;
    // the library defines none.
    friend struct detail::Inspector;

    Session() noexcept = default;
    Capabilities capabilities_ {};
    [[nodiscard]] Checked demand (const Checked& storage) const noexcept;
    [[nodiscard]] Answer reject (Answer answer) noexcept;

    // Are the devices placed — the machine's layer written for this source and target, and every measurement the
    // target's devices read ended (PlanStatus::Ready)? The table's placed columns; the Unplaced ones show the panel
    // read-only.
    [[nodiscard]] bool placed() const noexcept;

    void emit (Notification event) noexcept;
    void emit (Notification event, const Phase& progress) noexcept;
    [[nodiscard]] Phase jobProgress (JobId job) const noexcept;
    void dropJob (JobId job) noexcept;
    void clearMasters() noexcept;
    void settleMasterCrest (MeasurementReason reason) noexcept;
    void stepMasterCrestJoin() noexcept;
    [[nodiscard]] SnapshotView buildView() const noexcept;
    [[nodiscard]] SnapshotView buildSummary (std::span<MeasurementResult> results) const noexcept;
    [[nodiscard]] bool hasWork() const noexcept;
    void requestNeedles() noexcept;
    void stepNeedles() noexcept;
    void stepMeasurements() noexcept;
    void stepWaveform() noexcept;
    void stepSourceMeasurements() noexcept;
    void invalidateQueryCache (Analyzer analyzer) noexcept;
    [[nodiscard]] bool mandatoryReady() const noexcept;
    [[nodiscard]] TempoChoice tempoForDevice() const noexcept;
    // THE PLAN (src/Planner.h). What the planner may read for `project`'s target; the planner's machine layer for
    // `project` written into its devices (a person's layer stays, but for a device the shell does not offer); what
    // `project`'s devices read that has not ended; and the plan the snapshot and the table read, refreshed after every
    // accepted command and every unit of work — the planner runs again only when an input changed (plan_.key;
    // planRuns_ counts its runs).
    [[nodiscard]] detail::PlanInputs planInputs (const Project& project) const noexcept;
    void place (Project& project) const noexcept;
    [[nodiscard]] std::uint32_t planWaiting (const Project& project) const noexcept;
    void replan() noexcept;
    // A needed tempo goes ahead of the optional analyzers the source's job has left, at an analyzer's boundary; the job
    // returns to them after it, repeating nothing.
    void preferTempo() noexcept;
    // The loudness need `project`'s target sets the input — the needles are measured at the input's peak less it.
    [[nodiscard]] std::optional<double> needlesNeed (const Project& project) const noexcept;
    void clearNeedles() noexcept;
    void needlesChanged() noexcept;
    std::unique_ptr<detail::NeedlesWork> needlesWork_;
    std::unique_ptr<detail::NeedlesResult> needlesResult_;
    JobId needlesJob_ = 0;
    std::uint64_t needlesSource_ = 0, needlesKey_ = 0;
    std::optional<double> needlesNeedDb_, needlesCeilingDb_;
    Phase needlesProgress_ {};
    Checked needlesDemand_ {};
    JobId measurementJob_ = 0;
    std::uint32_t measurementUnit_ = 0, masterUnit_ = 0;
    Phase measurementProgress_ {}, masterProgress_ {};
    LandingSummary masterSummary_ {};
    std::uint32_t masterTraceCursor_ = 0;
    bool masterTraceActive_ = false;
    Notification events_[kEventBatch] {};
    std::size_t eventCount_ = 0;
    std::uint64_t sequence_ = 0;

    State state_ = State::Empty, stoppedState_ = State::Loaded;
    bool mastering_ = false, devicesPlaced_ = false;
    std::uint64_t revision_ = 0;
    Project project_ {};
    MachineDifference differences_[kDeviceFields] {};
    std::size_t differenceCount_ = 0;
    PlanView plan_ {};
    std::uint64_t planRuns_ = 0;
    bool machineFromFile_ = false;
    // A master asked for with measurements its devices read still running: its recipe's project is the one captured then
    // (the target, its numbers, a person's layer), and its machine's layer is placed for that project when they end.
    bool jobWaiting_ = false;
    // Derived at placement and after accepted commands; owned snapshots copy these points.
    void refreshEqCurve() noexcept;
    EqPoint eqCurve_[kEqCurvePoints] {};
    // OWNED BUFFERS, EACH ONE EXACT REQUEST — not std::vector: a debugging standard library (MSVC's at
    // _ITERATOR_DEBUG_LEVEL 1 or 2) gives every vector a heap-allocated proxy of its own, which no declared demand
    // counts. The source: its samples planar, channel after channel (source_.channels × source_.frames), and the name
    // the load gave (source_.name views it; null when it is empty).
    Source source_ {};
    MeasurementStorage measurementStorage_ {};
    std::uint64_t measurementKey_ = 0;
    MeasurementResult measurementResults_[kAnalyzers] {};
    OwnedMeasurements measurementOwners_[kAnalyzers];
    MeasurementValue sidecarNumbers_[2] {};
    bool measurementsFromSidecar_ = false;
    std::uint64_t measurementOwnedBytes_ = 0;
    std::unique_ptr<detail::MeasurementWorkspace> measurementWorkspace_;
    std::unique_ptr<detail::LiveMeasurements> liveMeasurements_;
    std::unique_ptr<detail::SourceMeasurements> sourceMeasurements_;
    std::unique_ptr<detail::WaveformState> waveform_;
    std::unique_ptr<detail::QueryCache> queryCache_;
    std::unique_ptr<float[]> samples_;
    std::unique_ptr<char[]> name_;
    // The master being made, and the masters kept: `masterCount_` of them in room for `masterRoom_`. The ids count
    // from 1 for the session's life.
    JobId job_ = 0;
    JobId lastJob_ = 0;
    Recipe jobRecipe_ {};
    std::unique_ptr<Kept[]> masters_;
    std::unique_ptr<detail::MasterRows[]> masterRows_;
    std::unique_ptr<detail::MasterJob> masterJob_;
    std::uint64_t masterJobBytes_ = 0;
    MasterToken pendingMaster_ {};
    MasterAudio masterAudio_ {};
    std::uint32_t masterAudioBits_ = 0;
    std::size_t masterCount_ = 0;
    std::size_t masterRoom_ = 0;
    std::size_t crestJoinIndex_ = 0;
    bool crestJoin_ = false;
    MeasurementReason crestJoinReason_ = MeasurementReason::None;
};

} // namespace felitronics::session
