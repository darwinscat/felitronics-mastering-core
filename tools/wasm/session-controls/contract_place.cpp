// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// Contract fixture seam for the pre-placement slice 0 scenarios; absent from the shipped module.
#include "BuildGuards.h"
#include "fc_session_abi.h"
#include "Driver.h"
#include <emscripten/emscripten.h>

using felitronics::session::Session;
extern Session* contractSession (fc_session) noexcept;
extern "C" EMSCRIPTEN_KEEPALIVE fc_session_status contract_place (fc_session handle, unsigned phase)
{
    auto* session = contractSession (handle);
    if (! session) return FC_SESSION_ERR_HANDLE;
    const auto job = session->measurementJob();
    const auto source = session->source().hash;
    const bool ok = phase == 1 ? felitronics::session::detail::Driver::measured1 (*session, job, source)
                               : phase == 2 && felitronics::session::detail::Driver::measured2 (*session, job, source);
    return ok ? FC_SESSION_OK : FC_SESSION_ERR_CONTRACT;
}
