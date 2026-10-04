// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

//==============================================================================
// felitronics::session::text — FACTS, NOT TEXT. Nothing in the session prints. It states a FACT: an id and typed
// arguments — a number with its unit, precision, sign and bound; a count; a term (a word the catalog names); a note as a
// MIDI number; a text of the user's (a file name, a target's label), which is never translated. No fact carries a ready
// string. Text::text() renders a fact in a language, and that is the only place words are made: the same fact reads the
// same on the web and on the desktop, re-renders in another language without being computed again, and compares as
// data (docs/SESSION.md, "The text").
//
// TWO DOCUMENTS, COMPILED IN. modules/session/text/catalog.toml holds WHOLE messages — a message is never assembled from
// fragments — each with its placeholders named ({passes}), and with plural or select variants where the words depend on
// a number or a term; modules/session/text/format.toml is the ONE table of how every language writes a number: decimal
// sign, grouping, the minus, the bounds, the sign of an absent value, the units, the names of the notes. Both are
// compiled into the library as constexpr data (felitronics-toml's felitronics_toml_embed); nothing reads a file.
//
// THE BUILD HOLDS THE CATALOG. Every build of the library runs felitronics_session_text_check over both documents before
// the library is built: every language the catalog declares has every message and every term; every message's
// placeholders name its fact's arguments, and every language uses the same set of them (repeated or reordered freely);
// a plural message has exactly its language's CLDR categories, a select message exactly its term group's values; the
// formatting table has a row, and every unit, for every language of Lang. A missing key or a stray placeholder is a red
// build at `<file>:<line>:<column>`. So a declared language never lacks a message, and there is no fallback to English
// anywhere: a message the catalog does not have in a language — a language the catalog does not declare — renders as
// its id.
//
// NUMBERS ARE FORMATTED BY RULES OF ITS OWN, stated here and tested: no locale, no printf, no JavaScript semantics.
//   * Rounding: the double is read as its SHORTEST ROUND-TRIP DECIMAL — the fewest significant digits that read back as
//     the same double (std::to_chars's shortest form: specified exactly by the standard, the same on every standard
//     library, no locale) — and that decimal is rounded to the grid of `precision` fraction digits, to the nearest
//     multiple of 10^−precision, a decimal exactly halfway rounded away from zero. So the number a person wrote rounds
//     as they would round it: 1.005 → 1.01 and 2.675 → 2.68 (their doubles are 1.00499999999999989… and
//     2.67499999999999982…), −14.05 → −14.1, 0.125 → 0.13, 2.5 → 3; 1e300 prints a one and three hundred zeros. The
//     rounding is on the decimal's digits, in characters and integers: no floating-point arithmetic takes part.
//   * The sign is the printed number's: a negative value takes the minus, a positive one the plus under Sign::Always —
//     and a value whose digits print as zero takes neither, under every Sign: −0.04 at one digit is "0.0", never
//     "−0.0", which a musician reads as a bug; either zero is "0". The sign is read from the double's bits, and the digits
//     come from std::to_chars and integer steps: no floating-point operation decides a rendering, so the calling
//     thread's floating-point environment cannot change one.
//   * Plural categories are CLDR's, selected on the number AS PRINTED: "1.0" is not "one" in English, "1" is. The
//     formatter and the selector are one component.
//   * A value that is not finite prints the language's absent sign ("—") alone: no sign, no bound, no unit.
//   * Parsing (Text::parse) reads what a person typed with std::from_chars, never strtod: no locale reaches it. Its one
//     division asks for IEEE-754's default environment first, as every call of the session that computes does.
//
// As Session.h: this header carries no function body. The rendering is compiled with the library's own flags
// (docs/SESSION.md).
namespace felitronics::session::text
{

// The languages a shell may ask for: the twelve the site speaks. Which of them the catalog DECLARES is data
// (Text::speaks); the formatting table covers all twelve.
enum class Lang : std::uint8_t { En, De, Ru, Uk, Cs, Es, Fr, It, Pl, Pt, Ro, Tr };
inline constexpr std::size_t kLangCount = 12;

// A number's unit. Percent takes the number of percent (45 for 45 %), not a share of one. None is the number alone.
enum class Unit : std::uint8_t { None, Percent, Db, DbTp, DbFs, Lufs, Lu, Hz, KHz, Ms, S, Bpm };
inline constexpr std::size_t kUnitCount = 12;

// Which sign a number shows. Negative: the minus of a negative value. Always: the plus of a positive value too (a gain).
// Under both, a number that prints as zero shows none.
enum class Sign : std::uint8_t { Negative, Always };

// Whether a number is a bound: Exact prints the number, AtLeast "≥ 6 %", AtMost "≤ 6 %".
enum class Bound : std::uint8_t { Exact, AtLeast, AtMost };

// CLDR's plural categories.
enum class Plural : std::uint8_t { Zero, One, Two, Few, Many, Other };

// THE FACTS — one id per message of modules/session/text/catalog.toml, whose key Text::key() gives. The numbers are
// stable: a fact is stored and compared by its id, so an id is never reused for another message, and each kind of fact
// has a range of its own:
//     1 –  99   readings and the landing
//   100 – 199   a command's rejection: 100 + its Rejection code (Commands.h), one fact per code; from 180, a field's
//               rejection said with its numbers (the value refused, and the domain it left)
//   200 – 299   the phases of the work
//   300 – 399   the session's errors
//   400 – 499   the measurements and the observations
//   500 – 599   the plan's advice and the targets' notes
//   600 – 699   the landing and the master's report, continued (1 – 99 is full): a landing held short (600 – 602), the
//               damage the processing did and the loudness range (603 – 607)
// The arguments each fact takes, by name and kind, are src/TextFacts.h's, and the build holds the catalog to them.
enum class FactId : std::uint16_t
{
    Value = 1,               // a reading alone: {value}
    LandingPass = 2,         // the landing's progress: {pass} of {passes}
    LandingConverged = 3,    // the landing converged in {passes} passes (plural on passes)
    PairsConsistent = 4,     // the blind test's repeats: {agreed} of {pairs} pairs (plural on pairs)
    LoudestLowNote = 5,      // the loudest note of the low end: {note}
    WideBass = 6,            // the bass is wide (side {side}) — the warning of phase 1
    RateAboveLimit = 7,      // the file's rate {rate} is above what this platform takes, {limit} (select on platform)
    MachineDifferences = 8,  // {count}: where today's planner differs from the file's machine layer, which is kept
    MasterLandingMiss = 11,
    MasterHintSubBass = 12,
    MasterHintPeaks = 13,
    MasterHintDark = 14,
    MasterHintDemand = 15,
    MasterHintGainRange = 16,
    MasterHintTruePeak = 17,
    MasterCrestSourceRate = 18,
    MasterCrestPending = 19,
    MasterCrestUnavailable = 20,
    MasterReportUnavailable = 21,
    MasterCrestDelivered = 22,
    MasterLandingAbove = 23,
    MasterCostShape = 24,
    MasterCostCrest = 25,
    MasterCostPumping = 26,
    MasterCostK2Deferred = 27,
    MasterCostUnavailable = 28,
    PlanWaiting = 29,        // a master waits: {device} reads {analyzer}, measured to {progress}
    // What the high-pass and mono bass found (PlanView::hpf, ::monoBass; PlanText).
    HpfNote = 30,            // {cutoff} from the lowest note {note} at {hz}, taking {loss} of it
    HpfBelowFloor = 31,      // {cutoff}, the floor: the lowest note {note} at {hz} is below it, cut by {loss}
    HpfTop = 32,             // {cutoff}, the top: the lowest note {note} at {hz} is higher
    HpfUnsure = 33,          // {cutoff}, the floor: no sure lowest note
    HpfShort = 34,           // {cutoff}, the floor: a programme shorter than {seconds} is not searched
    HpfQuiet = 35,           // {cutoff}, the floor: the input is too quiet to search
    HpfUnmeasured = 36,      // {cutoff}, the floor: the low end was not measured
    MonoBassPartial = 37,    // bass partly in opposite polarity: mono bass takes {loss} of the low end
    MonoBassAntiPhase = 38,  // bass in opposite polarity: mono bass would take {loss}; check a channel's polarity
    MonoBassUnmeasured = 39, // mono bass left out: the loss could not be weighed
    HpfFloor = 40,           // {cutoff}, the floor: it takes {loss} of the lowest note {note} at {hz}, more than the note allows
    // What the glue comes to (PlanView::glue; PlanText) and what the glue and the saturation did (MasterCost; MasterReportText).
    GlueUnavailable = 41,    // the glue is out — no short-term P95 to stand its threshold on; the knob's {upTo} is kept
    GlueTempoFallback = 42,  // the glue's release is set for {bpm}: the tempo was not measured with confidence
    GlueReleaseHeld = 43,    // the glue's release is held at {release}: the tempo asked for {asked}
    MasterGlue = 44,         // the glue took {usual} on the loud places, {largest} at most
    MasterSaturation = 45,   // the saturation cut peaks by up to {largest}, usually {usual}
    // A device sounding otherwise than the planner proposes: named as what it is, without the planner's reasons.
    HpfByHand = 46,          // the high-pass at {cutoff}: a person's value
    HpfKept = 47,            // the high-pass at {cutoff}: a project file's machine layer
    HpfOff = 48,             // the high-pass is out of the chain
    MonoBassByHand = 49,     // mono bass below {crossover}: a person's value
    MonoBassKept = 50,       // mono bass below {crossover}: a project file's machine layer
    MonoBassPartWeighed = 51, // mono bass was weighed over the first {covered} of the piece's {whole}
    // What the limiter comes to (PlanView::limiter; PlanText): its ceiling and what the peak clipper does, as it sounds.
    LimiterShort = 52,       // ceiling {ceiling}; short needles ({p90}, {bass}, {plr}): the clipper cuts at most {cut}
    LimiterBetween = 53,     // ...needles neither short nor ruled out: cut with care, at most {cut}
    LimiterManual = 54,      // ...cut by hand, at most {cut} — a cap ({cut} is Bound::AtMost): the landing decides how much
    LimiterNeedlesOff = 55,  // ...the peak clipper is switched off
    LimiterLittleNeed = 56,  // ...not cut: the need at the aim, {need}, is no more than {little}
    LimiterNoExcursions = 57, // ...not cut: no peak stands above the ceiling
    LimiterUnmeasured = 58,  // ...not cut: the needles were not measured
    LimiterClipped = 59,     // ...not cut: the source is clipped, {rate} clips a minute
    LimiterLowPlr = 60,      // ...not cut: the input's PLR {plr} is under {bound}
    LimiterBass = 61,        // ...not cut: the excursions carry much bass, {bass} of the dose
    LimiterLong = 62,        // ...not cut: the excursions are long, 90 % within {p90}
    LimiterNoClipper = 63,   // ...not cut: the target has no peak clipper
    LimiterQuiet = 64,       // ...not cut: the input is too quiet to measure
    LimiterPending = 65,     // ...the needles are still being measured
    LimiterPlain = 66,       // the ceiling alone
    NeedlesAgainstMachine = 67, // beside a manual threshold: the machine would not cut — {why} (select)
    VinylCeiling = 68,       // for vinyl the ceiling usually stands no higher than {medium}; it is {ceiling}
    VinylNeedles = 69,       // for vinyl the needles are usually not cut
    VinylTop = 70,           // on vinyl the top above {hz} is usually rolled off in the cutting room
    // What the dither comes to (PlanView::dither; PlanText).
    DitherOn = 71,           // on: a {bits}-bit delivery
    DitherOffByHand = 72,    // switched off by a person: the {bits}-bit delivery is rounded without noise
    DitherOff = 73,          // off: the {bits}-bit delivery is rounded without noise
    DitherNotApplied = 74,   // does not apply: a {bits}-bit delivery, dither only up to {upTo} bits
    DitherKept = 75,         // ...and a person's tick is kept without effect
    // A master's medium and input (MasterReportText), published with the master.
    MasterVinylReady = 76,   // ready for cutting: no cardinal corrections; RIAA and the level are the cutter's
    MasterVinylDeparts = 77, // readiness is not confirmed: the settings depart from the rules of vinyl
    MasterVinylChecked = 78, // checked on the file: mono below {crossover}, infra-low cut from {cutoff}, {peak} ≤ {ceiling}
    MasterVinylUncheckable = 79, // what a file cannot tell: the side's length, sibilance at the cutter, the centre
    MasterQuietInput = 80,   // a very quiet input ({lufs}) raised by {gain}: no device but the high-pass and the dither
    TargetChangeResetsEdits = 81, // before a change of target: {count} device edits by hand will be reset
    // Each rule of vinyl a master departs from (MasterReportText::vinylDepartures), the chain's number beside the rule's.
    MasterVinylNoFold = 82,  // mono bass off: for vinyl the bass below {medium} is folded
    MasterVinylFoldDeparts = 83, // mono below {crossover} at width {width}: for vinyl below {medium}, width ≤ {mediumWidth}
    MasterVinylNoHighPass = 84, // high-pass off: for vinyl the infra-low is cut from {medium} at ≥ {mediumSlope} dB/oct
    MasterVinylHighPassDeparts = 85, // high-pass from {cutoff} at {slope} dB/oct: for vinyl from {medium} at ≥ {mediumSlope}
    MasterVinylCeilingDeparts = 86, // the ceiling {ceiling} above the medium's {medium}
    MasterVinylNeedlesDeparts = 87, // the clipper is set to cut at most {cut}: vinyl is cut without the clipper
    // The landing's verdict (MasterReportText::landing), one per status, published with the master. A miss names its
    // numbers in MasterLandingMiss/Above above; its status line says why, against the tolerance.
    MasterLandingSolved = 88,      // the master reached {achieved} against {target} (tolerance ±{tolerance})
    MasterLandingUnreachable = 89, // no closer than ±{tolerance} to the target: {limit} holds it (select; the solver's binding)
    MasterLandingPassLimit = 90,   // the passes ran out before the master came within {tolerance} of the target
    MasterLandingBetween = 91,     // the target falls between the nearest levels {below} and {above}, both beyond the tolerance
    MasterLandingFailed = 92,      // the master stopped on a technical failure
    // The master's cost line by line (MasterReportText), each published where its numbers were measured.
    MasterCostSection = 93,        // the largest section shift: {shift} at {from}–{to}
    MasterCostSections = 94,       // sections compared: {count}
    MasterCostLimiter = 95,        // the limiter's reduction over the active windows: median {median}, P95 {p95}
    MasterCostActive = 96,         // the limiter works in {limiter} of the windows; {active} of the windows are active
    MasterCostBands = 97,          // the impact loss by band: low {low}, low-mid {lowMid}, high-mid {highMid}, high {high}
    // No render under the ceiling: the master delivered is the gentlest, its true peak {truePeak} above the {ceiling}.
    MasterPeaksAboveCeiling = 98,
    // The glue's release is set for {bpm}: the tempo was measured, {measured}, with too little confidence to follow.
    GlueTempoUnsure = 99,

    // A command's rejection, by its code — what was refused and why. The three a field refuses name it: {field}.
    RejectedFloatingPointEnvironment = 101,
    RejectedNoSource = 102,
    RejectedNotPlaced = 103,
    RejectedNotMeasured = 104,
    RejectedBusy = 105,
    RejectedNoJob = 106,
    RejectedNoMaster = 107,
    RejectedUnknownTarget = 108,
    RejectedNotOffered = 109,
    RejectedUnknownJob = 110,
    RejectedUnknownMaster = 111,
    RejectedNotFinite = 112,     // {field}
    RejectedNotOneOf = 113,      // {field}
    RejectedOutOfDomain = 114,   // {field}
    RejectedBadChannels = 115,
    RejectedBadRate = 116,
    RejectedNoAudio = 117,
    RejectedTooLong = 118,
    RejectedNoJobId = 119,
    RejectedInvalidUtf8 = 120,
    RejectedProjectTooLarge = 121,
    RejectedProjectSyntax = 122,
    RejectedProjectMissing = 123,
    RejectedProjectType = 124,
    RejectedProjectUnknownKey = 125,
    RejectedUnknownDefaults = 126,
    RejectedNewerDefaults = 128,
    RejectedRateAboveLimit = 129,
    RejectedMemory = 130,

    RejectedContract = 131,
    RejectedOutputPending = 132,
    RejectedMandatoryUnavailable = 133,
    RejectedDeliveryFormat = 134,  // {bits} {rate}: the target's bit depth and delivery rate
    RejectedPlanPending = 135,
    // A field's rejection with its numbers, where the answer carries them (Answer::value, low, high): the refused value
    // against the domain the check read, and a value a knob does not take.
    RejectedOutOfDomainValue = 180, // {field} {value} {low} {high}
    RejectedNotOneOfValue = 181,    // {field} {value}

    Measurement1 = 200,
    Measurement2 = 201,
    MasterPass = 202,          // {pass}, without a pass budget
    MasterReady = 203,
    Cancelled = 204,
    Convert = 205,
    Lra = 206,
    Final = 207,

    SessionTrap = 300,
    SessionContract = 301,
    SessionRefusal = 302,
    SessionMemory = 303,
    SessionPoisoned = 304,
    SessionStale = 305,
    MeasurementPending = 400,
    MeasurementReady = 401,
    MeasurementStopped = 402,
    MeasurementUnsupported = 403,
    MeasurementTooShort = 404,
    MeasurementNonFinite = 405,
    MeasurementCapacity = 406,
    MeasurementNoSignal = 407,
    MeasurementUnavailable = 408,
    NeedlesSkipped = 409,
    NeedlesRunsTruncated = 410,
    SourceClipping = 411,
    ContinueMeasurement = 412,
    AnalyzerStatus = 413,
    SourceQuiet = 414,
    SourceGainOnly = 415,
    SourceDc = 416,
    SourceUnusedBits = 417,
    SourcePolarity = 418,
    // The observations (Observations, Session.h; ObservationText): what the measurements found in the file.
    SourceClips = 419,         // {count} clips in the source
    SourceClipsAt1 = 420,      // ...at {first}: it looks like an edit
    SourceClipsAt2 = 421,      // ...at {first} and {second}
    SourceClipsAt3 = 422,      // ...the first at {first}, {second} and {third}
    SourceClipped = 423,       // the source is clipped: {count} clips, {rate} a minute
    SourceDualMono = 424,      // the channels of the stereo file are equal
    SourceEdgeSilence = 425,   // silence at the edges: {leading} at the start, {trailing} at the end
    SourceShort = 426,         // a short recording ({seconds}): under {limit} the lowest note is not sought
    SourceLimited = 427,       // already limited: PLR {plr}, under {bound}
    SourceLimitedClipped = 428, // already limited: the source is clipped (PLR {plr})
    SourceWall = 429,          // the spectrum ends at {cutoff}, a drop of {drop}
    SourceLowestBand = 430,    // the lowest occupied band: {note} ({hz})
    SourceInfraLow = 431,      // {share} of the energy lies below {hz}
    SourceSibilance = 432,     // sibilance: bursts {excess} above their baseline, {rate} a minute
    SourceSibilanceAt = 433,   // ...the loudest at {first}, {second} and {third}
    SourceHum = 434,           // hum: a line at {hz}, {prominence} above the background
    SourceHumWandered = 435,   // possible hum: a line near {hz} that does not hold its frequency
    SourceLowestBandUnsure = 436, // the lowest occupied band: {note} ({hz}), unsure — under the margin a sure note stands
    ObservationUnmeasured = 437, // {name}: not measured — {reason}
    TempoConfidence = 438,     // a reading: how sure the tempo is — {confidence}
    // The observations' own words where the owner gave them (01.10): a style by size says its own line.
    SourceDcNote = 439,        // a DC offset ({offset}) as a note: the master's high-pass removes it, check the mix chain
    SourceTruncatedBits = 440, // the effective depth {bits}: truncated somewhere in the mix chain
    SourceShallowMix = 441,    // a {bits}-bit mix: mix down at {depth} bits
    SourceLimitedBus = 442,    // the mix is limited already (PLR {plr}): send a version without the bus limiter
    SourceLossy = 443,         // the source went through mp3/AAC (the top cut at {hz}): master from a lossless source
    SourceInfraLowNote = 444,  // a little energy below {hz}: the master's high-pass removes it
    SourceInfraLowWarning = 445, // a notable part of the energy ({share}) below {hz}: the high-pass cuts it, check what it is
    // A DC offset of a stereo source, each channel's as the readings print it (a mono source keeps SourceDcNote/SourceDc).
    SourceDcNoteStereo = 446,  // L {left}, R {right} as a note: the master's high-pass removes it, check the mix chain
    SourceDcStereo = 447,      // L {left}, R {right} as a warning or an error
    // The plan's advice (PlanText): a device's value as it sounds, against the norm the config draws on its knob.
    HpfBelowComfort = 500,     // the high-pass at {cutoff} is below the comfort window {low}–{high}
    HpfAboveComfort = 501,     // the high-pass at {cutoff} is above the comfort window {low}–{high}
    HpfSlopeGentle = 502,      // a slope of {slope} dB/oct is gentler than the usual, {gentlest} dB/oct and up
    HpfSlopeSteep = 503,       // a slope of {slope} dB/oct is steeper than the usual, {steepest} dB/oct at most
    EqOvershoot = 504,         // the EQ curve is beyond the norm: {db} at {hz}
    MonoBassOutsideZones = 505, // mono bass at {crossover}, above what a club ({clubTo}) and vinyl ({vinylTo}) need
    // The target's note (SnapshotText::targetNote): where its loudness comes from, where that is not a platform's
    // published number or a standard.
    TargetMeasured = 506,      // {lufs} is measured on the platform, not a published specification
    TargetPractice = 507,      // {lufs} is mastering practice, not a standard
    TargetNoNormalisation = 508, // the destination does not normalise loudness: a delivery preset
    // Mono bass's crossover below every zone (MonoBassOutsideZones is the one above them).
    MonoBassBelowZones = 509,  // mono bass at {crossover}, below what a club ({clubFrom}) and vinyl ({vinylFrom}) ask for
    // A device's card while a field of its machine layer waits for a measurement (DevicePlan::pending): not measured yet.
    DeviceUnmeasured = 510,    // not measured yet: the machine sets it when {analyzer} ends
    // A landing held short (MasterReportText::landing, ::gate), published with the master.
    MasterLandingBudget = 600, // target {target}, landed {achieved}: further, the limiter would take more than {budget} (P95)
    MasterLandingGate = 601,   // by the loud part {loud}, by the file's BS.1770 reading {file}
    MasterLandingOverBudget = 602, // target {target}, landed {achieved}: no render kept {budget} (P95); the gentlest takes {taken}

    // The damage the processing did, heard (MasterDamage): the worst window's {grade}, where it starts ({from}) and the
    // {share} of the windows graded heard below imperceptible; nothing heard in any window graded; each with how many
    // windows were graded of all ({graded} of {windows}); not graded, and why ({reason}).
    MasterDamage = 603,
    MasterDamageInaudible = 604,
    MasterDamageUnmeasured = 605,
    // The loudness range: the input's {source}, the master's {master}, the change as a share ({change}) and in LU
    // ({changeLu}); or why it was not measured ({reason}).
    MasterLraChange = 606,
    MasterLraUnmeasured = 607,
};

// THE TERMS — words an argument of kind Term names: one value of a group of the catalog's [terms]. Printed as the
// catalog's word, or chosen on by a select message. Stable numbers too: a new term takes the next one.
enum class Term : std::uint16_t
{
    PlatformWeb = 1,
    PlatformDesktop = 2,
    // The fields a command's check can refuse (Rejection's NotFinite, NotOneOf, OutOfDomain): the target's two
    // numbers, every device's knob and choice — a tick is never refused — and a load's audio, whose samples can be.
    FieldTargetLufs = 3,
    FieldTargetTp = 4,
    FieldHpfFq = 5,
    FieldHpfSlope = 6,
    FieldMonoBassFq = 7,
    FieldMonoBassWidth = 8,
    FieldGlueUpToDb = 9,
    FieldSaturationDrive = 10,
    FieldSaturationMix = 11,
    FieldTiltDb = 13,
    FieldLimiterNeedles = 14,
    FieldLimiterNeedlesDb = 15,
    FieldLowDb = 16,
    FieldAudio = 17,
    AnalyzerLowEnd, AnalyzerLowEnd150, AnalyzerInfraLow, AnalyzerForensics, AnalyzerStereo, AnalyzerCrest, AnalyzerHum, AnalyzerBursts,
    StatusReady, StatusUnsupported, StatusShort, StatusNonFinite, StatusCapacity, StatusNoSignal, StatusMemory,
    AnalyzerWaveform,
    AnalyzerTempo,
    // The needles job, and the devices by the labels a person knows them by (a plan's waited-for measurement names both).
    AnalyzerNeedles,
    DeviceHpf, DeviceMonoBass, DeviceGlue, DeviceSaturation, DeviceTilt, DeviceLimiter, DeviceDither, DeviceLow,
    // Why the machine's peak clipper does not cut (NeedlesWhy, Session.h), for the warning beside a manual threshold.
    NeedlesWhyShell, NeedlesWhyTarget, NeedlesWhyQuiet, NeedlesWhyNoReadings, NeedlesWhyLittleNeed, NeedlesWhyUnmeasured,
    NeedlesWhyNoExcursions, NeedlesWhyClipped, NeedlesWhyLowPlr, NeedlesWhyBass, NeedlesWhyLong,
    // The saturation's type, a person's choice: the field a refusal names, and the five types the page offers
    // (SaturationType, Project.h — Atan, Cubic and Asym are the config's only and have no words).
    FieldSaturationType,
    SaturationTypeTanh, SaturationTypeTube, SaturationTypeTransistor, SaturationTypeTransformer, SaturationTypeTape,
    // The observations by name, in the order of ObservationKind (Session.h); what deals with one, in the order of
    // HandledBy; and why one was not measured — every MeasurementReason but None (Measurements.h), in its order.
    ObservationClipping, ObservationDcOffset, ObservationBitsUnused, ObservationDualMono, ObservationEdgeSilence,
    ObservationTooQuiet, ObservationTooShort, ObservationAlreadyLimited, ObservationSpectralWall, ObservationLoudestLowNote,
    ObservationLowestLowBand, ObservationInfraLow, ObservationWideBass, ObservationPolarity, ObservationSibilance,
    ObservationHum, ObservationHumWandered,
    HandledNothing, HandledHpf, HandledMonoBass, HandledPerson,
    ReasonPending, ReasonCancelled, ReasonUnsupported, ReasonTooShort, ReasonNonFinite, ReasonCapacity, ReasonNoSignal,
    ReasonNotImplemented, ReasonNeedNotAbove3, ReasonMemory,
    // The readings by name, in the order of ReadingKind (Measurements.h); how sure a tempo is, in the order of the tempo
    // detector's ConfidenceLabel (undetermined, low, medium, high).
    ReadingIntegrated, ReadingTruePeak, ReadingLra, ReadingPlr, ReadingDcOffset, ReadingDcOffsetLeft, ReadingDcOffsetRight,
    ReadingLowestBand, ReadingPcmBits, ReadingCorrelation, ReadingBurstsMid, ReadingBurstsSide, ReadingHum, ReadingTempo,
    ReadingTempoConfidence, ReadingClipRuns, ReadingLongestRun, ReadingClippedSamples, ReadingSamplePeak, ReadingLowSide,
    ReadingStereoWindows, ReadingCrestBlocksLow, ReadingCrestBlocksLowMid, ReadingCrestBlocksHighMid, ReadingCrestBlocksHigh,
    ReadingCrestBlocksFull, ReadingTarget, ReadingCeiling, ReadingGain, ReadingPasses, ReadingCheckPasses,
    TempoUndetermined, TempoLow, TempoMedium, TempoHigh,
    // What held a landing short of its target (LandingConstraint, the solver's binding): MasterLandingUnreachable selects.
    LandingLimitNone, LandingLimitTruePeak, LandingLimitLimiter, LandingLimitPlr, LandingLimitLra, LandingLimitGain,
    // The EQ bands: the device, and its five gains as the fields a refusal names, in the order Project.h writes them.
    DeviceBands, FieldBandsBody, FieldBandsMud, FieldBandsForward, FieldBandsBrightness, FieldBandsAir,
    // The BS.1116 impairment grades the damage's line names, 5 down to 1.
    DamageGradeImperceptible, DamageGradePerceptible, DamageGradeSlightlyAnnoying, DamageGradeAnnoying,
    DamageGradeVeryAnnoying,
    // Why a master's damage was not graded: a new master was asked for while it was (MeasurementReason::Superseded); no
    // job id was left for it (NoJobId).
    ReasonSuperseded, ReasonNoJobId,

};

enum class ArgKind : std::uint8_t { None, Value, Count, Term, Midi, UserText };

// One typed argument of a fact. Built by the static members, not by hand.
struct Arg
{
    ArgKind kind = ArgKind::None;
    Unit unit = Unit::None;              // Value
    std::uint8_t precision = 0;          // Value: fraction digits, 0 … 9
    Sign sign = Sign::Negative;          // Value
    Bound bound = Bound::Exact;          // Value
    Term termId {};                      // Term
    double number = 0.0;                 // Value
    std::int64_t integer = 0;            // Count; Midi: the MIDI note number, 0 … 127 (60 is C4)
    std::string_view userText;           // UserText: a VIEW — its bytes are the caller's, kept alive while it is rendered

    [[nodiscard]] static Arg value (double number, Unit unit, std::uint8_t precision, Sign sign = Sign::Negative,
                                    Bound bound = Bound::Exact) noexcept;
    [[nodiscard]] static Arg count (std::int64_t n) noexcept;
    [[nodiscard]] static Arg term (Term t) noexcept;
    [[nodiscard]] static Arg midi (std::int64_t note) noexcept;
    [[nodiscard]] static Arg text (std::string_view userText) noexcept;
};

// A fact: its id and its arguments, in the order src/TextFacts.h declares them for that id. A value: copying one
// allocates nothing. A fact whose arguments do not match its declaration — one missing, one too many, one of another
// kind or with a field the renderer does not know — is INCOMPLETE (Text::complete) and renders nothing: never its
// template with a placeholder showing.
struct Fact
{
    static constexpr std::size_t kMaxArgs = 6;
    FactId id = FactId::Value;
    std::array<Arg, kMaxArgs> args {};
    std::uint8_t argCount = 0;

    [[nodiscard]] static Fact of (FactId id) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b, const Arg& c) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b, const Arg& c, const Arg& d) noexcept;
    [[nodiscard]] static Fact of (FactId id, const Arg& a, const Arg& b, const Arg& c, const Arg& d, const Arg& e) noexcept;
};

//==============================================================================
// THE RENDERER — pure functions over the compiled-in documents: no state, no file, no locale, the same bytes on every
// row the library runs on, native and wasm. Static members, as Session's are (a public header declares no free
// function).
struct Text
{
    // THE FACT IN A LANGUAGE: its whole message, each placeholder replaced by its argument formatted by the table. A
    // message the catalog does not have in `lang` renders as its id (Text::key); nothing falls back to another language.
    // Allocates the result's bytes, and at most textBytes() of them.
    [[nodiscard]] static std::string text (const Fact& fact, Lang lang);

    // THE DEMAND OF text(), before it is made (law 11d): the bytes it will request from the heap, at most — the result's
    // length and its terminator, rounded up to the 16 bytes a standard library allocates a string's storage in, and 64
    // for what MSVC's STL asks beside them (the alignment of a block of 4 KiB or more; iterator debugging's proxy).
    [[nodiscard]] static std::uint64_t textBytes (const Fact& fact, Lang lang) noexcept;

    // The same rendering without the heap. size(): its length in bytes. write(): the bytes into `out`, and how many; a
    // buffer shorter than size() gets nothing and the answer 0 (refused whole). Both allocate nothing. An incomplete
    // fact renders nothing — size() and write() 0, text() empty — and complete() tells it from a rendering.
    [[nodiscard]] static std::size_t size (const Fact& fact, Lang lang) noexcept;
    [[nodiscard]] static std::size_t write (const Fact& fact, Lang lang, std::span<char> out) noexcept;

    // Does the fact carry exactly the arguments src/TextFacts.h declares for its id — their count, each one's kind,
    // and fields the renderer knows (a unit, a precision of 0 … 9, a term of the declared group)? An id this library
    // does not have needs none: it renders as its number.
    [[nodiscard]] static bool complete (const Fact& fact) noexcept;

    // The plural category of a number argument as `lang` prints it — the category a plural message selects on it with.
    // Other for an argument that is not a number (a Value or a Count), and for a value that prints as absent.
    [[nodiscard]] static Plural category (const Arg& number, Lang lang) noexcept;

    // WHAT A PERSON TYPED, read as a number: ASCII spaces around it ignored; an optional sign — "−" (U+2212), "-" or "+";
    // decimal digits, with at most one decimal sign: the language's own, or "." (every keyboard has one) — but not in a
    // language whose grouping separator is ".". A grouping separator is refused rather than guessed ("1.234" is a
    // thousand to a German reader, and nearly one to an English one), so a grouped number the table printed is never
    // read back as another. At least one digit, at most 9 after the sign, and the digits as one integer no larger than
    // 2^53. The answer is that integer over 10^digits, one correctly rounded division: the double nearest the typed
    // decimal, as a correctly rounded strtod would give — without strtod, which follows the process's locale. "−0" is
    // −0.0. Anything else is nullopt, and so is every input on a thread whose floating-point environment is not
    // IEEE-754's default (Session::checkFloatingPointEnvironment), where the division would round otherwise.
    [[nodiscard]] static std::optional<double> parse (std::string_view typed, Lang lang) noexcept;

    // The language's code ("ru"), and the language of a code (nullopt for one that is not one of the twelve).
    [[nodiscard]] static std::string_view code (Lang lang) noexcept;
    [[nodiscard]] static std::optional<Lang> langOf (std::string_view code) noexcept;

    // Does the catalog declare `lang` — does every message exist in it?
    [[nodiscard]] static bool speaks (Lang lang) noexcept;

    // A fact's key in the catalog: the id a message renders as where the catalog does not have it.
    [[nodiscard]] static std::string_view key (FactId id) noexcept;

    // THE FACT OF A REJECTION — what a shell shows for a refused command: the rejection's fact (100 + its code), and,
    // where the rejection is a field's, that field as a Term — read off the request the answer is for: the target's
    // number (editTarget), the device's knob (editDevice, revertEdits: the request's device and the answer's field), a
    // load's audio. A field's rejection whose field no table names (a master's own numbers) is the command's refusal,
    // fact 131, whole. nullopt for an accepted answer, or a code this library does not know.
    [[nodiscard]] static std::optional<Fact> rejected (const Answer& answer, const Request& request) noexcept;
};

} // namespace felitronics::session::text
