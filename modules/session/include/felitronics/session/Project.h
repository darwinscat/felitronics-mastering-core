// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

//==============================================================================
// felitronics::session — THE PROJECT: the one structure that holds what a master is made from — the target and a
// person's edits of its numbers, the manual mode, and every device's parameters. The session
// keeps one, changes it only through its commands (Commands.h), and moves its revision with every change; the project
// file is this structure printed.
//
// ONE PARAMETER FORM PER DEVICE. A device's parameters are its knobs, as typed fields with the knobs' names — no string
// addresses anywhere. Each device's fields are written ONCE, below, as a template over the FORM a field takes, and used
// in three forms:
//   Value    every field says a value                    the machine's layer: complete once the devices are placed
//   Touched  a field says a value only where touched     a person's layer: a touched field belongs to the person, even
//                                                        where its number equals the machine's
//   Mark     a field says yes or no                      which fields: what revertEdits takes back
// So the machine's layer, a person's edits and the mask of a revert cannot disagree on a field's name or type: they are
// one list. A master is made from the machine's layer with a person's touched fields over it.
//
// THE MACHINE'S LAYER is the planner's (src/Planner.h): each device proposes its fields from the target, the source and
// what it may measure, starting from the config's defaults (src/Devices.h). It is placed when the first measurement ends
// and again on a change of target, and it is at its types' zeros before. An imported layer retains its saved
// values; where the planner would decide otherwise is exposed by the snapshot, and adoptMachine takes it.
//
// The units, knob domains and slider hints are the config's (modules/session/config/engine.toml, the section of each device);
// they are not repeated here.
namespace felitronics::session
{

struct Version
{
    std::uint32_t major = 0, minor = 0, patch = 0;
};

struct ProjectPosition
{
    std::uint32_t line = 0, column = 0;
};

template <class T> using Value = T;
template <class T> using Touched = std::optional<T>;
template <class T> using Mark = bool;

// The limiter's "cut needles" knob: the peak clipper inside the limiter decides by itself, at a threshold a person sets,
// or not at all.
enum class Needles : std::uint8_t { Auto, Manual, Off };

// HOW A LANDING TAKES ITS LOUDNESS: the target's number (Manual), or a max mode — as loud as its promise allows
// ([landing.max] in engine.toml): MaxClean, the damage not heard; MaxDense, heard but not annoying; MaxExtreme, the
// loudest, its damage accepted.
enum class LoudnessMode : std::uint8_t { Manual, MaxClean, MaxDense, MaxExtreme };

// THE TARGET'S NUMBERS a person may edit in place ([edit] in targets.toml): the loudness, LUFS, and the true-peak ceiling,
// dBTP — and the loudness mode (the target row's when untouched). The other numbers of a target come with its name.
template <template <class> class F> struct TargetFields
{
    F<double> lufs {};
    F<double> tp {};
    F<LoudnessMode> loudnessMode {};
};

// THE DEVICES — eight devices, including the separate tilt and low tasks on every target. A device is a
// task with its knobs and its tick (`on`), not a stage of the chain: the high-pass, tilt and the low shelf write one EQ
// stage. The limiter is always on and has no tick.

// [hpf]: the cutoff, Hz, and the slope, dB/oct.
template <template <class> class F> struct HpfFields
{
    F<bool> on {};
    F<double> fq {};
    F<std::int32_t> slope {};
};

// [monoBass]: the crossover, Hz, and the width kept below it (0 is mono).
template <template <class> class F> struct MonoBassFields
{
    F<bool> on {};
    F<double> fq {};
    F<double> width {};
};

// [glue]: the glue knob, "up to N dB" — 0 takes the compressor out of the chain — and its mix (v0.17.0): the compressed
// share of its output, the rest dry (1: the downward glue).
template <template <class> class F> struct GlueFields
{
    F<bool> on {};
    F<double> upToDb {};
    F<double> mix {};
    // The five by hand (07.10): the compressor's threshold (dBFS on the input brought to [input] referenceLufs), ratio,
    // knee (dB), attack and release (ms). The machine's layer holds the travel's values at its own amount; what sounds
    // is a person's field where set, else the travel at the amount as it sounds.
    F<double> thresholdDb {};
    F<double> ratio {};
    F<double> kneeDb {};
    F<double> attackMs {};
    F<double> releaseMs {};
    // THE WATERFALL (MVP): a person's wish, 0…1, of the share of the peak work at the landing the glue takes — the master
    // steers its mix towards it. A person's field alone: the machine's 0 is never read.
    F<double> share {};
};

// The saturation's type: the shaper's curve, in felitronics-core's WaveShaper::Shape order and values. The machine's layer
// takes [saturation] shape of the config, any of the eight; a person picks one of the types the page offers — Tanh,
// Tube, Transistor, Transformer, Tape, and the two diodes (owner, 07.10): Cubic, the symmetric, and Asym, the asymmetric.
// Atan stays the config's (research): a hand edit refuses it.
enum class SaturationType : std::uint8_t { Tanh, Atan, Cubic, Asym, Tube, Transistor, Transformer, Tape };

// [saturation]: the drive, dB from the programme's peak; the mix, 0…1; the type (a person's choice only:
// the machine never picks one).
template <template <class> class F> struct SaturationFields
{
    F<bool> on {};
    F<double> drive {};
    F<double> mix {};
    F<SaturationType> type {};   // field 4 (3, the output, left in v0.6.0: the landing undid any trim)
    F<double> share {};          // the waterfall (MVP): the wished share of the peak work, steered through the mix
};

// [tilt]: the tilt, dB.
template <template <class> class F> struct TiltFields
{
    F<bool> on {};
    F<double> db {};
};

// [limiter]: the peak clipper's mode and its manual threshold, dB above the ceiling. The ceiling is the target's tp.
template <template <class> class F> struct LimiterFields
{
    F<Needles> needles {};
    F<double> needlesDb {};
    // The three by hand (08.10): the limiter's release (ms), its lookahead (ms) and the oversampling its detector and gain
    // run at (a factor, one of [limiter] oversamplingDomain's powers of two). The machine's layer holds [limiter]
    // releaseMs and lookaheadMs and [chain] oversampleFactor; a person's field wins for that field alone. The oversampling
    // is the chain's: the saturation stage, when it sounds, runs at the same factor.
    F<double> releaseMs {};
    F<double> lookaheadMs {};
    F<std::int32_t> oversampling {};
    // The waterfall (MVP): the wished share of the peak work the needles' clipper takes, steered through its cut within
    // [limiter.peakClipper] manualDomain; the limiter takes the rest.
    F<double> cutShare {};
};

// [dither]: on a delivery of 16 bits.
template <template <class> class F> struct DitherFields
{
    F<bool> on {};
};

// [low]: the low shelf's gain, dB — offered on every target.
template <template <class> class F> struct LowFields
{
    F<bool> on {};
    F<double> db {};
};

// [bands]: the five static EQ bands' gains, dB — a person's only: the machine leaves them at 0 (it does not touch timbre).
// A band at 0 dB is neutral and out of the EQ stage. The tick (`on`, appended after the gains) takes the whole device in
// or out of the chain with its gains kept: the machine's layer always has it on, a person may untick it. The device
// sounds where it is on and any band is not 0.
template <template <class> class F> struct BandsFields
{
    F<double> body {};
    F<double> mud {};
    F<double> forward {};
    F<double> brightness {};
    F<double> air {};
    F<bool> on {};
};

// WHERE A DEVICE'S TICK COMES FROM — a person's edit always sounds. Their own tick when they set one, on or off (Hand);
// otherwise ON when any of the device's fields carries their value (Touched: `[tilt] db.hand = 3` sounds with no tick
// written); otherwise the machine's (Machine). What sounds is the machine's layer with a person's fields over it and this
// tick; a change of target resets a person's layer, and with it the tick it gave.
enum class TickFrom : std::uint8_t { Machine, Hand, Touched };

// The devices, in the order Devices below holds them (and a device edit's alternatives, Commands.h, are listed).
enum class Device : std::uint8_t { Hpf, MonoBass, Glue, Saturation, Tilt, Limiter, Dither, Low, Bands };

// A device's two layers: the machine's, complete, and a person's, only what was touched.
template <template <template <class> class> class Fields> struct Layers
{
    Fields<Value> machine {};
    Fields<Touched> hand {};
};

struct Devices
{
    Layers<HpfFields> hpf;
    Layers<MonoBassFields> monoBass;
    Layers<GlueFields> glue;
    Layers<SaturationFields> saturation;
    Layers<TiltFields> tilt;
    Layers<LimiterFields> limiter;
    Layers<DitherFields> dither;
    Layers<LowFields> low;
    Layers<BandsFields> bands;
};

// Values use the field's native numeric domain: flags 0/1, choices their enum value.
struct MachineDifference
{
    Device device = Device::Hpf;
    std::uint8_t field = 0;
    double fileValue = 0.0, coreValue = 0.0;
};
inline constexpr std::size_t kDeviceFields = 34;

struct Project
{
    // The target: a row of [targets] in targets.toml, counted in the order the rows are written (the session's
    // targetName() gives its key), and a person's edits of its numbers.
    std::uint16_t target = 0;
    TargetFields<Touched> targetEdit {};
    // The device panel's visibility only. Touched fields remain owned and effective when it is hidden.
    bool manual = false;
    Devices devices {};
};

} // namespace felitronics::session
