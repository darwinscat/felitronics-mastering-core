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

TempoChoice tempoChoice (const Rules& rules, const MeasurementResult& result) noexcept
{
    if (result.status == MeasurementStatus::Pending || result.status == MeasurementStatus::Cancelled)
        return { false, false, 0.0, result.reason };
    double bpm = 0.0; bool hasBpm = false, high = false;
    if (result.status == MeasurementStatus::Ready)
        for (const auto& value : result.numbers) if (value.value)
        {
            if (value.name == "headlineBpm") { bpm = *value.value; hasBpm = true; }
            else if (value.name == "headlineLabel")
                high = std::bit_cast<std::uint64_t> (*value.value)
                    == std::bit_cast<std::uint64_t> (double (tempo::ConfidenceLabel::High));
        }
    if (result.status == MeasurementStatus::Ready && hasBpm && high && std::isfinite (bpm) && bpm > 0)
        return { true, true, bpm, MeasurementReason::None };
    const double fallback = number (rules.engine.find ("compressor").find ("tempo").find ("bpmWhenUnsure"));
    const auto reason = result.status == MeasurementStatus::Ready ? MeasurementReason::NoSignal : result.reason;
    return { true, false, fallback, reason };
}

double glueKnob (const Rules& rules, const Layers<GlueFields>& glue) noexcept
{
    const auto settings = settingsOf (rules, glue);
    const bool tickedUntouched = glue.hand.on && *glue.hand.on && ! glue.hand.upToDb && ! (glue.machine.upToDb > 0.0);
    return tickedUntouched ? number (rules.engine.find ("glue").find ("whenTicked")) : settings.upToDb;
}

GlueFinding glueFinding (const PlanInputs& in, const Devices& devices) noexcept
{
    GlueFinding f;
    const bool offeredByShell = (in.offered & (1u << unsigned (Device::Glue))) != 0;
    f.upToDb = glueKnob (in.rules, devices.glue);
    if (! offeredByShell || ! settingsOf (in.rules, devices.glue).on || ! (f.upToDb > 0.0)) return f;
    const auto levels = inputLevels (in);
    if (! levels.p95Db) { f.state = GlueState::Unavailable; return f; }
    const auto curve = glueFor (in.rules, f.upToDb);
    f.state = GlueState::Active;
    f.ratio = curve.ratio;
    // The calibrated place of the loud places on the detector's scale, then the travel's offset from it.
    f.thresholdDb = *levels.p95Db + number (in.rules.engine.find ("glue").find ("detectorOverP95Db")) + curve.threshOffsetDb;
    f.kneeDb = curve.kneeDb;
    f.attackMs = curve.attackMs;
    if (const auto* tempo = resultOf (in, Analyzer::Tempo))
        if (const auto choice = tempoChoice (in.rules, *tempo); choice.ready)
        {
            const double wanted = 60000.0 / choice.bpm / curve.divisor;
            const double release = clampTo (in.rules.engine.find ("compressor").find ("limits"), "release", wanted);
            f.bpm = choice.bpm;
            f.releaseAskedMs = wanted;
            f.releaseMs = release;
            f.releaseClamped = ! same (release, wanted);
            f.tempoMeasured = choice.measured;
        }
    return f;
}

SaturationFinding saturationFinding (const PlanInputs& in, const Devices& devices) noexcept
{
    SaturationFinding f;
    const auto settings = settingsOf (in.rules, devices.saturation);
    f.knobDb = settings.drive;
    const bool offeredByShell = (in.offered & (1u << unsigned (Device::Saturation))) != 0;
    const auto levels = inputLevels (in);
    if (! offeredByShell || ! settings.on || ! (settings.drive > 0.0) || ! levels.truePeakDb) return f;
    f.active = true;
    f.peakDbTp = levels.truePeakDb;
    // The shaper's gain aligned: the knob is the drive at 0 dBTP, k = 10^(drive/20) − 1 scaled by the peak.
    f.driveDb = 20 * core::det::log10 (1 + (core::det::pow10 (settings.drive / 20) - 1) * core::det::pow10 (-*levels.truePeakDb / 20));
    return f;
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
    params.compressorMix = number (compressor.find ("mix"));
    const bool compressing = glue.state == GlueState::Active && glue.releaseMs.has_value();
    params.bypassCompressor = ! compressing;
    if (compressing)
    {
        c.thresholdDb = *glue.thresholdDb;
        c.ratio = *glue.ratio;
        c.kneeDb = *glue.kneeDb;
        c.attackMs = *glue.attackMs;
        c.releaseMs = *glue.releaseMs;
    }

    const auto saturation = in.rules.engine.find ("saturation");
    const auto settings = settingsOf (in.rules, devices.saturation);
    const auto shaped = saturationFinding (in, devices);
    auto& s = params.clipper;
    s = {};
    const auto shape = text (saturation.find ("shape"));
    s.shape = shape == "tanh" ? saturation::WaveShaper::Shape::Tanh : shape == "atan" ? saturation::WaveShaper::Shape::Atan
            : shape == "cubic" ? saturation::WaveShaper::Shape::Cubic : saturation::WaveShaper::Shape::Asym;
    s.driveDb = float (shaped.driveDb.value_or (0.0));
    s.bias = float (number (saturation.find ("bias")));
    s.mix = float (settings.mix);
    s.outputDb = float (settings.output);
    s.autoComp = float (number (saturation.find ("autoComp")));
    s.dcBlockHz = float (number (saturation.find ("dcBlockHz")));
    params.bypassClipper = ! shaped.active;
}
} // namespace felitronics::session::detail
