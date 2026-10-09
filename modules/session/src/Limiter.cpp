// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE LIMITER, ITS NEEDLES AND THE DITHER (src/Limiter.h): every number from the config read in place.

#include "BuildGuards.h"

#include "Limiter.h"
#include "Devices.h"
#include "Dynamics.h"
#include "Grid.h"
#include "Observations.h"
#include "BuildContract.h"

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
bool offeredByShell (const PlanInputs& in, Device device) noexcept { return (in.offered & (1u << unsigned (device))) != 0; }
// The dither's seed: sixteen hexadecimal digits, which the build's gate checked.
std::uint64_t seedOf (View v) noexcept
{
    const auto text = v.string();
    if (! text || text->size() != 16) storageOverflow();
    std::uint64_t bits = 0;
    for (const char c : *text)
    {
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) storageOverflow();
        bits = (bits << 4) | std::uint64_t (digit);
    }
    return bits;
}
} // namespace

std::optional<double> loudnessNeed (const Rules& rules, std::uint16_t row, const TargetFields<Touched>& edit,
                                    const MeasurementResult& loudness) noexcept
{
    const auto lufs = reading (&loudness, "integratedLufs"), peak = reading (&loudness, "truePeakDb");
    if (! lufs || ! peak) return {};
    const auto target = rules.row (row);
    const double targetLufs = edit.lufs ? *edit.lufs : target.lufs.toDouble();
    const double targetTp = edit.tp ? *edit.tp : target.tp.toDouble();
    return (*peak - *lufs) - (targetTp - targetLufs);
}

NeedlesKnob needlesKnob (const Layers<LimiterFields>& limiter, bool withHand) noexcept
{
    NeedlesKnob knob { limiter.machine.needles, limiter.machine.needlesDb, false };
    if (! withHand) return knob;
    if (limiter.hand.needlesDb) knob.overDb = *limiter.hand.needlesDb;
    if (limiter.hand.needles) { knob.mode = *limiter.hand.needles; knob.byHand = true; }
    else if (limiter.hand.needlesDb) { knob.mode = Needles::Manual; knob.byHand = true; }
    return knob;
}

NeedlesAnswer needlesAnswer (const PlanInputs& in) noexcept
{
    NeedlesAnswer a;
    const auto clipper = in.rules.engine.find ("limiter").find ("peakClipper");
    const auto target = in.rules.row (in.row);
    const auto* loudness = resultOf (in, Analyzer::Loudness);
    // What is known whatever the class: the need, the input's PLR and the source's confirmed clips.
    if (loudness)
    {
        a.needDb = loudnessNeed (in.rules, in.row, in.targetEdit, *loudness);
        const auto lufs = reading (loudness, "integratedLufs"), peak = reading (loudness, "truePeakDb");
        if (lufs && peak) a.plrDb = *peak - *lufs;
    }
    if (const auto clips = reading (resultOf (in, Analyzer::Clipping), "runCount"))
    {
        a.clips = std::uint64_t (*clips);
        if (in.sampleRate != 0 && in.frames != 0)
            a.clipsPerMinute = *clips * 60.0 * double (in.sampleRate) / double (in.frames);
    }
    const auto off = [&] (NeedlesWhy why) { a.proposed = NeedlesClass::None; a.why = why; return a; };
    if (! offeredByShell (in, Device::Limiter)) return off (NeedlesWhy::Shell);
    if (target.noClipper) return off (NeedlesWhy::Target);
    if (quietInput (in)) return off (NeedlesWhy::Quiet);
    if (! a.needDb || ! a.plrDb || ! std::isfinite (*a.needDb)) return off (NeedlesWhy::NoReadings);
    if (*a.needDb <= number (clipper.find ("littleNeedDb"))) return off (NeedlesWhy::LittleNeed);
    // A clipped source is ruled out whatever its needles are: its answer does not wait for them.
    if (a.clipsPerMinute && regularlyClipped (in.rules, *a.clipsPerMinute)) return off (NeedlesWhy::Clipped);
    const auto* needles = resultOf (in, Analyzer::Excursions);
    if (! needles || ! in.needlesCurrent || needles->status == MeasurementStatus::Pending) return off (NeedlesWhy::Pending);
    if (needles->status != MeasurementStatus::Ready)
    {
        a.reason = needles->reason;
        return off (NeedlesWhy::Unmeasured);
    }
    const auto runs = reading (needles, "runCount"), p90 = reading (needles, "p90Ms"), bass = reading (needles, "bassDoseShare");
    if (! runs || ! p90 || ! bass)
    {
        a.reason = MeasurementReason::Unsupported;
        return off (NeedlesWhy::Unmeasured);
    }
    if (! (*runs > 0.0)) return off (NeedlesWhy::NoExcursions);
    a.p90Ms = p90;
    a.bassShare = bass;
    // Ruled out first — any one of them is enough.
    if (*a.plrDb < number (clipper.find ("longPlrDb"))) return off (NeedlesWhy::LowPlr);
    if (*bass >= number (clipper.find ("longBassShare"))) return off (NeedlesWhy::Bass);
    if (*p90 >= number (clipper.find ("longP90Ms"))) return off (NeedlesWhy::Long);
    const bool isShort = *p90 <= number (clipper.find ("shortP90Ms")) && *bass <= number (clipper.find ("shortBassShare"))
                      && *a.plrDb >= number (clipper.find ("shortPlrDb"));
    a.proposed = isShort ? NeedlesClass::Short : NeedlesClass::Between;
    a.why = NeedlesWhy::Cuts;
    a.overDb = kept (number (clipper.find (isShort ? "shortCutDb" : "betweenCutDb")));
    return a;
}

LimiterFinding limiterFinding (const PlanInputs& in, const Devices& devices) noexcept
{
    LimiterFinding f;
    const auto target = in.rules.row (in.row);
    const auto answer = needlesAnswer (in);
    f.ceilingDbTp = in.targetEdit.tp ? *in.targetEdit.tp : target.tp.toDouble();
    f.needDb = answer.needDb;
    f.proposed = answer.proposed;
    f.why = answer.why;
    f.reason = answer.reason;
    f.proposedOverDb = answer.overDb;
    f.p90Ms = answer.p90Ms;
    f.bassShare = answer.bassShare;
    f.plrDb = answer.plrDb;
    f.clipsPerMinute = answer.clipsPerMinute;
    f.clips = answer.clips;

    const auto knob = needlesKnob (devices.limiter);
    f.mode = knob.mode;
    switch (knob.mode)
    {
        case Needles::Off:    break;
        case Needles::Manual: f.cutting = true; f.overDb = knob.overDb; break;
        case Needles::Auto:
            f.cutting = answer.proposed != NeedlesClass::None;
            if (f.cutting) f.overDb = *answer.overDb;
            break;
    }
    // A wished cut share (the waterfall) cuts needles no person set: from the knob's amount, the landing steers it.
    // A wished cut share of 0 takes the needles' clipper out of the chain — a zone at 0 % does not sound.
    if (devices.limiter.hand.cutShare && ! (*devices.limiter.hand.cutShare > detail::kZeroShare)) f.cutting = false;
    else if (devices.limiter.hand.cutShare && ! devices.limiter.hand.needles && ! f.cutting) { f.cutting = true; f.overDb = knob.overDb; }
    // Only a device the shell offers is a person's to turn; placement took a person's layer off one it does not.
    if (! offeredByShell (in, Device::Limiter)) { f.cutting = false; f.mode = Needles::Off; }
    const bool machineCuts = answer.proposed != NeedlesClass::None;
    const bool asProposed = f.cutting == machineCuts && (! f.cutting || same (f.overDb, *answer.overDb));
    const bool touched = devices.limiter.hand.needles.has_value() || devices.limiter.hand.needlesDb.has_value();
    f.sounding = asProposed ? Sounding::Proposal : touched ? Sounding::Hand : Sounding::File;
    // The machine's reason is said beside a threshold that cuts against it — once the machine has an answer.
    f.againstMachine = f.cutting && knob.mode == Needles::Manual && ! machineCuts && answer.why != NeedlesWhy::Pending;

    f.vinyl = target.vinyl;
    f.mediumCeilingDbTp = target.tp.toDouble();
    f.ceilingAboveMedium = target.vinyl && f.ceilingDbTp > f.mediumCeilingDbTp;
    f.needlesAgainstMedium = target.vinyl && f.cutting;
    f.vinylTopHz = target.vinyl ? number (in.rules.engine.find ("observations").find ("vinylTop").find ("aboveHz")) : 0.0;

    // The limiter's own settings, as writeLimiter and the chain's topology take them: the release, the lookahead and
    // the oversampling as they sound — a person's field where set (08.10), else the machine's, the constants.
    const auto limiter = in.rules.engine.find ("limiter");
    const auto chain = in.rules.engine.find ("chain");
    const auto sounding = settingsOf (in.rules, devices.limiter);
    f.releaseMs = sounding.releaseMs;
    f.dualRelease = limiter.find ("dualRelease").boolean().value_or (false);
    f.slowReleaseMs = number (limiter.find ("slowReleaseMs"));
    f.lookaheadMs = sounding.lookaheadMs;
    limiter::TruePeakLimiterConfig taken;
    taken.lookaheadMs = f.lookaheadMs;
    taken.oversampleFactor = int (sounding.oversampling);
    taken.tapsPerPhase = int (number (chain.find ("tapsPerPhase")));
    f.oversampling = std::int32_t (limiter::TruePeakLimiter::oversampleFactorFor (taken));
    return f;
}

// The plan's shaping IS the core's: the same order, the same values.
static_assert (int (DitherShaping::None) == int (dither::NoiseShaping::None)
               && int (DitherShaping::Weighted) == int (dither::NoiseShaping::Weighted)
               && int (DitherShaping::Psychoacoustic) == int (dither::NoiseShaping::Psychoacoustic));

DitherFinding ditherFinding (const PlanInputs& in, const Devices& devices) noexcept
{
    DitherFinding f;
    f.bits = in.rules.row (in.row).bitDepth;
    f.applies = offered (in.rules, in.row, in.channels, Device::Dither) && offeredByShell (in, Device::Dither);
    const bool ticked = settingsOf (in.rules, devices.dither).on;
    f.on = f.applies && ticked;
    f.offByHand = f.applies && ! ticked && devices.dither.hand.on.has_value();
    f.keptWithoutEffect = ! f.applies && devices.dither.hand.on.has_value();
    const auto dither = in.rules.engine.find ("dither");
    const auto shaping = dither.find ("shaping").string();
    const auto shapingUpTo = dither.find ("shapingUpToBits").integer();
    if (! shaping || ! shapingUpTo) storageOverflow();
    f.shaping = f.bits > *shapingUpTo || *shaping == "none" ? DitherShaping::None
              : *shaping == "weighted" ? DitherShaping::Weighted : DitherShaping::Psychoacoustic;
    return f;
}

void writeLimiter (const PlanInputs& in, const Devices& devices, mastering::MasteringChainParams& params) noexcept
{
    const auto limiter = in.rules.engine.find ("limiter");
    const auto clipper = limiter.find ("peakClipper");
    const auto finding = limiterFinding (in, devices);
    auto& l = params.limiter;
    l = {};
    l.ceilingDbTp = finding.ceilingDbTp;
    l.releaseMs = finding.releaseMs;
    l.dualRelease = finding.dualRelease;
    l.slowReleaseMs = finding.slowReleaseMs;
    // The clipper cuts AT MOST its amount off the peaks and the limiter does the rest (owner, 30.09): the input's need
    // stands the peaks that far above the ceiling, so the threshold is max(0, need − cut) above it. Where the need is
    // not known, or that threshold lies beyond the limiter's working range, no threshold it can take keeps to the
    // amount: the clipper stays off. With it off the threshold is not read; it is stated all the same, as it always was:
    // the "between" class's number.
    // That threshold is a forecast for one gain, the landing's first, and from the input's true peak. A landing settles
    // elsewhere (it adds the gain the limiter takes off the loudness, it lowers its pass ceiling), and the stages before
    // the limiter move the peaks. So the chain is given the cut and the peak instead (MasteringChainParams::peakClipCutDb,
    // peakClipPeakDb) and works the threshold out for the gain and the ceiling it renders at; the landing replaces the
    // forecast peak with the one its first pass measured at the limiter's input. The forecast: the input's PLR over the
    // loudness it is normalised to, referenceLufs — the need plus the target's headroom, need + ceiling − targetLufs.
    const double threshold = finding.needDb ? std::max (0.0, *finding.needDb - finding.overDb) : 0.0;
    l.peakClip = finding.cutting && finding.needDb && threshold <= limiter::TruePeakLimiter::kMaxOverCeilingDb;
    l.overCeilingDb = l.peakClip ? threshold : number (clipper.find ("betweenCutDb"));
    l.kneeDb = number (clipper.find ("kneeDb"));
    const auto target = in.rules.row (in.row);
    const double targetLufs = in.targetEdit.lufs ? *in.targetEdit.lufs : target.lufs.toDouble();
    // Stated only where the clipper sounds; NaN leaves the chain the threshold above, which it does not read then.
    const double none = std::numeric_limits<double>::quiet_NaN();
    params.peakClipCutDb = l.peakClip ? finding.overDb : none;
    params.peakClipPeakDb = l.peakClip
        ? (*finding.needDb + finding.ceilingDbTp - targetLufs) + number (in.rules.engine.find ("input").find ("referenceLufs")) : none;
    params.bypassLimiter = false;

    const auto dither = in.rules.engine.find ("dither");
    const auto sounding = ditherFinding (in, devices);
    auto& d = params.dither;
    d = {};
    d.bits = sounding.bits;
    d.shaping = dither::NoiseShaping (sounding.shaping);
    d.seed = seedOf (dither.find ("seed"));
    d.autoBlank = dither.find ("autoBlank").boolean().value_or (false);
    d.autoBlankSamples = int (number (dither.find ("autoBlankSamples")));
    params.bypassDither = false;
}
} // namespace felitronics::session::detail
