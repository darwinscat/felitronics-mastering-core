// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once
#include <string>
int sessionScript (const std::string& script, const char* path, bool parseOnly);
[[noreturn]] void sessionScriptPoison (void (*continuation)());
