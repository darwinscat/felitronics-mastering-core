<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# felitronics::session — its laws, and what holds each one

`felitronics::session` is the mastering session: the object a shell talks to — the web worker through the `fcsession`
wasm module, a desktop application by linking the library, and `fcore_session`, the native CLI. Today it is an empty
`Session`: it is created, destroyed and asked for its version, and it checks the floating-point environment of the
thread that calls it. It keeps no state.

What is fixed is the ground it stands on: how it is built, and the laws it keeps. Each law here is written down together
with the check that holds it, and each check has a control that turns it red. A law no check holds is marked as held
**only in words**.

## Deterministic, bounded, one thread at a time

The session runs no audio thread and no sample loop. It is called synchronously, with its input as arguments, and answers
with values. Its laws are about **replay** — the same calls into a new session give the same session, on every row the
product runs on — and about **bounds**: memory it asks for is stated before it is asked for.

Calls are made **from one thread at a time**. Two threads calling into the library at once is a data race: nothing in the
library detects it, and the C boundary's poison (below) does not either — the poison marks a call that never returned,
not two calls that overlap.

## Which of felitronics-core's laws apply

The laws are felitronics-core's (`docs/DSP-ARCHITECTURE.md` §2), numbered as there.

| law | applies? | held by |
|---|---|---|
| **1** no threads | yes | no threading header is on the source lint's include allowlist and `std::atomic` / `__atomic_*` / `__sync_*` are refused as tokens (an atomic leaves no symbol); a thread that is called is an undefined symbol the object-file gate refuses; on the wasm tier the suite and `fcsession` link with `--wrap=pthread_create` and pass core's no-threads audit of the artifact |
| **2** no allocation in the audio path | **no** — allocation is allowed | the session is offline code, never on an audio thread. What holds its memory is law 11d |
| **4** heavy dependencies behind a seam | yes | **only in words**: the session reaches no heavy primitive |
| **5** configurable sizes, a reported footprint | yes | the footprint of `create()` is published (`Session::createBytes()`) and held to what `create()` asks for; the C boundary's capacities are stated, not discovered — `FC_SESSION_MAX_HANDLES` sessions at once, `FC_SESSION_SLOT_GENERATIONS` creates per slot — and its suite pins both |
| **6** no exceptions, RTTI, OS, filesystem, locale; no global mutable state | yes | exceptions and RTTI: the target's flags, `src/BuildGuards.h` first in every unit, the build controls, and the source lint's token rule in every `#if` branch. OS, files, console, locale, clock, process state: the object-file gate (what the objects call) and the source lint's include allowlist. Global state: the object-file gate (what lives in writable memory) |
| **7** a C++ subset every toolchain accepts | yes | the CI matrix compiles it — clang, gcc 13 and 14, MSVC, emscripten — and in this repository's own builds its sources compile under core's hygiene warning set with `-Werror` |
| **8** software denormal handling in feedback kernels | **no** | the session has no feedback kernel. What it does about subnormals is a different guarantee: it refuses a thread that flushes them (the floating-point environment, below) |
| **8a**, **11a** the sample clock | **no** | the session has no stream of samples and no clock |
| **9** no `long double` | yes | core's long-double lint reads every `modules/*/include` and `modules/*/src`, this module's included, and the wasm tier's artifact gate reads every emitted object |
| **10** FP contraction is stated | yes — **as `off`** | the target's own flags in one `SHELL:` group, the compile line read back (this build's and a consumer's), the hostile-flags tests, the library's probes asked from a contracting caller, and the source lint's pragma rule. Core states `on` for its tree; the session's numbers are compared across rows, native and wasm, and baseline wasm has no fused multiply-add, so a contracting native build would disagree with the module |
| **11**, **11b** a request that cannot be honoured is refused whole; checks in a fixed order | yes | `create()` checks the floating-point environment, then the kept canary, then allocates: a refused create requested nothing (`felitronics_session_tests`). The C boundary's checks run in its header's order and a refused call writes nothing and allocates nothing (`felitronics_session_abi_tests`). `fcore_session` reads and checks a whole script before it creates a session |
| **11d** memory is declared before the work | yes | `Session::createBytes()` is the demand of `create()`, counted by the expression that sizes the request; the declared-budget harness (`modules/session/tests/DeclaredBudget.h`, on core's one allocation counter) holds every declared call to *declared ≥ requested*, and is itself shown to fail on a sample that under-declares. The demand is checked in C++; the draft C ABI does not forward it. The C boundary adds nothing to it (its table is static) and keeps the poison |

Not listed, and why: **3** (float in the hot path) — there is no hot path; **11c** (a pause is silence) — there are no
clock-only calls; **11e** (a restart adopts an accepted publication) — the session publishes and adopts nothing.

### And one law of its own: deterministic math

The session's decisions are thresholds, and a threshold computed through the system libm is a different decision on
another row — `log10` alone disagrees between Apple's libm and glibc at 2 % of the points this tree measured. So **every
file of `modules/session` is in the deterministic zone** (`tools/lint/det-math-zone.txt`) and **every translation unit
is an entry point**: felitronics-core's det-math lint, run from this repository with `--satellite`, refuses a system
transcendental in any of them and follows each unit's `#include`s whatever the included file is called. The source lint
refuses a file of the module with no `zone` line and a unit with no `entry` line.

## The build — a compiled library with flags of its own

Everything else in this repository is header-only and compiles under its consumer's flags. The session is a **static
library**: its sources are compiled with its own flags, and an application that links it cannot recompile it with its
own. The flags are PRIVATE (`modules/session/CMakeLists.txt`), and they are **one `SHELL:` group**, so CMake's option
de-duplication cannot remove a member of it: a parent that states `add_compile_options(-ffp-contract=off)` first used to
make CMake drop the library's own later `-ffp-contract=off`, leaving core's `-ffp-contract=on` last on the line.

- gcc, clang, emscripten: `modules/session/build-flags.txt` — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti`,
  in that order (`-fno-fast-math` does not undo a `-ffp-contract=fast` given before it). `tools/wasm/build.sh` reads the
  same file, so the library and the wasm module are compiled with one definition of the flags.
- MSVC 2022 or later: `/fp:precise /EHs-c- /EHa- /GR- /we4530 /we4541` — the last two make a `try`, a `typeid` and a
  `dynamic_cast` errors. A bare `throw` compiles under `/EHs-c-`; the source lint refuses the token.
- Any other compiler, a compiler in MSVC-compatible mode (clang-cl, icx-cl), and Intel's icx (fast floating point by
  default) are refused at configure time: their spelling of these guarantees is unverified here.

What holds the flags:

1. **Every translation unit refuses to compile without them.** `src/BuildGuards.h`, which the source lint requires as
   the first `#include` of every unit, stops the build if exceptions, RTTI or fast-math are on, if
   `__FLT_EVAL_METHOD__` is not 0, or, on MSVC, if the compiler is older than 2022, not at `/fp:precise`, at
   `/fp:contract`, or on 32-bit x87 arithmetic. A per-source override is caught in the unit it overrides.
2. **The compile line is read back.** Contraction is the one flag no preprocessor macro announces on clang or gcc, so
   `felitronics_session_compile_line` reads this build's `compile_commands.json` and
   `felitronics_session_consumer_compile_line` configures a consumer (`modules/session/tests/consumer/`, the parent above)
   and reads its: for every source of the library the last `-ffp-contract=` is `off`, nothing after `-fno-fast-math`
   licenses fast math, and the last word on exceptions and RTTI is no. (Makefile and Ninja generators; MSVC announces all
   four to the preprocessor.)
3. **The library answers for itself, under hostile flags.** Its build probes (`src/FpProbes.h`: a fused multiply-add, a
   division turned into a reciprocal, a reassociated sum, a dropped signed zero) are compiled into the library and asked
   from a translation unit compiled with contraction forced on. The library's sources are also compiled again with
   `-ffp-contract=fast`, and with `-freciprocal-math -fassociative-math -fno-signed-zeros -fno-trapping-math` (MSVC
   `/fp:fast`), placed **ahead** of the target's options, and must answer IEEE-754 anyway — while the same probes compiled
   with the licence alone must change on that row, so the licence is live. A row with a fused multiply-add (every arm64
   row) requires the contraction controls to fuse; a row without one (baseline x86-64) says the check cannot fail there.
4. **The build controls** (`modules/session/tests/controls/`): each forbidden construct — `try`, `throw`, `typeid`,
   `dynamic_cast` — compiled with the library's own compile options, clean in every build and planted in a build ctest
   requires to fail with that construct's diagnostic; and `src/BuildContract.cpp` compiled with `-ffast-math`,
   `-fexceptions` or `-frtti` (MSVC `/fp:fast`, `/EHsc`, `/GR`) appended after the library's options, which must fail on
   the guard's own message. **On the wasm tier** the preset compiles everything with `-fno-exceptions -fno-rtti`, so the
   planted constructs fail there whatever the library's options say: that tier proves the preset, and the native rows
   prove the library.

In this repository's own builds the sources also compile under core's hygiene warning set with `-Werror`; a consumer's
build of the library does not get `-Werror`.

The library reports the releases it was built from — `Session::version()` (this repository) and `Session::coreVersion()`
(the felitronics-core it was compiled against) — answered by the compiled code, so they name the binary that runs.

## The floating-point environment

The flags decide what the compiler emits. The thread decides what the arithmetic does: a host may have set flush-to-zero
or denormals-are-zero for its audio thread, or changed the rounding mode, and no flag reaches that.
`Session::checkFloatingPointEnvironment()` reads it with ordinary arithmetic — no `<cfenv>` — and answers
`Status::FloatingPointEnvironment` for a thread that flushes subnormal results, reads subnormal inputs as zero, or rounds
other than to nearest. `create()` asks it first and refuses such a thread, having allocated nothing; the rule for every
call that computes is to ask it on entry, and `create()` is the only such call today. Nothing in the library changes the
environment: the caller restores it, or calls from another thread. `felitronics_session_tests` sets each condition the
way a host would (MXCSR, FPCR, `fesetround`), confirms it took effect, and requires the refusal; a row with no such
control (the wasm tier) says the check is not reachable there.

## Inline code and the linker

An inline function is compiled into every translation unit that uses it out of line, each copy under that unit's flags,
and the linker keeps one copy for the program. So when an application compiles, under its own flags, an inline function
the session also uses, the session may run the application's copy — and the library's flags say nothing about it.

- **A canary.** `src/ContractionCanary.h` is a header-inline multiply-add split across two statements, which the
  standard's contraction (felitronics-core's policy, `-ffp-contract=on`) never fuses and gcc's default
  `-ffp-contract=fast` and fast-math do. `create()` calls it through a pointer — the kept copy — and refuses with
  `Status::ContractedHelper` if it fuses. `felitronics_session_comdat_tests` links a copy compiled with
  `-ffp-contract=fast` ahead of the library and requires the refusal on every row that can fuse.
- **Its limit.** The canary proves the kept copy of itself. A foreign copy of another shared inline function is invisible
  to it. What holds the rest is structural: the session's arithmetic lives in its own translation units — its public
  header carries no function body, and the source lint refuses one — and the multiply-adds it shares through
  felitronics-core are pinned (`core::det::mulAdd` and `mul` round through a volatile, which no copy's flags can fuse).
- **What a desktop application owes.** It compiles with felitronics-core's floating-point policy
  (`cmake/FelitronicsPolicy.cmake`: `-ffp-contract=on`, no fast-math), and never with fast-math or gcc's default
  contraction for the translation units that include felitronics headers.

## The object-file gate

"No mutable state outside an object" and "no operating system, file, console, locale, clock or process state" are
questions the compiled object answers exactly, so they are asked of it (`modules/session/tests/object-gates.cmake`, on
every native row and the wasm tier, with the row's own symbol-table reader — `objdump`, `dumpbin`, `llvm-readobj`):

- **Writable memory.** Every symbol in a writable section — ELF `.data*` (not `.data.rel.ro`), `.bss*`, `.tdata*`,
  `.tbss*`, COMMON; Mach-O `__DATA` `__data` / `__bss` / `__common` / `__thread_*`; COFF `.data`, `.bss`, `.tls$`; wasm
  `.data` / `.bss` segments — is state with static storage duration. The library's objects may hold none; the C
  boundary's may hold exactly its handle table and its poison flag, named in `tools/lint/session-objects.txt` and each
  required to be found once.
- **What is called.** Every undefined symbol must be defined by another object of the set or match a line of
  `tools/lint/session-objects.txt` — operator new and delete, what the compiler emits for a copy, the stack protector,
  the toolchain's own markers, and the sanitizer runtime on the sanitizer row. `printf`, `fopen`, `time`, `getenv`,
  `strtod`, `isalpha`, `rand` — or `std::to_string` taken by pointer — are refused whatever header declared them.

Its controls (`modules/session/tests/object-controls/`) compile each shape the reviews found — a direct-initialised
global a declaration reader took for a function, a static inside a lambda, a global planted by a macro, an asm-labelled
global, class and function-local statics, an array of `const char*`, thread-local state, a const object with a start-up
constructor, the forbidden calls, a function taken by pointer, the `::rand` / `std ::stod` spellings — and require the
gate to refuse each, naming the symbol.

What the gate cannot see: code that is never emitted, and facilities that leave no symbol — an atomic, a clock read by an
intrinsic or by inline assembly, a pragma. Those are the source lint's.

## The source lint

`tools/lint/check-session-laws.mjs`, run from the repository root (`--build <dir>` cross-checks the target's sources
against that build's `compile_commands.json`). It scans every file of `modules/session` from the target's list
(`modules/session/sources.txt`, which CMake and `tools/wasm/build.sh` read too) and the `#include` closure of its units,
whatever the files are called; it refuses a file of unknown type, a unit the list does not name, and a file nothing
compiles or includes. Only `modules/session/tests` is outside it. Its rules: the include **allowlist** (canonically
spelled standard headers that reach no OS, file, locale, thread, clock or process state, felitronics headers that
resolve, quoted headers inside the module); no pragma but `#pragma once`, no `_Pragma` / `__pragma`, no per-function
optimisation or target attribute; no exception or RTTI token in any `#if` branch; no conditional compilation outside
`src/BuildGuards.h` and `src/BuildContract.cpp`; no atomics, cycle counters, target intrinsics or inline assembly; no
`std::unordered_*`, `std::hash` or unstable sort, whose order is implementation-defined; no function body in the public
header; `"BuildGuards.h"` first in every unit; every file in the det-math zone. Its controls
(`tools/lint/session-controls/run.sh`) plant each rule's violation in the real tree and require the lint to fail on the
planted file and line.

## The C boundary — `fc_session`

`tools/fc_session_abi.h`, implemented by `tools/wasm/fc_session.cpp`. Its version is **0, a draft**: no promise — any
entry point, argument, code or constant may change without a bump. It carries the ABI version, a session created and
destroyed through a handle, the session's refusals passed through as status codes, and the poison. It follows
fc_master's law — the facade is thin: handles instead of pointers, a status per call, checks on the addresses a page
computed, the poison, and nothing that decides. It is compiled with the session library's own options, natively and in
the wasm module.

- **Two globals, the only ones session has**: the handle table and the poison flag — a handle must name a session between
  two calls, and the poison must outlive the call that never returned. Both are trivially destructible, so no exit-time
  destructor runs. The object-file gate refuses a third.
- **Handles retire, they do not wrap.** A handle is a slot and that slot's 24-bit generation. A generation that wrapped
  would give an old handle's number to a new session, and the stale handle would destroy it (reproduced on the module:
  16 777 214 create/destroy cycles, a fraction of a second). So a slot issues `FC_SESSION_SLOT_GENERATIONS` handles and
  then retires; after `FC_SESSION_MAX_HANDLES × FC_SESSION_SLOT_GENERATIONS` creates a module instance answers
  `FC_SESSION_ERR_EXHAUSTED` for good. The native suite and `tools/wasm/session-check.mjs` walk one slot through all of
  its generations.
- **The poison**: an entry point that finds a call still in progress — an earlier one never returned (the wasm module
  aborted inside it), or it was re-entered from inside an allocation — answers `FC_SESSION_ERR_POISONED`, for good.

`fcsession` is its wasm module (`tools/wasm/build.sh`): the facade and the sources `modules/session/sources.txt` lists
(the build refuses a `.cpp` under `modules/session/src`, at any depth, that is not listed), linked with
`--wrap=pthread_create` — 3.2 KB of wasm, 1.5 KB brotli. `tools/wasm/session-check.mjs` compares every export of the
loaded module against the ABI's surface and the runtime's own, runs the surface, and walks the wrap boundary; `build.sh`
builds a control copy with one undeclared export and requires the check to refuse it.

## The native CLI — `fcore_session`

`fcore_session version` prints the two releases and the ABI version; `fcore_session run <script>` reads a command script
(`-` is stdin) into a fresh session and prints `done <commands>`. There are no commands: a script with none in it —
empty, or comments and blank lines — answers `done 0`; a script with a command in it, and a session that refuses to be
created, are refused with exit status 2 and nothing on stdout. It links the library as C++, the way a desktop
application does.
