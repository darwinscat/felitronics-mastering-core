// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include <felitronics/session/Session.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Text.h>
#include <cstdio>
int main()
{
    using namespace felitronics::session;
    std::puts ("contract fixture reached"); std::fflush (stdout);
    const auto fact = text::Fact::of (text::FactId::WideBass, text::Arg::value (-12345.25, text::Unit::Db, 2));
    const auto rendered = text::Text::text (fact, text::Lang::En);
    auto made = Session::create();
    float samples[4] {}; const float* pcm[] { samples };
    if (made.session->apply (command::Load { 1, { pcm, 1, 4, 48000 }, {} }).rejection != Rejection::None) return 1;
    (void) made.session->step (16);
    (void) made.session->column();
    (void) config::Config::bind ("default = \"bad\"", "");
    return rendered.empty() ? 1 : 0;
}
