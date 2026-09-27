<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# felitronics::session — its laws, and what holds each one

`felitronics::session` is the mastering session: the object a shell talks to — the web worker through the `fcsession`
wasm module, a desktop application by linking the library, and `fcore_session`, the native CLI. A `Session` holds one
project, one source and the masters made from it; it moves between its states only by the commands a shell asks and by
its own transitions when a piece of work ends, and it answers every command whole (below, "The states and the
commands"). It checks the floating-point environment of the thread that calls it.

Beside it is its config, every number it decides with (below). What is fixed is the ground it stands on: how it is built, and the laws it keeps. Each law here is written down together
with the check that holds it, and each check has a control that turns it red. A law no check holds is marked as held
**only in words**.

## The threat model

The checks catch **honest mistakes and reasonable spelling variants** — a header that brought `printf` in, a static
someone forgot, `::rand` for `rand`, a keyword split across a line continuation, an attribute second in a list — **not
deliberate obfuscation by a hostile author**. Where a whole class of spelling can be closed by a cheap structural ban, it
is: session code has no macros, no directive beyond `#include` and `#pragma once` (and the few the guard files and the C
boundary are allowed by name), no alternative tokens, no vendor attributes, no namespace-scope function in its public
header. Nothing here is a C preprocessor or a C++ parser, and nothing tries to be one. What an author set on hiding
something could still do is exactly what a reviewer reads a diff for.

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
| **5** configurable sizes, a reported footprint | the footprint: yes. Configurable sizes: **not provided** | the footprint of `create()` is reported (`Session::createBytes()`) and held to what `create()` asks for; what each command adds to it is reported before it runs (`Session::check()`, law 11d below). The C boundary's sizes are fixed, stated and not configurable: its handle table has a capacity of **8** sessions at once (`FC_SESSION_MAX_HANDLES`), compiled in, and a slot issues `FC_SESSION_SLOT_GENERATIONS` handles; its suite pins both. A shell that needs another capacity needs another build — there is no switch for it, and none is promised |
| **6** no exceptions, RTTI, OS, filesystem, locale; no global mutable state | yes | exceptions and RTTI: the target's flags, `src/BuildGuards.h` first in every unit, the build controls, and the source lint's token rule in every `#if` branch. OS, files, console, locale, clock, process state: the object-file gate (what the objects call) and the source lint's include allowlist. Global state: the object-file gate (what lives in writable memory) |
| **7** a C++ subset every toolchain accepts | yes | the CI matrix compiles it — clang, gcc 13 and 14, MSVC, emscripten — and in this repository's own builds its sources compile under core's hygiene warning set with `-Werror` |
| **8** software denormal handling in feedback kernels | **no** | the session has no feedback kernel. What it does about subnormals is a different guarantee: it refuses a thread that flushes them (the floating-point environment, below) |
| **8a**, **11a** the sample clock | **no** | the session has no stream of samples and no clock |
| **9** no `long double` | yes | core's long-double lint reads every `modules/*/include` and `modules/*/src`, this module's included, and the wasm tier's artifact gate reads every emitted object |
| **10** FP contraction is stated | yes — **as `off`** | the target's own flags in one `SHELL:` group, the compile line read back (this build's and a consumer's), the hostile-flags tests, the library's probes asked from a contracting caller, and the source lint's pragma and attribute rules. Core states `on` for its tree; the session's numbers are compared across rows, native and wasm, and baseline wasm has no fused multiply-add, so a contracting native build would disagree with the module. The library's flags reach its own objects only: a program that links it compiles **every** translation unit with the same FP flags (below, "What the flags do not reach"). The sign and payload of a NaN, and the floating-point exception masks and flags, are outside every check here, as core's law 10 leaves them |
| **11**, **11b** a request that cannot be honoured is refused whole; checks in a fixed order | yes | `create()` checks the floating-point environment and the config it reads, then allocates: a refused create requested nothing (`felitronics_session_tests`). Every command runs the checks `Commands.h` declares, in their order, before it touches anything, and a rejected one changed nothing — the revision included: `felitronics_session_state_tests` compares the whole session before and after every rejection it produces, produces every rejection code, and holds the order with requests wrong in several ways. The C boundary's checks run in its header's order and a refused call writes nothing and allocates nothing (`felitronics_session_abi_tests`). `fcore_session` reads and checks a whole script before it creates a session |
| **11d** memory is declared before the work | yes | `Session::createBytes()` is the demand of `create()`, counted by the expression that sizes the request, and `Session::check()` gives the demand of every command before it runs — computed by the same function `apply()` runs first; the declared-budget harness (`modules/session/tests/DeclaredBudget.h`, on core's one allocation counter) holds `create()` and every command to *declared ≥ requested* (exactly equal, where the request is one exact allocation), holds `check()`, every rejection and every transition to nothing requested, and is itself shown to fail on a sample that under-declares. The demand is checked in C++; the draft C ABI does not forward it. The C boundary adds nothing to it (its table is static) and keeps the poison |

Not listed, and why: **3** (float in the hot path) — there is no hot path; **11c** (a pause is silence) — there are no
clock-only calls; **11e** (a restart adopts an accepted publication) — the session publishes and adopts nothing.

### And one law of its own: deterministic math

The session's decisions are thresholds, and a threshold computed through the system libm is a different decision on
another row — `log10` alone disagrees between Apple's libm and glibc at 2 % of the points this tree measured. So **every
file of `modules/session` is in the deterministic zone** (`tools/lint/det-math-zone.txt`), with the C boundary's
source and header, and **every translation unit is an entry point** — the library's, the C boundary's and the native
CLI's: felitronics-core's det-math lint, run from this repository with `--satellite`, refuses a system transcendental in
any of them and follows each unit's `#include`s whatever the included file is called (CI plants a `std::cos` in a header
each road includes). The source lint refuses a scanned file with no `zone` line and a unit with no `entry` line.

## The build — a compiled library with flags of its own

Everything else in this repository is header-only and compiles under its consumer's flags. The session is a **static
library**: its sources are compiled with its own flags — which settles what its own objects contain, and not what the
linker keeps of the header-inline code it shares with the program (below). The flags are PRIVATE
(`modules/session/CMakeLists.txt`), and they are **one `SHELL:` group**, so CMake's option
de-duplication cannot remove a member of it: a parent that states `add_compile_options(-ffp-contract=off)` first used to
make CMake drop the library's own later `-ffp-contract=off`, leaving core's `-ffp-contract=on` last on the line.

- gcc, clang, emscripten: `modules/session/build-flags.txt` — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti`,
  in that order: what `-fno-fast-math` does to an `-ffp-contract=` before it differs between compilers (Apple clang 21
  keeps an earlier `fast`, Apple clang 14 resets it to `on`), so the contraction switch is stated after it. `tools/wasm/build.sh` reads the
  same file, so the library and the wasm module are compiled with one definition of the flags.
- MSVC 2022 or later: `/fp:precise /EHs-c- /EHa- /GR- /we4530 /we4541` — the last two make a `try`, a `typeid` and a
  `dynamic_cast` errors. A bare `throw` compiles under `/EHs-c-`; the source lint refuses the token.
- Any other compiler, a compiler in MSVC-compatible mode (clang-cl, icx-cl), and Intel's icx (fast floating point by
  default) are refused at configure time: their spelling of these guarantees is unverified here.

What holds the flags:

1. **Every translation unit refuses to compile without them.** `src/BuildGuards.h`, which the source lint requires as
   the first `#include` of every unit — the library's and the C boundary's — stops the build if exceptions, RTTI or
   fast-math are on, if `__FLT_EVAL_METHOD__` is not 0, or, on MSVC, if the compiler is older than 2022, not at
   `/fp:precise`, at `/fp:contract`, or on 32-bit x87 arithmetic. A per-source override is caught in the unit it overrides.
2. **The compile line is read back.** Contraction is the one flag no preprocessor macro announces on clang or gcc, and a
   licence like `-fno-honor-nans` or `-ffp-model=fast` defines nothing either. So `felitronics_session_compile_line` reads
   this build's `compile_commands.json`, and `felitronics_session_consumer_compile_line` configures a consumer
   (`modules/session/tests/consumer/`, the parent above) and reads its (`modules/session/tests/compile-line-gate.cmake`).
   For every unit of the library and of the C boundary, the library's group is the **last word** on floating point,
   exceptions and RTTI: its tokens appear in order, and after their last appearance no token touches floating-point
   semantics (`-ffp-*`, `-fno-honor-*`, `-fapprox-func`, `-fdenormal-fp-math*`, `-fcx-*`, `-mfpmath*`,
   `-fexcess-precision*`, the fast-math family, `-Ofast`), exceptions or RTTI; no forced include, pass-through, plugin
   or flag file appears anywhere, separated or joined (`-include` and `-include<path>`, `--include=`, `-imacros…`,
   `-Wp,…`, every `-X…`, `-mllvm…`, `/FI…`, `/Yu…`, `-fplugin…`, `-specs=`, `--config…`, `-B…`); a response file
   (`@file`, which the emscripten toolchain uses for its include directories) is read, and its tokens are checked where it
   stands; every listed unit is compiled into the target and every unit the target compiles is listed. An entry is
   matched by its `file`, its object read from `output` or from the command's `-o` — CMake 3.22's Ninja and Makefile
   generators write no `output`. `felitronics_session_compile_line_controls` requires the gate to refuse two real
   per-source options — `-ffp-model=fast` and a joined `-include<absolute path>` (copies of a library unit with those
   source properties, configured and never built) — a `-fno-honor-nans` after the group, the group removed, a separated
   forced include and every joined spelling above, an unlisted unit in the target and a listed unit nothing compiles —
   and to pass the same compile commands without `output` fields and with `arguments` arrays. **Not on MSVC, with any generator**: cl.exe
   announces `/fp:precise`, `/fp:contract`, exceptions and RTTI to the preprocessor, so the guards of item 1 hold MSVC
   from inside each unit and there is no compile line to read.
3. **The library answers for itself, under hostile flags.** Its build probes (`src/FpProbes.h`: a fused multiply-add, a
   division turned into a reciprocal, a reassociated sum, a dropped signed zero) are compiled into the library and asked
   from a translation unit compiled with contraction forced on. The library's sources are also compiled again with
   `-ffp-contract=fast`, and with `-freciprocal-math -fassociative-math -fno-signed-zeros -fno-trapping-math` (MSVC
   `/fp:fast`), placed **ahead** of the target's options, and must answer IEEE-754 anyway — while the same probes compiled
   with the licence alone must change on that row, so the licence is live. A row with a fused multiply-add (every arm64
   row) requires the contraction controls to fuse; a row without one (baseline x86-64) says the check cannot fail there.
   MSVC has no way back from `/fp:contract` — a later `/fp:precise` selects the model and leaves contraction on — so on
   MSVC a `/fp:contract` ahead of the library's options is refused by the guard instead, and a test requires that.
4. **The build controls** (`modules/session/tests/controls/`): each forbidden construct — `try`, `throw`, `typeid`,
   `dynamic_cast` — compiled with the library's own compile options, clean in every build and planted in a build ctest
   requires to fail with that construct's diagnostic; and `src/BuildContract.cpp` compiled with `-ffast-math`,
   `-fexceptions` or `-frtti` (MSVC `/fp:fast`, `/EHsc`, `/GR`) appended after the library's options, which must fail on
   the guard's own message. One measured exception: with MSVC's Visual Studio generator an appended `/EHsc` becomes the
   project's exception-handling property, and the library's `/EHs-c- /EHa-` are placed after every property on the
   command line — the library's flags win, so that control requires the build to succeed with exceptions off. **On the wasm tier** the preset compiles everything with `-fno-exceptions -fno-rtti`, so the
   planted constructs fail there whatever the library's options say: that tier proves the preset, and the native rows
   prove the library.

In this repository's own builds the sources also compile under core's hygiene warning set with `-Werror`; a consumer's
build of the library does not get `-Werror`.

The library reports the releases it was built from — `Session::version()` (this repository) and `Session::coreVersion()`
(the felitronics-core it was compiled against) — answered by the compiled code, so they name the binary that runs.

## The config — two documents, compiled in, read by schema

Every number the session decides, measures, renders and reports with lives in two TOML documents of the module:
`config/targets.toml`, the targets — the loudness and ceiling a person picks, the physics of the medium that come with
them, the delivery format — and `config/engine.toml`, every other number: the input's reference and its quiet
thresholds, the landing's series, the devices' travels and rules, the observations' thresholds, what a master's cost is
measured with (as measured, without a verdict), the progress weights. What each number means and where it came from is
written beside it, as a comment; a number the owner decided says so. The sound depends on no default of the core's:
every stage a device writes is named, the limiter's second release included.

- **Compiled in, never read.** felitronics-toml (v0.2.0, resolved like core: a sibling checkout, or the pinned tag)
  compiles both documents into the library as constexpr data (`felitronics_toml_embed`); the session reads no file
  (law 6). A document the parser refuses stops the build at its line and column. `tools/wasm/build.sh` embeds them the
  same way for `fcsession`, with felitronics-toml's own tool run through node.
- **Read by schema: form and physics.** `Config::load()` (`<felitronics/session/Config.h>`) binds them to typed structs:
  every key with its type and its domain — where a number stops meaning what its document says: a share outside 0…1, a
  ramp whose ends would divide by zero, a series that shrinks, a value off its knob's grid (a whole number of steps from
  where the travel starts, checked exactly on the decimals as written, across the two documents too) — a range another
  key states included (a target's loudness on the edit travel, its crossover on the knob's); the checks across keys (a
  name that is no target, a name given twice, an EQ band two devices share, "no DC" apart from the dcOffset finding's
  threshold, a ramp law outside its domain, the limiter switched off, a default written out); and every key nobody read
  reported as unknown. What an analyzer admits is the analyzer's to say: the blocks the config feeds one — the low end
  (both runs), the crest, the sibilance-band bursts — go to that analyzer's own `storageFor()` at the source rates the
  product accepts (8, 44.1, 48 and 96 kHz; the bursts from 44.1, since a 9 kHz corner needs the room) and are refused
  whole where it refuses. A problem is data: the document, the fault, the key path, the line and the column.
- **The build holds the schema — every build.** `felitronics_session_config_check`, a host tool compiled from the
  library's own `src/ConfigSchema.cpp` (so it does not link the library it gates), reads the two documents before
  `felitronics::session` is built — in a consumer's build and in a build without tests too, under the emulator where the
  build cross-compiles (node, on the wasm tier) — and a problem stops the build as
  `<file>:<line>:<column>: error: <fault> <key path>`; its stamp is written only on success, so a failed gate runs
  again. The stamp depends on the documents, the schema's sources and the checker — the built-in target, or a supplied
  `FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE` as a file, so a newer checker or another path runs the gate again (a
  supplied checker is its supplier's to keep built from the same sources). `tools/wasm/build.sh` runs the same gate before it links `fcsession`. Six controls plant a mistake in a copy of
  the documents and require the gate to go red at that spot, after it passed the copy without the plant; the config
  suite plants over sixty more in-process, every input a review found the schema accepting among them.
- **The owner's decisions are held apart** (`tests/ConfigDecisionsTests.cpp`): every target row field by field and the
  engine's decided numbers — the landing's series, the high-pass knob's travel and slopes and comfort window, the
  wide-bass warning, the quiet thresholds, the peak clipper's classes, the glue knob and its default of none, the
  mono-bass block, the delivery rates, and the rest. The schema would admit another number where the physics allows;
  this suite says which number was decided, so changing one is a deliberate edit of it. Its controls plant departures
  the schema admits (a high-pass top of 51 or 60 Hz, a slope of 36, another series, another target number or rate, glue
  by default, a wider mono bass) and require them named.
- **Its versions** (`Config::versions()`): 64-bit FNV-1a hashes of both documents' NORMALISED data — every number as the
  bits of its correctly rounded double (−0 as +0), every table walked in the byte order of its keys, order kept in
  arrays and in the rows of `[targets]` (for `all`: the order a shell lists them in) — so a number's spelling, the order
  keys are written in, inline-or-not, comments and spacing move nothing. `all` covers every key; `sound` is what can
  change a master, and keeps a key when it is not sure: it leaves out only what is shown (the main list and the rows'
  order, the edit's travels, the red and comfort zones, the curve scales, marks and zones), what prints a finding or a
  warning without switching a device (every observation threshold but `observations.polarity`; the peak clipper's
  density figures), what is measured after the master (the crest, the cost), development (the progress weights, the
  blind test), the name of the defaults, and — while no shell offers the de-esser — its block and the bursts only it
  reads. The default target stays in. `sound` is what a recipe will record. They walk the
  embedded data and allocate nothing, so the C ABI answers the config's `all` (`fc_session_config_version`) with no
  demand to declare. The suite changes every value of both documents, one at a time — through their text and through the
  embedded data — and requires `all` to move each time to a value of its own, and `sound` to move exactly for the values
  that can change a master; the decisions suite pins `sound` to the name of the defaults, so a sound number changed
  without new defaults is red. `fcore_session config version|sound-version`, the source files and the wasm module must
  answer the same numbers (ctest, and CI's artifact check).
- **Read in place by the commands.** What the session's commands check against — the knobs' travels and steps, the
  targets' rows, the numbers a device starts from — is read straight from the embedded documents, as the decimals
  written (`src/Rules.h`): no table is built and nothing is allocated, so reading the config adds nothing to a command's
  demand. Every number read there is one the schema required and checked before the library was built;
  `felitronics_session_state_tests` holds every one of them to the schema's binding of the same documents, row by row
  and knob by knob, and reads documents with the keys missing to see the reading say it is incomplete — which
  `create()` answers with `Status::Config`, not reachable in a library whose build ran the gate.
- **Only in words, for now: the config's memory.** `Config::load()` allocates and publishes no demand; the session does
  not call it.
- **Only in words: the golden pin is append-only.** A new set of sound numbers is a new name in `defaults` and a new
  line in the decisions suite's table, and the line of an old name is never rewritten — a project names its defaults,
  and two sets of numbers under one name would reopen it as another master. The suite holds the current name to its
  sound version; that an old line was not overwritten is held by review alone.

## The states and the commands

`<felitronics/session/Commands.h>` and `<felitronics/session/Project.h>`. A session is in one of four states — **Empty**
(nothing loaded), **Loaded** (a source, its first measurement running, the devices not placed), **Measured1** (the first
measurement ended: the devices are placed, a master can be made), **Measured2** (the second ended too) — and a master
being made is an overlay on the two measured ones. A shell asks by a `Request`, one struct per command with the shell's
own id for it: `load`, `setTarget`, `editTarget`, `editDevice`, `revertEdits`, `setManual`, `master`, `cancel`, `forget`.
`Session::apply()` answers it whole — accepted, with the revision it made, or rejected with a `Rejection` code, having
changed nothing. Every accepted command and every transition moves the revision by one; a rejection leaves it.

**Who may do what, when, is one table in code** (`Table` in `Commands.h`), and every command consults it before
anything else: in each column, `yes` where the command is taken, or the rejection it gets. Mastering1 and Mastering2
are a master being made on Measured1 and on Measured2. The endings of the work are the session's own transitions, not
commands: the work that measures and renders drives them (`src/Driver.h`, the library's internal seam, named the
session's friend and not public), and they happen only where their row says so. `fcore_session table` prints both tables
from the code, and ctest holds the text between the markers below to that output byte for byte.

<!-- the table: begin -->
| command | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 |
|---|---|---|---|---|---|---|
| load | yes | yes | yes | yes | yes | yes |
| setTarget | yes | yes | yes | yes | yes | yes |
| editTarget | yes | yes | yes | yes | yes | yes |
| editDevice | NoSource | NotPlaced | yes | yes | yes | yes |
| revertEdits | NoSource | NotPlaced | yes | yes | yes | yes |
| setManual | yes | yes | yes | yes | yes | yes |
| master | NoSource | NotMeasured | yes | yes | Busy | Busy |
| cancel | NoJob | NoJob | NoJob | NoJob | yes | yes |
| forget | NoSource | NoMaster | yes | yes | yes | yes |

| the session's own transition | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 |
|---|---|---|---|---|---|---|
| the first measurement ends | no | yes | no | no | no | no |
| the second measurement ends | no | no | yes | no | yes | no |
| the master is done | no | no | no | no | yes | yes |
<!-- the table: end -->

**The checks run in one declared order, the same for every command**, and the first that fails is the answer:
1. the calling thread's floating-point environment; 2. the table; 3. the manual mode, for the device panel's commands
(`editDevice`, `revertEdits`); 4. what the command names — a target, a device offered for this target and source, the
master being made, a master kept; 5. the fields — at least one touched, then each touched one in the order its struct
writes them: finite, one of its values, on its travel, on its step; 6. a load's audio — one or two channels, a rate of
at least felitronics-core's 8000 Hz, frames and data, a size the machine can address, every sample finite. A rejection
on a field names it by its place in its struct. `Session::check()` runs exactly these and says what the command would
answer; `apply()` runs `check()` first and does the work only when it passed, so a rejected command has changed nothing
by construction — `check()` is `const`.

**The project** (`Project.h`) is the target — a row of `[targets]` — with a person's edits of its two numbers (loudness
and ceiling, on `[edit]`'s travels), the manual mode, and every device's parameters. **One parameter form per device**:
a device's fields are written once, as a template over the form a field takes, and used as the machine's layer (every
field a value), a person's layer (a field a value only where touched — a touched field is the person's even where its
number is the machine's) and a revert's mask (a field yes or no). No string names a field anywhere: an edit is the
device's struct, a variant whose alternative is the device. The devices are the high-pass, mono bass, the glue,
saturation, tilt, the limiter's needles, the dither and the low shelf; the low shelf is offered on a target that
carries one, the dither where the target's bit depth is one it serves, mono bass except on a mono source.

- **The machine's layer** is placed from the config for the target and the source — the ticks from `[stages]`, what
  the target decides (the high-pass's slope and floor, the mono-bass crossover, no needles where the target has no peak
  clipper, the dither at its bit depth, the low shelf's gain, the glue its row names) and each device's own section for
  the rest; tilt starts flat. The devices are placed so when the first measurement ends, and again on a change of target.
- **A person's edits** are taken only after that — before it, `NotPlaced` — and only with the manual mode on. Every
  value is checked on its knob exactly: the double a shell sends is read as the decimal of nine places or fewer whose
  correctly rounded double it is, and that decimal must lie on the travel and a whole number of steps from where the
  travel starts — by the same code that holds the config's own numbers to their grids (`src/Grid.h`). A double that is
  the decimal of no such number is off the step.
- **`setTarget(name, onEdits)`** replaces the target's numbers silently — a person's edits of them go with the old
  target — and keeps or takes back a person's device edits as `onEdits` says; an edit of a device the new target does
  not offer goes either way. The machine's layer is placed again for the new target.
- **`setManual(false)`** takes back a person's device edits and nothing else: the machine's layer stays, and so does an
  edit of the target's numbers.
- **`load`** checks everything first, then DISARMS — whatever ran on the old source stops, and the old source, its
  measurements and its masters go; the manual mode is switched off, and a person's device edits go with it (the mode
  does not outlive the file: its edits were decisions about the old source); the old samples are freed before the new
  are asked for — and then WRITES the new source: its samples, channel after channel, its name and its hash (64-bit
  FNV-1a of its rate, channels, frames and every sample's bits). The target and its edited numbers stay.
- **`master`** captures the recipe — the project as it is, the source's hash, the config's sound version — and starts a
  job; the project may change meanwhile, and the master renders its recipe. When it is done the session keeps it under
  its job's id; `cancel(job)` ends the overlay, `forget(master)` lets a kept master go.

**Memory.** `check()` says, before the work, what a command will ask the heap for, by the expressions that size its
requests: a load its samples and its name, a master room for one more master kept (so that the render's end asks for
nothing), every other command nothing. `felitronics_session_state_tests` holds every command to it through the
allocation counter — exactly, since each is one exact request.

**No text.** A rejection is a `Rejection` code, and a field its place in its struct; the values are stable, and a new
reason is a new value at the end. What a person reads is written from the code by a shell's catalogue.

## The floating-point environment

The flags decide what the compiler emits. The thread decides what the arithmetic does: a host may have set flush-to-zero
or denormals-are-zero for its audio thread, or changed the rounding mode, and no flag reaches that.
`Session::checkFloatingPointEnvironment()` reads it with ordinary arithmetic — no `<cfenv>` — and answers
`Status::FloatingPointEnvironment` for a thread that flushes subnormal results, reads subnormal inputs as zero, or rounds
other than to nearest. Every call that computes asks it on entry: `create()` refuses such a thread having allocated
nothing, a command is rejected with `Rejection::FloatingPointEnvironment` before any other check, and a transition does
not happen. Nothing in the library changes the environment: the caller restores it, or calls from another thread.
`felitronics_session_tests` sets each condition the way a host would (MXCSR, FPCR, `fesetround`), confirms it took
effect, and requires the refusal, and `felitronics_session_state_tests` requires it of a command and a transition; a row
with no such control (the wasm tier) says the check is not reachable there.

Outside the check: the **floating-point exception masks and flags** (a host that unmasks a trap gets its trap), and the
**sign and payload of a NaN**, which differ between rows and which core's law 10 leaves unspecified — nothing in the
session compares or prints one.

## What the flags do not reach — header-inline code

An inline function — a `std::` template, a felitronics-core header function — is compiled into every translation unit
that uses it out of line, each copy under that unit's flags, and the linker keeps **one** copy for the program. So the
library's objects may run the application's copy of a function they share, compiled under the application's flags, and
nothing inside the library can tell which copy the linker kept. No check here pretends to: a runtime canary would prove
the kept copy of itself and nothing else, and no consumer could ever reach its red state — so there is none. The
requirement is stated instead:

- **A program that links `felitronics::session` compiles EVERY translation unit with the session's FP flags: no
  contraction and no fast-math** — `-fno-fast-math -ffp-contract=off` on gcc and clang (`modules/session/build-flags.txt`),
  `/fp:precise` without `/fp:contract` on MSVC. That is stricter than felitronics-core's policy
  (`cmake/FelitronicsPolicy.cmake` states `-ffp-contract=on`, which lets a compiler fuse within an expression): a shared
  inline helper fused in the application's copy answers differently from the library's own.
- **No partial linking** (`ld -r`, or any step that merges the library's objects with others before the final link):
  it folds inline copies where nothing here looks.
- **The wasm modules are not affected.** This repository builds each one whole — every translation unit, the C
  boundary's included, on one command line with the session's flags (`tools/wasm/build.sh`).
- What keeps the exposure small: the session's arithmetic lives in its own translation units — its public headers carry
  no function body and define no variable that is not constexpr (the source lint's BODY and PUBLIC rules) — and the multiply-adds it shares
  through felitronics-core are pinned (`core::det::mulAdd` and `mul` round through a volatile, which no copy's flags can
  fuse).

## The object-file gate

"No mutable state outside an object" and "no operating system, file, console, locale, clock or process state" are
questions the compiled object answers exactly, so they are asked of it (`modules/session/tests/object-gates.cmake`, on
every native row and the wasm tier, with the row's own reader — `readelf` on ELF, `objdump` on Mach-O, `dumpbin` on COFF,
`llvm-readobj` on wasm):

- **Writable memory.** Every symbol in writable memory is state with static storage duration — and writable is what the
  object **says** of the section, never a list of writable names, which a section attribute walks past: on ELF the
  section's alloc and write flags, on COFF its `IMAGE_SCN_MEM_WRITE`; Mach-O and wasm carry no write flag per section, so
  there the read-only places are the list — Mach-O `__TEXT,*`, `__DATA_CONST,*` and `__DATA,__const`, wasm `.rodata*` and
  `.data.rel.ro*` — and every other section of a data symbol is writable. Relocated constant data is not state, and
  passes: ELF `.data.rel.ro*` (flagged writable because the loader writes it before RELRO protects it, so there the name
  is trusted — and the source lint refuses every section attribute, so session code cannot choose that name), Mach-O
  `__DATA,__const` and `__DATA_CONST`, COFF `.rdata`, wasm `.rodata` and `.data.rel.ro` — a constexpr table of pointers
  lives there (a control built position-independent requires the gate to accept one). The library's objects may hold
  none; the C boundary's may hold exactly its handle table and its poison flag, named in `tools/lint/session-objects.txt`
  and each required to be found once — an allowance that names a symbol no longer there is refused as rot.
- **What is called.** Every undefined symbol must be defined — with global or weak binding — by another object of the
  set, or match a line of `tools/lint/session-objects.txt`: operator new and delete, what the compiler emits for a copy,
  the stack protector, the toolchain's own markers, and the sanitizer runtime on the sanitizer row. A **local**
  definition answers no other object's call — the linker never resolves one with it — so a `static getpid` in one object
  leaves another object's `getpid` call refused. `printf`, `fopen`, `time`, `getenv`, `strtod`, `isalpha`, `rand` — or
  `std::to_string` taken by pointer — are refused whatever header declared them.

Its controls (`modules/session/tests/object-controls/`) compile each shape the reviews found — a direct-initialised
global a declaration reader took for a function, a static inside a lambda, a global planted by a macro, an asm-labelled
global, class and function-local statics, an array of `const char*`, thread-local state, a const object with a start-up
constructor, a global in a section of its own naming (on ELF also one in a section *named* `.rodata.*` that the object
marks writable), the forbidden calls, a function taken by pointer, the `::rand` / `std ::stod` spellings, a call
"answered" by a local definition in another object; and, for the C boundary, the shipped facade with a third global
planted beside its two, and with an allowed global renamed. Each must be refused, and the refusal must name the planted
symbol **whole** — its raw or demangled name, or the last identifier of it — not a fragment of some other name.

What the gate cannot see: code that is never emitted — among it a variable only a consumer's translation unit emits,
which is why the public headers may define none — and facilities that leave no symbol — an atomic, a clock read by an
intrinsic or by inline assembly, a pragma. Those are the source lint's.

## The source lint

`tools/lint/check-session-laws.mjs`, run from the repository root (`--build <dir>` cross-checks the targets' sources
against that build's `compile_commands.json`, with or without `output` fields). It scans every file of `modules/session`
from the target's list (`modules/session/sources.txt`, which CMake and `tools/wasm/build.sh` read too), the C boundary
compiled with the session's flags (`tools/wasm/fc_session.cpp` and `tools/fc_session_abi.h`), and the `#include` closure
of those units, whatever the files are called; it refuses a file of unknown type, a unit the list does not name, and a
file nothing compiles or includes; the config's two documents are data, named as such. Only `modules/session/tests` is
outside it. `--build` also names the felitronics-core and felitronics-toml checkouts the lint resolves admitted headers in
(or their sibling checkouts).

Before any rule the text goes through **translation phase 2** — every backslash-newline joined, a line map kept — and
the lexer consumes identifiers and preprocessing numbers whole, so a keyword split across a continuation is read whole
and `u8'0'` is a character literal, not a digit separator. Its rules:

- **includes** on an allowlist: canonically spelled standard headers that reach no OS, file, locale, thread, clock or
  process state; felitronics headers **by name** (`FELITRONICS_ALLOWED`: core's `FlushToZero.h` sets flush-to-zero with
  no symbol and several core headers pull in `<atomic>`, so each is a reviewed one-line addition — today the module's
  own `Session.h`, `Config.h`, `Commands.h` and `Project.h`, felitronics-toml's `Toml.h`, `Schema.h` and `Embedded.h`, and the three analyzers the
  config's schema asks what they admit, `LowEnd.h`, `BandCrest.h` and `StereoBandBursts.h`, which bring core's DSP and
  `FlushToZero.h` with it; the schema calls only their `storageFor()`); quoted headers inside the module, and in
  `src/Config.cpp` the two headers the build generates from the config, by name;
- **no macros** — no `#define`, `#undef` or `##` — and **no directive** but `#include` and `#pragma once`;
  `src/BuildGuards.h` and `src/BuildContract.cpp` may carry `#if` / `#error` logic and nothing that defines a macro;
- no pragma but `#pragma once`, no `_Pragma` / `__pragma`;
- **attributes on an allowlist** — `nodiscard`, `maybe_unused`, `likely`, `unlikely`, `noreturn`, `fallthrough`, in
  `[[ ]]` with no namespace, every entry of a list read and `__x__` spellings read as `x`; no `__attribute__`,
  `__declspec`, `[[gnu::…]]`, `[[clang::…]]`, `[[using …:]]`, and by name no `section`, `allocate`, `data_seg`,
  `bss_seg`, `const_seg`;
- no alternative tokens (`<:`, `%:` …);
- no `mutable`, in any file: a mutable member is state that changes inside a `const` or `constexpr` object —
  `struct S { mutable int n = 0; }; inline constexpr S s {};` in a public header is one shared, changing variable in
  every consumer, which the `constexpr` rule below would pass — and the session has no use for it (nor for a mutable
  lambda);
- no exception or RTTI token in any `#if` branch; no atomics, cycle counters, target intrinsics or inline assembly, and
  no FP control register touched by hand (`ScopedFlushToZero`, `_mm_setcsr` / `_mm_getcsr`, every `_MM_SET_*` /
  `_MM_GET_*` macro: rounding mode, exception mask and state, flush-to-zero, denormals-are-zero) — what an admitted
  header brings cannot be used;
- no `std::unordered_*`, no `hash<` qualified or not, no unstable sort, no `using namespace`;
- in the public headers, no function body, no variable with static storage duration that is not `constexpr` (at
  namespace scope or as a static member, `inline` or not), and no namespace-scope function declaration — at namespace
  scope `T name (x);` is a function or a variable depending on what `x` is, which no lexer can tell, so a free function is
  a static member or a friend;
- `"BuildGuards.h"` first in every unit; every scanned file in the det-math zone.

**The C boundary's allowance** is stated in the lint by name: its `FC_EXPORT` macro (which `tools/wasm/build.sh`'s export
scanner reads), its one `#if defined(__EMSCRIPTEN__)` branch and the two emscripten headers it includes, its two quoted
includes; the ABI header's include guard, integer constants and C++ linkage block. Anything past that is refused as in
the module. Its controls (`tools/lint/session-controls/run.sh`) plant each rule's violation in the real tree — the
module's files and the boundary's — and require the lint to fail on the planted file and line; they also edit this
build's `compile_commands.json` to plant an unlisted unit and drop a listed one.

## The C boundary — `fc_session`

`tools/fc_session_abi.h`, implemented by `tools/wasm/fc_session.cpp`. Its version is **0, a draft**: no promise — any
entry point, argument, code or constant may change without a bump. It carries the ABI version, a session created and
destroyed through a handle, the config's version (`all`), the session's refusals of `create()` passed through as status
codes (`FC_SESSION_ERR_FP_ENVIRONMENT`, `FC_SESSION_ERR_CONFIG`), and the poison. It carries no command: the states
and the commands are the library's C++ surface. It follows
fc_master's law — the facade is thin: handles instead of pointers, a status per call, checks on the addresses a page
computed, the poison, and nothing that decides. It is compiled with the session library's own options and definitions,
natively and in the wasm module, includes `src/BuildGuards.h` first, and is under the source lint with the allowance
above.

- **Two globals, the only ones session has**: the handle table and the poison flag — a handle must name a session between
  two calls, and the poison must outlive the call that never returned. Both are trivially destructible, so no exit-time
  destructor runs. The object-file gate refuses a third.
- **Handles retire, they do not wrap.** A handle is a slot and that slot's 24-bit generation. A generation that wrapped
  would give an old handle's number to a new session, and the stale handle would destroy it (reproduced on the module:
  16 777 214 create/destroy cycles, a fraction of a second). So a slot issues `FC_SESSION_SLOT_GENERATIONS` handles and
  then retires; after `FC_SESSION_MAX_HANDLES × FC_SESSION_SLOT_GENERATIONS` creates a module instance answers
  `FC_SESSION_ERR_EXHAUSTED` for good. The table's capacity is **8** — fixed, compiled in, not configurable. The native suite and `tools/wasm/session-check.mjs` walk one slot through all of
  its generations.
- **The poison**: an entry point that finds a call still in progress — an earlier one never returned (the wasm module
  aborted inside it), or it was re-entered from inside an allocation — answers `FC_SESSION_ERR_POISONED`, for good.

`fcsession` is its wasm module (`tools/wasm/build.sh`): the facade and the sources `modules/session/sources.txt` lists
(the build refuses a `.cpp` under `modules/session/src`, at any depth, that is not listed), linked with
`--wrap=pthread_create`, with its embedded config — 61 KB of wasm, 18 KB brotli: the config's data, and the states and the commands over it. `tools/wasm/session-check.mjs` compares every export of the
loaded module against the ABI's surface and the runtime's own, runs the surface, and walks the wrap boundary; `build.sh`
builds a control copy with one undeclared export and requires the check to refuse it.

## The native CLI — `fcore_session`

`fcore_session version` prints the two releases and the ABI version; `fcore_session config targets|engine` prints a
document of the embedded config through felitronics-toml's canonical writer (its numbers, without the comments), and
`fcore_session config version|sound-version` its versions; `fcore_session table` prints who may do what, when — the
tables of `Commands.h`, as Markdown, which ctest holds to the block of this document; `fcore_session run <script>` reads
a command script (`-` is stdin) into a fresh session and prints `done <commands>`. A script carries no command: one with
none in it — empty, or comments and blank lines — answers `done 0`; a script with a command in it, and a session that
refuses to be created, are refused with exit status 2 and nothing on stdout. It links the library as C++, the way a
desktop application does.
