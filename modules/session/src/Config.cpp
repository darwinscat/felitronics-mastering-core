// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE CONFIG, READ BY SCHEMA (Config.h). The two documents arrive as constexpr data — modules/session/CMakeLists.txt
// embeds modules/session/config/*.toml with felitronics_toml_embed — and are read into the typed structs with
// felitronics-toml's Reader: every key with its type and its range, every key nobody read reported as unknown.
//
// THE RANGES BELOW ARE DOMAINS, NOT SETTINGS: where a number stops meaning what its document says (a share outside 0…1,
// a cutoff outside the knob's travel, a list that must ascend and does not). The settings are the documents'. A range
// that another key states — a target's loudness on the edit travel, a default on its knob's travel — is taken from that
// key once it has been read; until then the key's own physical domain stands in, so one wrong number is one problem.
//
// ORDER: the engine first, then the targets, whose rows are checked against the engine's travels (the mono-bass knob,
// the high-pass knob and its slopes, the low shelf). The engine's own references to targets (the glue per target, the
// blind test's series) are checked against the row keys of the targets document, read before either is bound.

#include <felitronics/session/Config.h>

#include <felitronics/toml/Embedded.h>
#include <felitronics/toml/Schema.h>
#include <felitronics/toml/Toml.h>

#include "ConfigVersion.h"
#include "embedded/engine.h"    // generated at build time from modules/session/config/engine.toml
#include "embedded/targets.h"   // ... and from modules/session/config/targets.toml

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

// The source numbers the documents' positions carry.
constexpr std::uint32_t kTargetsSource = 1;
constexpr std::uint32_t kEngineSource = 2;

// Shared domains. Functions, not namespace-scope objects: a toml::Range is not a literal type, and a const object of it
// would be initialised at load time.
R share() { return { 0.0, 1.0 }; }
R anyLufs() { return { -120.0, 0.0 }; }
I eqBand() { return { 0, 23 }; }   // the core's EQ stage has 24 bands (FC_MAX_EQ_BANDS)

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

// ONE DOCUMENT BEING READ: its reader's report, and the checks across keys that report into it.
struct Doc
{
    toml::Report report;

    void refuse (Reader& in, std::string_view key, Refusal why) { in.refuse (key, std::uint32_t (why)); }

    // Item i of the array at `key`: the problem points at the item.
    void refuseItem (Reader& in, std::string_view key, std::size_t i, Refusal why)
    {
        toml::Position at = in.data().position;
        if (const toml::Value* v = in.data().find (key))
            if (const auto* a = std::get_if<toml::Array> (&v->data); a != nullptr && i < a->size()) at = (*a)[i].position;
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

    // [min, max]: a two-item array within `domain`, min <= max.
    bool pair (Reader& in, std::string_view key, Span& out, const R& domain)
    {
        std::vector<double> v;
        if (! in.required (key, v, domain)) return false;
        if (v.size() != 2) { refuse (in, key, Refusal::NotAPair); return false; }
        if (v[0] > v[1]) { refuse (in, key, Refusal::OutOfOrder); return false; }
        out = { v[0], v[1] };
        return true;
    }

    // { min = …, max = … } within `domain`, min <= max.
    bool minMax (Reader& in, std::string_view key, Span& out, const R& domain)
    {
        bool good = false;
        in.table (key, Need::Required, [&] (Reader& t)
        {
            Span s;
            const bool lo = t.required ("min", s.min, domain);
            const bool hi = t.required ("max", s.max, domain);
            if (lo && hi && s.min > s.max) refuse (t, "max", Refusal::OutOfOrder);
            else if (lo && hi) { out = s; good = true; }
        });
        return good;
    }

    // Two keys of one table where the first must not exceed the second: the second is refused.
    void ordered (Reader& in, bool bothRead, double first, double second, std::string_view secondKey)
    {
        if (bothRead && first > second) refuse (in, secondKey, Refusal::OutOfOrder);
    }

    // A list of integers that must strictly ascend.
    void ascending (Reader& in, std::string_view key, const std::vector<std::int32_t>& v)
    {
        for (std::size_t i = 1; i < v.size(); ++i)
            if (v[i] <= v[i - 1]) { refuseItem (in, key, i, Refusal::OutOfOrder); return; }
    }
    void ascending (Reader& in, std::string_view key, const std::vector<double>& v)
    {
        for (std::size_t i = 1; i < v.size(); ++i)
            if (v[i] <= v[i - 1]) { refuseItem (in, key, i, Refusal::OutOfOrder); return; }
    }

    // An EQ band of the core, which no two devices may share.
    void band (Reader& in, std::int32_t& out, std::vector<std::int32_t>& taken)
    {
        if (! in.required ("band", out, eqBand())) return;
        if (std::find (taken.begin(), taken.end(), out) != taken.end()) refuse (in, "band", Refusal::Duplicate);
        else taken.push_back (out);
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
        // gain-only is the quieter of the two thresholds
        if (warning && gainOnly && o.quietGainOnlyLufs > o.quietWarningLufs) d.refuse (t, "gainOnlyLufs", Refusal::OutOfOrder);
    });
    in.required ("shortSeconds", o.shortSeconds, R { 0.0, 600.0 });
    in.required ("shortConfidence", o.shortConfidence, share());
}

void readLanding (Doc& d, Reader& in, Landing& o)
{
    if (in.required ("passes", o.passes, I { 1, 1000 }) && o.passes.empty()) d.refuse (in, "passes", Refusal::NotOneOf);
    in.required ("toleranceLu", o.toleranceLu, R { 0.001, 1.0 });
    in.required ("overshootFreeDb", o.overshootFreeDb, R { 0.0, 24.0 });
    in.required ("overshootPerDb", o.overshootPerDb, R { 0.1, 100.0 });
    in.required ("overshootCapDb", o.overshootCapDb, R { 0.0, 24.0 });
}

void readPeakClipper (Doc& d, Reader& in, PeakClipper& o)
{
    in.required ("littleNeedDb", o.littleNeedDb, R { 0.0, 24.0 });
    in.required ("shortP90Ms", o.shortP90Ms, R { 0.0, 1000.0 });
    in.required ("shortBassShare", o.shortBassShare, share());
    in.required ("shortPlrDb", o.shortPlrDb, R { 0.0, 40.0 });
    in.required ("longP90Ms", o.longP90Ms, R { 0.0, 1000.0 });
    in.required ("longBassShare", o.longBassShare, share());
    in.required ("longPlrDb", o.longPlrDb, R { 0.0, 40.0 });
    in.required ("shortOverDb", o.shortOverDb, R { 0.0, 12.0 });
    in.required ("betweenOverDb", o.betweenOverDb, R { 0.0, 12.0 });
    in.required ("bassBelowHz", o.bassBelowHz, R { 20.0, 2000.0 });
    in.required ("densityMinusDb", o.densityMinusDb, R { 0.0, 24.0 });
    in.required ("densityWithinDb", o.densityWithinDb, R { 0.0, 6.0 });
    in.required ("kneeDb", o.kneeDb, R { 0.0, 1.0 });
    const bool lo = in.required ("manualMinDb", o.manualMinDb, R { 0.0, 12.0 });
    const bool hi = in.required ("manualMaxDb", o.manualMaxDb, R { 0.0, 12.0 });
    d.ordered (in, lo && hi, o.manualMinDb, o.manualMaxDb, "manualMaxDb");
    in.required ("manualStepDb", o.manualStepDb, R { 0.01, 3.0 });
    in.required ("manualDefaultDb", o.manualDefaultDb, lo && hi ? R { o.manualMinDb, o.manualMaxDb } : R { 0.0, 12.0 });
}

void readLimiter (Doc& d, Reader& in, Limiter& o)
{
    in.required ("ceilingMarginDb", o.ceilingMarginDb, R { 0.0, 3.0 });
    in.required ("releaseMs", o.releaseMs, R { 1.0, 2000.0 });
    in.table ("peakClipper", Need::Required, [&] (Reader& t) { readPeakClipper (d, t, o.peakClipper); });
}

void readLowEnd (Doc& d, Reader& in, LowEnd& o)
{
    in.required ("occupiedFromDuty", o.occupiedFromDuty, share());
    in.required ("occupiedMarginWhenOnDb", o.occupiedMarginWhenOnDb, R { 0.0, 40.0 });
    in.required ("infraLowCrossoverHz", o.infraLowCrossoverHz, R { 10.0, 120.0 });
    in.table ("run", Need::Required, [&] (Reader& t)
    {
        LowEndRun& r = o.run;
        t.required ("crossoverHz", r.crossoverHz, R { 20.0, 500.0 });
        const bool lo = t.required ("lowNoteHz", r.lowNoteHz, R { 10.0, 1000.0 });
        const bool hi = t.required ("highNoteHz", r.highNoteHz, R { 10.0, 1000.0 });
        d.ordered (t, lo && hi, r.lowNoteHz, r.highNoteHz, "highNoteHz");
        t.required ("fftOrder", r.fftOrder, I { 10, 20 });
        t.required ("dutyThresholdDb", r.dutyThresholdDb, R { 1.0, 120.0 });   // the core takes −5 silently; we do not
        t.required ("skipBlocks", r.skipBlocks, I { 0, 1000 });                  // ... nor −1
    });
}

void readHpf (Doc& d, Reader& in, Hpf& o, std::vector<std::int32_t>& bands)
{
    d.band (in, o.band, bands);
    const bool lo = in.required ("hzMin", o.hzMin, R { 1.0, 200.0 });
    const bool hi = in.required ("hzMax", o.hzMax, R { 1.0, 200.0 });
    d.ordered (in, lo && hi, o.hzMin, o.hzMax, "hzMax");
    const R travel = lo && hi && o.hzMin <= o.hzMax ? R { o.hzMin, o.hzMax } : R { 1.0, 200.0 };
    d.pair (in, "hzNormal", o.hzNormal, travel);
    in.required ("hzDefault", o.hzDefault, travel);
    if (in.required ("slopes", o.slopes, I { 6, 96 }))
    {
        if (o.slopes.empty()) d.refuse (in, "slopes", Refusal::NotOneOf);
        for (std::size_t i = 0; i < o.slopes.size(); ++i)
            if (o.slopes[i] % 6 != 0) d.refuseItem (in, "slopes", i, Refusal::NotOneOf);   // the core's 6 dB/oct grid
        d.ascending (in, "slopes", o.slopes);
    }
    if (in.required ("slopesNormal", o.slopesNormal, I { 6, 96 }))
        for (std::size_t i = 0; i < o.slopesNormal.size(); ++i)
            if (! contains (o.slopes, o.slopesNormal[i])) d.refuseItem (in, "slopesNormal", i, Refusal::NotOneOf);
    if (in.required ("slopeDefault", o.slopeDefault, I { 6, 96 }) && ! contains (o.slopes, o.slopeDefault))
        d.refuse (in, "slopeDefault", Refusal::NotOneOf);
    in.required ("nothingToCut", o.nothingToCut, share());
    in.table ("comfort", Need::Required, [&] (Reader& t)
    {
        const bool low = t.required ("lowHz", o.comfort.lowHz, travel);
        const bool high = t.required ("highHz", o.comfort.highHz, travel);
        d.ordered (t, low && high, o.comfort.lowHz, o.comfort.highHz, "highHz");
        t.required ("warningLowHz", o.comfort.warningLowHz, low ? R { travel.min, o.comfort.lowHz } : travel);
    });
    const bool top = in.required ("curveTopDb", o.curveTopDb, R { -60.0, 24.0 });
    const bool bottom = in.required ("curveBottomDb", o.curveBottomDb, R { -120.0, 0.0 });
    d.ordered (in, top && bottom, o.curveBottomDb, o.curveTopDb, "curveTopDb");
    in.required ("curveStepDb", o.curveStepDb, R { 0.5, 24.0 });
    in.required ("curveHeadroomDb", o.curveHeadroomDb, R { 0.0, 24.0 });
    in.tables ("marks", Need::Required, [&] (Reader& t)
    {
        HpfMark m;
        t.required ("key", m.key);
        t.required ("hz", m.hz, R { 10.0, 200.0 });
        o.marks.push_back (std::move (m));
    });
}

void readMonoBass (Doc& d, Reader& in, MonoBass& o)
{
    in.required ("lowWidth", o.lowWidth, share());
    const bool range = d.pair (in, "frequencyRange", o.frequencyRange, R { 20.0, 1000.0 });
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
                d.ordered (t, from && to, out.fromHz, out.toHz, "toHz");
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
        d.minMax (t, "release", o.limitRelease, R { 1.0, 5000.0 });
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
    in.required ("default", o.defaultPosition, share());
    in.required ("whenTicked", o.whenTicked, share());
    in.table ("byTarget", Need::Required, [&] (Reader& t)
    {
        for (const auto& e : t.data().entries())
        {
            GlueAtTarget g { e.key, 0.0 };
            if (! t.required (e.key, g.position, share())) continue;
            if (targets != nullptr && ! contains (*targets, e.key)) d.refuse (t, e.key, Refusal::NotATarget);
            else o.byTarget.push_back (std::move (g));
        }
    });
    in.required ("step", o.step, R { 0.0001, 1.0 });
    auto ramp = [&] (std::string_view key, GlueRamp& out, const R& domain)
    {
        in.table (key, Need::Required, [&] (Reader& t)
        {
            t.required ("from", out.from, domain);
            t.required ("to", out.to, domain);
            d.name (t, "law", out.law, kLaws);
        });
    };
    ramp ("ratio", o.ratio, R { 1.0, 20.0 });
    ramp ("threshOffset", o.threshOffset, R { -60.0, 60.0 });
    ramp ("attack", o.attack, R { 0.01, 1000.0 });
    ramp ("knee", o.knee, R { 0.0, 48.0 });
    ramp ("divisor", o.divisor, R { 0.01, 1000.0 });   // above zero: a geometric law divides by it
    const bool lo = in.required ("knobMinDb", o.knobMinDb, R { 0.0, 24.0 });
    const bool hi = in.required ("knobMaxDb", o.knobMaxDb, R { 0.0, 24.0 });
    d.ordered (in, lo && hi, o.knobMinDb, o.knobMaxDb, "knobMaxDb");
    in.required ("knobStepDb", o.knobStepDb, R { 0.01, 3.0 });
}

void readSaturation (Doc& d, Reader& in, Saturation& o)
{
    d.name (in, "shape", o.shape, kShapes);
    const bool drive = d.pair (in, "driveRange", o.driveRange, R { 0.0, 48.0 });
    in.required ("driveDb", o.driveDb, drive ? R { o.driveRange.min, o.driveRange.max } : R { 0.0, 48.0 });
    in.required ("driveStep", o.driveStep, R { 0.01, 12.0 });
    in.required ("bias", o.bias, R { -0.95, 0.95 });   // the core's domain
    const bool mix = d.pair (in, "mixRange", o.mixRange, share());
    in.required ("mix", o.mix, mix ? R { o.mixRange.min, o.mixRange.max } : share());
    in.required ("mixStep", o.mixStep, R { 0.001, 1.0 });
    const bool output = d.pair (in, "outputRange", o.outputRange, R { -48.0, 24.0 });
    in.required ("outputDb", o.outputDb, output ? R { o.outputRange.min, o.outputRange.max } : R { -48.0, 24.0 });
    in.required ("outputStep", o.outputStep, R { 0.01, 6.0 });
    in.required ("autoComp", o.autoComp, share());       // the core's domain
    in.required ("dcBlockHz", o.dcBlockHz, R { 0.0, 200.0 });
}

// normal within hard, hard within ±24 dB.
void readMoveTravel (Doc& d, Reader& in, Span& normal, Span& hard)
{
    const bool h = d.pair (in, "hard", hard, R { -24.0, 24.0 });
    d.pair (in, "normal", normal, h ? R { hard.min, hard.max } : R { -24.0, 24.0 });
}

void readTilt (Doc& d, Reader& in, Tilt& o, std::vector<std::int32_t>& bands)
{
    d.band (in, o.band, bands);
    in.required ("freqHz", o.freqHz, R { 20.0, 20000.0 });
    readMoveTravel (d, in, o.normal, o.hard);
    in.required ("step", o.step, R { 0.01, 3.0 });
}

void readLowShelf (Doc& d, Reader& in, LowShelf& o, std::vector<std::int32_t>& bands)
{
    d.band (in, o.band, bands);
    in.required ("freqHz", o.freqHz, R { 20.0, 20000.0 });
    in.required ("q", o.q, R { 0.1, 10.0 });
    readMoveTravel (d, in, o.normal, o.hard);
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
    in.required ("onUpToBits", o.onUpToBits, I { 8, 32 });
    d.name (in, "shaping", o.shaping, kShapings);
    in.required ("shapingUpToBits", o.shapingUpToBits, I { 8, 32 });
}

void readDeEsser (Doc& d, Reader& in, DeEsser& o, std::vector<std::int32_t>& bands)
{
    in.required ("automatic", o.automatic);
    d.band (in, o.band, bands);
    in.required ("q", o.q, R { 0.1, 10.0 });
    in.required ("atk", o.atk, share());
    in.required ("rel", o.rel, share());
    in.required ("offsetDb", o.offsetDb, R { 0.0, 40.0 });
    in.required ("ratioSlope", o.ratioSlope, share());
    const bool lo = in.required ("minDepthDb", o.minDepthDb, R { 0.0, 24.0 });
    const bool hi = in.required ("maxDepthDb", o.maxDepthDb, R { 0.0, 24.0 });
    d.ordered (in, lo && hi, o.minDepthDb, o.maxDepthDb, "maxDepthDb");
    in.required ("confidenceAbove", o.confidenceAbove, share());
    const bool dlo = in.required ("minDeltaLufs", o.minDeltaLufs, R { -6.0, 6.0 });
    const bool dhi = in.required ("maxDeltaLufs", o.maxDeltaLufs, R { -6.0, 6.0 });
    d.ordered (in, dlo && dhi, o.minDeltaLufs, o.maxDeltaLufs, "maxDeltaLufs");
    in.required ("centreStepHz", o.centreStepHz, R { 1.0, 2000.0 });
    in.required ("widthStepOct", o.widthStepOct, R { 0.01, 1.0 });
    in.required ("depthStepDb", o.depthStepDb, R { 0.01, 6.0 });
    d.pair (in, "centreHz", o.centreHz, R { 500.0, 24000.0 });
    d.pair (in, "widthOct", o.widthOct, R { 0.05, 8.0 });
    const bool depth = d.pair (in, "depthDb", o.depthDb, R { -48.0, 0.0 });
    in.required ("manualDepthDb", o.manualDepthDb, depth ? R { o.depthDb.min, o.depthDb.max } : R { -48.0, 0.0 });
    in.required ("minPairedDepthShare", o.minPairedDepthShare, share());
    in.required ("witnessQuantiles", o.witnessQuantiles, share());
}

void readStereoBursts (Doc& d, Reader& in, StereoBursts& o)
{
    const bool lo = in.required ("bandLowHz", o.bandLowHz, R { 20.0, 24000.0 });
    const bool hi = in.required ("bandHighHz", o.bandHighHz, R { 20.0, 24000.0 });
    d.ordered (in, lo && hi, o.bandLowHz, o.bandHighHz, "bandHighHz");
    in.required ("hopMs", o.hopMs, R { 1.0, 1000.0 });
    in.required ("baselineMs", o.baselineMs, R { 10.0, 60000.0 });
    const bool entered = in.required ("enterDb", o.enterDb, R { 0.0, 60.0 });
    const bool exited = in.required ("exitDb", o.exitDb, R { 0.0, 60.0 });
    d.ordered (in, entered && exited, o.exitDb, o.enterDb, "enterDb");   // an event leaves below where it entered
    in.required ("eventCapacity", o.eventCapacity, I { 1, 1 << 24 });
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
    if (lo && hi && o.minHops > o.maxHops) d.refuse (in, "maxHops", Refusal::OutOfOrder);
    const bool sf = in.required ("fromShareOverDomeDb", o.fromShareOverDomeDb, R { -60.0, 0.0 });
    const bool sa = in.required ("fullAtShareOverDomeDb", o.fullAtShareOverDomeDb, R { -60.0, 0.0 });
    d.ordered (in, sf && sa, o.fromShareOverDomeDb, o.fullAtShareOverDomeDb, "fullAtShareOverDomeDb");
    in.required ("sideBelowMidDb", o.sideBelowMidDb, R { 0.0, 60.0 });
    in.required ("nearMonoShare", o.nearMonoShare, share());
    const bool pf = in.required ("fromPerMinute", o.fromPerMinute, R { 0.0, 10000.0 });
    const bool pa = in.required ("fullAtPerMinute", o.fullAtPerMinute, R { 0.0, 10000.0 });
    d.ordered (in, pf && pa, o.fromPerMinute, o.fullAtPerMinute, "fullAtPerMinute");
    in.required ("periodicToleranceHops", o.periodicToleranceHops, I { 0, 1000 });
    if (in.required ("periodicMultiples", o.periodicMultiples, I { 1, 64 })) d.ascending (in, "periodicMultiples", o.periodicMultiples);
    const bool qf = in.required ("periodicFrom", o.periodicFrom, share());
    const bool qa = in.required ("periodicFullAt", o.periodicFullAt, share());
    d.ordered (in, qf && qa, o.periodicFrom, o.periodicFullAt, "periodicFullAt");
    in.required ("blindAtDuty", o.blindAtDuty, share());
    const bool vf = in.required ("severityFromDb", o.severityFromDb, R { 0.0, 120.0 });
    const bool va = in.required ("severityFullAtDb", o.severityFullAtDb, R { 0.0, 120.0 });
    d.ordered (in, vf && va, o.severityFromDb, o.severityFullAtDb, "severityFullAtDb");
    in.required ("excessQuantile", o.excessQuantile, share());
    in.required ("shareQuantile", o.shareQuantile, share());
    in.required ("sideMidQuantile", o.sideMidQuantile, share());
    in.required ("loudestMoments", o.loudestMoments, I { 0, 1000 });
}

void readObservations (Doc& d, Reader& in, Observations& o)
{
    const bool doubtful = in.required ("doubtfulBelow", o.doubtfulBelow, share());
    in.table ("clipping", Need::Required, [&] (Reader& t)
    {
        t.required ("fullAtShareOfProgramme", o.clippingFullAtShareOfProgramme, share());
    });
    in.table ("dcOffset", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("from", o.dcOffsetFrom, share());
        const bool a = t.required ("fullAt", o.dcOffsetFullAt, share());
        d.ordered (t, f && a, o.dcOffsetFrom, o.dcOffsetFullAt, "fullAt");
    });
    in.table ("bitsUnused", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("fromBits", o.bitsUnusedFromBits, I { 0, 32 });
        const bool a = t.required ("fullAtBits", o.bitsUnusedFullAtBits, I { 0, 32 });
        if (f && a && o.bitsUnusedFromBits > o.bitsUnusedFullAtBits) d.refuse (t, "fullAtBits", Refusal::OutOfOrder);
    });
    in.table ("edgeSilence", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("fromSeconds", o.edgeSilenceFromSeconds, R { 0.0, 600.0 });
        const bool a = t.required ("fullAtSeconds", o.edgeSilenceFullAtSeconds, R { 0.0, 600.0 });
        d.ordered (t, f && a, o.edgeSilenceFromSeconds, o.edgeSilenceFullAtSeconds, "fullAtSeconds");
    });
    in.table ("hum", Need::Required, [&] (Reader& t)
    {
        const bool f = t.required ("fromProminenceDb", o.humFromProminenceDb, R { 0.0, 120.0 });
        const bool a = t.required ("fullAtProminenceDb", o.humFullAtProminenceDb, R { 0.0, 120.0 });
        d.ordered (t, f && a, o.humFromProminenceDb, o.humFullAtProminenceDb, "fullAtProminenceDb");
        t.required ("fullAtPowerAgainstProgramme", o.humFullAtPowerAgainstProgramme, share());
    });
    in.table ("humWandered", Need::Required, [&] (Reader& t)
    {
        // Always doubtful: strictly below doubtfulBelow.
        if (t.required ("confidenceCeiling", o.humWanderedConfidenceCeiling, share()) && doubtful
            && o.humWanderedConfidenceCeiling >= o.doubtfulBelow)
            d.refuse (t, "confidenceCeiling", Refusal::OutOfOrder);
    });
    in.table ("spectralWall", Need::Required, [&] (Reader& t)
    {
        t.required ("fullAtDropDb", o.spectralWallFullAtDropDb, R { 0.0, 200.0 });
        const bool f = t.required ("fromFractionBelowNyquist", o.spectralWallFromFractionBelowNyquist, share());
        const bool a = t.required ("fullAtFractionBelowNyquist", o.spectralWallFullAtFractionBelowNyquist, share());
        d.ordered (t, f && a, o.spectralWallFromFractionBelowNyquist, o.spectralWallFullAtFractionBelowNyquist,
                   "fullAtFractionBelowNyquist");
    });
    in.table ("infraLow", Need::Required, [&] (Reader& t)
    {
        const bool l = t.required ("low", o.infraLowLow, share());
        const bool h = t.required ("high", o.infraLowHigh, share());
        d.ordered (t, l && h, o.infraLowLow, o.infraLowHigh, "high");
    });
    in.table ("wideBass", Need::Required, [&] (Reader& t)
    {
        t.required ("sideFractionAtLeast", o.wideBassSideFractionAtLeast, share());
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

void readCrest (Doc& d, Reader& in, Crest& o)
{
    if (in.required ("bandEdgesHz", o.bandEdgesHz, R { 20.0, 24000.0 })) d.ascending (in, "bandEdgesHz", o.bandEdgesHz);
    in.required ("hopMs", o.hopMs, R { 1.0, 10000.0 });
    in.required ("blockHops", o.blockHops, I { 1, 1000 });
    in.required ("bandShareFloorDb", o.bandShareFloorDb, R { -200.0, 0.0 });
    in.required ("floorDb", o.floorDb, R { -200.0, 0.0 });
    in.required ("belowMeanSquareDb", o.belowMeanSquareDb, R { 0.0, 200.0 });
    in.required ("noProgrammeDb", o.noProgrammeDb, R { -1000.0, 0.0 });
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
    if (lo && hi && o.grTraceBucketsMin > o.grTraceBucketsMax) d.refuse (in, "grTraceBucketsMax", Refusal::OutOfOrder);
    if (in.required ("printedQuantiles", o.printedQuantiles, share())) d.ascending (in, "printedQuantiles", o.printedQuantiles);
    in.table ("pumping", Need::Required, [&] (Reader& t)
    {
        const bool hp = t.required ("highPassHz", o.pumping.highPassHz, R { 0.01, 1000.0 });
        const bool lp = t.required ("lowPassHz", o.pumping.lowPassHz, R { 0.01, 1000.0 });
        d.ordered (t, hp && lp, o.pumping.highPassHz, o.pumping.lowPassHz, "lowPassHz");
        t.required ("settleSeconds", o.pumping.settleSeconds, R { 0.0, 60.0 });
        t.required ("minTraceRateHz", o.pumping.minTraceRateHz, R { 1.0, 10000.0 });
    });
    in.table ("sections", Need::Required, [&] (Reader& t)
    {
        Sections& s = o.sections;
        t.required ("changeLu", s.changeLu, R { 0.1, 40.0 });
        t.required ("minSeconds", s.minSeconds, R { 0.0, 600.0 });
        const bool warn = t.required ("warnLu", s.warnLu, R { 0.0, 40.0 });
        const bool limit = t.required ("limitLu", s.limitLu, R { 0.0, 40.0 });
        d.ordered (t, warn && limit, s.warnLu, s.limitLu, "limitLu");
        t.required ("spikeLu", s.spikeLu, R { 0.0, 40.0 });
        t.required ("masterShorterByAtMost", s.masterShorterByAtMost, share());
        t.required ("minComparedShare", s.minComparedShare, share());
        t.required ("worstNamedAbove", s.worstNamedAbove, I { 0, 1000 });
        t.required ("quantile", s.quantile, share());
    });
}

void readProgress (Reader& in, Progress& o)
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
        });
    });
    in.table ("master", Need::Required, [&] (Reader& t)
    {
        t.required ("passWeight", o.masterPassWeight, R { 0.0, 1000.0 });
        t.required ("measureWeight", o.masterMeasureWeight, R { 0.0, 1000.0 });
        t.required ("expectedPasses", o.masterExpectedPasses, I { 1, 1000 });
    });
}

void readBlindTest (Doc& d, Reader& in, BlindTest& o, const std::vector<std::string>* targets, const Saturation& saturation)
{
    if (in.required ("targets", o.targets) && targets != nullptr)
        for (std::size_t i = 0; i < o.targets.size(); ++i)
            if (! contains (*targets, o.targets[i])) d.refuseItem (in, "targets", i, Refusal::NotATarget);
    in.required ("fragmentSeconds", o.fragmentSeconds, R { 1.0, 600.0 });
    in.required ("repeats", o.repeats, I { 1, 100 });
    in.required ("matchToleranceLu", o.matchToleranceLu, R { 0.001, 3.0 });
    // The saturator's knob, on its travel — once that travel was read.
    const Span& drive = saturation.driveRange;
    in.required ("clipperKnobDb", o.clipperKnobDb, drive.min < drive.max ? R { drive.min, drive.max } : R { 0.0, 48.0 });
    in.table ("listened", Need::Required, [&] (Reader& t) { t.required ("minSwitches", o.listenedMinSwitches, I { 0, 100 }); });
    in.tables ("variants", Need::Required, [&] (Reader& v)
    {
        BlindVariant b;
        v.required ("key", b.key);
        const toml::Value* c = v.data().find ("compressor");
        if (c != nullptr && std::holds_alternative<toml::Table> (c->data))
        {
            b.compressor = BlindCompressor::Given;
            v.table ("compressor", Need::Required, [&] (Reader& t)
            {
                t.required ("ratio", b.ratio, R { 1.0, 20.0 });
                t.required ("threshOffset", b.threshOffset, R { -60.0, 60.0 });
                t.required ("attackMs", b.attackMs, R { 0.01, 1000.0 });
                t.required ("kneeDb", b.kneeDb, R { 0.0, 48.0 });
                t.required ("releaseMs", b.releaseMs, R { 1.0, 5000.0 });
            });
        }
        else
        {
            std::string kind;
            if (v.required ("compressor", kind))
            {
                if (kind == "none") b.compressor = BlindCompressor::None;
                else if (kind == "page") b.compressor = BlindCompressor::Page;
                else d.refuse (v, "compressor", Refusal::NotOneOf);
            }
        }
        o.variants.push_back (std::move (b));
    });
}

void readEngine (Doc& d, Reader& in, Engine& o, const std::vector<std::string>* targets)
{
    std::vector<std::int32_t> bands;   // the core's EQ bands the devices have taken, in reading order
    in.required ("defaults", o.defaults);
    in.table ("input", Need::Required, [&] (Reader& t) { readInput (d, t, o.input); });
    in.table ("landing", Need::Required, [&] (Reader& t) { readLanding (d, t, o.landing); });
    in.table ("limiter", Need::Required, [&] (Reader& t) { readLimiter (d, t, o.limiter); });
    in.table ("lowEnd", Need::Required, [&] (Reader& t) { readLowEnd (d, t, o.lowEnd); });
    in.table ("hpf", Need::Required, [&] (Reader& t) { readHpf (d, t, o.hpf, bands); });
    in.table ("monoBass", Need::Required, [&] (Reader& t) { readMonoBass (d, t, o.monoBass); });
    in.table ("compressor", Need::Required, [&] (Reader& t) { readCompressor (d, t, o.compressor); });
    in.table ("glue", Need::Required, [&] (Reader& t) { readGlue (d, t, o.glue, targets); });
    in.table ("saturation", Need::Required, [&] (Reader& t) { readSaturation (d, t, o.saturation); });
    in.table ("tilt", Need::Required, [&] (Reader& t) { readTilt (d, t, o.tilt, bands); });
    in.table ("lowShelf", Need::Required, [&] (Reader& t) { readLowShelf (d, t, o.lowShelf, bands); });
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
    in.table ("stereoBursts", Need::Required, [&] (Reader& t) { readStereoBursts (d, t, o.stereoBursts); });
    in.table ("observations", Need::Required, [&] (Reader& t) { readObservations (d, t, o.observations); });
    in.table ("crest", Need::Required, [&] (Reader& t) { readCrest (d, t, o.crest); });
    in.table ("cost", Need::Required, [&] (Reader& t) { readCost (d, t, o.cost); });
    in.table ("progress", Need::Required, [&] (Reader& t) { readProgress (t, o.progress); });
    in.table ("blindTest", Need::Required, [&] (Reader& t) { readBlindTest (d, t, o.blindTest, targets, o.saturation); });
}

//==============================================================================
// targets.toml

// The travels a target row is checked against: from the edit travels and the engine's knobs once those were read.
struct RowDomains
{
    R lufs { -70.0, 0.0 };
    R tp { -20.0, 0.0 };
    R monoBass { 20.0, 1000.0 };
    R hpfFloor { 1.0, 200.0 };
    R lowShelf { -24.0, 24.0 };
    const std::vector<std::int32_t>* slopes = nullptr;
};

bool readEdit (Doc& d, Reader& in, std::string_view key, Edit& o, const R& domain)
{
    bool good = false;
    in.table (key, Need::Required, [&] (Reader& t)
    {
        const bool from = t.required ("from", o.from, domain);
        const bool to = t.required ("to", o.to, domain);
        d.ordered (t, from && to, o.from, o.to, "to");
        const bool travel = from && to && o.from <= o.to;
        d.pair (t, "green", o.green, travel ? R { o.from, o.to } : domain);
        t.required ("step", o.step, R { 0.001, 1.0 });
        good = travel;
    });
    return good;
}

void readTarget (Doc& d, Reader& row, Target& x, const RowDomains& b)
{
    d.name (row, "group", x.group, kGroups);
    row.required ("lufs", x.lufs, b.lufs);
    row.required ("tp", x.tp, b.tp);
    row.required ("monoBass", x.monoBass, b.monoBass);
    row.required ("hpfFloor", x.hpfFloor, b.hpfFloor);
    if (row.required ("hpfSlopeDbPerOct", x.hpfSlopeDbPerOct, I { 6, 96 }) && b.slopes != nullptr
        && ! contains (*b.slopes, x.hpfSlopeDbPerOct))
        d.refuse (row, "hpfSlopeDbPerOct", Refusal::NotOneOf);
    row.required ("noteLossDb", x.noteLossDb, R { 0.1, 3.0 });
    if (row.required ("sampleRate", x.sampleRate, I { 0, 384000 }) && x.sampleRate != 0 && x.sampleRate < 8000)
        d.refuse (row, "sampleRate", Refusal::NotOneOf);   // 0, or a delivery rate of 8 kHz and up
    if (row.required ("bitDepth", x.bitDepth, I { 16, 24 }) && x.bitDepth != 16 && x.bitDepth != 24)
        d.refuse (row, "bitDepth", Refusal::NotOneOf);
    row.optional ("noClipper", x.noClipper);
    row.optional ("hpfAlways", x.hpfAlways);
    row.optional ("sourceRatePass", x.sourceRatePass);
    if (row.data().find ("lowShelfDb") != nullptr)
    {
        double db = 0.0;
        if (row.required ("lowShelfDb", db, b.lowShelf)) x.lowShelfDb = db;
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
        if (readEdit (d, t, "lufs", o.editLufs, R { -70.0, 0.0 })) b.lufs = R { o.editLufs.from, o.editLufs.to };
        if (readEdit (d, t, "tp", o.editTp, R { -20.0, 0.0 })) b.tp = R { o.editTp.from, o.editTp.to };
    });
    // The engine's travels. A travel the engine failed to read is still its default {0, 0} here, and every row would be
    // refused against it for the engine's mistake — so only a travel that is a range is used.
    if (e.monoBass.frequencyRange.min < e.monoBass.frequencyRange.max)
        b.monoBass = R { e.monoBass.frequencyRange.min, e.monoBass.frequencyRange.max };
    if (e.hpf.hzMin < e.hpf.hzMax) b.hpfFloor = R { e.hpf.hzMin, e.hpf.hzMax };
    if (e.lowShelf.hard.min < e.lowShelf.hard.max) b.lowShelf = R { e.lowShelf.hard.min, e.lowShelf.hard.max };
    if (! e.hpf.slopes.empty()) b.slopes = &e.hpf.slopes;

    in.table ("targets", Need::Required, [&] (Reader& t)
    {
        for (const auto& entry : t.data().entries())
            t.table (entry.key, Need::Required, [&] (Reader& row)
            {
                Target x;
                x.key = entry.key;
                readTarget (d, row, x, b);
                o.targets.push_back (std::move (x));
            });
    });
    if (in.required ("default", o.defaultTarget) && find (o, o.defaultTarget) == nullptr)
        d.refuse (in, "default", Refusal::NotATarget);
    if (in.required ("main", o.main))
        for (std::size_t i = 0; i < o.main.size(); ++i)
        {
            if (find (o, o.main[i]) == nullptr) d.refuseItem (in, "main", i, Refusal::NotATarget);
            else if (std::find (o.main.begin(), o.main.begin() + std::ptrdiff_t (i), o.main[i]) != o.main.begin() + std::ptrdiff_t (i))
                d.refuseItem (in, "main", i, Refusal::Duplicate);
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
                q.refusal = p.detail <= std::uint32_t (Refusal::Fixed) ? static_cast<Refusal> (p.detail) : Refusal::None;
                break;
        }
        q.code = q.fault == Fault::Refused ? name (q.refusal) : name (q.fault);
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

toml::embedded::View rootOf (Document document) noexcept
{
    return document == Document::Targets ? embedded::targets.root() : embedded::engine.root();
}
} // namespace

const Target* find (const Targets& targets, std::string_view key) noexcept
{
    for (const Target& t : targets.targets)
        if (t.key == key) return &t;
    return nullptr;
}

const char* name (Document document) noexcept
{
    switch (document)
    {
        case Document::Targets: return "targets";
        case Document::Engine:  return "engine";
    }
    return "unknown";
}

const char* name (Fault fault) noexcept
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

const char* name (Refusal refusal) noexcept
{
    switch (refusal)
    {
        case Refusal::None:       return "Refused";
        case Refusal::NotOneOf:   return "NotOneOf";
        case Refusal::NotATarget: return "NotATarget";
        case Refusal::NotAPair:   return "NotAPair";
        case Refusal::OutOfOrder: return "OutOfOrder";
        case Refusal::Duplicate:  return "Duplicate";
        case Refusal::Fixed:      return "Fixed";
    }
    return "Refused";
}

bool Loaded::ok() const noexcept
{
    return problems.empty();
}

Loaded load()
{
    const toml::Table targets = toml::embedded::toTable (rootOf (Document::Targets), kTargetsSource);
    const toml::Table engine = toml::embedded::toTable (rootOf (Document::Engine), kEngineSource);
    return bindTables (&targets, &engine);
}

Loaded bind (std::string_view targetsToml, std::string_view engineToml)
{
    const toml::ParseResult targets = toml::parse (targetsToml, kTargetsSource);
    const toml::ParseResult engine = toml::parse (engineToml, kEngineSource);
    const auto* t = std::get_if<toml::Table> (&targets);
    const auto* e = std::get_if<toml::Table> (&engine);
    Loaded out = bindTables (t, e);
    // A document that did not parse is one problem; it comes where that document's problems would have.
    if (const auto* error = std::get_if<toml::Error> (&engine))
        out.problems.insert (out.problems.begin(), syntax (*error, Document::Engine));
    if (const auto* error = std::get_if<toml::Error> (&targets)) out.problems.push_back (syntax (*error, Document::Targets));
    return out;
}

std::string text (Document document)
{
    return toml::write (toml::embedded::toTable (rootOf (document)));
}

std::uint64_t version() noexcept
{
    return detail::versionOf (rootOf (Document::Targets), rootOf (Document::Engine));
}

std::optional<std::uint64_t> versionOf (std::string_view targetsToml, std::string_view engineToml)
{
    const toml::ParseResult targets = toml::parse (targetsToml);
    const toml::ParseResult engine = toml::parse (engineToml);
    const auto* t = std::get_if<toml::Table> (&targets);
    const auto* e = std::get_if<toml::Table> (&engine);
    if (t == nullptr || e == nullptr) return std::nullopt;
    return detail::versionOf (*t, *e);
}

} // namespace felitronics::session::config
