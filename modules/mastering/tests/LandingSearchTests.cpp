// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/mastering/DeliveredMastering.h>
#include <felitronics_test.h>
#include "../../../tests/DeclaredBudget.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics;
using namespace felitronics::mastering;

namespace
{
constexpr int kRate = 48000;
constexpr int kFrames = kRate;

struct Rig
{
    MasteringChain chain;
    OfflineRenderer renderer;
    TargetLoudnessSolver solver;
    MasteringChainParams params;
    std::vector<float> source, output;
    const float* input[1] {};
    float* out[1] {};

    Rig() : source ((std::size_t) kFrames), output ((std::size_t) kFrames)
    { input[0] = source.data(); out[0] = output.data(); }

    bool prepare (bool limiter = true)
    {
        MasteringChainConfig config;
        config.eq = false; config.monoBass = false; config.compressor = false;
        config.clipper = false; config.limiter = limiter; config.dither = true;
        if (! chain.prepare (kRate, 1, config) || ! renderer.prepare (1, 257)
            || ! solver.prepare (kRate, 1, 257, chain.internalBlock(), chain.tapOversampleFactor())) return false;
        params.limiter.ceilingDbTp = -1.0;
        for (int i = 0; i < kFrames; ++i)
        {
            const double t = (double) i / kRate;
            source[(std::size_t) i] = (float) (0.12 * std::sin (6.283185307179586 * 117.0 * t)
                + 0.04 * std::sin (6.283185307179586 * 3061.0 * t));
            if (i % 4096 == 0) source[(std::size_t) i] += 0.7f;
        }
        return true;
    }
};

struct StopFinal { double fraction = 0.0; bool seen = false; };
struct StopGate { double threshold = 0.0; int checkpoints = 0; };
bool stopGate (void* context, const ProgressEvent& event)
{
    auto& stop = *static_cast<StopGate*> (context);
    if (event.stage == ProgressStage::SearchPass && event.fraction > stop.threshold
        && event.fraction < 1.0)
        return ++stop.checkpoints < 3;
    return true;
}
bool stopFinal (void* context, const ProgressEvent& event)
{
    auto& stop = *static_cast<StopFinal*> (context);
    if (event.stage == ProgressStage::FinalRender && event.fraction >= stop.fraction)
        { stop.seen = true; return false; }
    return true;
}

bool run (Rig& rig, long long budget, double target, int passes, LoudnessSolution& answer)
{
    LandingSearch search (rig.solver);
    LoudnessRequest req;
    req.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    req.targetLufs = target; req.maxTruePeakDbTp = -1.0; req.maxPasses = passes;
    if (! search.begin (rig.chain, rig.renderer, rig.params, rig.input, kFrames, kRate,
                        rig.out, 1, kFrames, req)) return false;
    StepResult state = StepResult::More;
    for (int guard = 0; state == StepResult::More && guard < 2000000; ++guard)
        state = search.step (budget);
    answer = search.result();
    return state == StepResult::Done;
}

bool runDelivered (long long budget, LoudnessSolution& answer, std::vector<float>& pcm)
{
    constexpr int sourceRate = 44100;
    MasteringChain chain;
    OfflineRenderer renderer;
    TargetLoudnessSolver solver;
    DeliveryConverter converter;
    MasteringChainConfig config;
    config.eq = false; config.monoBass = false; config.compressor = false;
    config.clipper = false; config.limiter = true; config.dither = true;
    if (! chain.prepare (kRate, 1, config) || ! renderer.prepare (1, 257)
        || ! solver.prepare (kRate, 1, 257, chain.internalBlock(), chain.tapOversampleFactor())
        || ! converter.prepare (sourceRate, kRate, 1, 257)) return false;
    MasteringChainParams params;
    params.limiter.ceilingDbTp = -1.0;
    std::vector<float> source (sourceRate), output (kFrames);
    for (int i = 0; i < sourceRate; ++i)
    {
        const double t = (double) i / sourceRate;
        source[(std::size_t) i] = (float) (0.16 * std::sin (6.283185307179586 * 101.0 * t)
            + 0.03 * std::sin (6.283185307179586 * 3301.0 * t));
        if (i % 4096 == 0) source[(std::size_t) i] += 0.7f;
    }
    const float* input[1] { source.data() };
    float* out[1] { output.data() };
    LoudnessRequest req;
    req.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    req.targetLufs = -14.0; req.maxTruePeakDbTp = -1.0; req.maxPasses = 12;
    LandingSearch search (solver);
    if (! search.begin (chain, renderer, params, input, sourceRate, sourceRate,
                        out, 1, kFrames, req, {}, &converter)) return false;
    StepResult state = StepResult::More;
    for (int guard = 0; state == StepResult::More && guard < 2000000; ++guard)
        state = search.step (budget);
    answer = search.result(); pcm = std::move (output);
    return state == StepResult::Done;
}

enum class MixFault { SubBass, SharpPeaks, Dark, LoudTarget };
bool diagnose (MixFault fault, LoudnessSolution& answer)
{
    Rig rig;
    if (! rig.prepare()) return false;
    for (int i = 0; i < kFrames; ++i)
    {
        const double t = (double) i / kRate;
        double x = 0.0;
        switch (fault)
        {
            case MixFault::SubBass:
                x = 0.4 * std::sin (6.283185307179586 * 50.0 * t)
                  + 0.03 * std::sin (6.283185307179586 * 3000.0 * t); break;
            case MixFault::SharpPeaks:
                x = 0.04 * std::sin (6.283185307179586 * 1000.0 * t)
                  + (i % 9600 == 0 ? 0.9 : 0.0); break;
            case MixFault::Dark:
                x = 0.15 * std::sin (6.283185307179586 * 400.0 * t); break;
            case MixFault::LoudTarget:
                x = 0.1 * std::sin (6.283185307179586 * 1000.0 * t)
                  + 0.1 * std::sin (6.283185307179586 * 4000.0 * t); break;
        }
        rig.source[(std::size_t) i] = (float) x;
    }
    return run (rig, LLONG_MAX, 10.0, 12, answer);
}

// LOUDNESS AS A REQUEST: a mono programme at `rate`, its momentary series as the session's loudness measurement keeps it
// (a hop of ten lround (0.01 fs) frames, value i the 400 ms ending at (i + 1) hops), and its gating blocks.
struct Programme
{
    std::vector<float> source;
    int rate = kRate;
    long long hop = 0;
    std::vector<double> momentary;
    std::vector<double> blocks;   // the source's BS.1770 gating block energies, block j starting at j hops

    Programme (std::vector<float> pcm, int sampleRate = kRate) : source (std::move (pcm)), rate (sampleRate)
    {
        hop = 10LL * std::max (1L, std::lround (0.01 * rate));
        analysis::LoudnessMeter meter;
        const int frames = (int) source.size();
        if (! meter.prepareForSamples (rate, 1, frames)) return;
        for (long long at = 0; at + hop <= frames; at += hop)
        {
            const float* planes[1] { source.data() + at };
            if (! meter.process (planes, 1, (int) hop)) { momentary.clear(); return; }
            momentary.push_back (meter.momentaryLufs());
        }
        const auto e = meter.gatingBlockEnergies();
        blocks.assign (e.begin(), e.end());
    }
};

struct Landed
{
    LoudnessSolution answer;
    std::vector<float> out;
    double landedLufs = std::numeric_limits<double>::quiet_NaN();
    bool done = false;
    long long steps = 0;
};

struct LandingSetup
{
    double target = -10.0, ceiling = -1.0;
    bool onSourceGate = false;
    double limiterBudgetDb = std::numeric_limits<double>::quiet_NaN();   // P95 on the active windows; NaN: none
    long long stepBudget = LLONG_MAX;
    int deliveryRate = 0;                                                 // 0: the programme's own
    double highPassHz = 0.0;                                              // > 0: a 96 dB/oct high-pass before the limiter
    double initialGainDb = std::numeric_limits<double>::quiet_NaN();
    int maxPasses = 12;
};

// Lands `p` through the limiter alone (and the high-pass when asked), delivered at `deliveryRate`.
Landed land (const Programme& p, const LandingSetup& w)
{
    Landed r;
    const int rate = w.deliveryRate > 0 ? w.deliveryRate : p.rate;
    const long long sourceFrames = (long long) p.source.size();
    const bool convert = rate != p.rate;
    const long long frames = convert ? DeliveryConverter::deliveredFrames (p.rate, rate, sourceFrames) : sourceFrames;
    MasteringChain chain; OfflineRenderer renderer; TargetLoudnessSolver solver; MasteringChainParams params;
    DeliveryConverter converter;
    MasteringChainConfig config;
    config.eq = w.highPassHz > 0.0; config.monoBass = false; config.compressor = false;
    config.clipper = false; config.limiter = true; config.dither = true;
    if (frames <= 0 || ! chain.prepare (rate, 1, config) || ! renderer.prepare (1, 257)
        || ! solver.prepare (rate, 1, 257, chain.internalBlock(), chain.tapOversampleFactor())
        || (convert && ! converter.prepare (p.rate, rate, 1, 257))) return r;
    params.limiter.ceilingDbTp = w.ceiling;
    if (w.highPassHz > 0.0)
    {
        auto& hpf = params.eqBands[0];
        hpf.on = true; hpf.type = eq::FilterType::HighPass;
        hpf.lane (eq::Lane::Stereo).on = true;
        hpf.lane (eq::Lane::Stereo).freq = w.highPassHz;
        hpf.lane (eq::Lane::Stereo).slope = 96;
    }
    r.out.assign ((std::size_t) frames, 0.0f);
    const float* in[1] { p.source.data() };
    float* out[1] { r.out.data() };
    LoudnessRequest req;
    req.targetLufs = w.target; req.ceilingMarginDb = 0.15; req.maxTruePeakDbTp = w.ceiling; req.maxPasses = w.maxPasses;
    req.initialGainDb = w.initialGainDb;
    req.productLanding = true;
    req.landingOnSourceGate = w.onSourceGate;
    if (w.onSourceGate)
    {
        req.sourceMomentaryLufs = p.momentary.data(); req.sourceMomentaryCount = (long long) p.momentary.size();
        req.sourceMomentaryHopFrames = p.hop;
    }
    if (std::isfinite (w.limiterBudgetDb)) { req.limiterGr.limitDb = w.limiterBudgetDb; req.limiterGr.statistic = GrStatistic::P95; }
    LandingSearch search (solver);
    if (! search.begin (chain, renderer, params, in, sourceFrames, p.rate, out, 1, (int) frames, req, {},
                        convert ? &converter : nullptr)) { r.answer = search.result(); return r; }
    StepResult state = StepResult::More;
    for (; state == StepResult::More && r.steps < 200000000LL; ++r.steps) state = search.step (w.stepBudget);
    r.answer = search.result();
    r.landedLufs = search.landedLufs();
    r.done = state == StepResult::Done && r.answer.deliverable;
    return r;
}

// THE LEVEL ON THE SOURCE'S GATE, worked out apart from LandingSearch: the source's gate from its own gating blocks
// (absolute −70 LUFS, then 10 LU under the mean of the absolutely gated), and each block of the master given the source
// block whose start is nearest in time — found by scanning, not by a formula.
double gateLevelOf (const Programme& p, const std::vector<float>& master, int masterRate)
{
    analysis::LoudnessMeter meter;
    if (! meter.prepareForSamples (masterRate, 1, (double) master.size())) return std::numeric_limits<double>::quiet_NaN();
    const float* planes[1] { master.data() };
    if (! meter.process (planes, 1, (int) master.size())) return std::numeric_limits<double>::quiet_NaN();
    const auto m = meter.gatingBlockEnergies();
    const double absolute = std::pow (10.0, (-70.0 + 0.691) / 10.0);
    double sum = 0.0; long long count = 0;
    for (const double e : p.blocks) if (e > absolute) { sum += e; ++count; }
    const double relative = count > 0 ? 0.1 * sum / (double) count : 0.0;
    const double hopM = 10.0 * std::max (1L, std::lround (0.01 * masterRate));
    double total = 0.0; long long taken = 0; std::size_t k = 0;
    for (std::size_t j = 0; j < m.size(); ++j)
    {
        const double t = (double) j * hopM / masterRate;
        while (k + 1 < p.blocks.size()
               && std::fabs ((double) (k + 1) * (double) p.hop / p.rate - t) <= std::fabs ((double) k * (double) p.hop / p.rate - t)) ++k;
        if (k < p.blocks.size() && p.blocks[k] > absolute && p.blocks[k] > relative && std::isfinite (m[j])) { total += m[j]; ++taken; }
    }
    return taken > 0 && total > 0.0 ? -0.691 + 10.0 * std::log10 (total / (double) taken) : std::numeric_limits<double>::quiet_NaN();
}

double lufsOfMono (const float* x, int frames, int rate = kRate)
{
    analysis::LoudnessMeter meter;
    if (! meter.prepareForSamples (rate, 1, frames)) return std::numeric_limits<double>::quiet_NaN();
    const float* planes[1] { x };
    if (! meter.process (planes, 1, frames)) return std::numeric_limits<double>::quiet_NaN();
    return meter.integratedLufs();
}

// Music in code: a kick every half second, a held "voice" tone, a little white and some pink noise.
void music (std::vector<float>& out, int from, int frames, std::uint32_t seed, int rate = kRate)
{
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double white = (double) (seed >> 8) / 8388608.0 - 1.0;
        b0 = 0.99765 * b0 + white * 0.0990460; b1 = 0.96300 * b1 + white * 0.2965164; b2 = 0.57000 * b2 + white * 1.0526913;
        const double t = (double) i / rate, beat = std::fmod (t, 0.5);
        const double kick = std::sin (6.283185307179586 * 55.0 * beat) * std::exp (-beat / 0.12);
        const double voice = 0.25 * std::sin (6.283185307179586 * 300.0 * t) * (0.6 + 0.4 * std::sin (6.283185307179586 * 0.5 * t));
        const double pink = 0.05 * (b0 + b1 + b2 + white * 0.1848);
        out[(std::size_t) (from + i)] = (float) (kick + voice + 0.02 * white + pink);
    }
}

void scaleTo (std::vector<float>& x, int from, int frames, double lufs, int rate = kRate)
{
    const double gain = std::pow (10.0, (lufs - lufsOfMono (x.data() + from, frames, rate)) / 20.0);
    for (int i = from; i < from + frames; ++i) x[(std::size_t) i] = (float) (x[(std::size_t) i] * gain);
}
} // namespace

int main()
{
    test::group ("landing search: one budget, saved output, and split invariant");
    Rig whole, sliced;
    if (! test::run (whole.prepare() && sliced.prepare())) return test::report();
    LoudnessSolution a, b;
    const bool doneA = run (whole, LLONG_MAX, -14.0, 12, a);
    const bool doneB = run (sliced, 173, -14.0, 12, b);
    test::ok (doneA && doneB && a.deliverable && b.deliverable
              && a.passes <= 12 && a.passes == a.logCount && b.passes == b.logCount,
              "both cuts deliver a ceiling-safe file with every render recorded inside twelve passes");
    test::ok (a.status == b.status && a.passes == b.passes && a.achievedLufs == b.achievedLufs
              && a.measured.truePeakDbTp == b.measured.truePeakDbTp
              && std::memcmp (whole.output.data(), sliced.output.data(), sizeof (float) * kFrames) == 0,
              "large and small budgets give the same status, measurement, and PCM");
    test::ok (a.measured.truePeakDbTp <= -1.0 && a.distanceLu == std::fabs (a.achievedLufs + 14.0),
              "the delivered reference true peak holds the ceiling and the miss is measured");

    Rig wrapper;
    if (! test::run (wrapper.prepare())) return test::report();
    LoudnessRequest wrappedRequest;
    wrappedRequest.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    wrappedRequest.targetLufs = -14.0; wrappedRequest.maxTruePeakDbTp = -1.0;
    wrappedRequest.maxPasses = 12; wrappedRequest.productLanding = true;
    const LoudnessSolution wrapped = wrapper.solver.solve (wrapper.chain, wrapper.renderer, wrapper.params,
        wrapper.input, wrapper.out, 1, kFrames, wrappedRequest);
    test::ok (wrapped.status == a.status && wrapped.passes == a.passes && wrapped.deliverable
              && std::memcmp (wrapper.output.data(), whole.output.data(), sizeof (float) * kFrames) == 0,
              "the whole solver call drives the same saved search and delivers identical PCM");
    Rig budgeted;
    if (! test::run (budgeted.prepare())) return test::report();
    const std::uint64_t declared = LandingSearch::storageFor (kRate, 1, kFrames, wrappedRequest.grTraceBuckets);
    const long long before = alloc::bytes.load();
    const LoudnessSolution counted = budgeted.solver.solve (budgeted.chain, budgeted.renderer, budgeted.params,
        budgeted.input, budgeted.out, 1, kFrames, wrappedRequest);
    const long long asked = alloc::bytes.load() - before;
    test::ok (counted.deliverable && declared > 0 && asked >= 0 && (std::uint64_t) asked <= declared,
              "the landing call and retained meters fit the declared fresh-call memory budget");
    const std::uint64_t plain = TargetLoudnessSolver::ordinarySolveBytes (
        kRate, 1, kFrames, wrappedRequest.grTraceBuckets);
    const std::uint64_t plainLong = TargetLoudnessSolver::ordinarySolveBytes (
        kRate, 1, 2 * kFrames, wrappedRequest.grTraceBuckets);
    const std::uint64_t search = LandingSearch::storageFor (
        kRate, 1, kFrames, wrappedRequest.grTraceBuckets, 0);
    const std::uint64_t searchLong = LandingSearch::storageFor (
        kRate, 1, 2 * kFrames, wrappedRequest.grTraceBuckets, 0);
    test::ok (searchLong - search == 2u * (plainLong - plain)
              && declared == TargetLoudnessSolver::solveCallBytes (
                  kRate, 1, kFrames, wrappedRequest)
              && declared == DeliveredMastering::solveCallBytes (
                  kRate, kRate, 1, kFrames, wrappedRequest),
              "the landing declaration retains no third full programme");
    const auto fourMinutes = LandingSearch::storageForProgramme (
        4LL * 60 * kRate, 2, kRate, 4 * 60 * kRate, wrappedRequest.grTraceBuckets);
    const std::uint64_t pcmBytes = 4u * 60u * kRate * 2u * sizeof (float);
    test::ok (fourMinutes.ok && fourMinutes.sourceBytes == pcmBytes
              && fourMinutes.outputBytes == pcmBytes
              && fourMinutes.maxLiveBytes == 2u * pcmBytes + fourMinutes.workspaceBytes
              && fourMinutes.largestBlockBytes >= pcmBytes,
              "a four-minute stereo quote names source and output PCM only, with a bounded largest block");
    LoudnessRequest invalid = wrappedRequest;
    invalid.normalizationGainDb = std::numeric_limits<double>::quiet_NaN();
    const long long refusalBytes = alloc::bytes.load();
    const LoudnessSolution refused = budgeted.solver.solve (budgeted.chain, budgeted.renderer,
        budgeted.params, budgeted.input, budgeted.out, 1, kFrames, invalid);
    const long long refusalAsked = alloc::bytes.load() - refusalBytes;
    test::ok (refused.status == MasteringSolveStatus::InvalidRequest
              && refusalAsked <= (long long) TargetLoudnessSolver::solutionReturnBytes(),
              "invalid normalization is refused without a programme allocation ("
              + std::to_string (refusalAsked) + " B, status " + std::to_string ((int) refused.status) + ")");
    MasteringChainParams invalidGainParams = budgeted.params;
    invalidGainParams.inputGainDb = std::numeric_limits<double>::quiet_NaN();
    const long long gainRefusalBytes = alloc::bytes.load();
    const LoudnessSolution badGain = budgeted.solver.solve (budgeted.chain, budgeted.renderer,
        invalidGainParams, budgeted.input, budgeted.out, 1, kFrames, wrappedRequest);
    const long long gainRefusalAsked = alloc::bytes.load() - gainRefusalBytes;
    test::ok (badGain.status == MasteringSolveStatus::InvalidRequest
              && gainRefusalAsked <= (long long) TargetLoudnessSolver::solutionReturnBytes(),
              "invalid input gain is refused before any programme allocation");

    LandingSearch reused (whole.solver);
    LoudnessRequest fresh;
    fresh.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    fresh.targetLufs = -14.0; fresh.maxTruePeakDbTp = -1.0; fresh.maxPasses = 1;
    bool reusable = reused.begin (whole.chain, whole.renderer, whole.params, whole.input,
        kFrames, kRate, whole.out, 1, kFrames, fresh);
    StepResult reuseState = StepResult::More;
    for (int guard = 0; reusable && reuseState == StepResult::More && guard < 1000; ++guard)
        reuseState = reused.step (LLONG_MAX);
    fresh.normalizationGainDb = std::numeric_limits<double>::quiet_NaN();
    const bool rejected = ! reused.begin (whole.chain, whole.renderer, whole.params, whole.input,
        kFrames, kRate, whole.out, 1, kFrames, fresh);
    const auto& clean = reused.result();
    test::ok (reusable && reuseState == StepResult::Done && rejected
              && clean.status == MasteringSolveStatus::InvalidRequest && ! clean.deliverable
              && clean.passes == 0 && clean.logCount == 0 && clean.measured.gatingBlocks == 0,
              "a refused reuse clears the previous deliverable and its measurements and history");
    const auto rejectsCleanlyAfterSuccess = [&] (const MasteringChainParams& badParams,
                                                  const LoudnessRequest& badRequest)
    {
        LoudnessRequest good;
        good.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
        good.targetLufs = -14.0; good.maxTruePeakDbTp = -1.0; good.maxPasses = 1;
        if (! reused.begin (whole.chain, whole.renderer, whole.params, whole.input,
                            kFrames, kRate, whole.out, 1, kFrames, good)) return false;
        StepResult state = StepResult::More;
        for (int guard = 0; state == StepResult::More && guard < 1000; ++guard)
            state = reused.step (LLONG_MAX);
        if (state != StepResult::Done || ! reused.result().deliverable) return false;
        if (reused.begin (whole.chain, whole.renderer, badParams, whole.input,
                          kFrames, kRate, whole.out, 1, kFrames, badRequest)) return false;
        const auto& result = reused.result();
        return result.status == MasteringSolveStatus::InvalidRequest && ! result.deliverable
            && result.passes == 0 && result.logCount == 0 && result.measured.gatingBlocks == 0;
    };
    LoudnessRequest validRequest;
    validRequest.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    validRequest.targetLufs = -14.0; validRequest.maxTruePeakDbTp = -1.0;
    validRequest.maxPasses = 1;
    MasteringChainParams secondBadParams = whole.params;
    secondBadParams.inputGainDb = std::numeric_limits<double>::quiet_NaN();
    LoudnessRequest badTarget = validRequest;
    badTarget.targetLufs = std::numeric_limits<double>::quiet_NaN();
    test::ok (rejectsCleanlyAfterSuccess (secondBadParams, validRequest)
              && rejectsCleanlyAfterSuccess (whole.params, badTarget),
              "bad input gain and malformed target clear a previously deliverable result");

    Rig lifecycle;
    if (! test::run (lifecycle.prepare())) return test::report();
    LandingSearch recycled (lifecycle.solver);
    LoudnessRequest recycleRequest;
    recycleRequest.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    recycleRequest.targetLufs = -14.0; recycleRequest.maxTruePeakDbTp = -1.0;
    recycleRequest.maxPasses = 1;
    bool lifecycleBudget = true;
    std::string lifecycleDetail;
    std::uint64_t grownSource = 0, grownOutput = 0, repeatedQuote = 0;
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        const int frames = cycle == 1 ? kFrames : kFrames / 2;
        const auto quote = recycled.storageForJob (frames, 1, kRate, frames,
                                                    recycleRequest.grTraceBuckets);
        StepResult state = StepResult::More;
        bool admitted = false;
        const auto spent = felitronics::declared::spend ([&]
        {
            admitted = recycled.begin (lifecycle.chain, lifecycle.renderer, lifecycle.params,
                lifecycle.input, frames, kRate, lifecycle.out, 1, frames, recycleRequest);
            for (int guard = 0; admitted && state == StepResult::More && guard < 200; ++guard)
            {
                state = recycled.step (LLONG_MAX);
                if (cycle == 0 && guard == 1) { recycled.cancel(); state = StepResult::Failed; }
            }
        });
        lifecycleBudget = lifecycleBudget && quote.ok && admitted
            && felitronics::declared::covers (quote.maxLiveBytes, spent)
            && quote.largestBlockBytes >= quote.sourceBytes
            && quote.largestBlockBytes >= quote.outputBytes
            && (cycle == 0 ? state == StepResult::Failed : state == StepResult::Done);
        lifecycleDetail += " cycle " + std::to_string (cycle) + ": "
            + felitronics::declared::describe (quote.maxLiveBytes, spent)
            + ", admitted=" + std::to_string (admitted)
            + ", state=" + std::to_string ((int) state)
            + ", status=" + std::to_string ((int) recycled.result().status)
            + ", passes=" + std::to_string (recycled.result().passes);
        if (cycle == 1) { grownSource = quote.sourceBytes; grownOutput = quote.outputBytes; }
        if (cycle == 2)
        {
            lifecycleBudget = lifecycleBudget && quote.sourceBytes >= grownSource
                && quote.outputBytes >= grownOutput;
            repeatedQuote = quote.maxLiveBytes;
        }
        if (cycle == 3) lifecycleBudget = lifecycleBudget && quote.maxLiveBytes == repeatedQuote;
    }
    test::ok (lifecycleBudget,
              "DeclaredBudget covers cancel, grow and shrink with retained two-buffer capacity"
              + lifecycleDetail);

    Rig one;
    if (! test::run (one.prepare())) return test::report();
    LoudnessSolution limited;
    const bool ended = run (one, 71, -6.0, 1, limited);
    test::ok (ended ? limited.passes == 1 && limited.logCount == 1 && limited.deliverable
                      && (limited.measured.truePeakDbTp <= -1.0
                          || (limited.peaksAboveCeiling && limited.status == MasteringSolveStatus::TargetUnreachable))
                    : ! limited.deliverable && limited.status == MasteringSolveStatus::Unavailable,
              "a one-render budget cannot hide a final render; a render above the ceiling is delivered only marked");

    // NO RENDER UNDER THE CEILING (owner, 01.10): without a limiter a loud target keeps every render above it — the
    // file is still delivered, the gentlest render measured (the smallest overshoot), verified and marked.
    Rig open;
    if (! test::run (open.prepare (false))) return test::report();
    LoudnessSolution over;
    const bool overEnded = run (open, 409, -3.0, 12, over);
    bool everyAbove = over.logCount > 1, gentlest = true;
    for (int i = 0; i < over.logCount; ++i)
    {
        everyAbove = everyAbove && over.log[i].truePeakDbTp > -1.0
            && (over.log[i].violated & constraintBit (MasteringConstraint::TruePeakCeiling)) != 0;
        gentlest = gentlest && over.measured.truePeakDbTp <= over.log[i].truePeakDbTp;
    }
    // Here the gentlest is the first render, so the reserved last pass restores it (a real, logged render).
    bool restored = false;
    for (int i = 0; i + 1 < over.logCount; ++i)
        restored = restored || core::exactlyEqual (over.log[i].truePeakDbTp, over.measured.truePeakDbTp);
    restored = restored && over.logCount == 12
        && core::exactlyEqual (over.log[over.logCount - 1].truePeakDbTp, over.measured.truePeakDbTp);
    test::ok (overEnded && over.deliverable && over.peaksAboveCeiling
              && over.status == MasteringSolveStatus::TargetUnreachable
              && over.binding == MasteringConstraint::TruePeakCeiling && over.measured.truePeakDbTp > -1.0
              && everyAbove && gentlest && restored && std::isfinite (over.achievedLufs) && over.passes <= 12,
              "no render under the ceiling: the gentlest measured render is delivered, marked above the ceiling");

    Rig miss;
    if (! test::run (miss.prepare())) return test::report();
    LoudnessSolution high;
    const bool highEnded = run (miss, 409, 10.0, 12, high);
    test::ok (highEnded && high.deliverable && ! high.peaksAboveCeiling && high.passes == 12 && high.logCount == 12
              && high.status != MasteringSolveStatus::Solved && high.measured.truePeakDbTp <= -1.0,
              "an over-loud target spends the full budget and keeps its best safe miss");
    bool restoredInBudget = false;
    for (int i = 0; i + 1 < high.logCount; ++i)
        restoredInBudget = restoredInBudget
            || (high.log[i].gainDb == high.log[high.logCount - 1].gainDb
                && high.log[i].ceilingDb == high.log[high.logCount - 1].ceilingDb);
    test::ok (restoredInBudget && high.preLimiterGainDb == high.log[11].gainDb
              && high.ceilingDbTp == high.log[11].ceilingDb
              && high.measured.integratedLufs == high.log[11].integratedLufs,
              "the final pass restores the chosen safe miss and remains inside the twelve logged renders");
    Rig finiteExtreme;
    if (! test::run (finiteExtreme.prepare())) return test::report();
    LoudnessSolution extremeResult;
    const bool extremeEnded = run (finiteExtreme, LLONG_MAX,
        std::numeric_limits<double>::max(), 3, extremeResult);
    // Each safe pass is the best so far, so pass 2 of 3 is the chosen one when the budget runs out: the reserved last
    // pass would render it a second time, so the search ends on it instead.
    bool rendersTwice = false;
    for (int i = 0; i + 1 < extremeResult.logCount; ++i)
        rendersTwice = rendersTwice || (extremeResult.log[i].gainDb == extremeResult.log[i + 1].gainDb
                                        && extremeResult.log[i].ceilingDb == extremeResult.log[i + 1].ceilingDb);
    test::ok (extremeEnded && extremeResult.status == MasteringSolveStatus::PassLimit
              && extremeResult.deliverable && extremeResult.passes == 2 && extremeResult.logCount == 2
              && ! rendersTwice && extremeResult.preLimiterGainDb == extremeResult.log[1].gainDb
              && extremeResult.achievedLufs < std::numeric_limits<double>::max()
              && extremeResult.distanceLu > 0.0,
              "a finite extreme target ends on its best last pass without rendering it twice, and never reports an "
              "invented hit (" + std::to_string (extremeResult.passes) + " passes)");

    LoudnessSolution deliveredWhole, deliveredSliced;
    std::vector<float> deliveredA, deliveredB;
    const bool deliveryA = runDelivered (LLONG_MAX, deliveredWhole, deliveredA);
    const bool deliveryB = runDelivered (173, deliveredSliced, deliveredB);
    test::ok (deliveryA && deliveryB && deliveredWhole.deliverable && deliveredSliced.deliverable
              && deliveredWhole.passes == deliveredSliced.passes && deliveredA == deliveredB,
              "SRC, render, drain, search and final measurement resume to identical delivered PCM");
    for (double fraction : { 0.25, 0.75 })
    {
        Rig cancelled;
        if (! test::run (cancelled.prepare())) return test::report();
        StopFinal stop { fraction, false };
        LoudnessRequest request;
        request.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
        request.targetLufs = -14.0; request.maxTruePeakDbTp = -1.0;
        request.maxPasses = 12; request.productLanding = true;
        const LoudnessSolution result = cancelled.solver.solve (cancelled.chain, cancelled.renderer,
            cancelled.params, cancelled.input, cancelled.out, 1, kFrames, request,
            ProgressCallback { &stopFinal, &stop });
        test::ok (stop.seen && result.status == MasteringSolveStatus::Cancelled && ! result.deliverable,
                  fraction < 0.5 ? "cancel during final PCM copy with no published file"
                                 : "cancel during final remeasurement with no published file");
    }
    Rig gated;
    if (! test::run (gated.prepare())) return test::report();
    LoudnessRequest gateRequest;
    gateRequest.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
    gateRequest.targetLufs = -14.0; gateRequest.maxTruePeakDbTp = -1.0;
    gateRequest.maxPasses = 12; gateRequest.productLanding = true;
    const long long passFrames = 2LL * kFrames + gated.chain.latencySamples();
    StopGate gateStop { (double) passFrames / (double) (passFrames + kBandGrStride + 6), 0 };
    const LoudnessSolution gateCancelled = gated.solver.solve (gated.chain, gated.renderer,
        gated.params, gated.input, gated.out, 1, kFrames, gateRequest,
        ProgressCallback { &stopGate, &gateStop });
    test::ok (gateStop.checkpoints == 3 && gateCancelled.status == MasteringSolveStatus::Cancelled
              && ! gateCancelled.deliverable,
              "cancellation between the saved gate and range stages publishes no unverified file");
    LoudnessSolution subBass, sharp, dark, demand;
    const bool diagnosed = diagnose (MixFault::SubBass, subBass) && diagnose (MixFault::SharpPeaks, sharp)
        && diagnose (MixFault::Dark, dark) && diagnose (MixFault::LoudTarget, demand);
    test::ok (diagnosed && subBass.mainReason == LandingReason::ExcessSubBass
              && sharp.mainReason == LandingReason::SharpPeaks
              && dark.mainReason == LandingReason::DarkMix
              && demand.mainReason == LandingReason::LoudnessDemand,
              "measured bass share, transient limiting, and presence distinguish four normal misses ("
              + std::to_string ((int) subBass.mainReason) + ", " + std::to_string ((int) sharp.mainReason)
              + ", " + std::to_string ((int) dark.mainReason) + ", " + std::to_string ((int) demand.mainReason)
              + "; sharp PLR " + std::to_string (sharp.measured.plrDb) + ", active "
              + std::to_string (sharp.measured.limiter.activeFraction)
              + "; bass share " + std::to_string (subBass.sourceSubBassShare)
              + ", GR " + std::to_string (subBass.limiterMeanReductionDb)
              + "; sharp share " + std::to_string (sharp.sourcePresenceShare)
              + ", max GR " + std::to_string (sharp.measured.limiter.maxDb)
              + "; dark share " + std::to_string (dark.sourcePresenceShare)
              + "; demand share " + std::to_string (demand.sourcePresenceShare) + ")");

    // THE FIRST CEILING IS THE CALLER'S MARGIN — Session's engine.toml [limiter] ceilingMarginDb — and not a second
    // copy of it in this class: a margin under 0.15 dB starts there, a wider one caps a looser parameter.
    const auto firstPass = [] (double parameterCeiling, double margin, LoudnessSolution& answer) -> bool
    {
        Rig r;
        if (! r.prepare()) return false;
        r.params.limiter.ceilingDbTp = parameterCeiling;
        LoudnessRequest req;
        req.ceilingMarginDb = margin;
        req.targetLufs = -14.0; req.maxTruePeakDbTp = -1.0; req.maxPasses = 1; req.productLanding = true;
        LandingSearch search (r.solver);
        const bool began = search.begin (r.chain, r.renderer, r.params, r.input, kFrames, kRate, r.out, 1, kFrames, req);
        StepResult state = began ? StepResult::More : StepResult::Failed;
        for (int guard = 0; state == StepResult::More && guard < 2000000; ++guard) state = search.step (LLONG_MAX);
        answer = search.result();
        return began;
    };
    LoudnessSolution narrow, wide, unstated, negative;
    const bool narrowRan = firstPass (-1.05, 0.05, narrow), wideRan = firstPass (-1.0, 0.4, wide);
    test::ok (narrowRan && wideRan && narrow.logCount >= 1 && wide.logCount >= 1
              && narrow.log[0].ceilingDb == std::fmin (-1.05, -1.0 - 0.05)
              && wide.log[0].ceilingDb == std::fmin (-1.0, -1.0 - 0.4),
              "the first limiter ceiling is the stated margin below the promise, 0.05 and 0.4 dB alike (got "
              + std::to_string (narrow.logCount >= 1 ? narrow.log[0].ceilingDb : 0.0) + ", "
              + std::to_string (wide.logCount >= 1 ? wide.log[0].ceilingDb : 0.0) + ")");
    test::ok (! firstPass (-1.0, std::numeric_limits<double>::quiet_NaN(), unstated)
              && ! firstPass (-1.0, -0.1, negative)
              && unstated.status == MasteringSolveStatus::InvalidRequest
              && negative.status == MasteringSolveStatus::InvalidRequest,
              "an unstated or negative ceiling margin is refused before the search begins");

    test::group ("landing search: a target between two achievable levels names both as numbers and still delivers");
    {
        // A tolerance finer than the search's own gain resolution closes the bracket with the target still between its
        // sides — LoudnessSolverTests' way to build the status on demand. Whatever the target, the two sides are numbers
        // and the file is delivered: a target that cannot be hit still returns the master (owner, 2026-10-01).
        int between = 0; bool finite = true, delivered = true;
        std::string seen;
        for (const double target : { -20.0, -18.0, -16.0, -14.0, -12.0, -10.0, -8.0 })
        {
            Rig r;
            if (! test::run (r.prepare())) return test::report();
            LoudnessRequest req;
            req.ceilingMarginDb = 0.15;   // Session's engine.toml [limiter] ceilingMarginDb
            req.targetLufs = target; req.maxTruePeakDbTp = -1.0; req.toleranceLu = 1.0e-9; req.maxPasses = 12;
            LandingSearch search (r.solver);
            StepResult state = search.begin (r.chain, r.renderer, r.params, r.input, kFrames, kRate, r.out, 1, kFrames, req)
                ? StepResult::More : StepResult::Failed;
            for (int guard = 0; state == StepResult::More && guard < 2000000; ++guard) state = search.step (LLONG_MAX);
            const LoudnessSolution& answer = search.result();
            seen += " " + std::to_string ((int) answer.status);
            if (answer.status != MasteringSolveStatus::TargetBetweenAchievable) continue;
            ++between;
            finite = finite && std::isfinite (answer.achievedBelowLufs) && std::isfinite (answer.achievedAboveLufs);
            delivered = delivered && state == StepResult::Done && answer.deliverable;
        }
        std::printf ("        statuses by target:%s\n", seen.c_str());
        test::ok (between > 0 && finite && delivered,
                  "every between verdict carries two finite levels and a delivered file (statuses:" + seen + ")");
    }

    {
        test::group ("loudness as a request: the level landed on the source's gate — the loud part lands as it would alone");
        // 30 s of music at about -35 LUFS, then 30 s of the same kind at about -20; the loud half alone beside it. Landed
        // on its own gate, the song drives its loud half harder than the loud half alone needs: the quiet half, lifted by
        // the drive, joins the master's relative gate and dilutes the integrated loudness.
        const int half = 30 * kRate;
        std::vector<float> loudPcm ((std::size_t) half), songPcm ((std::size_t) (2 * half));
        music (loudPcm, 0, half, 7u);
        music (songPcm, 0, half, 11u);
        music (songPcm, half, half, 7u);
        scaleTo (loudPcm, 0, half, -20.0);
        scaleTo (songPcm, 0, half, -35.0);
        scaleTo (songPcm, half, half, -20.0);
        const Programme loud (loudPcm), song (songPcm);
        if (! test::run (! song.momentary.empty() && ! loud.momentary.empty())) return test::report();
        const double target = -10.0;
        LandingSetup onOwn; onOwn.target = target;
        LandingSetup onGate = onOwn; onGate.onSourceGate = true;
        LandingSetup onGateSliced = onGate; onGateSliced.stepBudget = 173;
        const Landed alone = land (loud, onOwn), own = land (song, onOwn);
        const Landed gate = land (song, onGate), sliced = land (song, onGateSliced);
        const double aloneLoud = lufsOfMono (alone.out.data(), half);
        const double ownLoud = lufsOfMono (own.out.data() + half, half), gateLoud = lufsOfMono (gate.out.data() + half, half);
        const double reference = gateLevelOf (song, gate.out, kRate);
        std::printf ("        target %.0f: loud alone %.2f; on its own gate the loud half %.2f, file %.2f; on the source's gate "
                     "the loud half %.2f, landed %.2f (worked out apart: %.2f), file %.2f\n", target, aloneLoud, ownLoud,
                     own.answer.achievedLufs, gateLoud, gate.landedLufs, reference, gate.answer.achievedLufs);
        test::ok (alone.done && own.done && ownLoud - aloneLoud > 0.3 && std::isnan (own.landedLufs),
                  "PRECONDITION: on its own gate the song drives its loud half more than 0.3 LU past the loud half alone");
        test::ok (gate.done && gate.answer.status == MasteringSolveStatus::Solved && std::fabs (gateLoud - aloneLoud) <= 0.3
                  && gate.answer.measured.truePeakDbTp <= -1.0,
                  "on the source's gate the loud half lands within 0.3 LU of the loud half alone, under the ceiling");
        test::ok (std::fabs (gate.landedLufs - target) <= 0.1 && gate.answer.achievedLufs < gate.landedLufs - 0.3
                  && gate.answer.achievedLufs == gate.answer.measured.integratedLufs,
                  "the level landed is on the target; the file's BS.1770 reading, certified as before, is said apart from it");
        test::ok (std::fabs (gate.landedLufs - reference) <= 1.0e-6,
                  "the level landed is the master's level on the blocks the source's gate admitted, block for block in time");
        test::ok (sliced.done && sliced.steps > gate.steps && sliced.answer.achievedLufs == gate.answer.achievedLufs
                  && sliced.landedLufs == gate.landedLufs && sliced.answer.passes == gate.answer.passes
                  && std::memcmp (sliced.out.data(), gate.out.data(), sliced.out.size() * sizeof (float)) == 0,
                  "the gate's level does not depend on how the work is sliced: the same master bit for bit");

        // ...on two grids: a source at 11025 Hz reads its momentary loudness every 1100 frames, 99.77 ms; delivered at
        // 44.1 kHz the master's blocks step 100 ms. Matched by index, a minute drifts more than a reading.
        const int rate11 = 11025, half11 = 30 * rate11;
        std::vector<float> song11 ((std::size_t) (2 * half11));
        music (song11, 0, half11, 11u, rate11);
        music (song11, half11, half11, 7u, rate11);
        scaleTo (song11, 0, half11, -35.0, rate11);
        scaleTo (song11, half11, half11, -20.0, rate11);
        const Programme slow (song11, rate11);
        LandingSetup converted = onGate; converted.deliveryRate = 44100;
        const Landed delivered = land (slow, converted);
        const double reference44 = gateLevelOf (slow, delivered.out, 44100);
        std::printf ("        11025 Hz delivered at 44.1 kHz: landed %.3f (worked out apart: %.3f), file %.3f, status %d\n",
                     delivered.landedLufs, reference44, delivered.answer.achievedLufs, (int) delivered.answer.status);
        test::ok (delivered.done && reference44 > delivered.answer.achievedLufs + 0.3
                  && std::fabs (delivered.landedLufs - reference44) <= 1.0e-6 && std::fabs (delivered.landedLufs - target) <= 0.1,
                  "the source's readings at 11025 Hz reach the master's blocks at 44.1 kHz by time, not by index");

        // 180 s at 11025 Hz, silent but for a 1 kHz tone in its last 0.4 s, for cd (−9 LUFS, −0.3 dBTP, 44.1 kHz): by
        // index the tone's readings fall past the master's last block and no level was ever read.
        std::vector<float> lateTone ((std::size_t) (180 * rate11), 0.0f);
        for (std::size_t i = (std::size_t) (179.6 * rate11); i < lateTone.size(); ++i)
            lateTone[i] = (float) (0.25 * std::sin (6.283185307179586 * 1000.0 * (double) i / rate11));
        LandingSetup cd = onGate; cd.target = -9.0; cd.ceiling = -0.3; cd.deliveryRate = 44100;
        const Landed late = land (Programme (lateTone, rate11), cd);
        std::printf ("        a tone in the last 0.4 s of 180 s: status %d, file %.2f, landed %.2f, %d passes\n",
                     (int) late.answer.status, late.answer.achievedLufs, late.landedLufs, late.answer.passes);
        test::ok (late.done && late.answer.status != MasteringSolveStatus::Unavailable && std::isfinite (late.landedLufs),
                  "a source whose loud readings end its series still lands and delivers its file");

        // 0.8 s at 22050 Hz delivered at 44.1 kHz: the source's hop is 2210 frames, 100.23 ms, so the master's last blocks
        // fall past its last reading in time. They take the last reading, as every other block takes its nearest.
        const int rate22 = 22050;
        std::vector<float> brief22 ((std::size_t) (rate22 * 8 / 10));
        for (std::size_t i = 0; i < brief22.size(); ++i)
            brief22[i] = (float) ((i < (std::size_t) (rate22 / 5) ? 0.5 : 0.1) * std::sin (6.283185307179586 * 1000.0 * (double) i / rate22));
        LandingSetup tail = onGate; tail.target = -23.0; tail.deliveryRate = 44100;
        const Landed tailed = land (Programme (brief22, rate22), tail);
        std::printf ("        0.8 s at 22050 Hz delivered at 44.1 kHz, target −23: status %d, file %.3f, landed %.3f\n",
                     (int) tailed.answer.status, tailed.answer.achievedLufs, tailed.landedLufs);
        test::ok (tailed.done && tailed.answer.status == MasteringSolveStatus::Solved
                  && std::fabs (tailed.answer.achievedLufs - tail.target) <= 0.1,
                  "the master's blocks past the source's last reading take that reading: the file lands on the target");

        // 18 s of a 30 Hz sine, then 2 s of 1 kHz, through a high-pass at 80 Hz, 96 dB/oct: the blocks the source's gate
        // admitted are the sine's, and the high-pass empties them. Their level cannot hold the file's down.
        std::vector<float> subPcm ((std::size_t) (20 * kRate));
        for (std::size_t i = 0; i < subPcm.size(); ++i)
        {
            const double t = (double) i / kRate;
            subPcm[i] = (float) (i < (std::size_t) (18 * kRate) ? 0.09 * std::sin (6.283185307179586 * 30.0 * t)
                                                                : 0.03 * std::sin (6.283185307179586 * 1000.0 * t));
        }
        LandingSetup quiet = onGate; quiet.target = -23.0; quiet.highPassHz = 80.0;
        const Landed emptied = land (Programme (subPcm), quiet);
        std::printf ("        30 Hz under a high-pass at 80 Hz, target −23: status %d, landed %.3f, file %.3f\n",
                     (int) emptied.answer.status, emptied.landedLufs, emptied.answer.achievedLufs);
        test::ok (emptied.done && emptied.answer.achievedLufs <= quiet.target + 0.1 && emptied.landedLufs >= emptied.answer.achievedLufs
                  && (emptied.answer.status != MasteringSolveStatus::Solved || std::fabs (emptied.answer.achievedLufs - quiet.target) <= 0.1),
                  "the level landed is never quieter than the file: an emptied gate does not land a file above the target");

        test::group ("loudness as a request: the limiter's budget holds a landing short of the target, and names itself");
        const Landed& free = gate;
        LandingSetup budget3 = onGate; budget3.limiterBudgetDb = 3.0;
        LandingSetup budget3Sliced = budget3; budget3Sliced.stepBudget = 173;
        LandingSetup budget30 = onGate; budget30.limiterBudgetDb = 30.0;
        const Landed held = land (song, budget3), heldSliced = land (song, budget3Sliced), loose = land (song, budget30);
        const auto activeP95 = [] (const Landed& l) { return l.answer.limiterActive.stats.p95Db; };
        std::printf ("        target %.0f: no budget %s, active P95 %.2f dB, landed %.2f; budget 3 dB: status %d binding %d, "
                     "landed %.2f, active P95 %.2f dB, %d passes\n", target, free.done ? "done" : "failed", activeP95 (free),
                     free.landedLufs, (int) held.answer.status, (int) held.answer.binding, held.landedLufs, activeP95 (held),
                     held.answer.passes);
        test::ok (free.done && free.answer.status == MasteringSolveStatus::Solved && activeP95 (free) > 3.0,
                  "PRECONDITION: without a budget the target is reached, the limiter's active P95 above 3 dB");
        bool overMarked = false;
        for (int i = 0; i < held.answer.logCount; ++i)
            overMarked = overMarked || (held.answer.log[i].violated & constraintBit (MasteringConstraint::LimiterGainReduction)) != 0;
        test::ok (held.done && held.answer.status == MasteringSolveStatus::TargetUnreachable
                  && held.answer.binding == MasteringConstraint::LimiterGainReduction
                  && held.landedLufs < target - 0.1 && activeP95 (held) <= 3.0
                  && held.answer.measured.truePeakDbTp <= -1.0 && overMarked,
                  "a budget of 3 dB: delivered short of the target with the active P95 inside it, TargetUnreachable with "
                  "LimiterGainReduction named, a pass over it marked in the log");
        test::ok (held.answer.mainReason == LandingReason::None && held.answer.secondReason == LandingReason::None,
                  "held by the budget, nothing in the mix is blamed");
        test::ok (heldSliced.done && heldSliced.answer.status == held.answer.status
                  && heldSliced.answer.achievedLufs == held.answer.achievedLufs && heldSliced.landedLufs == held.landedLufs
                  && std::memcmp (heldSliced.out.data(), held.out.data(), held.out.size() * sizeof (float)) == 0,
                  "the budget's verdict does not depend on how the work is sliced");
        test::ok (loose.done && loose.answer.status == MasteringSolveStatus::Solved && loose.answer.passes == free.answer.passes
                  && std::memcmp (loose.out.data(), free.out.data(), free.out.size() * sizeof (float)) == 0,
                  "a budget the landing never meets leaves it as it was without one, bit for bit");

        // A pass over the budget is no proof by itself: three passes — a low first, a second over the budget, the first
        // restored — end where the search ends, with its own status.
        LandingSetup brief = budget3; brief.maxPasses = 3; brief.initialGainDb = target + 18.0 - 12.0;
        const Landed shortRun = land (song, brief);
        bool shortOver = false;
        for (int i = 0; i < shortRun.answer.logCount; ++i)
            shortOver = shortOver || (shortRun.answer.log[i].violated & constraintBit (MasteringConstraint::LimiterGainReduction)) != 0;
        std::printf ("        three passes, the second over the budget: status %d binding %d, %d passes\n",
                     (int) shortRun.answer.status, (int) shortRun.answer.binding, shortRun.answer.passes);
        test::ok (shortRun.done && shortOver && shortRun.answer.status != MasteringSolveStatus::TargetUnreachable
                  && shortRun.answer.binding != MasteringConstraint::LimiterGainReduction,
                  "a pass over the budget, far above a candidate the search never brought near it, does not name the budget");

        // The budget reads the limiter's ACTIVE windows: 58 s of digital silence before the same 2 s does not water it down.
        const int burst = 2 * kRate, lead = 58 * kRate;
        std::vector<float> burstPcm ((std::size_t) burst), leadPcm ((std::size_t) (lead + burst), 0.0f);
        music (burstPcm, 0, burst, 7u);
        scaleTo (burstPcm, 0, burst, -20.0);
        std::copy (burstPcm.begin(), burstPcm.end(), leadPcm.begin() + lead);
        LandingSetup budget7; budget7.target = -10.0; budget7.limiterBudgetDb = 7.0;
        const Landed bare = land (Programme (burstPcm), budget7), led = land (Programme (leadPcm), budget7);
        std::printf ("        2 s at −10, budget 7: status %d, file %.2f, active P95 %.2f; after 58 s of silence: status %d, "
                     "file %.2f, active P95 %.2f, all-window P95 %.2f\n", (int) bare.answer.status, bare.answer.achievedLufs,
                     activeP95 (bare), (int) led.answer.status, led.answer.achievedLufs, activeP95 (led),
                     led.answer.measured.limiter.p95Db);
        test::ok (bare.done && bare.answer.status == MasteringSolveStatus::TargetUnreachable,
                  "PRECONDITION: 2 s of music at −10 LUFS is held by a budget of 7 dB");
        test::ok (led.done && led.answer.status == MasteringSolveStatus::TargetUnreachable
                  && led.answer.binding == MasteringConstraint::LimiterGainReduction && activeP95 (led) <= 7.0
                  && led.answer.achievedLufs < budget7.target - 0.1,
                  "with 58 s of silence before it, held the same: silence is no window the limiter works in");

        Programme seriesless (loudPcm);
        seriesless.momentary.clear();
        LandingSetup negativeBudget = onGate; negativeBudget.limiterBudgetDb = -1.0;
        const Landed negative = land (song, negativeBudget), blind = land (seriesless, onGate);
        test::ok (! negative.done && negative.answer.status == MasteringSolveStatus::InvalidRequest
                  && ! blind.done && blind.answer.status == MasteringSolveStatus::InvalidRequest,
                  "a negative budget is refused, and a landing on the source's gate without the source's series");
    }

    return test::report();
}
