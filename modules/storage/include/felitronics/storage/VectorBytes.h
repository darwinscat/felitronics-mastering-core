// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace felitronics::storage
{
// A separately allocated object, not allocator padding. Use the STL's type so the budget follows the
// implementation and pointer width. The constructor controls exercise this with iterator debugging on.
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL != 0
inline constexpr std::uint64_t kVectorProxyBytes = sizeof (std::_Container_proxy);
#else
inline constexpr std::uint64_t kVectorProxyBytes = 0;
#endif

// Core's fixed member layouts: PolyphaseOversampler has six vectors; LoudnessMeter has three;
// ReferenceTruePeakMeter has one scratch vector and one oversampler per supported channel.
inline constexpr std::uint64_t kPolyphaseProxies = 6;
inline constexpr std::uint64_t kLoudnessProxies = 3;
} // namespace felitronics::storage
