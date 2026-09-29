// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Landing.h>
#include <felitronics/mastering/LandingSearch.h>
#include <felitronics/analysis/BandCrest.h>
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
    std::size_t crestCapacity = 0;
    analysis::BandCrestParams crestParams {};
    double sourceLufs = 0;
    std::uint64_t bytes = 0, largestBlock = 0;
};

struct MasterRows
{
    std::unique_ptr<LandingPass[]> passes;
    std::unique_ptr<LandingTraceBucket[]> traces;
    std::uint32_t traceCapacity = 0;
    std::unique_ptr<double[]> crest;
    std::size_t crestCapacity = 0;
    std::size_t crestMaskCopied = 0;
};

struct MasterJob final
{
    static MasterPlan plan (const Session& session, const command::Master& input) noexcept;
    static std::uint64_t fingerprint (const command::MasterReady& ready) noexcept;
    bool begin (const Session& session, const MasterPlan& plan);
    mastering::StepResult step (long long budget) noexcept;
    const mastering::LoudnessSolution& result() const noexcept { return search.result(); }
    const analysis::BandCrest& crestResult() const noexcept { return crest; }
    std::size_t copiedCrestRows() const noexcept { return crestCopied; }
    const MasterReport& reportResult() const noexcept { return report; }
    std::unique_ptr<float[]> takeOutput() noexcept { return std::move (output); }

    mastering::MasteringChain chain;
    mastering::OfflineRenderer renderer;
    mastering::TargetLoudnessSolver solver;
    mastering::DeliveryConverter converter;
    mastering::LandingSearch search { solver };
    analysis::BandCrest crest;
    std::unique_ptr<float[]> output;
    std::unique_ptr<float[]> impactScratch;
    const float* sourcePlanes[2] {};
    float* outputPlanes[2] {};
    int frames = 0, channels = 0;
    std::uint32_t deliveryRate = 0, sourceRate = 0;
    std::uint64_t sourceFrames = 0;
    std::size_t crestCopied = 0;
    std::span<const double> sourceMask;
    long long crestCursor = 0, impactDelay = 0;
    enum class Stage : std::uint8_t { Search, Prepare, Read, Finish, Copy, Done, Failed };
    Stage stage = Stage::Search;
    const Session* session = nullptr;
    analysis::BandCrestParams crestParams {};
    double sourceLufs = 0, targetLufs = 0, targetTp = 0;
    command::MasterReady ready {};
    MasterReport report {};
};
} // namespace detail
} // namespace felitronics::session
