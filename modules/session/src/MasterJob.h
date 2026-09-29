// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Landing.h>
#include <felitronics/mastering/LandingSearch.h>
#include <memory>

namespace felitronics::session
{
class Session;
namespace detail
{
struct MasterPlan
{
    Rejection rejection = Rejection::Contract;
    command::MasterReady ready {};
    mastering::LoudnessRequest request {};
    std::uint32_t deliveryRate = 0;
    int frames = 0, traceBuckets = 0;
    std::uint64_t bytes = 0, largestBlock = 0;
};

struct MasterRows
{
    std::unique_ptr<LandingPass[]> passes;
    std::unique_ptr<LandingTraceBucket[]> traces;
    std::uint32_t traceCapacity = 0;
};

struct MasterJob final
{
    static MasterPlan plan (const Session& session, const command::Master& input) noexcept;
    static std::uint64_t fingerprint (const command::MasterReady& ready) noexcept;
    bool begin (const Session& session, const MasterPlan& plan);
    mastering::StepResult step (long long budget) { return search.step (budget); }
    const mastering::LoudnessSolution& result() const noexcept { return search.result(); }
    std::unique_ptr<float[]> takeOutput() noexcept { return std::move (output); }

    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    mastering::DeliveryConverter converter;
    mastering::LandingSearch search { solver };
    std::unique_ptr<float[]> output;
    const float* sourcePlanes[2] {};
    float* outputPlanes[2] {};
    int frames = 0, channels = 0;
    std::uint32_t deliveryRate = 0;
};
} // namespace detail
} // namespace felitronics::session
