// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — an unstable sort: equal elements come out in an implementation-defined order.
#include <algorithm>
#include <vector>
namespace felitronics::session
{
void controlOrder (std::vector<int>& v);
void controlOrder (std::vector<int>& v) { std::sort (v.begin(), v.end()); }   // VIOLATION
}
