// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

// THE PREVIOUS SNAPSHOT SIZING, KEPT AS AN ORACLE (slice 5). Wire::snapshotBytes used to run the text codec over the
// whole view — Codec::encodedBytes, every row as decimal text — only to learn whether the view encodes, and then size it
// again with binary rows. It now checks what that pass checked and walks the view once, as the write does. The oracle is
// the previous function, copied from src/Wire.cpp as it was, never the changed code: felitronics_session_master_query_tests
// holds the sizes and statuses to it, felitronics_session_snapshot_sizing the cost.

#include "JsonCodec.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/session/Wire.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace felitronics::session::previous
{
inline TransferNeed previousSnapshotBytes (const SnapshotView& v)
{
    const auto check = Codec::encodedBytes (v);
    if (check.status != CodecStatus::Ok) return { check.status, 0, 0 };
    detail::Writer w; w.binaryRows = true; w.value (v);
    if (! w.good || w.size > 4294967295ull || w.rowSize > 4294967295ull / sizeof (double)) return { CodecStatus::Invalid, 0, 0 };
    return { CodecStatus::Ok, std::uint32_t (w.size), std::uint32_t (w.rowSize * sizeof (double)) };
}
inline bool sameNeed (const TransferNeed& a, const TransferNeed& b)
{
    return a.status == b.status && a.jsonBytes == b.jsonBytes && a.rowBytes == b.rowBytes;
}
// A view with many reading points: the rows the text pass had to print one by one.
struct Wide
{
    std::vector<ReadingPoint> points;
    SnapshotView view {};
    explicit Wide (std::size_t count) : points (count)
    {
        for (std::size_t i = 0; i < count; ++i) points[i] = { i * 4800u, -23.0 + 1e-3 * double (i % 9973) + 1.0 / 3.0 };
        view.momentary = points; view.shortTerm = points;
    }
};
} // namespace felitronics::session::previous
