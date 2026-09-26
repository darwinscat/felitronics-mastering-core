### session · tools — `felitronics::session`: the mastering session, compiled, with its laws held by the build

A new module, `felitronics::session` (`<felitronics/session/Session.h>`): the one object a shell talks to, where the
voicing and the project belong — the product is a shell that shows, plays and stores files. Today it is an empty
`Session` (`create()`, destruction by its owner, `version()`, `coreVersion()`, and `createBytes()`, the demand of
`create()`); what this release fixes is the ground it stands on. The laws, which of felitronics-core's apply to it and
which do not, and what holds each one: `docs/SESSION.md`.

**The repository's first compiled target.** `felitronics::session` is a STATIC library whose sources are compiled
with PRIVATE flags — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti`; MSVC `/fp:precise /EHs-c- /GR-
/we4530 /we4541` — so an application that links it cannot recompile it with its own. The library refuses to compile
if exceptions, RTTI or fast-math reach it (or MSVC's `/fp:contract`); `fusesMultiplyAdd()` measures contraction from
inside it, and its suite calls it from a translation unit compiled with contraction forced on. Four build controls
(`try`, `throw`, `typeid`, `dynamic_cast`) compile a forbidden construct with the library's own compile options and
require the build to fail with that construct's diagnostic. It reports the releases it was built from, compiled in.
Consumers link it like any other module: `target_link_libraries(app PRIVATE felitronics::session)`.

**Its laws, each held by a check with a control.** A new lint, `tools/lint/check-session-laws.mjs` (scope and
exceptions in `tools/lint/session-laws.txt`), refuses mutable state with static storage duration in the module
(namespace scope, class statics, function-local statics, `thread_local`, several names in one declaration), any
header that reaches the operating system, files, the console, the locale, threads (`<atomic>` included), the clock,
randomness, process-wide state, exceptions or RTTI — each with its reason — and the calls that reach the same through
headers that cannot be banned (`std::to_string`, the `sto*` family, …); and it requires every file of the module to be
in the deterministic zone, which `tools/lint/det-math-zone.txt` now extends to it, so felitronics-core's det-math lint
refuses a system libm call anywhere in the session. `tools/lint/session-controls/run.sh` plants a violation of every
rule in the real tree and requires the lint to fail on the planted file and line (13 controls); the det-math controls
gain one in the session. Memory is declared before the work: a declared-budget harness on core's allocation counter
holds `create()` to `createBytes()`, and is itself shown to fail on a sample that under-declares.

**`fc_session` v1**, the C ABI over it (`tools/fc_session_abi.h`, `tools/wasm/fc_session.cpp`): `fc_session_abi_version`,
`fc_session_create`, `fc_session_destroy` — handles with generations, a status per call (`fc_session_status`, its
own codes), the out-pointer checked before anything is written, at most `FC_SESSION_MAX_HANDLES` (8) live sessions per
module instance, and the poison: a call that finds another still in progress answers `FC_SESSION_ERR_POISONED` for
good. The append-only rule is in its header. The handle table and the poison flag are the only mutable globals session
has, and the lint names both. `tools/wasm/build.sh` builds a fifth module, `fcsession` (`createFcSession`; ES-module
web glue and node glue, byte-identical wasm): 2.5 KB of wasm, 1.2 KB brotli. `tools/wasm/session-check.mjs` holds it
to its exact export set and runs its surface on the artifact; `felitronics_session_abi_tests` runs the TU natively
(ASan, UBSan) and on the wasm tier.

**`fcore_session`**, the native CLI over the session: `fcore_session version` prints both releases and the ABI version;
`fcore_session run <script|->` accepts a script with no command in it (empty, or comments and blank lines) and prints
`done 0`, and refuses a script with a command in it with exit status 2 and nothing on stdout. Its contract is held
byte for byte by ctest.
