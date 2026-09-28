// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/Queries.h>
#include <felitronics/analysis/WaveformIndex.h>

namespace felitronics::session::detail
{
struct WaveformState
{
    analysis::WaveformIndex index;
    MeasurementValue numbers[4] {};
    std::uint64_t bytes = 0;
    bool prepared = false, finished = false;
};
struct QueryCache
{
    struct Entry
    {
        MeasurementQuery request {};
        std::uint64_t key = 0, bytes = 0;
        QueryResult result;
    };
    Entry entries[2];
    unsigned next = 0;
    [[nodiscard]] std::uint64_t bytes() const noexcept;
};
} // namespace felitronics::session::detail
