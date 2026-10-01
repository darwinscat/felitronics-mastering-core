// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/mastering/DeliveredMastering.h>
#include <felitronics_test.h>
#include "../../../tests/DeclaredBudget.h"

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

    bool prepare()
    {
        MasteringChainConfig config;
        config.eq = false; config.monoBass = false; config.compressor = false;
        config.clipper = false; config.limiter = true; config.dither = true;
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
}

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
                    : limited.status == MasteringSolveStatus::Unavailable,
              "a one-render budget cannot hide a final render or deliver an unsafe candidate");

    Rig miss;
    if (! test::run (miss.prepare())) return test::report();
    LoudnessSolution high;
    const bool highEnded = run (miss, 409, 10.0, 12, high);
    test::ok (highEnded && high.deliverable && high.passes == 12 && high.logCount == 12
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

    return test::report();
}
