// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Project.h>
#include <felitronics/session/Session.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Text.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

//==============================================================================
// felitronics::session::Kit — THE PURE KIT (architecture §4.5): the answers a shell needs within one frame, on its UI
// thread, without a round trip to the session's worker — a published fact as text in the page's language, what a person
// typed into a knob's field, where a value stands along the knob's travel and how far it is from the knob's comfortable
// window ("heat"), mono bass's purpose zones, the EQ curve a set of knob values would draw, and a low-end curve from band
// energies. The shell computes none of it: it draws the answers.
//
// STATELESS AND ALLOCATION-FREE. Every member is a pure function of its arguments and of the config and catalogue
// compiled into the library; it writes only into the caller's spans and keeps nothing. So the same module that runs the
// session on a worker is instantiated a second time on the page's main thread and answers these there (fc_kit_* in
// tools/fc_session_abi.h). A kit answer is never truth the shell stores: the session's snapshot is.
//
// ONE SOURCE. Each answer delegates to the code the session itself uses for the same question: Text::write and the
// codec's own fact reader; Text::parse, the commands' domains (Knob::accepts, Rules::slope) and the config's grid; the
// config's travels, green windows, comfort and normal ranges and zones; the EQ stage's writeEq / eqCurve / eqFinding; the
// det-math zone's log10. The plan's advice about the high-pass's comfort and mono bass's zones reads its comparison from
// heat() and monoZonesAt() — so a knob's colour and the advice beside it never disagree.
//
// A FIELD is named by its text::Term (FieldTargetLufs … FieldLowDb): the id a refusal names the field by already, stable
// and append-only. A field the kit has no answer for (a choice, a tick, the audio, a term that is no field) is Invalid.
//
// Every answer carries a CodecStatus: Ok; Invalid (an unknown field or language, a non-finite or out-of-domain argument,
// a fact that is not one); TooSmall (the output span is shorter than the answer, whose size is given); and
// FloatingPointEnvironment (the calling thread's floating-point environment is not IEEE-754's default — checked first,
// as every computing call of the session does). As every public header of the session, this one carries no body.
namespace felitronics::session
{

// What a person typed, read for a field: the value to send, or why there is none.
enum class KitRefusal : std::uint8_t
{
    None = 0,          // accepted: `value` is the number to send
    NotANumber = 1,    // nothing Text::parse reads as a number in that language
    OutOfDomain = 2,   // a number, but — on the field's grid — one the command would refuse (or beyond ±2^53 at a place)
};

struct KitParsed
{
    CodecStatus status = CodecStatus::Ok;
    KitRefusal refusal = KitRefusal::NotANumber;
    double value = 0.0;
};

struct KitNumber
{
    CodecStatus status = CodecStatus::Ok;
    double value = 0.0;
};

// A knob's travel and step, as the config writes them — the slider's hints, not its domain.
struct KitTravel
{
    CodecStatus status = CodecStatus::Ok;
    double from = 0.0, to = 0.0, step = 0.0;
};

// HEAT: how far a value stands outside its knob's comfortable window, 0 … 1. 0 inside the window; outside it, the share
// of the way from the window's edge to the outer bound on that side, and 1 at or past that bound. side: −1 below the
// window, 0 inside, +1 above. The windows and their outer bounds, all from the config:
//   the target's loudness and ceiling   [edit] lufs/tp green, out to the knob's travel (targets.toml)
//   the high-pass's cutoff              [hpf] comfort lowHz…highHz, out to warningLowHz / warningHighHz
//   tilt and low                        [tilt]/[low] normal, out to hard
// A knob without a window answers window = false, heat 0, side 0.
struct KitHeat
{
    CodecStatus status = CodecStatus::Ok;
    bool window = false;
    double heat = 0.0;
    std::int8_t side = 0;
};

// Mono bass's purpose zones ([monoBass.zones]), in this order; what lies in none of them is what no destination asks for.
enum class KitZone : std::uint8_t { Club = 0, Vinyl = 1 };
inline constexpr std::size_t kKitZones = 2;
struct KitZoneSpan
{
    double fromHz = 0.0, toHz = 0.0;
};
struct KitZones
{
    CodecStatus status = CodecStatus::Ok;
    std::array<KitZoneSpan, kKitZones> zones {};
};

// The three EQ devices' knobs as they sound (Project.h's own field types): a preview of the EQ stage's curve.
struct KitEq
{
    HpfFields<Value> hpf {};
    TiltFields<Value> tilt {};
    LowFields<Value> low {};
};

struct KitEqPreview
{
    CodecStatus status = CodecStatus::Ok;
    EqFinding finding {};          // the shelves' peak against [eq] curve.warnDb, as the plan's EqOvershoot reads it
};

struct KitCount
{
    CodecStatus status = CodecStatus::Ok;
    std::uint64_t count = 0;       // bytes or points written — or, with TooSmall, needed
};

class Kit final
{
public:
    // A FACT IN A LANGUAGE: `wireFact` is a fact as the session publishes it ({"FactId":…,"args":[…]} — a snapshot's, an
    // event's, a plan's), read by the codec's own fact reader; the UTF-8 written is Text::write's. TooSmall with the size
    // when `out` is shorter. No terminator is written.
    [[nodiscard]] static KitCount text (std::string_view wireFact, text::Lang lang, std::span<char> out) noexcept;

    // WHAT A PERSON TYPED into a field: Text::parse in `lang` (its decimal sign or ".", the minus signs, no grouping);
    // on a knob whose whole travel lies at or below zero (the target's loudness and ceiling) a number typed without a
    // sign is read as negative ("14" is −14), and a sign typed is kept; the number is rounded to the knob's grid — its
    // travel's start plus a whole number of steps, a value exactly halfway taken away from zero, as Text rounds — in
    // decimal digits, and the result must lie in the domain the command checks (Knob::accepts: `sourceRate` is the
    // source's, for the high-pass's cutoff below its Nyquist). The high-pass's slope reads a whole number of dB/oct that
    // Rules::slope accepts, unrounded.
    [[nodiscard]] static KitParsed parse (std::string_view typed, text::Lang lang, text::Term field,
                                          std::uint32_t sourceRate) noexcept;

    // The knob's travel and step.
    [[nodiscard]] static KitTravel travel (text::Term field) noexcept;
    // Where `value` stands along the travel, 0 … 1 (a value past an end stands at that end).
    [[nodiscard]] static KitNumber position (text::Term field, double value) noexcept;
    // The value at `position` (clamped to 0 … 1) along the travel, on its grid: the step nearest, in decimal digits.
    [[nodiscard]] static KitNumber valueAt (text::Term field, double position) noexcept;
    // The heat of `value` on the knob (above).
    [[nodiscard]] static KitHeat heat (text::Term field, double value) noexcept;

    // Mono bass's purpose zones, and the zones holding `hz` (bounds included) as bits 1 << KitZone — 0 for none, or a
    // value that is not a number.
    [[nodiscard]] static KitZones monoZones() noexcept;
    [[nodiscard]] static std::uint32_t monoZonesAt (double hz) noexcept;

    // THE EQ CURVE the three EQ devices would draw at `rate` (a whole number of hertz, at least kMinSampleRate): the
    // stage written from `eq` as writeEq writes it from a project, summed on kEqCurvePoints into `out` as the snapshot's
    // eqCurve is, and the shelves' peak against warnDb as eqFinding states it. Each knob must lie in its command's
    // domain (the cutoff below rate / 2), else Invalid.
    [[nodiscard]] static KitEqPreview eqCurve (const KitEq& eq, double rate, std::span<EqPoint> out) noexcept;

    // A LOW-END CURVE from band energies: the bands whose centre lies in fromHz … toHz, in their order, each as its
    // centre and 10·log10 of its energy (−∞ for no energy), two doubles per point into `out` (hz, then dB — the
    // snapshot's row layout). `count` is in points. Fewer than two such bands are no curve: count 0. Centres must be
    // finite and positive, energies finite and not negative, the two spans of one length, fromHz ≤ toHz.
    [[nodiscard]] static KitCount lowEndCurve (std::span<const double> centreHz, std::span<const double> energy,
                                               double fromHz, double toHz, std::span<double> out) noexcept;
};

} // namespace felitronics::session
