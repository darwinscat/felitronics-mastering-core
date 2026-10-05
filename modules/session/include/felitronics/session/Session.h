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

#include <array>
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

// Shell input; exact byte counts are doubles strictly below 2^53. Device bits follow Device: kAllDevices is every one —
// a shell that predates a device does not set its bit and is not offered it.
inline constexpr std::uint32_t kAllDevices = (1u << (unsigned (Device::Bands) + 1u)) - 1u;
struct Capabilities
{
    double heapCeilingBytes = 9007199254740991.0;
    std::uint32_t maxRateHz = 4294967295u;
    std::uint32_t offeredDevices = kAllDevices;
    double largestFreeBlockBytes = 9007199254740991.0;
    // The summary without the masters' heavy rows: each master keeps its scalars, its pass log and its cost sections;
    // its limiter and peak-clip traces, crest rows and mask and waveform buckets are left out
    // (SnapshotView::masterRowsIncluded false) and read, a master at a time, by QueryKind::MasterReport. The full
    // snapshot is the same either way. Off: the summary as it always was.
    bool leanSummary = false;
};

struct Capacity
{
    double heapCeilingBytes = 9007199254740991.0;
    double largestFreeBlockBytes = 9007199254740991.0;
};

// THE DAMAGE GRADES, a job each (Session::damageJobs): a grade the shell asked for a master kept (command::GradeDamage),
// graded one at a time in the order asked. Waiting — its turn has not come (a master job, the source's measurement or an
// earlier grade runs) or a new master parked it; Running — its walks are being made. A grade ends with its result, or by
// cancel of its job id, forget of its master or a new source; never by a new master.
enum class DamageJobState : std::uint8_t { Waiting, Running };
enum class DamageWaitReason : std::uint8_t { Queue, SourceMeasurement };
struct DamageJobEntry
{
    JobId job = 0;
    MasterId masterId = 0;
    DamageJobState state = DamageJobState::Waiting;
    Phase progress {};
    std::optional<DamageWaitReason> waitReason = std::nullopt;
};
// THE QUEUE'S ROOM: a grade asked past it is refused (Rejection::DamageQueueFull). Bounded so a new source can say every
// grade's last word in its own batch — beside the load's own events (a measured load's one per analyzer, and a few) —
// within kEventBatch.
inline constexpr std::size_t kMaxDamageGrades = 32;
static_assert (kMaxDamageGrades + kAnalyzers + 8 <= kEventBatch, "a new source's last words of every grade fit its batch");

struct ProjectText
{
    Rejection rejection = Rejection::None;
    std::unique_ptr<char[]> data;
    std::size_t size = 0;
    [[nodiscard]] std::string_view view() const noexcept;
};
// A canonical, read-only TOML account of one completed master. Unlike the project file, it is flattened to the values
// that sounded and belongs to the master's captured recipe, so later project edits cannot change it.
struct WorkedText
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
// layer is written as soon as the first measurement ends — the loudness and the true peak — (and again on a change of
// target, and for the fields a measurement decides when it ends); what a device then still reads — the low end the
// high-pass's cutoff and mono bass's tick are decided by, the tempo the glue's release follows, the needles the
// limiter's peak clipper is classed by — ends later. The panel takes edits meanwhile; the plan is not Ready until then.
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
//               (but for the high-pass, at its floor, and the dither of the delivery's format)
//   Pending     a measurement it reads has not ended yet: its fields in DevicePlan::pending are "not measured yet" —
//               the machine fills them when it ends (owner, 02.10)
enum class HeldBack : std::uint8_t { None, Shell, Source, Target, Unmeasured, Measured, Quiet, Pending };

// A FINDING IS THE PLANNER'S PROPOSAL AND WHAT IT STOOD ON — the machine's own cutoff, the loss at the machine's
// crossover. WHAT SOUNDS is the project's device, a person's layer over a machine's that a project file may have
// written; a finding says which, so that no sentence claims for the sound what is true of the proposal alone:
//   Proposal   the planner's own values sound: the finding describes the sound
//   Hand       a person's value sounds instead
//   File       a machine layer kept from a project file sounds, another than the planner proposes now
//   Off        the device is out of the chain
enum class Sounding : std::uint8_t { Proposal, Hand, File, Off };

// WHERE THE MACHINE'S HIGH-PASS CUTOFF CAME FROM (owner decisions 3.2–3.4) — always on, the cutoff:
//   Note         the cutoff the sure lowest note allows: the target's noteLossDb at the note, on the chain's response
//   Floor        a sure note above the target's floor whose cutoff is below it: the floor, taking more of the note
//   BelowFloor   a sure note below the target's floor itself (an 808, a sub): the floor, cutting into the note
//   Top          a sure note whose cutoff is above the machine's top ([hpf] machineTopHz): the top — the note is higher
//   Unsure       the lowest band that was on is not a sure note (or none was on): the floor — never a higher band
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
    Sounding sounding = Sounding::Proposal;    // whether that cutoff, at the target's slope, is what sounds
    double soundingHz = 0.0;                   // the cutoff that sounds (meaningless when Off)
    std::int32_t soundingSlope = 0;            // ...and the slope that sounds, dB/oct (meaningless when Off)
};

// WHAT MONO BASS FOUND (owner decision 3.5) — the loss of the low end when it folds to mono, (L+R)/2, below the target's
// crossover, where the bass sounds:
//   On           under [monoBass] loss.warnFromDb: placed
//   Partial      from warnFromDb to offAboveDb, both included: placed, with the number
//   AntiPhase    above offAboveDb: left out — "check the polarity of a channel"; a person may switch it on
//   Unmeasured   the loss could not be weighed (too little bass sounding, or no low-end reading): left out
//   MonoSource   a mono input has no side to fold
//   Quiet        an input too quiet to measure
// Mono bass stands unless a MEASURED loss rules against it. Where the low-end reading holds only a first part of the
// piece (its blocks are capped, about 10.9 minutes) the loss is weighed over that part and the device placed by it;
// `coveredSeconds` says how much of the piece the weighing covered.
enum class MonoBassVerdict : std::uint8_t { On, Partial, AntiPhase, Unmeasured, MonoSource, Quiet };
struct MonoBassFinding
{
    MonoBassVerdict verdict = MonoBassVerdict::Unmeasured;
    double crossoverHz = 0.0;                  // the crossover it was weighed at: the target's
    std::optional<double> lossDb;              // the measured loss, dB
    double soundingSeconds = 0.0;              // how long the bass sounds, in the blocks it was weighed over
    // Set where the reading holds only the beginning of the piece: the seconds it covers, of `pieceSeconds`.
    std::optional<double> coveredSeconds;
    double pieceSeconds = 0.0;
    // The project's mono bass is on where the machine would leave it out (a person switched it on): the warning beside
    // the tick and in the report.
    bool againstMachine = false;
    Sounding sounding = Sounding::Proposal;    // whether the fold at that crossover, at the machine's width, is what sounds
    double soundingHz = 0.0;                   // the crossover that sounds (meaningless when Off)
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
    // The machine fields with no value yet, a bit per field as `target`: a measurement they are decided by has not ended.
    // The layer holds a placeholder there (the high-pass's floor, mono bass off) that a shell draws as "not measured
    // yet", with no number; the machine fills the field when the measurement ends, a person's value over it untouched.
    std::uint8_t pending = 0;
};

// Every device's plan, in the order of Device.
struct DevicePlans
{
    DevicePlan hpf, monoBass, glue, saturation, tilt, limiter, dither, low, bands;
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
    double mix = 0.0;                          // the parallel share as it sounds (a person's, or the machine's [glue] mix)
    // The knob's numbers — Active, and Out as well (unticked, at 0 dB: what the compressor would get from the knob as it
    // stands, which no compressor gets); none where Unavailable. The ratio, knee and attack always; the threshold, in the
    // normalised input's dB, where the input has a short-term P95.
    std::optional<double> ratio, thresholdDb, kneeDb, attackMs;
    // ...and once the tempo is decided — the measured one, or [compressor.tempo] bpmWhenUnsure: the tempo, the release
    // it asks for (a beat over the travel's divisor) and the release the compressor gets, inside [compressor.limits]. A
    // glue in the chain waits for its tempo; nothing measures the tempo of one out of it, whose release is stated at
    // bpmWhenUnsure until a tempo is decided.
    std::optional<double> bpm, releaseAskedMs, releaseMs;
    bool releaseClamped = false;               // the release asked for was outside the limits: releaseMs is the limit
    bool tempoMeasured = false;                // the release follows the measured tempo, not the fallback
    // Active or Out: the input's short-term P95 on the detector's scale, the normalised input's dB — the level the threshold
    // stands on ([glue] detectorOverP95Db above the P95) and where the static curve takes upToDb: a transfer curve's
    // point for the knob.
    std::optional<double> p95DetectorDb;
    // The tempo the detector gave and the rule did not follow — its label under [compressor.tempo] trustedConfidence: the
    // release stays at bpmWhenUnsure, and the number is said beside it. None where the tempo was followed or gave none.
    std::optional<double> tempoUnsureBpm;
};

// WHAT THE SATURATION COMES TO (owner decision 3.9): active when ticked above 0 dB; the shaper's drive is the knob's at
// the input's true peak after the normalising gain.
struct SaturationFinding
{
    bool active = false;
    double knobDb = 0.0;
    std::optional<double> driveDb, peakDbTp;
};

// THE NEEDLES' CLASS (owner decisions 3.6, 3.7) — what the machine's peak clipper, inside the limiter, makes of the
// needles measured on the input at the ceiling the target's numbers give (the input's peak less the need):
//   None      the machine does not cut: NeedlesWhy says why
//   Short     90 % of the excursions no longer than [limiter.peakClipper] shortP90Ms, the bass share of their dose at most
//             shortBassShare, the input's PLR at least shortPlrDb: up to shortCutDb off the peaks
//   Between   neither short nor ruled out: up to betweenCutDb off the peaks, with care
enum class NeedlesClass : std::uint8_t { None, Short, Between };

// WHY THE MACHINE DOES NOT CUT — the first of these that holds, in this order; Cuts where it does:
//   Shell         the shell does not offer the limiter's knob
//   Target        the target has no peak clipper (vinyl)
//   Quiet         the input is too quiet to measure: the machine places no device
//   NoReadings    the input's loudness or true peak is not known: the need is not known
//   LittleNeed    the need is [limiter.peakClipper] littleNeedDb or less: the needles are not measured and not touched
//   Pending       the needles at this ceiling are not measured yet
//   Unmeasured    the needles' measurement ended without a value (LimiterFinding::reason says how)
//   NoExcursions  measured: no peak of the input stands above the ceiling
//   Clipped       the source is clipped: clippedPerMinute confirmed clips a minute or more — a clipper would add distortion
//   LowPlr        the input's PLR is under longPlrDb
//   Bass          the bass share of the excursions' dose is longBassShare or more
//   Long          90 % of the excursions are longP90Ms or longer
enum class NeedlesWhy : std::uint8_t
{
    Cuts, Shell, Target, Quiet, NoReadings, LittleNeed, Pending, Unmeasured, NoExcursions, Clipped, LowPlr, Bass, Long
};

// WHAT THE LIMITER COMES TO — always in the chain, with no tick; its setting is the ceiling, and its one knob is the
// peak clipper's: decide by itself (the class above), cut as much as a person sets, or not at all. It cuts AT MOST that
// amount off the peaks, the limiter does the rest: its threshold stands max(0, needDb − amount) above the ceiling as a
// forecast; the master's is worked out from the peak the landing measures at the limiter's input, at any landing gain.
struct LimiterFinding
{
    // The ceiling the master holds: the target's, or a person's edit of it, dBTP.
    double ceilingDbTp = 0.0;
    // The loudness need the target sets the input, dB — (input peak − input loudness) − (ceiling − target loudness).
    std::optional<double> needDb;
    // THE MACHINE'S OWN ANSWER, whatever sounds: its class, why it does not cut, and how much it would cut off the peaks.
    NeedlesClass proposed = NeedlesClass::None;
    NeedlesWhy why = NeedlesWhy::NoReadings;
    MeasurementReason reason = MeasurementReason::None;   // why == Unmeasured: how the needles' measurement ended
    std::optional<double> proposedOverDb;      // dB off the peaks, at most; absent where the machine does not cut
    // What the class stood on, where the needles were measured: the excursions' p90 length, the bass share of their
    // dose, the input's PLR; and the source's confirmed clips, in all and a minute (where the clip detector ended).
    std::optional<double> p90Ms, bassShare, plrDb, clipsPerMinute;
    std::uint64_t clips = 0;
    // WHAT SOUNDS: the knob as the project gives it — a person's mode, a threshold a person turned (a knob turned is a
    // device wanted: manual), else the machine's — whether the peak clipper cuts, and how much off the peaks, at most.
    Needles mode = Needles::Auto;
    bool cutting = false;
    double overDb = 0.0;                       // meaningless where it does not cut
    Sounding sounding = Sounding::Proposal;    // Proposal: as the machine answers; Hand, File: otherwise (never Off)
    // A person's manual threshold cuts where the machine would not: the warning beside the knob names `why`.
    bool againstMachine = false;
    // THE MEDIUM'S RULES (owner decision 3.12), on a target cut to vinyl: the machine never stands above the target's
    // own ceiling and never cuts needles; a person may do either, and is warned, never refused.
    bool vinyl = false;
    double mediumCeilingDbTp = 0.0;            // the target's own ceiling (meaningful on vinyl)
    bool ceilingAboveMedium = false;           // a person's ceiling stands above it
    bool needlesAgainstMedium = false;         // the peak clipper cuts on a target that has none
    double vinylTopHz = 0.0;                   // the constant note: above this the cutting room usually rolls off
    // THE LIMITER'S OWN SETTINGS, as the chain is written with them ([limiter] and [chain] of engine.toml): its release,
    // ms; whether a second, slower envelope runs beside it, and that one's release, ms (stated either way); its
    // lookahead, ms; and the oversampling its detector and gain run at, as the limiter takes the factor (a requested 1
    // becomes 2).
    double releaseMs = 0.0;
    bool dualRelease = false;
    double slowReleaseMs = 0.0;
    double lookaheadMs = 0.0;
    std::int32_t oversampling = 0;
};

// THE DITHER'S NOISE SHAPING — the core's dither::NoiseShaping, in its order: none (flat), weighted, psychoacoustic.
enum class DitherShaping : std::uint8_t { None, Weighted, Psychoacoustic };

// WHAT THE DITHER COMES TO — by the delivery's format alone: at [dither] onUpToBits or less it is in the chain unless a
// person switched it off (the delivery is then rounded to its grid without noise); above that there is no dither at
// all, and a person's tick is kept in the project without effect.
struct DitherFinding
{
    std::int32_t bits = 0;                     // the delivery's bit depth: the target's
    bool applies = false;                      // the depth takes dither, and the shell offers the device
    bool on = false;                           // it is in the chain
    bool offByHand = false;                    // it applies, and a person switched it off
    bool keptWithoutEffect = false;            // it does not apply, and a person's tick is kept: nothing sounds of it
    // The shaping the dither runs with at the delivery's depth: [dither] shaping up to shapingUpToBits, none above.
    DitherShaping shaping = DitherShaping::None;
};

// WHERE THE EQ CURVE LEAVES ITS NORM ([eq] curve.warnDb of engine.toml) — judged on the curve eqOnlyCurve draws: tilt, low
// and the EQ bands as they sound, summed at the source's rate on the snapshot's 128 points (owner, 02.10: where the curve is
// red, the line is there); the high-pass is not judged by it (a filter's slope goes down by definition: its own norm is its
// comfort window and its normal slopes). The point of the largest |dB|, the first where two are equal; `over` where that
// exceeds warnDb; `device` the one that gives the largest part of it there — Tilt, Low or Bands (the five together), tilt
// before low before the bands where they give as much.
struct EqFinding
{
    bool over = false;
    double hz = 0.0, db = 0.0;
    Device device = Device::Tilt;
};

// ONE OF THE PLAN'S REASONS: a fact PlanText states, and the device it is said of.
struct PlanFact
{
    Device device = Device::Hpf;
    text::Fact fact {};
};
// Every line PlanText can state of one plan — the high-pass's one and its two pieces of advice, mono bass's three and
// its advice, the glue's three, the EQ curve's advice, the limiter's five, the dither's one — and the waiting fact.
inline constexpr std::size_t kPlanFacts = 18;

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
    // What the master button names while it waits: the first waited-for analyzer (the needles before the tempo — the
    // order a waiting master measures them in), the device that reads it, and how far its measurement is, 0…1.
    std::optional<Analyzer> awaited;
    std::optional<Device> awaitedBy;
    double awaitedFraction = 0.0;
    // The panel takes no device edit: the devices are not placed. Once they are, a person may edit any field — one the
    // machine has not measured yet included (DevicePlan::pending; owner, 02.10).
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
    LimiterFinding limiter {};
    DitherFinding dither {};
    // THE PLAN'S REASONS, as PlanText states them — so a shell shows each device's reasons without composing one. A Ready
    // plan states every line PlanText gives for it, in the order of Device and, within a device, of PlanText's members:
    // the high-pass, its cutoff and its slope against the norm; mono bass, its polarity warning, the part of the piece it
    // was weighed over, its crossover outside every destination's zone; the glue's refusal, its fallback tempo, its held
    // release; the EQ curve beyond its norm, said of tilt; the limiter, the warning beside a manual threshold, vinyl's
    // ceiling, needles and top; the dither; the EQ curve beyond its norm, said of low. Where the plan names what a master waits for (awaited and awaitedBy), the waiting fact
    // (PlanWaiting, said of awaitedBy, its progress awaitedFraction) comes last — and a plan that is not Ready states that
    // fact alone, or nothing. A new plan states its reasons anew; the waiting fact follows the progress between plans.
    BoundedList<PlanFact, kPlanFacts> facts {};
};

// THE OBSERVATIONS (owner decision 3.13) — what the measurements found in the FILE, each a fact with its numbers and
// never a verdict of taste. They INFORM ONLY: no observation switches a device or changes the sound (a device that
// decides from a measurement has its own finding in the plan). The thresholds are [observations] of engine.toml, its
// first edition.
//   status      Found; NotFound — measured, and it is not there; NotMeasured — the measurement it stands on has not
//               ended, or ended without a value (`reason` says how). The three are never folded into one.
//   style       error, warning, note or reading ([observations.kinds]): the nature of the finding, not its size — a
//               reading is a number the file shows, not a finding at all. It styles a finding and decides nothing.
//   confidence  0…1, how firmly it is measured; `doubtful` under [observations] doubtfulBelow — shown all the same.
//   severity    0…1, how much it matters for the master. Both rise along the config's ramps; a kind without a ramp
//               has confidence 1 where found and severity 0.
//   hypothesis  its thresholds are starting values no measurement has confirmed yet.
//   handledBy   the device that deals with it, or Person — nothing in the chain cures it (a new mix or export does) —
//               or Nothing: it describes the file. `handled`: that device is in the chain as the project stands.
//   value, second, third   the evidence, by kind (below), in the units ObservationText prints them with
//   places, at1…at3        up to three places in the programme, seconds from its start
// THE KINDS, in the order of the analysis — the file, then the spectrum, then the hum:
//   clipping        confirmed clips: value their count, second a minute, third the clipped share of the programme
//                   (the fuller channel); places — the first distinct ones. Regular clipping (the limiter's
//                   clippedPerMinute) is what stops the machine's peak clipper; rarer clips are named by place.
//   dcOffset        value the largest channel offset, of full scale
//   bitsUnused      value the low bits every channel leaves unused, of the file's depth
//   dualMono        a stereo file whose channels are equal
//   edgeSilence     value the silence at the start, second at the end, seconds
//   tooQuiet        value the input's loudness, LUFS; second 1 where the machine sets only the gain and the ceiling
//   tooShort        value the programme's length, second the length under which the lowest note is not sought, seconds
//   alreadyLimited  value the input's PLR, second the bound, dB; third 1 where it is said because the source is clipped
//   spectralWall    value the cutoff, Hz; second the drop, dB; third how far below Nyquist it stands, as a share
//   loudestLowNote  value its MIDI number, second its frequency, Hz
//   lowestLowBand   value the lowest occupied band's MIDI number, second its centre, Hz
//   infraLow        value the infra-low share of the energy, second the crossover it is weighed at, Hz
//   wideBass        value the side's share of the energy below the crossover, second the crossover, Hz
//   polarity        value the stereo correlation, second the raw side's share
//   sibilance       value the bursts' excess over their baseline at its quantile, dB; second the bursts a minute;
//                   third their band's share against the instrument's own ceiling, dB; places — the loudest
//   hum             value the fundamental, Hz; second its prominence, dB; third its power against the programme's, dB
//   humWandered     a line that does not hold its frequency: value, second as hum's
enum class ObservationKind : std::uint8_t
{
    Clipping, DcOffset, BitsUnused, DualMono, EdgeSilence, TooQuiet, TooShort, AlreadyLimited,
    SpectralWall, LoudestLowNote, LowestLowBand, InfraLow, WideBass, Polarity, Sibilance,
    Hum, HumWandered
};
inline constexpr std::size_t kObservationKinds = 17;
enum class ObservationStatus : std::uint8_t { NotMeasured, NotFound, Found };
enum class ObservationStyle : std::uint8_t { Error, Warning, Note, Reading };
enum class HandledBy : std::uint8_t { Nothing, Hpf, MonoBass, Person };
struct Observation
{
    ObservationStatus status = ObservationStatus::NotMeasured;
    MeasurementReason reason = MeasurementReason::Pending;
    ObservationStyle style = ObservationStyle::Note;
    double confidence = 0.0, severity = 0.0;
    bool doubtful = false, hypothesis = false;
    HandledBy handledBy = HandledBy::Nothing;
    bool handled = false;
    double value = 0.0, second = 0.0, third = 0.0;
    std::uint32_t places = 0;
    double at1 = 0.0, at2 = 0.0, at3 = 0.0;
};
// Every kind's observation, in the order of ObservationKind.
struct Observations
{
    Observation clipping, dcOffset, bitsUnused, dualMono, edgeSilence, tooQuiet, tooShort, alreadyLimited;
    Observation spectralWall, loudestLowNote, lowestLowBand, infraLow, wideBass, polarity, sibilance;
    Observation hum, humWandered;
};
// The source report opens with the clipping detector's verdict. A true peak over zero with samples below full scale
// is an inter-sample over, not evidence of a flattened waveform. Unknown remains unknown when a reading is absent.
enum class SourceClipStatus : std::uint8_t { NotMeasured, Clean, Clipped, InterSampleOvers, SampleOvers };
struct SourceReport
{
    SourceClipStatus clipping = SourceClipStatus::NotMeasured;
    MeasurementReason clippingReason = MeasurementReason::Pending;
    std::optional<bool> alreadyMastered;
    std::optional<double> loudnessLufs, truePeakDbTp, plrDb;
    // The mode a default version-0 master would take for the selected target. Mastered while recognition is absent,
    // the target is a specification, or the target asks for more loudness.
    DeliveryMode deliveryMode = DeliveryMode::Mastered;
    std::optional<double> deliveryCeilingDbTp, deliveryGainDb;
    BoundedList<text::Fact, 2> facts {};
    BoundedList<text::Fact, 2> advice {};      // later lines, past the frozen v13 room of `facts`
};
// ONE OBSERVATION'S LINE: the fact ObservationText states, and the kind it is said of.
struct ObservationFact
{
    ObservationKind kind = ObservationKind::Clipping;
    text::Fact fact {};
};
// An observation as a fact — its sentence with its numbers and units, for one that was found; for one not measured,
// its name and why (ObservationUnmeasured); nothing for one measured and not found.
// facts(): every kind's line, in the order of ObservationKind, each as fact() states it — the lines a snapshot carries.
struct ObservationText
{
    [[nodiscard]] static const Observation& of (const Observations& all, ObservationKind kind) noexcept;
    [[nodiscard]] static std::optional<text::Fact> fact (ObservationKind kind, const Observation& observation) noexcept;
    [[nodiscard]] static BoundedList<ObservationFact, kObservationKinds> facts (const Observations& all) noexcept;
};

// THE FINDINGS AS FACTS — the report's lines, typed: what the high-pass stood on, and, where mono bass has something to
// say (the bass partly or wholly in opposite polarity, or not weighed), that.
// A sentence states what sounds: the planner's reasons only where its proposal is what sounds (Sounding::Proposal); a
// person's value, or a file's, is named as that, without the machine's claims.
struct PlanText
{
    [[nodiscard]] static text::Fact hpf (const HpfFinding& finding) noexcept;
    // THE ADVICE BESIDE A KNOB — a person's value as it sounds against the norm engine.toml draws on the knob; nothing
    // for the machine's own value (Sounding::Proposal, or File: its proposal, inside its own rule — owner, 01.10), for a
    // device out of the chain or for a value inside the norm. The plan says each piece of advice only where a person set
    // the value it judges (the cutoff, the slope, the crossover, a shelf). The high-pass's cutoff below [hpf]
    // comfort.lowHz or above comfort.highHz (strictly), with the window; its slope gentler than the gentlest of
    // slopesNormal or steeper than the steepest (a slope between two normal ones is inside the norm). Mono bass's
    // crossover outside every zone of [monoBass.zones], both ends inside. The EQ curve beyond [eq] curve.warnDb.
    [[nodiscard]] static std::optional<text::Fact> hpfCutoffAdvice (const HpfFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> hpfSlopeAdvice (const HpfFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> monoBassAdvice (const MonoBassFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> eqAdvice (const EqFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> monoBass (const MonoBassFinding& finding) noexcept;
    // Beside it: the polarity warning where the fold sounds at a person's or a file's crossover against an opposite-
    // polarity verdict, and the part of the piece the loss was weighed over where the reading does not hold it whole.
    [[nodiscard]] static std::optional<text::Fact> monoBassPolarity (const MonoBassFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> monoBassCoverage (const MonoBassFinding& finding) noexcept;
    // The glue's refusal and its clamps, each with its reason: unavailable without a P95; the release on the fallback
    // tempo; the release held at a limit.
    [[nodiscard]] static std::optional<text::Fact> glue (const GlueFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> glueTempo (const GlueFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> glueRelease (const GlueFinding& finding) noexcept;
    // The limiter's line — the ceiling and what the peak clipper does, as it sounds; beside it, where a person's
    // threshold cuts against the machine, the machine's reason ("the machine would not cut: …"); and on a target cut to
    // vinyl the medium's warnings (a ceiling above its own, needles cut) and the constant note about the top.
    [[nodiscard]] static text::Fact limiter (const LimiterFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> needlesAgainstMachine (const LimiterFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> vinylCeiling (const LimiterFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> vinylNeedles (const LimiterFinding& finding) noexcept;
    [[nodiscard]] static std::optional<text::Fact> vinylTop (const LimiterFinding& finding) noexcept;
    // The dither's line: in the chain at the delivery's depth, switched off by a person, not applicable at this depth —
    // and, there, a person's tick kept without effect.
    [[nodiscard]] static text::Fact dither (const DitherFinding& finding) noexcept;
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

namespace detail { struct Driver; struct Inspector; struct PlanInputs; struct MasterPlan; struct MeasurementWorkspace; struct LiveMeasurements; struct SourceMeasurements; struct NeedlesWork; struct NeedlesResult; struct WaveformState; struct QueryCache; struct MasterJob; struct MasterRows; struct DamageJob; }

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
    [[nodiscard]] Checked exportWorkedBytes (MasterId master) const noexcept;
    [[nodiscard]] WorkedText exportWorked (MasterId master) const noexcept;
    [[nodiscard]] Rejection exportWorked (MasterId master, std::span<char> output) const noexcept;

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
    // A page transfer with every source measurement row and no master's heavy rows or traces. The ordinary full
    // snapshot and the frequent rowless-source summary keep their existing shapes.
    [[nodiscard]] std::uint64_t sourceSnapshotBytes() const noexcept;
    [[nodiscard]] Snapshot sourceSnapshot() const noexcept;

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
    // The damage being graded for a delivered master (MasterReport.damage Pending), a job of its own; 0 when none is.
    [[nodiscard]] JobId damageJob() const noexcept;
    // Every grade not yet ended — the one graded first (Running) and those waiting their turn — in the order they run.
    [[nodiscard]] std::span<const DamageJobEntry> damageJobs() const noexcept;
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
    // Against the heap as it is, less what the command frees first (Checked::releasedBytes).
    [[nodiscard]] Checked demand (const Checked& storage) const noexcept;
    [[nodiscard]] Answer reject (Answer answer) noexcept;

    // Are the devices placed — the machine's layer written for this source and target, and every measurement the
    // target's devices read ended (PlanStatus::Ready)? The table's placed columns; the Unplaced ones are the same states
    // while a measurement the devices read still runs: edits are taken there, an import and adoptMachine are not.
    [[nodiscard]] bool placed() const noexcept;

    void emit (Notification event) noexcept;
    void emit (Notification event, const Phase& progress) noexcept;
    [[nodiscard]] Phase jobProgress (JobId job) const noexcept;
    void dropJob (JobId job) noexcept;
    void needlesAfterDroppedMaster() noexcept;
    void startMaster (const detail::MasterPlan& plan) noexcept;
    void clearMasters() noexcept;
    void settleMasterCrest (MeasurementReason reason) noexcept;
    void stepMasterCrestJoin() noexcept;
    // THE DAMAGE'S JOBS (src/Damage.h): asked by the shell (command::GradeDamage) for a master kept — a job id each,
    // announced Pending — and graded one at a time, in the order asked, behind every other work: startDamage takes the
    // first waiting grade when nothing runs — its walks' room checked against the capacity then (refused:
    // MeasurementReason::Memory, said) — in the unit of its first step; a new master parks the running one (its walks
    // freed before the master allocates; it starts again, first, when its turn comes back). A grade ends with its walks
    // (`stopped` None: their result) or is stopped — by cancel of its job (Cancelled), by forget of its master
    // (MasterForgotten, no line: the master is gone), by the room it lacked (Memory) — and says so: its master's report
    // settles, the damage's line (`line`) and the Damage event, its last word. A new source ends them all (endAllDamage),
    // each with its last word, MasterForgotten, in the load's own batch, stamped with the source they belonged to.
    [[nodiscard]] bool startDamage() noexcept;
    void stepDamage() noexcept;
    void parkDamage() noexcept;
    void endDamage (std::size_t index, MeasurementReason stopped, bool line) noexcept;
    void endAllDamage() noexcept;
    [[nodiscard]] std::size_t damageIndex (JobId job) const noexcept;
    [[nodiscard]] Phase damageWaitingProgress() const noexcept;
    [[nodiscard]] SnapshotView buildView() const noexcept;
    [[nodiscard]] SnapshotView buildSummary (std::span<MeasurementResult> results) const noexcept;
    [[nodiscard]] SnapshotView buildSourceSnapshot() const noexcept;
    void stripMasterRows (SnapshotView& view) const noexcept;
    [[nodiscard]] bool hasWork() const noexcept;
    void requestNeedles() noexcept;
    void stepNeedles() noexcept;
    void stepMeasurements() noexcept;
    void stepWaveform() noexcept;
    void stepSourceMeasurements() noexcept;
    void invalidateQueryCache (Analyzer analyzer) noexcept;
    [[nodiscard]] bool mandatoryReady() const noexcept;
    [[nodiscard]] TempoChoice tempoForDevice() const noexcept;
    // What the measurements found in the file (src/Observations.h), refreshed with the plan.
    Observations observations_ {};
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
    void preferTempo (std::uint32_t waiting) noexcept;
    void placeAgain (Analyzer ended) noexcept;
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
    // THE STEP'S EVENTS — a block of their own, allocated with the session (createBytes declares both): the batch is
    // most of what a session holds (62 events of every payload kind), and the session object stays small. Each block a
    // create asks for fits AddressSanitizer's largest primary size class with its redzone (128 KiB less 2 KiB): past
    // it every create is an mmap, and the ABI suite's walk through a slot's 16.7 million generations takes hours under
    // the sanitizers instead of minutes (felitronics_session_abi_tests holds both blocks to it).
    std::unique_ptr<std::array<Notification, kEventBatch>> events_;
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
    // (the target, its numbers, a person's layer), and the needles are measured at that project's ceiling until the
    // wait ends or the master is dropped — cancelled, stopped with its measurement, ended by a contract fault, or
    // ended by a load or loadMeasured, which clear it with every other master state. When the wait ends the master's
    // chain is taken from that project's devices (src/Chain.h) and its job starts.
    bool jobWaiting_ = false;
    bool jobMasterAnyway_ = false;
    std::optional<double> jobBudgetResolutionDb_;
    bool jobMachineFromFile_ = false;          // the waiting master's recipe kept a file's machine layer: never placed again
    // Derived at placement and after accepted commands; owned snapshots copy these points.
    void refreshEqCurve() noexcept;
    EqPoint eqCurve_[kEqCurvePoints] {};
    EqPoint eqOnlyCurve_[kEqCurvePoints] {};
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
    // Scratch for any master-rowless transfer: as many as masters_ has, written afresh by each view, never state.
    std::unique_ptr<Kept[]> leanMasters_;
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
    // The damage being graded: its job, the bytes it holds (inside the master's admitted demand) and its progress.
    std::unique_ptr<detail::DamageJob> damageJob_;
    JobId damageJobId_ = 0;
    std::uint64_t damageJobBytes_ = 0;
    Phase damageProgress_ {};
    // THE GRADES' QUEUE: an entry per grade asked and not yet ended, in the order they run (the running one first); what
    // a grade's walks need to start is its master's (MasterRows), so an entry holds no walk buffer. Room for
    // kMaxDamageGrades, in the session object itself: a new source's last words for all of them fit one event batch.
    std::array<DamageJobEntry, kMaxDamageGrades> damageJobs_ {};
    std::size_t damageCount_ = 0;
    // The batch's leading events of the world a command replaced (a new source's farewells, endAllDamage): they keep the
    // revision they were said under where the command stamps its own on the rest.
    std::size_t oldWorldEvents_ = 0;

};

} // namespace felitronics::session
