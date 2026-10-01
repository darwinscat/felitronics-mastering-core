// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE DECLARED BUDGET — law 11d as felitronics::session keeps it, and the bricks under it (a landing search, a resumable
// render) with it: MEMORY IS DECLARED BEFORE THE WORK. Before a call
// runs, the session states what that call will ask the heap for; after it ran, the allocation counter says what it
// asked for; declared >= requested, or the check is red. A shell that reads the declaration and finds its heap too
// small can refuse the call before it is made — on the wasm tier an allocation that cannot be served is not a refusal
// but the end of the module — and that is only worth anything if the declaration is never short.
//
// A shared test header: modules/mastering's suites use it as modules/session's do, and it includes nothing of either
// (tools/lint/check-brick-includes.mjs holds the bricks to never including the session). One call is declared,
// Session::create(). The harness does not depend on which call it holds: a declared call is a
// demand, and each demand is one line in its suite through spend() and covers().
//
// INCLUDING THIS HEADER INSTALLS THE COUNTER (it includes core's test_support/alloc_counter.h, which replaces every
// form of `new`), so it belongs in exactly ONE translation unit of an executable, as that header says.
//
// What it cannot see is what the counter cannot see (alloc_counter.h lists it: malloc, a dependency's own pool, a path
// nobody drives) — and one thing of its own: a declaration that is LARGER than the request passes. That direction is
// the harmless one, and law 11d allows it (a bound, not an exact figure, for a call that frees and reallocates);
// where a demand is exact by construction its suite pins the equality besides.

#include <alloc_counter.h>

#include <cstdint>
#include <algorithm>
#include <string>

namespace felitronics::declared
{

// What one piece of work asked the heap for: the number of requests and their bytes, as the counter's `rawBytes` —
// exactly what reached the allocator. The session asks with plain `new` and `new[]` and holds no container, so there is
// no container padding to take off: the counter's `bytes` subtracts what MSVC's STL adds on top of a container's own
// request of 4 KiB or more, and would count a plain array of that size short (alloc_counter.h, "WHAT THE NUMBERS
// MEAN").
struct Spent
{
    long long requests = 0;
    long long bytes = 0;
};

// Runs `work` and returns what it requested. A delta, never an absolute: the counter spends allocations of its own
// before main() to prove itself.
template <typename Work>
Spent spend (Work&& work)
{
    namespace alloc = felitronics::test::alloc;
    const long long requests = alloc::count.load();
    const long long bytes = alloc::rawBytes.load();
    work();
    return { alloc::count.load() - requests, alloc::rawBytes.load() - bytes };
}

// Does the declaration cover what was requested?
inline bool covers (std::uint64_t declared, const Spent& s)
{
    return s.bytes >= 0 && (std::uint64_t) s.bytes <= declared;
}

// A sum of every allocation requested during a lifecycle conservatively bounds
// the extra bytes that can be live at any instant, even across replacement peaks.
struct LifecycleBudget
{
    std::uint64_t declared = 0, largestBlock = 0, allocated = 0, largestCall = 0;
    bool charge (Spent s) noexcept
    {
        if (s.bytes < 0 || std::uint64_t (s.bytes) > declared - std::min (declared, allocated)) return false;
        allocated += std::uint64_t (s.bytes);
        largestCall = std::max (largestCall, std::uint64_t (s.bytes));
        return largestCall <= largestBlock;
    }
};

inline std::string describe (std::uint64_t declared, const Spent& s)
{
    return "declared " + std::to_string (declared) + " bytes, requested " + std::to_string (s.bytes) + " in "
         + std::to_string (s.requests) + " allocation(s)";
}

} // namespace felitronics::declared
