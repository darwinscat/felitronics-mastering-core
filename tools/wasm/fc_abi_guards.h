// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#ifndef FC_ABI_GUARDS_H
#define FC_ABI_GUARDS_H

// What every translation unit of the ANALYSIS ABIs shares: the export macro and the checks an argument from
// JavaScript passes before it reaches an analyzer. Two TUs include it — tools/wasm/fc_probe.cpp (every analyzer) and
// tools/wasm/fc_tempo.cpp (the tempo detector alone) — and the tempo entry points both of them publish
// (fc_tempo_entry.h) stand on it. One text, so the two modules cannot come to refuse different spans.
//
// INTERNAL LINKAGE, ON PURPOSE: an unnamed namespace, so each module gets its own copy exactly as it had when these
// lived in fc_probe.cpp. The two TUs are never linked into one binary — they define the same extern "C" tempo entry
// points — so there is no second copy to disagree with. (fc_master.cpp keeps its own guards: it is a different ABI
// with its own refusal rules.)

#include <felitronics/core/Config.h>   // kMaxChannels

#include <cstdint>

#if defined(__EMSCRIPTEN__)
  #include <emscripten/emscripten.h>
  #include <emscripten/heap.h>              // emscripten_get_heap_size — NOT declared by emscripten.h
  #define FC_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
  #define FC_EXPORT extern "C"
#endif

namespace
{
    // Does [p, p+bytes) lie inside the wasm linear memory? A pointer can be aligned, and its length can fit a
    // 32-bit address space, and the span can still run off the end of the heap — an aligned pointer four
    // bytes below the top with frames=2 passes every other check here and then traps with "memory access out
    // of bounds". This cannot prove the caller actually owns the span (no ABI of this shape can), but it does
    // turn "the module dies" into "the call is refused".
    bool inHeap (const void* p, std::uint64_t bytes)
    {
#if defined(__EMSCRIPTEN__)
        const std::uint64_t base = (std::uint64_t) reinterpret_cast<std::uintptr_t> (p);
        if (base == 0) return false;
        const std::uint64_t end = base + bytes;
        if (end < base) return false;                                     // wrapped
        return end <= (std::uint64_t) emscripten_get_heap_size();
#else
        (void) p; (void) bytes;
        return true;                                                      // native: no linear memory to bound
#endif
    }

    // THE ONE CHANNEL PREDICATE OF THESE ABIs, read by the input checks below and by every
    // fc_probe_<mode>_storage_bytes query. A second copy would be a seam the two roads could drift apart
    // along, which is the whole class this ABI keeps closing. The range is tested
    // BEFORE the narrowing to the analyzers' `int`: the conversion of a uint32 above INT_MAX is well defined
    // in C++20 and would land outside [1, kMaxChannels] anyway, but a width is refused here because it IS
    // out of range, not because a cast happened to carry it back out of range.
    bool geometry (std::uint32_t channels)
    {
        return channels >= 1 && channels <= (std::uint32_t) felitronics::core::kMaxChannels;
    }

    // Whether `frames` x `channels` float32 planes fit one 32-bit address space — the span a wasm32 caller can
    // have malloc'd at all. A function of the geometry alone, so a price that takes the programme's LENGTH (tempo's)
    // can refuse the same spans the run refuses, with this one predicate rather than a copy of it.
    bool spanFits (std::uint32_t frames, std::uint32_t channels)
    {
        return (std::uint64_t) frames * (std::uint64_t) channels * sizeof (float) <= (std::uint64_t) 0xFFFFFFFFu;
    }

    // The planar input span: non-null, non-empty, a width the core has, 4-byte aligned, and inside the heap.
    bool planarSpan (const float* planar, std::uint32_t frames, std::uint32_t channels)
    {
        if (planar == nullptr || frames == 0) return false;
        if (! geometry (channels)) return false;
        if ((reinterpret_cast<std::uintptr_t> (planar) & 0x3u) != 0) return false;   // a misaligned float* reads
                                                                                     // garbage in a release build
                                                                                     // and only traps under SAFE_HEAP
        // frames*channels must address real memory: on wasm32 the product is what a caller malloc'd, so a
        // wrapped one would hand us a window onto someone else's heap.
        if (! spanFits (frames, channels)) return false;
        return inHeap (planar, (std::uint64_t) frames * (std::uint64_t) channels * sizeof (float));
    }

    // FOUR of the five offline analyzers accept an EMPTY programme and report on it — lowend does
    // NOT, because `fcore_measure lowend` refuses it too and the two roads must refuse the same set. — `fcore_measure report`
    // on /dev/null prints 2109 bytes of a perfectly good empty report. planarSpan() refuses frames == 0,
    // and rightly so for `clips`, whose emptiness is a FILE that was probably truncated; but here the
    // caller hands over a buffer, and "no audio" is a measurement, not a truncation. Refusing it in the
    // module while the CLI answers it is a byte-parity break, so these runs take this check instead.
    bool planarSpanOrEmpty (const float* planar, std::uint32_t frames, std::uint32_t channels)
    {
        if (! geometry (channels)) return false;
        if (frames == 0) return true;                                  // nothing to address, nothing to bound
        return planarSpan (planar, frames, channels);
    }

    // An output span of `count` elements of `align` bytes each: non-null, aligned, inside the heap.
    bool outSpan (const void* out, std::uint32_t count, std::uint32_t align)
    {
        if (out == nullptr) return false;
        if ((reinterpret_cast<std::uintptr_t> (out) & (align - 1u)) != 0) return false;
        return inHeap (out, (std::uint64_t) count * align);
    }

    // `st.ok`, ALWAYS — never a bare `st.bytes()`. A REFUSED Storage IS NOT AN EMPTY ONE:
    // ProgrammeReport::Storage carries a DeterministicLoudnessMeter::Storage, whose bytes() has a constant
    // ring term (LoudnessMeter.h, `kSubRing`), so a default-constructed one reports 2400 bytes. A query that
    // forwarded that would quote a price for a measurement that cannot happen — and only for `report`, so a
    // test that probed the other four would not see it. (Measured on a9816e2; it is also why
    // ProgrammeReport.h's own "all zeros where prepare() refuses" note is true of the members and not of the
    // total.)
    template <typename Storage>
    double demand (const Storage& st)
    {
        if (! st.ok) return 0.0;
        if constexpr (requires { st.firstBytes(); }) return (double) st.firstBytes();
        else return (double) st.bytes();
    }
}

#endif   // FC_ABI_GUARDS_H
