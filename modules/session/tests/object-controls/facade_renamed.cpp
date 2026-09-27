// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL for the facade's ALLOWANCE — the C boundary with its poison flag renamed. An allowance that names a
// symbol no longer there is rot: it would admit whatever takes the name later. The gate must refuse the object for the
// allowed name it did not find ([ALLOWANCE] g_callState), not only for the global it did.

#define g_callState g_callStateRenamed
#include "fc_session.cpp"
