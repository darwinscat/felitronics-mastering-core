// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a mutable global a declaration reader took for a function: the parenthesis holds
// a type, so `int counter (int{0})` read as a declaration with a parameter. The object file knows it is a variable.

namespace felitronics::session::control
{
int directInitCounter (int{0});
int nextDirect() { return ++directInitCounter; }
}
