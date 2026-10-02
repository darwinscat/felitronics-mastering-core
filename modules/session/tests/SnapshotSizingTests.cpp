// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// THE SNAPSHOT SIZED IN ONE PASS — ITS COST (slice 5). A ratio of two timings in one process. A target of its own, built
// as the other timing suites are (felitronics_session_live_tests): a clock is not for a unit compiled with the session's
// own flags, which refuse exceptions where the standard library's clock would bring them (MSVC C4530).

#include "PreviousSnapshotBytes.h"
#include <felitronics_test.h>
#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace felitronics::session;
using namespace felitronics::session::previous;
using felitronics::test::ok;

namespace
{
// On 200 000 reading points in each of two rows the size costs less than half the previous path's, the best of seven
// each — the previous path took more than the whole write.
void theSnapshotSizeCostsOnePass()
{
    const Wide wide (200000);
    const auto best = [&] (auto&& f)
    {
        double fastest = 1e300;
        for (int i = 0; i < 7; ++i)
        {
            const auto t0 = std::chrono::steady_clock::now();
            f();
            fastest = std::min (fastest, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        return fastest;
    };
    TransferNeed a {}, b {};
    const double now = best ([&] { a = Wire::snapshotBytes (wide.view); });
    const double before = best ([&] { b = previousSnapshotBytes (wide.view); });
    std::printf ("    sizing 2 x 200000 reading points: %.3f ms, the previous path %.3f ms\n", now * 1e3, before * 1e3);
    ok (sameNeed (a, b) && a.status == CodecStatus::Ok && now < 0.5 * before,
        "the snapshot is sized in one pass: under half the previous path's time, to the same sizes");
}
} // namespace

int main()
{
    std::printf ("felitronics::session — the snapshot sized in one pass\n");
    theSnapshotSizeCostsOnePass();
    return felitronics::test::report();
}
