// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE GLUE AND THE SATURATION (src/Dynamics.h): every number from the config read in place, every transcendental the
// deterministic one.

#include "BuildGuards.h"

#include "Dynamics.h"
#include "Devices.h"
#include "Grid.h"
#include "BuildContract.h"

#include <felitronics/core/DetMath.h>
#include <felitronics/tempo/TempoDetector.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <string_view>

namespace felitronics::session::detail
{
// A project's saturation type IS the core's shape: the same order, the same values (felitronics-core v0.57.0).
static_assert (int (SaturationType::Tanh) == int (saturation::WaveShaper::Shape::Tanh)
               && int (SaturationType::Atan) == int (saturation::WaveShaper::Shape::Atan)
               && int (SaturationType::Cubic) == int (saturation::WaveShaper::Shape::Cubic)
               && int (SaturationType::Asym) == int (saturation::WaveShaper::Shape::Asym)
               && int (SaturationType::Tube) == int (saturation::WaveShaper::Shape::Tube)
               && int (SaturationType::Transistor) == int (saturation::WaveShaper::Shape::Transistor)
               && int (SaturationType::Transformer) == int (saturation::WaveShaper::Shape::Transformer)
               && int (SaturationType::Tape) == int (saturation::WaveShaper::Shape::Tape));

namespace
{
using View = toml::embedded::View;
double number (View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    storageOverflow();
}
std::string_view text (View v) noexcept
{
    const auto s = v.string();
    if (! s) storageOverflow();
    return *s;
}
// A value of the travel: from `from` at 0 towards `to` at 1 and on, by its law.
double ramp (View r, double g) noexcept
{
    const double from = number (r.find ("from")), to = number (r.find ("to"));
    const auto law = text (r.find ("law"));
    if (law == "linear") return from + g * (to - from);
    if (law == "geometric") return from * core::det::exp2 (g * core::det::log2 (to / from));
    if (law == "byDepth")
    {
        // Even steps of 1 − 1/ratio, the share of the overshoot removed; never a whole share.
        const double depth = (1 - 1 / from) + g * ((1 - 1 / to) - (1 - 1 / from));
        return 1 / (1 - std::min (depth, 0.999));
    }
    storageOverflow();
}
double clampTo (View limits, std::string_view key, double x) noexcept
{
    const auto l = limits.find (key);
    return std::clamp (x, number (l.find ("min")), number (l.find ("max")));
}
std::optional<double> reading (const MeasurementResult* r, std::string_view name) noexcept
{
    if (r && r->status == MeasurementStatus::Ready)
        for (const auto& v : r->numbers)
            if (v.name == name && v.value && std::isfinite (*v.value)) return v.value;
    return {};
}
const MeasurementResult* resultOf (const PlanInputs& in, Analyzer analyzer) noexcept
{
    const auto i = std::size_t (analyzer);
    return i < in.measurements.size() ? &in.measurements[i] : nullptr;
}
} // namespace

GlueCurve glueAt (const Rules& rules, double travel) noexcept
{
    const auto glue = rules.engine.find ("glue");
    const auto limits = rules.engine.find ("compressor").find ("limits");
    GlueCurve c;
    c.travel = travel;
    c.ratio = ramp (glue.find ("ratio"), travel);
    c.threshOffsetDb = clampTo (limits, "threshOffset", ramp (glue.find ("threshOffset"), travel));
    c.kneeDb = clampTo (limits, "knee", ramp (glue.find ("knee"), travel));
    c.attackMs = clampTo (limits, "attack", ramp (glue.find ("attack"), travel));
    c.divisor = ramp (glue.find ("divisor"), travel);
    return c;
}

double glueStaticLossDb (const GlueCurve& curve) noexcept
{
    // The core's own curve, read at the P95: a level of 0 against a threshold threshOffset away.
    dynamics::GainComputer computer;
    computer.setMode (dynamics::Mode::DownCompress);
    computer.setThresholdDb (curve.threshOffsetDb);
    computer.setRatio (curve.ratio);
    computer.setKneeDb (curve.kneeDb);
    computer.setRangeDb (400.0);
    return -computer.deltaDb (0.0);
}

GlueCurve glueFor (const Rules& rules, double upToDb) noexcept
{
    // The loss grows with the travel without a break; 1.5 is past every knob of the domain (12.7 dB).
    double lo = 0.0, hi = 1.5;
    if (! (upToDb > 0.0)) return glueAt (rules, 0.0);
    for (int i = 0; i < 200; ++i)
    {
        const double mid = lo + (hi - lo) * 0.5;
        if (! (mid > lo && mid < hi)) break;
        if (glueStaticLossDb (glueAt (rules, mid)) < upToDb) lo = mid; else hi = mid;
    }
    return glueAt (rules, hi);
}

double glueReleaseMs (const Rules& rules, const GlueCurve& curve, double bpm) noexcept
{
    return clampTo (rules.engine.find ("compressor").find ("limits"), "release", 60000.0 / bpm / curve.divisor);
}

double glueBpmWhenUnsure (const Rules& rules) noexcept
{
    return number (rules.engine.find ("compressor").find ("tempo").find ("bpmWhenUnsure"));
}

InputLevels inputLevels (const PlanInputs& in) noexcept
{
    InputLevels out;
    const auto lufs = reading (resultOf (in, Analyzer::Loudness), "integratedLufs");
    if (! lufs) return out;
    out.gainDb = number (in.rules.engine.find ("input").find ("referenceLufs")) - *lufs;
    if (const auto peak = reading (resultOf (in, Analyzer::Loudness), "truePeakDb")) out.truePeakDb = *peak + *out.gainDb;
    if (const auto p95 = reading (resultOf (in, Analyzer::Programme), "shortTermP95")) out.p95Db = *p95 + *out.gainDb;
    return out;
}

std::optional<double> tempoHeard (const MeasurementResult& result) noexcept
{
    if (result.status != MeasurementStatus::Ready) return std::nullopt;
    for (const auto& value : result.numbers)
        if (value.value && value.name == "headlineBpm" && std::isfinite (*value.value) && *value.value > 0) return *value.value;
    return std::nullopt;
}

TempoChoice tempoChoice (const Rules& rules, const MeasurementResult& result) noexcept
{
    if (result.status == MeasurementStatus::Pending || result.status == MeasurementStatus::Cancelled)
        return { false, false, 0.0, result.reason };
    bool high = false;
    if (result.status == MeasurementStatus::Ready)
        for (const auto& value : result.numbers) if (value.value && value.name == "headlineLabel")
            high = std::bit_cast<std::uint64_t> (*value.value) == std::bit_cast<std::uint64_t> (double (tempo::ConfidenceLabel::High));
    const auto heard = tempoHeard (result);
    if (heard && high) return { true, true, *heard, MeasurementReason::None };
    // The fallback. A tempo heard but not trusted has no failure to name: the measurement is whole, the rule declined it
    // (reason None, measured false). NoSignal only where a ready result gave no tempo at all.
    const double fallback = number (rules.engine.find ("compressor").find ("tempo").find ("bpmWhenUnsure"));
    const auto reason = result.status != MeasurementStatus::Ready ? result.reason : heard ? MeasurementReason::None : MeasurementReason::NoSignal;
    return { true, false, fallback, reason };
}

bool glueTicked (const Layers<GlueFields>& glue) noexcept
{
    // The five by hand take no part: the amount they are laid over never moves with them, so a field left alone keeps its
    // value whatever another field holds.
    // A wished share (the waterfall) ticks a glue no person turned on or off.
    const auto& h = glue.hand;
    return (h.on ? *h.on : h.share.has_value()) && ! h.upToDb && ! (glue.machine.upToDb > 0.0);
}

double glueKnob (const Rules& rules, const Layers<GlueFields>& glue) noexcept
{
    // A person's tick on a knob the machine left at 0 gives way to whenTicked.
    return glueTicked (glue) ? number (rules.engine.find ("glue").find ("whenTicked")) : settingsOf (rules, glue).upToDb;
}

namespace
{
// The tick's character ([glue] ticked, owner 08.10) over the travel's numbers: the ratio, knee, attack and a fixed
// release, which follows no tempo — so the finding names none. The threshold stays the travel's at the amount.
void tickedCharacter (const Rules& rules, GlueFinding& f) noexcept
{
    const auto ticked = rules.engine.find ("glue").find ("ticked");
    f.ratio = number (ticked.find ("ratio"));
    f.kneeDb = number (ticked.find ("kneeDb"));
    f.attackMs = number (ticked.find ("attackMs"));
    f.releaseAskedMs = number (ticked.find ("releaseMs"));
    f.releaseMs = f.releaseAskedMs;
    f.releaseClamped = false;
    f.bpm.reset();
    f.tempoMeasured = false;
    f.tempoUnsureBpm.reset();
}
} // namespace

bool saturationTicked (const Layers<SaturationFields>& saturation) noexcept
{
    const auto& h = saturation.hand;
    return (h.on ? *h.on : h.share.has_value()) && ! h.drive && ! (saturation.machine.drive > 0.0);
}

double saturationKnob (const Rules& rules, const Layers<SaturationFields>& saturation) noexcept
{
    // A person's tick on a drive the machine left at 0, the drive untouched, gives way to [saturation] whenTicked.
    return saturationTicked (saturation) ? number (rules.engine.find ("saturation").find ("whenTicked"))
                                         : settingsOf (rules, saturation).drive;
}

GlueFinding glueLaw (const PlanInputs& in, double upToDb, bool waitsForTempo) noexcept
{
    GlueFinding f;
    f.upToDb = upToDb;
    const auto levels = inputLevels (in);
    const auto curve = glueFor (in.rules, upToDb);
    f.ratio = curve.ratio;
    f.kneeDb = curve.kneeDb;
    f.attackMs = curve.attackMs;
    // The calibrated place of the loud places on the detector's scale, then the travel's offset from it.
    if (levels.p95Db)
    {
        f.p95DetectorDb = *levels.p95Db + number (in.rules.engine.find ("glue").find ("detectorOverP95Db"));
        f.thresholdDb = *f.p95DetectorDb + curve.threshOffsetDb;
    }
    // The release follows the tempo once it is decided. Nothing measures the tempo of a glue out of the chain: its
    // release is stated at [compressor.tempo] bpmWhenUnsure until a tempo is decided; a glue in the chain waits for it.
    std::optional<TempoChoice> choice;
    std::optional<double> unsure;
    if (const auto* tempo = resultOf (in, Analyzer::Tempo))
        if (const auto c = tempoChoice (in.rules, *tempo); c.ready)
        {
            choice = c;
            if (! c.measured) unsure = tempoHeard (*tempo);
        }
    if (! choice && ! waitsForTempo)
        choice = TempoChoice { true, false, number (in.rules.engine.find ("compressor").find ("tempo").find ("bpmWhenUnsure")),
                               MeasurementReason::Pending };
    if (choice)
    {
        const double wanted = 60000.0 / choice->bpm / curve.divisor;
        const double release = glueReleaseMs (in.rules, curve, choice->bpm);
        f.bpm = choice->bpm;
        f.releaseAskedMs = wanted;
        f.releaseMs = release;
        f.releaseClamped = ! same (release, wanted);
        f.tempoMeasured = choice->measured;
        f.tempoUnsureBpm = unsure;
    }
    return f;
}

GlueFinding glueFinding (const PlanInputs& in, const Devices& devices) noexcept
{
    GlueFinding f;
    const bool offeredByShell = (in.offered & (1u << unsigned (Device::Glue))) != 0;
    const double upToDb = glueKnob (in.rules, devices.glue);
    const double mix = settingsOf (in.rules, devices.glue).mix;
    const bool wished = ! devices.glue.hand.on && devices.glue.hand.share.has_value();
    // THE WATERFALL: a wished share of 0 takes the glue out of the chain — a zone at 0 % does not sound.
    const bool zeroShare = devices.glue.hand.share.has_value() && ! (*devices.glue.hand.share > kZeroShare);
    const bool ticked = offeredByShell && (settingsOf (in.rules, devices.glue).on || wished) && upToDb > 0.0 && ! zeroShare;
    const auto levels = inputLevels (in);
    if (ticked && ! levels.p95Db) { f.upToDb = upToDb; f.mix = mix; f.state = GlueState::Unavailable; return f; }
    // In the chain, or out of it — unticked, at 0 dB, not offered — the knob's numbers as it stands (slice 5): what the
    // compressor gets, or would get, from it. Out of the chain they reach no compressor (writeDynamics reads Active).
    const auto state = ticked ? GlueState::Active : GlueState::Out;
    f = glueLaw (in, upToDb, state != GlueState::Out);
    if (glueTicked (devices.glue)) tickedCharacter (in.rules, f);
    f.state = state;
    f.mix = mix;
    // THE FIVE BY HAND: a person's field wins for that field alone, taken as written — the limits clamp the travel only.
    const auto& hand = devices.glue.hand;
    if (hand.ratio) f.ratio = *hand.ratio;
    if (hand.kneeDb) f.kneeDb = *hand.kneeDb;
    if (hand.attackMs) f.attackMs = *hand.attackMs;
    if (hand.thresholdDb) f.thresholdDb = *hand.thresholdDb;
    if (hand.releaseMs)
    {
        // A person's release follows no tempo: the finding names none, so no line says what the release was set for.
        f.releaseAskedMs = *hand.releaseMs;
        f.releaseMs = *hand.releaseMs;
        f.releaseClamped = false;
        f.bpm.reset();
        f.tempoMeasured = false;
        f.tempoUnsureBpm.reset();
    }
    return f;
}

GlueFinding glueMachine (const PlanInputs& in, const Devices& devices) noexcept
{
    auto f = glueLaw (in, glueKnob (in.rules, devices.glue), false);
    if (glueTicked (devices.glue)) tickedCharacter (in.rules, f);
    return f;
}

SaturationFinding saturationFinding (const PlanInputs& in, const Devices& devices) noexcept
{
    SaturationFinding f;
    const auto settings = settingsOf (in.rules, devices.saturation);
    const double drive = saturationKnob (in.rules, devices.saturation);
    f.knobDb = drive;
    const bool offeredByShell = (in.offered & (1u << unsigned (Device::Saturation))) != 0;
    const auto levels = inputLevels (in);
    const bool wished = ! devices.saturation.hand.on && devices.saturation.hand.share.has_value();
    // A wished share of 0 (the waterfall) takes the saturation out of the chain, as the glue's.
    const bool zeroShare = devices.saturation.hand.share.has_value() && ! (*devices.saturation.hand.share > kZeroShare);
    if (! offeredByShell || ! (settings.on || wished) || ! (drive > 0.0) || ! levels.truePeakDb || zeroShare) return f;
    f.active = true;
    f.peakDbTp = levels.truePeakDb;
    // The shaper's gain aligned: the knob is the drive at 0 dBTP, k = 10^(drive/20) − 1 scaled by the peak.
    f.driveDb = 20 * core::det::log10 (1 + (core::det::pow10 (drive / 20) - 1) * core::det::pow10 (-*levels.truePeakDb / 20));
    return f;
}

saturation::Saturator::Params clipperParams (const Rules& rules, SaturationType type, double driveDb, double mix) noexcept
{
    const auto saturation = rules.engine.find ("saturation");
    saturation::Saturator::Params s {};
    s.shape = saturation::WaveShaper::Shape (type);
    s.driveDb = float (driveDb);
    // The asymmetric diode's bias and its DC blocker (owner, 07.10): asym alone; every other type at 0, as before.
    const bool asym = type == SaturationType::Asym;
    s.bias = asym ? float (number (saturation.find ("bias"))) : 0.0f;
    s.mix = float (mix);
    s.outputDb = 0.0f;   // neutral: the landing sets the level before the limiter, so a trim here would be undone
    s.autoComp = float (number (saturation.find ("autoComp")));
    s.dcBlockHz = asym ? float (number (saturation.find ("dcBlockHz"))) : 0.0f;
    return s;
}

void writeDynamics (const PlanInputs& in, const Devices& devices, mastering::MasteringChainParams& params) noexcept
{
    const auto compressor = in.rules.engine.find ("compressor");
    const auto glue = glueFinding (in, devices);
    auto& c = params.compressor;
    c = {};
    const auto detector = text (compressor.find ("detector")), link = text (compressor.find ("link")), mode = text (compressor.find ("mode"));
    c.detector = detector == "rms" ? dynamics::Detector::Rms : dynamics::Detector::Peak;
    c.link = link == "max" ? dynamics::LinkMode::Max : dynamics::LinkMode::MeanPower;
    c.mode = mode == "downCompress" ? dynamics::Mode::DownCompress : mode == "upCompress" ? dynamics::Mode::UpCompress : dynamics::Mode::DownExpand;
    c.rmsWindowMs = number (compressor.find ("rmsWindowMs"));
    c.rangeDb = number (compressor.find ("rangeDb"));
    c.makeupDb = number (compressor.find ("makeupDb"));
    c.autoMakeup = compressor.find ("autoMakeup").boolean().value_or (false);
    c.lookaheadMs = 0.0;
    const bool compressing = glue.state == GlueState::Active && glue.releaseMs.has_value();
    // The glue's own mix (v0.17.0): the compressed share of its output, a person's or the machine's. A glue out of the
    // chain leaves the stage at 1, as before: its warm bypass passes the input at a gain of exactly 1, and the input
    // blended with itself at another share is rounded (0.6 x + 0.4 x is not x in binary), so a master without a glue
    // would move by a mix it never hears.
    params.compressorMix = compressing ? glue.mix : 1.0;
    params.bypassCompressor = ! compressing;
    if (compressing)
    {
        c.thresholdDb = *glue.thresholdDb;
        c.ratio = *glue.ratio;
        c.kneeDb = *glue.kneeDb;
        c.attackMs = *glue.attackMs;
        c.releaseMs = *glue.releaseMs;
    }

    const auto settings = settingsOf (in.rules, devices.saturation);
    const auto shaped = saturationFinding (in, devices);
    // The type as it sounds: a person's pick over the machine's, which is the config's [saturation] shape.
    params.clipper = clipperParams (in.rules, settings.type, shaped.driveDb.value_or (0.0), settings.mix);
    params.bypassClipper = ! shaped.active;
}

double glueTakenDb (double reductionDb, double mix) noexcept
{
    if (core::exactlyEqual (mix, 1.0)) return reductionDb;
    const double kept = (1.0 - mix) + mix * core::det::pow10 (-reductionDb / 20.0);
    return 0.0 - 20.0 * core::det::log10 (kept);
}
} // namespace felitronics::session::detail
