// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Session.h>

namespace felitronics::session::detail
{
struct PlanInputs;
struct ImportedProject
{
    // The candidate, comparison rows and original label are inline; committing copies into the
    // session and its fixed OwnedFact buffers. Schema string conversions belong to toml::storageFor.
    [[nodiscard]] static constexpr std::uint64_t storageBytes() noexcept { return 0; }
    Answer answer {};
    Project project {};
    MachineDifference differences[kDeviceFields] {};
    std::size_t differenceCount = 0;
    bool foreignCore = false;
    bool convertedDefaults = false;
    char originalDefaults[7] {};
};
[[nodiscard]] Checked importBytes (std::string_view bytes) noexcept;
// The document read by schema, and its machine layer compared with the planner's for its target on this source
// (`inputs`, whose target is the document's).
[[nodiscard]] ImportedProject readProject (std::string_view bytes, const PlanInputs& inputs) noexcept;
}
