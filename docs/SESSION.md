<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# felitronics::session — its laws, and what holds each one

`felitronics::session` is the mastering session: the one object a shell talks to — the web worker through the
`fcsession` wasm module, the desktop application by linking the library, and `fcore_session`, the native CLI. It is
the layer that **decides**: the voicing (preset tables, targets, what the machine proposes) and the project belong
here, beside the chain and the analyzers they drive, and not in the product. The product is a shell: it shows, it
plays, it stores files, and every decision it displays is the session's.

Today the module is an empty `Session` — created, destroyed, asked for its version. What is fixed from the first
line is the ground it stands on: how it is built, and the laws it keeps. A law here is written down together with
the check that holds it, and each check has a control that turns it red; a law that no check holds is marked as
held **only in words**.

## Not real time — deterministic and bounded

The session runs no audio thread and no sample loop. It is called synchronously by a shell, with its input as
arguments, and answers with values. Its laws are not about deadlines; they are about **replay** — the same commands
into a new session must give the same session, on every row the product runs on — and about **bounds**: memory it
will ask for is stated before it is asked for.

## Which of felitronics-core's laws apply

The laws are felitronics-core's (`docs/DSP-ARCHITECTURE.md` §2), numbered as there.

| law | applies? | held by |
|---|---|---|
| **1** no threads | yes | the `os` rule of the session-laws lint refuses every threading header, `<atomic>` included (a long job runs in steps the shell drives; cancelling it is a call between two steps, not a flag another thread sets); on the wasm tier the suite links with `--wrap=pthread_create` (core's policy) and `fcsession` passes core's no-threads audit of the artifact |
| **2** no allocation in the audio path | **no** — allocation is allowed | the session is offline code, never on an audio thread (the loudness solver it will drive already runs on `std::vector`). What replaces the ban is law 11d below: memory is declared before the work |
| **4** heavy dependencies behind a seam | yes | **only in words** today: the session reaches no heavy primitive yet. It will reach them through the modules of this repository and of the core, which keep their seams |
| **6** no exceptions, RTTI, OS, filesystem, locale; no global mutable state | yes | exceptions and RTTI: the target's flags, asserted from inside the library, and four build controls (below). OS, files, console, locale, clock: the `os` rule. Global state: the `globals` rule |
| **7** a C++ subset every toolchain accepts | yes | the CI matrix compiles it — clang, gcc 13 and 14, MSVC, emscripten — and in this repository's own builds its sources compile under core's hygiene warning set with `-Werror` |
| **8**, **11a** the sample clock | **no** | the session has no stream of samples and no clock of its own; it has commands |
| **9** no `long double` | yes | core's long-double lint reads every `modules/*/include` and `modules/*/src`, this module's included, and the wasm tier's artifact gate reads every emitted object, its objects included |
| **10** FP contraction is stated | yes — **as `off`** | the target's own flags (below). Core states `on` for its tree; the session's numbers are compared across rows, native and wasm, and baseline wasm has no fused multiply-add to contract into, so a contracting native build would disagree with the module. Measured from inside the library by `fusesMultiplyAdd()`, called from a translation unit compiled with contraction forced on |
| **11**, **11b** a request that cannot be honoured is refused whole; checks in a fixed order; disarm, validate, write | yes | today the C boundary's: its checks run in the header's order (poison, out-pointer, handle, table), a refused call writes nothing and allocates nothing (`felitronics_session_abi_tests`), and `fcore_session` reads and checks a whole script before it creates a session, so a refused script touched none. The session's own commands keep the same law as they arrive |
| **11d** memory is declared before the work; exhaustion is fatal, so the demand is published | yes | `Session::createBytes()` is the demand of `Session::create()`, counted by the expression that sizes the request; the declared-budget harness (`modules/session/tests/DeclaredBudget.h`, core's one allocation counter) holds every declared call to *declared ≥ requested* — today the create, exactly — and is itself shown to fail on a sample that asks for more than it declares. The C boundary adds nothing to the demand (its table is static) and keeps the poison: an entry point that finds a call still in progress answers `FC_SESSION_ERR_POISONED` for good |

Laws 3, 5, 11c and 11e are not listed: they govern a sample loop and the objects that stream audio, and the session
has neither.

### And one law of its own: deterministic math

The session's decisions are thresholds, and a threshold computed through the system libm is a different decision on
another row — `log10` alone disagrees between Apple's libm and glibc at 2 % of the points this tree measured. So
**every file of `modules/session` is in the deterministic zone** (`tools/lint/det-math-zone.txt`): felitronics-core's
det-math lint, run from this repository with `--satellite`, refuses a system transcendental in any of them. The zone
is a written list, so it is extended file by file — and the `zone` rule of the session-laws lint refuses a file of the
module that has no line there, so the list cannot fall behind the module.

## The build — a compiled library with flags of its own

Everything else in this repository is header-only and compiles under its consumer's flags. The session is a
**static library**: its sources are compiled once, with its own flags, and an application that links it cannot
recompile the brain with its flags instead. The flags are PRIVATE (`modules/session/CMakeLists.txt`):

- `-fno-fast-math -ffp-contract=off` — in that order: `-fno-fast-math` does not undo an explicit
  `-ffp-contract=fast` that came before it, so the contraction switch is stated last, where nothing after it moves it.
- `-fno-exceptions -fno-rtti` — a `throw`, a `try`, a `typeid` or a `dynamic_cast` in a session source is a compile
  error.
- MSVC: `/fp:precise`, `/EHs-c- /GR-`, and `/we4530 /we4541`, which make a `try`, a `typeid` and a `dynamic_cast`
  errors rather than warnings. A bare `throw` still compiles under `/EHs-c-`; on that row the clang and gcc rows of
  the matrix are what refuse it.

Three things hold them:

1. **The library asserts its own flags.** `src/BuildContract.cpp` does not compile if exceptions, RTTI or fast-math
   are on, or if MSVC is not at `/fp:precise` or is at `/fp:contract`. One translation unit is enough: the flags are
   the target's, the same for all of its sources.
2. **Contraction is measured**, because no compiler announces `-ffp-contract`. `fusesMultiplyAdd()` computes
   `(1 + 2⁻²⁷)² − (1 + 2⁻²⁶)` inside the library; rounded twice it is exactly 0, fused it keeps 2⁻⁵⁴. The session's
   suite calls it from a translation unit compiled with `-ffp-contract=fast` (`/fp:contract`) and requires 0. Where
   the ISA has no fused multiply-add (baseline x86-64) nothing fuses under any flag and the suite says the check is
   vacuous there. Where there is one to contract into — every arm64 row (CI's macOS and arm64 Linux), and x86-64 built
   with FMA enabled — the check is live: built without the flag, the library answers "fused" and the suite is red.
3. **The build controls** (`modules/session/tests/controls/`). Each forbidden construct is one source, compiled with
   the library's own compile options, read off the target: clean as part of every build, and with the construct
   planted in a build that ctest runs and requires to **fail with that construct's diagnostic**. Dropping a flag from
   the library turns its control red; a build that fails for any other reason does not count as a pass.

In this repository's own builds the sources also compile under core's hygiene warning set with `-Werror`; a
consumer's build of the library does not get `-Werror`, so a warning a newer compiler invents cannot break it.

The library reports the releases it was built from — `Session::version()` (this repository) and
`Session::coreVersion()` (the felitronics-core it was compiled against) — answered by the compiled code, not by a
header, so they name the binary that runs. Both decide results: a recipe replayed on another pair is another master.

## The session-laws lint

`tools/lint/check-session-laws.mjs`, run from the repository root; its scope and its only exceptions are
`tools/lint/session-laws.txt`.

- **`globals`** — no variable with static storage duration that the code can change: namespace scope (named or
  anonymous namespace, `extern` included), a class's `static` data member, a function-local `static`, anything
  `thread_local`. `constexpr` and `const` data are allowed. An array of `const char*` is not const — the pointers
  are not — and is refused. A declaration of several such names is refused whatever they are, so a second name
  cannot pass under an allowed first one. Why here more than anywhere: a session is replayed, and sessions live
  side by side; a global is where their histories meet.
- **`os`** — no header that reaches the operating system, files, the console, the locale, threads, the clock,
  randomness, process-wide state (errno, the floating-point environment, the default memory resource), exceptions or
  RTTI; and no call, through a header that cannot be banned, that reaches the same things (`std::to_string` and the
  `sto*` family, which format and parse through the locale or throw; `set_new_handler`; …). The lint's header lists
  every one with its reason. `<charconv>` is allowed: exact and locale-free, it is what the session's own number
  formatting stands on.
- **`zone`** — every file of `modules/session` is listed in the deterministic zone (above).

It is a lexer and says what it cannot do: a macro that expands to a global, an include or a call is invisible to
it; it checks direct includes only; it reads "function declaration or variable?" from the shape of a declaration,
and where the shape is ambiguous it refuses. A file it cannot follow is red, not skipped.

**Its controls** (`tools/lint/session-controls/run.sh`) plant a violation of every rule in the real tree — each shape
of mutable state, a forbidden header and a forbidden call in `modules/session`, a file missing from the zone, a third
global in the C boundary and a second name behind an allowed one, rotted lists, a file whose braces do not close —
and require the lint to fail on the planted file and line. CI's lints job runs the self-test, the tree and the
controls, and felitronics-core's det-math lint with a planted `std::cos` in the session among its controls.

## The C boundary — `fc_session`

`tools/fc_session_abi.h`, implemented by `tools/wasm/fc_session.cpp`. Its version is **0, a draft**: the ABI version,
a session created and destroyed through a handle, and the poison — and no promise. Any entry point, argument, code or
constant may change without a bump, and **no release of this repository presents `fcsession` as a stable
interface**; a shell's loader gates on `fc_session_abi_version() >= 1`. Version 1 is the first frozen surface, frozen
together with a `create` that takes the shell's capabilities (its heap ceiling, the highest rate it accepts, the
devices it offers) and its config, and with the create's demand forwarded through the ABI (law 11d) — the two things
a stable create cannot do without. It follows fc_master's law — the facade is thin: handles instead of
pointers, a status per call, checks on the addresses a page computed, the poison, and nothing that decides.

It keeps the **only mutable globals session has anywhere** — the handle table and the poison flag — because a handle
must name a session between two calls and the poison must outlive the call that never returned.
`tools/lint/session-laws.txt` names both, and the `globals` rule refuses a third.

From v1 the surface grows **append-only**, by the rule already written in its header: an entry point added, or a field
appended to a struct that crosses the boundary, moves `FC_SESSION_ABI_VERSION`; nothing existing changes meaning,
moves or goes.

`fcsession` is its wasm module (`tools/wasm/build.sh`): the facade and every source of `modules/session/src` in one
link, with the library's flags and releases — 2.5 KB of wasm, 1.2 KB brotli. `tools/wasm/session-check.mjs` holds the
artifact to its exact export set and runs its surface there; `felitronics_session_abi_tests` runs the same translation
unit natively (ASan, UBSan) and on the wasm tier.

## The native CLI — `fcore_session`

`fcore_session version` prints the two releases and the ABI version; `fcore_session run <script>` reads a command
script (`-` is stdin) into a fresh session and prints `done <commands>`. There are no commands yet: a script with none
in it — empty, or comments and blank lines — answers `done 0`, and a script with a command in it is refused with exit
status 2 and nothing on stdout. It links the library as C++, the way the desktop application does, so a script run
through it and through `fcsession` are the two roads into one session.
