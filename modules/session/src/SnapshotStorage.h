// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include "BuildContract.h"
#include <cstdint>
#include <limits>

namespace felitronics::session::detail
{
// Size is size_t in the library; tests also exercise the wasm32 arithmetic on a native host.
template <class Size> constexpr std::uint64_t snapshotTextBytes (Size target, Size name) noexcept
{
    return std::uint64_t (target) + std::uint64_t (name);
}
template <class Size> constexpr std::uint64_t snapshotStorage (Size target, Size name, Size masters,
                                                             Size momentary, Size shortTerm, Size runs) noexcept
{
    return snapshotTextBytes (target, name) + std::uint64_t (masters)
         + std::uint64_t (momentary) + std::uint64_t (shortTerm) + std::uint64_t (runs);
}
template <class Size> Size snapshotAllocationSize (std::uint64_t bytes) noexcept
{
    if (bytes > std::uint64_t (std::numeric_limits<Size>::max())) storageOverflow();
    return Size (bytes);
}
} // namespace felitronics::session::detail
