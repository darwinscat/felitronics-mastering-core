// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL for the FACADE role — the C boundary exactly as it ships, with a THIRD global planted beside its
// two. The facade may keep the handle table and the poison flag, by name (tools/lint/session-objects.txt), and nothing
// else: the gate must refuse g_planted while it finds each allowed global once.

#include "fc_session.cpp"

namespace
{
int g_planted = 0;
}

extern "C" int fc_session_control_planted (void) { return ++g_planted; }
