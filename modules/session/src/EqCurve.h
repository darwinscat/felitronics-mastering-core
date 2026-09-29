// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include "Rules.h"
#include <felitronics/session/Session.h>
namespace felitronics::session::detail
{
void eqCurve (const Project& project, const Rules& rules, double rate, std::span<EqPoint> output) noexcept;
}
