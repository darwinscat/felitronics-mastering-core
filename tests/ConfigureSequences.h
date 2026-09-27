// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A configure is a stream restart. Compare every later sample with the original
// setParams/prepare path, including writes that precede the first quantum.
inline void configureSequences()
{
    using namespace felitronics;
    for (bool clip : { false, true })
        for (bool warm : { false, true })
            for (int sequence = 0; sequence < 4; ++sequence)
            {
                mastering::MasteringChain a, b;
                mastering::MasteringChainConfig cfg;
                cfg.clipper = clip;
                bool ready = a.prepare (48000.0, 2, cfg) && b.prepare (48000.0, 2, cfg);
                mastering::MasteringChainParams p;
                p.eqBands[0].on = true;
                auto& lane = p.eqBands[0].lane (eq::Lane::Stereo);
                lane.freq = 120.0;
                lane.gainDb = 3.0;
                p.clipper.driveDb = 6.0f;
                p.limiter.peakClip = clip;
                constexpr int n = 8192;
                std::vector<float> x (2 * n), y (2 * n);
                const auto fill = [&]
                {
                    for (int i = 0; i < 2 * n; ++i) x[(std::size_t) i] = (float) (i % 137 - 68) / 100.0f;
                    y = x;
                };
                fill();
                if (warm)
                {
                    float* ax[] { x.data(), x.data() + n };
                    float* bx[] { y.data(), y.data() + n };
                    ready = a.process (ax, 2, n) && b.process (bx, 2, n) && ready;
                }
                (void) a.configure (p);
                b.setParams (p);
                ready = b.prepare (48000.0, 2, cfg) && ready;
                fill();
                for (int offset = 0; offset < n; offset += 64)
                {
                    if ((sequence == 0 && offset == 0) || (sequence == 1 && offset == cfg.internalBlock)
                        || (sequence == 2 && (offset == 0 || offset == 64 || offset == 512 || offset == 1024)))
                    {
                        lane.gainDb = offset == 512 ? -6.0 : 9.0;
                        a.setParams (p); b.setParams (p);
                        if (sequence == 2 && offset == 0)
                        {
                            lane.gainDb = 12.0;
                            a.setParams (p); b.setParams (p);
                        }
                    }
                    if (sequence == 3 && offset == 512) { a.reset(); b.reset(); }
                    float* ax[] { x.data() + offset, x.data() + n + offset };
                    float* bx[] { y.data() + offset, y.data() + n + offset };
                    ready = a.process (ax, 2, 64) && b.process (bx, 2, 64) && ready;
                }
                std::printf ("  configure sequence %d, clipper %d, prior audio %d\n", sequence, clip, warm);
                test::ok (ready && std::memcmp (x.data(), y.data(), x.size() * sizeof (float)) == 0,
                          "configure and old re-prepare: every subsequent sample is bit-identical");
            }
}
