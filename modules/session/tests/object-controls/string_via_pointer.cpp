// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a forbidden facility taken by POINTER rather than called: std::to_string of a
// double formats through the C library's printf family (the locale), and no call-site pattern sees `&std::to_string`.

#include <string>

namespace felitronics::session::control
{
std::string describe (double x)
{
    auto p = static_cast<std::string (*) (double)> (&std::to_string);
    return p (x);
}
}
