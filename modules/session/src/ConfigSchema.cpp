// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE CONFIG'S SCHEMA (Config.h, src/ConfigBind.h): two parsed documents read into the typed structs with felitronics-toml's
// Reader — every key with its type and its range, every key nobody read reported as unknown — and the checks across keys.
// Compiled into the library and into felitronics_session_config_check, the gate every build runs.
//
// FORM AND PHYSICS, NOT CHOICES. A range here is where a number stops meaning what its document says: a share outside 0…1,
// a ramp that would divide by zero, a default outside its knob's domain. What an analyzer admits is the analyzer's to say, so a
// block the config feeds one (analysis::LowEnd, analysis::BandCrest, analysis::StereoBandBursts) is handed to that
// analyzer's own storageFor() at the source rates the product accepts, and refused whole when the analyzer refuses it —
// one source of truth, never a hand-written copy of its domain. The numbers the owner chose are pinned by tests/ConfigDecisionsTests.cpp, not here. A range that
// another key states — a target's default inside its knob's domain — is taken from that key
// once it was read; until then the key's own domain stands in, so one wrong number is one problem.
//
// ORDER: the engine first, then the targets, whose rows are checked against the engine's knobs. Within the engine the
// observations come before the high-pass, whose "no DC" is the dcOffset finding's threshold. The engine's references to
// targets (the glue per target, the blind test's series) are checked against the row keys of the targets document, read
// before either is bound.

#include "BuildGuards.h"

#include "ConfigBind.h"
#include "BuildContract.h"
#include <limits>
#include "ConfigVersion.h"
#include "Grid.h"

#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/LowEnd.h>
#include <felitronics/analysis/StereoBandBursts.h>
#include <felitronics/session/Config.h>
#include <felitronics/toml/Schema.h>
#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace felitronics::session::config
{
namespace
{
namespace toml = felitronics::toml;
using toml::Need;
using toml::Reader;
using R = toml::Range<double>;
using I = toml::Range<std::int32_t>;

// Shared domains. Functions, not namespace-scope objects: a toml::Range is not a literal type, and a const object of it
// would be initialised at load time.
R share() { return { 0.0, 1.0 }; }
R anyLufs() { return { -120.0, 0.0 }; }
I eqBand() { return { 0, 23 }; }   // the core's EQ stage has 24 bands (FC_MAX_EQ_BANDS)

// THE SOURCE RATES THE PRODUCT ACCEPTS, at which an analyzer must admit its block: the lowest the core admits
// (core::kMinSampleRate), the two common ones and the highest the web shell takes. The phase-one measurements the machine
// decides from (the low end, the crest) must hold at every one of them.
constexpr double kSourceRates[] = { 8000.0, 44100.0, 48000.0, 96000.0 };
// The sibilance-band bursts from 44.1 kHz up: their band's upper corner needs 0.49 of the rate above it, which a source
// under about 18.4 kHz cannot give a 9 kHz corner — there the analyzer refuses and the bursts go unmeasured, a fact of
// that source and not of this config.
constexpr double kBurstRates[] = { 44100.0, 48000.0, 96000.0 };
constexpr int kChannels = 2;                   // a stereo source, the most an analyzer is asked to hold here
// Where analysis::SourceForensics starts calling a drop a wall (SourceForensicsParams::minDropDb): the spectral wall's
// confidence ramp starts there, so its full weight must lie above it.
constexpr double kCoreMinDropDb = 24.0;
constexpr std::int32_t kLowestDeliveryRate = 8000;     // a delivery rate other than 0 is at least the core's lowest
constexpr std::int32_t kHighestDeliveryRate = 384000;

template <class E> struct Name
{
    std::string_view text;
    E value;
};
constexpr Name<Group> kGroups[] = { { "streaming", Group::Streaming }, { "delivery", Group::Delivery },
                                    { "aggregator", Group::Aggregator } };
constexpr Name<Detector> kDetectors[] = { { "peak", Detector::Peak }, { "rms", Detector::Rms } };
constexpr Name<Link> kLinks[] = { { "max", Link::Max }, { "meanPower", Link::MeanPower } };
constexpr Name<CompressorMode> kModes[] = { { "downCompress", CompressorMode::DownCompress },
                                            { "upCompress", CompressorMode::UpCompress },
                                            { "downExpand", CompressorMode::DownExpand } };
constexpr Name<ThresholdFrom> kThresholdFrom[] = { { "shortTermP95", ThresholdFrom::ShortTermP95 } };
constexpr Name<ConfidenceLabel> kConfidence[] = { { "low", ConfidenceLabel::Low }, { "medium", ConfidenceLabel::Medium },
                                                  { "high", ConfidenceLabel::High } };
constexpr Name<Law> kLaws[] = { { "byDepth", Law::ByDepth }, { "linear", Law::Linear }, { "geometric", Law::Geometric } };
constexpr Name<SaturationShape> kShapes[] = { { "tanh", SaturationShape::Tanh }, { "atan", SaturationShape::Atan },
                                              { "cubic", SaturationShape::Cubic }, { "asym", SaturationShape::Asym } };
constexpr Name<NoiseShaping> kShapings[] = { { "none", NoiseShaping::None }, { "weighted", NoiseShaping::Weighted },
                                             { "psycho", NoiseShaping::Psycho } };
constexpr Name<Kind> kKinds[] = { { "error", Kind::Error }, { "warning", Kind::Warning }, { "note", Kind::Note } };

// Exact equality of two doubles, each the correctly rounded value of a decimal of a document, without -Wfloat-equal's
// objection.
bool same (double a, double b) { return ! (a < b) && ! (b < a); }

void appendIndex (std::string& out, std::size_t n)
{
    char digits[24];
    std::size_t k = 0;
    do { digits[k++] = char ('0' + n % 10); n /= 10; } while (n != 0);
    while (k != 0) out += digits[--k];
}

std::string pathOf (const Reader& in, std::string_view key)
{
    std::string path = in.path();
    if (! path.empty()) path += '.';
    path += key;        // every key of the config documents is bare, so it needs no quoting
    return path;
}

// Is x on the grid that starts at `from` with `step` — is (x − from) a whole number of steps, exactly, on the decimals
// as written? The one rule for a knob's grid, which the session's commands hold a person's edits to as well
// (src/Grid.h).
using felitronics::session::detail::onGrid;

// The decimal a key holds, and the decimal an array's item holds — the document's own digits, never a double's.
std::optional<toml::Decimal> decimalAt (const Reader& in, std::string_view key)
{
    const toml::Value* v = in.data().find (key);
    return v != nullptr ? toml::asDecimal (*v) : std::nullopt;
}

// A knob's grid: where its travel starts, and its step — both as written.
struct Grid
{
    std::optional<toml::Decimal> from, step;
};

// ONE DOCUMENT BEING READ: its reader's report, and the checks across keys that report into it.
struct Doc
{
    toml::Report report;

    void refuse (Reader& in, std::string_view key, Refusal why) { in.refuse (key, std::uint32_t (why)); }

    toml::Position positionOf (const Reader& in, std::string_view key) const
    {
        const toml::Value* v = in.data().find (key);
        return v != nullptr ? v->position : in.data().position;
    }

    // A bound the Reader's inclusive ranges cannot say (above zero, above the core's number): OutOfRange at the value.
    void outOfRange (Reader& in, std::string_view key)
    {
        report.problems.push_back ({ toml::Fault::OutOfRange, toml::Severity::Error, pathOf (in, key), positionOf (in, key), 0 });
    }

    // Item i of the array at `key`: the problem points at the item.
    void refuseItem (Reader& in, std::string_view key, std::size_t i, Refusal why)
    {
        toml::Position at = in.data().position;
        if (const toml::Value* v = in.data().find (key))
        {
            if (const auto* a = std::get_if<toml::Array> (&v->data); a != nullptr && i < a->size()) at = (*a)[i].position;
            else at = v->position;
        }
        std::string path = pathOf (in, key);
        path += '[';
        appendIndex (path, i);
        path += ']';
        report.problems.push_back ({ toml::Fault::Refused, toml::Severity::Error, std::move (path), at, std::uint32_t (why) });
    }

    template <class E, std::size_t N> bool name (Reader& in, std::string_view key, E& out, const Name<E> (&names)[N])
    {
        std::string text;
        if (! in.required (key, text)) return false;
        for (const auto& n : names)
            if (text == n.text) { out = n.value; return true; }
        refuse (in, key, Refusal::NotOneOf);
        return false;
    }

    // [min, max]: a two-item array within `domain`, min < max — a travel of one point is no travel.
    bool pair (Reader& in, std::string_view key, Span& out, const R& domain)
    {
        std::vector<double> v;
        if (! in.required (key, v, domain)) return false;
        if (v.size() != 2) { refuse (in, key, Refusal::NotAPair); return false; }
        if (! (v[0] < v[1])) { refuse (in, key, Refusal::OutOfOrder); return false; }
        out = { v[0], v[1] };
        return true;
    }

    // { min = …, max = … } within `domain`, min < max.
    bool minMax (Reader& in, std::string_view key, Span& out, const R& domain)
    {
        bool good = false;
        in.table (key, Need::Required, [&] (Reader& t)
        {
            Span s;
            const bool lo = t.required ("min", s.min, domain);
            const bool hi = t.required ("max", s.max, domain);
            if (lo && hi && ! (s.min < s.max)) refuse (t, "max", Refusal::OutOfOrder);
            else if (lo && hi) { out = s; good = true; }
        });
        return good;
    }

    // Two keys of one table where the first must lie strictly below the second — the two ends of a ramp, so that nothing
    // divides by their difference being zero: the second is refused.
    void below (Reader& in, bool bothRead, double first, double second, std::string_view secondKey)
    {
        if (bothRead && ! (first < second)) refuse (in, secondKey, Refusal::OutOfOrder);
    }
    // ...or at most the second.
    void notAbove (Reader& in, bool bothRead, double first, double second, std::string_view secondKey)
    {
        if (bothRead && second < first) refuse (in, secondKey, Refusal::OutOfOrder);
    }

    // A list that must strictly ascend — which also says no item is there twice.
    template <class T> void ascending (Reader& in, std::string_view key, const std::vector<T>& v)
    {
        for (std::size_t i = 1; i < v.size(); ++i)
            if (! (v[i - 1] < v[i])) { refuseItem (in, key, i, Refusal::OutOfOrder); return; }
    }

    // A list whose items are named once each.
    void unique (Reader& in, std::string_view key, const std::vector<std::string>& v)
    {
        for (std::size_t i = 1; i < v.size(); ++i)
            if (std::find (v.begin(), v.begin() + std::ptrdiff_t (i), v[i]) != v.begin() + std::ptrdiff_t (i))
                refuseItem (in, key, i, Refusal::Duplicate);
    }

    // The analyzer hop sits on its real time quantum.
    void onStep (Reader& in, std::string_view key, const Grid& grid)
    {
        const auto x = decimalAt (in, key);
        if (grid.from && grid.step && x && ! onGrid (*x, *grid.from, *grid.step)) refuse (in, key, Refusal::NotOnStep);
    }

    // An EQ band of the core, which no two devices may share.
    void band (Reader& in, std::int32_t& out, std::vector<std::int32_t>& taken)
    {
        if (! in.required ("band", out, eqBand())) return;
        if (std::find (taken.begin(), taken.end(), out) != taken.end()) refuse (in, "band", Refusal::Duplicate);
        else taken.push_back (out);
    }

    // An optional flag, false when absent; written only when true.
    void flag (Reader& in, std::string_view key, bool& out)
    {
        if (in.optional (key, out) && ! out) refuse (in, key, Refusal::WrittenDefault);
    }
};

bool contains (const std::vector<std::string>& v, std::string_view s)
{
    return std::find (v.begin(), v.end(), s) != v.end();
}
bool contains (const std::vector<std::int32_t>& v, std::int32_t x)
{
    return std::find (v.begin(), v.end(), x) != v.end();
}

// The keys of the rows of [targets], straight from the document, before anything is bound.
std::vector<std::string> rowKeys (const toml::Table& targetsDocument)
{
    std::vector<std::string> keys;
    if (const toml::Value* v = targetsDocument.find ("targets"))
        if (const auto* t = std::get_if<toml::Table> (&v->data))
            for (const auto& e : t->entries()) keys.push_back (e.key);
    return keys;
}

//==============================================================================
// engine.toml

void readInput (Doc& d, Reader& in, Input& o)
{
    in.required ("referenceLufs", o.referenceLufs, R { -40.0, -6.0 });
    in.table ("quiet", Need::Required, [&] (Reader& t)
    {
        const bool warning = t.required ("warningLufs", o.quietWarningLufs, anyLufs());
        const bool gainOnly = t.required ("gainOnlyLufs", o.quietGainOnlyLufs, anyLufs());
        d.below (t, warning && gainOnly, o.quietGainOnlyLufs, o.quietWarningLufs, "warningLufs");   // gain-only is the quieter
    });
    in.required ("shortSeconds", o.shortSeconds, R { 0.0, 600.0 });
    in.required ("shortConfidence", o.shortConfidence, share());
}

void readLanding (Doc& d, Reader& in, Landing& o)
{
    if (in.required ("passes", o.passes, I { 1, 1000 }))
    {
        if (o.passes.empty()) d.refuse (in, "passes", Refusal::NotOneOf);   // a landing has at least one series
        for (std::size_t i = 1; i < o.passes.size(); ++i)                   // a series is never shorter than the last
            if (o.passes[i] < o.passes[i - 1]) { d.refuseItem (in, "passes", i, Refusal::OutOfOrder); break; }
    }
    in.required ("toleranceLu", o.toleranceLu, R { 0.001, 1.0 });
    in.required ("overshootFreeDb", o.overshootFreeDb, R { 0.0, 24.0 });
    in.required ("overshootPerDb", o.overshootPerDb, R { 0.1, 100.0 });
    in.required ("overshootCapDb", o.overshootCapDb, R { 0.0, 24.0 });
}

R readDomain (Doc& d, Reader& in, std::string_view key, Span& out, const R& limits)
{
    return d.pair (in, key, out, limits) ? R { out.min, out.max } : limits;
}
R hpfDomain() { return { std::numeric_limits<double>::min(), 3999.999999999 }; }

void readPeakClipper (Doc& d, Reader& in, PeakClipper& o)
{
    const R domain = readDomain (d, in, "manualDomain", o.manualDomain, R { 0.0, 6.0 });
    in.required ("littleNeedDb", o.littleNeedDb, R { 0.0, 24.0 });
    // The classes: short needles are shorter, carry less bass and come with more PLR than long ones; the short class's
    // threshold stands further above the ceiling than the "between" class's.
    const bool sp = in.required ("shortP90Ms", o.shortP90Ms, R { 0.0, 1000.0 });
    const bool lp = in.required ("longP90Ms", o.longP90Ms, R { 0.0, 1000.0 });
    d.below (in, sp && lp, o.shortP90Ms, o.longP90Ms, "longP90Ms");
    const bool sb = in.required ("shortBassShare", o.shortBassShare, share());
    const bool lb = in.required ("longBassShare", o.longBassShare, share());
    d.below (in, sb && lb, o.shortBassShare, o.longBassShare, "longBassShare");
    const bool splr = in.required ("shortPlrDb", o.shortPlrDb, R { 0.0, 40.0 });
    const bool lplr = in.required ("longPlrDb", o.longPlrDb, R { 0.0, 40.0 });
    d.below (in, splr && lplr, o.longPlrDb, o.shortPlrDb, "shortPlrDb");
    in.required ("bassBelowHz", o.bassBelowHz, R { 20.0, 2000.0 });
    const bool dm = in.required ("densityMinusDb", o.densityMinusDb, R { 0.0, 24.0 });
    const bool dw = in.required ("densityWithinDb", o.densityWithinDb, R { 0.0, 24.0 });
    d.below (in, dm && dw, o.densityWithinDb, o.densityMinusDb, "densityMinusDb");
    in.required ("kneeDb", o.kneeDb, R { 0.0, 1.0 });
    const bool lo = in.required ("manualMinDb", o.manualMinDb, domain);
    const bool hi = in.required ("manualMaxDb", o.manualMaxDb, domain);
    d.below (in, lo && hi, o.manualMinDb, o.manualMaxDb, "manualMaxDb");
    in.required ("manualStepDb", o.manualStepDb, R { 0.01, 3.0 });
    // The manual threshold starts at the "between" class, inside the accepted domain.
    const bool between = in.required ("betweenOverDb", o.betweenOverDb, domain);
    const bool sh = in.required ("shortOverDb", o.shortOverDb, R { 0.0, 12.0 });
    d.below (in, between && sh, o.betweenOverDb, o.shortOverDb, "shortOverDb");
}

void readLimiter (Doc& d, Reader& in, Limiter& o)
{
    in.required ("ceilingMarginDb", o.ceilingMarginDb, R { 0.0, 3.0 });
    const bool fast = in.required ("releaseMs", o.releaseMs, R { 8000.0 / kSourceRates[0], std::numeric_limits<double>::max() });
    in.required ("dualRelease", o.dualRelease);
    const bool slow = in.required ("slowReleaseMs", o.slowReleaseMs, R { 8000.0 / kSourceRates[0], std::numeric_limits<double>::max() });
    d.notAbove (in, fast && slow, o.releaseMs, o.slowReleaseMs, "slowReleaseMs");   // the slow envelope is not the faster
    in.table ("peakClipper", Need::Required, [&] (Reader& t) { readPeakClipper (d, t, o.peakClipper); });
}

// The low end (analysis::LowEnd): the main run and the infra-low run — the same geometry, split at infraLowCrossoverHz —
// each admitted by LowEnd::storageFor at every source rate, or refused whole.
bool lowEndAdmits (const LowEndRun& r, double crossoverHz)
{
    analysis::LowEndParams p;
    p.crossoverHz = crossoverHz;
    p.lowNoteHz = r.lowNoteHz;
    p.highNoteHz = r.highNoteHz;
    p.fftOrder = r.fftOrder;
    p.dutyThresholdDb = r.dutyThresholdDb;
    p.skipBlocks = r.skipBlocks;
    return std::all_of (std::begin (kSourceRates), std::end (kSourceRates),
                        [&] (double rate) { return analysis::LowEnd::storageFor (rate, kChannels, p).ok; });
}

void readLowEnd (Doc& d, Reader& in, LowEnd& o)
{
    in.required ("occupiedFromDuty", o.occupiedFromDuty, share());
    in.required ("occupiedMarginWhenOnDb", o.occupiedMarginWhenOnDb, R { 0.0, 40.0 });
    bool run = false;
    in.table ("run", Need::Required, [&] (Reader& t)
    {
        LowEndRun& r = o.run;
        // Types and signs here; what the analyzer admits, below, is the analyzer's to say. Every key is read, whichever fail.
        const bool read[] = { t.required ("crossoverHz", r.crossoverHz, R { 0.0, 1.0e6 }),
                              t.required ("lowNoteHz", r.lowNoteHz, R { 0.0, 1.0e6 }),
                              t.required ("highNoteHz", r.highNoteHz, R { 0.0, 1.0e6 }),
                              t.required ("fftOrder", r.fftOrder, I { 0, 30 }),
                              t.required ("dutyThresholdDb", r.dutyThresholdDb, R { -1.0e6, 1.0e6 }),
                              t.required ("skipBlocks", r.skipBlocks) };
        run = std::all_of (std::begin (read), std::end (read), [] (bool x) { return x; });
    });
    if (run && ! lowEndAdmits (o.run, o.run.crossoverHz)) d.refuse (in, "run", Refusal::AnalyzerRefuses);
    // The infra-low run is a second split BELOW the main one.
    if (in.required ("infraLowCrossoverHz", o.infraLowCrossoverHz, R { 0.0, 1.0e6 }) && run)
    {
        if (! (o.infraLowCrossoverHz < o.run.crossoverHz)) d.refuse (in, "infraLowCrossoverHz", Refusal::OutOfOrder);
        else if (! lowEndAdmits (o.run, o.infraLowCrossoverHz)) d.refuse (in, "infraLowCrossoverHz", Refusal::AnalyzerRefuses);
    }
}

void readHpf (Doc& d, Reader& in, Hpf& o, std::vector<std::int32_t>& bands, const std::optional<double>& dcFrom)
{
    if (in.required ("frequencyDomain", o.frequencyDomain) && o.frequencyDomain != "sourceNyquist")
        d.refuse (in, "frequencyDomain", Refusal::Fixed);
    readDomain (d, in, "slopeDomain", o.slopeDomain, R { 6.0, 96.0 });
    if (! same (o.slopeDomain.min, 6) || ! same (o.slopeDomain.max, 96)) d.refuse (in, "slopeDomain", Refusal::Fixed);
    if (in.required ("slopeMultiple", o.slopeMultiple) && o.slopeMultiple != 6) d.refuse (in, "slopeMultiple", Refusal::Fixed);
    in.required ("hzStep", o.hzStep, R { std::numeric_limits<double>::min(), 4000.0 });
    d.band (in, o.band, bands);
    // Frequency travel is a slider hint within the domain at the lowest supported rate.
    const bool lo = in.required ("hzMin", o.hzMin, hpfDomain());
    const bool hi = in.required ("hzMax", o.hzMax, hpfDomain());
    d.below (in, lo && hi, o.hzMin, o.hzMax, "hzMax");
    const R travel = lo && hi && o.hzMin < o.hzMax ? R { o.hzMin, o.hzMax } : R { 1.0, 200.0 };
    in.required ("hzDefault", o.hzDefault, hpfDomain());
    if (in.required ("slopes", o.slopes, I { 6, 96 }))
    {
        if (o.slopes.empty()) d.refuse (in, "slopes", Refusal::NotOneOf);
        for (std::size_t i = 0; i < o.slopes.size(); ++i)
            if (o.slopes[i] % 6 != 0) d.refuseItem (in, "slopes", i, Refusal::NotOnStep);   // the core's 6 dB/oct grid
        d.ascending (in, "slopes", o.slopes);
    }
    if (in.required ("slopesNormal", o.slopesNormal, I { 6, 96 }))
    {
        for (std::size_t i = 0; i < o.slopesNormal.size(); ++i)
            if (! contains (o.slopes, o.slopesNormal[i])) d.refuseItem (in, "slopesNormal", i, Refusal::NotOneOf);
        d.ascending (in, "slopesNormal", o.slopesNormal);
    }
    if (in.required ("slopeDefault", o.slopeDefault, I { 6, 96 }) && o.slopeDefault % 6 != 0)
        d.refuse (in, "slopeDefault", Refusal::NotOneOf);
    in.table ("nothingBelowNote", Need::Required, [&] (Reader& t)
    {
        t.required ("infraLowBelow", o.nothingBelowNoteInfraLowBelow, share());
        // "No DC" is no dcOffset finding: the same threshold, named here and refused apart from it.
        if (t.required ("dcOffsetBelow", o.nothingBelowNoteDcOffsetBelow, share()) && dcFrom
            && ! same (o.nothingBelowNoteDcOffsetBelow, *dcFrom))
            d.refuse (t, "dcOffsetBelow", Refusal::Mismatch);
    });
    in.table ("comfort", Need::Required, [&] (Reader& t)
    {
        HpfComfort& c = o.comfort;
        const bool wl = t.required ("warningLowHz", c.warningLowHz, travel);
        const bool l = t.required ("lowHz", c.lowHz, travel);
        const bool h = t.required ("highHz", c.highHz, travel);
        const bool wh = t.required ("warningHighHz", c.warningHighHz, travel);
        d.notAbove (t, wl && l, c.warningLowHz, c.lowHz, "lowHz");
        d.below (t, l && h, c.lowHz, c.highHz, "highHz");
        d.notAbove (t, h && wh, c.highHz, c.warningHighHz, "warningHighHz");
    });
    const bool top = in.required ("curveTopDb", o.curveTopDb, R { -60.0, 24.0 });
    const bool bottom = in.required ("curveBottomDb", o.curveBottomDb, R { -120.0, 0.0 });
    d.below (in, top && bottom, o.curveBottomDb, o.curveTopDb, "curveTopDb");
    in.required ("curveStepDb", o.curveStepDb, R { 0.5, 24.0 });
    in.required ("curveHeadroomDb", o.curveHeadroomDb, R { 0.0, 24.0 });
    std::vector<std::string> keys;
    in.tables ("marks", Need::Required, [&] (Reader& t)
    {
        HpfMark m;
        if (t.required ("key", m.key))
        {
            if (contains (keys, m.key)) d.refuse (t, "key", Refusal::Duplicate);
            keys.push_back (m.key);
        }
        t.required ("hz", m.hz, R { 10.0, 200.0 });
        o.marks.push_back (std::move (m));
    });
}

void readMonoBass (Doc& d, Reader& in, MonoBass& o)
{
    const R width = readDomain (d, in, "lowWidthDomain", o.lowWidthDomain, share());
    d.pair (in, "lowWidthRange", o.lowWidthRange, width);
    in.required ("lowWidthStep", o.lowWidthStep, R { 0.001, 1.0 });
    in.required ("lowWidth", o.lowWidth, width);
    const R frequency = readDomain (d, in, "frequencyDomain", o.frequencyDomain, R { 60.0, 300.0 });
    const bool range = d.pair (in, "frequencyRange", o.frequencyRange, frequency);
    in.required ("frequencyStep", o.frequencyStep, R { 0.1, 50.0 });
    const R travel = range ? R { o.frequencyRange.min, o.frequencyRange.max } : R { 20.0, 1000.0 };
    in.table ("zones", Need::Required, [&] (Reader& z)
    {
        auto zone = [&] (std::string_view key, Zone& out)
        {
            z.table (key, Need::Required, [&] (Reader& t)
            {
                const bool from = t.required ("fromHz", out.fromHz, travel);
                const bool to = t.required ("toHz", out.toHz, travel);
                d.below (t, from && to, out.fromHz, out.toHz, "toHz");
            });
        };
        zone ("club", o.clubZone);
        zone ("vinyl", o.vinylZone);
    });
}

void readCompressor (Doc& d, Reader& in, Compressor& o)
{
    d.name (in, "detector", o.detector, kDetectors);
    in.required ("rmsWindowMs", o.rmsWindowMs, R { 0.1, 1000.0 });
    d.name (in, "link", o.link, kLinks);
    d.name (in, "mode", o.mode, kModes);
    in.required ("rangeDb", o.rangeDb, R { 0.0, 120.0 });
    in.required ("makeupDb", o.makeupDb, R { -24.0, 24.0 });
    in.required ("autoMakeup", o.autoMakeup);
    in.required ("mix", o.mix, share());
    d.name (in, "thresholdFrom", o.thresholdFrom, kThresholdFrom);
    in.required ("roundToMs", o.roundToMs, R { 0.001, 100.0 });
    in.required ("roundToDb", o.roundToDb, R { 0.001, 10.0 });
    in.table ("limits", Need::Required, [&] (Reader& t)
    {
        d.minMax (t, "threshOffset", o.limitThreshOffset, R { -60.0, 60.0 });
        d.minMax (t, "attack", o.limitAttack, R { 0.01, 1000.0 });
        d.minMax (t, "release", o.limitRelease, R { 8000.0 / kSourceRates[0], std::numeric_limits<double>::max() });
        d.minMax (t, "knee", o.limitKnee, R { 0.0, 48.0 });
    });
    in.table ("tempo", Need::Required, [&] (Reader& t)
    {
        t.required ("bpmWhenUnsure", o.tempoBpmWhenUnsure, R { 20.0, 400.0 });
        d.name (t, "trustedConfidence", o.tempoTrustedConfidence, kConfidence);
    });
}

// `targets`: the row keys of the targets document, or null when it did not parse (nothing to check a name against).
void readGlue (Doc& d, Reader& in, Glue& o, const std::vector<std::string>* targets)
{
    // THE KNOB, "up to N dB", first: every glue number of the document is written on it.
    const R knob = readDomain (d, in, "domain", o.domain, R { 0.0, 6.0 });
    const bool lo = in.required ("knobMinDb", o.knobMinDb, knob);
    const bool hi = in.required ("knobMaxDb", o.knobMaxDb, knob);
    d.below (in, lo && hi, o.knobMinDb, o.knobMaxDb, "knobMaxDb");
    in.required ("knobStepDb", o.knobStepDb, R { 0.01, 3.0 });
    in.required ("default", o.defaultUpToDb, knob);
    in.required ("whenTicked", o.whenTickedUpToDb, knob);
    // THE TRAVEL, 0…1 in steps of `step`: the compressor's internal mapping.
    in.required ("step", o.step, R { 0.0001, 1.0 });
    in.table ("byTarget", Need::Required, [&] (Reader& t)
    {
        for (const auto& e : t.data().entries())
        {
            GlueAtTarget g { e.key, 0.0 };
            if (! t.required (e.key, g.upToDb, knob)) continue;
            if (targets != nullptr && ! contains (*targets, e.key)) d.refuse (t, e.key, Refusal::NotATarget);
            else o.byTarget.push_back (std::move (g));
        }
    });
    // A ramp's law must fit its field and its ends: byDepth is the ratio's alone — it is defined through 1 − 1/ratio — and
    // needs both ends at 1 or above; geometric moves by a constant ratio between neighbours, so both ends lie above zero.
    auto ramp = [&] (std::string_view key, GlueRamp& out, const R& domain, bool byDepthAllowed)
    {
        in.table (key, Need::Required, [&] (Reader& t)
        {
            const bool from = t.required ("from", out.from, domain);
            const bool to = t.required ("to", out.to, domain);
            if (! d.name (t, "law", out.law, kLaws) || ! from || ! to) return;
            const bool fits = out.law == Law::ByDepth ? byDepthAllowed && out.from >= 1.0 && out.to >= 1.0
                            : out.law == Law::Geometric ? out.from > 0.0 && out.to > 0.0
                            : true;
            if (! fits) d.refuse (t, "law", Refusal::OutsideLaw);
        });
    };
    ramp ("ratio", o.ratio, R { 1.0, 20.0 }, true);
    ramp ("threshOffset", o.threshOffset, R { -60.0, 60.0 }, false);
    ramp ("attack", o.attack, R { 0.01, 1000.0 }, false);
    ramp ("knee", o.knee, R { 0.0, 48.0 }, false);
    ramp ("divisor", o.divisor, R { 0.01, 1000.0 }, false);   // above zero whatever the law: the release divides by it
}

void readSaturation (Doc& d, Reader& in, Saturation& o)
{
    d.name (in, "shape", o.shape, kShapes);
    in.required ("driveStep", o.driveStep, R { 0.01, 12.0 });
    const R drive = readDomain (d, in, "driveDomain", o.driveDomain, R { 0.0, 12.0 });
    d.pair (in, "driveRange", o.driveRange, drive);
    in.required ("driveDb", o.driveDb, drive);
    in.required ("bias", o.bias, R { -0.95, 0.95 });   // the core's domain
    in.required ("mixStep", o.mixStep, R { 0.001, 1.0 });
    const R mix = readDomain (d, in, "mixDomain", o.mixDomain, R { 0.0, 1.0 });
    d.pair (in, "mixRange", o.mixRange, mix);
    in.required ("mix", o.mix, mix);
    in.required ("outputStep", o.outputStep, R { 0.01, 6.0 });
    const R output = readDomain (d, in, "outputDomain", o.outputDomain, R { -6.0, 0.0 });
    d.pair (in, "outputRange", o.outputRange, output);
    in.required ("outputDb", o.outputDb, output);
    in.required ("autoComp", o.autoComp, share());       // the core's domain
    in.required ("dcBlockHz", o.dcBlockHz, R { 0.0, 200.0 });
}

// Slider ranges lie within the domain, which also contains the implicit zero default.
void readMoveTravel (Doc& d, Reader& in, Span& normal, Span& hard, Span& domain)
{
    const R bounds = readDomain (d, in, "domain", domain, R { -6.0, 6.0 });
    if (domain.min > 0.0 || domain.max < 0.0) d.outOfRange (in, "domain");
    const bool h = d.pair (in, "hard", hard, bounds);
    d.pair (in, "normal", normal, h ? R { hard.min, hard.max } : R { -24.0, 24.0 });
}

void readTilt (Doc& d, Reader& in, Tilt& o, std::vector<std::int32_t>& bands)
{
    d.band (in, o.band, bands);
    in.required ("freqHz", o.freqHz, R { 20.0, 20000.0 });
    readMoveTravel (d, in, o.normal, o.hard, o.domain);
    in.required ("step", o.step, R { 0.01, 3.0 });
}

void readLow (Doc& d, Reader& in, Low& o, std::vector<std::int32_t>& bands)
{
    d.band (in, o.band, bands);
    in.required ("freqHz", o.freqHz, R { 20.0, 20000.0 });
    in.required ("q", o.q, R { 0.1, 10.0 });
    readMoveTravel (d, in, o.normal, o.hard, o.domain);
    in.required ("step", o.step, R { 0.01, 3.0 });
}

void readStages (Doc& d, Reader& in, Stages& o)
{
    in.required ("eq", o.eq);
    in.required ("monoBass", o.monoBass);
    in.required ("compressor", o.compressor);
    in.required ("clipper", o.clipper);
    if (in.required ("limiter", o.limiter) && ! o.limiter) d.refuse (in, "limiter", Refusal::Fixed);
    in.required ("dither", o.dither);
}

void readDither (Doc& d, Reader& in, Dither& o)
{
    const bool on = in.required ("onUpToBits", o.onUpToBits, I { 8, 32 });
    d.name (in, "shaping", o.shaping, kShapings);
    const bool shaped = in.required ("shapingUpToBits", o.shapingUpToBits, I { 8, 32 });
    // Shaping shapes the dither, so it cannot reach above the depths that are dithered at all.
    if (on && shaped && o.onUpToBits < o.shapingUpToBits) d.refuse (in, "shapingUpToBits", Refusal::OutOfOrder);
}

void readDeEsser (Doc& d, Reader& in, DeEsser& o, std::vector<std::int32_t>& bands)
{
    in.required ("offered", o.offered);
    in.required ("automatic", o.automatic);
    d.band (in, o.band, bands);
    in.required ("q", o.q, R { 0.1, 10.0 });
    in.required ("atk", o.atk, share());
    in.required ("rel", o.rel, share());
    in.required ("offsetDb", o.offsetDb, R { 0.0, 40.0 });
    in.required ("ratioSlope", o.ratioSlope, share());
    const bool lo = in.required ("minDepthDb", o.minDepthDb, R { 0.0, 24.0 });
    const bool hi = in.required ("maxDepthDb", o.maxDepthDb, R { 0.0, 24.0 });
    d.below (in, lo && hi, o.minDepthDb, o.maxDepthDb, "maxDepthDb");
    in.required ("confidenceAbove", o.confidenceAbove, share());
    const bool dlo = in.required ("minDeltaLufs", o.minDeltaLufs, R { -6.0, 6.0 });
    const bool dhi = in.required ("maxDeltaLufs", o.maxDeltaLufs, R { -6.0, 6.0 });
    d.below (in, dlo && dhi, o.minDeltaLufs, o.maxDeltaLufs, "maxDeltaLufs");
    in.required ("centreStepHz", o.centreStepHz, R { 8000.0 / kSourceRates[0], std::numeric_limits<double>::max() });
    in.required ("widthStepOct", o.widthStepOct, R { 0.01, 1.0 });
    in.required ("depthStepDb", o.depthStepDb, R { 0.01, 6.0 });
    d.pair (in, "centreHz", o.centreHz, R { 500.0, 24000.0 });
    d.pair (in, "widthOct", o.widthOct, R { 0.05, 8.0 });
    const bool depth = d.pair (in, "depthDb", o.depthDb, R { -48.0, 0.0 });
    in.required ("manualDepthDb", o.manualDepthDb, depth ? R { o.depthDb.min, o.depthDb.max } : R { -48.0, 0.0 });
    if (in.required ("witnessQuantiles", o.witnessQuantiles, share()))
    {
        if (o.witnessQuantiles.empty()) d.refuse (in, "witnessQuantiles", Refusal::NotOneOf);
        d.ascending (in, "witnessQuantiles", o.witnessQuantiles);
    }
}

// The sibilance-band bursts (analysis::StereoBandBursts): types and signs here; the block is admitted, or refused whole,
// by StereoBandBursts::storageFor — see burstsAdmit.
bool readStereoBursts (Reader& in, StereoBursts& o)
{
    const R any { -1.0e9, 1.0e9 };
    const bool read[] = { in.required ("bandLowHz", o.bandLowHz, any), in.required ("bandHighHz", o.bandHighHz, any),
                          in.required ("hopMs", o.hopMs, any), in.required ("baselineMs", o.baselineMs, any),
                          in.required ("enterDb", o.enterDb, any), in.required ("exitDb", o.exitDb, any),
                          in.required ("eventCapacity", o.eventCapacity) };
    return std::all_of (std::begin (read), std::end (read), [] (bool x) { return x; });
}

bool burstsAdmit (const StereoBursts& o)
{
    analysis::StereoBandBursts::Params p;
    p.bandLowHz = o.bandLowHz;
    p.bandHighHz = o.bandHighHz;
    p.hopMs = o.hopMs;
    p.baselineMs = o.baselineMs;
    p.enterDb = o.enterDb;
    p.exitDb = o.exitDb;
    p.maxEvents = o.eventCapacity;
    return std::all_of (std::begin (kBurstRates), std::end (kBurstRates),
                        [&] (double rate) { return analysis::StereoBandBursts::storageFor (rate, p).ok; });
}

void readKinds (Doc& d, Reader& in, Kinds& o)
{
    d.name (in, "clipping", o.clipping, kKinds);
    d.name (in, "dcOffset", o.dcOffset, kKinds);
    d.name (in, "bitsUnused", o.bitsUnused, kKinds);
    d.name (in, "dualMono", o.dualMono, kKinds);
    d.name (in, "edgeSilence", o.edgeSilence, kKinds);
    d.name (in, "hum", o.hum, kKinds);
    d.name (in, "humWandered", o.humWandered, kKinds);
    d.name (in, "spectralWall", o.spectralWall, kKinds);
    d.name (in, "loudestLowNote", o.loudestLowNote, kKinds);
    d.name (in, "sibilance", o.sibilance, kKinds);
    d.name (in, "lowestLowBand", o.lowestLowBand, kKinds);
    d.name (in, "infraLow", o.infraLow, kKinds);
    d.name (in, "wideBass", o.wideBass, kKinds);
    d.name (in, "polarity", o.polarity, kKinds);
    d.name (in, "alreadyLimited", o.alreadyLimited, kKinds);
    d.name (in, "tooQuiet", o.tooQuiet, kKinds);
    d.name (in, "tooShort", o.tooShort, kKinds);
}

void readSibilance (Doc& d, Reader& in, Sibilance& o)
{
    const bool lo = in.required ("minHops", o.minHops, I { 1, 10000 });
    const bool hi = in.required ("maxHops", o.maxHops, I { 1, 10000 });
    if (lo && hi && ! (o.minHops < o.maxHops)) d.refuse (in, "maxHops", Refusal::OutOfOrder);
    const bool sf = in.required ("fromShareOverDomeDb", o.fromShareOverDomeDb, R { -60.0, 0.0 });
    const bool sa = in.required ("fullAtShareOverDomeDb", o.fullAtShareOverDomeDb, R { -60.0, 0.0 });
    d.below (in, sf && sa, o.fromShareOverDomeDb, o.fullAtShareOverDomeDb, "fullAtShareOverDomeDb");
    in.required ("sideBelowMidDb", o.sideBelowMidDb, R { 0.0, 60.0 });
    in.required ("nearMonoShare", o.nearMonoShare, share());
    const bool pf = in.required ("fromPerMinute", o.fromPerMinute, R { 0.0, 10000.0 });
    const bool pa = in.required ("fullAtPerMinute", o.fullAtPerMinute, R { 0.0, 10000.0 });
    d.below (in, pf && pa, o.fromPerMinute, o.fullAtPerMinute, "fullAtPerMinute");
    in.required ("periodicToleranceHops", o.periodicToleranceHops, I { 0, 1000 });
    if (in.required ("periodicMultiples", o.periodicMultiples, I { 1, 64 })) d.ascending (in, "periodicMultiples", o.periodicMultiples);
    const bool qf = in.required ("periodicFrom", o.periodicFrom, share());
    const bool qa = in.required ("periodicFullAt", o.periodicFullAt, share());
    d.below (in, qf && qa, o.periodicFrom, o.periodicFullAt, "periodicFullAt");
    in.required ("blindAtDuty", o.blindAtDuty, share());
    const bool vf = in.required ("severityFromDb", o.severityFromDb, R { 0.0, 120.0 });
    const bool va = in.required ("severityFullAtDb", o.severityFullAtDb, R { 0.0, 120.0 });
    d.below (in, vf && va, o.severityFromDb, o.severityFullAtDb, "severityFullAtDb");
    in.required ("excessQuantile", o.excessQuantile, share());
    in.required ("shareQuantile", o.shareQuantile, share());
    in.required ("sideMidQuantile", o.sideMidQuantile, share());
    in.required ("loudestMoments", o.loudestMoments, I { 0, 1000 });
}

// Every weight rises from `from` to 1 at `fullAt`, and divides by their difference: from < fullAt, strictly. A weight
// written with its `fullAt` alone rises from 0, so its `fullAt` lies above zero.
void readObservations (Doc& d, Reader& in, Observations& o, std::optional<double>& dcFrom)
{
    const bool doubtful = in.required ("doubtfulBelow", o.doubtfulBelow, share());
    auto fullAtAlone = [&] (Reader& t, std::string_view key, double& out, const R& domain)
    {
        if (t.required (key, out, domain) && ! (out > 0.0)) d.outOfRange (t, key);
    };
    in.table ("clipping", Need::Required, [&] (Reader& t)
    {
        fullAtAlone (t, "fullAtShareOfProgramme", o.clippingFullAtShareOfProgramme, share());
    });
    in.table ("dcOffset", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("from", o.dcOffsetFrom, share());
        const bool a = t.required ("fullAt", o.dcOffsetFullAt, share());
        d.below (t, f && a, o.dcOffsetFrom, o.dcOffsetFullAt, "fullAt");
        if (f) dcFrom = o.dcOffsetFrom;
    });
    in.table ("bitsUnused", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("fromBits", o.bitsUnusedFromBits, I { 0, 32 });
        const bool a = t.required ("fullAtBits", o.bitsUnusedFullAtBits, I { 0, 32 });
        if (f && a && ! (o.bitsUnusedFromBits < o.bitsUnusedFullAtBits)) d.refuse (t, "fullAtBits", Refusal::OutOfOrder);
    });
    in.table ("edgeSilence", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("fromSeconds", o.edgeSilenceFromSeconds, R { 0.0, 600.0 });
        const bool a = t.required ("fullAtSeconds", o.edgeSilenceFullAtSeconds, R { 0.0, 600.0 });
        d.below (t, f && a, o.edgeSilenceFromSeconds, o.edgeSilenceFullAtSeconds, "fullAtSeconds");
    });
    in.table ("hum", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("fromProminenceDb", o.humFromProminenceDb, R { 0.0, 120.0 });
        const bool a = t.required ("fullAtProminenceDb", o.humFullAtProminenceDb, R { 0.0, 120.0 });
        d.below (t, f && a, o.humFromProminenceDb, o.humFullAtProminenceDb, "fullAtProminenceDb");
        fullAtAlone (t, "fullAtPowerAgainstProgramme", o.humFullAtPowerAgainstProgramme, share());
    });
    in.table ("humWandered", Need::Required, [&] (Reader& t)
    {
        // Always doubtful: strictly below doubtfulBelow.
        if (t.required ("confidenceCeiling", o.humWanderedConfidenceCeiling, share()) && doubtful
            && ! (o.humWanderedConfidenceCeiling < o.doubtfulBelow))
            d.refuse (t, "confidenceCeiling", Refusal::OutOfOrder);
    });
    in.table ("spectralWall", Need::Required, [&] (Reader& t)
    {
        // Its confidence rises from the core's own minDropDb, so its full weight lies above that.
        if (t.required ("fullAtDropDb", o.spectralWallFullAtDropDb, R { 0.0, 200.0 }) && ! (o.spectralWallFullAtDropDb > kCoreMinDropDb))
            d.outOfRange (t, "fullAtDropDb");
        const bool f = t.required ("fromFractionBelowNyquist", o.spectralWallFromFractionBelowNyquist, share());
        const bool a = t.required ("fullAtFractionBelowNyquist", o.spectralWallFullAtFractionBelowNyquist, share());
        d.below (t, f && a, o.spectralWallFromFractionBelowNyquist, o.spectralWallFullAtFractionBelowNyquist,
                 "fullAtFractionBelowNyquist");
    });
    in.table ("infraLow", Need::Required, [&] (Reader& t)
    {
        const bool l = t.required ("low", o.infraLowLow, share());
        const bool h = t.required ("high", o.infraLowHigh, share());
        d.below (t, l && h, o.infraLowLow, o.infraLowHigh, "high");
    });
    in.table ("wideBass", Need::Required, [&] (Reader& t)
    {
        fullAtAlone (t, "sideFractionAtLeast", o.wideBassSideFractionAtLeast, share());   // at 0 every file has wide bass
    });
    in.table ("polarity", Need::Required, [&] (Reader& t)
    {
        t.required ("correlationBelow", o.polarityCorrelationBelow, R { -1.0, 1.0 });
        t.required ("rawSideFractionAbove", o.polarityRawSideFractionAbove, share());
    });
    in.table ("alreadyLimited", Need::Required, [&] (Reader& t)
    {
        t.required ("plrBelowDb", o.alreadyLimitedPlrBelowDb, R { 0.0, 40.0 });
    });
    in.table ("kinds", Need::Required, [&] (Reader& t) { readKinds (d, t, o.kinds); });
    in.table ("sibilance", Need::Required, [&] (Reader& t) { readSibilance (d, t, o.sibilance); });
}

// The crest per band (analysis::BandCrest): three corners — the shape of its parameters — and a hop of whole 10 ms
// sub-hops, which the analyzer rounds to without refusing (15 would run as 20 and read back as 15); everything else it
// admits, or refuses, itself — see crestAdmits.
bool readCrest (Doc& d, Reader& in, Crest& o)
{
    bool corners = false;
    if (in.required ("bandEdgesHz", o.bandEdgesHz, R { -1.0e9, 1.0e9 }))
    {
        corners = o.bandEdgesHz.size() == 3;
        if (! corners) d.refuse (in, "bandEdgesHz", Refusal::NotOneOf);
    }
    const bool hop = in.required ("hopMs", o.hopMs, R { -1.0e9, 1.0e9 });
    if (hop) d.onStep (in, "hopMs", Grid { toml::Decimal { 0, 1, false }, toml::Decimal { 100, 1, false } });
    const bool block = in.required ("blockHops", o.blockHops);
    const bool shareFloor = in.required ("bandShareFloorDb", o.bandShareFloorDb, R { -1.0e9, 1.0e9 });
    const bool programmeFloor = in.required ("floorDb", o.floorDb, R { -1.0e9, 1.0e9 });
    in.required ("belowMeanSquareDb", o.belowMeanSquareDb, R { 0.0, 200.0 });
    in.required ("noProgrammeDb", o.noProgrammeDb, R { -1000.0, 0.0 });
    return corners && hop && block && shareFloor && programmeFloor;
}

bool crestAdmits (const Crest& o)
{
    analysis::BandCrestParams p;
    for (std::size_t i = 0; i < 3; ++i) p.bandEdgeHz[i] = o.bandEdgesHz[i];
    p.hopMs = o.hopMs;
    p.blockHops = o.blockHops;
    p.programmeFloorDb = o.floorDb;
    p.bandShareFloorDb = o.bandShareFloorDb;
    return std::all_of (std::begin (kSourceRates), std::end (kSourceRates), [&] (double rate)
    {
        const long long minute = (long long) (60.0 * rate);   // a length to size by; the domain does not depend on it
        return analysis::BandCrest::storageFor (rate, kChannels, minute, p).ok;
    });
}

void readCost (Doc& d, Reader& in, Cost& o)
{
    in.required ("activityBelowIntegratedLu", o.activityBelowIntegratedLu, R { 0.0, 120.0 });
    in.required ("activityFloorLufs", o.activityFloorLufs, anyLufs());
    in.required ("limiterActiveInputDb", o.limiterActiveInputDb, R { -200.0, 0.0 });
    in.required ("firstBlockSeconds", o.firstBlockSeconds, R { 0.1, 60.0 });
    in.required ("blockSeconds", o.blockSeconds, R { 0.01, 10.0 });
    in.required ("histogramStepDb", o.histogramStepDb, R { 0.001, 10.0 });
    in.required ("absoluteGateLufs", o.absoluteGateLufs, anyLufs());
    in.required ("relativeGateLu", o.relativeGateLu, R { -60.0, 0.0 });
    in.required ("tooQuietBelowLufs", o.tooQuietBelowLufs, anyLufs());
    in.required ("grWindowSeconds", o.grWindowSeconds, R { 0.0001, 1.0 });
    const bool lo = in.required ("grTraceBucketsMin", o.grTraceBucketsMin, I { 1, 1 << 20 });
    const bool hi = in.required ("grTraceBucketsMax", o.grTraceBucketsMax, I { 1, 1 << 20 });
    if (lo && hi && o.grTraceBucketsMax < o.grTraceBucketsMin) d.refuse (in, "grTraceBucketsMax", Refusal::OutOfOrder);
    // The quantiles are in the fields' names (p50Db, p95Db): the list is a claim about them, and nothing else is admitted.
    if (in.required ("printedQuantiles", o.printedQuantiles, share())
        && ! (o.printedQuantiles.size() == 2 && same (o.printedQuantiles[0], 0.5) && same (o.printedQuantiles[1], 0.95)))
        d.refuse (in, "printedQuantiles", Refusal::Fixed);
    in.table ("pumping", Need::Required, [&] (Reader& t)
    {
        const bool hp = t.required ("highPassHz", o.pumping.highPassHz, R { 0.01, 1000.0 });
        const bool lp = t.required ("lowPassHz", o.pumping.lowPassHz, R { 0.01, 1000.0 });
        d.below (t, hp && lp, o.pumping.highPassHz, o.pumping.lowPassHz, "lowPassHz");
        t.required ("settleSeconds", o.pumping.settleSeconds, R { 0.0, 60.0 });
        // The trace is sampled at no less than minTraceRateHz, so the filter on it must stay below half that.
        const bool rate = t.required ("minTraceRateHz", o.pumping.minTraceRateHz, R { 1.0, 10000.0 });
        if (lp && rate && ! (o.pumping.lowPassHz < 0.5 * o.pumping.minTraceRateHz)) d.refuse (t, "lowPassHz", Refusal::AboveNyquist);
    });
    in.table ("sections", Need::Required, [&] (Reader& t)
    {
        Sections& s = o.sections;
        t.required ("changeLu", s.changeLu, R { 0.1, 40.0 });
        t.required ("minSeconds", s.minSeconds, R { 0.0, 600.0 });
        t.required ("masterShorterByAtMost", s.masterShorterByAtMost, share());
        t.required ("minComparedShare", s.minComparedShare, share());
        t.required ("worstNamedAbove", s.worstNamedAbove, I { 0, 1000 });
        t.required ("quantile", s.quantile, share());
    });
}

void readProgress (Doc& d, Reader& in, Progress& o)
{
    in.table ("analysis", Need::Required, [&] (Reader& t)
    {
        t.required ("referenceSeconds", o.analysisReferenceSeconds, R { 1.0, 36000.0 });
        t.required ("estimateFromSeconds", o.analysisEstimateFromSeconds, R { 0.0, 3600.0 });
        t.table ("weights", Need::Required, [&] (Reader& w)
        {
            const R ms { 0.0, 1.0e7 };
            AnalysisWeights& a = o.analysisWeights;
            w.required ("loudness", a.loudness, ms);
            w.required ("report", a.report, ms);
            w.required ("lowEnd120", a.lowEnd120, ms);
            w.required ("forensics", a.forensics, ms);
            w.required ("stereo", a.stereo, ms);
            w.required ("lowEndSweep", a.lowEndSweep, ms);
            w.required ("stereoBursts", a.stereoBursts, ms);
            w.required ("crest", a.crest, ms);
            w.required ("hum", a.hum, ms);
            w.required ("tempo", a.tempo, ms);
            if (! (a.loudness + a.report + a.lowEnd120 + a.forensics + a.stereo + a.lowEndSweep
                   + a.stereoBursts + a.crest + a.hum + a.tempo > 0)) d.outOfRange (t, "weights");
        });
    });
    in.table ("master", Need::Required, [&] (Reader& t)
    {
        t.required ("passWeight", o.masterPassWeight, R { 0.0, 1000.0 });
        t.required ("measureWeight", o.masterMeasureWeight, R { 0.0, 1000.0 });
        t.required ("expectedPasses", o.masterExpectedPasses, I { 1, 1000 });
        if (! (o.masterPassWeight * o.masterExpectedPasses + o.masterMeasureWeight > 0)) d.outOfRange (t, "passWeight");
    });
}

void readBlindTest (Doc& d, Reader& in, BlindTest& o, const std::vector<std::string>* targets)
{
    if (in.required ("targets", o.targets))
    {
        if (targets != nullptr)
            for (std::size_t i = 0; i < o.targets.size(); ++i)
                if (! contains (*targets, o.targets[i])) d.refuseItem (in, "targets", i, Refusal::NotATarget);
        d.unique (in, "targets", o.targets);
    }
    in.required ("fragmentSeconds", o.fragmentSeconds, R { 1.0, 600.0 });
    in.required ("repeats", o.repeats, I { 1, 100 });
    in.required ("matchToleranceLu", o.matchToleranceLu, R { 0.001, 3.0 });
    in.table ("listened", Need::Required, [&] (Reader& t) { t.required ("minSwitches", o.listenedMinSwitches, I { 0, 100 }); });
}

void readEngine (Doc& d, Reader& in, Engine& o, const std::vector<std::string>* targets)
{
    std::vector<std::int32_t> bands;   // the core's EQ bands the devices have taken, in reading order
    std::optional<double> dcFrom;      // observations.dcOffset.from, once read
    in.required ("defaults", o.defaults);
    in.table ("input", Need::Required, [&] (Reader& t) { readInput (d, t, o.input); });
    in.table ("landing", Need::Required, [&] (Reader& t) { readLanding (d, t, o.landing); });
    in.table ("limiter", Need::Required, [&] (Reader& t) { readLimiter (d, t, o.limiter); });
    in.table ("lowEnd", Need::Required, [&] (Reader& t) { readLowEnd (d, t, o.lowEnd); });
    in.table ("observations", Need::Required, [&] (Reader& t) { readObservations (d, t, o.observations, dcFrom); });
    in.table ("hpf", Need::Required, [&] (Reader& t)
    {
        readHpf (d, t, o.hpf, bands, dcFrom);
    });
    in.table ("monoBass", Need::Required, [&] (Reader& t)
    {
        readMonoBass (d, t, o.monoBass);
    });
    in.table ("compressor", Need::Required, [&] (Reader& t) { readCompressor (d, t, o.compressor); });
    in.table ("glue", Need::Required, [&] (Reader& t) { readGlue (d, t, o.glue, targets); });
    in.table ("saturation", Need::Required, [&] (Reader& t) { readSaturation (d, t, o.saturation); });
    in.table ("tilt", Need::Required, [&] (Reader& t) { readTilt (d, t, o.tilt, bands); });
    in.table ("low", Need::Required, [&] (Reader& t)
    {
        readLow (d, t, o.low, bands);
    });
    in.table ("eq", Need::Required, [&] (Reader& t)
    {
        t.table ("curve", Need::Required, [&] (Reader& c)
        {
            c.required ("warnDb", o.eq.curveWarnDb, R { 0.0, 24.0 });
            c.required ("scaleDb", o.eq.curveScaleDb, R { 0.0, 24.0 });
            c.required ("fieldDb", o.eq.curveFieldDb, R { 0.0, 24.0 });
        });
    });
    in.table ("stages", Need::Required, [&] (Reader& t) { readStages (d, t, o.stages); });
    in.table ("dither", Need::Required, [&] (Reader& t) { readDither (d, t, o.dither); });
    in.table ("deEsser", Need::Required, [&] (Reader& t) { readDeEsser (d, t, o.deEsser, bands); });
    bool bursts = false, crest = false;
    in.table ("stereoBursts", Need::Required, [&] (Reader& t) { bursts = readStereoBursts (t, o.stereoBursts); });
    if (bursts && ! burstsAdmit (o.stereoBursts)) d.refuse (in, "stereoBursts", Refusal::AnalyzerRefuses);
    in.table ("crest", Need::Required, [&] (Reader& t) { crest = readCrest (d, t, o.crest); });
    if (crest && ! crestAdmits (o.crest)) d.refuse (in, "crest", Refusal::AnalyzerRefuses);
    in.table ("cost", Need::Required, [&] (Reader& t) { readCost (d, t, o.cost); });
    in.table ("progress", Need::Required, [&] (Reader& t) { readProgress (d, t, o.progress); });
    in.table ("blindTest", Need::Required, [&] (Reader& t) { readBlindTest (d, t, o.blindTest, targets); });
}

//==============================================================================
// targets.toml

// Defaults use the accepted domains, independently of the slider hints.
struct RowDomains
{
    R lufs {};
    R tp { -6.0, -0.1 };
    R monoBass { 60.0, 300.0 };
    R hpfFloor = hpfDomain();
    R low { -6.0, 6.0 };
};

void readEdit (Doc& d, Reader& in, std::string_view key, Edit& o, R& domain)
{
    in.table (key, Need::Required, [&] (Reader& t)
    {
        if (key == "lufs")
        {
            std::string kind;
            if (t.required ("domain", kind) && kind != "finite") d.refuse (t, "domain", Refusal::Fixed);
        }
        else domain = readDomain (d, t, "domain", o.domain, R { -6.0, -0.1 });
        const bool from = t.required ("from", o.from, domain);
        const bool to = t.required ("to", o.to, domain);
        d.below (t, from && to, o.from, o.to, "to");
        d.pair (t, "green", o.green, from && to && o.from < o.to ? R { o.from, o.to } : domain);
        t.required ("step", o.step, R { 0.001, 1.0 });
    });
}

void readTarget (Doc& d, Reader& row, Target& x, const RowDomains& b)
{
    d.name (row, "group", x.group, kGroups);
    row.required ("lufs", x.lufs, b.lufs);
    row.required ("tp", x.tp, b.tp);
    row.required ("monoBass", x.monoBass, b.monoBass);
    // A target floor must work even at the lowest supported source rate.
    row.required ("hpfFloor", x.hpfFloor, b.hpfFloor);
    if (row.required ("hpfSlopeDbPerOct", x.hpfSlopeDbPerOct, I { 6, 96 }) && x.hpfSlopeDbPerOct % 6 != 0)
        d.refuse (row, "hpfSlopeDbPerOct", Refusal::NotOneOf);
    row.required ("noteLossDb", x.noteLossDb, R { 0.1, 3.0 });
    // 0 (the source's rate) or a physical delivery rate; which rates the targets deliver at is a decision, pinned apart.
    if (row.required ("sampleRate", x.sampleRate, I { 0, kHighestDeliveryRate }) && x.sampleRate != 0
        && x.sampleRate < kLowestDeliveryRate)
        d.outOfRange (row, "sampleRate");
    if (row.required ("bitDepth", x.bitDepth, I { 16, 24 }) && x.bitDepth != 16 && x.bitDepth != 24)
        d.refuse (row, "bitDepth", Refusal::NotOneOf);
    d.flag (row, "noClipper", x.noClipper);
    d.flag (row, "hpfAlways", x.hpfAlways);
    d.flag (row, "sourceRatePass", x.sourceRatePass);
    // A pass at the source's rate exists only where the delivery rate is another.
    if (x.sourceRatePass && x.sampleRate == 0 && row.data().find ("sampleRate") != nullptr)
        d.refuse (row, "sourceRatePass", Refusal::NotApplicable);
    if (row.data().find ("lowDb") != nullptr)
    {
        double db = 0.0;
        if (row.required ("lowDb", db, b.low))
        {
            x.lowDb = db;
        }
    }
    if (row.data().find ("album") != nullptr)
        row.table ("album", Need::Required, [&] (Reader& a)
        {
            Album album;
            const bool lufs = a.required ("lufs", album.lufs, b.lufs);
            const bool desktopOnly = a.required ("desktopOnly", album.desktopOnly);
            if (lufs && desktopOnly) x.album = album;
        });
}

void readTargets (Doc& d, Reader& in, Targets& o, const Engine& e)
{
    RowDomains b;
    in.table ("edit", Need::Required, [&] (Reader& t)
    {
        readEdit (d, t, "lufs", o.editLufs, b.lufs);
        readEdit (d, t, "tp", o.editTp, b.tp);
    });
    if (e.monoBass.frequencyDomain.min < e.monoBass.frequencyDomain.max)
        b.monoBass = R { e.monoBass.frequencyDomain.min, e.monoBass.frequencyDomain.max };
    if (e.low.domain.min < e.low.domain.max) b.low = R { e.low.domain.min, e.low.domain.max };

    in.table ("targets", Need::Required, [&] (Reader& t)
    {
        for (const auto& entry : t.data().entries())
        {
            // A person picks a target, and a project names one, by its key: an empty key names nothing.
            if (entry.key.empty()) d.refuse (t, entry.key, Refusal::EmptyKey);
            t.table (entry.key, Need::Required, [&] (Reader& row)
            {
                Target x;
                x.key = entry.key;
                readTarget (d, row, x, b);
                o.targets.push_back (std::move (x));
            });
        }
    });
    if (in.required ("default", o.defaultTarget) && o.find (o.defaultTarget) == nullptr)
        d.refuse (in, "default", Refusal::NotATarget);
    if (in.required ("main", o.main))
    {
        for (std::size_t i = 0; i < o.main.size(); ++i)
            if (o.find (o.main[i]) == nullptr) d.refuseItem (in, "main", i, Refusal::NotATarget);
        d.unique (in, "main", o.main);
    }
}

//==============================================================================

void collect (const toml::Report& report, Document document, std::vector<Problem>& out)
{
    for (const toml::Problem& p : report.problems)
    {
        Problem q;
        q.document = document;
        q.line = p.position.line;
        q.column = p.position.column;
        q.path = p.path;
        switch (p.fault)
        {
            case toml::Fault::Missing:    q.fault = Fault::Missing; break;
            case toml::Fault::WrongType:  q.fault = Fault::WrongType; break;
            case toml::Fault::OutOfRange: q.fault = Fault::OutOfRange; break;
            case toml::Fault::UnknownKey: q.fault = Fault::UnknownKey; break;
            case toml::Fault::Refused:
                q.fault = Fault::Refused;
                if (p.detail > std::uint32_t (Refusal::EmptyKey)) felitronics::session::detail::storageOverflow();
                q.refusal = static_cast<Refusal> (p.detail);
                break;
        }
        q.code = q.fault == Fault::Refused ? Problem::name (q.refusal) : Problem::name (q.fault);
        out.push_back (std::move (q));
    }
}

Problem syntax (const toml::Error& e, Document document)
{
    Problem q;
    q.document = document;
    q.fault = Fault::Syntax;
    q.line = e.line;
    q.column = e.column;
    q.code = toml::codeName (e.code);
    return q;
}
} // namespace

namespace detail
{
Loaded bindTables (const toml::Table* targetsDocument, const toml::Table* engineDocument)
{
    Loaded out;
    std::optional<std::vector<std::string>> targets;
    if (targetsDocument != nullptr) targets = rowKeys (*targetsDocument);
    if (engineDocument != nullptr)
    {
        Doc d;
        Reader reader (*engineDocument, d.report);
        readEngine (d, reader, out.config.engine, targets ? &*targets : nullptr);
        reader.finish();
        collect (d.report, Document::Engine, out.problems);
    }
    if (targetsDocument != nullptr)
    {
        Doc d;
        Reader reader (*targetsDocument, d.report);
        readTargets (d, reader, out.config.targets, out.config.engine);
        reader.finish();
        collect (d.report, Document::Targets, out.problems);
    }
    return out;
}
} // namespace detail

const Target* Targets::find (std::string_view key) const noexcept
{
    for (const Target& t : targets)
        if (t.key == key) return &t;
    return nullptr;
}

const char* Problem::name (Document document) noexcept
{
    switch (document)
    {
        case Document::Targets: return "targets";
        case Document::Engine:  return "engine";
    }
    return "unknown";
}

const char* Problem::name (Fault fault) noexcept
{
    switch (fault)
    {
        case Fault::Syntax:     return "Syntax";
        case Fault::Missing:    return "Missing";
        case Fault::WrongType:  return "WrongType";
        case Fault::OutOfRange: return "OutOfRange";
        case Fault::UnknownKey: return "UnknownKey";
        case Fault::Refused:    return "Refused";
    }
    return "Unknown";
}

const char* Problem::name (Refusal refusal) noexcept
{
    switch (refusal)
    {
        case Refusal::None:           return "Refused";
        case Refusal::NotOneOf:       return "NotOneOf";
        case Refusal::NotATarget:     return "NotATarget";
        case Refusal::NotAPair:       return "NotAPair";
        case Refusal::OutOfOrder:     return "OutOfOrder";
        case Refusal::Duplicate:      return "Duplicate";
        case Refusal::Fixed:          return "Fixed";
        case Refusal::NotOnStep:      return "NotOnStep";
        case Refusal::Mismatch:       return "Mismatch";
        case Refusal::WrittenDefault: return "WrittenDefault";
        case Refusal::NotApplicable:  return "NotApplicable";
        case Refusal::OutsideLaw:     return "OutsideLaw";
        case Refusal::AboveNyquist:   return "AboveNyquist";
        case Refusal::AnalyzerRefuses: return "AnalyzerRefuses";
        case Refusal::EmptyKey:       return "EmptyKey";
    }
    return "Refused";
}

bool Loaded::ok() const noexcept
{
    return problems.empty();
}

Loaded Config::bind (std::string_view targetsToml, std::string_view engineToml)
{
    const toml::ParseResult targets = toml::parse (targetsToml, detail::kTargetsSource);
    const toml::ParseResult engine = toml::parse (engineToml, detail::kEngineSource);
    const auto* t = std::get_if<toml::Table> (&targets);
    const auto* e = std::get_if<toml::Table> (&engine);
    Loaded out = detail::bindTables (t, e);
    // A document that did not parse is one problem; it comes where that document's problems would have.
    if (const auto* error = std::get_if<toml::Error> (&engine))
        out.problems.insert (out.problems.begin(), syntax (*error, Document::Engine));
    if (const auto* error = std::get_if<toml::Error> (&targets)) out.problems.push_back (syntax (*error, Document::Targets));
    return out;
}

std::optional<Versions> Config::versionsOf (std::string_view targetsToml, std::string_view engineToml)
{
    const toml::ParseResult targets = toml::parse (targetsToml);
    const toml::ParseResult engine = toml::parse (engineToml);
    const auto* t = std::get_if<toml::Table> (&targets);
    const auto* e = std::get_if<toml::Table> (&engine);
    if (t == nullptr || e == nullptr) return std::nullopt;
    return Versions { detail::version (*t, *e, false), detail::version (*t, *e, true) };
}

} // namespace felitronics::session::config
