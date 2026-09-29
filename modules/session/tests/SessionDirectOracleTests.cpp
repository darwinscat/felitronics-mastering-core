// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// The direct LandingSearch path predates Session's ready-master bridge (dacd2b2).
// Its implementation differs from that revision only by two read-only accessors.
#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/mastering/DeliveryConverter.h>
#include <felitronics/session/Session.h>
#include <felitronics/session/Snapshot.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

using namespace felitronics;
using felitronics::test::ok;

namespace
{
bool bits (double a, double b) noexcept
{ return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
bool optionalBits (const std::optional<double>& a, double b) noexcept
{ return a && bits (*a, b); }
bool traceMatches (const std::optional<session::LandingTrace>& reported,
                   const mastering::GainReductionTrace& direct) noexcept
{
    if (! reported || reported->rows.size() != direct.bucket.size()
        || reported->columns != std::uint32_t (direct.buckets)
        || reported->toFrame != direct.programmeFrames || reported->complete != direct.complete
        || reported->valid != direct.valid || reported->samples != direct.samples
        || reported->nonFinite != direct.nonFinite) return false;
    for (std::size_t i = 0; i < direct.bucket.size(); ++i)
    {
        const auto& a = reported->rows[i]; const auto& b = direct.bucket[i];
        const auto finite = b.samples - b.nonFinite;
        const double mean = direct.complete || finite == 0 ? b.meanDb : b.meanDb / double (finite);
        if (! bits (a.minDb, b.minDb) || ! bits (a.maxDb, b.maxDb) || ! bits (a.meanDb, mean)
            || a.samples != b.samples || a.nonFinite != b.nonFinite) return false;
    }
    return true;
}
bool measurementsMatch (const session::LandingSummary& a, const mastering::LoudnessSolution& b,
                        std::uint32_t rate) noexcept
{
    if (! a.deliverable || ! b.deliverable || a.status != session::LandingStatus::Solved
        || b.status != mastering::MasteringSolveStatus::Solved
        || int (a.mainReason) != int (b.mainReason) || int (a.secondReason) != int (b.secondReason)
        || a.passes != std::uint32_t (b.passes)
        || a.log.size() != std::size_t (b.logCount)
        || ! optionalBits (a.achievedLufs, b.achievedLufs)
        || ! optionalBits (a.missLu, b.missLu)
        || ! optionalBits (a.distanceLu, b.distanceLu)
        || ! optionalBits (a.truePeakDbTp, b.measured.truePeakDbTp)
        || ! optionalBits (a.sourceSubBassShare, b.sourceSubBassShare)
        || ! optionalBits (a.sourcePresenceShare, b.sourcePresenceShare)
        || ! optionalBits (a.limiterMeanReductionDb, b.limiterMeanReductionDb)
        || ! traceMatches (a.limiterTrace, b.limiterTrace)
        || ! traceMatches (a.peakClipTrace, b.peakClipTrace)
        || a.limiterTrace->sampleRateHz != rate || a.peakClipTrace->sampleRateHz != rate) return false;
    for (int i = 0; i < b.logCount; ++i)
    {
        const auto& row = a.log[std::size_t (i)]; const auto& old = b.log[i];
        if (! bits (row.gainDb, old.gainDb) || ! bits (row.ceilingDbTp, old.ceilingDb)
            || ! bits (row.achievedLufs, old.integratedLufs)
            || ! bits (row.truePeakDbTp, old.truePeakDbTp)
            || ! bits (row.limiterMaxReductionDb, old.limiterMaxGrDb)
            || row.ceilingSafe != ((old.violated & mastering::constraintBit (
                mastering::MasteringConstraint::TruePeakCeiling)) == 0)) return false;
    }
    return true;
}
bool caseRun (std::uint32_t sourceRate, std::uint32_t deliveryRate,
              std::uint32_t sessionCut, long long directCut, bool perturb) 
{
    const std::uint32_t inputFrames = 2u * sourceRate;
    const int outputFrames = int (mastering::DeliveryConverter::deliveredFrames (
        double (sourceRate), double (deliveryRate), inputFrames));
    std::vector<float> source (std::size_t (inputFrames) * 2u), oldOutput (std::size_t (outputFrames) * 2u);
    for (std::uint32_t i = 0; i < inputFrames; ++i)
    {
        source[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f + (i % 257u == 0 ? 0.12f : 0.0f);
        source[inputFrames + i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f
                                - (i + 1u == inputFrames ? 0.19f : 0.0f);
    }
    const float* input[2] { source.data(), source.data() + inputFrames };
    float* output[2] { oldOutput.data(), oldOutput.data() + outputFrames };
    auto made = session::Session::create();
    if (made.status != session::Status::Ok) return false;
    auto& s = *made.session;
    if (s.apply (session::command::Load { 1, { input, 2, inputFrames, sourceRate },
        { "oracle.wav", sourceRate, true, 24 } }).rejection != session::Rejection::None) return false;
    for (unsigned i = 0; i < 20000 && (s.state() == session::State::Loaded
        || ! s.snapshot().view().mandatoryMeasurementsReady); ++i) (void) s.step (16);
    const double sourceLufs = s.snapshot().view().integratedLufs;
    if (! std::isfinite (sourceLufs)) return false;
    session::command::Master request { 2 };
    request.ready.version = 1;
    request.ready.deliveryRateHz = deliveryRate;
    auto& cfg = request.ready.topology;
    cfg.clipper = true;
    auto& params = request.ready.params;
    params.eqBands[0].on = true;
    params.eqBands[0].lanes[0].freq = 200.0;
    params.eqBands[0].lanes[0].gainDb = 1.5;
    params.eqBands[0].dyn.on = true;
    params.eqBands[0].dyn.rangeDb = -2.0;
    params.compressor.thresholdDb = -30.0;
    params.compressor.ratio = 2.5;
    params.clipper.mix = 0.5f;
    request.source = s.source().hash; request.revision = s.revision();

    // The direct call has no Session planner. These constants are the dacd2b2
    // allStreaming target and engine landing inputs, spelled independently here.
    mastering::LoudnessRequest directRequest;
    directRequest.targetLufs = -14.0;
    directRequest.maxTruePeakDbTp = -1.0;
    directRequest.toleranceLu = 0.1;
    directRequest.truePeakAimDb = 0.05;
    directRequest.maxPasses = 12;
    directRequest.normalizationGainDb = -18.0 - sourceLufs;
    directRequest.initialGainDb = 4.0;
    directRequest.productLanding = true;
    directRequest.pcmBits = 24;
    directRequest.grTraceBuckets = std::min (outputFrames, mastering::GainReductionTrace::kDefaultBuckets);
    params.limiter.ceilingDbTp = -1.15;
    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    mastering::DeliveryConverter converter;
    if (! chain.prepare (double (deliveryRate), 2, cfg) || ! renderer.prepare (2, 1024)
        || ! solver.prepare (double (deliveryRate), 2, 1024, cfg.internalBlock, cfg.oversampleFactor)
        || (sourceRate != deliveryRate && ! converter.prepare (double (sourceRate), double (deliveryRate), 2, 1024))) return false;
    chain.setParams (params);
    mastering::LandingSearch old (solver);
    if (! old.begin (chain, renderer, params, input, inputFrames, double (sourceRate), output,
        2, outputFrames, directRequest, {}, sourceRate == deliveryRate ? nullptr : &converter))
    { std::printf ("oracle begin failed %u/%u\n", sourceRate, deliveryRate); return false; }
    mastering::StepResult oldState = mastering::StepResult::More;
    for (unsigned i = 0; oldState == mastering::StepResult::More && i < 200000; ++i)
        oldState = old.step (directCut);
    if (oldState != mastering::StepResult::Done)
    { std::printf ("oracle state %u/%u %d\n", sourceRate, deliveryRate, int (oldState)); return false; }

    const auto started = s.apply (request);
    if (started.rejection != session::Rejection::None)
    { std::printf ("session refused %u/%u %d\n", sourceRate, deliveryRate, int (started.rejection)); return false; }
    for (unsigned i = 0; s.job() != 0 && i < 200000; ++i) (void) s.step (sessionCut);
    if (s.job() != 0 || s.masters().size() != 1 || ! s.masters()[0].landing)
    { std::printf ("session incomplete %u/%u job=%u masters=%zu\n", sourceRate, deliveryRate, s.job(), s.masters().size()); return false; }
    const auto token = s.pendingMaster();
    if (token.master == 0)
    { std::printf ("session no PCM %u/%u\n", sourceRate, deliveryRate); return false; }
    std::vector<float> newer (oldOutput.size());
    if (s.copyMaster (token, newer) != session::MasterTransferStatus::Ok) return false;
    if (perturb) newer[outputFrames / 2] = std::nextafter (newer[outputFrames / 2],
        std::numeric_limits<float>::infinity());
    const bool pcmMatches = std::memcmp (newer.data(), oldOutput.data(), newer.size() * sizeof (float)) == 0;
    const bool measures = measurementsMatch (*s.masters()[0].landing, old.result(), deliveryRate);
    if ((! pcmMatches || ! measures) && ! perturb)
    {
        const auto& a = *s.masters()[0].landing; const auto& b = old.result();
        std::printf ("oracle mismatch %u/%u pcm=%d measurements=%d oldstatus=%d sessionstatus=%d passes=%u/%d\n",
            sourceRate, deliveryRate, int (pcmMatches), int (measures), int (old.result().status),
            int (s.masters()[0].landing->status), s.masters()[0].landing->passes, old.result().passes);
        std::printf ("work=%llu/%llu lufs=%d tp=%d bass=%d presence=%d limiter=%d traces=%d/%d\n",
            static_cast<unsigned long long> (a.workUnits), static_cast<unsigned long long> (b.workUnits),
            int (optionalBits (a.achievedLufs, b.achievedLufs)),
            int (optionalBits (a.truePeakDbTp, b.measured.truePeakDbTp)),
            int (optionalBits (a.sourceSubBassShare, b.sourceSubBassShare)),
            int (optionalBits (a.sourcePresenceShare, b.sourcePresenceShare)),
            int (optionalBits (a.limiterMeanReductionDb, b.limiterMeanReductionDb)),
            int (traceMatches (a.limiterTrace, b.limiterTrace)), int (traceMatches (a.peakClipTrace, b.peakClipTrace)));
    }
    return pcmMatches && measures;
}
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--fault") == 0)
    {
        ok (caseRun (48000, 44100, 1, 73, true), "one-bit PCM fault must fail the direct oracle");
        return felitronics::test::report();
    }
    ok (caseRun (48000, 48000, 1, std::numeric_limits<long long>::max(), false),
        "direct dacd2b2 whole-call search equals Session at source rate");
    ok (caseRun (44100, 48000, 17, 73, false), "direct dacd2b2 search equals Session for SRC up");
    ok (caseRun (48000, 44100, 73, 997, false), "direct dacd2b2 search equals Session for SRC down");
    return felitronics::test::report();
}
