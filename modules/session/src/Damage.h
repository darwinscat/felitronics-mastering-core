// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/LandingResult.h>
#include <felitronics/session/Events.h>
#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/analysis/Peaq.h>
#include <felitronics/analysis/StreamingLoudnessMeter.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace felitronics::session::detail
{
//==============================================================================
// THE DAMAGE OF A MASTER, HEARD ([cost.damage] in engine.toml): PEAQ Basic of the master against the same chain with its
// dynamics at rest, in windows. Two chains of one topology — the master's without its dither — run side by side at
// 48 kHz on the same input block: the master's with the parameters it was delivered with, the reference's with the glue
// in its exact bypass and the chain fed referenceBelowDb lower — by its input gain, and where that knob's range
// (MasteringChain::kMaxGainDb) stops short, by a scale of its input for the rest — so the saturation runs in its
// small-signal line and the limiter and its needles never reach the ceiling. Same stages, same oversamplers, same
// delays: a stage at rest leaves the two equal to a gain's rounding (the reference's lowering and lifting back). Two
// walks over the source: the first measures both integrated loudnesses (the gain that brings the reference to the
// master's), the second feeds the K PEAQ instances, one per window in flight.
//
// The source reaches 48 kHz through DamageResampler where it is not at 48 kHz already: a windowed sinc whose kernel is
// built from core::det, summed in a fixed order, so native and wasm give the same bits (core's DeliveryResampler designs
// its kernel with the platform's libm). Its quality is the analysis's, not a delivery's: both chains take its output.
// From 44.1 kHz it is flat to 16.4 kHz (-0.01 dB), -1 dB at 18.3, -6 dB at 19.9 and -60 dB from 23.3 kHz, nothing above
// 24 kHz louder than -75 dB; from 96 kHz flat to 17.9 kHz, -6 dB at 21.6 and -60 dB from 25.4 kHz (its 24-25.4 kHz edge
// folds back at -28 dB or less). PEAQ Basic reads to 18 kHz.
//
// Memory: plan() prices everything begin() allocates; step() allocates nothing. Any slicing gives the same bits.
//==============================================================================

// A rational polyphase resampler to 48 kHz, kernel from deterministic math. Output n is the input at time n·down/up
// (input samples), the kernel's group delay trimmed off and the end fed with silence until the exact count is out.
class DamageResampler
{
public:
    struct Shape
    {
        bool ok = false;
        std::uint32_t up = 1, down = 1;
        int phaseTaps = 0;                       // taps per phase
        std::uint64_t kernel = 0;                // doubles: up · phaseTaps
        std::uint64_t bytes (int channels) const noexcept
        { return 8u * (kernel + 2u * std::uint64_t (phaseTaps) * std::uint64_t (channels)); }
    };
    static Shape shapeFor (std::uint32_t inRate, int taps) noexcept;
    static long long outFrames (const Shape& s, long long inFrames) noexcept;
    bool prepare (const Shape& s, int channels);
    void reset() noexcept;
    // At most this many outputs for n inputs.
    static int maxOutputs (const Shape& s, int n) noexcept;
    // n input frames (null planes: silence) -> the outputs they complete, trimmed, at most `remaining` of them.
    int push (const float* const* in, int n, float* const* out, long long remaining) noexcept;
private:
    Shape s_ {};
    int channels_ = 0, trim_ = 0, pos_ = 0;
    long long inIndex_ = 0, outIndex_ = 0;       // inputs consumed; outputs produced, the trimmed ones included
    std::unique_ptr<double[]> kernel_, history_;
};

struct DamageRules
{
    double windowSeconds = 0, hopSeconds = 0, referenceBelowDb = 0;
    std::array<double, 4> floors {};
    int taps = 0;
};

struct DamagePlan
{
    bool ok = false;
    MeasurementReason reason = MeasurementReason::Unsupported;
    DamageRules rules {};
    DamageResampler::Shape resampler {};
    bool resample = false;
    long long frames48 = 0, hop48 = 0;
    int instances = 0;
    mastering::MasteringChainConfig topology {};
    std::uint64_t bytes = 0, largestBlock = 0;
};

struct Inspector;   // the suites' seam (MasterJobTests); the library defines none

class Damage
{
public:
    static constexpr int kBlock = 1024;          // source frames a step reads at most
    static DamageRules configured() noexcept;
    // What a damage pass of this source and topology asks the heap for, or why it cannot run.
    static DamagePlan plan (std::uint32_t sourceRate, std::uint64_t sourceFrames, int channels,
                            mastering::MasteringChainConfig topology) noexcept;
    // Allocates per plan; `master` is the master's chain, prepared again here at 48 kHz without its dither, `winning` the
    // parameters it was delivered with. False: nothing to grade (the report says Unsupported).
    bool begin (const DamagePlan& plan, mastering::MasteringChain& master, const mastering::MasteringChainParams& winning,
                const float* const* source, std::uint64_t sourceFrames, int channels);
    // True when the work is done (published or refused).
    bool step (long long budget) noexcept;
    PhaseName phase() const noexcept { return stage_ == Stage::Loudness || stage_ == Stage::Gate ? PhaseName::Reference : PhaseName::Damage; }
    std::optional<double> walk() const noexcept;
    void publish (MasterDamage& out) const noexcept;
private:
    friend struct Inspector;
    enum class Stage : std::uint8_t { Idle, Loudness, Gate, Grade, Done };
    bool feed (long long budget) noexcept;          // one block of the source through both chains; false on a refusal
    void closeWindow (int instance) noexcept;
    void fail (MeasurementReason why) noexcept { reason_ = why; stage_ = Stage::Done; }

    DamagePlan plan_ {};
    mastering::MasteringChain reference_;
    mastering::MasteringChain* master_ = nullptr;
    DamageResampler resampler_;
    analysis::StreamingLoudnessMeter meters_[2];
    std::array<analysis::Peaq, 4> peaq_;
    std::array<long long, 4> window_ {};            // the window each instance holds, -1 for none
    std::unique_ptr<float[]> scratch_;               // the 48 kHz block, then the master's and the reference's copies
    int scratchFrames_ = 0, channels_ = 0;
    const float* const* source_ = nullptr;
    std::uint64_t sourceFrames_ = 0, read_ = 0;
    long long emitted_ = 0, delivered_ = 0, chainDelay_ = 0;   // chain frames out, frames past the chain's latency
    double gain_ = 1.0, lift_ = 1.0, masterLufs_ = 0, referenceLufs_ = 0;   // lift_: undoes referenceBelowDb for the meter's gates
    double inputScale_ = 1.0;                       // what the reference's input gain could not take of referenceBelowDb
    bool scaled_ = false;
    bool masterGated_ = false;
    Stage stage_ = Stage::Idle;
    MeasurementReason reason_ = MeasurementReason::NotImplemented;
    // The windows' tally.
    std::uint32_t windows_ = 0, audible_ = 0, ungraded_ = 0;
    long long worstWindow_ = -1;
    analysis::PeaqResult worst_ {};
    bool nonFinite_ = false, undefined_ = false, outOfRange_ = false;
};

// THE DAMAGE AS A JOB OF ITS OWN (Session::damageJob, the running grade): made when a grade the shell asked
// (command::GradeDamage) takes its turn in the queue — no master job and no source measurement running, its room checked
// against the capacity then (Session::startDamage) — from what its master kept (MasterRows: the walks' plan and the
// parameters it was delivered with). It owns what the walks need: the master's chain, prepared again by begin() at
// 48 kHz, and those parameters. A new master frees it (the grade parked, to be made again at its turn); cancel, forget of
// its master and a new source free it with the grade. bytes() is all it holds: the object, its master chain's
// construction and the walks' plan.
struct DamageJob
{
    static std::uint64_t bytes (const DamagePlan& plan) noexcept
    { return sizeof (DamageJob) + mastering::MasteringChain::constructBytes() + plan.bytes; }
    MasterId master = 0;
    DamagePlan plan {};
    mastering::MasteringChainParams winning {};
    mastering::MasteringChain chain;
    Damage damage;
    const float* source[2] {};               // the session's source planes, which the walks read (Damage keeps the array)
    bool begun = false, refused = false;     // refused: begin() could not prepare the walks
    std::uint32_t units = 0;
};
} // namespace felitronics::session::detail
