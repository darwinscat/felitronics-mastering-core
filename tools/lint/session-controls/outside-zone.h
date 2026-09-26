// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — a clean header that nobody listed in tools/lint/det-math-zone.txt. Every other law holds in
// it; the det-math lint would still let a system libm call in it through. run.sh plants it in
// modules/session/include/ and requires [ZONE] naming it.

#pragma once

namespace felitronics::session
{
struct Unlisted
{
    int value = 0;
};
}
