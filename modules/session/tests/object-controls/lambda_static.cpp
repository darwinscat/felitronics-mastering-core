// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// OBJECT-GATE CONTROL — a mutable static hidden inside a lambda inside a const function-local static:
// the outer declaration is a constant, the state is one level in.

namespace felitronics::session::control
{
int nextLambda()
{
    static const auto f = [] {
        static int lambdaCounter = 0;
        return ++lambdaCounter;
    };
    return f();
}
}
