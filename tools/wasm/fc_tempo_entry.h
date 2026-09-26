// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#ifndef FC_TEMPO_ENTRY_H
#define FC_TEMPO_ENTRY_H

// THE TEMPO ENTRY POINTS — DEFINITIONS, not declarations, and that is the point of the file. Two modules publish
// them and both compile THIS text: fcprobe (tools/wasm/fc_probe.cpp, every analyzer) and fctempo
// (tools/wasm/fc_tempo.cpp, the tempo detector alone, for a page that needs nothing else). The same names, the same
// arguments, the same rows, byte for byte — so a page moves from one module to the other without touching a call, and
// the two cannot drift, because there is nothing to drift: one body, compiled twice.
//
// INCLUDE IT FROM EXACTLY ONE TU PER MODULE. Everything here has internal linkage except the entry points themselves,
// which are extern "C" — a second TU of the same binary including it would define each of them twice.
//
// THE VERSIONS. These entry points are part of two surfaces, and each surface carries its own number:
// FC_PROBE_ABI_VERSION (tools/fc_probe_abi.h) and FC_TEMPO_ABI_VERSION (tools/fc_tempo_abi.h). A change to THIS file
// that a caller can see moves BOTH, in one commit; a change to fc_probe outside it moves only fc_probe's.
//
// build.sh reads every FC_EXPORT line of a module's #include closure, not of its .cpp alone, so a name declared here
// is on both export lists by the fact of being declared.

#include "fc_abi_guards.h"

#include <felitronics/tempo/TempoDetector.h>

#include <cstddef>
#include <cstdint>
#include <limits>

//==================================================================================================
// tempo::TempoDetector through the ABI — the page's BPM tool (dsp/tempo.js, tempoCurve) as a measurement.
//
// THE MIX IS THE DETECTOR'S, NOT THIS FILE'S: the planes cross as they are, and TempoDetector mixes them the way
// the page's toMono does (float32, channel by channel, divided by the count). A page that already holds its mono
// mix passes it as one channel and gets the same bits.
//
// NaN IS THE SPEC'S `null`, in every field that can be null — the bpm of an undetermined programme, a confidence
// the spec itself computes as 0/0 (a lone onset), the beat period and offset, a range bound, an absent alternative,
// a gap in the curve — and a field beside each says whether it is there, so a reader never has to decide what a
// NaN means. (An undetermined programme's confidence is 0, not NaN: the spec says 0 there.) AN EMPTY PROGRAMME IS A
// MEASUREMENT: the spec answers it (undetermined), so the run does too.
namespace
{
    felitronics::tempo::TempoDetector& tempoDetector()
    {
        static felitronics::tempo::TempoDetector d;
        return d;
    }
    bool haveTempo = false;

    // One definition, two roads — the run and the price read the same constant.
    constexpr felitronics::tempo::TempoParams kTempoParams {};

    constexpr std::uint32_t kTempoScalars     = 35;
    constexpr std::uint32_t kTempoCandStride  = 2;    // bpm (0.1), score
    constexpr std::uint32_t kTempoPointStride = 5;    // t (0.1 s), hasBpm, bpm (0.1, NaN in a gap), conf (0.01), raw bpm

    double tempoAlt (const felitronics::tempo::TempoHeadline& h, int i)
    {
        return i < h.altCount ? h.alts[i] : std::numeric_limits<double>::quiet_NaN();
    }
}

static int tempoRunWith (const float* planar, std::uint32_t frames, std::uint32_t channels, double sampleRate,
                         const felitronics::tempo::TempoParams& p)
{
    haveTempo = false;
    if (! planarSpanOrEmpty (planar, frames, channels)) return 0;
    auto& d = tempoDetector();
    d.setParams (p);
    // The detector validates the rate, the width and every parameter itself (storageFor refuses exactly what
    // prepare refuses); a second opinion here would be a second definition.
    if (! d.prepare (sampleRate, (int) channels, (std::uint64_t) frames)) return 0;
    const float* view[felitronics::core::kMaxChannels] {};
    if (frames != 0)
        for (std::uint32_t k = 0; k < channels; ++k) view[k] = planar + (std::size_t) k * (std::size_t) frames;
    if (frames != 0 && ! d.process (view, (int) channels, (int) frames)) return 0;
    if (! d.finish()) return 0;
    haveTempo = true;
    return 1;
}

FC_EXPORT int fc_probe_tempo_run (const float* planar, std::uint32_t frames, std::uint32_t channels, double sampleRate)
{
    return tempoRunWith (planar, frames, channels, sampleRate, kTempoParams);
}

// The spec's `opts`: the BPM search range and the curve's window and hop, in seconds.
FC_EXPORT int fc_probe_tempo_run_with (const float* planar, std::uint32_t frames, std::uint32_t channels,
                                       double sampleRate, double minBpm, double maxBpm, double winSec, double hopSec)
{
    return tempoRunWith (planar, frames, channels, sampleRate,
                         felitronics::tempo::TempoParams { minBpm, maxBpm, winSec, hopSec });
}

FC_EXPORT std::uint32_t fc_probe_tempo_scalars_len  (void) { return kTempoScalars; }
FC_EXPORT std::uint32_t fc_probe_tempo_cand_stride  (void) { return kTempoCandStride; }
FC_EXPORT std::uint32_t fc_probe_tempo_point_stride (void) { return kTempoPointStride; }

// The scalars. The ORDER IS THE CONTRACT:
//   0 sampleRate  1 channels  2 samples  3 odfSr  4 onset frames
//   5 minBpm  6 maxBpm  7 winSec  8 hopSec  9 window frames  10 hop frames            (what the RUN installed;
//                                                                                       9/10 as the spec forms them)
//   11 determined  12 bpm  13 confidence  14 label (0 undetermined, 1 low, 2 medium, 3 high)
//   15 alt count  16 alt 0  17 alt 1  18 beatPeriodSec  19 beatOffsetSec              (tempoCurve's headline)
//   20 varies  21 hasRange  22 range low  23 range high  24 candidates  25 points
//   26 bpm  27 alt count  28 alt 0  29 alt 1  30 beatPeriodSec                          (detectTempo's — the rest
//                                                                                       of its fields are 13, 14, 19)
//   31 anchor bpm  32 anchor confidence  33 anchor lag  34 non-finite samples          (unrounded, for inspection)
FC_EXPORT std::uint32_t fc_probe_tempo_scalars (double* out, std::uint32_t cap)
{
    if (! haveTempo || out == nullptr || cap < kTempoScalars || ! outSpan (out, cap, 8)) return 0u;
    const auto& d = tempoDetector();
    const auto& p = d.params();
    const auto& h = d.headline();
    const auto& w = d.wholeTrack();
    std::uint32_t i = 0;
    out[i++] = d.sampleRate();                 out[i++] = (double) d.channels();
    out[i++] = (double) d.framesSeen();        out[i++] = d.odfSampleRate();
    out[i++] = (double) d.onsetFrames();
    out[i++] = p.minBpm;                       out[i++] = p.maxBpm;
    out[i++] = p.winSec;                       out[i++] = p.hopSec;
    out[i++] = d.windowFrames();               out[i++] = d.hopFrames();
    out[i++] = h.determined ? 1.0 : 0.0;       out[i++] = h.bpm;
    out[i++] = h.confidence;                   out[i++] = (double) (int) h.label;
    out[i++] = (double) h.altCount;            out[i++] = tempoAlt (h, 0);
    out[i++] = tempoAlt (h, 1);                out[i++] = h.beatPeriodSec;
    out[i++] = h.beatOffsetSec;
    out[i++] = d.varies() ? 1.0 : 0.0;         out[i++] = d.hasRange() ? 1.0 : 0.0;
    out[i++] = d.rangeLow();                   out[i++] = d.rangeHigh();
    out[i++] = (double) d.candidateCount();    out[i++] = (double) d.pointCount();
    out[i++] = w.bpm;                          out[i++] = (double) w.altCount;
    out[i++] = tempoAlt (w, 0);                out[i++] = tempoAlt (w, 1);
    out[i++] = w.beatPeriodSec;
    out[i++] = d.anchorBpm();                  out[i++] = d.anchorConfidence();
    out[i++] = d.anchorLag();                  out[i++] = (double) d.nonFiniteSamples();
    return i;
}

// One row per candidate (at most five): bpm rounded to 0.1, the raw score. Rows that fit the capacity; the
// capacity is in ELEMENTS, as every copier in this ABI takes it.
FC_EXPORT std::uint32_t fc_probe_tempo_candidates (double* out, std::uint32_t cap)
{
    if (! haveTempo || out == nullptr || ! outSpan (out, cap, 8)) return 0u;
    const auto& d = tempoDetector();
    const std::uint32_t room = cap / kTempoCandStride;
    std::uint32_t at = 0;
    for (int k = 0; k < d.candidateCount() && at < room; ++k, ++at)
    {
        const auto c = d.candidate (k);
        out[(std::size_t) at * kTempoCandStride + 0] = c.bpm;
        out[(std::size_t) at * kTempoCandStride + 1] = c.score;
    }
    return at;
}

// One row per point of the curve: t, hasBpm, bpm (NaN in a gap), conf, and the window's own bpm BEFORE the
// median (NaN where that window was a gap) — the one column that is not the spec's, so a reader can tell a
// smoothed-away spike from a gap.
FC_EXPORT std::uint32_t fc_probe_tempo_curve (double* out, std::uint32_t cap)
{
    if (! haveTempo || out == nullptr || ! outSpan (out, cap, 8)) return 0u;
    const auto& d = tempoDetector();
    const std::uint32_t room = cap / kTempoPointStride;
    std::uint32_t at = 0;
    for (std::int64_t k = 0; k < d.pointCount() && at < room; ++k, ++at)
    {
        const auto q = d.point (k);
        const auto r = d.rawPoint (k);
        double* row = out + (std::size_t) at * kTempoPointStride;
        row[0] = q.t;
        row[1] = q.hasBpm ? 1.0 : 0.0;
        row[2] = q.hasBpm ? q.bpm : std::numeric_limits<double>::quiet_NaN();
        row[3] = q.conf;
        row[4] = r.hasBpm ? r.bpm : std::numeric_limits<double>::quiet_NaN();
    }
    return at;
}

// THE PRICE TAKES THE PROGRAMME'S LENGTH, because every onset buffer is one double per 512 samples of it — the
// crest price's shape, for the same reason.
//
// AND BECAUSE IT KNOWS THE LENGTH, IT REFUSES THE SPANS THE RUN REFUSES: a programme whose planes cannot fit a 32-bit
// address space is priced at zero here, where the length-free prices of the other modes cannot see it. Only what
// the price cannot know — the pointer — is left for the run to refuse on its own.
FC_EXPORT double fc_probe_tempo_storage_bytes (std::uint32_t channels, double sampleRate, std::uint32_t frames)
{
    if (! geometry (channels) || ! spanFits (frames, channels)) return 0.0;
    return demand (felitronics::tempo::TempoDetector::storageFor (sampleRate, (int) channels, (std::uint64_t) frames,
                                                                  kTempoParams));
}

// ...and law 11d: a caller about to run `_run_with` must be able to ask the price of THOSE parameters.
FC_EXPORT double fc_probe_tempo_storage_bytes_with (std::uint32_t channels, double sampleRate, std::uint32_t frames,
                                                    double minBpm, double maxBpm, double winSec, double hopSec)
{
    if (! geometry (channels) || ! spanFits (frames, channels)) return 0.0;
    return demand (felitronics::tempo::TempoDetector::storageFor (
                       sampleRate, (int) channels, (std::uint64_t) frames,
                       felitronics::tempo::TempoParams { minBpm, maxBpm, winSec, hopSec }));
}

#endif   // FC_TEMPO_ENTRY_H
