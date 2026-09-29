// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <felitronics/session/Measurements.h>
#include <memory>

namespace felitronics::session::detail
{
// Output storage belongs to the session and outlives every analyzer workspace.
struct LiveMeasurements
{
    std::unique_ptr<double[]> loudness, clips;
    std::uint64_t rowCapacity = 0, clipCapacity = 0, rows = 0, runs = 0, frames = 0, hop = 0;
    std::uint64_t bytes = 0, work = 0;
    unsigned stage = 0;
    bool clippingWarned = false;
    MeasurementValue numbers[3][kMeasurementNumbers] {};
    MeasurementArray arrays[3][4] {};
    char names[3][kMeasurementNumbers][kMeasurementNameBytes] {};
    std::size_t numberCount[3] {};
};
}
