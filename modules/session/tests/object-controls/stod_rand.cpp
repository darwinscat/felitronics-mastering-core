// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — the spellings a text match missed: a leading `::` and `std ::stod`, through a
// header the session may include (<string>). stod parses through the locale; rand is a process-wide generator.

#include <cstdlib>
#include <string>

namespace felitronics::session::control
{
int nextRandom() { return ::rand(); }
double parse (const std::string& s) { return std ::stod (s); }
}
