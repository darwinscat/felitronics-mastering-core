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

// Why a call was refused. A refused call did nothing: it allocated nothing, and it created or changed no session.
enum class Status : std::uint8_t
{
    Ok = 0,

    // The calling thread's floating-point environment is not IEEE-754's default: it flushes subnormal results to zero,
    // reads subnormal inputs as zero, or rounds other than to nearest. A host that set FTZ or DAZ for its audio thread
    // and calls the session on it is the usual case. The session's numbers would not be the numbers it computes on any
    // other row, so it refuses rather than answering with them. Nothing in the library changes the environment; the
    // caller restores it (or calls from another thread) and calls again.
    FloatingPointEnvironment = 1,

    // A helper the session shares with the rest of the program was kept, by the linker, in a copy compiled with FP
    // contraction across statements (gcc's default -ffp-contract=fast, or fast-math) — the application linking the
    // library does not compile with felitronics-core's FP policy. docs/SESSION.md, "Inline code and the linker".
    ContractedHelper = 2,
};

class Session;

// What create() gives back: a session, or the reason there is none (`session` null, `status` not Ok).
struct Created
{
    Status status = Status::Ok;
    std::unique_ptr<Session> session;
};

//==============================================================================
// felitronics::session::Session — the mastering session: the object a shell (the web worker through the fcsession
// module, a desktop application linking the library, the native CLI fcore_session) talks to. It is empty: it is created,
// destroyed and asked for its version, and it keeps no state. docs/SESSION.md has the laws it is held to and what holds
// each one.
//
// A COMPILED LIBRARY, NOT A HEADER. Everything else in this repository is header-only and compiles under its consumer's
// flags. This does not: `felitronics::session` is a static library whose sources are compiled with ITS flags (no FP
// contraction, no fast-math, no exceptions, no RTTI — modules/session/CMakeLists.txt), and an application that links it
// cannot recompile it with its own. This header is the whole of what a consumer compiles, which is why it carries no
// function body (the session-laws lint refuses one): a body here would be compiled under the consumer's flags.
//
// ONE OWNER, ONE THREAD AT A TIME. A Session is created on the heap and owned by the caller's unique_ptr; destroying it
// is the unique_ptr's reset. It is neither copied nor moved — a shell holds it by address (the C ABI's handle table
// does). Calls are made from one thread at a time: two threads calling into the library at once is a data race, and
// nothing in the library detects it.
class Session final
{
public:
    // THE DEMAND OF create(), before it is made (law 11d: memory that cannot be had is fatal, so the demand is published
    // instead). The bytes create() requests from the heap, counted by the same expression that sizes the request, so the
    // two cannot drift. REQUESTED bytes: the allocator's own header and alignment are the caller's margin.
    [[nodiscard]] static std::uint64_t createBytes() noexcept;

    // A new, empty session — or a refusal, in this order, before anything is allocated:
    //   Status::FloatingPointEnvironment   the calling thread's FP environment (see checkFloatingPointEnvironment);
    //   Status::ContractedHelper           the kept copy of a shared helper was compiled with contraction.
    // An accepted create never gives a null session: under -fno-exceptions a heap that cannot serve createBytes() ends
    // the process (natively) or the module (wasm) inside this call, which is what the demand above keeps a shell clear of.
    [[nodiscard]] static Created create() noexcept;

    // Is the calling thread's floating-point environment IEEE-754's default — no flush-to-zero, no denormals-are-zero,
    // rounding to nearest? Status::Ok if it is, Status::FloatingPointEnvironment if not. Read with ordinary arithmetic;
    // it changes nothing. create() asks it first; every call that computes asks it on entry.
    [[nodiscard]] static Status checkFloatingPointEnvironment() noexcept;

    ~Session();

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;
    Session (Session&&) = delete;
    Session& operator= (Session&&) = delete;

    // THIS LIBRARY's release (felitronics-mastering-core) and the felitronics-core it was compiled against — answered by
    // the compiled library, not by this header, so they name the binary that runs, whatever header a consumer included.
    // Both numbers decide results: a recipe replayed on another pair is a different master.
    [[nodiscard]] static Version version() noexcept;
    [[nodiscard]] static Version coreVersion() noexcept;

private:
    Session() noexcept = default;
};

} // namespace felitronics::session
