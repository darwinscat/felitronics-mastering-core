// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — an inline static data member: the same variable, spelled as a class member.
namespace felitronics::session
{
struct ControlPlanted
{
    inline static int controlCount = 0;   // VIOLATION
};
}
