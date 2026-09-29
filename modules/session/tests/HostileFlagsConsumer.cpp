// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <felitronics/session/Session.h>

bool sessionHostileCanCreate()
{
    auto c = felitronics::session::Session::create();
    return c.status == felitronics::session::Status::Ok && c.session != nullptr;
}
