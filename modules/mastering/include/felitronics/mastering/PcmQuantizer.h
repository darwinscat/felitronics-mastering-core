// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <cmath>
#include <cstdint>

namespace felitronics::mastering
{
// The integer WAV and measured output use one signed grid. A zero code for a
// non-finite sample is only for writing; the solver retains it for refusal.
[[nodiscard]] inline std::int32_t pcmCode (float sample, unsigned bits) noexcept
{
    if (! std::isfinite (sample)) return 0;
    const double full = double (std::uint32_t (1) << (bits - 1u));
    const double q = std::floor (double (sample) * full + 0.5);
    return std::int32_t (q <= -full ? -full : q >= full - 1.0 ? full - 1.0 : q);
}

[[nodiscard]] inline float pcmSample (float sample, unsigned bits) noexcept
{
    if (! std::isfinite (sample) || (bits != 16u && bits != 20u && bits != 24u)) return sample;
    return float (double (pcmCode (sample, bits)) / double (std::uint32_t (1) << (bits - 1u)));
}
}
