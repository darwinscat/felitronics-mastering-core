// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <felitronics/mastering/DeliveredMastering.h>

// The reference takes the old setParams/prepare path at the handle's settings. Keep
// it independent of configure(), so changing that operation changes only one side.
inline void masterConfigureSequences()
{
    using namespace felitronics;
    using namespace mastering;
    test::group ("configure: subsequent calls match the old re-preparation bit for bit");
    for (bool peak : { false, true })
        for (bool delivery : { false, true })
            for (int sequence = 0; sequence < 3; ++sequence)
            {
                fc_master_config c {}; FC_INIT (c);
                (void) fc_master_config_defaults (&c);
                c.sampleRate = 48000.0; c.channels = 2;
                c.deliveryRate = delivery ? 96000.0 : 0.0;
                fc_master h = 0;
                MasteringChain chain;
                MasteringChainConfig cfg;
                const double rate = delivery ? 96000.0 : 48000.0;
                bool ready = fc_master_create (&c, &h) == FC_OK && chain.prepare (rate, 2, cfg);
                fc_master_params p {}; FC_INIT (p);
                (void) fc_master_params_defaults (&p);
                MasteringChainParams cp;
                p.eqBands[0].on = 1; cp.eqBands[0].on = true;
                p.eqBands[0].lanes[0].freq = 120.0;
                cp.eqBands[0].lane (eq::Lane::Stereo).freq = 120.0;
                p.peakClipper = peak ? 1 : 0; cp.limiter.peakClip = peak;
                p.inputGainDb = 12.0; cp.inputGainDb = 12.0;
                const auto gain = [&] (double db)
                {
                    p.eqBands[0].lanes[0].gainDb = db;
                    cp.eqBands[0].lane (eq::Lane::Stereo).gainDb = db;
                };
                fc_master_resolved resolved {}; FC_INIT (resolved);
                const auto configure = [&]
                {
                    ready = fc_master_configure (h, &p, &resolved) == FC_OK && ready;
                    chain.setParams (cp);
                    ready = chain.prepare (rate, 2, cfg) && ready;
                };
                gain (3.0); configure();
                if (delivery)
                {
                    OfflineRenderer renderer;
                    DeliveredMastering delivered;
                    ready = renderer.prepare (2, 4096) && delivered.prepare (48000.0, rate, 2, 4096) && ready;
                    constexpr int n = 8192, dn = 2 * n;
                    std::vector<float> input (2 * n), actual (2 * dn), expected (2 * dn);
                    for (int i = 0; i < 2 * n; ++i) input[(std::size_t) i] = (float) (i % 137 - 68) / 100.0f;
                    const auto render = [&]
                    {
                        const float* ip[] { input.data(), input.data() + n };
                        float* op[] { expected.data(), expected.data() + dn };
                        ready = fc_master_render_delivered (h, input.data(), n, actual.data(), dn) == FC_OK
                             && delivered.render (chain, renderer, ip, 2, n, op, dn) && ready;
                        ready = std::memcmp (actual.data(), expected.data(), actual.size() * sizeof (float)) == 0 && ready;
                    };
                    if (sequence == 1) render();
                    gain (9.0); configure();
                    if (sequence == 2) { gain (-6.0); configure(); gain (12.0); configure(); }
                    // A delivering handle has no streaming edits. Its refusal must leave the configured set intact.
                    gain (-12.0);
                    ready = fc_master_set_params (h, &p) == FC_ERR_STATE && ready;
                    render();
                }
                else
                {
                    constexpr int block = 64;
                    float actual[2 * block], expected[2 * block];
                    for (int offset = 0; offset < 8192; offset += block)
                    {
                        if ((sequence == 0 && offset == 0) || (sequence == 1 && offset == cfg.internalBlock)
                            || (sequence == 2 && (offset == 0 || offset == 64 || offset == 512 || offset == 1024)))
                        {
                            gain (offset == 512 ? -6.0 : 9.0);
                            ready = fc_master_set_params (h, &p) == FC_OK && ready;
                            chain.setParams (cp);
                            if (sequence == 2 && offset == 0)
                            {
                                gain (12.0);
                                ready = fc_master_set_params (h, &p) == FC_OK && ready;
                                chain.setParams (cp);
                            }
                        }
                        for (int i = 0; i < 2 * block; ++i)
                            actual[i] = expected[i] = (float) ((i + offset) % 137 - 68) / 100.0f;
                        float* planes[] { expected, expected + block };
                        ready = fc_master_process (h, actual, actual, block) == FC_OK && chain.process (planes, 2, block) && ready;
                        ready = std::memcmp (actual, expected, sizeof actual) == 0 && ready;
                    }
                }
                std::printf ("  configure ABI sequence %d, peak clipper %d, delivery %d\n", sequence, peak, delivery);
                test::ok (ready, "all samples match the old re-prepare path bit for bit");
                (void) fc_master_destroy (h);
            }
}

inline void masterBudgetFields()
{
    using namespace felitronics::mastering;
    felitronics::test::group ("core budgets forwarded field by field, including refused solves");
    for (double delivery : { 0.0, 48000.0, 96000.0 })
    {
        fc_master_config c {}; FC_INIT (c); (void) fc_master_config_defaults (&c);
        c.sampleRate = 48000.0; c.channels = 2; c.deliveryRate = delivery;
        MasteringChainConfig cfg;
        fc_need need {}; FC_INIT (need);
        felitronics::test::ok (fc_master_need_create (&c, &need) == FC_OK
            && need.callBytes == createInstanceBytes (48000.0, delivery, 2, cfg, 4096), "create forwards the complete core budget");
        fc_master h = 0; (void) fc_master_create (&c, &h);
        for (int frames : { 0, 48000 })
            for (int buckets : { 0, 1000, 65536 })
            {
                fc_loudness_request req {}; FC_INIT (req); (void) fc_loudness_request_defaults (&req);
                req.grTraceBuckets = buckets;
                const auto call = delivery != 0.0
                    ? DeliveredMastering::solveCallBytes (48000.0, delivery, 2, frames, buckets)
                    : TargetLoudnessSolver::solveCallBytes (48000.0, 2, frames, buckets);
                felitronics::test::ok (fc_master_need_solve (h, &req, (std::uint32_t) frames, &need) == FC_OK
                    && need.callBytes == call && need.facadeBytes == sizeof (LoudnessSolution),
                    "solve forwards the core cost; facadeBytes is only sizeof, even on refusal");
            }
        (void) fc_master_destroy (h);
    }
}
