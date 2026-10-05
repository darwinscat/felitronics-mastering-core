// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE PURE KIT (include/felitronics/session/Kit.h): stateless answers for a shell's UI thread. Nothing here owns a rule:
// every answer is read off the code and the config the session already uses for the same question.

#include "BuildGuards.h"
#include <felitronics/session/Kit.h>

#include "BuildContract.h"
#include "Devices.h"
#include "Dynamics.h"
#include "EqCurve.h"
#include "Grid.h"
#include "JsonCodec.h"
#include "Rules.h"

#include <felitronics/core/DetMath.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>

namespace felitronics::session
{
namespace
{
using detail::Knob;
using detail::Rules;
using text::Term;
using toml::Decimal;
using View = toml::embedded::View;

bool environmentOk() noexcept { return Session::checkFloatingPointEnvironment() == Status::Ok; }

// A number of a build-checked document: an integer exactly, a decimal by its one correctly rounded division.
double number (View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto n = v.integer()) return double (*n);
    detail::storageOverflow();
}

// A range of a document, written as [min, max] or as { min, max }.
void span (View v, double& min, double& max) noexcept
{
    if (v.is (toml::embedded::Type::Array)) { min = number (v[0]); max = number (v[1]); }
    else { min = number (v.find ("min")); max = number (v.find ("max")); }
}

// Every gain of the EQ bands finite and in its command's domain.
bool bandsAccepted (const BandsFields<Value>& bands, const Rules& r) noexcept
{
    const double gains[] { bands.body, bands.mud, bands.forward, bands.brightness, bands.air };
    for (std::size_t i = 0; i < std::size (gains); ++i)
        if (! std::isfinite (gains[i]) || ! r.bands[i].accepts (gains[i], 0)) return false;
    return true;
}

// The knob a field turns — the one the commands check that field with — or null.
const Knob* knobOf (const Rules& r, Term field) noexcept
{
    // An if-chain, not a switch: Term names every word of the catalogue, and the kit answers for eighteen of them.
    if (field == Term::FieldTargetLufs) return &r.lufs;
    if (field == Term::FieldTargetTp) return &r.tp;
    if (field == Term::FieldHpfFq) return &r.hpfFq;
    if (field == Term::FieldMonoBassFq) return &r.monoBassFq;
    if (field == Term::FieldMonoBassWidth) return &r.monoBassWidth;
    if (field == Term::FieldGlueUpToDb) return &r.glue;
    if (field == Term::FieldGlueMix) return &r.glueMix;
    if (field == Term::FieldSaturationDrive) return &r.drive;
    if (field == Term::FieldSaturationMix) return &r.mix;
    if (field == Term::FieldTiltDb) return &r.tilt;
    if (field == Term::FieldLimiterNeedlesDb) return &r.needles;
    if (field == Term::FieldLowDb) return &r.low;
    if (field == Term::FieldBandsBody) return &r.bands[0];
    if (field == Term::FieldBandsMud) return &r.bands[1];
    if (field == Term::FieldBandsForward) return &r.bands[2];
    if (field == Term::FieldBandsBrightness) return &r.bands[3];
    if (field == Term::FieldBandsAir) return &r.bands[4];
    return nullptr;
}

// A knob's comfortable window and the outer bounds its heat runs to (Kit.h, KitHeat).
struct Window
{
    double low = 0.0, high = 0.0, outLow = 0.0, outHigh = 0.0;
};

std::optional<Window> windowOf (const Rules& r, Term field) noexcept
{
    Window w;
    if (field == Term::FieldTargetLufs || field == Term::FieldTargetTp)
    {
        const View edit = r.targets.find ("edit").find (field == Term::FieldTargetLufs ? "lufs" : "tp");
        span (edit.find ("green"), w.low, w.high);
        w.outLow = number (edit.find ("from"));
        w.outHigh = number (edit.find ("to"));
        return w;
    }
    if (field == Term::FieldHpfFq)
    {
        const View comfort = r.engine.find ("hpf").find ("comfort");
        w.low = number (comfort.find ("lowHz"));
        w.high = number (comfort.find ("highHz"));
        w.outLow = number (comfort.find ("warningLowHz"));
        w.outHigh = number (comfort.find ("warningHighHz"));
        return w;
    }
    if (field == Term::FieldTiltDb || field == Term::FieldLowDb)
    {
        const View device = r.engine.find (field == Term::FieldTiltDb ? "tilt" : "low");
        span (device.find ("normal"), w.low, w.high);
        span (device.find ("hard"), w.outLow, w.outHigh);
        return w;
    }
    // The five EQ bands, coloured as tilt and low are: their normal, out to their travel's ends.
    constexpr std::pair<Term, std::string_view> bands[] { { Term::FieldBandsBody, "body" }, { Term::FieldBandsMud, "mud" },
        { Term::FieldBandsForward, "forward" }, { Term::FieldBandsBrightness, "brightness" }, { Term::FieldBandsAir, "air" } };
    for (const auto& [term, name] : bands)
        if (field == term)
        {
            const View device = r.engine.find ("bands").find (name);
            span (device.find ("normal"), w.low, w.high);
            span (device.find ("hard"), w.outLow, w.outHigh);
            return w;
        }
    return std::nullopt;
}

// THE KNOB'S GRID, exactly: x moved to the nearest from + k·step, a value exactly halfway taken away from zero (Text's
// rounding), all three brought to one scale and nothing but integers taking part. The result is given back at the fewest
// places it needs; nothing when a number leaves the exact range on the way (src/Grid.h's limits).
std::optional<Decimal> nearestOnGrid (const Decimal& x, const Decimal& from, const Decimal& step) noexcept
{
    const int scale = std::max ({ int (x.scale), int (from.scale), int (step.scale) });
    std::int64_t a = 0, f = 0, b = 0;
    if (! detail::scaleUp (x.mantissa, scale - int (x.scale), a) || ! detail::scaleUp (from.mantissa, scale - int (from.scale), f)
        || ! detail::scaleUp (step.mantissa, scale - int (step.scale), b) || b <= 0)
        return std::nullopt;
    constexpr std::int64_t half = std::numeric_limits<std::int64_t>::max() / 2;
    if (a > half || a < -half || f > half || f < -half) return std::nullopt;
    const std::int64_t d = a - f;
    std::int64_t q = d / b, r = d % b;
    if (r < 0) { r += b; --q; }
    std::int64_t m = f + q * b;
    if (r != 0 && (r > b - r || (r == b - r && a > 0))) m += b;
    int places = scale;
    while (places > 1 && m % 10 == 0) { m /= 10; --places; }
    const Decimal out { m, std::uint8_t (places), false };
    if (! out.valid()) return std::nullopt;
    return out;
}

// A typed number that carries its own sign: a minus ("−" or "-") or a plus after the ASCII spaces Text::parse skips.
bool signTyped (std::string_view typed) noexcept
{
    std::size_t i = 0;
    while (i < typed.size() && typed[i] == ' ') ++i;
    const auto rest = typed.substr (i);
    return rest.starts_with ('-') || rest.starts_with ('+') || rest.starts_with ("\xE2\x88\x92");
}

bool rateOk (double rate) noexcept
{
    return std::isfinite (rate) && rate >= double (kMinSampleRate) && rate <= 4294967295.0 && detail::same (std::floor (rate), rate);
}
} // namespace

KitCount Kit::text (std::string_view wireFact, text::Lang lang, std::span<char> out) noexcept
{
    if (! environmentOk()) return { CodecStatus::FloatingPointEnvironment, 0 };
    if (unsigned (lang) >= text::kLangCount) return { CodecStatus::Invalid, 0 };
    detail::Storage storage;
    detail::Reader reader { wireFact, storage, 0, true, {}, 0, 0, false };
    text::Fact fact;
    reader.value (fact);
    reader.space();
    if (! reader.good || reader.pos != wireFact.size()) return { CodecStatus::Invalid, 0 };
    // A fact short of an argument its message needs, or with one of another kind, is no fact: refused, never its
    // template.
    if (! text::Text::complete (fact)) return { CodecStatus::Invalid, 0 };
    const std::size_t size = text::Text::size (fact, lang);
    if (out.size() < size) return { CodecStatus::TooSmall, size };
    return { CodecStatus::Ok, text::Text::write (fact, lang, out) };
}

KitParsed Kit::parse (std::string_view typed, text::Lang lang, Term field, std::uint32_t sourceRate) noexcept
{
    KitParsed out;
    if (! environmentOk()) { out.status = CodecStatus::FloatingPointEnvironment; return out; }
    if (unsigned (lang) >= text::kLangCount) { out.status = CodecStatus::Invalid; return out; }
    const Rules rules = detail::rules();
    const Knob* knob = knobOf (rules, field);
    if (knob == nullptr && field != Term::FieldHpfSlope) { out.status = CodecStatus::Invalid; return out; }
    const auto read = text::Text::parse (typed, lang);
    if (! read) { out.refusal = KitRefusal::NotANumber; return out; }
    out.refusal = KitRefusal::OutOfDomain;
    if (knob == nullptr)
    {
        // The slope: a whole number of dB/oct, as written — never moved to the nearest choice (engine.toml [hpf]).
        if (! (std::fabs (*read) < 2147483648.0) || ! detail::same (std::floor (*read), *read)
            || ! rules.slope (std::int32_t (*read))) return out;
        out.refusal = KitRefusal::None;
        out.value = detail::kept (*read);
        return out;
    }
    double value = *read;
    if (! signTyped (typed) && detail::compare (knob->to, Decimal { 0, 1, false }) <= 0) value = -value;
    const auto typedDecimal = detail::decimalOf (value);
    if (! typedDecimal) return out;
    const auto onGrid = nearestOnGrid (*typedDecimal, knob->from, knob->step);
    if (! onGrid) return out;
    const double v = detail::kept (onGrid->toDouble());
    if (! knob->accepts (v, sourceRate)) return out;
    out.refusal = KitRefusal::None;
    out.value = v;
    return out;
}

KitTravel Kit::travel (Term field) noexcept
{
    if (! environmentOk()) return { CodecStatus::FloatingPointEnvironment };
    const Rules rules = detail::rules();
    const Knob* knob = knobOf (rules, field);
    if (knob == nullptr) return { CodecStatus::Invalid };
    return { CodecStatus::Ok, knob->from.toDouble(), knob->to.toDouble(), knob->step.toDouble() };
}

KitNumber Kit::position (Term field, double value) noexcept
{
    if (! environmentOk()) return { CodecStatus::FloatingPointEnvironment };
    const Rules rules = detail::rules();
    const Knob* knob = knobOf (rules, field);
    if (knob == nullptr || ! std::isfinite (value)) return { CodecStatus::Invalid };
    const double from = knob->from.toDouble(), to = knob->to.toDouble();
    if (detail::same (from, to)) return { CodecStatus::Ok, 0.0 };
    return { CodecStatus::Ok, std::clamp ((value - from) / (to - from), 0.0, 1.0) };
}

KitNumber Kit::valueAt (Term field, double position) noexcept
{
    if (! environmentOk()) return { CodecStatus::FloatingPointEnvironment };
    const Rules rules = detail::rules();
    const Knob* knob = knobOf (rules, field);
    if (knob == nullptr || ! std::isfinite (position)) return { CodecStatus::Invalid };
    const auto& from = knob->from;
    const auto& to = knob->to;
    const auto& step = knob->step;
    const int scale = std::max ({ int (from.scale), int (to.scale), int (step.scale) });
    std::int64_t f = 0, t = 0, b = 0;
    if (! detail::scaleUp (from.mantissa, scale - int (from.scale), f) || ! detail::scaleUp (to.mantissa, scale - int (to.scale), t)
        || ! detail::scaleUp (step.mantissa, scale - int (step.scale), b) || b <= 0 || t < f)
        return { CodecStatus::Invalid };
    const std::int64_t steps = (t - f) / b;
    const double along = std::clamp (position, 0.0, 1.0) * double (steps);
    const auto k = std::int64_t (std::floor (along + 0.5));
    std::int64_t m = f + std::min (k, steps) * b;
    int places = scale;
    while (places > 1 && m % 10 == 0) { m /= 10; --places; }
    const Decimal at { m, std::uint8_t (places), false };
    if (! at.valid()) return { CodecStatus::Invalid };
    return { CodecStatus::Ok, detail::kept (at.toDouble()) };
}

KitHeat Kit::heat (Term field, double value) noexcept
{
    KitHeat out;
    if (! environmentOk()) { out.status = CodecStatus::FloatingPointEnvironment; return out; }
    const Rules rules = detail::rules();
    if (knobOf (rules, field) == nullptr || ! std::isfinite (value)) { out.status = CodecStatus::Invalid; return out; }
    const auto w = windowOf (rules, field);
    if (! w) return out;
    out.window = true;
    if (value < w->low)
    {
        out.side = -1;
        out.heat = w->low > w->outLow ? std::min (1.0, (w->low - value) / (w->low - w->outLow)) : 1.0;
    }
    else if (value > w->high)
    {
        out.side = 1;
        out.heat = w->outHigh > w->high ? std::min (1.0, (value - w->high) / (w->outHigh - w->high)) : 1.0;
    }
    return out;
}

KitZones Kit::monoZones() noexcept
{
    KitZones out;
    if (! environmentOk()) { out.status = CodecStatus::FloatingPointEnvironment; return out; }
    const View zones = detail::rules().engine.find ("monoBass").find ("zones");
    constexpr const char* names[kKitZones] { "club", "vinyl" };
    for (std::size_t i = 0; i < kKitZones; ++i)
    {
        const View zone = zones.find (names[i]);
        out.zones[i] = { number (zone.find ("fromHz")), number (zone.find ("toHz")) };
    }
    return out;
}

std::uint32_t Kit::monoZonesAt (double hz) noexcept
{
    const View zones = detail::rules().engine.find ("monoBass").find ("zones");
    constexpr const char* names[kKitZones] { "club", "vinyl" };
    std::uint32_t bits = 0;
    for (std::size_t i = 0; i < kKitZones; ++i)
    {
        const View zone = zones.find (names[i]);
        if (hz >= number (zone.find ("fromHz")) && hz <= number (zone.find ("toHz"))) bits |= 1u << i;
    }
    return bits;
}

KitEqPreview Kit::eqCurve (const KitEq& eq, double rate, std::span<EqPoint> out) noexcept
{
    KitEqPreview preview;
    if (! environmentOk()) { preview.status = CodecStatus::FloatingPointEnvironment; return preview; }
    if (out.size() < kEqCurvePoints) { preview.status = CodecStatus::TooSmall; return preview; }
    const Rules rules = detail::rules();
    if (! rateOk (rate) || ! std::isfinite (eq.hpf.fq) || ! rules.hpfFq.accepts (eq.hpf.fq, std::uint32_t (rate))
        || ! rules.slope (eq.hpf.slope) || ! std::isfinite (eq.tilt.db) || ! rules.tilt.accepts (eq.tilt.db, 0)
        || ! std::isfinite (eq.low.db) || ! rules.low.accepts (eq.low.db, 0) || ! bandsAccepted (eq.bands, rules))
    {
        preview.status = CodecStatus::Invalid;
        return preview;
    }
    detail::EqStage stage;
    detail::writeEq (eq.hpf, eq.tilt, eq.low, eq.bands, rules, stage);
    detail::eqCurve (stage.bands, rate, out.first (kEqCurvePoints));
    preview.finding = detail::eqFinding (stage, rules, rate);
    return preview;
}

KitCount Kit::saturationCurve (SaturationType type, double driveDb, double mix, std::span<double> out) noexcept
{
    if (! environmentOk()) return { CodecStatus::FloatingPointEnvironment, 0 };
    const Rules rules = detail::rules();
    if (std::uint8_t (type) > std::uint8_t (SaturationType::Tape) || ! detail::handSaturationType (type)
        || ! std::isfinite (driveDb) || driveDb < 0.0 || ! std::isfinite (mix) || ! rules.mix.accepts (mix, 0))
        return { CodecStatus::Invalid, 0 };
    // The stage as the chain writes it, designed as the stage designs itself: one source for both (src/Dynamics.h,
    // mastering::MasteringChain::clipperDesign). A drive whose gain is no float has no curve.
    const auto design = mastering::MasteringChain::clipperDesign (detail::clipperParams (rules, type, driveDb, mix));
    if (! std::isfinite (design.shaper.drive())) return { CodecStatus::Invalid, 0 };
    if (out.size() / 2 < kKitSaturationPoints) return { CodecStatus::TooSmall, kKitSaturationPoints };
    for (std::size_t i = 0; i < kKitSaturationPoints; ++i)
    {
        const double x = -1.0 + double (i) / 64.0;
        out[2 * i] = x;
        out[2 * i + 1] = double (mastering::MasteringChain::clipperTransfer (design, float (x)));
    }
    return { CodecStatus::Ok, kKitSaturationPoints };
}

KitCount Kit::lowEndCurve (std::span<const double> centreHz, std::span<const double> energy, double fromHz, double toHz,
                           std::span<double> out) noexcept
{
    if (! environmentOk()) return { CodecStatus::FloatingPointEnvironment, 0 };
    if (centreHz.size() != energy.size() || ! std::isfinite (fromHz) || ! std::isfinite (toHz) || fromHz > toHz)
        return { CodecStatus::Invalid, 0 };
    std::uint64_t count = 0;
    for (std::size_t i = 0; i < centreHz.size(); ++i)
    {
        if (! std::isfinite (centreHz[i]) || ! (centreHz[i] > 0.0) || ! std::isfinite (energy[i]) || energy[i] < 0.0)
            return { CodecStatus::Invalid, 0 };
        if (centreHz[i] >= fromHz && centreHz[i] <= toHz) ++count;
    }
    if (count < 2) return { CodecStatus::Ok, 0 };
    if (out.size() / 2 < count) return { CodecStatus::TooSmall, count };
    std::size_t at = 0;
    for (std::size_t i = 0; i < centreHz.size(); ++i)
        if (centreHz[i] >= fromHz && centreHz[i] <= toHz)
        {
            out[at++] = centreHz[i];
            out[at++] = energy[i] > 0.0 ? 10.0 * core::det::log10 (energy[i]) : -std::numeric_limits<double>::infinity();
        }
    return { CodecStatus::Ok, count };
}

} // namespace felitronics::session
