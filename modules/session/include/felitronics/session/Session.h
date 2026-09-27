// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

#include <felitronics/session/Commands.h>
#include <felitronics/session/Project.h>

#include <cstdint>
#include <memory>
#include <cstddef>
#include <span>
#include <string_view>

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

    // The config compiled into the library does not hold a number the session's commands check against where the
    // config's schema puts it. Not reachable in a library whose build ran the config's gate (every build does): the
    // session reads the same documents the gate read, and its reading is held to the schema's by the state suite.
    Config = 2,
};

class Session;

// What create() gives back: a session, or the reason there is none (`session` null, `status` not Ok).
struct Created
{
    Status status = Status::Ok;
    std::unique_ptr<Session> session;
};

// THE RECIPE OF A MASTER — what a master is made from, captured when it is asked for: the project as it was then (the
// machine's decisions included), the source it renders and the config's sound version. Two masters with equal recipes,
// made by one release, are one master.
struct Recipe
{
    Project project {};
    std::uint64_t source = 0;                  // the source's hash (Session::source)
    std::uint64_t sound = 0;                   // config::Config::versions().sound
};

// A master kept: its id and its recipe.
struct Kept
{
    MasterId id = 0;
    Recipe recipe {};
};

// The source a load gave, as the session holds it. The name and the samples live in the session until the next load.
struct Source
{
    std::uint32_t channels = 0;                // 0: nothing loaded
    std::uint32_t sampleRate = 0;
    std::uint64_t frames = 0;
    std::uint64_t hash = 0;                    // 64-bit FNV-1a of the rate, the channels, the frames and every sample's bits
    std::uint32_t fileRate = 0;
    bool rateKnown = false;
    std::uint8_t bitDepth = 0;
    std::string_view name;
};

namespace detail { struct Driver; struct Inspector; }

//==============================================================================
// felitronics::session::Session — the mastering session: the object a shell (the web worker through the fcsession
// module, a desktop application linking the library, the native CLI fcore_session) talks to. It holds one project, one
// source and the masters made from it, and it moves between the states of Commands.h only by the commands a shell asks
// (apply) and by its own transitions (the endings of the work). docs/SESSION.md has the laws it is held to and what holds
// each one.
//
// A COMPILED LIBRARY, NOT A HEADER. Everything else in this repository is header-only and compiles under its consumer's
// flags. This does not: `felitronics::session` is a static library whose own sources are compiled with ITS flags (no FP
// contraction, no fast-math, no exceptions, no RTTI — modules/session/CMakeLists.txt). The public headers are the whole
// of what a consumer compiles, which is why they carry no function body and no variable that is not constexpr (the
// session-laws lint refuses both): either would be compiled under the consumer's flags.
//
// WHAT THE LIBRARY'S FLAGS DO NOT REACH. Inline code the session shares with the program — a template or inline function
// from a header both include — is compiled once per translation unit that uses it, each copy under that unit's flags,
// and the linker keeps one copy for the program. So a program that links felitronics::session compiles EVERY translation
// unit with the session's FP flags — no contraction (-ffp-contract=off) and no fast-math — and does no partial linking.
// The wasm modules this repository builds are built whole, with those flags, and are not affected. docs/SESSION.md,
// "What the flags do not reach".
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

    // A new session — Empty, at revision 0, on the config's default target, the manual mode off — or a refusal before
    // anything is allocated: Status::FloatingPointEnvironment, the calling thread's FP environment (see
    // checkFloatingPointEnvironment), or Status::Config.
    // An accepted create never gives a null session: under -fno-exceptions a heap that cannot serve createBytes() ends
    // the process (natively) or the module (wasm) inside this call, which is what the demand above keeps a shell clear of.
    [[nodiscard]] static Created create() noexcept;

    // Is the calling thread's floating-point environment IEEE-754's default — no flush-to-zero, no denormals-are-zero,
    // rounding to nearest? Status::Ok if it is, Status::FloatingPointEnvironment if not. Read with ordinary arithmetic;
    // it changes nothing. Every call that computes asks it first: create(), check(), apply().
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

    //==========================================================================
    // THE COMMANDS (Commands.h)

    // Does the request, whole, or rejects it having changed nothing — the revision included. The checks run in the order
    // Commands.h declares: the floating-point environment, then the table, then the rest. Every accepted request moves
    // the revision by one, and so does each of the session's own transitions; a rejected one leaves it as it was.
    [[nodiscard]] Answer apply (const Request& request) noexcept;

    // What apply() would answer, without doing it — and the bytes it would ask the heap for if it is accepted, counted by
    // the expressions that size its requests (law 11d). apply() runs exactly this first, so the two cannot disagree.
    [[nodiscard]] Checked check (const Request& request) const noexcept;

    //==========================================================================
    // WHAT THE SESSION HOLDS — read between calls; a reference stays valid until the next call that changes the session.

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool mastering() const noexcept;
    [[nodiscard]] Column column() const noexcept;          // the state and the overlay, as the tables' column
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const Project& project() const noexcept;
    [[nodiscard]] std::string_view targetName() const noexcept;   // the key of the project's target row
    [[nodiscard]] Source source() const noexcept;
    [[nodiscard]] JobId job() const noexcept;               // the master being made; 0 when none is
    [[nodiscard]] const Recipe& jobRecipe() const noexcept; // ...and its recipe (meaningless when job() is 0)
    [[nodiscard]] std::span<const Kept> masters() const noexcept;   // the masters kept, in the order they were made

private:
    friend struct detail::Driver;   // the session's own transitions, driven by the work that ends (src/Driver.h)
    // Defined by the state suite alone, to stand a session where only billions of calls would take it (its last job
    // id); the library defines none.
    friend struct detail::Inspector;

    Session() noexcept = default;

    State state_ = State::Empty;
    bool mastering_ = false;
    std::uint64_t revision_ = 0;
    Project project_ {};
    // OWNED BUFFERS, EACH ONE EXACT REQUEST — not std::vector: a debugging standard library (MSVC's at
    // _ITERATOR_DEBUG_LEVEL 1 or 2) gives every vector a heap-allocated proxy of its own, which no declared demand
    // counts. The source: its samples planar, channel after channel (source_.channels × source_.frames), and the name
    // the load gave (source_.name views it; null when it is empty).
    Source source_ {};
    std::unique_ptr<float[]> samples_;
    std::unique_ptr<char[]> name_;
    // The master being made, and the masters kept: `masterCount_` of them in room for `masterRoom_`. The ids count
    // from 1 for the session's life.
    JobId job_ = 0;
    JobId lastJob_ = 0;
    Recipe jobRecipe_ {};
    std::unique_ptr<Kept[]> masters_;
    std::size_t masterCount_ = 0;
    std::size_t masterRoom_ = 0;
};

} // namespace felitronics::session
