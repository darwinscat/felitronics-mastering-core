// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "fc_session_abi.h"
#include <felitronics/session/Config.h>
#include <alloc_counter.h>
#include <cstdio>
namespace session_destroy_control
{
bool armed = false;
fc_session_status inner = FC_SESSION_OK;
void onFree() noexcept
{
    if (armed) { armed = false; inner = fc_session_destroy (0); }
}
}
using namespace session_destroy_control;
int main()
{
    const auto v = felitronics::session::config::Config::versions().all;
    const fc_session_capabilities caps { sizeof (caps), 9007199254740991.0, 48000, 255, 9007199254740991.0 };
    fc_session h = 0;
    if (fc_session_create (&caps, std::uint32_t (v), std::uint32_t (v >> 32), &h) != FC_SESSION_OK) return 1;
    armed = true;
    const auto outer = fc_session_destroy (h);
    if (inner != FC_SESSION_ERR_POISONED || outer != FC_SESSION_ERR_POISONED) return 2;
    if (fc_session_destroy (h) != FC_SESSION_ERR_POISONED || fc_session_set_capacity (0, nullptr) != FC_SESSION_ERR_POISONED) return 3;
    std::puts ("destroy: reentrant deallocation poisons inner, outer, and subsequent calls");
}
