// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// SESSION-LAWS CONTROL — the locale, through a header that cannot be banned: std::to_string of a double is printf's
// "%f", and the decimal separator it prints is the user's locale's. run.sh plants this file in modules/session/src/
// and requires [OS] on the marked line.

#include <string>

namespace felitronics::session
{
std::string describeGain (double db)
{
    return std::to_string (db) + " dB";   // VIOLATION
}
}
