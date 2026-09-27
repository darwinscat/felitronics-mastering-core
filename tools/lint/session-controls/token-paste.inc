// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — token pasting, the way `thr ## ow` would spell a keyword no rule could see.
namespace felitronics::session
{
int controlPasted = controlA ## controlB;   // VIOLATION
}
