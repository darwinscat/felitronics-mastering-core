// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/mastering/LandingSearch.h>
#include <felitronics_test.h>
#include <alloc_counter.h>

#include <cmath>
#include <climits>
#include <cstring>
#include <limits>
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
    std::vector<float> source, output, safe;
    const float* input[1] {};
    float* out[1] {};
    float* best[1] {};

    Rig() : source ((std::size_t) kFrames), output ((std::size_t) kFrames), safe ((std::size_t) kFrames)
    { input[0] = source.data(); out[0] = output.data(); best[0] = safe.data(); }

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
    req.targetLufs = target; req.maxTruePeakDbTp = -1.0; req.maxPasses = passes;
    if (! search.begin (rig.chain, rig.renderer, rig.params, rig.input, kFrames, kRate,
                        rig.out, rig.best, 1, kFrames, req)) return false;
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
    std::vector<float> source (sourceRate), output (kFrames), safe (kFrames);
    for (int i = 0; i < sourceRate; ++i)
    {
        const double t = (double) i / sourceRate;
        source[(std::size_t) i] = (float) (0.16 * std::sin (6.283185307179586 * 101.0 * t)
            + 0.03 * std::sin (6.283185307179586 * 3301.0 * t));
        if (i % 4096 == 0) source[(std::size_t) i] += 0.7f;
    }
    const float* input[1] { source.data() };
    float* out[1] { output.data() };
    float* best[1] { safe.data() };
    LoudnessRequest req;
    req.targetLufs = -14.0; req.maxTruePeakDbTp = -1.0; req.maxPasses = 12;
    LandingSearch search (solver);
    if (! search.begin (chain, renderer, params, input, sourceRate, sourceRate,
                        out, best, 1, kFrames, req, {}, &converter)) return false;
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
              "the landing call, safe PCM and retained meters fit the declared fresh-call memory budget");

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
    Rig finiteExtreme;
    if (! test::run (finiteExtreme.prepare())) return test::report();
    LoudnessSolution extremeResult;
    const bool extremeEnded = run (finiteExtreme, LLONG_MAX,
        std::numeric_limits<double>::max(), 3, extremeResult);
    test::ok (extremeEnded && extremeResult.status == MasteringSolveStatus::PassLimit
              && extremeResult.deliverable && extremeResult.passes == 3
              && extremeResult.achievedLufs < std::numeric_limits<double>::max()
              && extremeResult.distanceLu > 0.0,
              "a finite extreme target spends its budget and never reports an invented hit");

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
    return test::report();
}
