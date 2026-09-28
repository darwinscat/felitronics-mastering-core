// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/LowEnd.h>
#include <felitronics_test.h>
#include <bit>
#include <cstdio>
#include <vector>
using namespace felitronics::analysis;
int main()
{
    std::uint64_t digest = 0xcbf29ce484222325ull;
    const auto take = [&] (double v) { const auto bits = std::bit_cast<std::uint64_t> (v); for (unsigned i = 0; i < 8; ++i) digest = (digest ^ std::uint8_t (bits >> (8 * i))) * 0x100000001b3ull; };
    std::vector<float> pcm (150001);
    for (unsigned i = 0; i < pcm.size(); ++i) pcm[i] = float (.1 * felitronics::core::det::sin (double (i) * .019) + .03 * felitronics::core::det::sin (double (i) * .007));
    for (const int rate : { 8000, 48000, 192000 })
    {
        BandCrest crest; LowEnd low; LowEndParams params; params.maxBlocks = 17; low.setParams (params);
        for (unsigned run = 0; run < 5; ++run)
        {
            if (run == 1 || run == 3) { crest.reset(); low.reset(); }
            else { (void) crest.prepare (rate, 2, static_cast<long long> (pcm.size())); (void) low.prepare (rate, 1024, 2); }
            const unsigned length = run == 1 ? 123 : run == 2 ? 7777 : unsigned (pcm.size());
            for (unsigned at = 0; at < length;)
            {
                const auto n = std::min (length - at, run == 4 ? 1u : 317u); const float* p[] { pcm.data() + at, pcm.data() + at };
                (void) crest.process (p, 2, int (n)); (void) low.process (p, 2, int (n)); at += n;
            }
            (void) crest.finish(); (void) low.finish();
            take (crest.programmeMeanSquareDb()); take (double (crest.blockCount()));
            for (long long b = 0; b < crest.blockCount(); ++b) for (int band = 0; band < 5; ++band)
            { take (crest.blockMeanSq (b, band)); take (crest.blockPeakLin (b, band)); take (double (crest.blockActive (b, band))); }
            take (low.lowMidEnergy()); take (low.lowSideEnergy()); take (double (low.blocksComplete())); take (double (low.blockCount()));
            for (long long b = 0; b < low.storedBlockCount(); ++b) { const auto v = low.block (b); take (v.midEnergy); take (v.sideEnergy); take (double (v.samples)); }
        }
    }
    std::printf ("source-storage-digest=%016llx\n", static_cast<unsigned long long> (digest));
    // Recorded from the preceding vector/cleared-record implementation on the same call sequences.
    felitronics::test::ok (digest == 0xec2c5c61db26669aull, "source storage fingerprint");
    return felitronics::test::report();
}
