// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-core — see LICENSE.

#pragma once

#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/mastering/Progress.h>

#include <algorithm>
#include <vector>

namespace felitronics::mastering::testing
{
// Whole-call render loop preserved from OfflineRenderer.h at 690adf1, before
// begin/step/finish existed. Keep the loop here so the stepped implementation
// cannot become its own numerical oracle.
template <class AfterBlock>
inline bool previousRender (MasteringChain& chain, const float* const* in, float* const* out,
                            int numChannels, int frames, int block, AfterBlock&& afterBlock)
{
    if (! chain.isPrepared() || numChannels != chain.numChannels() || block < 1 || frames < 0
        || (frames > 0 && (in == nullptr || out == nullptr))) return false;
    std::vector<float> scratch ((std::size_t) numChannels * (std::size_t) block, 0.0f);
    chain.reset();
    const long long D = chain.latencySamples();
    const long long total = (long long) frames + D;
    float* sp[core::kMaxChannels] {};
    for (int c = 0; c < numChannels; ++c)
        sp[c] = scratch.data() + (std::size_t) c * (std::size_t) block;
    MasteringChainTaps taps;
    long long tapPos = 0;
    for (long long off = 0; off < total; )
    {
        const int m = (int) std::min<long long> (block, total - off);
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < m; ++i)
            {
                const long long s = off + i;
                sp[c][i] = (s < (long long) frames) ? in[c][s] : 0.0f;
            }
        if (! chain.process (sp, numChannels, m, taps)) return false;
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < m; ++i)
            {
                const long long o = off + i - D;
                if (o >= 0 && o < (long long) frames) out[c][o] = sp[c][i];
            }
        tapPos += taps.framesWritten;
        off += m;
        afterBlock (off);
    }
    (void) tapPos;
    return true;
}
inline bool previousRender (MasteringChain& chain, const float* const* in, float* const* out,
                            int numChannels, int frames, int block)
{
    return previousRender (chain, in, out, numChannels, frames, block, [] (long long) {});
}
} // namespace felitronics::mastering::testing
