// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — an encoding-prefixed character literal: u8'0' once read as a digit separator, which made the rest of
// the line a character literal and hid what followed.
namespace felitronics::session
{
void controlHidden ();
char8_t controlChar = u8'0'; void controlHidden () { throw 1; }   // VIOLATION
}
