// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Every scalar mapping, optional alternative, record and array crosses the real encoder here.
// The offline JS consumer validates its output against the generated declarations.
#include "CodecSchema.h"
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>

using namespace felitronics::session;
namespace
{
struct Fill
{
    unsigned mode;
    template <class T> void value (T& x)
    {
        if constexpr (std::is_same_v<T, bool>) x = mode % 2 != 0;
        else if constexpr (std::is_same_v<T, double>)
        {
            const double values[] = { 1.5, -0.0, std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 0.125 };
            x = values[mode];
        }
        else if constexpr (std::is_same_v<T, std::string_view>) x = "name \xF0\x9F\x8E\xB5";
        else if constexpr (std::is_enum_v<T>) x = T (mode % (detail::enumLast<T>() + 1));
        else if constexpr (std::is_integral_v<T>) x = mode % 2 ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
        else detail::describe (*this, x);
    }
    template <class T> void value (std::optional<T>& x)
    {
        if (mode == 0) x.reset();
        else { T v {}; value (v); x = v; }
    }
    template <class T> void value (std::span<const T>&) {}
    template <class T> void field (std::string_view, T& x) { value (x); }
};
}
int main()
{
    for (unsigned mode = 0; mode < 6; ++mode)
    {
        Fill fill { mode };
        SnapshotView view;
        Kept kept[1]; ReadingPoint points[1]; ReadingRun runs[1]; MachineDifference differences[1];
        fill.value (view); fill.value (kept[0]); fill.value (points[0]); fill.value (runs[0]); fill.value (differences[0]);
        view.sourceBytes = mode ? 9007199254740991.0 : 0.0;
        if (mode) { view.masters = kept; view.momentary = points; view.shortTerm = points; view.runs = runs; view.machineDifferences = differences; }
        const auto need = Codec::encodedBytes (view);
        if (need.status != CodecStatus::Ok) return 1;
        std::string json (std::size_t (need.bytes), '\0');
        if (Codec::encode (view, json) != CodecStatus::Ok) return 2;
        std::puts (json.c_str());
    }
    return 0;
}
