// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Project.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

//==============================================================================
// felitronics::session::config — the numbers of the mastering session, as typed structs. Two TOML documents,
// modules/session/config/targets.toml (what a master is made for) and modules/session/config/engine.toml (every other
// number the session decides, measures, renders and reports with), are compiled into the library at build time as
// constexpr data (felitronics-toml's felitronics_toml_embed); nothing reads a file at run time. Config::load() binds
// them to the structs below by schema — every key read with its type and its range, and every key the schema did not
// read reported — so a typo is an error with a line and a column, never a silently ignored setting.
//
// THE DOCUMENTS SAY WHAT THE NUMBERS MEAN. Every struct here mirrors a table of a document and every field a key of the
// same name — a small inline table's fields joined to its key's name (observations.hum.fromProminenceDb is
// Observations::humFromProminenceDb), the comment naming the key where the spelling is not obvious. The units and the
// reasons are written beside the numbers in the documents and are not repeated here. A field is required unless it is
// std::optional or its comment says otherwise.
//
// THE BUILD HOLDS THE SCHEMA. Every build of the library — a consumer's included — runs the schema over the two documents
// before the library is built (felitronics_session_config_check, a host tool: modules/session/CMakeLists.txt), and a
// problem stops the build as `<file>:<line>:<column>: error:`. So for a library that was built, load() has no problems;
// it returns them anyway, because bind() — the same schema over texts a caller hands in — can.
//
// THE SCHEMA HOLDS FORM AND PHYSICS, NOT CHOICES: a key's type, the domain where its number means what its document says
// (a share within 0…1, a band that ascends, a corner the analyzer admits), and the checks across keys. Which numbers the
// owner chose is pinned elsewhere, by the decisions suite (tests/ConfigDecisionsTests.cpp), so that changing one is a
// deliberate edit of that test and not a schema error.
//
// As Session.h: this header carries no function body that computes anything. The binding, the canonical text and the
// version are compiled with the library's own flags (docs/SESSION.md).
namespace felitronics::session::config
{

//==============================================================================
// targets.toml

enum class Group : std::uint8_t { Streaming, Delivery, Aggregator };
// How an already-mastered source is delivered. A specification is a requirement and is met normally; a normalising
// platform is never turned down; Other is a medium/preset whose quieter target likewise does not justify more work.
enum class TargetClass : std::uint8_t { Specification, Streaming, Other };

struct Album
{
    double lufs = 0.0;
    bool desktopOnly = false;
};

// One row of [targets]; `key` is the row's key.
struct Target
{
    std::string key;
    Group group = Group::Streaming;
    TargetClass targetClass = TargetClass::Other;
    double lufs = 0.0;
    double tp = 0.0;
    double monoBass = 0.0;
    double hpfFloor = 0.0;
    std::int32_t hpfSlopeDbPerOct = 0;
    double noteLossDb = 0.0;
    std::int32_t sampleRate = 0;               // 0: the source's rate
    std::int32_t bitDepth = 0;
    bool noClipper = false;                    // optional in the document: false when absent
    bool vinyl = false;                        // optional: false when absent — the master goes to a cutting lathe
    bool sourceRatePass = false;               // optional: false when absent
    LoudnessMode loudnessMode = LoudnessMode::Manual;   // optional: manual when absent
    std::optional<double> lowDb;
    std::optional<Album> album;
};

// An inclusive range, written in a document as a two-item array [min, max] or as { min, max }.
struct Span
{
    double min = 0.0;
    double max = 0.0;
};

struct Edit
{
    Span domain; // finite LUFS has no bounds; TP uses this interval
    double from = 0.0;
    double to = 0.0;
    Span green;
    double step = 0.0;
};

// The note beside a target ([notes]): where its loudness comes from.
enum class TargetNote : std::uint8_t { Measured, Practice, NoNormalisation };
struct TargetNoteRow
{
    std::string key;                           // a row of [targets]
    TargetNote note = TargetNote::Measured;
};

struct Targets
{
    std::string defaultTarget;                 // `default`
    std::vector<std::string> main;
    std::vector<Target> targets;               // in the document's order
    Edit editLufs;                             // [edit] lufs
    Edit editTp;                               // [edit] tp
    std::vector<TargetNoteRow> notes;          // [notes], in the byte order of their keys

    // The row whose key is `key`, or null.
    [[nodiscard]] const Target* find (std::string_view key) const noexcept;
};

//==============================================================================
// engine.toml

struct Input
{
    double referenceLufs = 0.0;
    double quietWarningLufs = 0.0;             // quiet.warningLufs
    double quietGainOnlyLufs = 0.0;            // quiet.gainOnlyLufs
    double shortSeconds = 0.0;
};

struct Landing
{
    std::int32_t passes = 0;
    double toleranceLu = 0.0;
    double truePeakAimDb = 0.0;
    bool onSourceGate = false;
    // The limiter's budget by the target's loudness: below `middleLufs` quietDb, within it (both ends) middleDb, above
    // it loudDb — the window p95 of its gain reduction, dB, a whole number of quarter dB.
    double quietBudgetDb = 0.0, middleBudgetDb = 0.0, loudBudgetDb = 0.0;
    Span middleLufs;
    // [landing.max]: the max modes' search ceiling, the floor no max master lands under, and each mode's limiter budget (dB,
    // active-window P95, a whole number of quarter dB).
    double maxCeilingLufs = 0.0, maxFloorLufs = 0.0;
    double cleanBudgetDb = 0.0, denseBudgetDb = 0.0;
    bool maxExcerptSearch = false;
    double maxExcerptSeconds = 0.0, maxExcerptPreRollSeconds = 0.0;
    double maxExcerptPercentile = 0.0, maxExcerptToleranceDb = 0.0, maxExcerptOffsetDb = 0.0;
    double maxBudgetResolutionDb = 0.0;
    double limiterSlopeBelow = 0.0, limiterSlopeSpacingDb = 0.0;
};

struct PeakClipper
{
    Span manualDomain;
    double littleNeedDb = 0.0;
    double shortP90Ms = 0.0, shortBassShare = 0.0, shortPlrDb = 0.0;
    double longP90Ms = 0.0, longBassShare = 0.0, longPlrDb = 0.0;
    double clippedPerMinute = 0.0;             // confirmed clips a minute from which the source is clipped
    double shortCutDb = 0.0, betweenCutDb = 0.0;  // at most this much off the peaks, by class; the limiter does the rest
    double bassBelowHz = 0.0;
    double densityMinusDb = 0.0, densityWithinDb = 0.0;
    double kneeDb = 0.0;
    double manualMinDb = 0.0, manualMaxDb = 0.0, manualStepDb = 0.0;   // the manual cut starts at betweenCutDb
};

struct Limiter
{
    double ceilingMarginDb = 0.0;
    double lookaheadMs = 0.0;
    double releaseMs = 0.0;
    bool dualRelease = false;
    double slowReleaseMs = 0.0;
    PeakClipper peakClipper;
};

struct LowEndRun
{
    double crossoverHz = 0.0;
    double lowNoteHz = 0.0;
    double highNoteHz = 0.0;
    std::int32_t fftOrder = 0;
    double dutyThresholdDb = 0.0;
    std::int32_t skipBlocks = 0;
};

struct LowEnd
{
    double occupiedFromDuty = 0.0;
    double occupiedMarginWhenOnDb = 0.0;
    double infraLowCrossoverHz = 0.0;
    LowEndRun run;
};

struct HpfComfort
{
    double lowHz = 0.0;
    double highHz = 0.0;
    double warningLowHz = 0.0;
    double warningHighHz = 0.0;
};

struct HpfMark
{
    std::string key;
    double hz = 0.0;
};

struct Hpf
{
    std::string frequencyDomain;
    Span slopeDomain;
    std::int32_t slopeMultiple = 0;
    double hzStep = 0.0;
    std::int32_t band = 0;
    double hzMin = 0.0;
    double hzMax = 0.0;                        // the knob's travel ends here
    double machineTopHz = 0.0;                 // the machine's cutoff never goes above it
    std::vector<std::int32_t> slopes;
    std::vector<std::int32_t> slopesNormal;
    std::int32_t slopeDefault = 0;
    double noteAboveHz = 0.0;                  // note.aboveHz
    double noteSoundingAtLeastS = 0.0;         // note.soundingAtLeastS
    HpfComfort comfort;
    double curveTopDb = 0.0, curveBottomDb = 0.0, curveStepDb = 0.0, curveHeadroomDb = 0.0;
    std::vector<HpfMark> marks;
};

struct Zone
{
    double fromHz = 0.0;
    double toHz = 0.0;
};

struct MonoBass
{
    Span frequencyDomain, lowWidthDomain;
    double lowWidth = 0.0;
    Span lowWidthRange;
    double lowWidthStep = 0.0;
    Span frequencyRange;
    double frequencyStep = 0.0;
    double lossWarnFromDb = 0.0, lossOffAboveDb = 0.0;              // loss.warnFromDb, loss.offAboveDb
    double lossSoundingWithinDb = 0.0, lossSoundingAtLeastS = 0.0;  // loss.soundingWithinDb, loss.soundingAtLeastS
    Zone clubZone;                             // zones.club
    Zone vinylZone;                            // zones.vinyl
};

enum class Detector : std::uint8_t { Peak, Rms };
enum class Link : std::uint8_t { Max, MeanPower };
enum class CompressorMode : std::uint8_t { DownCompress, UpCompress, DownExpand };
enum class ThresholdFrom : std::uint8_t { ShortTermP95 };
enum class ConfidenceLabel : std::uint8_t { Low, Medium, High };   // felitronics::tempo's labels above undetermined

struct Compressor
{
    Detector detector = Detector::Rms;
    double rmsWindowMs = 0.0;
    Link link = Link::Max;
    CompressorMode mode = CompressorMode::DownCompress;
    double rangeDb = 0.0;
    double makeupDb = 0.0;
    bool autoMakeup = false;
    double lookaheadMs = 0.0;
    double sidechainHpfHz = 0.0;               // 0: self-keyed
    ThresholdFrom thresholdFrom = ThresholdFrom::ShortTermP95;
    Span limitThreshOffset, limitAttack, limitRelease, limitKnee;   // [compressor.limits]
    double tempoBpmWhenUnsure = 0.0;                                 // [compressor.tempo] bpmWhenUnsure
    ConfidenceLabel tempoTrustedConfidence = ConfidenceLabel::High;  // [compressor.tempo] trustedConfidence
};

enum class Law : std::uint8_t { ByDepth, Linear, Geometric };

struct GlueRamp
{
    double from = 0.0;
    double to = 0.0;
    Law law = Law::Linear;
};

// The glue's numbers are on its knob, "up to N dB" — `default`, `whenTicked`, `byTarget`; the ramps are its travel, the
// compressor's internal mapping.
struct GlueAtTarget
{
    std::string target;
    double upToDb = 0.0;
};

struct Glue
{
    Span domain;
    double defaultUpToDb = 0.0;                // `default`
    double whenTickedUpToDb = 0.0;             // `whenTicked`
    std::vector<GlueAtTarget> byTarget;        // in the document's order
    GlueRamp ratio, threshOffset, attack, knee, divisor;
    double knobMinDb = 0.0, knobMaxDb = 0.0, knobStepDb = 0.0;
    double detectorOverP95Db = 0.0;            // the threshold's calibration: dB above P95 + threshOffset
    // The glue in parallel (v0.17.0): the compressed share of its output — `mix`, the machine's, on `mixRange` by
    // `mixStep` inside `mixDomain`.
    Span mixDomain, mixRange;
    double mix = 0.0, mixStep = 0.0;
};

// felitronics-core's WaveShaper::Shape, in its order and values (Tube … Tape since v0.57.0). The machine's type; a person
// picks among Tanh, Tube, Transistor, Transformer and Tape only (Project.h, SaturationType).
enum class SaturationShape : std::uint8_t { Tanh, Atan, Cubic, Asym, Tube, Transistor, Transformer, Tape };

struct Saturation
{
    Span driveDomain, mixDomain;
    SaturationShape shape = SaturationShape::Tanh;
    double driveDb = 0.0;
    Span driveRange;
    double driveStep = 0.0;
    double bias = 0.0;
    double mix = 0.0;
    Span mixRange;
    double mixStep = 0.0;
    double autoComp = 0.0;
    double dcBlockHz = 0.0;
    double cutLoudShare = 0.0;                 // cut.loudShare: the loud places of the measured peak cut
};

// Tilt, low and the five bands: the knob is engine.toml's; the filter — freqHz, q, type — is felitronics-bands' bands.toml
// (hz, q, type), the one table of the named bands every Felitronics product reads, bound here by the same schema.
struct Tilt
{
    Span domain;
    std::int32_t band = 0;
    double freqHz = 0.0;                       // bands.toml [bands.tilt] hz
    Span normal;
    Span hard;
    double step = 0.0;
};

struct Low
{
    Span domain;
    std::int32_t band = 0;
    double freqHz = 0.0;                       // bands.toml [bands.low] hz, q
    double q = 0.0;
    Span normal;
    Span hard;
    double step = 0.0;
};

// [bands]: the five static EQ bands a person turns (the machine leaves them at 0): each its own band of the EQ stage, a
// bell or a high shelf at freqHz with q (bands.toml), its gain's domain, its knob's travel `hard` and step.
enum class BandType : std::uint8_t { Bell, HighShelf };

struct EqMove
{
    std::int32_t band = 0;
    BandType type = BandType::Bell;
    double freqHz = 0.0;
    double q = 0.0;
    Span domain;
    Span normal;          // where the value turns red, within hard (a hint, as tilt's)
    Span hard;
    double step = 0.0;
};

struct Bands
{
    EqMove body, mud, forward, brightness, air;
};

struct Eq
{
    double curveWarnDb = 0.0;                  // curve.warnDb
    double curveScaleDb = 0.0;                 // curve.scaleDb
    double curveFieldDb = 0.0;                 // curve.fieldDb
};

struct Stages
{
    bool eq = false, monoBass = false, compressor = false, clipper = false, limiter = false, dither = false;
};

enum class NoiseShaping : std::uint8_t { None, Weighted, Psycho };

struct Dither
{
    std::int32_t onUpToBits = 0;
    NoiseShaping shaping = NoiseShaping::Weighted;
    std::int32_t shapingUpToBits = 0;
    std::uint64_t seed = 0;                    // written as sixteen hexadecimal digits
    bool autoBlank = false;
    std::int32_t autoBlankSamples = 0;
};

struct MasteredDelivery
{
    double loudAboveLufs = 0.0;
    double loudCeilingDbTp = 0.0;
    double regularCeilingDbTp = 0.0;
};

// [chain]: the chain's fixed geometry — the internal quantum and the oversampling of the saturation and the limiter.
struct Chain
{
    std::int32_t internalBlock = 0;
    std::int32_t oversampleFactor = 0;
    std::int32_t tapsPerPhase = 0;
};

struct DeEsser
{
    bool offered = false;
    bool automatic = false;
    double manualDepthDb = 0.0;
    std::int32_t band = 0;
    double q = 0.0, atk = 0.0, rel = 0.0;
    double offsetDb = 0.0, ratioSlope = 0.0, minDepthDb = 0.0, maxDepthDb = 0.0;
    double confidenceAbove = 0.0, minDeltaLufs = 0.0, maxDeltaLufs = 0.0;
    double centreStepHz = 0.0, widthStepOct = 0.0, depthStepDb = 0.0;
    Span centreHz, widthOct, depthDb;
    std::vector<double> witnessQuantiles;
};

struct StereoBursts
{
    double bandLowHz = 0.0, bandHighHz = 0.0;
    double hopMs = 0.0, baselineMs = 0.0;
    double enterDb = 0.0, exitDb = 0.0;
    std::int32_t eventCapacity = 0;
};

enum class Kind : std::uint8_t { Error, Warning, Note, Reading };

// [observations.kinds]: the kind of every finding the session publishes.
struct Kinds
{
    Kind clipping = Kind::Note, dcOffset = Kind::Note, bitsUnused = Kind::Note, dualMono = Kind::Note;
    Kind edgeSilence = Kind::Note, hum = Kind::Note, humWandered = Kind::Note, spectralWall = Kind::Note;
    Kind loudestLowNote = Kind::Note, sibilance = Kind::Note, lowestLowBand = Kind::Note, infraLow = Kind::Note;
    Kind wideBass = Kind::Note, polarity = Kind::Note, alreadyLimited = Kind::Note, tooQuiet = Kind::Note;
    Kind tooShort = Kind::Note;
};

struct Sibilance
{
    std::int32_t minHops = 0, maxHops = 0;
    double fromShareOverDomeDb = 0.0, fullAtShareOverDomeDb = 0.0;
    double sideBelowMidDb = 0.0, nearMonoShare = 0.0;
    double fromPerMinute = 0.0, fullAtPerMinute = 0.0;
    std::int32_t periodicToleranceHops = 0;
    std::vector<std::int32_t> periodicMultiples;
    double periodicFrom = 0.0, periodicFullAt = 0.0;
    double blindAtDuty = 0.0;
    double severityFromDb = 0.0, severityFullAtDb = 0.0;
    double excessQuantile = 0.0, shareQuantile = 0.0, sideMidQuantile = 0.0;
    std::int32_t loudestMoments = 0;
};

struct Observations
{
    double doubtfulBelow = 0.0;
    double clippingFullAtShareOfProgramme = 0.0;                                  // clipping
    double dcOffsetFrom = 0.0, dcOffsetFullAt = 0.0;                              // dcOffset
    double dcOffsetWarningFrom = 0.0, dcOffsetErrorFrom = 0.0;
    std::int32_t bitsUnusedDepthBits = 0;                                         // bitsUnused
    std::int32_t bitsUnusedFromBitsShort = 0, bitsUnusedFullAtBitsShort = 0, bitsUnusedErrorFromBitsShort = 0;
    double edgeSilenceFromSeconds = 0.0, edgeSilenceFullAtSeconds = 0.0;          // edgeSilence
    double humFromProminenceDb = 0.0, humFullAtProminenceDb = 0.0;                // hum
    double humFromPowerDb = 0.0, humFullAtPowerDb = 0.0, humWarningFromSeverity = 0.0;
    double humWanderedConfidenceCeiling = 0.0;                                    // humWandered
    double spectralWallFullAtDropDb = 0.0;                                        // spectralWall
    double spectralWallFromFractionBelowNyquist = 0.0, spectralWallFullAtFractionBelowNyquist = 0.0;
    double infraLowLow = 0.0, infraLowHigh = 0.0, infraLowWarningFrom = 0.0;      // infraLow
    double wideBassSideFractionAtLeast = 0.0, wideBassFullAt = 0.0;               // wideBass
    double polarityCorrelationBelow = 0.0, polarityRawSideFractionAbove = 0.0;    // polarity
    double polarityFullAtLowCorrelation = 0.0;
    double alreadyLimitedPlrBelowDb = 0.0, alreadyLimitedFullAtPlrDb = 0.0;       // alreadyLimited
    double masteredAboveLufs = 0.0, masteredPeakAboveDbTp = 0.0, masteredPlrBelowDb = 0.0;
    double vinylTopAboveHz = 0.0;                                                 // vinylTop
    Kinds kinds;
    Sibilance sibilance;
};

struct Crest
{
    std::vector<double> bandEdgesHz;
    double hopMs = 0.0;
    std::int32_t blockHops = 0;
    double bandShareFloorDb = 0.0, floorDb = 0.0, belowMeanSquareDb = 0.0, noProgrammeDb = 0.0;
};

struct Sections
{
    double changeLu = 0.0, minSeconds = 0.0;
    double masterShorterByAtMost = 0.0, minComparedShare = 0.0;
    std::int32_t worstNamedAbove = 0;
    double quantile = 0.0;
};

struct Pumping
{
    double highPassHz = 0.0, lowPassHz = 0.0, settleSeconds = 0.0, minTraceRateHz = 0.0;
};

// [cost.damage]: PEAQ of the master against its chain at rest — windows, the BS.1116 grades, the reference's level.
struct Damage
{
    double windowSeconds = 0.0, hopSeconds = 0.0;
    std::vector<double> gradeFloorsOdg;
    double referenceBelowDb = 0.0;
    std::int32_t resamplerTaps = 0;
};

struct Cost
{
    double activityBelowIntegratedLu = 0.0, activityFloorLufs = 0.0;
    double limiterActiveInputDb = 0.0;
    double firstBlockSeconds = 0.0, blockSeconds = 0.0, histogramStepDb = 0.0;
    double absoluteGateLufs = 0.0, relativeGateLu = 0.0;
    double tooQuietBelowLufs = 0.0;
    double grWindowSeconds = 0.0;
    std::int32_t grTraceBucketsMin = 0, grTraceBucketsMax = 0;
    std::vector<double> printedQuantiles;
    Pumping pumping;
    Sections sections;
    Damage damage;
};

// [progress.analysis.weights]: milliseconds of work per step of the measurement.
struct AnalysisWeights
{
    double loudness = 0.0, report = 0.0, lowEnd120 = 0.0, lowEnd150 = 0.0, forensics = 0.0, stereo = 0.0;
    double lowEndSweep = 0.0, stereoBursts = 0.0, crest = 0.0, hum = 0.0, tempo = 0.0;
    double waveformIndex = 0.0, excursionsIndex = 0.0;
};

struct Progress
{
    double analysisReferenceSeconds = 0.0;     // [progress.analysis] referenceSeconds
    double analysisEstimateFromSeconds = 0.0;  // [progress.analysis] estimateFromSeconds
    AnalysisWeights analysisWeights;
    double masterPassWeight = 0.0;             // [progress.master] passWeight
    double masterMeasureWeight = 0.0;          // [progress.master] measureWeight
    std::int32_t masterExpectedPasses = 0;     // [progress.master] expectedPasses
};

// The blind test's protocol. Its variants — the chains a pair compares — are not here: they are defined with the test.
struct BlindTest
{
    std::vector<std::string> targets;
    double fragmentSeconds = 0.0;
    std::int32_t repeats = 0;
    double matchToleranceLu = 0.0;
    std::int32_t listenedMinSwitches = 0;      // listened.minSwitches
};

struct Engine
{
    std::string defaults;
    Input input;
    Landing landing;
    Limiter limiter;
    LowEnd lowEnd;
    Hpf hpf;
    MonoBass monoBass;
    Compressor compressor;
    Glue glue;
    Saturation saturation;
    Tilt tilt;
    Low low;
    Bands bands;
    Eq eq;
    Chain chain;
    Stages stages;
    Dither dither;
    MasteredDelivery masteredDelivery;
    DeEsser deEsser;
    StereoBursts stereoBursts;
    Observations observations;
    Crest crest;
    Cost cost;
    Progress progress;
    BlindTest blindTest;
};

//==============================================================================
// PROBLEMS — data, never text: which document, what is wrong, where (line and column, 1-based, the column counted in
// characters), the key path in TOML spelling, and a stable name. The message a person reads is a shell's to write.

enum class Document : std::uint8_t { Targets, Engine, Bands };   // Bands: felitronics-bands' bands.toml

enum class Fault : std::uint8_t
{
    Syntax,        // the document is not TOML of felitronics-toml's subset; `code` is the parser's code (DuplicateKey, …)
    Missing,       // a required key is absent; points at the table that lacks it
    WrongType,     // the value has another type
    OutOfRange,    // outside the field's range, or not representable in the field's type
    UnknownKey,    // a key the schema does not read — a typo, most often; points at the key
    Refused        // a check across keys refused it; `code` names the check (Refusal below)
};

enum class Refusal : std::uint8_t
{
    None,
    NotOneOf,      // not one of the names or values this key takes
    NotATarget,    // names no row of [targets]
    NotAPair,      // a range must be a two-item array
    OutOfOrder,    // a minimum above its maximum, or a list that must ascend and does not
    Duplicate,     // named twice where once is the rule (a target in `main`, an EQ band given to two devices)
    Fixed,         // a value the session does not let change (the limiter is always on, the printed quantiles)
    NotOnStep,     // not a whole number of its step: an analyzer hop of its time quantum, the limiter's budget of 0.25 dB
    Mismatch,      // differs from the key it must equal
    WrittenDefault,// an optional flag written as its default: it is written only when it is true
    NotApplicable, // set where it cannot apply (a pass at the source's rate on a target that keeps the source's rate)
    OutsideLaw,    // a ramp's ends outside its law's domain, or a law the field does not take
    AboveNyquist,  // a frequency at or above half the rate the signal it filters is sampled at
    AnalyzerRefuses,// the analyzer this block feeds refuses it — its own storageFor() — at a source rate the product accepts
                   // (and the chain its [chain] geometry: MasteringChain::admits)
    EmptyKey       // a row of [targets] under an empty key: a target is named by its key, and "" names none
};

struct Problem
{
    Document document = Document::Targets;
    Fault fault = Fault::Syntax;
    Refusal refusal = Refusal::None;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string path;
    const char* code = "";                     // the fault's name, the parser's code for Syntax, the refusal's for Refused

    [[nodiscard]] static const char* name (Document document) noexcept;   // "targets", "engine", "bands"
    [[nodiscard]] static const char* name (Fault fault) noexcept;
    [[nodiscard]] static const char* name (Refusal refusal) noexcept;
};

// THE CONFIG'S VERSIONS — 64-bit FNV-1a hashes of the documents' NORMALISED data, targets, engine, then felitronics-bands'
// bands.toml (sound, every key of it). Every number is
// the bits of its correctly rounded double, −0 read as +0 (so 50, 50.0 and 50.00 are one value); every table is walked in
// the byte order of its keys (so the order keys are written in, and whether a table is inline, is no data), except the
// rows of [targets], whose written order `all` keeps (it is the order a shell lists them in); order counts in arrays,
// where it means something (the main targets, a series). Positions, comments and spacing are not data.
// The same on every platform, and computed from the data compiled into the library without allocating.
//   all    every key of both documents: which config this is.
//   sound  what can change a master; when it is not sure, a key stays in. It leaves out only: what is shown (the main
//          list and the order of the target rows, the targets' notes, the hand edit's travels and green ranges, the red and comfort zones —
//          hpf.comfort, tilt.normal, low.normal, hpf.slopesNormal — the curve scales and marks, the knob scale's
//          zones); what prints a finding or a warning without switching a device (every observation threshold but
//          observations.polarity, which keeps mono bass out; the peak clipper's density figures); what is measured
//          after the master (the crest, the cost); development (the progress weights, the blind test); the name of the
//          defaults; and, while deEsser.offered is false, the de-esser's block and the bursts only it reads. The default
//          target stays in: a project that leaves an unchanged target out reopens on it. A recipe records this one: a
//          master names the numbers it was made with.
struct Versions
{
    std::uint64_t all = 0;
    std::uint64_t sound = 0;
};

//==============================================================================
// THE CONFIG, and the functions that give it — static members, as Session's are: a public header declares no free
// function (the session-laws lint, rule PUBLIC).

struct Loaded;

struct Config
{
    Targets targets;
    Engine engine;

    // THE CONFIG THIS LIBRARY WAS BUILT WITH, bound by schema. Allocates; reads no file.
    [[nodiscard]] static Loaded load();

    // The same schema over the documents a caller hands in — a tool, a test. A document that does not parse is one
    // Syntax problem and binds nothing; the others are still read. Without bandsToml, the bands.toml compiled in.
    [[nodiscard]] static Loaded bind (std::string_view targetsToml, std::string_view engineToml, std::string_view bandsToml);
    [[nodiscard]] static Loaded bind (std::string_view targetsToml, std::string_view engineToml);

    // An embedded document as TOML, through felitronics-toml's canonical writer: the same data always gives the same
    // bytes. Comments are not data and are not kept. What `fcore_session config targets|engine` prints.
    [[nodiscard]] static std::string text (Document document);

    // The config's versions (above), from the data compiled into the library; allocates nothing.
    [[nodiscard]] static Versions versions() noexcept;

    // The versions of the documents given as text — the source files, say — by the same walk; nullopt when one does not
    // parse. For the library's own sources they equal versions(). Without bandsToml, the bands.toml compiled in.
    [[nodiscard]] static std::optional<Versions> versionsOf (std::string_view targetsToml, std::string_view engineToml,
                                                             std::string_view bandsToml);
    [[nodiscard]] static std::optional<Versions> versionsOf (std::string_view targetsToml, std::string_view engineToml);
};

struct Loaded
{
    Config config;
    std::vector<Problem> problems;             // in the order found: the engine first, then the targets
    [[nodiscard]] bool ok() const noexcept;    // no problem
};

} // namespace felitronics::session::config
