// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE OBSERVATIONS (src/Observations.h): every number from the config read in place, every transcendental the
// deterministic one.

#include "BuildGuards.h"

#include "Observations.h"
#include "Grid.h"
#include "BuildContract.h"

#include <felitronics/analysis/SourceForensics.h>
#include <felitronics/core/DetMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{
namespace
{
using View = toml::embedded::View;
double number (View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    storageOverflow();
}
const MeasurementResult* resultOf (const ObservationInputs& in, Analyzer analyzer) noexcept
{
    const auto i = std::size_t (analyzer);
    return i < in.measurements.size() ? &in.measurements[i] : nullptr;
}
// A number of a result that ended with values: present and finite, or absent with its own reason.
struct Reading
{
    std::optional<double> value;
    MeasurementReason reason = MeasurementReason::Pending;
};
Reading read (const MeasurementResult* r, std::string_view name) noexcept
{
    if (! r) return {};
    if (r->status != MeasurementStatus::Ready) return { {}, r->reason };
    for (const auto& v : r->numbers)
        if (v.name == name)
        {
            if (v.value && std::isfinite (*v.value)) return { v.value, MeasurementReason::None };
            return { {}, v.reason == MeasurementReason::None ? MeasurementReason::NonFinite : v.reason };
        }
    return { {}, MeasurementReason::Unsupported };
}
// A number an ended result carries whatever its status: an analyzer that refused to call its reading valid still says
// what it saw (the hum detector's reasons and candidates).
std::optional<double> carried (const MeasurementResult* r, std::string_view name) noexcept
{
    if (r && r->status != MeasurementStatus::Pending && r->status != MeasurementStatus::Cancelled)
        for (const auto& v : r->numbers)
            if (v.name == name && v.value && std::isfinite (*v.value)) return v.value;
    return {};
}
const MeasurementArray* carriedArray (const MeasurementResult* r, std::string_view name) noexcept
{
    if (r && r->status != MeasurementStatus::Pending && r->status != MeasurementStatus::Cancelled)
        for (const auto& a : r->arrays)
            if (a.name == name) return &a;
    return nullptr;
}
const MeasurementArray* arrayOf (const MeasurementResult* r, std::string_view name) noexcept
{
    if (r && r->status == MeasurementStatus::Ready)
        for (const auto& a : r->arrays)
            if (a.name == name) return &a;
    return nullptr;
}
// A weight along a ramp: 0 at `from` and under, 1 at `fullAt` and over.
double ramp (double x, double from, double fullAt) noexcept
{
    if (! (fullAt > from)) return x >= fullAt ? 1.0 : 0.0;
    return std::clamp ((x - from) / (fullAt - from), 0.0, 1.0);
}
ObservationStyle styleOf (const ObservationInputs& in, std::string_view kind) noexcept
{
    const auto name = in.rules.engine.find ("observations").find ("kinds").find (kind).string();
    if (! name) storageOverflow();
    return *name == "error" ? ObservationStyle::Error : *name == "warning" ? ObservationStyle::Warning : ObservationStyle::Note;
}
Observation start (const ObservationInputs& in, std::string_view kind, HandledBy handledBy, bool hypothesis) noexcept
{
    Observation o;
    o.style = styleOf (in, kind);
    o.handledBy = handledBy;
    o.hypothesis = hypothesis;
    o.handled = (handledBy == HandledBy::Hpf && in.hpfOn) || (handledBy == HandledBy::MonoBass && in.monoBassOn);
    return o;
}
Observation& unmeasured (Observation& o, MeasurementReason reason) noexcept
{
    o.status = ObservationStatus::NotMeasured;
    o.reason = reason == MeasurementReason::None ? MeasurementReason::Unsupported : reason;
    return o;
}
Observation& absent (Observation& o) noexcept
{
    o.status = ObservationStatus::NotFound;
    o.reason = MeasurementReason::None;
    return o;
}
Observation& found (const ObservationInputs& in, Observation& o, double confidence, double severity) noexcept
{
    o.status = ObservationStatus::Found;
    o.reason = MeasurementReason::None;
    o.confidence = std::clamp (confidence, 0.0, 1.0);
    o.severity = std::clamp (severity, 0.0, 1.0);
    o.doubtful = o.confidence < number (in.rules.engine.find ("observations").find ("doubtfulBelow"));
    return o;
}
double seconds (const ObservationInputs& in) noexcept
{
    return in.sampleRate != 0 ? double (in.frames) / double (in.sampleRate) : 0.0;
}
// "name[c]" for channel c (0 or 1): the per-channel numbers of a result.
struct Channelled
{
    char text[kMeasurementNameBytes] {};
    std::size_t size = 0;
    Channelled (std::string_view name, unsigned channel) noexcept
    {
        size = std::min (name.size(), sizeof (text) - 3);
        std::copy_n (name.begin(), size, text);
        text[size++] = '['; text[size++] = char ('0' + channel); text[size++] = ']';
    }
    std::string_view view() const noexcept { return { text, size }; }
};
// The `share`-quantile of a histogram of `bins` equal steps from `lowest`: the value whose bin the running count first
// reaches that share of the total in. 0 entries: nothing.
std::optional<double> quantile (std::span<const std::uint16_t> bins, double lowest, double step, double share) noexcept
{
    std::uint64_t total = 0;
    for (const auto n : bins) total += n;
    if (total == 0) return {};
    const double wanted = share * double (total);
    std::uint64_t below = 0;
    for (std::size_t i = 0; i < bins.size(); ++i)
    {
        below += bins[i];
        if (double (below) >= wanted) return lowest + double (i) * step;
    }
    return lowest + double (bins.size() - 1) * step;
}
void count (std::span<std::uint16_t> bins, double lowest, double step, double x) noexcept
{
    const double at = std::clamp ((x - lowest) / step, 0.0, double (bins.size() - 1));
    auto& bin = bins[std::size_t (at)];
    if (bin < std::numeric_limits<std::uint16_t>::max()) ++bin;
}

//==============================================================================
// THE FILE

Observation clipping (const ObservationInputs& in) noexcept
{
    auto o = start (in, "clipping", HandledBy::Person, false);
    const auto* r = resultOf (in, Analyzer::Clipping);
    const auto runs = read (r, "runCount");
    if (! runs.value) return unmeasured (o, runs.reason);
    if (! (*runs.value > 0.0)) return absent (o);
    o.value = *runs.value;
    const double length = seconds (in);
    o.second = length > 0.0 ? *runs.value * 60.0 / length : 0.0;
    for (unsigned c = 0; c < in.channels && c < 2; ++c)
        if (const auto clipped = read (r, Channelled ("clippedSamples", c).view()).value; clipped && in.frames != 0)
            o.third = std::max (o.third, *clipped / double (in.frames));
    // The first distinct places: each at least a second after the one before — two channels clipping together are one.
    if (const auto* clips = arrayOf (r, "clips"); clips && clips->columns == 6 && in.sampleRate != 0)
    {
        const auto rows = std::min<std::size_t> (std::size_t (clips->stored), clips->values.size() / 6);
        double after = -std::numeric_limits<double>::infinity();
        double* const places[] { &o.at1, &o.at2, &o.at3 };
        for (auto* place : places)
        {
            double next = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < rows; ++i)
            {
                const double at = clips->values[i * 6] / double (in.sampleRate);
                if (at >= after && at < next) next = at;
            }
            if (! std::isfinite (next)) break;
            *place = next;
            ++o.places;
            after = next + 1.0;
        }
    }
    const double fullAt = number (in.rules.engine.find ("observations").find ("clipping").find ("fullAtShareOfProgramme"));
    return found (in, o, 1.0, ramp (o.third, 0.0, fullAt));
}

Observation dcOffset (const ObservationInputs& in) noexcept
{
    auto o = start (in, "dcOffset", HandledBy::Hpf, false);
    const auto* r = resultOf (in, Analyzer::Clipping);
    std::optional<double> largest;
    auto reason = MeasurementReason::Unsupported;
    // Every channel is read, or the offset is not measured: a channel without its number is not a channel without DC.
    for (unsigned c = 0; c < in.channels && c < 2; ++c)
    {
        const auto dc = read (r, Channelled ("dcOffset", c).view());
        if (! dc.value) { reason = dc.reason; largest.reset(); break; }
        largest = std::max (largest.value_or (0.0), std::fabs (*dc.value));
    }
    if (! largest) return unmeasured (o, r ? reason : MeasurementReason::Pending);
    const auto config = in.rules.engine.find ("observations").find ("dcOffset");
    const double from = number (config.find ("from")), fullAt = number (config.find ("fullAt"));
    o.value = *largest;
    if (*largest < from) return absent (o);
    return found (in, o, 1.0, ramp (*largest, from, fullAt));
}

Observation bitsUnused (const ObservationInputs& in) noexcept
{
    auto o = start (in, "bitsUnused", HandledBy::Person, false);
    const auto* r = resultOf (in, Analyzer::Forensics);
    std::optional<double> fewest;
    auto reason = MeasurementReason::Unsupported;
    for (unsigned c = 0; c < in.channels && c < 2; ++c)
    {
        const auto bits = read (r, Channelled ("grid.alwaysZeroLowBits", c).view());
        if (! bits.value) { reason = bits.reason; fewest.reset(); break; }
        fewest = fewest ? std::min (*fewest, *bits.value) : *bits.value;
    }
    if (! fewest) return unmeasured (o, r ? reason : MeasurementReason::Pending);
    const auto config = in.rules.engine.find ("observations").find ("bitsUnused");
    const double from = number (config.find ("fromBits")), fullAt = number (config.find ("fullAtBits"));
    o.value = *fewest;
    if (*fewest < from) return absent (o);
    return found (in, o, 1.0, ramp (*fewest, from, fullAt));
}

Observation dualMono (const ObservationInputs& in) noexcept
{
    auto o = start (in, "dualMono", HandledBy::Nothing, false);
    const auto dual = read (resultOf (in, Analyzer::Stereo), "dualMono");
    if (! dual.value) return unmeasured (o, dual.reason);
    if (! (*dual.value > 0.5)) return absent (o);
    return found (in, o, 1.0, 0.0);
}

Observation edgeSilence (const ObservationInputs& in) noexcept
{
    auto o = start (in, "edgeSilence", HandledBy::Person, false);
    const auto* r = resultOf (in, Analyzer::Programme);
    const auto rate = read (r, "sampleRate");
    const auto leading = read (r, "leadingSilenceSamples"), trailing = read (r, "trailingSilenceSamples");
    const auto leadingValid = read (r, "leadingSilenceValid"), trailingValid = read (r, "trailingSilenceValid");
    const bool head = leading.value && leadingValid.value && *leadingValid.value > 0.5;
    const bool tail = trailing.value && trailingValid.value && *trailingValid.value > 0.5;
    // Both edges are measured, or neither is judged: an edge not measured is not an edge without silence.
    if (! rate.value || ! (*rate.value > 0.0) || ! head || ! tail)
    {
        // The analyzer calls an edge invalid only for a non-finite sample in it.
        auto missing = MeasurementReason::NonFinite;
        for (const auto* x : { &leading, &leadingValid, &trailing, &trailingValid })
            if (! x->value) { missing = x->reason; break; }
        return unmeasured (o, ! r ? MeasurementReason::Pending : ! rate.value ? rate.reason : ! (*rate.value > 0.0) ? MeasurementReason::Unsupported : missing);
    }
    o.value = *leading.value / *rate.value;
    o.second = *trailing.value / *rate.value;
    const auto config = in.rules.engine.find ("observations").find ("edgeSilence");
    const double from = number (config.find ("fromSeconds")), fullAt = number (config.find ("fullAtSeconds"));
    const double longest = std::max (o.value, o.second);
    if (longest < from) return absent (o);
    return found (in, o, 1.0, ramp (longest, from, fullAt));
}

Observation tooQuiet (const ObservationInputs& in) noexcept
{
    auto o = start (in, "tooQuiet", HandledBy::Person, false);
    const auto lufs = read (resultOf (in, Analyzer::Loudness), "integratedLufs");
    if (! lufs.value) return unmeasured (o, lufs.reason);
    const auto quiet = in.rules.engine.find ("input").find ("quiet");
    const double warning = number (quiet.find ("warningLufs")), gainOnly = number (quiet.find ("gainOnlyLufs"));
    o.value = *lufs.value;
    // Strictly quieter: at a boundary itself the input is still in the ordinary mode.
    if (! (*lufs.value < warning)) return absent (o);
    o.second = *lufs.value < gainOnly ? 1.0 : 0.0;
    return found (in, o, 1.0, o.second > 0.5 ? 1.0 : ramp (warning - *lufs.value, 0.0, warning - gainOnly));
}

Observation tooShort (const ObservationInputs& in) noexcept
{
    auto o = start (in, "tooShort", HandledBy::Nothing, false);
    if (in.channels == 0 || in.sampleRate == 0) return unmeasured (o, MeasurementReason::Pending);
    o.value = seconds (in);
    o.second = number (in.rules.engine.find ("input").find ("shortSeconds"));
    if (! (o.value < o.second)) return absent (o);
    return found (in, o, 1.0, 0.0);
}

Observation alreadyLimited (const ObservationInputs& in) noexcept
{
    auto o = start (in, "alreadyLimited", HandledBy::Nothing, true);
    const auto* loudness = resultOf (in, Analyzer::Loudness);
    const auto lufs = read (loudness, "integratedLufs"), peak = read (loudness, "truePeakDb");
    if (! lufs.value || ! peak.value) return unmeasured (o, ! lufs.value ? lufs.reason : peak.reason);
    o.value = *peak.value - *lufs.value;
    o.second = number (in.rules.engine.find ("observations").find ("alreadyLimited").find ("plrBelowDb"));
    const auto runs = read (resultOf (in, Analyzer::Clipping), "runCount");
    const double length = seconds (in);
    const bool clipped = runs.value && length > 0.0 && regularlyClipped (in.rules, *runs.value * 60.0 / length);
    const bool dense = o.value < o.second;
    // Dense is enough to find it; not dense, it is not found only where the clips were counted.
    if (! dense && ! runs.value) return unmeasured (o, runs.reason);
    if (! dense && ! (length > 0.0)) return unmeasured (o, MeasurementReason::Pending);
    if (! dense && ! clipped) return absent (o);
    o.third = ! dense && clipped ? 1.0 : 0.0;
    return found (in, o, 1.0, 0.0);
}

//==============================================================================
// THE SPECTRUM

Observation spectralWall (const ObservationInputs& in) noexcept
{
    auto o = start (in, "spectralWall", HandledBy::Nothing, false);
    const auto* r = resultOf (in, Analyzer::Forensics);
    const auto valid = read (r, "wall.valid");
    if (! valid.value) return unmeasured (o, valid.reason);
    const auto cutoff = read (r, "wall.cutoffHz"), drop = read (r, "wall.dropDb"), fraction = read (r, "wall.cutoffFractionOfNyquist");
    if (! (*valid.value > 0.5))
    {
        // No wall — unless the file was too short to look for one.
        if (cutoff.reason == MeasurementReason::TooShort) return unmeasured (o, cutoff.reason);
        return absent (o);
    }
    if (! cutoff.value || ! drop.value || ! fraction.value) return unmeasured (o, MeasurementReason::Unsupported);
    const auto config = in.rules.engine.find ("observations").find ("spectralWall");
    const double below = 1.0 - *fraction.value;
    const double from = number (config.find ("fromFractionBelowNyquist"));
    o.value = *cutoff.value;
    o.second = *drop.value;
    o.third = below;
    if (below < from) return absent (o);
    // Confidence by the depth of the drop, from the core's own bound for calling a drop a wall.
    const double coreMinDrop = analysis::SourceForensicsParams {}.minDropDb;
    return found (in, o, ramp (*drop.value, coreMinDrop, number (config.find ("fullAtDropDb"))),
                  ramp (below, from, number (config.find ("fullAtFractionBelowNyquist"))));
}

// A note of the low end's main run: found with its MIDI number and its frequency; not found where the run had no note.
Observation lowNote (const ObservationInputs& in, std::string_view kind, std::string_view midiName, std::string_view hzName) noexcept
{
    auto o = start (in, kind, HandledBy::Nothing, false);
    const auto* r = resultOf (in, Analyzer::LowEnd);
    const auto midi = read (r, midiName), hz = read (r, hzName);
    if (! midi.value || ! hz.value)
    {
        const auto reason = ! midi.value ? midi.reason : hz.reason;
        if (r && r->status == MeasurementStatus::Ready && reason == MeasurementReason::NoSignal) return absent (o);
        return unmeasured (o, reason);
    }
    o.value = *midi.value;
    o.second = *hz.value;
    // The lowest band carries its sureness: an unsure one is shown, doubtful — half as confident as the doubtful line.
    const auto sure = read (r, "lowestOccupiedSure");
    const bool doubtful = kind == "lowestLowBand" && sure.value && ! (*sure.value > 0.5);
    const double doubtfulBelow = number (in.rules.engine.find ("observations").find ("doubtfulBelow"));
    return found (in, o, doubtful ? 0.5 * doubtfulBelow : 1.0, 0.0);
}

Observation infraLow (const ObservationInputs& in) noexcept
{
    auto o = start (in, "infraLow", HandledBy::Hpf, true);
    const auto* r = resultOf (in, Analyzer::InfraLow);
    const auto share = read (r, "infraLowShare"), crossover = read (r, "crossoverHz");
    if (! share.value || ! crossover.value) return unmeasured (o, ! share.value ? share.reason : crossover.reason);
    const auto config = in.rules.engine.find ("observations").find ("infraLow");
    const double low = number (config.find ("low")), high = number (config.find ("high"));
    o.value = *share.value;
    o.second = *crossover.value;
    if (*share.value < low) return absent (o);
    return found (in, o, 1.0, ramp (*share.value, low, high));
}

Observation wideBass (const ObservationInputs& in) noexcept
{
    auto o = start (in, "wideBass", HandledBy::MonoBass, false);
    const auto* r = resultOf (in, Analyzer::LowEnd);
    const auto side = read (r, "lowSideFraction"), crossover = read (r, "crossoverHz");
    // A mono file has no side: no wide bass, once the low end has ended.
    if (in.channels == 1 && r && r->status != MeasurementStatus::Pending && r->status != MeasurementStatus::Cancelled) return absent (o);
    if (! side.value || ! crossover.value)
    {
        const auto reason = ! side.value ? side.reason : crossover.reason;
        if (r && r->status == MeasurementStatus::Ready && reason == MeasurementReason::NoSignal) return absent (o);
        return unmeasured (o, reason);
    }
    o.value = *side.value;
    o.second = *crossover.value;
    if (*side.value < number (in.rules.engine.find ("observations").find ("wideBass").find ("sideFractionAtLeast"))) return absent (o);
    return found (in, o, 1.0, 0.0);
}

Observation polarity (const ObservationInputs& in) noexcept
{
    auto o = start (in, "polarity", HandledBy::Person, false);
    const auto* stereo = resultOf (in, Analyzer::Stereo);
    if (in.channels == 1 && stereo && stereo->status != MeasurementStatus::Pending && stereo->status != MeasurementStatus::Cancelled)
        return absent (o);
    const auto correlation = read (stereo, "correlation"), rawSide = read (resultOf (in, Analyzer::LowEnd), "rawSideFraction");
    if (! correlation.value && ! rawSide.value)
    {
        if (stereo && stereo->status == MeasurementStatus::Ready && correlation.reason == MeasurementReason::NoSignal) return absent (o);
        return unmeasured (o, correlation.reason);
    }
    // Half the rule is not the rule: a reading that is missing is not measured, never 0.
    if (! correlation.value || ! rawSide.value) return unmeasured (o, ! correlation.value ? correlation.reason : rawSide.reason);
    const auto config = in.rules.engine.find ("observations").find ("polarity");
    o.value = *correlation.value;
    o.second = *rawSide.value;
    const bool opposite = *correlation.value < number (config.find ("correlationBelow"))
                       || *rawSide.value > number (config.find ("rawSideFractionAbove"));
    if (! opposite) return absent (o);
    return found (in, o, 1.0, 0.0);
}

// SIBILANCE ([observations.sibilance], all hypotheses) — on the mid axis of the sibilance-band bursts: the bursts
// minHops…maxHops long (a syllable, not a needle and not a step). How sure: by how many of them a judged minute has
// (fromPerMinute…fullAtPerMinute), by their band's share of the broadband energy against the instrument's own ceiling
// (the dome: fromShareOverDomeDb…fullAtShareOverDomeDb, at shareQuantile), damped by how periodic their onsets are
// (a hi-hat grid is no speech: periodicFrom…periodicFullAt of the spacings within periodicToleranceHops of the modal
// spacing and its periodicMultiples), and only where they sit in the centre — the side sideBelowMidDb under the mid at
// the bursts' peaks (at sideMidQuantile), or a side that is all but absent (nearMonoShare). How much it matters: by the
// bursts' excess over their own baseline (severityFromDb…severityFullAtDb, at excessQuantile). Bursts in blindAtDuty of
// the judged time or more mean the detector is blind — not measured, never "clean".
Observation sibilance (const ObservationInputs& in) noexcept
{
    auto o = start (in, "sibilance", HandledBy::Nothing, true);
    const auto* r = resultOf (in, Analyzer::StereoBursts);
    const auto rate = read (r, "sampleRate");
    if (! rate.value) return unmeasured (o, rate.reason);
    const auto hop = read (r, "hopSamples[0]"), eligible = read (r, "eligibleHops[0]"), bursting = read (r, "burstHops[0]");
    const auto dome = read (r, "domeShare");
    const auto* events = arrayOf (r, "midEvents");
    if (! hop.value || ! eligible.value || ! bursting.value || ! dome.value || ! events || events->columns != 16
        || ! (*rate.value > 0.0) || ! (*hop.value > 0.0) || ! (*dome.value > 0.0))
        return unmeasured (o, MeasurementReason::Unsupported);
    if (! (*eligible.value > 0.0)) return unmeasured (o, MeasurementReason::NoSignal);
    const auto config = in.rules.engine.find ("observations").find ("sibilance");
    if (*bursting.value / *eligible.value >= number (config.find ("blindAtDuty"))) return unmeasured (o, MeasurementReason::Capacity);
    const double minHops = number (config.find ("minHops")), maxHops = number (config.find ("maxHops"));
    // Tenth-of-a-decibel histograms — the excess 0…60 dB, the share against the dome −40…+20 dB, the side against the
    // mid −60…+20 dB — each value held at its ends; the events a result stores (eventCapacity, 16384) fit 16 bits a bin.
    std::uint16_t excess[601] {}, share[601] {}, sideMid[801] {};
    constexpr double step = 0.1, lowestShare = -40.0, lowestSide = -60.0;
    const auto rows = std::min<std::size_t> (std::size_t (events->stored), events->values.size() / 16);
    std::uint64_t candidates = 0;
    struct Moment { double excess = -1.0, at = 0.0; };
    Moment loudest[3];
    const auto moments = std::size_t (std::clamp (number (config.find ("loudestMoments")), 0.0, 3.0));
    for (std::size_t i = 0; i < rows; ++i)
    {
        const double* e = events->values.data() + i * 16;
        if (e[8] < minHops || e[8] > maxHops || e[9] > 0.5 || ! std::isfinite (e[5])) continue;
        ++candidates;
        count (excess, 0.0, step, e[5]);
        if (e[3] > 0.0 && e[6] > 0.0) count (share, lowestShare, step, 10.0 * core::det::log10 (e[3] / e[6] / *dome.value));
        if (e[14] > 0.5 && e[12] > 0.0 && e[3] > 0.0) count (sideMid, lowestSide, step, 10.0 * core::det::log10 (e[12] / e[3]));
        // The loudest moments, the greatest excess first; an equal one keeps its earlier place.
        Moment m { e[5], e[2] / *rate.value };
        for (std::size_t k = 0; k < moments; ++k)
            if (m.excess > loudest[k].excess) std::swap (m, loudest[k]);
    }
    if (candidates == 0) return absent (o);
    const double minutes = *eligible.value * *hop.value / *rate.value / 60.0;
    const double perMinute = minutes > 0.0 ? double (candidates) / minutes : 0.0;
    const auto excessAt = quantile (excess, 0.0, step, number (config.find ("excessQuantile")));
    const auto shareAt = quantile (share, lowestShare, step, number (config.find ("shareQuantile")));
    const auto sideAt = quantile (sideMid, lowestSide, step, number (config.find ("sideMidQuantile")));
    // In the centre: the side far enough under the mid, or all but absent.
    const auto sideAbsent = read (r, "sideAbsent"), sideZero = read (r, "zeroBaselineHops[1]"), sideHops = read (r, "hopCount[1]");
    const bool nearMono = (sideAbsent.value && *sideAbsent.value > 0.5)
        || (sideZero.value && sideHops.value && *sideHops.value > 0.0
            && *sideZero.value / *sideHops.value >= number (config.find ("nearMonoShare")));
    const bool centred = nearMono || (sideAt && *sideAt <= -number (config.find ("sideBelowMidDb")));
    // How periodic: the share of the onset spacings within the tolerance of the modal spacing and its multiples.
    double periodic = 0.0;
    if (const auto* intervals = arrayOf (r, "midIntervals"); intervals && intervals->columns == 2)
        if (const auto modal = read (r, "modalIntervalHops[0]").value; modal && *modal >= 1.0)
        {
            const auto bins = std::min<std::size_t> (std::size_t (intervals->stored), intervals->values.size() / 2);
            const auto tolerance = std::int64_t (number (config.find ("periodicToleranceHops")));
            const auto multiples = config.find ("periodicMultiples");
            double all = 0.0, near = 0.0;
            for (std::size_t b = 0; b < bins; ++b)
            {
                const double n = intervals->values[b * 2];
                all += n;
                bool close = false;
                for (std::size_t m = 0; m < multiples.size() && ! close; ++m)
                {
                    const auto centre = std::int64_t (*modal * number (multiples[m]));
                    const auto spacing = std::int64_t (b) + 1;
                    close = spacing >= centre - tolerance && spacing <= centre + tolerance;
                }
                if (close) near += n;
            }
            if (all > 0.0) periodic = near / all;
        }
    o.value = excessAt.value_or (0.0);
    o.second = perMinute;
    o.third = shareAt.value_or (lowestShare);
    for (std::size_t k = 0; k < moments; ++k)
        if (loudest[k].excess >= 0.0) ++o.places;
    // The places in the order of the programme: three at most, sorted in place (no library sort — one may ask the heap).
    double at[3] { loudest[0].at, loudest[1].at, loudest[2].at };
    for (std::uint32_t i = 1; i < o.places; ++i)
        for (std::uint32_t j = i; j > 0 && at[j] < at[j - 1]; --j) std::swap (at[j], at[j - 1]);
    o.at1 = at[0]; o.at2 = at[1]; o.at3 = at[2];
    const double confidence = ! centred || ! shareAt ? 0.0
        : ramp (perMinute, number (config.find ("fromPerMinute")), number (config.find ("fullAtPerMinute")))
          * ramp (*shareAt, number (config.find ("fromShareOverDomeDb")), number (config.find ("fullAtShareOverDomeDb")))
          * (1.0 - ramp (periodic, number (config.find ("periodicFrom")), number (config.find ("periodicFullAt"))));
    if (! (confidence > 0.0)) return absent (o);
    return found (in, o, confidence, ramp (o.value, number (config.find ("severityFromDb")), number (config.find ("severityFullAtDb"))));
}

//==============================================================================
// THE HUM

// THE HUM, by the detector's own verdict per channel (analysis::HumReason): a channel it judged — valid, or a mains line
// measured that did not stand still (CandidateNotStationary) — has an answer; one it could not judge (too short, no quiet
// stretch to listen in, a comb without its base, …) has none, and a programme with no channel judged is not measured.
//   hum          the stationary line the detector accepted with the greatest prominence
//   humWandered  a candidate whose base line was found and prominent and that did not stand still
enum : unsigned { kHumOk = 0, kHumShorterThanWindow = 4, kHumNotStationary = 9 };
Observation hum (const ObservationInputs& in, bool wandered) noexcept
{
    auto o = start (in, wandered ? "humWandered" : "hum", HandledBy::Person, false);
    const auto* r = resultOf (in, Analyzer::Hum);
    if (! r || r->status == MeasurementStatus::Pending || r->status == MeasurementStatus::Cancelled)
        return unmeasured (o, r ? r->reason : MeasurementReason::Pending);
    const auto config = in.rules.engine.find ("observations");
    const auto humConfig = config.find ("hum");
    const double from = number (humConfig.find ("fromProminenceDb")), fullAt = number (humConfig.find ("fullAtProminenceDb"));
    std::optional<double> hz, prominence, power;
    bool judged = false, tooShort = true;
    for (unsigned c = 0; c < in.channels && c < 2; ++c)
    {
        const auto reason = carried (r, Channelled ("reason", c).view());
        const auto code = reason ? unsigned (*reason) : kHumShorterThanWindow;
        judged = judged || code == kHumOk || code == kHumNotStationary;
        tooShort = tooShort && code == kHumShorterThanWindow;
        if (wandered || code != kHumOk) continue;
        const auto p = carried (r, Channelled ("prominenceDb", c).view());
        const auto f = carried (r, Channelled ("fundamentalHz", c).view()), t = carried (r, Channelled ("tonePower", c).view());
        if (p && f && (! prominence || *p > *prominence)) { prominence = p; hz = f; power = t; }
    }
    if (! judged) return unmeasured (o, tooShort ? MeasurementReason::TooShort : MeasurementReason::NoSignal);
    if (const auto* candidates = carriedArray (r, "candidates"); wandered && candidates && candidates->columns == 36)
    {
        const auto rows = std::min<std::size_t> (std::size_t (candidates->stored), candidates->values.size() / 36);
        for (std::size_t i = 0; i < rows; ++i)
        {
            const double* v = candidates->values.data() + i * 36;
            // base.found, base.prominent; not stationary and not passed.
            if (v[18] > 0.5 && v[19] > 0.5 && ! (v[16] > 0.5) && ! (v[17] > 0.5) && std::isfinite (v[26])
                && (! prominence || v[26] > *prominence))
            { prominence = v[26]; hz = v[22]; power = v[23]; }
        }
    }
    if (! prominence || *prominence < from) return absent (o);
    o.value = *hz;
    o.second = *prominence;
    // The line's power against the programme's mean power.
    double severity = 0.0;
    const auto programme = read (resultOf (in, Analyzer::Programme), "programmeMeanSquare");
    if (power && *power > 0.0 && programme.value && *programme.value > 0.0)
    {
        const double against = *power / *programme.value;
        o.third = 10.0 * core::det::log10 (against);
        severity = ramp (against, 0.0, number (humConfig.find ("fullAtPowerAgainstProgramme")));
    }
    double confidence = ramp (*prominence, from, fullAt);
    if (wandered) confidence = std::min (confidence, number (config.find ("humWandered").find ("confidenceCeiling")));
    return found (in, o, confidence, severity);
}
} // namespace

bool regularlyClipped (const Rules& rules, double clipsPerMinute) noexcept
{
    return clipsPerMinute >= number (rules.engine.find ("limiter").find ("peakClipper").find ("clippedPerMinute"));
}

void observe (const ObservationInputs& in, Observations& out) noexcept
{
    out = {};
    if (in.measurements.empty()) return;
    out.clipping = clipping (in);
    out.dcOffset = dcOffset (in);
    out.bitsUnused = bitsUnused (in);
    out.dualMono = dualMono (in);
    out.edgeSilence = edgeSilence (in);
    out.tooQuiet = tooQuiet (in);
    out.tooShort = tooShort (in);
    out.alreadyLimited = alreadyLimited (in);
    out.spectralWall = spectralWall (in);
    out.loudestLowNote = lowNote (in, "loudestLowNote", "peakMidi", "peakNoteHz");
    out.lowestLowBand = lowNote (in, "lowestLowBand", "lowestOccupiedMidi", "lowestOccupiedHz");
    out.infraLow = infraLow (in);
    out.wideBass = wideBass (in);
    out.polarity = polarity (in);
    out.sibilance = sibilance (in);
    out.hum = hum (in, false);
    out.humWandered = hum (in, true);
}
} // namespace felitronics::session::detail

//==============================================================================
// THE OBSERVATIONS AS FACTS

namespace felitronics::session
{
const Observation& ObservationText::of (const Observations& all, ObservationKind kind) noexcept
{
    switch (kind)
    {
        case ObservationKind::Clipping:       return all.clipping;
        case ObservationKind::DcOffset:       return all.dcOffset;
        case ObservationKind::BitsUnused:     return all.bitsUnused;
        case ObservationKind::DualMono:       return all.dualMono;
        case ObservationKind::EdgeSilence:    return all.edgeSilence;
        case ObservationKind::TooQuiet:       return all.tooQuiet;
        case ObservationKind::TooShort:       return all.tooShort;
        case ObservationKind::AlreadyLimited: return all.alreadyLimited;
        case ObservationKind::SpectralWall:   return all.spectralWall;
        case ObservationKind::LoudestLowNote: return all.loudestLowNote;
        case ObservationKind::LowestLowBand:  return all.lowestLowBand;
        case ObservationKind::InfraLow:       return all.infraLow;
        case ObservationKind::WideBass:       return all.wideBass;
        case ObservationKind::Polarity:       return all.polarity;
        case ObservationKind::Sibilance:      return all.sibilance;
        case ObservationKind::Hum:            return all.hum;
        case ObservationKind::HumWandered:    return all.humWandered;
    }
    detail::storageOverflow();
}

std::optional<text::Fact> ObservationText::fact (ObservationKind kind, const Observation& o) noexcept
{
    using text::Arg; using text::Fact; using text::FactId; using text::Unit;
    if (o.status != ObservationStatus::Found) return std::nullopt;
    const auto at = [] (double s) { return Arg::value (s, Unit::S, 0); };
    const auto count = [] (double n) { return Arg::count (std::int64_t (n)); };
    switch (kind)
    {
        case ObservationKind::Clipping:
            if (detail::regularlyClipped (detail::rules(), o.second))
                return Fact::of (FactId::SourceClipped, count (o.value), Arg::value (o.second, Unit::None, 1));
            // Rarer clips are named by place: one or two places read as an edit, more as the first of several.
            if (o.places >= 3) return Fact::of (FactId::SourceClipsAt3, count (o.value), at (o.at1), at (o.at2), at (o.at3));
            if (o.places == 2) return Fact::of (FactId::SourceClipsAt2, count (o.value), at (o.at1), at (o.at2));
            if (o.places == 1) return Fact::of (FactId::SourceClipsAt1, count (o.value), at (o.at1));
            return Fact::of (FactId::SourceClips, count (o.value));
        case ObservationKind::DcOffset:       return Fact::of (FactId::SourceDc, Arg::value (o.value, Unit::None, 4));
        case ObservationKind::BitsUnused:     return Fact::of (FactId::SourceUnusedBits, count (o.value));
        case ObservationKind::DualMono:       return Fact::of (FactId::SourceDualMono);
        case ObservationKind::EdgeSilence:
            return Fact::of (FactId::SourceEdgeSilence, Arg::value (o.value, Unit::S, 1), Arg::value (o.second, Unit::S, 1));
        case ObservationKind::TooQuiet:
            return Fact::of (o.second > 0.5 ? FactId::SourceGainOnly : FactId::SourceQuiet, Arg::value (o.value, Unit::Lufs, 1));
        case ObservationKind::TooShort:
            return Fact::of (FactId::SourceShort, Arg::value (o.value, Unit::S, 1), Arg::value (o.second, Unit::S, 0));
        case ObservationKind::AlreadyLimited:
            if (o.third > 0.5) return Fact::of (FactId::SourceLimitedClipped, Arg::value (o.value, Unit::Db, 1));
            return Fact::of (FactId::SourceLimited, Arg::value (o.value, Unit::Db, 1), Arg::value (o.second, Unit::Db, 1));
        case ObservationKind::SpectralWall:
            return Fact::of (FactId::SourceWall, Arg::value (o.value / 1000.0, Unit::KHz, 1), Arg::value (o.second, Unit::Db, 0));
        case ObservationKind::LoudestLowNote: return Fact::of (FactId::LoudestLowNote, Arg::midi (std::int64_t (o.value)));
        case ObservationKind::LowestLowBand:
            return Fact::of (o.doubtful ? FactId::SourceLowestBandUnsure : FactId::SourceLowestBand, Arg::midi (std::int64_t (o.value)),
                             Arg::value (o.second, Unit::Hz, 1));
        case ObservationKind::InfraLow:
            return Fact::of (FactId::SourceInfraLow, Arg::value (100.0 * o.value, Unit::Percent, 1), Arg::value (o.second, Unit::Hz, 0));
        case ObservationKind::WideBass:       return Fact::of (FactId::WideBass, Arg::value (100.0 * o.value, Unit::Percent, 0));
        case ObservationKind::Polarity:       return Fact::of (FactId::SourcePolarity);
        case ObservationKind::Sibilance:
            if (o.places >= 3)
                return Fact::of (FactId::SourceSibilanceAt, Arg::value (o.value, Unit::Db, 1), Arg::value (o.second, Unit::None, 0),
                                 at (o.at1), at (o.at2), at (o.at3));
            return Fact::of (FactId::SourceSibilance, Arg::value (o.value, Unit::Db, 1), Arg::value (o.second, Unit::None, 0));
        case ObservationKind::Hum:
            return Fact::of (FactId::SourceHum, Arg::value (o.value, Unit::Hz, 1), Arg::value (o.second, Unit::Db, 0));
        case ObservationKind::HumWandered:
            return Fact::of (FactId::SourceHumWandered, Arg::value (o.value, Unit::Hz, 1), Arg::value (o.second, Unit::Db, 0));
    }
    detail::storageOverflow();
}
} // namespace felitronics::session
