### session · tools — `felitronics::session`: the mastering session, compiled, with its laws held by the build

A new module, `felitronics::session` (`<felitronics/session/Session.h>`): the object a shell talks to. It is an empty
`Session` — `create()` (which returns the session or the reason there is none), destruction by its owner, `version()`,
`coreVersion()`, `createBytes()` (the demand of `create()`) and `checkFloatingPointEnvironment()`. It keeps no state. The
laws it is held to, which of felitronics-core's apply and which do not, and what holds each one: `docs/SESSION.md`.

**The repository's first compiled target.** A STATIC library whose sources are compiled with PRIVATE flags in one
`SHELL:` group — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti` (`modules/session/build-flags.txt`, which
`tools/wasm/build.sh` reads too); MSVC 2022+ `/fp:precise /EHs-c- /EHa- /GR- /we4530 /we4541` — so an application that
links it cannot recompile it with its own, and CMake's option de-duplication cannot drop a member of the group. Other
compilers, clang-cl, icx-cl and icx are refused at configure time. Every translation unit includes `src/BuildGuards.h`
first, which refuses exceptions, RTTI, fast-math, `FLT_EVAL_METHOD` other than 0 and, on MSVC, `/fp:contract`. The
compile line is read back from `compile_commands.json` — this build's, and a consumer's that states
`-ffp-contract=off` first. The library's sources are also compiled with `-ffp-contract=fast` and with fast-math licences
ahead of its own options and must answer IEEE-754 anyway; build controls compile `try`, `throw`, `typeid`, `dynamic_cast`
and appended `-ffast-math` / `-fexceptions` / `-frtti` and require the build to fail on each. Consumers link it like any
other module: `target_link_libraries(app PRIVATE felitronics::session)`.

**`create()` refuses before it allocates**: a thread that flushes subnormals to zero, reads them as zero or rounds other
than to nearest (`Status::FloatingPointEnvironment`, read with ordinary arithmetic), and a program in which the linker
kept a copy of a shared inline helper compiled with contraction across statements (`Status::ContractedHelper`, a
header-inline canary called through a pointer). A desktop application compiles with felitronics-core's FP policy.

**The laws, each held by a check with a control.** An object-file gate (`modules/session/tests/object-gates.cmake`, on
every native row and the wasm tier, with `objdump`, `dumpbin` or `llvm-readobj`) reads every object of the library and of
its C boundary: no symbol in writable memory (the boundary: exactly its handle table and poison flag), and nothing
called that is not on `tools/lint/session-objects.txt` — so `printf`, `fopen`, `time`, `getenv`, `strtod`, `isalpha`,
`rand` are refused whatever header declared them and however they are spelled or reached; twelve controls compile each
shape and require the refusal. A source lint (`tools/lint/check-session-laws.mjs`) holds what leaves no symbol: an
include allowlist, no pragma but `#pragma once`, no exception or RTTI token in any `#if` branch, no conditional
compilation outside the guards, no atomics, cycle counters or inline assembly, no `std::unordered_*`, `std::hash` or
unstable sort, no function body in the public header, the guards first in every unit — scanned from the target's
sources (`modules/session/sources.txt`, cross-checked against `compile_commands.json`) and the `#include` closure,
failing closed on files it cannot classify; `tools/lint/session-controls/run.sh` plants 19 violations and requires each
to fail on its file and line. Every file of the module is in the det-math zone and every translation unit is an entry
point (two new det-math controls). Memory is declared before the work: a declared-budget harness on core's allocation
counter holds `create()` to `createBytes()`.

**`fc_session`, a draft — ABI version 0, no promise** (`tools/fc_session_abi.h`, `tools/wasm/fc_session.cpp`):
`fc_session_abi_version`, `fc_session_create`, `fc_session_destroy`; handles with 24-bit generations — a slot retires at
its last generation instead of wrapping, so a stale handle can never name a new session — at most
`FC_SESSION_MAX_HANDLES` (8) live sessions and `FC_SESSION_SLOT_GENERATIONS` creates per slot, the session's refusals as
`FC_SESSION_ERR_FP_ENVIRONMENT` / `FC_SESSION_ERR_CONTRACTED_HELPER`, and the poison. The boundary is compiled with the
library's own options. `tools/wasm/build.sh` builds a fifth module, `fcsession` (`createFcSession`; ES-module web glue and
node glue, byte-identical wasm, `--wrap=pthread_create`, the sources `sources.txt` lists and none it does not): 3.2 KB of
wasm, 1.5 KB brotli. `tools/wasm/session-check.mjs` compares every export of the module against the ABI and the runtime's
own, runs the surface, and walks one slot through all of its generations; a control copy with one undeclared export
must be refused.

**`fcore_session`**, the native CLI over the session: `fcore_session version` prints both releases and the ABI version;
`fcore_session run <script|->` accepts a script with no command in it and prints `done 0`, and refuses a script with a
command in it, or a session that refuses to be created, with exit status 2 and nothing on stdout.
