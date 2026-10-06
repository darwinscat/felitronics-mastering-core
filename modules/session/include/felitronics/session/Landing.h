// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Config.h>
#include <felitronics/session/LandingResult.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include <cstdint>
#include <utility>

namespace felitronics::session
{

enum class LandingPlanStatus : std::uint8_t { Ready, InvalidTarget, UnavailableSource, GainRange };

// Captured once with the master recipe. The source measurement supplies one normalization trim;
// each render changes only the separate pre-limiter search gain and limiter ceiling.
struct LandingPlan
{
    LandingPlanStatus status = LandingPlanStatus::InvalidTarget;
    mastering::LoudnessRequest request {};
    double initialCeilingDbTp = 0.0;
    bool sourceRateImpactPass = false;
};

class LandingOps final
{
public:
    [[nodiscard]] static LandingPlan plan (const config::Engine& engine, bool sourceLoudnessValid,
                                           double sourceLufs, double targetLufs, double targetTruePeakDbTp,
                                           double sourceRate, double deliveryRate) noexcept;

    // Copies exactly the actual pass log into caller-owned rows, then publishes a view of those rows.
    [[nodiscard]] static bool summarize (const mastering::LoudnessSolution& solution,
                                         std::span<LandingPass> rows, LandingSummary& out) noexcept;
    [[nodiscard]] static bool summarize (const mastering::LoudnessSolution& solution,
                                         std::span<LandingPass> rows,
                                         std::span<LandingTraceBucket> limiterRows,
                                         std::span<LandingTraceBucket> peakClipRows,
                                         std::uint32_t deliveryRateHz, LandingSummary& out) noexcept;
    // A max master pulled up to its floor lands twice and keeps one record: passRows writes the first landing's pass log
    // into the rows when the floor landing begins, and this summary appends the floor landing's after those `earlier`
    // rows — the pass count and the work count are both landings'.
    [[nodiscard]] static bool summarize (const mastering::LoudnessSolution& solution, std::uint32_t earlier,
                                         std::uint64_t earlierWork, std::span<LandingPass> rows, LandingSummary& out) noexcept;
    [[nodiscard]] static bool passRows (const mastering::LoudnessSolution& solution, std::span<LandingPass> rows) noexcept;
    // The Session pump uses the pass-only summary followed by bounded trace copies.
    // The whole-call overload above remains the numerical control for those copies.
    [[nodiscard]] static bool stepTraces (const mastering::LoudnessSolution& solution,
                                         std::span<LandingTraceBucket> limiterRows,
                                         std::span<LandingTraceBucket> peakClipRows,
                                         std::uint32_t deliveryRateHz, LandingSummary& out,
                                         std::uint32_t& cursor, std::uint32_t budget) noexcept;
    // The same copies with the compressor's trace into `compressorRows` and `compressor`, and the soft clipper's shave
    // (`LoudnessSolution::saturationTrace`) into `saturationRows` and `saturation`, on the limiter's buckets. An empty
    // span: no such trace — both empty is the call above.
    [[nodiscard]] static bool stepTraces (const mastering::LoudnessSolution& solution,
                                         std::span<LandingTraceBucket> limiterRows,
                                         std::span<LandingTraceBucket> peakClipRows,
                                         std::span<LandingTraceBucket> compressorRows,
                                         std::span<LandingTraceBucket> saturationRows,
                                         std::uint32_t deliveryRateHz, LandingSummary& out,
                                         std::optional<LandingTrace>& compressor,
                                         std::optional<LandingTrace>& saturation,
                                         std::uint32_t& cursor, std::uint32_t budget) noexcept;
    // Seven f64 columns: [firstFrame,lastFrame,minDb,maxDb,meanDb,samples,nonFinite].
    // Selects retained buckets intersecting the requested delivered-frame range. Their original
    // bounds remain in the response, including buckets that cross either edge of the request.
    [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> bucketRange (
        const LandingTrace& trace, std::uint64_t fromFrame, std::uint64_t toFrame) noexcept;
    // Groups consecutive selected buckets without inventing raw samples. Zero means refusal.
    [[nodiscard]] static std::uint32_t query (const LandingTrace& trace, std::uint32_t columns,
                                               std::span<double> output) noexcept;
    [[nodiscard]] static std::uint32_t query (const LandingTrace& trace,
                                               std::uint64_t fromFrame, std::uint64_t toFrame,
                                               std::uint32_t columns, std::span<double> output) noexcept;
};

} // namespace felitronics::session
