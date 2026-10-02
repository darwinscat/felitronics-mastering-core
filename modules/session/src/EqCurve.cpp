// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "EqCurve.h"
#include "BuildContract.h"
#include "Devices.h"
#include <felitronics/core/DetMath.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace felitronics::session::detail
{
namespace
{
// The high-pass and shelf coefficient designs from felitronics-core's MatchedBiquad.h
// (Vicanek matched filters), evaluated with deterministic transcendentals for snapshot replay.
// Tests compare the curves with core's audio-band response, including the low shelf's Q.
// Inputs: rates >= 8 kHz; design frequencies clamped to [10 Hz, 0.49 * rate], gains +/-6 dB,
// Q from the checked config. Trigonometric arguments stay within 2*pi, far inside det::tan's
// argument limit; pow10 sees +/-0.3, and response log10 sees positive power floored at 1e-24.
constexpr double kPi = 3.1415926535897932384626433832795;
bool sameNumber (double a, double b) noexcept { return a <= b && a >= b; }
double number (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto n = v.integer()) return double (*n);
    storageOverflow();
}
double expDet (double x) noexcept { return core::det::exp2 (x * 1.4426950408889634074); }
double coshDet (double x) noexcept { return (expDet (x) + expDet (-x)) * 0.5; }
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    bool isStable() const noexcept { return std::abs (a1) < 2 && std::abs (a1) - 1 < a2 && a2 < 1; }
    double db (double w) const noexcept
    {
        const double c1 = core::det::cos (w), s1 = core::det::sin (w);
        const double c2 = core::det::cos (2 * w), s2 = core::det::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = b1 * s1 + b2 * s2;
        const double dr = 1 + a1 * c1 + a2 * c2, di = a1 * s1 + a2 * s2;
        return 10 * core::det::log10 (std::max (1e-24, (nr * nr + ni * ni) / (dr * dr + di * di)));
    }
};
    double safeSqrt (double x) noexcept { return std::sqrt (std::max (0.0, x)); }
    void matchedPoles (double w0, double Q, double& a1, double& a2) noexcept
    {
        const double q   = 1.0 / (2.0 * Q);
        const double eqw = expDet (-q * w0);
        a1 = (q <= 1.0) ? -2.0 * eqw * core::det::cos (w0 * std::sqrt (1.0 - q * q))
                        : -2.0 * eqw * coshDet (w0 * std::sqrt (q * q - 1.0));
        a2 = expDet (-2.0 * q * w0);
    }
    void solveNumerator (double B0, double B1, double B2, BiquadCoeffs& c) noexcept
    {
        const double sB0 = safeSqrt (B0), sB1 = safeSqrt (B1);
        const double W   = 0.5 * (sB0 + sB1);
        c.b0 = 0.5 * (W + safeSqrt (W * W + B2));
        c.b1 = 0.5 * (sB0 - sB1);
        c.b2 = (! sameNumber (c.b0, 0.0)) ? -B2 / (4.0 * c.b0) : 0.0;
    }
    bool shelfFitFeasible (double B0, double B1, double B2) noexcept
    {
        const double W = 0.5 * (safeSqrt (B0) + safeSqrt (B1));
        return W * W + B2 > 0.0;
    }
    struct RatioBound { double lo, hi; };
    RatioBound ratioExtent (double n2, double n1, double n0, double d2, double d1, double d0,
                                   double lo, double hi) noexcept
    {
        auto val = [&] (double t) noexcept { return (n2 * t * t + n1 * t + n0) / (d2 * t * t + d1 * t + d0); };
        double mn = 1e300, mx = -1e300;
        auto upd = [&] (double t) noexcept { if (t >= lo && t <= hi) { const double v = val (t); mn = std::min (mn, v); mx = std::max (mx, v); } };
        upd (lo);
        if (std::isfinite (hi)) upd (hi);
        else if (! sameNumber (d2, 0.0)) { const double v = n2 / d2; mn = std::min (mn, v); mx = std::max (mx, v); }
        const double a = n2 * d1 - n1 * d2, b = 2.0 * (n2 * d0 - n0 * d2), c = n1 * d0 - n0 * d1;
        if (std::abs (a) < 1e-300) { if (std::abs (b) > 1e-300) upd (-c / b); }
        else { const double disc = b * b - 4.0 * a * c; if (disc >= 0.0) { const double s = std::sqrt (disc); upd ((-b + s) / (2.0 * a)); upd ((-b - s) / (2.0 * a)); } }
        return { mn, mx };
    }
    struct ShelfTmp { double v, w, a0, aa2, bb2; };
    ShelfTmp shelfSolve (double fc, double g) noexcept
    {
        const double piHalf = kPi * 0.5;
        const double invg = 1.0 / g;
        const double fc2 = fc * fc, fc4 = fc2 * fc2;
        const double hny = (fc4 + g) / (fc4 + invg);
        const double f1 = fc / std::sqrt (0.160 + 1.543 * fc2);
        const double f14 = f1 * f1 * f1 * f1;
        const double h1 = (fc4 + f14 * g) / (fc4 + f14 * invg);
        const double s1 = core::det::sin (piHalf * f1); const double phi1 = s1 * s1;
        const double f2 = fc / std::sqrt (0.947 + 3.806 * fc2);
        const double f24 = f2 * f2 * f2 * f2;
        const double h2 = (fc4 + f24 * g) / (fc4 + f24 * invg);
        const double s2 = core::det::sin (piHalf * f2); const double phi2 = s2 * s2;
        const double d1 = (h1 - 1.0) * (1.0 - phi1);
        const double c11 = -phi1 * d1, c12 = phi1 * phi1 * (hny - h1);
        const double d2 = (h2 - 1.0) * (1.0 - phi2);
        const double c21 = -phi2 * d2, c22 = phi2 * phi2 * (hny - h2);
        const double alfa1 = (c22 * d1 - c12 * d2) / (c11 * c22 - c12 * c21);
        const double aa1 = (d1 - c11 * alfa1) / c12;
        const double bb1 = hny * aa1;
        ShelfTmp t;
        t.aa2 = 0.25 * (alfa1 - aa1);
        t.bb2 = 0.25 * (alfa1 - bb1);
        t.v = 0.5 * (1.0 + safeSqrt (aa1));
        t.w = 0.5 * (1.0 + safeSqrt (bb1));
        t.a0 = 0.5 * (t.v + safeSqrt (t.v * t.v + t.aa2));
        return t;
    }
    BiquadCoeffs highpass (double f0, double fs, double Q) noexcept
    {
        BiquadCoeffs c;
        const double w0 = 2.0 * kPi * f0 / fs;
        matchedPoles (w0, Q, c.a1, c.a2);
        const double s = core::det::sin (w0 * 0.5);
        const double phi1 = s * s, phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;
        const double t0 = 1.0 + c.a1 + c.a2, t1 = 1.0 - c.a1 + c.a2;
        const double A0 = t0 * t0, A1 = t1 * t1, A2 = -4.0 * c.a2;
        c.b0 = Q * safeSqrt (A0 * phi0 + A1 * phi1 + A2 * phi2) / (4.0 * phi1);
        c.b1 = -2.0 * c.b0;
        c.b2 = c.b0;
        return c;
    }
    // The matched bell (peaking) of felitronics-core's MatchedBiquad.h: a cut is the boost of the same size inverted, so
    // -G and +G dB at one Q are mirror images.
    BiquadCoeffs peaking (double f0, double fs, double Q, double gainLin) noexcept
    {
        if (gainLin < 1.0 && gainLin > 0.0)
        {
            const BiquadCoeffs b = peaking (f0, fs, Q, 1.0 / gainLin);
            const double inv = 1.0 / b.b0;
            return { inv, b.a1 * inv, b.a2 * inv, b.b1 * inv, b.b2 * inv };
        }
        BiquadCoeffs c;
        const double w0 = 2.0 * kPi * f0 / fs;
        matchedPoles (w0, Q, c.a1, c.a2);
        const double s = core::det::sin (w0 * 0.5);
        const double phi1 = s * s, phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;
        const double t0 = 1.0 + c.a1 + c.a2, t1 = 1.0 - c.a1 + c.a2;
        const double A0 = t0 * t0, A1 = t1 * t1, A2 = -4.0 * c.a2;
        const double G2 = gainLin * gainLin;
        const double B0 = A0;
        const double R1 = (A0 * phi0 + A1 * phi1 + A2 * phi2) * G2;
        const double R2 = (-A0 + A1 + 4.0 * (phi0 - phi1) * A2) * G2;
        const double B2 = (R1 - R2 * phi1 - B0) / (4.0 * phi1 * phi1);
        const double B1 = R2 + B0 + 4.0 * (phi1 - phi0) * B2;
        solveNumerator (B0, B1, B2, c);
        return c;
    }
    BiquadCoeffs highpass1 (double f0, double fs) noexcept
    {
        BiquadCoeffs c;
        const double K = core::det::tan (kPi * f0 / fs), nrm = 1.0 / (1.0 + K);
        c.b0 = nrm; c.b1 = -nrm; c.b2 = 0.0; c.a1 = (K - 1.0) * nrm; c.a2 = 0.0;
        return c;
    }
    BiquadCoeffs highShelf (double f0, double fs, double gainLin) noexcept
    {
        BiquadCoeffs c;
        const double fc = 2.0 * f0 / fs;
        const double g  = (std::abs (1.0 - gainLin) < 1e-6) ? 1.00001 : gainLin;
        const auto t = shelfSolve (fc, g);
        const double inva0 = 1.0 / t.a0;
        c.a1 = (1.0 - t.v) * inva0;
        c.a2 = -0.25 * t.aa2 * inva0 * inva0;
        c.b0 = (0.5 * (t.w + safeSqrt (t.w * t.w + t.bb2))) * inva0;
        c.b1 = (1.0 - t.w) * inva0;
        c.b2 = (-0.25 * t.bb2 / c.b0) * inva0 * inva0;
        return c;
    }
    BiquadCoeffs lowShelf (double f0, double fs, double gainLin) noexcept
    {
        BiquadCoeffs c;
        const double fc = 2.0 * f0 / fs;
        const double g  = (std::abs (1.0 - gainLin) < 1e-6) ? 1.00001 : 1.0 / gainLin;
        const double invg = 1.0 / g;
        const auto t = shelfSolve (fc, g);
        const double inva0 = 1.0 / t.a0;
        const double ginva0 = invg * inva0;
        c.a1 = (1.0 - t.v) * inva0;
        c.a2 = -0.25 * t.aa2 * inva0 * inva0;
        const double b0raw = 0.5 * (t.w + safeSqrt (t.w * t.w + t.bb2));
        c.b1 = (1.0 - t.w) * ginva0;
        c.b2 = (-0.25 * t.bb2 / b0raw) * ginva0;
        c.b0 = b0raw * ginva0;
        return c;
    }
    BiquadCoeffs shelf (double f0, double fs, double gainLin, double Q, bool high) noexcept
    {
        if (gainLin < 1.0 && gainLin > 0.0)
        {
            const BiquadCoeffs b = shelf (f0, fs, 1.0 / gainLin, Q, high);
            const double inv = 1.0 / b.b0;
            const BiquadCoeffs cut { inv, b.a1 * inv, b.a2 * inv, b.b1 * inv, b.b2 * inv };
            if (cut.isStable()) return cut;
        }
        BiquadCoeffs c;
        const double w0 = 2.0 * kPi * f0 / fs;
        matchedPoles (w0, Q, c.a1, c.a2);
        const double s = core::det::sin (w0 * 0.5);
        const double phi1 = s * s, phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;
        const double t0 = 1.0 + c.a1 + c.a2, t1 = 1.0 - c.a1 + c.a2;
        const double A0 = t0 * t0, A1 = t1 * t1, A2 = -4.0 * c.a2;
        const double A   = gainLin, A2g = A * A;
        const double B0  = high ? A0       : A2g * A0;
        const double B1  = high ? A2g * A1 : A1;
        const double Hc2 = Q * Q * (A - 1.0) * (A - 1.0) + A;
        const double denW0 = A0 * phi0 + A1 * phi1 + A2 * phi2;
        const double B2  = (Hc2 * denW0 - B0 * phi0 - B1 * phi1) / phi2;
        if (! shelfFitFeasible (B0, B1, B2))
            return high ? highShelf (f0, fs, gainLin) : lowShelf (f0, fs, gainLin);
        solveNumerator (B0, B1, B2, c);
        const double kq = 1.0 / (Q * Q) - 2.0;
        const auto an = ratioExtent (high ? A * A : 1.0, A * kq, high ? 1.0 : A * A,
                                             1.0, kq, 1.0, 0.0, std::numeric_limits<double>::infinity());
        const auto dg = ratioExtent (4.0 * c.b0 * c.b2, 2.0 * c.b1 * (c.b0 + c.b2),
                                             (c.b0 - c.b2) * (c.b0 - c.b2) + c.b1 * c.b1,
                                             4.0 * c.a2, 2.0 * c.a1 * (1.0 + c.a2),
                                             (1.0 - c.a2) * (1.0 - c.a2) + c.a1 * c.a1, -1.0, 1.0);
        const double m2 = 2.0;
        if (dg.hi > an.hi * m2 || dg.lo < an.lo / m2)
            return high ? highShelf (f0, fs, gainLin) : lowShelf (f0, fs, gainLin);
        return c;
    }
// The high-pass cascade of order slope/6 at `fc`, into `bands` from `count` on (up to 9 sections).
void highPassSections (double fc, std::int32_t slope, double rate, BiquadCoeffs* bands, std::size_t& count) noexcept
{
    const int order = int (slope / 6);
    if (order % 2) bands[count++] = highpass1 (fc, rate);
    const int pairs = order / 2;
    for (int k = 1; k <= pairs; ++k)
        bands[count++] = highpass (fc, rate, 1 / (2 * core::det::sin ((2.0 * (pairs - k) + 1) * kPi / (2 * order))));
}
eq::BandParams band (eq::FilterType type, bool on, double hz) noexcept
{
    eq::BandParams b;
    b.on = on;
    b.type = type;
    b.lanes[0].freq = hz;
    return b;
}
} // namespace

int eqBand (Device device) noexcept
{
    switch (device)
    {
        case Device::Hpf:  return 0;
        case Device::Tilt: return 1;
        case Device::Low:  return 2;
        case Device::Bands:
        case Device::MonoBass:
        case Device::Glue:
        case Device::Saturation:
        case Device::Limiter:
        case Device::Dither: return -1;
    }
    storageOverflow();
}

int bandsSlot (const Rules& rules, std::size_t i) noexcept
{
    const auto slot = rules.engine.find ("bands").find (kBandNames[i]).find ("band").integer();
    if (! slot || *slot < 0 || *slot >= eq::EqEngine::kMaxBands) storageOverflow();
    return int (*slot);
}

void writeEq (const Devices& devices, const Rules& rules, EqStage& stage) noexcept
{
    writeEq (settingsOf (rules, devices.hpf), settingsOf (rules, devices.tilt), settingsOf (rules, devices.low),
             settingsOf (rules, devices.bands), rules, stage);
}

void writeEq (const HpfFields<Value>& hpf, const TiltFields<Value>& tilt, const LowFields<Value>& low,
              const BandsFields<Value>& bands, const Rules& rules, EqStage& stage) noexcept
{
    auto& h = stage.bands[eqBand (Device::Hpf)];
    h = band (eq::FilterType::HighPass, hpf.on, hpf.fq);
    h.lanes[0].slope = hpf.slope;

    auto& t = stage.bands[eqBand (Device::Tilt)];
    t = band (eq::FilterType::Tilt, tilt.on && ! sameNumber (tilt.db, 0), number (rules.engine.find ("tilt").find ("freqHz")));
    t.lanes[0].gainDb = tilt.db;

    const auto config = rules.engine.find ("low");
    auto& l = stage.bands[eqBand (Device::Low)];
    l = band (eq::FilterType::LowShelf, low.on && ! sameNumber (low.db, 0), number (config.find ("freqHz")));
    l.lanes[0].Q = number (config.find ("q"));
    l.lanes[0].gainDb = low.db;

    // The EQ bands: a band at 0 dB is no band — its slot as the stage holds it untouched, so the chain gets exactly what
    // it got before the device was there. The device off writes all five slots so, its gains kept in the project.
    const double gains[] { bands.body, bands.mud, bands.forward, bands.brightness, bands.air };
    static_assert (std::size (gains) == std::size (kBandNames));
    for (std::size_t i = 0; i < std::size (gains); ++i)
    {
        auto& b = stage.bands[bandsSlot (rules, i)];
        if (! bands.on || sameNumber (gains[i], 0)) { b = {}; continue; }
        const auto move = rules.engine.find ("bands").find (kBandNames[i]);
        const auto type = move.find ("type").string();
        if (! type) storageOverflow();
        b = band (*type == "highShelf" ? eq::FilterType::HighShelf : eq::FilterType::Bell, true, number (move.find ("freqHz")));
        b.lanes[0].Q = number (move.find ("q"));
        b.lanes[0].gainDb = gains[i];
    }
}

void eqCurve (std::span<const eq::BandParams> written, double rate, std::span<EqPoint> output) noexcept
{
    // The high-pass's cascade (at most 8 sections at 96 dB/oct), tilt's two shelves, low's shelf, the five bands.
    BiquadCoeffs bands[16];
    std::size_t count = 0;
    const auto frequency = [&] (double hz) { return std::clamp (hz, 10.0, 0.49 * rate); };
    for (const auto& b : written)
    {
        if (! b.on) continue;
        const auto& lane = b.lanes[0];
        switch (b.type)
        {
            case eq::FilterType::HighPass:
                highPassSections (frequency (lane.freq), lane.slope, rate, bands, count);
                break;
            case eq::FilterType::Tilt:
            {
                const double fq = frequency (lane.freq);
                bands[count++] = lowShelf (fq, rate, core::det::pow10 (-lane.gainDb / 20));
                bands[count++] = highShelf (fq, rate, core::det::pow10 (lane.gainDb / 20));
                break;
            }
            case eq::FilterType::LowShelf:
                bands[count++] = shelf (frequency (lane.freq), rate, core::det::pow10 (lane.gainDb / 20), lane.Q, false);
                break;
            case eq::FilterType::Bell:
                bands[count++] = peaking (frequency (lane.freq), rate, lane.Q, core::det::pow10 (lane.gainDb / 20));
                break;
            case eq::FilterType::HighShelf:
                bands[count++] = shelf (frequency (lane.freq), rate, core::det::pow10 (lane.gainDb / 20), lane.Q, true);
                break;
            case eq::FilterType::LowPass:
            case eq::FilterType::BandPass:
            case eq::FilterType::Notch:
            case eq::FilterType::AllPass:
                storageOverflow();
        }
    }
    const double last = std::min (20000.0, rate * 0.49);
    const double octaves = core::det::log2 (last / 20);
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        const double hz = 20 * core::det::exp2 (octaves * double (i) / double (output.size() - 1));
        double db = 0;
        for (std::size_t b = 0; b < count; ++b) db += bands[b].db (2 * kPi * hz / rate);
        output[i] = { hz, db };
    }
}

void eqCurve (const Project& project, const Rules& rules, double rate, std::span<EqPoint> output) noexcept
{
    EqStage stage;
    writeEq (project.devices, rules, stage);
    eqCurve (stage.bands, rate, output);
}

void eqOnlyCurve (const Project& project, const Rules& rules, double rate, std::span<EqPoint> output) noexcept
{
    EqStage stage;
    writeEq (project.devices, rules, stage);
    stage.bands[eqBand (Device::Hpf)].on = false;
    eqCurve (stage.bands, rate, output);
}

EqFinding eqFinding (const Devices& devices, const Rules& rules, double rate) noexcept
{
    EqStage stage;
    writeEq (devices, rules, stage);
    return eqFinding (stage, rules, rate);
}

EqFinding eqFinding (const EqStage& stage, const Rules& rules, double rate) noexcept
{
    EqPoint tilt[kEqCurvePoints], low[kEqCurvePoints];
    eqCurve (std::span<const eq::BandParams> (&stage.bands[eqBand (Device::Tilt)], 1), rate, tilt);
    eqCurve (std::span<const eq::BandParams> (&stage.bands[eqBand (Device::Low)], 1), rate, low);
    EqFinding f;
    double largest = -1.0;
    for (std::size_t i = 0; i < kEqCurvePoints; ++i)
    {
        const double db = tilt[i].db + low[i].db;
        if (! (std::abs (db) > largest)) continue;
        largest = std::abs (db);
        f.hz = tilt[i].hz;
        f.db = db;
        f.device = std::abs (low[i].db) > std::abs (tilt[i].db) ? Device::Low : Device::Tilt;
    }
    f.over = largest > number (rules.engine.find ("eq").find ("curve").find ("warnDb"));
    return f;
}

double highPassLossDb (double fc, std::int32_t slope, double rate, double hz) noexcept
{
    BiquadCoeffs sections[9];
    std::size_t count = 0;
    highPassSections (std::clamp (fc, 10.0, 0.49 * rate), slope, rate, sections, count);
    double db = 0;
    for (std::size_t i = 0; i < count; ++i) db += sections[i].db (2 * kPi * hz / rate);
    return -db;
}

double highPassCutoffFor (double hz, double lossDb, std::int32_t slope, double rate) noexcept
{
    // The loss at `hz` grows with the cutoff: below the answer it takes less, above it more. At the lowest cutoff the
    // engine designs it may take the loss already — then that cutoff is the answer.
    double lo = 10.0, hi = std::max (lo, std::min (hz, 0.49 * rate));
    if (highPassLossDb (lo, slope, rate, hz) >= lossDb) return lo;
    for (int i = 0; i < 200; ++i)
    {
        const double mid = lo + (hi - lo) * 0.5;
        if (! (mid > lo && mid < hi)) break;
        if (highPassLossDb (mid, slope, rate, hz) < lossDb) lo = mid; else hi = mid;
    }
    return lo;
}
}
