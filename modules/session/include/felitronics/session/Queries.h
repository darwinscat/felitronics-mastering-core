// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/LandingResult.h>
#include <felitronics/session/Measurements.h>

namespace felitronics::session
{
class Snapshot;
inline constexpr std::uint32_t kQueryColumns = 2048, kWaveformStride = 13;
inline constexpr std::uint64_t kQueryValues = std::uint64_t (kQueryColumns) * 4u * kWaveformStride;
// The kinds, in the order they were published. WHAT A MASTER ANSWERS (masterId, and audioId the source it was made from):
//   LimiterGr, PeakClipGr   the retained reduction traces, on the delivered-frame grid
//   GlueGr                  the glue's (the compressor's) gain reduction from the delivered render, rows as LimiterGr's on
//                           its buckets; a master whose glue did not compress answers Unavailable, reason NoSignal
//   SaturationShave         what the saturation (the soft clipper) took off the peaks, in dB >= 0, from the delivered
//                           render, rows as LimiterGr's on its buckets: per internal quantum of the chain, the stage's
//                           input peak times its clean gain over its output peak (ClipperPeaks' pair), floored at 0 —
//                           a bucket's maxDb its largest quantum's, meanDb the frames' mean. A master whose soft clipper
//                           did not shape answers Unavailable, reason NoSignal
//   MasterWaveform          its retained buckets, L and R: [from,to,channel,min,max,rms,finite]
//   MasterAxes              the same buckets as the source's Waveform answers — four axes (L, R, Mid, Side), rows of
//                           kWaveformStride: [from,to,axis,min,max,peak,envelope,rms,low,middle,high,finite,reason]
//   Momentary, ShortTerm    with a masterId: the master's own loudness curves, [frame,value,reason], a row per 100 ms of
//                           the delivered audio, asked and named in the SOURCE's frames (the frame of the same moment
//                           its window ends at) — the source's own request plus a masterId; without one, the source's
//   MasterReport            the master whole — the record a full snapshot carries for it, rows included (QueryView::master)
// WHAT A SOURCE ALSO ANSWERS WITHOUT A MEASUREMENT (masterId 0, the whole source's frames, as LowSpectrum asks):
//   DitherFloor             the noise floor the current delivery leaves (owner, 07.10): [hz,dbPerBin,reason] on a LOG grid
//                           of `columns` points from fromHz (> 0) to toHz, both ends exactly as asked; Pending, with
//                           nothing allocated, until the plan has placed the devices. The delivery is the plan's
//                           (plan.dither: its bit depth, on, shaping) at the target's rate (the source's where the target
//                           keeps it). The
//                           total added noise is white — TPDF dither of ±1 LSB with its quantiser, LSB²/4; plain
//                           rounding where no dither runs, LSB²/12; LSB = 2^-(bits-1) of full scale 1.0 — shaped by the
//                           dither's NTF = 1 − H (Weighted, Psychoacoustic: felitronics-core Dither.h; flat with none).
//                           The value is in the forensics `meanPower` convention, so it lies on the song's spectrum on
//                           one axis: 10·log10 of the power a Hann window of N = 2^SourceForensicsParams::fftOrder (16384)
//                           reads in ONE bin at the SOURCE's rate, |X_k|² / (N·Σw²), one-sided, not folded —
//                           variance · |NTF(f)|² · sourceRate / (deliveryRate · N). Above the delivery's Nyquist:
//                           reason Unsupported; a delivery of 32 bits and more (no quantiser): reason NoSignal.
enum class QueryKind : std::uint8_t { Waveform, LowSpectrum, LowSide, Momentary, ShortTerm, Clipping, Stereo, LimiterGr, PeakClipGr, MasterWaveform,
                                      MasterAxes, MasterReport, GlueGr, SaturationShave, DitherFloor };
// WHAT LowSpectrum ANSWERS per band of the retained low-end measurement, interpolated between the bands' centres:
//   Density   the band's energy per hertz (its energy over its width in Hz) — the tilt-free quantity: level across
//             bands that widen with frequency; the default, and what LowSpectrum answered before this field existed
//   Energy    the band's whole energy — what a bar per band shows; above the density by 10·log10(band width in Hz)
//             dB, so it climbs with frequency where the density is flat
enum class SpectrumQuantity : std::uint8_t { Density, Energy };
enum class QueryStatus : std::uint8_t { Ready, Pending, Unavailable, Empty, InvalidRange, ColumnLimit, StaleSource, Memory, Contract, Cancelled, FloatingPointEnvironment };
// Frame ranges are integer [fromFrame,toFrame). Invalid/out-of-source ranges are refused,
// never clamped. Waveform emits min(columns,range length) buckets with integer floor boundaries.
// Spectrum/Side use columns equally spaced Hz points, inclusive endpoints (one point: fromHz).
// The spectral measurements cover the whole source: those requests require [0,source.frames).
// crossoverHz selects the retained 120 or 150 Hz measurement, never a new analysis.
struct MeasurementQuery
{
    QueryKind kind = QueryKind::Waveform;
    std::uint64_t audioId = 0, fromFrame = 0, toFrame = 0;
    std::uint32_t columns = 512;
    std::uint64_t requestId = 0;
    double crossoverHz = 120, fromHz = 20, toHz = 250;
    std::uint32_t masterId = 0; // required by every master kind; with Momentary/ShortTerm the master's curve; 0 is a source query
    SpectrumQuantity spectrum = SpectrumQuantity::Density;   // LowSpectrum alone reads it
};
struct QueryView
{
    MeasurementQuery request {};
    std::uint64_t audioId = 0, revision = 0, measurementKey = 0;
    QueryStatus status = QueryStatus::Pending;
    MeasurementReason reason = MeasurementReason::Pending;
    std::uint32_t sampleRate = 0, channels = 0, stride = 0;
    std::uint64_t total = 0, stored = 0;
    bool complete = false, cacheHit = false;
    std::uint64_t pcmFramesRead = 0;
    std::span<const double> values;
    // MasterReport alone: the kept master, rows included. Every other kind leaves it empty; MasterReport has no values.
    std::optional<Kept> master;
};
// A response owns its rows independently of the Session, cache eviction, load and other answers.
class QueryResult final
{
public:
    QueryResult() noexcept;
    ~QueryResult();
    QueryResult (QueryResult&&) noexcept;
    QueryResult& operator= (QueryResult&&) noexcept;
    QueryResult (const QueryResult&) = delete;
    QueryResult& operator= (const QueryResult&) = delete;
    [[nodiscard]] const QueryView& view() const noexcept;
    [[nodiscard]] static std::uint64_t storageFor (const QueryView& view) noexcept;
    [[nodiscard]] static QueryResult copy (const QueryView& view) noexcept;
private:
    QueryView view_ {};
    std::unique_ptr<double[]> rows_;
    std::unique_ptr<Snapshot> master_;         // the owner of a MasterReport answer's record and rows
};
struct QueryDemand
{
    QueryStatus status = QueryStatus::Ready;
    std::uint64_t bytes = 0, largestBlockBytes = 0, rowBytes = 0;
};
} // namespace felitronics::session
