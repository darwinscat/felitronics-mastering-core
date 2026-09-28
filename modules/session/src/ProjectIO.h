// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Session.h>

namespace felitronics::session::detail
{
struct ImportedProject
{
    Answer answer {};
    Project project {};
    MachineDifference differences[kDeviceFields] {};
    std::size_t differenceCount = 0;
    bool foreignCore = false;
    bool convertedDefaults = false;
    char originalDefaults[7] {};
};
[[nodiscard]] std::uint64_t importBytes (std::size_t size) noexcept;
[[nodiscard]] ImportedProject readProject (std::string_view bytes, std::uint32_t channels, std::uint32_t offeredDevices, std::uint32_t sourceRate) noexcept;
}
