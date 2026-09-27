### session · tools — `felitronics::session`: the mastering session, compiled, with its laws held by the build

A new module, `felitronics::session` (`<felitronics/session/Session.h>`): the object a shell talks to. It is an empty
`Session` — `create()` (which returns the session or the reason there is none), destruction by its owner, `version()`,
`coreVersion()`, `createBytes()` (the demand of `create()`) and `checkFloatingPointEnvironment()`. It keeps no state. The
laws it is held to, which of felitronics-core's apply and which do not, and what holds each one: `docs/SESSION.md`.

**The repository's first compiled target.** A STATIC library whose sources are compiled with PRIVATE flags in one
`SHELL:` group — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti` (`modules/session/build-flags.txt`, which
`tools/wasm/build.sh` reads too); MSVC 2022+ `/fp:precise /EHs-c- /EHa- /GR- /we4530 /we4541` — and CMake's option
de-duplication cannot drop a member of the group. Other compilers, clang-cl, icx-cl and icx are refused at configure
time. Every translation unit — the library's and its C boundary's — includes `src/BuildGuards.h` first, which refuses
exceptions, RTTI, fast-math, `FLT_EVAL_METHOD` other than 0 and, on MSVC, `/fp:contract`. The compile line is read back
from `compile_commands.json` — this build's, and a consumer's that states `-ffp-contract=off` first: the library's group
must be the last word on floating point, exceptions and RTTI (a per-source `-ffp-model=fast` or `-fno-honor-nans` after
it is red), with no forced include or pass-through, entries matched by file and read with or without CMake's `output`
field (skipped on MSVC, whose guards hold it from inside each unit). The library's sources are also compiled with
`-ffp-contract=fast` and with fast-math licences ahead of its own options and must answer IEEE-754 anyway — the targets
that carry those positive controls are built optimised, so a build with no configuration is green for the right
reason; build controls compile `try`, `throw`, `typeid`, `dynamic_cast` and appended `-ffast-math` / `-fexceptions` /
`-frtti` and require the build to fail on each. Consumers link it like any other module:
`target_link_libraries(app PRIVATE felitronics::session)`.

**What the flags do not reach, stated rather than checked**: header-inline code the library shares with the program
(`std::` templates, core's header functions), of which the linker keeps one copy. A program that links
`felitronics::session` compiles EVERY translation unit with the session's FP flags — no contraction, no fast-math —
and does no partial linking; the wasm modules are built whole by this repository and are not affected.

**`create()` refuses before it allocates** a thread that flushes subnormals to zero, reads them as zero or rounds other
than to nearest (`Status::FloatingPointEnvironment`, read with ordinary arithmetic). NaN sign and payload and the FP
exception masks are outside the check.

**The laws, each held by a check with a control** — against honest mistakes and reasonable spelling variants, not a
hostile author (`docs/SESSION.md` states the threat model). An object-file gate (`modules/session/tests/object-gates.cmake`,
on every native row and the wasm tier, with `readelf`, `objdump`, `dumpbin` or `llvm-readobj`) reads every object of the
library and of its C boundary: no symbol in writable memory, judged by what the object says of each section (ELF and
COFF write flags; the read-only places by name on Mach-O and wasm, which carry no such flag) — the boundary keeps exactly
its handle table and poison flag, and an allowance naming a symbol that is gone is rot — and nothing called that is not
on `tools/lint/session-objects.txt` or defined with global binding elsewhere in the set, so `printf`, `fopen`, `time`,
`getenv`, `strtod`, `isalpha`, `rand` are refused whatever header declared them and however they are spelled or
reached, and a local `getpid` in one object answers no other object's call. Sixteen controls compile each shape — a
global in a section of its own naming among them, and the shipped boundary with a third global and with a renamed one —
and require the refusal to name the planted symbol whole; one — a constexpr table of pointers, relocated constant data —
requires the gate to accept it. A source lint (`tools/lint/check-session-laws.mjs`) holds what leaves no symbol, over the
module and the C boundary, after translation phase 2: an include allowlist (felitronics headers by name), no macros and
no directive but `#include` and `#pragma once` outside the guards and the boundary's stated allowance, no pragma, an
attribute allowlist (no vendor attribute, no section placement), no alternative tokens, no exception or RTTI token in
any `#if` branch, no atomics, cycle counters or inline assembly, no `std::unordered_*`, `hash<`, unstable sort or
`using namespace`, no function body, non-constexpr variable or namespace-scope function in the public header, the guards
first in every unit — scanned from the targets' sources (`modules/session/sources.txt` and the boundary's unit,
cross-checked against `compile_commands.json`) and the `#include` closure, failing closed on files it cannot classify;
`tools/lint/session-controls/run.sh` plants 41 violations and requires each to fail on its file and line. Every file of
the module and the boundary is in the det-math zone and every translation unit — the library's, the boundary's, the
CLI's — is an entry point (four new det-math controls). Memory is declared before the work: a declared-budget harness on
core's allocation counter holds `create()` to `createBytes()`.

**`fc_session`, a draft — ABI version 0, no promise** (`tools/fc_session_abi.h`, `tools/wasm/fc_session.cpp`):
`fc_session_abi_version`, `fc_session_create`, `fc_session_destroy`; handles with 24-bit generations — a slot retires at
its last generation instead of wrapping, so a stale handle can never name a new session — at most
`FC_SESSION_MAX_HANDLES` (8 — a fixed capacity, not configurable) live sessions and `FC_SESSION_SLOT_GENERATIONS`
creates per slot, the session's refusal as `FC_SESSION_ERR_FP_ENVIRONMENT`, and the poison. The boundary is compiled
with the library's own options and held to its source laws. `tools/wasm/build.sh` builds a fifth module, `fcsession` (`createFcSession`; ES-module web glue and
node glue, byte-identical wasm, `--wrap=pthread_create`, the sources `sources.txt` lists and none it does not): 2.9 KB of
wasm, 1.4 KB brotli. `tools/wasm/session-check.mjs` compares every export of the module against the ABI and the runtime's
own, runs the surface, and walks one slot through all of its generations; a control copy with one undeclared export
must be refused.

**`fcore_session`**, the native CLI over the session: `fcore_session version` prints both releases and the ABI version;
`fcore_session run <script|->` accepts a script with no command in it and prints `done 0`, and refuses a script with a
command in it, or a session that refuses to be created, with exit status 2 and nothing on stdout.
