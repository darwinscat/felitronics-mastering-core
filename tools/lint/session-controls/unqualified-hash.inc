// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// SESSION-LAWS CONTROL — std::hash reached unqualified, through a using-declaration.
#include <string>
namespace felitronics::session
{
using std::hash;
unsigned long controlHash (int x);
unsigned long controlHash (int x) { return hash<int>{} (x); }   // VIOLATION
}
