// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "Damage.h"
#include "Rules.h"
#include <felitronics/core/DetMath.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace felitronics::session::detail
{
namespace
{
constexpr std::uint32_t kPeaqRate = 48000u;
constexpr double kPi = 3.14159265358979323846;
double number (toml::embedded::View value) noexcept
{
    if (const auto decimal = value.decimal()) return decimal->toDouble();
    if (const auto integer = value.integer()) return double (*integer);
    return std::numeric_limits<double>::quiet_NaN();
}
std::uint32_t gcd (std::uint32_t a, std::uint32_t b) noexcept
{
    while (b != 0) { const auto t = a % b; a = b; b = t; }
    return a;
}
bool add (std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value > 9007199254740991ull - total) return false;
    total += value;
    return true;
}
} // namespace

//==============================================================================
DamageResampler::Shape DamageResampler::shapeFor (std::uint32_t inRate, int taps) noexcept
{
    Shape s;
    if (inRate < 8000u || taps < 8 || taps > 256) return s;
    const auto g = gcd (kPeaqRate, inRate);
    s.up = kPeaqRate / g;
    s.down = inRate / g;
    if (s.up > 4096u || s.down > 4096u) return s;
    // Going down, the passband narrows by up/down: the kernel lengthens by as much, so its transition keeps its width.
    const std::uint32_t stretch = s.down > s.up ? (s.down + s.up - 1u) / s.up : 1u;
    if (stretch > 16u) return s;
    s.phaseTaps = taps * int (stretch);
    s.kernel = std::uint64_t (s.up) * std::uint64_t (s.phaseTaps);
    if (s.kernel > (std::uint64_t (1) << 22)) return s;
    s.ok = true;
    return s;
}
long long DamageResampler::outFrames (const Shape& s, long long inFrames) noexcept
{
    return (inFrames * (long long) s.up + (long long) s.down - 1) / (long long) s.down;
}
int DamageResampler::maxOutputs (const Shape& s, int n) noexcept
{
    return int ((long long) n * (long long) s.up / (long long) s.down) + 2;
}
bool DamageResampler::prepare (const Shape& s, int channels)
{
    s_ = {};
    if (! s.ok || channels < 1 || channels > 2) return false;
    kernel_.reset (new double[std::size_t (s.kernel)]);
    history_.reset (new double[std::size_t (2 * s.phaseTaps * channels)]);
    s_ = s;
    channels_ = channels;
    // The prototype at the upsampled rate up·in: a Blackman-windowed sinc cut at 0.45 of the lower rate (0.9 of its
    // Nyquist), gain `up`, dealt into `up` phases (the band it leaves: Damage.h). Every term from core::det and IEEE
    // operations: the same bits everywhere.
    const long long n = (long long) s.kernel;
    const double centre = double (n - 1) / 2.0;
    const double cut = 0.45 / double (std::max (s.up, s.down));
    for (long long k = 0; k < n; ++k)
    {
        const double x = 2.0 * cut * (double (k) - centre);
        const double sinc = x == 0.0 ? 1.0 : core::det::sin (kPi * x) / (kPi * x);
        const double a = 2.0 * kPi * double (k) / double (n - 1);
        const double window = 0.42 - 0.5 * core::det::cos (a) + 0.08 * core::det::cos (2.0 * a);
        const double h = double (s.up) * 2.0 * cut * sinc * window;
        const long long phase = k % (long long) s.up, tap = k / (long long) s.up;
        kernel_[std::size_t (phase * s.phaseTaps + tap)] = h;
    }
    trim_ = int (std::floor (centre / double (s.down) + 0.5));
    reset();
    return true;
}
void DamageResampler::reset() noexcept
{
    if (! s_.ok) return;
    std::fill_n (history_.get(), std::size_t (2 * s_.phaseTaps * channels_), 0.0);
    pos_ = 0;
    inIndex_ = 0;
    outIndex_ = 0;
}
int DamageResampler::push (const float* const* in, int n, float* const* out, long long remaining) noexcept
{
    const int taps = s_.phaseTaps;
    int written = 0;
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < channels_; ++c)
        {
            double* h = history_.get() + std::size_t (2 * taps * c);
            const double x = in != nullptr ? double (in[c][i]) : 0.0;
            h[pos_] = x;
            h[pos_ + taps] = x;
        }
        // Every output whose newest input is this one: floor (n·down / up) == inIndex_.
        for (;;)
        {
            const long long u = outIndex_ * (long long) s_.down;
            if (u / (long long) s_.up != inIndex_) break;
            const long long phase = u % (long long) s_.up;
            if (outIndex_ >= trim_ && written < remaining)
            {
                const double* k = kernel_.get() + std::size_t (phase * taps);
                for (int c = 0; c < channels_; ++c)
                {
                    const double* h = history_.get() + std::size_t (2 * taps * c) + std::size_t (pos_ + taps);
                    double y = 0.0;
                    for (int j = 0; j < taps; ++j) y = y + k[j] * h[-j];
                    out[c][written] = float (y);
                }
                ++written;
            }
            ++outIndex_;
        }
        ++inIndex_;
        pos_ = pos_ + 1 == taps ? 0 : pos_ + 1;
    }
    return written;
}

//==============================================================================
DamageRules Damage::configured() noexcept
{
    const auto d = rules().engine.find ("cost").find ("damage");
    DamageRules r;
    r.windowSeconds = number (d.find ("windowSeconds"));
    r.hopSeconds = number (d.find ("hopSeconds"));
    r.referenceBelowDb = number (d.find ("referenceBelowDb"));
    const auto taps = d.find ("resamplerTaps").integer();
    r.taps = taps ? int (*taps) : 0;
    const auto floors = d.find ("gradeFloorsOdg");
    for (std::size_t i = 0; i < r.floors.size(); ++i)
        r.floors[i] = i < floors.size() ? number (floors[i]) : std::numeric_limits<double>::quiet_NaN();
    return r;
}

DamagePlan Damage::plan (std::uint32_t sourceRate, std::uint64_t sourceFrames, int channels,
                         mastering::MasteringChainConfig topology) noexcept
{
    DamagePlan p;
    p.rules = configured();
    const auto& r = p.rules;
    bool finite = std::isfinite (r.windowSeconds) && std::isfinite (r.hopSeconds) && std::isfinite (r.referenceBelowDb)
        && r.hopSeconds > 0.0 && r.taps >= 8;
    for (double f : r.floors) finite = finite && std::isfinite (f);
    if (! finite || channels < 1 || channels > analysis::Peaq::kMaxChannels || sourceFrames == 0
        || sourceFrames > std::uint64_t (std::numeric_limits<int>::max())) return p;
    topology.dither = false;                       // both chains before the dither: see the header
    p.topology = topology;
    p.resample = sourceRate != kPeaqRate;
    if (p.resample)
    {
        p.resampler = DamageResampler::shapeFor (sourceRate, r.taps);
        if (! p.resampler.ok) return p;
        p.frames48 = DamageResampler::outFrames (p.resampler, (long long) sourceFrames);
    }
    else p.frames48 = (long long) sourceFrames;
    if (p.frames48 <= 0 || p.frames48 > std::numeric_limits<int>::max()) return p;
    if (! mastering::MasteringChain::admits (double (kPeaqRate), channels, topology)) return p;
    const auto chainBytes = mastering::MasteringChain::prepareBytes (double (kPeaqRate), channels, topology);
    if (chainBytes == 0) return p;
    p.hop48 = (long long) std::floor (r.hopSeconds * double (kPeaqRate) + 0.5);
    p.instances = int (std::floor (r.windowSeconds / r.hopSeconds + 0.5));
    if (p.hop48 < 1 || p.instances < 1 || p.instances > 4) return p;
    const long long window48 = std::min ((long long) p.instances * p.hop48, p.frames48);
    const auto peaq = analysis::Peaq::storageFor (channels, window48);
    analysis::StreamingLoudnessMeter::Storage meter;
    if (! peaq.ok || ! analysis::StreamingLoudnessMeter::storageFor (double (kPeaqRate), double (p.frames48), meter)) return p;
    const int scratch = p.resample ? DamageResampler::maxOutputs (p.resampler, kBlock) : kBlock;
    const std::uint64_t scratchBytes = 3u * std::uint64_t (channels) * std::uint64_t (scratch) * sizeof (float);
    const std::uint64_t resamplerBytes = p.resample ? p.resampler.bytes (channels) : 0u;
    // Two chains at 48 kHz (the master's, prepared again, and the reference's), the four PEAQ objects and K of them sized
    // for a window, two meters for the programme, the blocks, the kernel; and an allocator's margin for every one of them.
    std::uint64_t total = 0;
    if (! add (total, 2u * chainBytes) || ! add (total, mastering::MasteringChain::constructBytes())
        || ! add (total, std::uint64_t (p.instances) * peaq.bytes())
        || ! add (total, 4u * analysis::Peaq::constructBytes())
        || ! add (total, 2u * meter.bytes()) || ! add (total, scratchBytes) || ! add (total, resamplerBytes)
        || ! add (total, 16u * 4096u)) return p;
    p.bytes = total;
    p.largestBlock = std::max ({ chainBytes, peaq.bytes(), meter.bytes(), scratchBytes,
                                 p.resample ? 8u * p.resampler.kernel : 0u });
    p.ok = true;
    p.reason = MeasurementReason::None;
    return p;
}

bool Damage::begin (const DamagePlan& plan, mastering::MasteringChain& master, const mastering::MasteringChainParams& winning,
                    const float* const* source, std::uint64_t sourceFrames, int channels)
{
    plan_ = plan;
    stage_ = Stage::Done;
    reason_ = MeasurementReason::Unsupported;
    if (! plan.ok) return false;
    master_ = &master;
    source_ = source;
    sourceFrames_ = sourceFrames;
    channels_ = channels;
    if (! master.prepare (double (kPeaqRate), channels, plan.topology)
        || ! reference_.prepare (double (kPeaqRate), channels, plan.topology)) return false;
    // The reference: the master's settings with its dynamics at rest — the glue in its exact bypass (ratio 1: a gain of
    // exactly 1.0f, the lookahead kept), the whole chain fed referenceBelowDb lower so the saturation stays in its
    // small-signal line and the limiter and its needles never reach the ceiling.
    // The input gain's range stops at -kMaxGainDb: what it cannot take of referenceBelowDb, a scale of the reference's
    // input takes before the chain, so it is the whole referenceBelowDb lower than the gain the master's chain applies.
    constexpr double range = mastering::MasteringChain::kMaxGainDb;
    const double applied = std::clamp (winning.inputGainDb, -range, range);
    const double wanted = applied - plan.rules.referenceBelowDb;
    auto quiet = winning;
    quiet.bypassCompressor = true;
    quiet.inputGainDb = std::max (wanted, -range);
    scaled_ = wanted < quiet.inputGainDb;
    inputScale_ = scaled_ ? core::det::pow10 ((wanted - quiet.inputGainDb) / 20.0) : 1.0;
    // The meter's absolute gate is not scale-free: the reference is measured lifted back by what it was lowered.
    lift_ = core::det::pow10 ((applied - wanted) / 20.0);
    master.setParams (winning);
    master.reset();
    reference_.setParams (quiet);
    reference_.reset();
    chainDelay_ = master.latencySamples();
    if (reference_.latencySamples() != chainDelay_) return false;
    for (auto& meter : meters_)
        if (! meter.prepareForSamples (double (kPeaqRate), channels, double (plan.frames48))) return false;
    const long long window48 = std::min ((long long) plan.instances * plan.hop48, plan.frames48);
    for (int k = 0; k < plan.instances; ++k)
        if (! peaq_[std::size_t (k)].prepare (channels, window48)) return false;
    if (plan.resample && ! resampler_.prepare (plan.resampler, channels)) return false;
    scratchFrames_ = plan.resample ? DamageResampler::maxOutputs (plan.resampler, kBlock) : kBlock;
    scratch_.reset (new float[3u * std::size_t (channels) * std::size_t (scratchFrames_)]);
    window_.fill (-1);
    read_ = 0; emitted_ = 0; delivered_ = 0;
    stage_ = Stage::Loudness;
    reason_ = MeasurementReason::None;
    return true;
}

std::optional<double> Damage::walk() const noexcept
{
    if ((stage_ != Stage::Loudness && stage_ != Stage::Grade) || plan_.frames48 <= 0) return std::nullopt;
    return double (delivered_) / double (plan_.frames48);
}

bool Damage::feed (long long budget) noexcept
{
    const int n48 = channels_ * scratchFrames_;
    float* in[2] { scratch_.get(), channels_ == 2 ? scratch_.get() + scratchFrames_ : nullptr };
    float* m[2] { scratch_.get() + n48, channels_ == 2 ? scratch_.get() + n48 + scratchFrames_ : nullptr };
    float* q[2] { scratch_.get() + 2 * n48, channels_ == 2 ? scratch_.get() + 2 * n48 + scratchFrames_ : nullptr };
    const int want = int (std::min<long long> (budget, kBlock));
    // The programme at 48 kHz, then the chains' latency in silence: frames48 + chainDelay_ frames through each chain.
    const long long fed = emitted_;
    int got = 0;
    if (fed < plan_.frames48)
    {
        const long long remaining = plan_.frames48 - fed;
        if (plan_.resample)
        {
            const int take = int (std::min<long long> (want, (long long) (sourceFrames_ - read_)));
            const float* planes[2] { source_[0] + read_, channels_ == 2 ? source_[1] + read_ : nullptr };
            got = take > 0 ? resampler_.push (planes, take, in, remaining) : resampler_.push (nullptr, want, in, remaining);
            read_ += std::uint64_t (std::max (take, 0));
        }
        else
        {
            got = int (std::min<long long> (want, remaining));
            for (int c = 0; c < channels_; ++c) std::copy_n (source_[c] + read_, got, in[c]);
            read_ += std::uint64_t (got);
        }
    }
    else
    {
        got = int (std::min<long long> ({ (long long) want, (long long) scratchFrames_, plan_.frames48 + chainDelay_ - fed }));
        for (int c = 0; c < channels_; ++c) std::fill_n (in[c], got, 0.0f);
    }
    if (got <= 0) return true;
    for (int c = 0; c < channels_; ++c)
    {
        std::copy_n (in[c], got, m[c]);
        if (! scaled_) std::copy_n (in[c], got, q[c]);
        else for (int i = 0; i < got; ++i) q[c][i] = float (double (in[c][i]) * inputScale_);
    }
    if (! master_->process (m, channels_, got) || ! reference_.process (q, channels_, got)) return false;
    // OfflineRenderer's alignment, out[n] = y[n + D]: the first D frames out are the chains' latency.
    const int skip = int (std::min<long long> (got, std::max (0LL, chainDelay_ - emitted_)));
    const int count = int (std::min<long long> (got - skip, plan_.frames48 - delivered_));
    emitted_ += got;
    if (count <= 0) return true;
    const float* mm[2] { m[0] + skip, channels_ == 2 ? m[1] + skip : nullptr };
    float* qq[2] { q[0] + skip, channels_ == 2 ? q[1] + skip : nullptr };
    if (stage_ == Stage::Loudness)
    {
        for (int c = 0; c < channels_; ++c)
            for (int i = 0; i < count; ++i) qq[c][i] = float (double (qq[c][i]) * lift_);
        const float* qc[2] { qq[0], qq[1] };
        if (! meters_[0].process (mm, channels_, count) || ! meters_[1].process (qc, channels_, count)) return false;
    }
    else
    {
        for (int c = 0; c < channels_; ++c)
            for (int i = 0; i < count; ++i) qq[c][i] = float (double (qq[c][i]) * gain_);
        // PEAQ's order: the reference, then the test.
        long long t = delivered_;
        int off = 0;
        while (off < count)
        {
            const long long hop = t / plan_.hop48;
            if (t == hop * plan_.hop48)
            {
                // A window starts here: its instance closes the one it held (which ends here) and takes the new one, if
                // the new one reaches past the coverage of the one before it.
                const int k = int (hop % plan_.instances);
                if (window_[std::size_t (k)] >= 0) closeWindow (k);
                if (hop == 0 || hop * plan_.hop48 + (plan_.instances - 1) * plan_.hop48 < plan_.frames48)
                { peaq_[std::size_t (k)].reset(); window_[std::size_t (k)] = hop; }
            }
            const int len = int (std::min<long long> (count - off, (hop + 1) * plan_.hop48 - t));
            const float* ref[2] { qq[0] + off, channels_ == 2 ? qq[1] + off : nullptr };
            const float* test[2] { mm[0] + off, channels_ == 2 ? mm[1] + off : nullptr };
            for (int k = 0; k < plan_.instances; ++k)
                if (window_[std::size_t (k)] >= 0 && ! peaq_[std::size_t (k)].process (ref, test, channels_, len)) return false;
            off += len;
            t += len;
        }
    }
    delivered_ += count;
    return true;
}

void Damage::closeWindow (int k) noexcept
{
    auto& p = peaq_[std::size_t (k)];
    p.finish();
    const auto& r = p.result();
    const long long at = window_[std::size_t (k)];
    window_[std::size_t (k)] = -1;
    if (r.verdict == analysis::PeaqVerdict::Graded || r.verdict == analysis::PeaqVerdict::Transparent)
    {
        ++windows_;
        if (r.odg < plan_.rules.floors[0]) ++audible_;
        if (worstWindow_ < 0 || r.odg < worst_.odg || (! (worst_.odg < r.odg) && at < worstWindow_))
        { worst_ = r; worstWindow_ = at; }
        return;
    }
    ++ungraded_;
    nonFinite_ = nonFinite_ || r.verdict == analysis::PeaqVerdict::NonFinite;
    undefined_ = undefined_ || r.verdict == analysis::PeaqVerdict::Undefined;
    outOfRange_ = outOfRange_ || r.verdict == analysis::PeaqVerdict::OutOfRange;
}

bool Damage::step (long long budget) noexcept
{
    if (stage_ == Stage::Done || stage_ == Stage::Idle) return true;
    if (budget <= 0) return false;
    if (stage_ == Stage::Loudness || stage_ == Stage::Grade)
    {
        if (! feed (budget)) { fail (MeasurementReason::NonFinite); return true; }
        if (delivered_ < plan_.frames48) return false;
        if (stage_ == Stage::Grade)
        {
            for (int k = 0; k < plan_.instances; ++k)
                if (window_[std::size_t (k)] >= 0) closeWindow (k);
            stage_ = Stage::Done;
            return true;
        }
        meters_[0].beginIntegratedScan();
        meters_[1].beginIntegratedScan();
        masterGated_ = false;
        stage_ = Stage::Gate;
        return false;
    }
    // Stage::Gate: the two integrated loudnesses, the master's first; then the gain, and the second walk from the top.
    auto& meter = meters_[masterGated_ ? 1 : 0];
    int used = 0;
    const bool done = meter.stepIntegratedScan (int (std::min<long long> (budget, 256)), masterGated_ ? referenceLufs_ : masterLufs_, used);
    if (! done) return false;
    if (! masterGated_) { masterGated_ = true; return false; }
    if (! std::isfinite (masterLufs_) || ! std::isfinite (referenceLufs_) || masterLufs_ <= -70.0 || referenceLufs_ <= -70.0)
    { fail (MeasurementReason::NoSignal); return true; }
    gain_ = lift_ * core::det::pow10 ((masterLufs_ - referenceLufs_) / 20.0);
    master_->reset();
    reference_.reset();
    if (plan_.resample) resampler_.reset();
    read_ = 0; emitted_ = 0; delivered_ = 0;
    stage_ = Stage::Grade;
    return false;
}

void Damage::publish (MasterDamage& out) const noexcept
{
    out.windowSeconds = plan_.rules.windowSeconds;
    out.hopSeconds = plan_.rules.hopSeconds;
    out.windows = windows_;
    out.audibleWindows = audible_;
    out.ungradedWindows = ungraded_;
    if (std::isfinite (masterLufs_) && std::isfinite (referenceLufs_) && stage_ == Stage::Done && reason_ == MeasurementReason::None)
        out.referenceGainDb = masterLufs_ - referenceLufs_;   // against the reference lifted back (lift_)
    // A walk that failed grades nothing, whatever windows closed before it failed.
    if (reason_ != MeasurementReason::None)
    {
        out.status = MeasurementStatus::Unavailable; out.reason = reason_; out.verdict = DamageVerdict::NotRun;
        return;
    }
    if (windows_ > 0)
    {
        const auto& f = plan_.rules.floors;
        const double odg = worst_.odg;
        out.status = MeasurementStatus::Ready;
        out.reason = MeasurementReason::None;
        out.verdict = worst_.verdict == analysis::PeaqVerdict::Transparent ? DamageVerdict::Transparent : DamageVerdict::Graded;
        out.grade = odg >= f[0] ? 5u : odg >= f[1] ? 4u : odg >= f[2] ? 3u : odg >= f[3] ? 2u : 1u;
        out.worstOdg = odg;
        if (std::isfinite (worst_.distortionIndex)) out.worstDi = worst_.distortionIndex;
        out.worstFromSeconds = double (worstWindow_ * plan_.hop48) / double (kPeaqRate);
        out.audibleShare = double (audible_) / double (windows_);
        return;
    }
    out.status = MeasurementStatus::Unavailable;
    if (nonFinite_) { out.reason = MeasurementReason::NonFinite; out.verdict = DamageVerdict::NonFinite; return; }
    // A sample past PEAQ's range (+18 dBFS) — the reference, at the master's loudness without its limiter, can get there.
    if (outOfRange_) { out.reason = MeasurementReason::Unsupported; out.verdict = DamageVerdict::OutOfRange; return; }
    if (undefined_)
    {
        // A MOV averaged over no frame: a programme under half a second, or a reference with nothing PEAQ can read
        // (never above 8.1 kHz, nothing over the EHS floor).
        out.reason = plan_.frames48 < (long long) (kPeaqRate / 2u) ? MeasurementReason::TooShort : MeasurementReason::NoSignal;
        out.verdict = DamageVerdict::Undefined;
        return;
    }
    out.reason = MeasurementReason::NoSignal;
    out.verdict = DamageVerdict::NoSignal;
}
} // namespace felitronics::session::detail
