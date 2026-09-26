// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <cstdint>
#include <memory>

namespace felitronics::session
{

// A release number, as the project() line of the repository that built it states it.
struct Version
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

//==============================================================================
// felitronics::session::Session — the mastering session: the one object a shell (the web worker, the desktop
// application, the native CLI) talks to. What it will hold — the loaded programme, the measurements, the plan, the
// project — and the commands that will change it come later; today it is the empty object those grow in, and what
// is already fixed is the ground it stands on. docs/SESSION.md has the laws and what holds each one.
//
// A COMPILED LIBRARY, NOT A HEADER. Everything else in this repository is header-only and compiles under its
// consumer's flags. This does not: its decisions are thresholds, and a threshold computed with a fused multiply-add
// or a reassociated sum is a different decision. So `felitronics::session` is a static library whose sources are
// compiled with ITS flags (no FP contraction, no fast-math, no exceptions, no RTTI — modules/session/CMakeLists.txt),
// and an application that links it cannot recompile the brain with its own. This header is the whole of what a
// consumer compiles, which is why it carries no function body that computes anything: an inline body here would be
// compiled under the consumer's flags again.
//
// ONE OWNER, NO SHARING. A Session is created on the heap and owned by the caller's unique_ptr; destroying it is
// the unique_ptr's reset. It is neither copied nor moved — a shell holds it by address (the C ABI's handle table
// does), and a move would leave that address answering for an empty object.
class Session final
{
public:
    // THE DEMAND OF create(), before it is made (law 11d: memory that cannot be had is fatal, so the core publishes
    // what it will ask for instead). The bytes create() requests from the heap, counted by the same expression that
    // sizes the request, so the two cannot drift; a shell compares it with what its heap can give before it calls.
    // REQUESTED bytes: the allocator's own header and alignment are the caller's margin, as everywhere in this tree.
    [[nodiscard]] static std::uint64_t createBytes() noexcept;

    // A new, empty session. Never null: under -fno-exceptions a heap that cannot serve createBytes() ends the process
    // (natively) or the module (wasm) inside this call, which is what the demand above exists to keep a shell clear of.
    [[nodiscard]] static std::unique_ptr<Session> create() noexcept;

    ~Session();

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;
    Session (Session&&) = delete;
    Session& operator= (Session&&) = delete;

    // THIS LIBRARY's release (felitronics-mastering-core) and the felitronics-core it was compiled against — answered
    // by the compiled library, not by this header, so they name the binary that runs, whatever header a consumer
    // happened to include. Both numbers decide results: a recipe replayed on another pair is a different master.
    [[nodiscard]] static Version version() noexcept;
    [[nodiscard]] static Version coreVersion() noexcept;

private:
    Session() noexcept = default;
};

//==============================================================================
// THE BUILD CONTRACT, ASKED OF THE LIBRARY ITSELF. Does the library's own code fuse `a*b + c` into one rounding? It
// must not: that is what -ffp-contract=off on the target promises, and the flag is the kind of line that disappears
// in a refactor with every test still green. The answer is computed inside the library, under the library's flags,
// so it is true of the binary and not of whoever calls it.
//
// It can only see what the machine can do: where the ISA has no fused multiply-add to contract into (baseline
// x86-64) nothing fuses under any flag, and this answers false whatever the build says. The rows that CAN fuse —
// arm64 everywhere, x86-64 with FMA enabled — are where it holds the flag.
[[nodiscard]] bool fusesMultiplyAdd() noexcept;

} // namespace felitronics::session
