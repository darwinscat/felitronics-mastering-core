// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE PURE KIT (include/felitronics/session/Kit.h, fc_kit_* in tools/fc_session_abi.h): each answer is the session's own
// code's — text is Text::write, parse is Text::parse on the commands' domains and the knob's grid, the EQ preview is the
// stage the project writes — the C ABI answers what the C++ call answers, and one corpus of answers hashes to the value
// tools/wasm/session-check.mjs holds the wasm module to (native == wasm, byte for byte).

#include "fc_session_abi.h"
#include "EqCurve.h"
#include "Grid.h"
#include "JsonCodec.h"
#include "Rules.h"
#include "FpEnvironmentControl.h"

#include <felitronics/session/Kit.h>
#include <felitronics/session/Text.h>
#include <felitronics_test.h>
#include <felitronics/saturation/Saturator.h>
#include <felitronics/core/Math.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace felitronics::session;
using felitronics::test::ok;
using text::Arg;
using text::Fact;
using text::FactId;
using text::Lang;
using text::Term;
using text::Unit;
namespace fpenv = felitronics::session::testing;

bool sameBits (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }

// A fact as the session publishes it: the codec's own writer.
std::string wire (const Fact& f)
{
    detail::Writer size; size.value (f);
    std::string out (std::size_t (size.size), '\0');
    detail::Writer w; w.output = out.data(); w.value (f);
    return out;
}

std::string rendered (const Fact& f, Lang lang)
{
    std::string out (text::Text::size (f, lang), '\0');
    (void) text::Text::write (f, lang, { out.data(), out.size() });
    return out;
}

// THE PARITY CORPUS — the same inputs tools/wasm/session-check.mjs gives the wasm module, in the same order.
const char* const kFacts[] = {
    R"({"FactId":3,"args":[{"kind":2,"unit":0,"precision":0,"sign":0,"bound":0,"termId":0,"number":0,"integer":"7","userText":""}]})",
    R"({"FactId":504,"args":[{"kind":1,"unit":2,"precision":1,"sign":1,"bound":0,"termId":0,"number":2.375,"integer":"0","userText":""},{"kind":1,"unit":7,"precision":0,"sign":0,"bound":0,"termId":0,"number":62.5,"integer":"0","userText":""}]})",
    R"({"FactId":5,"args":[{"kind":4,"unit":0,"precision":0,"sign":0,"bound":0,"termId":0,"number":0,"integer":"28","userText":""}]})",
    R"({"FactId":112,"args":[{"kind":3,"unit":0,"precision":0,"sign":0,"bound":0,"termId":3,"number":0,"integer":"0","userText":""}]})",
};
const Fact kFactValues[] = {
    Fact::of (FactId::LandingConverged, Arg::count (7)),
    Fact::of (FactId::EqOvershoot, Arg::value (2.375, Unit::Db, 1, text::Sign::Always), Arg::value (62.5, Unit::Hz, 0)),
    Fact::of (FactId::LoudestLowNote, Arg::midi (28)),
    Fact::of (FactId::RejectedNotFinite, Arg::term (Term::FieldTargetLufs)),
};
const char* const kLangs[] = { "en", "ru" };
struct ParseCase { const char* typed; const char* lang; std::uint32_t field; std::uint32_t rate; };
const ParseCase kParses[] = {
    { "-14.05", "en", 3, 0 }, { "\xE2\x88\x92" "14,05", "ru", 3, 0 }, { "14", "en", 3, 0 }, { "-0.04", "en", 4, 0 },
    { "0.25", "ru", 4, 0 }, { "30.5", "en", 5, 48000 }, { "30,4", "ru", 5, 0 }, { "24", "en", 6, 0 }, { "25", "en", 6, 0 },
    { "0.325", "en", 8, 0 }, { "abc", "en", 9, 0 }, { "1,5", "de", 13, 0 },
};
struct KnobCase { std::uint32_t field; double value; };
const KnobCase kKnobs[] = { { 3, -20 }, { 3, -14 }, { 3, -9 }, { 4, -1.5 }, { 5, 22 }, { 5, 46 }, { 13, 2.25 }, { 9, 1.2 }, { 16, -4 } };
const double kZoneProbes[] = { 70, 100, 120, 150, 250 };
const double kEqParams[2][FC_SESSION_KIT_EQ_PARAMS] = { { 1, 30, 24, 1, 2, 0, 0 }, { 1, 25, 12, 1, -3, 1, 4.5 } };
const double kEqRates[] = { 48000, 44100 };
const double kLowCentres[] = { 20, 30, 40, 50, 60 };
const double kLowEnergies[] = { 0.01, 0, 1, 0.0001, 0.5 };

struct Fnv
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    void byte (std::uint8_t b) { h ^= b; h *= 0x100000001b3ull; }
    void bytes (const void* p, std::size_t n) { for (std::size_t i = 0; i < n; ++i) byte (static_cast<const std::uint8_t*> (p)[i]); }
    void number (double v) { const auto bits = std::bit_cast<std::uint64_t> (v); for (int i = 0; i < 8; ++i) byte (std::uint8_t (bits >> (8 * i))); }
    void word (std::uint32_t v) { for (int i = 0; i < 4; ++i) byte (std::uint8_t (v >> (8 * i))); }
};

// The corpus through the C ABI, hashed in the order session-check.mjs hashes it.
std::uint64_t corpusHash()
{
    Fnv f;
    for (const char* fact : kFacts)
        for (const char* lang : kLangs)
        {
            char out[512]; std::uint32_t written = 0;
            f.byte (std::uint8_t (fc_kit_text (fact, std::uint32_t (std::strlen (fact)), lang, 2, out, sizeof (out), &written)));
            f.word (written); f.bytes (out, written);
        }
    for (const auto& c : kParses)
    {
        double value = -1; std::uint32_t refusal = 9;
        f.byte (std::uint8_t (fc_kit_parse (c.typed, std::uint32_t (std::strlen (c.typed)), c.lang, 2, c.field, c.rate, &value, &refusal)));
        f.byte (std::uint8_t (refusal)); f.number (value);
    }
    for (const auto& k : kKnobs)
    {
        double out[3] {};
        f.byte (std::uint8_t (fc_kit_position (k.field, k.value, out))); f.number (out[0]);
        f.byte (std::uint8_t (fc_kit_value_at (k.field, 0.37, out))); f.number (out[0]);
        f.byte (std::uint8_t (fc_kit_heat (k.field, k.value, out))); f.number (out[0]); f.number (out[1]); f.number (out[2]);
    }
    double zones[2 * FC_SESSION_KIT_ZONES] {};
    f.byte (std::uint8_t (fc_kit_mono_zones (zones)));
    for (double z : zones) f.number (z);
    for (double hz : kZoneProbes) { std::uint32_t bits = 0; f.byte (std::uint8_t (fc_kit_mono_zones_at (hz, &bits))); f.word (bits); }
    for (const auto& params : kEqParams)
        for (double rate : kEqRates)
        {
            double curve[2 * FC_SESSION_KIT_EQ_POINTS] {}, peak[FC_SESSION_KIT_EQ_PEAK_VALUES] {};
            f.byte (std::uint8_t (fc_kit_eq_curve (params, rate, curve, peak)));
            for (double v : curve) f.number (v);
            for (double v : peak) f.number (v);
        }
    double points[10] {}; std::uint32_t written = 0;
    f.byte (std::uint8_t (fc_kit_low_end_curve (kLowCentres, kLowEnergies, 5, 25, 55, points, 5, &written)));
    f.word (written);
    for (std::uint32_t i = 0; i < 2 * written; ++i) f.number (points[i]);
    return f.h;
}

void textIsTextWrite()
{
    felitronics::test::group ("text: the published fact, rendered by Text::write, through the codec's own reader");
    const Fact corpus[] = {
        kFactValues[0], kFactValues[1], kFactValues[2], kFactValues[3],
        Fact::of (FactId::Value, Arg::value (-14.05, Unit::Lufs, 1)),
        Fact::of (FactId::Value, Arg::value (12345.678, Unit::Hz, 0)),
        Fact::of (FactId::Value, Arg::value (std::numeric_limits<double>::quiet_NaN(), Unit::Db, 1)),
        Fact::of (FactId::LandingPass, Arg::count (2), Arg::count (5)),
        Fact::of (FactId::LandingConverged, Arg::count (1)),
        Fact::of (FactId::LandingConverged, Arg::count (22)),
        Fact::of (FactId::LimiterManual, Arg::value (-1, Unit::DbTp, 1), Arg::value (1.5, Unit::Db, 1)),
        Fact::of (FactId::MonoBassOutsideZones, Arg::value (250, Unit::Hz, 0), Arg::value (120, Unit::Hz, 0), Arg::value (200, Unit::Hz, 0)),
        Fact::of (FactId::RejectedOutOfDomain, Arg::term (Term::FieldHpfFq)),
    };
    for (std::size_t i = 0; i < 4; ++i) ok (wire (kFactValues[i]) == kFacts[i], "the corpus's wire fact " + std::to_string (i) + " is the codec's");
    bool same = true;
    for (const auto& fact : corpus)
        for (const Lang lang : { Lang::Ru, Lang::En })
        {
            const auto json = wire (fact);
            char out[1024];
            const auto answer = Kit::text (json, lang, out);
            same = same && answer.status == CodecStatus::Ok && std::string_view (out, std::size_t (answer.count)) == rendered (fact, lang);
        }
    ok (same, "every fact of the corpus, in ru and in en, renders as Text::write renders it");
    char tiny[4];
    const auto shortOut = Kit::text (kFacts[0], Lang::Ru, tiny);
    ok (shortOut.status == CodecStatus::TooSmall && shortOut.count == rendered (kFactValues[0], Lang::Ru).size(), "a short buffer: TooSmall and the size");
    char out[256];
    ok (Kit::text ("{\"FactId\":9,\"args\":[]}", Lang::En, out).status == CodecStatus::Invalid, "a retired id is no fact");
    ok (Kit::text ("not json", Lang::En, out).status == CodecStatus::Invalid, "garbage is no fact");
    ok (Kit::text (std::string (kFacts[0]) + " x", Lang::En, out).status == CodecStatus::Invalid, "trailing bytes are refused");
}

void parseReadsTheField()
{
    felitronics::test::group ("parse: Text::parse, the sign, the knob's grid, the command's domain");
    const auto accepted = [] (const char* typed, Lang lang, Term field, double want, std::uint32_t rate = 0)
    {
        const auto p = Kit::parse (typed, lang, field, rate);
        return p.status == CodecStatus::Ok && p.refusal == KitRefusal::None && sameBits (p.value, want);
    };
    const auto refused = [] (const char* typed, Lang lang, Term field, KitRefusal why, std::uint32_t rate = 0)
    {
        const auto p = Kit::parse (typed, lang, field, rate);
        return p.status == CodecStatus::Ok && p.refusal == why;
    };
    ok (accepted ("-14.05", Lang::En, Term::FieldTargetLufs, -14.1), "-14.05 LUFS rounds away from zero to the step: −14.1");
    ok (accepted ("\xE2\x88\x92" "14,05", Lang::Ru, Term::FieldTargetLufs, -14.1), "the typographic minus and the Russian decimal comma");
    ok (accepted ("14", Lang::En, Term::FieldTargetLufs, -14.0), "a bare number on a travel below zero is negative");
    ok (accepted ("+14", Lang::En, Term::FieldTargetLufs, 14.0), "a sign typed is kept (loudness has no bound)");
    ok (accepted ("0.25", Lang::Ru, Term::FieldTargetTp, -0.3), "a halfway ceiling goes away from zero: −0.25 → −0.3");
    ok (refused ("-0.04", Lang::En, Term::FieldTargetTp, KitRefusal::OutOfDomain), "a ceiling that rounds to 0 is out of its domain");
    ok (refused ("-7", Lang::En, Term::FieldTargetTp, KitRefusal::OutOfDomain), "a ceiling below −6 is out of its domain");
    ok (accepted ("30.4", Lang::En, Term::FieldHpfFq, 30.0, 48000) && accepted ("30.5", Lang::En, Term::FieldHpfFq, 31.0, 48000),
        "the cutoff rounds to its 1 Hz step, halfway up");
    ok (refused ("30", Lang::En, Term::FieldHpfFq, KitRefusal::OutOfDomain, 0), "without a source the cutoff has no Nyquist to stay under");
    ok (accepted ("0.325", Lang::En, Term::FieldMonoBassWidth, 0.35) && accepted ("0.33", Lang::En, Term::FieldMonoBassWidth, 0.35),
        "the width moves to its 0.05 grid");
    ok (refused ("1.2", Lang::En, Term::FieldMonoBassWidth, KitRefusal::OutOfDomain), "a width above 1 is refused");
    ok (accepted ("1,5", Lang::De, Term::FieldTiltDb, 1.5) && accepted ("-2.25", Lang::En, Term::FieldTiltDb, -2.3), "tilt, signed both ways");
    ok (accepted ("24", Lang::En, Term::FieldHpfSlope, 24.0) && accepted ("18", Lang::En, Term::FieldHpfSlope, 18.0),
        "a slope is a whole number of dB/oct as written, 18 included");
    ok (refused ("25", Lang::En, Term::FieldHpfSlope, KitRefusal::OutOfDomain) && refused ("24.5", Lang::En, Term::FieldHpfSlope, KitRefusal::OutOfDomain),
        "a slope that is no multiple of 6, or no whole number, is refused");
    ok (refused ("abc", Lang::En, Term::FieldGlueUpToDb, KitRefusal::NotANumber) && refused ("", Lang::En, Term::FieldGlueUpToDb, KitRefusal::NotANumber)
        && refused ("1.2.3", Lang::En, Term::FieldGlueUpToDb, KitRefusal::NotANumber) && refused ("1,234", Lang::En, Term::FieldGlueUpToDb, KitRefusal::NotANumber),
        "garbage, nothing, two decimal signs and a grouping separator are no number");
    ok (refused ("1e300", Lang::En, Term::FieldTargetLufs, KitRefusal::NotANumber), "an exponent is no number a person types");
    ok (Kit::parse ("1", Lang::En, Term::FieldAudio, 0).status == CodecStatus::Invalid
        && Kit::parse ("1", Lang::En, Term::PlatformWeb, 0).status == CodecStatus::Invalid, "a field the kit has no knob for is Invalid");
    // The probe runs only where the mode was set (FpEnvironmentControl.h): the wasm tier has no rounding modes, and a
    // refusal test on an environment that never changed would test nothing.
    const auto saved = fpenv::saveFpEnvironment();
    if (fpenv::setRounding (fpenv::kRoundUpward))
    {
        const auto p = Kit::parse ("1", Lang::En, Term::FieldGlueUpToDb, 0);
        const auto h = Kit::heat (Term::FieldTargetLufs, -14);
        fpenv::restoreFpEnvironment (saved);
        ok (p.status == CodecStatus::FloatingPointEnvironment && h.status == CodecStatus::FloatingPointEnvironment,
            "rounding upward on the calling thread: refused, as every computing call of the session");
    }
    else
    {
        fpenv::restoreFpEnvironment (saved);
        std::printf ("    rounding upward: not probed — this platform cannot set the mode\n");
    }
}

void travelAndHeat()
{
    felitronics::test::group ("travel and heat: the config's travels, windows and grids");
    const auto t = Kit::travel (Term::FieldTargetLufs);
    ok (t.status == CodecStatus::Ok && sameBits (t.from, -25) && sameBits (t.to, -5) && sameBits (t.step, 0.1), "the loudness travel is [edit] lufs");
    ok (Kit::travel (Term::FieldHpfSlope).status == CodecStatus::Invalid, "a slope is a choice, not a travel");
    const auto hpf = Kit::travel (Term::FieldHpfFq);
    ok (hpf.status == CodecStatus::Ok && sameBits (hpf.from, 15) && sameBits (hpf.to, 80) && sameBits (hpf.step, 1),
        "the high-pass knob travels from 15 to 80 Hz by the hertz (owner, 01.10), past the machine's 50 Hz top");
    ok (sameBits (Kit::position (Term::FieldTargetLufs, -15).value, 0.5) && sameBits (Kit::position (Term::FieldTargetLufs, -30).value, 0.0)
        && sameBits (Kit::position (Term::FieldTargetLufs, 0).value, 1.0), "a position along the travel, past an end at that end");
    ok (sameBits (Kit::valueAt (Term::FieldTargetLufs, 0.5).value, -15.0) && sameBits (Kit::valueAt (Term::FieldTargetLufs, 0.123).value, -22.5)
        && sameBits (Kit::valueAt (Term::FieldTargetLufs, 2.0).value, -5.0) && sameBits (Kit::valueAt (Term::FieldTargetLufs, -1.0).value, -25.0),
        "the value at a position is the nearest step, in decimal digits");
    bool roundTrip = true;
    for (std::int64_t k = 0; k <= 200; ++k)
    {
        const double v = felitronics::toml::Decimal { -250 + k, 1, false }.toDouble();
        roundTrip = roundTrip && sameBits (Kit::valueAt (Term::FieldTargetLufs, Kit::position (Term::FieldTargetLufs, v).value).value, detail::kept (v));
    }
    ok (roundTrip, "every step of the loudness travel goes to its position and back to itself, bit for bit");
    ok (Kit::position (Term::FieldTargetLufs, std::numeric_limits<double>::quiet_NaN()).status == CodecStatus::Invalid
        && Kit::valueAt (Term::FieldTargetLufs, std::numeric_limits<double>::infinity()).status == CodecStatus::Invalid, "non-finite: Invalid");
    const auto heat = [] (Term f, double v, double want, int side)
    {
        const auto h = Kit::heat (f, v);
        return h.status == CodecStatus::Ok && h.window && sameBits (h.heat, want) && h.side == side;
    };
    ok (heat (Term::FieldTargetLufs, -14, 0, 0) && heat (Term::FieldTargetLufs, -15, 0, 0) && heat (Term::FieldTargetLufs, -13, 0, 0),
        "inside the green window, its edges included: no heat");
    ok (heat (Term::FieldTargetLufs, -20, 0.5, -1) && heat (Term::FieldTargetLufs, -25, 1, -1) && heat (Term::FieldTargetLufs, -40, 1, -1)
        && heat (Term::FieldTargetLufs, -9, 0.5, 1), "outside it, the share of the way to the travel's end");
    ok (heat (Term::FieldTargetTp, -1.5, 0, 0) && heat (Term::FieldTargetTp, -3, 0.5, -1), "the ceiling's green window");
    ok (heat (Term::FieldHpfFq, 30, 0, 0) && heat (Term::FieldHpfFq, 22, 0.5, -1) && heat (Term::FieldHpfFq, 46, 0.5, 1) && heat (Term::FieldHpfFq, 60, 1, 1)
        && heat (Term::FieldHpfFq, 50, 1, 1) && heat (Term::FieldHpfFq, 70, 1, 1) && heat (Term::FieldHpfFq, 80, 1, 1),
        "the high-pass's comfort window, out to its warnings; red from 50 Hz to the travel's 80 (owner, 01.10)");
    ok (heat (Term::FieldTiltDb, 0, 0, 0) && heat (Term::FieldTiltDb, 2.25, 0.5, 1) && heat (Term::FieldLowDb, -3, 1, -1), "tilt and low: normal, out to hard");
    const auto none = Kit::heat (Term::FieldGlueUpToDb, 1.2);
    ok (none.status == CodecStatus::Ok && ! none.window && sameBits (none.heat, 0) && none.side == 0, "the glue has no window");
}

void zonesAndAdvice()
{
    felitronics::test::group ("mono bass's zones, and the advice that reads them and the comfort window");
    const auto z = Kit::monoZones();
    ok (z.status == CodecStatus::Ok && sameBits (z.zones[0].fromHz, 80) && sameBits (z.zones[0].toHz, 120) && sameBits (z.zones[1].fromHz, 120)
        && sameBits (z.zones[1].toHz, 200), "club 80…120 and vinyl 120…200, from [monoBass.zones]");
    ok (Kit::monoZonesAt (100) == 1u && Kit::monoZonesAt (120) == 3u && Kit::monoZonesAt (150) == 2u && Kit::monoZonesAt (70) == 0u
        && Kit::monoZonesAt (250) == 0u && Kit::monoZonesAt (std::numeric_limits<double>::quiet_NaN()) == 0u, "the zones holding a frequency, bounds included");
    bool hpf = true, mono = true;
    for (double hz = 10; hz <= 320; hz += 0.5)
    {
        // A person's value: the advice speaks of nothing else (owner, 01.10).
        HpfFinding h; h.sounding = Sounding::Hand; h.soundingHz = hz;
        hpf = hpf && PlanText::hpfCutoffAdvice (h).has_value() == (Kit::heat (Term::FieldHpfFq, hz).side != 0);
        MonoBassFinding m; m.sounding = Sounding::Hand; m.soundingHz = hz;
        mono = mono && PlanText::monoBassAdvice (m).has_value() == (Kit::monoZonesAt (hz) == 0u);
    }
    ok (hpf, "the high-pass's comfort advice is said exactly where the knob's heat has a side");
    ok (mono, "the outside-every-zone advice is said exactly where no zone holds the crossover");
}

void eqPreview()
{
    felitronics::test::group ("the EQ preview: the stage a project writes, its curve and its finding");
    const auto compare = [] (const KitEq& eq, double rate)
    {
        Devices devices;   // the project's path: each device's machine layer, nothing by hand
        devices.hpf.machine = eq.hpf; devices.tilt.machine = eq.tilt; devices.low.machine = eq.low;
        const auto rules = detail::rules();
        Project project; project.devices = devices;
        EqPoint want[kEqCurvePoints], got[kEqCurvePoints];
        detail::eqCurve (project, rules, rate, want);
        const auto finding = detail::eqFinding (devices, rules, rate);
        const auto preview = Kit::eqCurve (eq, rate, got);
        bool same = preview.status == CodecStatus::Ok && preview.finding.over == finding.over && sameBits (preview.finding.hz, finding.hz)
            && sameBits (preview.finding.db, finding.db) && preview.finding.device == finding.device;
        for (std::size_t i = 0; i < kEqCurvePoints; ++i) same = same && sameBits (got[i].hz, want[i].hz) && sameBits (got[i].db, want[i].db);
        return std::pair { same, preview.finding };
    };
    KitEq a; a.hpf = { true, 30, 24 }; a.tilt = { true, 1 }; a.low = { false, 0 };
    KitEq b; b.hpf = { true, 25, 12 }; b.tilt = { true, -3 }; b.low = { true, 4.5 };
    const auto [sameA, fa] = compare (a, 48000);
    const auto [sameB, fb] = compare (b, 44100);
    ok (sameA && sameB, "the curve and the finding are the project path's, bit for bit, at two rates");
    ok (! fa.over && fb.over && std::fabs (fb.db) > 2.0, "1 dB of tilt is inside warnDb, a 4.5 dB low shelf with −3 dB tilt is beyond it");
    EqPoint out[kEqCurvePoints];
    KitEq bad = a; bad.hpf.fq = 30000;
    ok (Kit::eqCurve (bad, 48000, out).status == CodecStatus::Invalid, "a cutoff above Nyquist is refused");
    bad = a; bad.hpf.slope = 25;
    ok (Kit::eqCurve (bad, 48000, out).status == CodecStatus::Invalid, "a slope outside the domain is refused");
    bad = a; bad.tilt.db = 7;
    ok (Kit::eqCurve (bad, 48000, out).status == CodecStatus::Invalid, "a tilt outside its domain is refused");
    ok (Kit::eqCurve (a, 7999, out).status == CodecStatus::Invalid && Kit::eqCurve (a, 48000.5, out).status == CodecStatus::Invalid,
        "a rate below the floor, or not whole, is refused");
    ok (Kit::eqCurve (a, 48000, std::span<EqPoint> (out, 10)).status == CodecStatus::TooSmall, "a short curve buffer: TooSmall");
}

void lowEndCurve()
{
    felitronics::test::group ("the low-end curve: the bands in range, 10·log10 of their energy");
    double out[10] {};
    const auto c = Kit::lowEndCurve (kLowCentres, kLowEnergies, 25, 55, out);
    ok (c.status == CodecStatus::Ok && c.count == 3 && sameBits (out[0], 30) && sameBits (out[1], -std::numeric_limits<double>::infinity())
        && sameBits (out[2], 40) && sameBits (out[3], 0) && sameBits (out[4], 50) && sameBits (out[5], 10 * felitronics::core::det::log10 (0.0001)),
        "three bands in 25…55 Hz: no energy is −∞, 1 is 0 dB, 1e−4 is det::log10's −40");
    ok (Kit::lowEndCurve (kLowCentres, kLowEnergies, 25, 35, out).count == 0, "one band in range is no curve");
    ok (Kit::lowEndCurve (kLowCentres, kLowEnergies, 25, 55, std::span<double> (out, 4)).status == CodecStatus::TooSmall
        && Kit::lowEndCurve (kLowCentres, kLowEnergies, 25, 55, std::span<double> (out, 4)).count == 3, "a short buffer: TooSmall and the points");
    const double negative[] = { 0.1, -1, 0.1, 0.1, 0.1 };
    ok (Kit::lowEndCurve (kLowCentres, negative, 25, 55, out).status == CodecStatus::Invalid, "a negative energy is refused");
    ok (Kit::lowEndCurve (std::span<const double> (kLowCentres, 4), kLowEnergies, 25, 55, out).status == CodecStatus::Invalid, "spans of two lengths");
    ok (Kit::lowEndCurve (kLowCentres, kLowEnergies, 55, 25, out).status == CodecStatus::Invalid, "from above to");
}

void saturationCurve()
{
    felitronics::test::group ("the saturation's transfer curve: the chain's shaper, on the level alone");
    const auto sat = detail::rules().engine.find ("saturation");
    const auto read = [] (felitronics::toml::embedded::View v)
    { return v.decimal() ? v.decimal()->toDouble() : double (*v.integer()); };
    const float bias = float (read (sat.find ("bias"))), autoComp = float (read (sat.find ("autoComp")));
    const float dcBlockHz = float (read (sat.find ("dcBlockHz")));
    using Shape = felitronics::saturation::WaveShaper::Shape;
    const SaturationType hand[] { SaturationType::Tanh, SaturationType::Tube, SaturationType::Transistor,
                                  SaturationType::Transformer, SaturationType::Tape };
    double out[2 * kKitSaturationPoints];
    // The oracle: the core's WaveShaper set as the stage sets it (Saturator::design), the stage's base-rate blend.
    bool kernel = true, grid = true;
    for (const auto type : hand)
        for (const double drive : { 0.0, 1.5, 6.0, 12.0, 30.0 })
            for (const double mix : { 1.0, 0.35 })
            {
                const auto c = Kit::saturationCurve (type, drive, mix, out);
                felitronics::saturation::WaveShaper w;
                w.setShape (Shape (type)); w.setBias (bias);
                w.setDrive (float (felitronics::core::dbToGain (float (drive)) - 1.0));
                const float comp = float (std::pow (double (std::max (1.0e-6f, w.slopeAtZero())), double (-autoComp)));
                kernel = kernel && c.status == CodecStatus::Ok && c.count == kKitSaturationPoints;
                for (std::size_t i = 0; i < kKitSaturationPoints; ++i)
                {
                    const double x = -1.0 + double (i) / 64.0;
                    const float m = float (mix), xf = float (x);
                    const float y = 1.0f * ((1.0f - m) * xf + m * (comp * w.processSample (xf)));
                    grid = grid && sameBits (out[2 * i], x);
                    kernel = kernel && sameBits (out[2 * i + 1], double (y));
                }
            }
    ok (grid, "129 inputs from −1 to +1 of full scale, a 64th apart, each beside its output");
    ok (kernel, "each output is the core's WaveShaper at k = 10^(drive/20) − 1 with the config's bias, its drive compensation and the "
                "dry/wet blend, bit for bit — five types, five drives, two mixes");
    // The running stage, as the chain sets it, settles on the curve: a level held long enough leaves the oversampler, and
    // what the stage gives for it is the curve's output. Not the transformer: its flux follows the signal's history.
    bool settles = true;
    for (const auto type : { SaturationType::Tanh, SaturationType::Tube, SaturationType::Transistor, SaturationType::Tape })
        for (const double drive : { 3.0, 9.0 })
        {
            (void) Kit::saturationCurve (type, drive, 1.0, out);
            for (const std::size_t i : { std::size_t (0), std::size_t (32), std::size_t (80), std::size_t (112), std::size_t (128) })
            {
                felitronics::saturation::Saturator stage;
                settles = settles && stage.prepare (48000, 512, 1, 4, 64);
                felitronics::saturation::Saturator::Params p;
                p.shape = Shape (type); p.driveDb = float (drive); p.bias = bias; p.mix = 1.0f; p.outputDb = 0.0f;
                p.autoComp = autoComp; p.dcBlockHz = dcBlockHz;
                stage.setParams (p);
                std::vector<float> held (4096, float (out[2 * i]));
                float* io[] { held.data() };
                settles = settles && stage.process (io, 1, int (held.size())) && std::fabs (double (held.back()) - out[2 * i + 1]) < 1e-4;
            }
        }
    ok (settles, "the chain's saturator, held at a level, settles on the curve's output — tanh, tube, transistor, tape");
    ok (Kit::saturationCurve (SaturationType::Atan, 3, 1, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType::Cubic, 3, 1, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType::Asym, 3, 1, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType (9), 3, 1, out).status == CodecStatus::Invalid,
        "atan, cubic and asym are the config's alone, and 9 is no type: refused");
    ok (Kit::saturationCurve (SaturationType::Tape, -0.5, 1, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType::Tape, std::numeric_limits<double>::quiet_NaN(), 1, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType::Tape, 1000, 1, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType::Tape, 3, 1.5, out).status == CodecStatus::Invalid
        && Kit::saturationCurve (SaturationType::Tape, 3, -0.1, out).status == CodecStatus::Invalid,
        "a negative or non-finite drive, one whose gain overflows a float, a mix outside its knob: refused");
    const auto shortOut = Kit::saturationCurve (SaturationType::Tape, 3, 1, std::span<double> (out, 2 * kKitSaturationPoints - 1));
    ok (shortOut.status == CodecStatus::TooSmall && shortOut.count == kKitSaturationPoints, "a short buffer: TooSmall and the points");
}

void theAbiAnswersAsTheCall()
{
    felitronics::test::group ("each fc_kit_* answers what its C++ call answers, and guards what a page hands over");
    bool text = true;
    for (std::size_t i = 0; i < 4; ++i)
        for (const char* lang : kLangs)
        {
            char out[512]; std::uint32_t written = 0;
            const auto st = fc_kit_text (kFacts[i], std::uint32_t (std::strlen (kFacts[i])), lang, 2, out, sizeof (out), &written);
            text = text && st == FC_SESSION_OK && std::string_view (out, written) == rendered (kFactValues[i], *text::Text::langOf (lang));
        }
    ok (text, "fc_kit_text");
    std::uint32_t needed = 0;
    ok (fc_kit_text (kFacts[0], std::uint32_t (std::strlen (kFacts[0])), "ru", 2, nullptr, 0, &needed) == FC_SESSION_ERR_TOO_SMALL
        && needed == rendered (kFactValues[0], Lang::Ru).size(), "a size query: TOO_SMALL and the bytes needed");
    char out[64]; std::uint32_t written = 0;
    ok (fc_kit_text (kFacts[0], std::uint32_t (std::strlen (kFacts[0])), "xx", 2, out, sizeof (out), &written) == FC_SESSION_ERR_CONTRACT,
        "an unknown language is a contract fault");
    ok (fc_kit_text (kFacts[0], std::uint32_t (std::strlen (kFacts[0])), "ru", 2, out, sizeof (out), nullptr) == FC_SESSION_ERR_NULL, "a null written");
    bool parse = true;
    for (const auto& c : kParses)
    {
        double value = 0; std::uint32_t refusal = 0;
        const auto st = fc_kit_parse (c.typed, std::uint32_t (std::strlen (c.typed)), c.lang, 2, c.field, c.rate, &value, &refusal);
        const auto want = Kit::parse (c.typed, *text::Text::langOf (c.lang), Term (c.field), c.rate);
        parse = parse && st == FC_SESSION_OK && refusal == unsigned (want.refusal)
            && sameBits (value, want.refusal == KitRefusal::None ? want.value : 0.0);
    }
    ok (parse, "fc_kit_parse");
    double value = 0; std::uint32_t refusal = 0;
    ok (fc_kit_parse ("1", 1, "en", 2, 17, 0, &value, &refusal) == FC_SESSION_ERR_CONTRACT
        && fc_kit_parse ("1", 1, "en", 2, 0x10003u, 0, &value, &refusal) == FC_SESSION_ERR_CONTRACT, "a field the kit has no knob for");
    alignas (8) unsigned char raw[16] {};
    ok (fc_kit_parse ("1", 1, "en", 2, 9, 0, reinterpret_cast<double*> (raw + 4), &refusal) == FC_SESSION_ERR_ALIGNMENT, "a misaligned value");
    bool knobs = true;
    for (const auto& k : kKnobs)
    {
        const Term f = Term (k.field);
        double travel[3], pos = 0, at = 0, heat[3];
        const auto t = Kit::travel (f); const auto p = Kit::position (f, k.value); const auto v = Kit::valueAt (f, 0.37); const auto h = Kit::heat (f, k.value);
        knobs = knobs && fc_kit_travel (k.field, travel) == FC_SESSION_OK && sameBits (travel[0], t.from) && sameBits (travel[1], t.to) && sameBits (travel[2], t.step)
            && fc_kit_position (k.field, k.value, &pos) == FC_SESSION_OK && sameBits (pos, p.value)
            && fc_kit_value_at (k.field, 0.37, &at) == FC_SESSION_OK && sameBits (at, v.value)
            && fc_kit_heat (k.field, k.value, heat) == FC_SESSION_OK && sameBits (heat[0], h.heat) && sameBits (heat[1], double (h.side))
            && sameBits (heat[2], h.window ? 1.0 : 0.0);
    }
    ok (knobs, "fc_kit_travel, fc_kit_position, fc_kit_value_at, fc_kit_heat");
    double pos = 0;
    ok (fc_kit_position (3, std::numeric_limits<double>::quiet_NaN(), &pos) == FC_SESSION_ERR_CONTRACT && fc_kit_travel (3, nullptr) == FC_SESSION_ERR_NULL,
        "a non-finite value and a null output");
    double zones[4]; std::uint32_t bits = 0;
    const auto z = Kit::monoZones();
    ok (fc_kit_mono_zones (zones) == FC_SESSION_OK && sameBits (zones[0], z.zones[0].fromHz) && sameBits (zones[3], z.zones[1].toHz)
        && fc_kit_mono_zones_at (120, &bits) == FC_SESSION_OK && bits == Kit::monoZonesAt (120), "fc_kit_mono_zones, fc_kit_mono_zones_at");
    bool eq = true;
    for (const auto& params : kEqParams)
        for (double rate : kEqRates)
        {
            double curve[2 * FC_SESSION_KIT_EQ_POINTS], peak[FC_SESSION_KIT_EQ_PEAK_VALUES];
            KitEq e; e.hpf = { params[0] > 0, params[1], std::int32_t (params[2]) }; e.tilt = { params[3] > 0, params[4] }; e.low = { params[5] > 0, params[6] };
            EqPoint want[kEqCurvePoints];
            const auto w = Kit::eqCurve (e, rate, want);
            eq = eq && fc_kit_eq_curve (params, rate, curve, peak) == FC_SESSION_OK && sameBits (peak[0], w.finding.hz) && sameBits (peak[1], w.finding.db)
                && sameBits (peak[2], w.finding.over ? 1.0 : 0.0)
                && sameBits (peak[3], w.finding.device == Device::Low ? double (FC_SESSION_DEVICE_LOW_SHELF) : double (FC_SESSION_DEVICE_TILT));
            for (std::size_t i = 0; i < kEqCurvePoints; ++i) eq = eq && sameBits (curve[2 * i], want[i].hz) && sameBits (curve[2 * i + 1], want[i].db);
        }
    ok (eq, "fc_kit_eq_curve");
    // ...with the EQ bands: zero gains are fc_kit_eq_curve's answer bit for bit; gains are the kit's curve with them; a gain
    // outside its domain (mud above 0 dB) is a contract fault.
    bool bands = true;
    for (const auto& params : kEqParams)
        for (double rate : kEqRates)
        {
            double flat[FC_SESSION_KIT_EQ_BANDS_PARAMS] {}, gains[FC_SESSION_KIT_EQ_BANDS_PARAMS] {};
            std::copy (std::begin (params), std::end (params), flat);
            std::copy (std::begin (params), std::end (params), gains);
            const double set[] { 2.5, -1.5, 3.0, -2.0, 1.0 };
            std::copy (std::begin (set), std::end (set), gains + FC_SESSION_KIT_EQ_PARAMS);
            double unticked[FC_SESSION_KIT_EQ_BANDS_PARAMS] {}, ua[2 * FC_SESSION_KIT_EQ_POINTS], pu[FC_SESSION_KIT_EQ_PEAK_VALUES];
            std::copy (std::begin (gains), std::end (gains), unticked);
            gains[FC_SESSION_KIT_EQ_BANDS_PARAMS - 1] = 1;
            double a[2 * FC_SESSION_KIT_EQ_POINTS], b[2 * FC_SESSION_KIT_EQ_POINTS], c[2 * FC_SESSION_KIT_EQ_POINTS];
            double pa[FC_SESSION_KIT_EQ_PEAK_VALUES], pb[FC_SESSION_KIT_EQ_PEAK_VALUES], pc[FC_SESSION_KIT_EQ_PEAK_VALUES];
            KitEq e; e.hpf = { params[0] > 0, params[1], std::int32_t (params[2]) }; e.tilt = { params[3] > 0, params[4] }; e.low = { params[5] > 0, params[6] };
            e.bands = { 2.5, -1.5, 3.0, -2.0, 1.0, true };
            EqPoint want[kEqCurvePoints];
            const auto w = Kit::eqCurve (e, rate, want);
            bands = bands && fc_kit_eq_curve (params, rate, a, pa) == FC_SESSION_OK && fc_kit_eq_curve_bands (flat, rate, b, pb) == FC_SESSION_OK
                && fc_kit_eq_curve_bands (gains, rate, c, pc) == FC_SESSION_OK && w.status == CodecStatus::Ok
                && std::memcmp (a, b, sizeof a) == 0 && std::memcmp (pa, pb, sizeof pa) == 0 && std::memcmp (pa, pc, sizeof pa) == 0
                && fc_kit_eq_curve_bands (unticked, rate, ua, pu) == FC_SESSION_OK && std::memcmp (a, ua, sizeof a) == 0
                && std::memcmp (pa, pu, sizeof pa) == 0;
            for (std::size_t i = 0; i < kEqCurvePoints; ++i) bands = bands && sameBits (c[2 * i], want[i].hz) && sameBits (c[2 * i + 1], want[i].db);
        }
    double mudUp[FC_SESSION_KIT_EQ_BANDS_PARAMS] = { 1, 30, 24, 1, 2, 0, 0, 0, 0.5, 0, 0, 0, 0 };
    double halfBands[FC_SESSION_KIT_EQ_BANDS_PARAMS] = { 1, 30, 24, 1, 2, 0, 0, 1, 0, 0, 0, 0, 0.5 };
    double curveB[2 * FC_SESSION_KIT_EQ_POINTS], peakB[FC_SESSION_KIT_EQ_PEAK_VALUES];
    ok (bands && fc_kit_eq_curve_bands (mudUp, 48000, curveB, peakB) == FC_SESSION_ERR_CONTRACT
        && fc_kit_eq_curve_bands (halfBands, 48000, curveB, peakB) == FC_SESSION_ERR_CONTRACT,
        "fc_kit_eq_curve_bands: at 0 dB fc_kit_eq_curve's answer, with gains the kit's curve, ticked off with gains no band, the peak "
        "still the shelves'; mud above 0 refused even ticked off, a tick neither 0 nor 1 a contract fault");
    double curve[2 * FC_SESSION_KIT_EQ_POINTS], peak[FC_SESSION_KIT_EQ_PEAK_VALUES];
    const double halfTick[FC_SESSION_KIT_EQ_PARAMS] = { 0.5, 30, 24, 1, 2, 0, 0 };
    ok (fc_kit_eq_curve (halfTick, 48000, curve, peak) == FC_SESSION_ERR_CONTRACT, "a tick that is neither 0 nor 1 is a contract fault");
    ok (fc_kit_eq_curve (kEqParams[0], 48000, curve, curve + 10) == FC_SESSION_ERR_OVERLAP, "overlapping outputs");
    double points[10]; std::uint32_t count = 0;
    double want[10];
    const auto w = Kit::lowEndCurve (kLowCentres, kLowEnergies, 25, 55, want);
    bool low = fc_kit_low_end_curve (kLowCentres, kLowEnergies, 5, 25, 55, points, 5, &count) == FC_SESSION_OK && count == w.count;
    for (std::uint32_t i = 0; i < 2 * count; ++i) low = low && sameBits (points[i], want[i]);
    ok (low && fc_kit_low_end_curve (kLowCentres, kLowEnergies, 5, 25, 55, points, 2, &count) == FC_SESSION_ERR_TOO_SMALL && count == 3,
        "fc_kit_low_end_curve, and TOO_SMALL with the points needed");
    double satCurve[2 * FC_SESSION_KIT_SATURATION_POINTS], satWant[2 * kKitSaturationPoints];
    bool sat = true;
    for (const std::uint32_t type : { 0u, 4u, 5u, 6u, 7u })
    {
        const auto w2 = Kit::saturationCurve (SaturationType (type), 4.5, 0.8, satWant);
        sat = sat && w2.status == CodecStatus::Ok && fc_kit_saturation_curve (type, 4.5, 0.8, satCurve) == FC_SESSION_OK;
        for (std::size_t i = 0; i < 2 * kKitSaturationPoints; ++i) sat = sat && sameBits (satCurve[i], satWant[i]);
    }
    ok (sat && fc_kit_saturation_curve (1, 4.5, 0.8, satCurve) == FC_SESSION_ERR_CONTRACT
        && fc_kit_saturation_curve (256, 4.5, 0.8, satCurve) == FC_SESSION_ERR_CONTRACT
        && fc_kit_saturation_curve (7, -1, 0.8, satCurve) == FC_SESSION_ERR_CONTRACT
        && fc_kit_saturation_curve (7, 4.5, 0.8, nullptr) == FC_SESSION_ERR_NULL,
        "fc_kit_saturation_curve: the kit's curve for the five types; atan, a number past the types, a negative drive refused, "
        "a null curve too");
}

void theCorpusIsTheWasmModulesBytes()
{
    felitronics::test::group ("native == wasm: the corpus session-check.mjs gives the module hashes to one value");
    // tools/wasm/session-check.mjs, "the pure kit", holds the wasm module to this same value.
    constexpr std::uint64_t kPinned = 0x7607472f9fe4c104ull;
    const auto h = corpusHash();
    std::printf ("    kit corpus: %016llx\n", static_cast<unsigned long long> (h));
    ok (h == kPinned, "the kit corpus hashes to the pinned value");
}
} // namespace

int main()
{
    std::printf ("felitronics session kit tests\n");
    textIsTextWrite();
    parseReadsTheField();
    travelAndHeat();
    zonesAndAdvice();
    eqPreview();
    lowEndCurve();
    saturationCurve();
    theAbiAnswersAsTheCall();
    theCorpusIsTheWasmModulesBytes();
    return felitronics::test::report();
}
