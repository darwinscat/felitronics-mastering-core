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

The session runs no audio thread; its offline sample loops advance only when called. It is called synchronously, with its input as arguments, and answers
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
| **8a**, **11a** the sample clock | yes, through its analyzers | streaming advances by source frames on a fixed grid; pauses consume no samples, and native analyzer comparisons hold chunk invariance |
| **9** no `long double` | yes | core's long-double lint reads every `modules/*/include` and `modules/*/src`, this module's included, and the wasm tier's artifact gate reads every emitted object |
| **10** FP contraction is stated | yes — **as `off`** | the target's own flags in one `SHELL:` group, the compile line read back (this build's and a consumer's), the hostile-flags tests, the library's probes asked from a contracting caller, and the source lint's pragma and attribute rules. Core states `on` for its tree; the session's numbers are compared across rows, native and wasm, and baseline wasm has no fused multiply-add, so a contracting native build would disagree with the module. The library's flags reach its own objects only: a program that links it compiles **every** translation unit with the same FP flags (below, "What the flags do not reach"). The sign and payload of a NaN, and the floating-point exception masks and flags, are outside every check here, as core's law 10 leaves them |
| **11**, **11b** a request that cannot be honoured is refused whole; checks in a fixed order | yes | `create()` checks the floating-point environment and the config it reads, then allocates: a refused create requested nothing (`felitronics_session_tests`). Every command runs the checks `Commands.h` declares, in their order — the thread's floating-point environment, then the table — before changing session state. Import adds the ordered document checks below. A rejection publishes its event and advances `seq`; the session’s state and revision do not change. A `load` runs its checks, then disarms, then writes (check → disarm → write), so a rejected load, too, leaves state and revision unchanged while publishing its rejection: `felitronics_session_state_tests` compares the whole session before and after every rejection it produces, produces every rejection code, and holds the order with requests wrong in several ways. The C boundary's checks run in its header's order and a refused call writes nothing and allocates nothing (`felitronics_session_abi_tests`). `fcore_session` reads and checks a whole script before it creates a session |
| **11d** memory is declared before the work | yes | `Session::createBytes()` is the demand of `create()`, counted by the expression that sizes the request, and `Session::check()` gives the demand of every command before it runs — computed by the same function `apply()` runs first; the declared-budget harness (`tests/DeclaredBudget.h`, on core's one allocation counter) holds `create()` and every command to *declared ≥ requested* (exactly equal, where the request is one exact allocation), holds `check()`, typed-command refusals and every transition to nothing requested; import parsing and its refusals are covered by the library storage declaration in `felitronics_session_project_tests`, and is itself shown to fail on a sample that under-declares. The event suite also holds `stepBytes()`, `snapshotBytes()`, snapshot copy, and codec size queries and work to their declared demands (below). The demand is checked in C++ and `fc_session_create_bytes` publishes creation demand before creation. The shell supplies a heap ceiling; live declared bytes plus each allocating command's demand must fit before work begins. The C boundary adds nothing to it (its table is static) and keeps the poison |

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

- gcc, clang, emscripten: `modules/session/build-flags.txt` — `-fno-fast-math -ffp-contract=off -fno-exceptions -fno-rtti -Werror=switch`,
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
thresholds, the landing's twelve-pass budget, the devices' travels and rules, the observations' thresholds, what a master's cost is
measured with (as measured, without a verdict), the progress weights. What each number means and where it came from is
written beside it, as a comment; a number the owner decided says so. The sound depends on no default of the core's:
every stage a device writes is named, the limiter's second release included.

- **Compiled in, never read.** felitronics-toml (v0.3.0, resolved like core: a sibling checkout, or the pinned tag)
  compiles both documents into the library as constexpr data (`felitronics_toml_embed`); the session reads no file
  (law 6). A document the parser refuses stops the build at its line and column. `tools/wasm/build.sh` embeds them the
  same way for `fcsession`, with felitronics-toml's own tool run through node.
- **Read by schema: form and physics.** `Config::load()` (`<felitronics/session/Config.h>`) binds them to typed structs:
  every key with its type and its domain — where a number stops meaning what its document says: a share outside 0…1, a
  ramp whose ends would divide by zero, a series that shrinks, slider travel outside its domain, or a default outside
  its domain; the checks across keys (a
  name that is no target, a target under an empty key, a name given twice, an EQ band two devices share, "no DC" apart from the dcOffset finding's
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
  engine's decided numbers — the landing's one budget, tolerance and true-peak aim, the high-pass knob's travel (15…80 Hz) and the machine's 50 Hz top apart, its slopes and comfort window, the
  wide-bass warning, the quiet thresholds, the peak clipper's classes, the glue slider (0…3 dB, step 0.1; accepted domain 0…6 dB) with its default of none, 0.5 dB when ticked and 2.6 dB on cd, the mono-bass block, the delivery rates, and the rest. The schema would admit another number where the physics allows;
  this suite says which number was decided, so changing one is a deliberate edit of it. Its controls plant departures
  the schema admits (a machine high-pass top of 51 or 80 Hz, a knob travel to 50 or 81 Hz, a slope of 36, another series, another target number or rate, glue
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
  reads. The default target stays in. `sound` is what a recipe records. The build gate hashes the config into constants shared by the compiled library and generated page declarations; queries allocate nothing, so the C ABI answers the config's `all` (`fc_session_config_version`) with no
  demand to declare. The suite changes every value of both documents, one at a time — through their text and through the
  embedded data — and requires `all` to move each time to a value of its own, and `sound` to move exactly for the values
  that can change a master; the decisions suite pins `sound` to the name of the defaults, so a sound number changed
  without new defaults is red. `fcore_session config version|sound-version`, the source files and the wasm module must
  answer the same numbers (ctest, and CI's artifact check).
- **Read in place by the commands.** Domains, slider hints, target rows and defaults are read from the build-checked
  embedded documents without allocation. The state suite compares them with the typed schema binding. Broken required
  lookups trap as contract failures; session creation does not revalidate the config.
- **Only in words, for now: the config's memory.** `Config::load()` allocates and publishes no demand; the session does
  not call it.
- **Only in words: the golden pin is append-only once released.** A new set of sound numbers is a new name in
  `defaults` and a new line in the decisions suite's table, and the line of defaults that a release carries is never
  rewritten — a project names its defaults, and two sets of numbers under one name would reopen it as another master.
  Before the first release that carries a name, its line may be updated in place: no project can name defaults no
  release shipped (no release tag carries `2026-09`). The suite holds the current name to its sound version; that a
  released line was not overwritten is held by review alone.

## The states and the commands

`<felitronics/session/Commands.h>` and `<felitronics/session/Project.h>`. A session is in one of five states — **Empty**
(nothing loaded), **Loaded** (a source, its first measurement not completed, the devices not placed), **Measured1** (the first
measurement — the programme's loudness and true peak — ended: the planner places the devices, a person may edit them and a
master can be made; what the devices still read ends later, see "The plan of the devices"), **Measured2** (the second ended too), **MeasurementStopped** (audio, results, and unfinished work retained) — and a master
being made is an overlay on the two measured ones. A shell asks by a `Request`, one struct per command with the shell's
own id for it: `load`, `setTarget`, `editTarget`, `editDevice`, `revertEdits`, `setManual`, `master`, `cancel`, `forget`, `importProject`, `continueMeasurement`, `adoptMachine`.
`Session::apply()` answers it whole — accepted, with the revision it made, or rejected with a `Rejection` code and no state change. A rejection publishes an event and advances `seq`. Every accepted command and every transition moves the revision by one; a rejection leaves it.

**Who may do what, when, is one table in code** (`Table` in `Commands.h`), and every command consults it right after
the floating-point entry check, before anything else: in each column, `yes` where the command is taken, or the rejection it gets. Mastering1 and Mastering2
are a master being made on Measured1 and on Measured2. The endings of the work are the session's own transitions, not
commands: the work that measures and renders drives them (`src/Driver.h`, the library's internal seam, named the
session's friend and not public), and they happen only where their row says so. `fcore_session table` prints both tables
from the code, and ctest holds the text between the markers below to that output byte for byte.

<!-- the table: begin -->
| command | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 | Stopped | StoppedMeasured | MasteringStopped | Measured1Unplaced | Measured2Unplaced | Mastering1Unplaced | Mastering2Unplaced | StoppedMeasuredUnplaced | MasteringStoppedUnplaced |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| load | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| setTarget | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| editTarget | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| editDevice | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes | yes | yes | yes | yes | yes | yes |
| revertEdits | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes | yes | yes | yes | yes | yes | yes |
| setManual | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| master | NoSource | NotMeasured | yes | yes | Busy | Busy | NotMeasured | yes | Busy | yes | yes | Busy | Busy | yes | Busy |
| cancel | NoJob | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| forget | NoSource | NoMaster | yes | yes | yes | yes | NoMaster | yes | yes | yes | yes | yes | yes | yes | yes |
| importProject | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes | NotPlaced | NotPlaced | NotPlaced | NotPlaced | NotPlaced | NotPlaced |
| continueMeasurement | NoJob | NoJob | NoJob | NoJob | NoJob | NoJob | yes | yes | yes | NoJob | NoJob | NoJob | NoJob | yes | yes |
| adoptMachine | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes | NotPlaced | NotPlaced | NotPlaced | NotPlaced | NotPlaced | NotPlaced |

| the session's own transition | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 | Stopped | StoppedMeasured | MasteringStopped | Measured1Unplaced | Measured2Unplaced | Mastering1Unplaced | Mastering2Unplaced | StoppedMeasuredUnplaced | MasteringStoppedUnplaced |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the first measurement ends | no | yes | no | no | no | no | no | no | no | no | no | no | no | no | no |
| the second measurement ends | no | no | yes | no | yes | no | no | no | no | yes | no | yes | no | no | no |
| the master is done | no | no | no | no | yes | yes | no | no | yes | no | no | yes | yes | no | yes |
<!-- the table: end -->

**The checks run in one declared order, the same for every command**, and the first that fails is the answer:
1. the calling thread's floating-point environment; 2. the table; 3. what the command names — a target, a device offered for this target and source,
the source's audio (`master`: `NoAudio` for a sidecar source before `attachAudio`), with the panel open a plan with nothing left to wait for (`master`
the session decides: `PlanPending`), no device field not measured yet (`importProject`: `PlanPending`; the file's own
target is asked after its name is read, and an export answers the same), an id left for a new job (`load` and `master`), a load's valid
UTF-8 name, the active job (`cancel`: `NoJob` when none runs, `UnknownJob` when another does), a master kept; 4. the fields — each touched one in the order its struct
writes them: finite, one of its values, within its domain; 5. a load's audio — one or two channels, a rate of
at least felitronics-core's 8000 Hz, frames and data, a size the machine can address, every sample finite. A rejection
on a field names it by its place in its struct. `Session::check()` runs exactly these and says what the command would
answer; `apply()` runs `check()` first and does the work only when it passed, so a rejected command changes no state
by construction — `check()` is `const`.

**The project** (`Project.h`) is the target — a row of `[targets]` — with a person's edits of its two numbers (loudness
and ceiling, within `[edit]`'s domains), the manual mode, and every device's parameters. **One parameter form per device**:
a device's fields are written once, as a template over the form a field takes, and used as the machine's layer (every
field a value), a person's layer (a field a value only where touched — a touched field is the person's even where its
number is the machine's) and a revert's mask (a field yes or no). Commands name fields through typed structs: an edit is the
device's struct, a variant whose alternative is the device. TOML keys are bound at the serialization boundary. The devices are the high-pass, mono bass, the glue (its
knob, "up to N dB", as the config writes every glue number),
saturation, tilt, the limiter's needles, dither, low and the EQ bands. Tilt and low are separate devices on every target.
The machine enables low only for a target with `lowDb` (today `lp`, +0.5 dB); otherwise low starts off at 0 dB.
The EQ bands (device 8, `[bands]`) are five static bands of the EQ stage — body (bell, 160 Hz), mud (bell, 300 Hz, a cut
only), forward (bell, 3 kHz), brightness (high shelf, 8 kHz) and air (high shelf, 12 kHz), each its own band of the stage
(3–7) — and a person's only: the machine leaves every band at 0 dB, a band at 0 dB is no band (its slot as the stage held
it before the device). Its tick (`on`, field 5, appended after the gains) takes the whole device in or out with the gains
kept: the machine's layer is always on (even where the shell does not offer the bands), a person may untick it, and off
writes all five slots as no band — the stage, the curve and the master of a project without the bands, bit for bit. The
plan says the device sounds where it is on and any band is not 0. No dynamics this release. A band's knob is coloured as tilt's and low's are: `fc_kit_heat` reads its `[bands.*] normal` (±1.5 dB, the mud band −1.5…0) out to its travel — a hint, not sound (owner, 02.10). The curve's `[eq] curve.warnDb` judges the shelves of tilt and low only
(`felitronics_session_eq_bands_tests`).
Dither is offered through 16 bits, and mono bass except on a mono source; the shell may exclude any device.

- **The machine's layer** is the planner's (below, "The plan of the devices"): each device proposes its fields from
  the config for the target and the source — what the target decides (the high-pass's slope
  and floor, the mono-bass crossover, no needles where the target has no peak clipper, the dither at its bit depth, the
  low shelf's gain, the glue its row names) and each device's own section for the rest; tilt starts off and flat — and from
  what it measures. The devices are placed when the first measurement ends — the loudness and the true peak (owner,
  02.10: «Master is available as soon as LUFS and TP are ready») — by the pump, and by a sidecar's facts — and again on a
  change of target after that, and whenever a source measurement ends (the fields it decides); before it they are
  unplaced — every field of the machine's layer at its type's zero, and the state says so. A load unplaces them again.
- **A field the machine has not measured yet** — the high-pass's cutoff before the low end, mono bass's tick before the
  low-end run at the target's crossover — holds a placeholder (the target's floor, off) and its bit in
  `plan.devices.<device>.pending`, the device's `heldBack` is `Pending`, and a plan that waits states the card's line
  `deviceUnmeasured` (what it waits for). A shell draws such a field as "not measured yet", with no number. When the
  measurement ends the machine fills it (owner, 02.10: «низ для авто-ФВЧ нужен самому мастерингу, но не старту — пусть
  домеряется уже при мастеринге»).
- **Every value the machine places is one a person could set**: inside its knob's accepted domain, which the schema
  holds for every default and `felitronics_session_state_tests` checks for every target and source.
- **A person's edits** are taken from placement on — before the plan is Ready too (the table's Unplaced columns take
  `editDevice` and `revertEdits`), whether the panel is visible or hidden (owner, 02.10: «прибор крутить можно. на UI у
  ручки будет два слоя - машины и юзера»). A touched field is the person's: the machine's layer under it is still placed,
  a field it has not measured yet filled when its measurement ends, and the touched value never moves. A shell draws the
  two layers of a knob from `project.devices` (`machine`, `hand`) and `plan.devices.<device>.pending`. While any device
  field is not measured yet, a project is neither exported nor imported (`PlanPending`, as the open panel's master): a
  file never carries a placeholder as the machine's opinion, and an import — refused too while the file's own target
  has a field not measured yet on this source — is never compared against an incomplete one. An import and
  `adoptMachine` still wait for the plan (`NotPlaced` in the Unplaced columns). Where a plan is Ready with a field still
  pending (its device not offered by the shell, which reads nothing then), `adoptMachine` takes the machine's layer as it
  stands — the placeholder — and the placement that follows when the measurement ends fills it, the file's layer being
  gone. Values must be
  finite and inside the knob's domain; travel and step guide the slider. Values between steps or outside travel are
  accepted within the domain. A number is kept with −0 written as +0, so equal values give identical project bits.
- **`editTarget(lufs, tp)`** sets a person's target numbers; a field's **null clears it** (owner, 01.10): the person's
  number goes and the field is the target row's again, per field (`command::EditTarget::clear`). An absent key leaves the
  field; an edit that sets and clears nothing is accepted without a revision; a field both set and cleared (C++ only —
  the wire has one value per key) is `Contract` on that field. The project writes only the numbers a person holds, so a
  cleared field leaves its `lufs.hand`/`tp.hand` line, and the replay gives the same project.
- **`setTarget(name)`** replaces the target's numbers silently and always resets every device edit, including hidden
  edits and dither edits. The machine decides again for the new target. The warning before it is the core's fact:
  `SnapshotText::targetChange (snapshot().view())` gives `targetChangeResetsEdits` — "Manual device edits (N) will be
  reset", N the `handFieldCount` — and nothing when no field carries a person's value. The shell shows it in its
  confirmation and sends the command on confirm; a cancel sends nothing. The command answer and events are unchanged.
  New dither edits above 16 bits still return `NotOffered`.
- **A person's edit always sounds.** A device's tick is the person's own when they set one, on or off; otherwise it is
  on when any of the device's fields carries their value — a knob turned is a device wanted, so a project with
  `[tilt] db.hand = 3` and no tick written sounds; otherwise it is the machine's. Every reader of what sounds goes
  through the one rule (`settingsOf` in `src/Devices.h`): the EQ stage and its curve, the plan's needs, a recipe. The
  snapshot says it per device (`plan.devices.<device>.on` and `.tick`: Machine, Hand or Touched), so a shell draws the
  tick without deciding. A change of target resets the edits, and with them the ticks they gave.
- **`setManual(bool)`** changes only panel visibility. Device edits stay effective in either mode, including in a
  master recipe. `snapshot().view().handFieldCount` counts touched device fields, including false ticks and values
  equal to the machine, for the hidden-panel marker. Target-number edits are separate. `revertEdits` and
  `setTarget(name)` explicitly remove device edits.
- **`load`** checks everything first, then DISARMS — whatever ran on the old source stops, and the old source, its
  measurements and its masters go; the manual mode is switched off, and a person's device edits go with it (the mode
  does not outlive the file: its edits were decisions about the old source); the old samples are freed before the new
  are asked for — and then WRITES the new source: its samples, channel after channel, its name and its hash (64-bit
  FNV-1a of its rate, channels, frames and every sample's bits). The target and its edited numbers stay.
- **`master`** captures the recipe — the project as it is, the source's hash, the config's sound version — and starts a
  job, numbered on from the last for the session's whole life, a load included: an id is never 0 and never issued twice,
  and once the last is issued a load or master is rejected (`NoJobId`); the project may change meanwhile, and the master renders its recipe. When it is done the session keeps it under
  its job's id; `cancel(job)` stops the named measurement or master, `forget(master)` lets a kept master go. A master the
  session decides (version 0) waits for what its devices read: with the panel open it is refused (`PlanPending`) until
  that has ended; with the panel hidden it is taken, and its job measures it first — its recipe, target and a person's
  layer included, is the project when it was asked for, whatever changes while it waits.
- **`adoptMachine`** takes the planner's machine layer for the project as it stands — after an import, the decisions the
  file's differ from (`machineDifferences`). A person's layer stays. With no difference to take it is accepted without a
  revision.

### The plan of the devices

`src/Planner.h`. **The planner is the one place the machine decides.** Each of the nine devices — the high-pass, mono
bass, glue, saturation, tilt, the limiter with its needles, dither, low and the EQ bands — is a `Planned<>` of its fields: it
*proposes* its machine fields from what it may read (the target and its edited numbers, the source's shape, the devices
the shell offers, the measurements) and says where each came from (its target, a measurement, or the config's default)
and what held it back (the shell, the source, the target, a measurement that ended without a value); and it says what
it *needs* measured for the settings the project gives it, the machine's layer with a person's over it. A device writes
none of another's fields. The programme's loudness and true peak are not needs: the first measurement does not end
without them. What is needed today: the low end, for the high-pass's cutoff (the 120 Hz run, where it searches — ticked,
offered, an input neither too quiet nor shorter than `[input] shortSeconds`) and for mono bass's tick (the run at the
target's crossover, 120 or 150 Hz, on an input not too quiet and at least `[monoBass] loss.soundingAtLeastS` long); the
tempo, where a glue compresses (its release follows it — cd's machine glue, or a person's); and the needles at the
target's ceiling, where the limiter's peak clipper decides by itself. Mono bass's tick may change the chain's geometry
when its run ends: a master that waited for it has its demand checked against the heap again where the wait ends.

**The plan** (`SnapshotView::plan`, `PlanView`) says where placement stands — `None` (nothing placed), `Pending` (a
measurement the target's machine layer reads is still running), `Stopped` (…and it was stopped: `continueMeasurement`,
or a master, resumes it), `Ready`, `Unavailable` (no usable loudness or true peak: no plan and no master) — what the
project's devices need and which of those have not ended, the one waited for first (the needles job runs ahead of the
source's), the device that reads it and how far it is, whether the panel is read-only (the devices not placed; once
they are, edits are taken whatever the plan), whether the machine's layer is an imported file's, and every device's plan
(its `pending` fields: not measured yet). A measurement has ended
when it is Ready or Unavailable; a cancelled needles job has ended too (it is not built again, and the limiter takes its
path without needles), a stopped source measurement has not. So no plan waits for ever: every need is either measured,
ends with a reason, or waits for a person's `continueMeasurement`, which a hidden panel's master sends by itself. A
needed tempo goes ahead of the optional findings in the source's second phase, and the needles are measured at the
ceiling of the project being planned — a waiting master's own, until what it reads has ended or the master is dropped:
cancelled, stopped with the source's measurement it waited for, ended by a contract fault, or cleared by a `load` or
`loadMeasured` (which end every master state, the wait included), it hands the needles back to the project's own ceiling, so the project's plan does not wait for needles nobody measures. A master stopped with the
measurement says so under its own job's id (fact `cancelled`), beside the measurement's `measurementStopped`.

**The plan states its reasons.** `plan.facts` carries every line `PlanText` gives for a Ready plan — `{device, fact}`,
in the order of `Device` and, within a device, of `PlanText`'s members — and, last, the waiting fact `planWaiting` (the
device, the measurement, `awaitedFraction` as its percent) wherever the plan names what a master waits for; a plan that
is not Ready states, before that fact, only the card line `deviceUnmeasured` (the measurement it waits for) of each device
with `pending` fields — or nothing. A shell shows them as they are and composes none: some carry config
numbers it does not have. Each is written as an event's fact is (`WireFact`); the contract scenario `plan-pending`
records a waiting plan, the master it refuses, and its reasons once Ready.

**The advice beside a knob is a fact too** (facts 500–505 and 509, `PlanText::hpfCutoffAdvice`, `hpfSlopeAdvice`,
`monoBassAdvice`, `eqAdvice`): a person's value as it sounds against the norm engine.toml draws on the knob, so a hand
edit that leaves the norm gets its advice in the next snapshot. **Advice only where a hand acted** (owner, 01.10): the
machine's own value — proposed now, or kept from a file's machine layer — is its proposal, inside its own rule, and is
never advised against; each piece of advice is said only where a person set the value it judges (500/501 the cutoff,
502/503 the slope, 505/509 the crossover, 504 a shelf of tilt or low), so a slope by hand says nothing of the machine's
cutoff beside it. The high-pass's cutoff below or above
`[hpf] comfort` (24–42 Hz, strictly, the window named in the fact) and its slope gentler or steeper than `slopesNormal`
(a slope between two normal ones is inside; `plan.hpf.soundingSlope` carries the slope that sounds); mono bass's
crossover outside every zone of `[monoBass.zones]` (ends inside) — above them `MonoBassOutsideZones` (505, against the
club's and vinyl's upper ends), below them `MonoBassBelowZones` (509, against their lower ends); the EQ curve of the shelves as they sound (tilt and
low, never the high-pass) past `[eq] curve.warnDb`, at the point of the largest |dB|, said of the shelf that gives the
larger part there. Nothing for a device out of the chain. `kPlanFacts` is 18. The machine's own plan raises none of the
advice (500–505, 509) — by the rule above, and `theMachineKeepsItsOwnNorm` plans every target on every input the suite
measures and holds it. **The target's note** (facts 506–508,
`SnapshotText::targetNote`, the snapshot's `targetNote` beside `target`): where the target's loudness comes from, where
targets.toml `[notes]` says it is not a platform's published number or a standard — measured (youtubeMusic), practice
(cdDynamic, club), no normalisation (bandcamp). `[notes]` is shown, not sounding: it moves `all`, never `sound`.

**A finding is the planner's proposal; a sentence states what sounds.** `plan.hpf` and `plan.monoBass` hold what the
machine proposes and what it stood on — its cutoff and the note behind it, the loss at its crossover. What sounds is the
project's device: a person's layer over a machine's layer that a project file may have written. Each finding says which
(`sounding`: `Proposal`, `Hand`, `File`, `Off`, with `soundingHz`), and `PlanText` gives the planner's reasons only where
its proposal is what sounds: a person's cutoff is `hpfByHand` with that cutoff and no claim about the note, a file's
`hpfKept`, an unticked high-pass `hpfOff`; mono bass at another crossover or width is `monoBassByHand` / `monoBassKept`,
"will take N dB" is said only of the fold as proposed, the opposite-polarity warning of that fold left out or switched
on against the machine — and beside the by-hand sentence where a person's crossover sounds against that verdict
(`PlanText::monoBassPolarity`) — and nothing of a loss that does not happen. (The full wording of a person's layer is the next
slice's.) The chain's topology follows the same effective settings — the tick rule, `settingsOf` — never `[stages]`,
which is only the defaults layer a project file is written against: the planner decides every tick it has a rule for
(mono bass by its loss, whatever `[stages] monoBass` says). `writeEq` and `writeDynamics` are those settings as the chain
gets them — and `writeLimiter` the limiter, its peak clipper and the dither; `writeChain` (`src/Chain.h`) is the one
derivation of a master's chain from a project, and a master the session decides (version 0) is rendered from it.

**The high-pass and mono bass** (owner decisions 3.2–3.5) are the first devices that decide from a measurement, both
from the first phase's low-end runs, with no trial render. The high-pass stands always — every target, a quiet input
included — at a cutoff of max(what the sure lowest note allows, the target's floor, 32 Hz everywhere), never above the
machine's 50 Hz top (`[hpf] machineTopHz`). A person's knob travels further, to 80 Hz (`hzMax`, owner 01.10: a voice with
a guitar from a microphone), and its field is red from 50 Hz on (`comfort.warningHighHz`). The lowest band of the 120 Hz run that was on at all decides, alone: it is a sure note when it is on in 10 %
of the frames, stands 2 dB over the duty line, lies above 20 Hz and sounds 3 s in all — and if it is not (a rare 808, one
thump), the cutoff is the floor: the detector never takes a higher band as the note; a programme under 10 s is not searched, and a note that is not
sure — or a lowest band that fails — gives the floor, never a higher band. The cutoff the note allows is found on the
chain's own response (`highPassCutoffFor`: the matched cascade of the target's slope at the source's rate, by bisection,
not rounded to the hertz) so that it takes exactly the target's `noteLossDb` there (1 dB, club 0.3). Mono bass is weighed
by the harm itself: the loss the low end takes folded to mono, 10·log10((mid + side) / mid) over the 10 ms blocks of the
run at the target's crossover (120 Hz, vinyl 150) where the bass sounds (within 20 dB of the level the loudest 5 % reach,
3 s at least). Under 1 dB it is placed; from 1 to 3 dB, both included, placed with the number; above 3 dB left out —
and a person may switch it on, with `plan.monoBass.againstMachine` for the warning. A loss it cannot weigh is its own
reason, and it is left out: the machine does not fold what it did not weigh. The run keeps 65536 blocks of 10 ms
(10.9 minutes), and a piece longer than that has only its beginning in them: the loss is weighed over that part and
mono bass placed by it — it stands unless a measured loss rules against it — with `coveredSeconds` of `pieceSeconds`
in the finding and the sentence `monoBassPartWeighed` (`PlanText::monoBassCoverage`) saying what the weighing covered. `plan.hpf` and `plan.monoBass` carry the
findings typed; `PlanText` gives their report lines. `felitronics_session_hpf_mono_tests` holds each boundary to the bit,
the cutoff to core's own response on every target, slope and rate, and — through the pump on synthetic mixes — a
detector that never errs upward and a loss that tells centred, partial and inverted bass apart, opposite polarity above
the crossover not counting.

**The limiter, its needles and the dither** (owner decisions 3.6, 3.7, 3.12; technical decision 3О11; `src/Limiter.h`).
The limiter is always in the chain, with no tick; nothing — no gain reduction, no PLR, no cost — limits how far it goes
for the loudness, and the ceiling (the target's, or a person's edit of it) alone is hard. Its one knob is the peak
clipper's, inside its oversampler: `auto` — the machine classes the needles measured on the input at the ceiling the
target's numbers give (the input's peak less the need, `(TPin − Iin) − (TPtarget − Itarget)`): short (p90 of the runs
≤ 2 ms, bass share of their dose ≤ 0.25, PLR ≥ 10) lose up to 3 dB off their peaks; not cut where p90 ≥ 8 ms, the bass
share ≥ 0.5, the PLR < 8 or the source is clipped — ten confirmed clip-detector clips a minute or more (`[limiter.
peakClipper] clippedPerMinute`); between, with care, up to 1.5 dB. A need of 3 dB or less is not measured and not cut.
The two numbers (`shortCutDb`, `betweenCutDb`) are AMOUNTS (owner, 30.09): the clipper takes at most that much off the
peaks and the limiter does the rest, so its threshold stands max(0, need − cut) above the ceiling, the need the input's
(`plan.limiter.needDb`); a person's `manual X` is X dB off the peaks the same way. The finding carries the amount
(`overDb`, `proposedOverDb`), the chain the threshold; its line (`LimiterShort`/`Between`/`Manual`, and a vinyl master's
`MasterVinylNeedlesDeparts`) names the amount as a cap in its own words, the number exact ("the clipper will take no
more than 1.5 dB off the peaks, the limiter will take the rest", owner wording 01.10 — never "no more than ≤ …"), since the landing's drive decides how much the clipper really takes — and `LimiterLittleNeed` names the need as the one at the target. Where the need is not known, or need − cut lies beyond the
limiter's working range (12 dB), no threshold keeps to the amount and the clipper stays off. The landing then moves the
peaks by its own gain and pass ceiling; a rendered master's clipper reduction stays within the amount plus that drift
(`theClipperCutsItsAmount`: about 1.4–2.0 dB on the suite's mix).
Every reason is its own (`plan.limiter.why`, `NeedlesWhy`): the shell, the target (vinyl), a quiet input, no readings, a
little need, still measured, not measured (with how the measurement ended), no excursions, clipped, already limited,
bass, long. `manual X` — a person's threshold, or a threshold turned with no mode set (a knob turned is a device wanted)
— sounds over every one of those refusals, with the machine's reason beside it (`plan.limiter.againstMachine`,
`PlanText::needlesAgainstMachine`: "the machine would not cut: …"); `off`, none. "The same ceiling" is decided by the
bits in both rules that read it (`requestNeedles` and `needlesCurrent`). The dither follows the delivery's format alone:
at 16 bits it is in the chain — weighted TPDF, seed `0x853c49e6748fea9b`, blanked after 4096 zero samples, a stated
version of the sound (`[dither]`) — unless a person switches it off (the delivery is then rounded to its grid without
noise); above 16 bits there is none, and a person's tick, which only a project file can bring, is kept and does nothing
(`plan.dither.keptWithoutEffect`, `PlanText::dither`); `plan.dither.shaping` is the shaping it runs with at the
delivery's depth (`DitherShaping`, the core's order). The plan states the limiter's own settings as the chain is written
with them (slice 5): `plan.limiter.releaseMs`, `dualRelease`, `slowReleaseMs`, `lookaheadMs` and `oversampling` — the
factor as the limiter takes it; writeLimiter reads the release and the dither's shaping from the finding. The delivery is quantised once, in the chain: its WAV adds no
second noise. On a target cut to vinyl (`vinyl = true`, lp) the machine never stands above the target's own ceiling
(−3 dBTP) and never cuts needles; a person's ceiling above it and a person's needles sound, with the medium's warnings
(`PlanText::vinylCeiling`, `vinylNeedles`) and no refusal, and the plan carries the constant note that the cutting room
usually rolls off above 16 kHz (`PlanText::vinylTop`). A quiet input — strictly under −55 LUFS — gets the gain, the
ceiling, the format's dither and the high-pass at its floor, and no other device of the machine's (no mono bass, no glue,
no low shelf, no needles); a person's hand stays open. `felitronics_session_plan_sound_tests` holds every boundary.

**The whole plan sounding** (`src/Chain.h`, `writeChain`). A master the session decides takes its chain from the
project's devices in the order defaults → the machine's layer → a person's layer → what can physically apply: the EQ
stage when one of its three bands is on, mono bass when ticked on a stereo source, the compressor when the glue
compresses, the clipper stage when the saturation shapes, the limiter always, the dither where the delivery takes it —
the topology from the devices as they sound, never `[stages]`, and the fixed geometry (`[chain]`, the two lookaheads,
the key filter) stated. Neither gain of the chain is a device's: the landing search normalises the input and lands the
loudness, once. The chain is fixed when the master starts — at once, or, for a master that waited for its measurements,
when the wait ends, from its recipe's project — and its fingerprint is the recipe's `readyHash` (`readyVersion` 0). Its
demand is declared by the command whole (the same plan the job starts from); a wait that ends on a heap too small drops
the master with a memory error under its own job's id. Its report carries `medium` (`MasterMedium`): on vinyl whether the
medium's rules held in the chain it ran — then "ready for cutting: no cardinal corrections (mono bass, infra-low, peaks);
RIAA and the level for the side's length are the cutting engineer's", what the file shows (the fold's crossover, the
high-pass's cutoff, the true peak under the ceiling) and what no file can show (the side's length, sibilance at the
cutter, the centre) — or that the settings depart from them; and a very quiet input's line. The suite holds a decided
master, sample for sample, to the PREVIOUS path's master of the chain it composes itself from the owner's rules — on
allStreaming, on cd (44.1 kHz, 16 bits, the glue on the measured tempo) and on lp, and under a person's edits of every
kind with the panel hidden — and a master that waited to the one asked for after.

**The observations** (owner decision 3.13; `snapshot().view().observations`, `src/Observations.h`). What the
measurements found in the file, as facts with numbers and never a verdict of taste, in the order of the analysis — the
file (clipping, DC offset, unused low bits, dual mono, silence at the edges, a quiet input, a short one, an input already
limited), the spectrum (a spectral wall, the loudest low note, the lowest occupied band, infra-low, wide bass, opposite
polarity, sibilance — shown whatever the de-esser), the hum (a line, and one that wanders). Each says found, not found or
not measured (with the measurement's reason) — the three never fold into one — its kind's style, a confidence and a
severity along the ramps of `[observations]` (its first edition), `doubtful` under `doubtfulBelow`, `hypothesis` where
its thresholds are starting values, and what deals with it (`handledBy`: the high-pass, mono bass, a person, nothing)
and whether that device is in the chain. They change nothing: no observation switches a device. Rarer clips are named by
place ("found 2 clips: 0:12, 1:47 — it looks like an edit"); ten a minute is a clipped source. The lowest occupied band
is the low end's reading published with its sureness (`lowestOccupiedSure`, `lowestOccupiedResolved` beside it): an
unsure one is shown as unsure, never withheld, while the high-pass takes the floor for it. `ObservationText::fact` gives
a found one's sentence, and a not-measured one's name and reason (`ObservationUnmeasured`); a kind measured and not
found has none. The style is error, warning, note or reading (`[observations.kinds]`): the loudest low note and the lowest
occupied band are readings, numbers the file shows, not findings. **The owner's table (01.10)**: clipping and opposite
polarity are errors; lost bits, a dual-mono file, an input already limited (PLR under 10.5 dB), a spectral wall at 88 % of
Nyquist or lower (a lossy source), wide bass and a quiet input are warnings. Four kinds also take their style by size —
`sized()` in `src/Observations.cpp`, the one place, with the thresholds in their own `[observations]` rows: a DC offset
is a note under 1 % of full scale, a warning from 1 % and an error from 10 %; the EFFECTIVE depth (the container's bits
less the low bits always zero) is nothing at 24 bits, a warning at 23 to 17 and an error at 16 and less; infra-low is a
note from 2 %, a warning from 5 %; the hum is a note, a warning from half its severity (−50 dB against the programme)
when it is confident. A size raises a style, never lowers it, and never raises a doubtful finding; where the size chose
the style the line says so (`SourceDcNote`, `SourceTruncatedBits`/`SourceShallowMix`, `SourceInfraLowNote`/`Warning`).
A stereo source's DC line names both channels, signed, as the readings print them — `SourceDcNoteStereo`/`SourceDcStereo`
(446/447, "L {left}, R {right}"; the observation's `second`/`third`, `places` the channels read); a mono source keeps its
one number (`SourceDcNote`/`SourceDc`).
The hum is not measured only where the detector could not listen (too short, every frame holed, too coarse a
resolution, or a base line heard in too little quiet to tell whether it stands still), or where the session refused the
analyzer before any work (no price for the programme, the memory) — then with that refusal's own reason, never "too
short"; where it listened — a programme
never quiet, a comb without its base, a quiet stretch without a base line — and no steady line stood out, it is not
found. A hum is a line heard in the quiet passages too (owner, 01.10): a steady line that is absent from the frames where
the whole programme is quiet — its own bands included — plays only with the music, and the detector says so
(`HumReason::LineOnlyWithMusic`, 11): answered, the hum not found. Only such frames inside the programme speak —
strictly between its first and last frame above the gate — so a dithered lead-in, a tail or room tone at an edge never
vetoes a hum loud enough to keep every frame it plays in above the gate. **The observations speak for themselves**: `observationFacts` carries every
kind's line as `ObservationText::facts` states it — `{kind, fact}`, in the order of `ObservationKind`, empty before a
source — so a shell shows them without composing one; the names, the handling and the reasons are catalogue terms
(`terms.observation`, `terms.handledBy`, `terms.measurementReason`).
**The readings are facts** too, one table per place keyed by `ReadingKind` (appended, 31 kinds): each entry is
`{kind, fact}`, the fact `Value` — one number with the core's unit and precision. The snapshot's `readings` (at most 26)
are the source's — integrated loudness, true peak, LRA, PLR, DC offset per channel, the lowest occupied band, the exact
PCM bits, correlation, burst events Mid and Side, hum, tempo and its confidence (`TempoConfidence`, 438, a word of
`terms.tempoConfidence`), the clipping's runs, longest run, clipped samples and sample peak, the low end's side share,
the stereo windows and the crest's active blocks per band — as `ReadingText::source` states them from the measurement,
following each new source. A master's `MasterReport::readings` (at most 9) — achieved loudness, true peak, LRA, PLR,
target, ceiling, gain, the landing's passes and the check passes — are `MasterReportText::readings`, stated when the job
settles the report. A kind not measured has no entry; the need is not a reading. The quantities' names are
`terms.reading`, in the order of `ReadingKind` (`ReadingText::name`). On the wire a reading is `{"fact", "kind"}`, both
lists are optional, and the decoder refuses an unknown kind.

**Tilt and low** (technical decision 3О10) are two devices of the person's taste in that one stage. The machine never
ticks tilt and leaves it at 0 dB; it ticks low only for a target's correction for its medium (`lowDb`: vinyl's
+0.5 dB) — a number of the target, not a measurement — and not on an input too quiet to measure, where the number
stays on the knob. Tilt is the core's tilt about 1 kHz: the low end down by the knob, the top up by it, the ends 2·dB
apart, the pivot unmoved; low is a static shelf at 80 Hz, Q 0.6 — no dynamics, nothing swept or split. Both knobs take
±6 dB as written — between steps and past the slider's travel — and the band carries the value bit for bit: inside the
domain nothing in the engine clamps. `felitronics_session_tilt_low_tests` holds the machine's layer on every target and
input, the domain's ends, the geometry against core's response, the three EQ devices together (each tick taking out its
own contribution; the engine fed the written bands giving, sample for sample, the sound of the decided filters), and a
person's layer kept hidden, saved and imported until a change of target resets it.

**Glue and saturation** (owner decisions 3.8, 3.8а, 3.9; `src/Dynamics.h`) read the input in ONE system of levels:
brought to `[input] referenceLufs` by one gain, `referenceLufs − integrated` (`plan.inputGainDb`), which the landing
search adds to the chain's input gain, once — `writeDynamics` writes the two stages and touches neither gain of the
chain, and the search, moving only the gain before the limiter, recalibrates neither. The short-term P95 and the true
peak are read plus that gain, so one mix exported louder or quieter gets the same compressor and the same shaper (the
true peak to the bit for a level that is a power of two; the P95 within the programme report's own 0.1 LU bin, since
that report reads its percentiles off bins of the file's level).
The glue's knob, "up to N dB", is the loss on the loud places: the travel `g` at which the core's own static curve
(`dynamics::GainComputer`, soft knee included) takes exactly N dB at the loud places (the P95 and the calibration over it, below), found on that curve by bisection — one
smooth formula over 0…6 dB (`[glue]`: the ratio by depth, the threshold offset, knee and attack linear, the release's
beat divisor geometric), nothing rounded, no clamp reached inside the domain; 2.6 dB is ratio 1.74 with the threshold
6.1 dB under the loud places, 6 dB ratio 2.84 and 9.3 dB under. It is not a ceiling on the live reduction. THE SCALE IS CALIBRATED TO MUSIC (`[glue] detectorOverP95Db`, 1.5 dB): the
P95 is a short-term loudness, the detector a 5 ms RMS with an attack, so the threshold stands that offset above
P95 + the travel's offset — chosen so that the gain reduction really taken on the loud places (its P95 over the
programme, the median of the owner's 11 mixes) is the knob: 1.27 dB at 1.25, 2.61 at 2.6, 3.02 at 3, single mixes within
about ±0.45 dB; uncalibrated it was 1.68, 3.24 and 3.70. The slider's 0…3 dB
is a hint; the core takes 0…6 as written, the machine sets it on cd alone, 2.6 (the schema refuses a machine value
above `knobMaxDb`), and a tick without a knob starts at 0.5. The release is a beat over the divisor, from the tempo
where its label is high (the detector's confidence of 0.5 and up), from 120 BPM otherwise — a tempo that ended
unavailable included — and is kept inside 50…500 ms. A glue that compresses reads the tempo, so a master waits for a
pending one; a glue at 0 dB, or unavailable, reads nothing. WITHOUT A P95 (a scrap too short for a short-term loudness)
the glue is unavailable to the machine and to a person alike: the machine's tick is left off (`HeldBack::Unmeasured`),
a person's tick and value are kept and shown, `plan.glue.state` is `Unavailable`, the chain gets no compressor, no P95
is invented and the master is made without it. `plan.glue` carries the state and, where it compresses, the values the
chain gets — ratio, threshold (in the normalised input's dB), knee, attack, and once the tempo is decided the tempo,
the release asked for and the release given. Out of the chain — unticked, or at 0 dB — it carries the same numbers for the
knob as it stands (slice 5), which no compressor gets: the ratio, knee and attack always, the threshold and its P95 point
where there is a P95, and the release at the decided tempo or, since nothing measures the tempo of a glue out of the
chain, at 120 BPM (`tempoMeasured` false); unavailable, none; `PlanText::glue`, `::glueTempo` and `::glueRelease` give the refusal, the
fallback tempo and a release held at a limit as facts.
The saturation is the chain's shaper stage (`MasteringChainParams::clipper`) — not the peak clipper inside the limiter,
which is the limiter's. The machine never sets it. The knob is the drive the input would get at 0 dBTP: the core drives
the shaper at k = 10^(drive/20) − 1, so the stage gets `20·log10(1 + (10^(knob/20) − 1)·10^(−peak/20))` for the
normalised true peak — no trial render; the compensation is 0, the mix as set, the shaper's output at 0 dB (`plan.saturation`). Its TYPE is
the shaper's curve (`SaturationFields::type`, felitronics-core's `WaveShaper::Shape`): the machine's layer holds the
config's `[saturation] shape`, tape (owner, 01.10; tanh before), and never another — so a hand drive with no type
picked sounds tape; a person picks tanh, tube, transistor, transformer or tape, and
an edit or a file that gives atan, cubic or asym by hand is refused `NotOneOf` (those stay the config's, for research).
WHAT EACH DID is measured on its own stage and reported in the master's cost: `glueP95Db` and `glueMaxDb`, the
compressor's gain reduction over the programme's 4 ms windows and its largest sample; `saturationCutMaxDb` and
`saturationCutUsualDb`, the soft clipper's cut of peaks — `MasteringChain::clipperPeaks`: the peak of the stage's
input against the peak of its output, internal quantum by quantum, after its mix and output trim, each against the gain
the stage gives a sound too quiet to bend (a peak-normalised shaper lifts everything under full scale, so the ratio
alone is a gain, not a cut; the output knob moves both and cancels). The loud places are the quanta with the highest
input peaks, `[saturation] cut.loudShare` of them (5 %); the largest cut is the largest among them and the usual one
their median — one hit moves the first and not the second. Neither is the fall of the chain's true peak.
`MasterReportText::glue` and `::saturation` are the report's lines, published with the master's cost; a stage out of
the chain has no number (`NoSignal`) and no line. `felitronics_session_glue_saturation_tests` holds the knob to the
core's curve at 0.5, 1.25, 2.6, 3 and 6 dB, the curve's evenness over the whole domain and at 3, the machine's layer on
every target, a person's values with the panel hidden, saved and imported, the refusal without a P95 through the real
pump, every tempo outcome and both clamps, the drive at five peaks, every written field, one mix at three levels
through the real analyzers and the real stages, the cut's even growth over the drive, and a real master whose reported
numbers are, to the bit, those of the same two stages run alone behind the one gain.

**One need, one measurement.** The source's measurements serve every target: a change of target or of a person's
layer measures nothing again, and the needles run again only for a new ceiling (another target with the same numbers
reuses them). **The plan's key** is a hash of everything it is made from — the source and its measurements' keys and
outcomes, the target and its numbers, both layers of every device, the devices offered, the config and core versions;
equal keys, one plan, and the planner runs again only when the key moved. Showing or hiding the panel is no input.
`felitronics_session_plan_tests` holds all of it: every device's proposal equal to the previous path's placement for
every target, source and shell, bit for bit; the key's hits and misses; the open panel's refusal and the hidden panel's
wait, with the recipe captured when the master was asked for; the tempo waited for only where a glue compresses; the
needles at a new ceiling; a stopped needles job that ends the wait; one measurement for every target; an import that
keeps the file's machine and an `adoptMachine` that takes the planner's; a change of target.

**The one EQ stage.** The high-pass, tilt and low are three devices writing one stage of the chain
(`MasteringChainParams::eqBands`): each owns its own band (`src/EqCurve.h`: the high-pass 0, tilt 1, low 2) and writes
only that band, so one's change or tick moves nothing of another's. The curve the snapshot shows is drawn from the
bands written, by the designs the chain's EQ engine runs for them, at the source's rate: the plan test holds it to the
previous curve bit for bit, to core's response of the same bands, and to the engine's own output on sines.

**Memory.** `check()` says, before the work, what a command will ask the heap for, by the expressions that size its
requests: a load its samples and its name, a master room for one more master kept (so that the render's end asks for
nothing), import the library's parse/read allowance plus the session's owned storage, and every other command nothing.
`felitronics_session_state_tests` holds typed commands to exact requests; `felitronics_session_project_tests` holds
import, including malformed and hostile documents, to its bound on cumulative allocation requests. Import preflight
checks entry and state, then counts the text allocation-free; document validation is work inside `apply()`, so a passed
`check()` is not a promise that the document passes its schema.

**No text.** A rejection is a `Rejection` code, and a field its place in its struct; the values are stable, and a new
reason is a new value at the end. Import also returns a 1-based Unicode line and column and, for a device field,
its typed device identity. `Text::rejected()` renders the code and field from the shared catalog.

## The project file and replay

`exportProject()` materializes the placed `Project` in one canonical TOML writer. `exportProjectBytes()` gives its
exact allocation demand without allocating; the returned `ProjectText` owns exactly those bytes, without a terminator.
An Empty or Loaded session refuses export (`NoSource` or `NotPlaced`): its unplaced zeros are not machine decisions. A
placed project is exportable while what its devices read still ends: its machine layer is written.
Files and browser storage belong to the shell. The writer reads no filesystem and the session reads no file itself.

```toml
defaults = "2026-10"
manual = true

[target]
name = "allStreaming"
lufs.hand = -12.5

[hpf]
fq.hand = 36

```

The first two keys identify the defaults and the release that placed the machine layer. A new placement stamps
`Session::version()`, sourced from the root `project(... VERSION ...)` line that the release tool updates; the existing
version suite pins the binary to those build definitions. A core version is canonical `major.minor.patch`, each part
an unsigned 32-bit integer. The manual flag follows. `[target]` names a target by its config **key**, never an index;
only touched `lufs.hand` and `tp.hand` follow it. Device sections follow the typed order and use the config's names;
a section appears only when a machine difference or touched hand follows. The reader accepts absent and empty sections.
Each field writes `.machine` before `.hand`, one value per line. Defaults are omitted from the machine layer; every
touched hand is retained, including a number equal to the machine's. Booleans are TOML booleans, slopes integers, and
needles are `"auto"`, `"manual"` or `"off"`. Doubles representable in TOML's exact nine-place decimal subset use ordinary
numbers. Other binary64 values use quoted shortest round-trip decimals, parsed by the exact JSON numeric reader.
Negative zero writes `0`. Allocation tests and domain/extreme-value round trips hold the writer to its demand.

`importProject(commandId, bytes)` is `apply(command::ImportProject{...})`. Its row in the one command table is the
device-edit row: `NoSource`, `NotPlaced`, then accepted in both measured states and both mastering overlays. The file
supplies panel visibility and accepts hand fields with either value of `manual`; an edit to
a device excluded by the shell or mono bass on a mono source is refused. Stored dither edits may be dormant above 16 bits. Import leaves ongoing jobs, captured
recipes, measurements and kept masters alone. An accepted import moves the revision exactly once.

Checks run in this order:

1. The thread's floating-point environment, then the command table, then the library's allocation-free storage count.
   A saturated count or a sum that does not fit `size_t` is `ProjectTooLarge`, without allocation. There is no separate
   session text cap; felitronics-toml enforces its own 1 MiB document limit during parsing.
2. felitronics-toml syntax, then its `Reader` schema: required defaults, core and manual fields; target name and touched
   target numbers; devices in typed order, fields in declaration order, machine before hand. Numeric fields check the
   accepted domain at the loaded source's sample rate. Unknown keys are reported as each table closes, including unknown sections
   and author suffixes. Dotted keys and inline tables bind through the same schema.
3. The defaults version, the core version spelling, and the target name.
4. Offered-device constraints in device/field order, then comparison with today's machine decisions.

The first refusal carries its code and position. A missing key points at its table; a bad value at the value; an
unknown key at the key. Syntax refusals carry the parser's position. The candidate and its comparison live separately
until every check passes. Rejection changes no project, state, revision, source, job, progress, master or comparison;
it publishes the ordinary rejected event and advances only `seq`. Each appended rejection is fact `100 + code`, in
Russian first and English, held by the catalog gate.

`check(ImportProject)` obtains `s = toml::storageFor(text, toml::ReadStorage{required})` and publishes
`s.parse + s.read + ImportedProject::storageBytes()`.
The library counts cumulative allocation requests for this text, including malformed input, container growth and
MSVC Debug bookkeeping. The possible missing required paths are `defaults`, `core`, `manual`, `target` and `target.name`.
The read visits each present table and field once. Each custom refusal follows a successful conversion and is the
only problem for that key, so it needs no additional path allowance. String conversions are included in the library's
read allowance. The session's own storage function returns zero: the candidate project, comparison rows, original
defaults label and emitted owned facts use fixed inline storage, already on the stack or in the session.
The two library counts and the owned allowance are added with overflow checks. The declared-budget harness measures
the complete import on realistic projects, schema refusals and adversarial texts up to the library's document limit,
and prints declared/actual tightness for a realistic project. It also proves an under-declaration fails. Parser
allocation laws belong to felitronics-toml's storage suite. If the environment cannot serve the published demand,
allocation has no recovery path.

The core carries **only the current defaults table**; today its label is `2026-10`. Labels are strictly `YYYY-MM`, with months `01` through `12`.
An import accepts only the current defaults label and never converts (owner, 30.09: no project was ever saved with
another one, so the core carries no code for older labels). Any other label — `2026-09`, saved before the core had its
planner, included — is refused whole as `UnknownDefaults` (fact 126), as is a malformed one; a newer label is
`NewerDefaults` (28, fact 128). These refusals follow the schema read, in its fixed order, and leave state and revision
unchanged. `ProjectTests.cpp:defaultsVersions` names `2026-10` and turns red if it stops opening with its own machine
layer. Export uses the current label. The file carries no core stamp: a `core` key is an unknown key
(`ProjectUnknownKey`).

After defaults selection, the file's complete machine layer always wins (omitted fields mean defaults),
with the person's touched layer over it. The planner decides again beside it, for the file's target on this source;
`plan.fromFile` says the layer is the file's, and `adoptMachine` takes the planner's decisions in its place, the
person's layer kept. `MachineDifferences` (8) publishes the count of differences between the file's machine layer and
today's planner, however they arose, when there are any; it renders in Russian and English.
`snapshot().view().machineDifferences` holds ordered `(device, field, fileValue, coreValue)` rows. Flags use 0/1,
choices their enum numbers, and knobs their doubles. Owned snapshots, the codec and its generated `.d.ts` carry them.
A target change explicitly places today's machine and clears the comparison. Hand edits and panel visibility do not replace it.

The page draws **`snapshot().view().eqCurve`**: the summed high-pass + tilt + low response using hand-over-machine
values, independent of panel visibility. It is empty before placement, then contains 128 logarithmic `(hz, db)` points
from 20 Hz to min(20 kHz, 0.49 × source rate). Each tick bypasses its own contribution. The coefficient designs follow
core's matched filters with deterministic math; the event suite compares their response with core across rates and
all high-pass slopes. Low keeps EQ band 2, 80 Hz and Q 0.6; tilt keeps band 1 and its 1 kHz pivot.
The one codec generator carries `eqCurve` into `snapshot.d.ts`; `SessionSnapshot.eqCurve` describes transferable
little-endian f64 rows, columns `[hz, db]`, stride 2. `handFieldCount` is a JSON number in both snapshot forms.
**`eqOnlyCurve`** (slice 5) is the same stage with the high-pass's band out — tilt, low and the EQ bands — on the same
points and summed the same way, so a page draws the tone apart from the filter without subtracting a Butterworth of its
own; the same row form, empty with `eqCurve`. An owned snapshot holds both curves in one block.

**The snapshot is sized in one walk** (slice 5). `Wire::snapshotBytes` used to run `Codec::encodedBytes` — the whole
view with every row printed as decimal text — only to learn whether the view encodes, then walk it again with binary
rows: two passes, the first the costly one (168.8 ms on a 3-minute stereo source with one master, 0.94 ms per second of
audio). It now asks `detail::snapshotEncodable` what only the text walk refused — the floating-point environment, the
view's invariants and a machine difference's device out of range; every other check is the same in both walks — and
walks once: 10.2 ms on the same snapshot, the bytes written unchanged (both contracts).
`felitronics_session_master_query_tests` holds the sizes and statuses to the previous function, copied
(`tests/PreviousSnapshotBytes.h`), on a session and on five broken views, and `felitronics_session_snapshot_sizing` its
cost to under half the previous one's — a target of its own, built without the session's flags (a clock under
`/EHs-c- /we4530` is C4530 on MSVC).

**A create asks for two blocks** — the session object and its step's events (`Session::createBytes` is their sum, exact).
The event batch (62 events of every payload kind, 113 KB) is most of a session; held apart, the object stays small. Each
block fits AddressSanitizer's largest primary size class with its redzone (128 KiB less 2 KiB), which
`felitronics_session_abi_tests` holds: past it every create is an mmap and a munmap, and the walk through a slot's 16.7
million generations takes hours under the sanitizers instead of minutes (slice 5's object reached 129320 B in one block).

Recovery and heap compaction use the same operation: create a new session, load the same source, advance measurement
to the same measured state, then import the last exported project. The project includes target, manual mode and both
layers. **Revision, sequence numbers, job ids, progress and the masters list are not part of the project.** Retained
masters and active work are not recreated by import. The replay suite drives repeated loads, target changes, edits,
manual mode and masters, then compares the recovered state and every project field. The measurement suite compares
all facts in order and the complete final snapshot with step budgets 1, 7 and a large budget; phase-event counts are
not a replay requirement.

The facade's existing poison latch is permanent. Its native replay test injects the allocation failure used by the
master ABI suite. Since session creation is `noexcept`, the test observes the abandoned call from a termination
handler, verifies every status entry point refuses, replays outside the facade, and exits; it never resumes or clears
the broken instance. A test-only accessor reaches its C++ project for comparison with its owned snapshot. The
exceptions-free tier exercises the same permanent latch through allocation reentry. The C ABI is version 1.

## The text — facts, one catalog, one formatting table

Nothing in the session prints to a console. It states a **fact** — a `FactId` and typed arguments: a number with its unit, precision,
sign and bound; a count; a term the catalog names; a note as a MIDI number; a text of the user's, never translated
(`<felitronics/session/Text.h>`). `Text::text(fact, lang)` renders it: a pure function over two documents compiled into
the library — no state, no file, the same bytes on every row, native and wasm — so the pure kit can call it on a page's
main thread.

- **Two documents, compiled in.** `text/catalog.toml` holds whole messages — never assembled from fragments — with named
  placeholders (`{passes}`) and, where the words depend on a number or a term, `plural` or `select` variants; it declares
  its languages, today `ru` and `en`. `text/format.toml` is the one table of how each of the twelve site languages
  writes a number: its decimal sign, its grouping separator and CLDR's minimum grouping, the Unicode minus, the plus, the
  bounds (`≥`, `≤`), the absent sign (`—`), each unit's pattern (Turkish `%45`, French narrow no-break spaces), the names
  of the notes (letters; the German system, where B natural is H; solfège). felitronics_toml_embed compiles both in
  (`embedded/catalog.h` and `embedded/format.h`, admitted by name in `src/Text.cpp` alone), and the renderer walks them
  in place: a lookup is a binary search over a table's keys, and nothing is parsed or allocated.
- **The build holds the catalog.** `felitronics_session_text_check`, a host tool compiled from the library's own
  `src/TextSchema.cpp`, checks both documents before the library is built — in every build, a consumer's and one without
  tests included, through node on the wasm tier, and in `tools/wasm/build.sh`: every declared language has every message
  and every term; a placeholder names an argument of its fact (`src/TextFacts.h` declares each fact's arguments by name
  and kind), every text — each variant on its own, since Russian "one" is also 21 — places every argument (a `select`'s
  own may be left out, its variant says it), and every language places the same set, repeated or reordered freely; a plural message has exactly its language's CLDR categories, a
  select message exactly its group's terms; no brace is malformed; the table has every field — a separator with no digit
  and no sign, the row's own minus included; signs with no digit that are no separator, the fixed ones exactly as the
  law states them (minus U+2212, absent "—", "≥" and "≤" with a no-break space); a pattern for every unit with no
  breaking space — in all twelve languages; and no key is one nothing reads. A problem is `<file>:<line>:<column>:
  error: <fault> <key path> — <rule>`, and its stamp is written only on success. Eight controls
  (`tests/text-must-fail.cmake`) plant a missing message, a stray placeholder, a lost one, a missing plural category, an
  undeclared language, a unit without a pattern, a breaking space before a unit and an absent sign of "1" in a copy, and
  require the gate to go red at the spot; the suite plants forty-six more in-process.
- **No fallback, no guess.** A message the catalog does not have in a language — every message, in a language it does
  not declare — renders as its id (`Text::key`), never in another language. A fact whose arguments do not match its
  declaration — one missing, one too many, one of another kind — is incomplete (`Text::complete`): it renders nothing,
  and `fc_kit_text` refuses it with `FC_SESSION_ERR_CONTRACT`, never printing its template.
- **Numbers by rules of its own**, stated in `Text.h` and held by `felitronics_session_text_tests`: a double is read as
  its shortest round-trip decimal — `std::to_chars`'s shortest form, which the standard specifies exactly, so every
  standard library gives the same digits, with no locale — and that decimal is rounded to the grid of `precision`
  digits, a half away from zero, in characters and integers (`src/TextNumber.cpp`: no libm, no printf, no floating-point
  arithmetic). The number a person wrote rounds as they would round it: 1.005 → 1.01 and −14.05 → −14.1, though their
  doubles lie just off the half (the owner's decision, 2026-09-27). Checked against an independent oracle over 24 000
  decimals of up to 15 significant digits — each its own double's shortest form, since DBL_DIG is 15 — rounded in 64-bit
  integers, and at the halves where the two readings disagree, across a carry into a new digit and at the extremes of
  the double (1e300 prints a one and three hundred zeros); the sign is the printed number's — a value that prints as zero takes none,
  under every `Sign` (−0.04 at one digit is "0.0", never "−0.0", which a musician reads as a bug; either zero is "0");
  a value that is not finite prints the absent sign alone. Between a number and its unit is a no-break space (U+00A0;
  French U+202F), so "−14" and "LUFS" never wrap apart. All twelve rows are pinned to CLDR 48 as ICU 78
  writes it.
- **No rendering depends on the thread's floating-point environment.** The digits come from `std::to_chars` (integer
  arithmetic in every standard library) and the signs are read from the double's bits — through a volatile, since clang
  folds an integer test on them back into a floating-point compare, which a thread that reads subnormals as zero answers
  "zero" (measured). The suite renders subnormals, halves and zeros under flush-to-zero, denormals-are-zero and each
  rounding mode, set the way a host sets them, and requires the bytes of the default environment — which a formatter
  that multiplied or rounded in floating point would miss.
- **Plural categories on the printed number.** CLDR 48's cardinal rules for the twelve languages, evaluated on the digits
  as printed — "1.0" is not "one" in English — by the component that formats them; pinned against ICU 78's own answers on
  39 numbers of every category in each language, each language's set reached exactly.
- **What a person typed.** `Text::parse` reads a number with `std::from_chars` over its digits and one correctly rounded
  division: the double a correctly rounded `strtod` gives, without the process's locale. The language's decimal sign, or
  "." where "." does not group; no grouping separator, so a grouped number the table printed is never read back as
  another ("12.345" is not twelve in German); at most nine fraction digits and 2^53. It is the text's one computing call,
  and asks for the default floating-point environment on entry, as the rule is; a thread that rounds upward gets nullopt,
  not 0.30000000000000004.
- **Memory.** `size()` and `write()` allocate nothing; `text()` asks the heap for at most `textBytes()` — the result's
  length and terminator, rounded up to the 16-byte step a standard library allocates a string's storage in, and 64 bytes
  for what MSVC's STL asks beside them (47 to align a block of 4 KiB or more, a 16-byte proxy under iterator debugging).
  The suite holds both through the allocation counter, and requires a declaration 80 bytes short to be caught.
- **The same bytes on every row.** The suite renders a corpus — every fact in every language, 36 000 numbers from a fixed
  generator across every unit, sign, bound and precision, every note — and pins one FNV-1a hash of it, which every native
  row and the wasm tier must give.
- **A command's rejection is a fact.** Every code of the state machine's `Rejection` (`Commands.h`) is the fact
  100 + its code, a sentence in every declared language that says what was refused and why; the three a field refuses
  (not finite, not one of its values, outside its domain) name the field — a term for each field a check can
  refuse: the target's two numbers, every device's knob and choice, a load's audio. `Text::rejected(answer, request)`
  builds it from a refused answer, reading the field off the request. Where the answer carries the refused number
  (`Answer::value`, and for `OutOfDomain` the domain the check read, `low`/`high` — the knob's bounds, or 0 and half the
  source's rate for a Nyquist domain) the field is said with them: `RejectedOutOfDomainValue` (180, {field} {value} {low}
  {high}, each number at the places it has) and `RejectedNotOneOfValue` (181, a slope the knob does not take); an
  import's refusal names the field alone. The wire's rejected answer carries this fact last (`fact`, a `WireFact` as
  events carry it), and a contract answer carries `RejectedContract` (131): a shell maps no index to a word. The mapping is a switch over every code with no
  default, so a code the state machine adds and nobody maps is an error in this repository's builds (`-Wswitch`,
  `-Werror`); the suite holds the table code by code, the field terms position by position against `src/Devices.h`'s
  walk of the fields (a term exactly where a check can refuse), and renders the answers of a real session.
- **Only in words:** the wording itself — the glossary's Latin terms and the polite form, which the site's guards hold
  for its own catalogs; this catalog has no wording lint; and that a fact's user text is a view whose bytes
  its caller keeps alive while it is rendered.

The facts' ids are stable and fall in ranges (`Text.h`): 1–99 readings and the landing, 100–199 a command's rejection,
200–299 the phases of the work, 300–399 the session's errors and 400–499 the source: its measurement's state, its observations and its readings' words. Adding a fact is three edits: its id in
`Text.h`, in its range, its row in `src/TextFacts.h`, in id order, and its message in every declared language — the
build is red until the three agree.

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

## Work units, event deltas, and snapshots

The mastering render's source-rate conversion, chain latency, drain, and preparation cursors are specified in
[Resumable delivery render](RESUMABLE-RENDER.md). The session's master job can drive that API within its work-unit pump.

`LandingSearch` keeps one search across calls. It first surveys source spectrum and crest in bounded units, then
renders and measures within one budget of up to twelve passes. A measured hit stops immediately. Every render is
logged; an exhausted budget returns the closest ceiling-safe PCM and its measured miss. When measured renders exist and
none is safe it returns the gentlest — the smallest overshoot of the ceiling — as `TargetUnreachable` bound by the
ceiling and marked `peaksAboveCeiling`; `Unavailable` only when no render could be measured. Source and output are the
only full PCM buffers. If a previous candidate wins, a counted pass restores it; the last pass is reserved for that
render once a candidate exists (a safe one, or else the gentlest above the ceiling). The search can pause during SRC, rendering,
metering, band statistics, integrated gates, the LRA scan, restoration and independent remeasurement.
`TargetLoudnessSolver::solve()` drives this same path when `LoudnessRequest::productLanding` is set. Its older
request policy remains available for existing callers. The request-aware `TargetLoudnessSolver::solveCallBytes(req)`
and `DeliveredMastering::solveCallBytes(req)` include the product search workspace; the integer-bucket overloads
keep the legacy solve quote. `LandingSearch::storageForProgramme()` quotes source, output, workspace and the largest
block, including capacity retained on reuse through `storageForJob()`. Chain and converter preparations are separate.
`LandingOps::plan()`
sets the source normalization toward −18 LUFS separately from the adjustable pre-limiter gain; a delivery-rate
change requests one extra source-rate impact pass outside the twelve landing renders.

`LandingSummary` carries status, achieved LUFS, signed miss, absolute distance, reference true peak, measured
source and limiter hints, work units and the ordered pass log — and, where the solver decided them, `binding` (the
`LandingConstraint` that held an unreachable landing, the solver's `LoudnessSolution::binding`) and `belowLufs`/`aboveLufs`
(the two levels a between landing's target fell between, quieter first; were a solver's side not a number, the verdict
stands without them and the master is still delivered — a target that cannot be hit always returns the file). The decoder refuses a limit on another status
and levels out of order or on another status; the keys are always present, null where the status has none, and a
missing one is a decode error. Its limiter and K13 clipper traces share
the delivered-frame grid and carry min/max/mean reduction, finite counts, validity and completion. Limiter
GR follows the audio receiving gain after lookahead; K13 reduction follows the detector's input time.
`Kept::landing` and the trace fields are nullable and always present in the generated session codec: a missing key
is a decode error, and a snapshot is never persisted. `Snapshot` owns the pass rows and both trace row arrays.

`Session::step(budget)` runs live loudness, source clipping, the programme report, waveform, source analyzers, needles, and mastering. The budget counts **work units**, never milliseconds. A call
consumes at most `min(budget, 16)` units, reports the number consumed, and returns `More` while any job remains or
`Done` when none does. Zero units poll without progress. The shell measures its own speed and converts time to
units. The library has no clock. A measurement unit prepares one instrument, reads at most 1024 source frames,
publishes at most four newly decided clipping runs, or advances report finalization by at most 1024 entries.
The report drains reference true peak and scans tail energy, integrated gates, and short-term gates without changing
summation order. A master needs terminal tempo for CD targets or enabled Glue. While tempo is pending, its
own job schedules that analyzer after the current analyzer reaches a stage boundary, resumes a
stopped measurement if needed, and reports `Analyzers` progress with no render pass. The recipe
is captured once tempo becomes terminal. Other phase-two analyzers resume after the master;
targets without a tempo dependency start their passes immediately. Cancelling a waiting master
leaves the measurement running; cancelling that measurement also stops its waiting master.
Cancelling the needles job a waiting master waits for does not stop it: the master renders without the peak clipper
and its plan says `limiterUnmeasured`; cancelling the source's measurement while it waits for one of its results ends
the master.

Every completed unit publishes a phase. The source's `Analyzers` phase moves by `progress.analysis.weights`: the
weights of the analyzers that ended and the read share of the current one, against the sum of all nine — lowEnd120 (the
120 Hz run, and the infra-low run, the same geometry at another crossover), lowEnd150, forensics, stereo, crest, hum,
stereoBursts, tempo; so a 0.13 s stereo pass moves the bar a sliver and the 3 s crest a long stretch. The live
`Stream`/`Report` phases keep their own fractions. Master passes use `passWeight` against `expectedPasses * passWeight + measureWeight`; remeasurement
finishes at one. `weightsVersion` is the config's complete version, which includes progress weights. Fractions are
estimates; the interface permits them to move backwards. The human pass label uses only `pass`; `totalPasses` belongs
to the diagnostic journal. Completed and total work units are deterministic inputs for a shell's time estimate.

`events()` views the latest `apply()` or `step()` batch. The caller copies or consumes it before the next such call;
queries leave it intact. Each `Notification` is an independent value with `seq`, `jobId`, source hash, revision, state, phase, deterministic work, `kind`, and the payload
selected by kind. Sequence numbers count publications across loads and cancellations. No callback runs inside the
session. Event facts use `OwnedFact`: `assign(text::Fact)` copies up to 256 user-text bytes without allocation and
refuses excess whole; `view()` returns a `text::Fact` whose user-text views belong to that event. Copying or moving a
notification preserves its own bytes. Render with `text::Text::text(event.payload.fact.view(), lang)`. Rejected events
remain `{commandId, code}`: render their answer and original request with `text::Text::rejected(...)`, fact `100 + code`.
A step with no work returns `Done` without a publication or a `seq` change, even under a hostile FP environment.
Restoring the FP environment allows a refused job to resume. The fixed batch has room for the maximum three events per unit, declared inside `createBytes()`.

| kind | payload and publication |
|---|---|
| phase | phase name, weighted fraction, weights version, pass and work counters; every completed unit |
| fact | `text::FactId` and `text::Arg` arguments; measurement completion, each master pass, master readiness, cancellation |
| reading | new owned momentary and short-term points, fixed frame grid, window reasons, and detailed clipping runs |
| done | the completed master's id, immediately after its recipe is kept |
| rejected | command id and rejection code; the project, revision and work remain unchanged; `seq` advances |
| error | trap/contract/refusal/memory/poisoned/stale code, fact and arguments, byte demand and none/replay/continue recovery; an FP refusal publishes `Error{Refusal, Continue}` and leaves the job resumable |

An accepted load issues a measurement job id, shared by its two phases. Masters and loads use one monotonic id
sequence; the load answer returns its id and `measurementJob()` exposes the active measurement. `job()` retains its
meaning as the active master. Cancellation checks the named id after the state table. Cancelling measurement retains
the source, completed results, analyzer workspace and saved progress; unfinished results become `Cancelled`.
`continueMeasurement` gets a fresh job identity, restores unfinished results to `Pending`, and resumes at the saved
position without feeding silence or repeating a completed instrument. The snapshot publishes its availability and
the state it resumes. Stopped denotes cancellation before mandatory readiness. StoppedMeasured and MasteringStopped
denote ready sources with placed devices whose plan is Ready; their Unplaced counterparts, the same states while a
measurement the devices read runs.
Cancelling a master ends only that overlay.
Cancellation in either measured state, Stopped or StoppedMeasured returns `NoJob` only when no measurement, master or needles job runs:
needles start at the first phase's end, one unit before Measured1, so a measurement stopped there leaves them running in
Stopped, cancellable like anywhere else. A cancel's fact names the job it stopped — the measurement stopped, or a
master or needles job cancelled.
A target change after phase two can start needles; its active ID can be cancelled in `Measured2`.
A failed Driver transition publishes `Error{Contract, None}` and drops that job.
Every internal completion checks its captured job id, and measurement completions also check the captured source hash.
A stale completion changes nothing, including when the same samples are loaded again or a newer master runs.

`Snapshot` is a move-only immutable owned value. Its const view includes state, revision, both project layers, target
name, machine-layer differences, source metadata and identity, both jobs, the captured master recipe, kept masters, progress and reading arrays.
A retained snapshot survives later commands and destruction of its session. Calls, including snapshot acquisition,
remain on one thread at a time; a completed value can be handed to a shell independently. Demand sums widen each
term to `uint64_t` before addition. Copy traps before allocating, in every configuration, if combined text or
reading-point storage exceeds `size_t`; it never allocates a wrapped size.

`Measurements.h` adds owned analyzer results: named optional numbers with reasons, named numeric arrays with frame
grids, total/stored counters and completeness. Each source has one result per analyzer. The all-target plan retains
independent 120 Hz (`LowEnd`), 150 Hz (`LowEnd150`) and infra-low (`InfraLow`) readings, each with its own preparation
and result demand. `LowEnd150` is appended after the existing analyzer identities. `OwnedMeasurements::copy`
copies every name and row; snapshot copies therefore survive workspace destruction, replacement loads and session
destruction. A `Measurement` event owns the result identity, status, counters and revision; its numeric data is read
from the corresponding snapshot. The codec generator describes both forms, including transferable f64 rows.

The measurement key combines the PCM bit hash, actual native parameters, config version and core/session versions.
The target is absent from source-wide measurements. An identical load reuses PCM, saved progress and those results;
changing the target does not invalidate them. Forensics also reads the file's bit depth (its unused low bits), so its
result key mixes the measurement key with that depth: a reload of the same PCM under another depth recomputes those
bits from the retained grid, moves the forensics key alone, and publishes one `Measurement` event under the reload's
revision; an unchanged depth is the same result, unannounced. Needles have a separate source-and-ceiling identity. Each prepared analyzer stays resident across pauses and is destroyed after publishing its result. Live result arrays
belong to the session independently of analyzer scratch, so final publication needs no programme-sized copy. The live needles job consumes its
retained loudness and true-peak readings through the controller seam; see [NEEDLES.md](NEEDLES.md).

`MeasurementQuery` reads retained source data between pump steps on the session thread. The caller supplies
`audioId` (`Source::hash`), a half-open source-frame range `[fromFrame,toFrame)`, `columns` in `[1,2048]`, and
an exact uint64 `requestId`. A nonempty valid range has at most `min(columns,toFrame-fromFrame)` waveform buckets;
bucket `i` uses `fromFrame + floor((toFrame-fromFrame)*i/n)` to the next boundary. The last bucket includes the
last source frame. `fromFrame == toFrame` returns `Empty` with no rows; reversed or out-of-source ranges return
`InvalidRange`; a stale `audioId` returns `StaleSource`; invalid column counts return `ColumnLimit`. None clamps.
The response echoes the request and supplies the source id, session revision, measurement key, status, reason,
total/stored counts, completeness, source rate/channels, row stride and PCM frames read. Each `QueryResult` owns its
rows after cache eviction, source replacement or session destruction. Repeated equivalent queries share a bounded
two-entry cache; `requestId` is an answer label and does not change the measurement. Target edits do not remeasure
the source. The source/config/core identity and actual grid parameters distinguish cache entries.

The generated `Query*Row` tuples in `snapshot.d.ts` give column names. Their numeric units and order are:

| kind | row meaning |
|---|---|
| Waveform | `[firstFrame,lastFrame,axis,min,max,peak,envelope,rms,lowEnergy,middleEnergy,highEnergy,finiteFrames,reason]`; axes 0=L, 1=R, 2=Mid=(L+R)/2, 3=Side=(L-R)/2; mono has only 0 and 2. Amplitudes are linear, energies are sums of squares. Peak preserves either signed extremum; envelope is the maximum absolute box mean near 8 kHz. Bands use complementary 250/2500 Hz one-pole drawing filters and are not LR4 measurements. |
| LowSpectrum / LowSide | `[Hz,density,reason]` / `[Hz,sideFraction,reason]`; the requested Hz grid includes both endpoints (one column uses `fromHz`). LowSpectrum's quantity is the request's `spectrum`: `0` (the default, and what a request without the field gets) is DENSITY — the band's energy per hertz of its width, the tilt-free curve; `1` is ENERGY — the band's whole energy, what a bar per band shows, above the density by 10·log10(band width in Hz) dB (0.8 dB at the lowest band — 1.19 Hz wide at 20.6 Hz — and 11.6 dB at 250 Hz), row `[Hz,energy,reason]`. The retained full-source LowEnd measurement is selected by exact `crossoverHz` 120 or 150. These kinds require the whole-source frame range and a finite grid within Nyquist. |
| Momentary / ShortTerm | `[sourceFrame,LUFS,reason]` from the retained live grid, decimated to the requested maximum row count. `sourceFrame` is the window end: `(fromFrame,toFrame]` selects readings for the half-open audio range `[fromFrame,toFrame)`, including a reading at the source end. With a `masterId` (and the master's source `audioId`) the curve is that MASTER's: the job's own meter over the delivered audio, a row per 100 ms of it, reasoned the same way (the rows before a whole window are `TooShort`) — each reading bit for bit what the delivered audio gives measured as a source — and ASKED AND NAMED IN THE SOURCE'S FRAMES: the source's own request with a `masterId` added, its range refused past the source's end, `sampleRate` the source's rate, so on A/B the master's rows lie under the source's. How the delivered audio lines up with the source: the renderer cuts the chain's latency off and keeps the length, so at the source's rate (a target with `sampleRate = 0`) delivered frame n is source frame n and the rows are the source's frame for frame; a converted delivery is `ceil(frames·delivery/source)` long and its frame n carries source time n/delivery within half a delivered sample (the converter's rational latency, trimmed to the nearest sample), so a row ending at delivered frame e is named `round(e·source/delivery)` — exactly the source's row when both rates are whole multiples of 100 Hz. |
| Clipping | `[firstFrame,frameCount,channel,sign,level,evidence]` for retained runs intersecting the range; `total` counts all matches, `stored` is capped by `columns`, and `complete` reports truncation. |
| Stereo | `[firstFrame,lastFrame,width,correlation,rms,reason]` from retained source stereo columns intersecting the requested range. Bounds name each retained column's actual source interval; `total` counts intersecting columns and `complete` is false when the column limit omits some. |
| LimiterGr / PeakClipGr | `[firstFrame,lastFrame,minDb,maxDb,meanDb,samples,nonFinite]` on the delivered-frame grid. These require `masterId`, the master's source `audioId`, and a nonempty range inside the trace. A zoom selects every retained bucket intersecting the range; requested columns group those buckets and report their actual bounds, including edge buckets that cross the requested bounds. `total` is the selected bucket count, `stored` is the returned count, and `complete` is false for a reduced column count. An unfinished render returns `Pending` without rows. `sampleRate` is the delivery rate. |

| GlueGr | The glue's gain reduction over time: the compressor's own |GR| tap from the delivered render (the render whose audio is the master), rows as LimiterGr's, on LimiterGr's very buckets — the same bounds, the same zoom and grouping, `sampleRate` the delivery rate. The tap is the compressor's detector, read for the input sample its gain was decided for, and it does not move with a parallel mix. Its largest bucket is the report's `glueMaxDb`. A master whose glue did not compress (out of the chain, or bypassed) keeps no trace and answers `Unavailable` with reason `NoSignal`, the reason its report gives the glue's numbers; an unknown master is `Unavailable` / `Unsupported`, as for every master kind. The trace is in the master's declared memory, a third bucket row beside the other two. |
| SaturationShave | What the saturation (the chain's soft clipper) took off the peaks over time, in dB ≥ 0, from the delivered render, rows as LimiterGr's on LimiterGr's very buckets (`sampleRate` the delivery rate). Per internal quantum of the chain (256 frames): the peak of the stage's input, as its aligner holds it — delayed by the stage's own latency, so a peak and its shaved self are in the same quantum — times the stage's clean gain (what it multiplies a sound too small to bend by: the dry share, and the wet one at the shape's slope at zero under its drive compensation, times the output trim — `MasteringChain::clipperQuietGain`), against the peak of its output, floored at 0: only what the curvature took, never a level change. It is the pair the report's `saturationCutMaxDb` reads, so its largest bucket is that number. Every frame of a quantum carries its value: a bucket's `maxDb` is its loudest quantum's shave (for a static shape the bucket's own input peak plus clean gain minus output peak), `meanDb` the frames' mean, `minDb` the least. Peaks are read at the base rate: the oversampled copies live inside the Saturator, and the limiter behind reconstructs its own peaks from this very output. A quantum whose input peak is under −72 dBFS reads 0. On a clean 1 kHz sine under the knee it reads under 0.0001 dB, tanh and tape alike (tape's emphasis filters around the bend move nothing a floor at 0 lets through). The soft clipper sits after the glue (the compressor) and before the landing gain, the limiter and the limiter's peak clipper: gate → input gain → EQ → mono bass → glue → saturation → landing gain → limiter (peak clipper inside it) → dither. A master whose soft clipper did not shape (out of the chain, or bypassed) keeps no trace and answers `Unavailable` with reason `NoSignal`, the reason its report gives the saturation's cut; an unknown master is `Unavailable` / `Unsupported`. The trace is in the master's declared memory, a bucket row beside the others. |
| MasterWaveform | `[firstFrame,lastFrame,channel,min,max,rms,finiteFrames]`, L then R, from the master's retained buckets (at most 2048 a channel). |
| MasterAxes | The same buckets in the source Waveform's own shape — four axes a column, rows of 13 as Waveform's — kept beside them by the job with the waveform's own arithmetic (`analysis::WaveformStream`). A bucket is, to the bit, what the source's Waveform answers for those frames of the delivered audio (RMS and band energies to a rounding); columns merge buckets, and a merged column's envelope is the largest of its buckets'. On a caller-supplied chunk (`masterWaveformChunk`) the band split starts from rest at the chunk's first frame. |
| MasterReport | No rows: `master` is the kept master whole — the record a full snapshot carries for it, its traces, crest rows and mask, sections and waveform buckets included — in storage of the answer's own. `columns` and the frame range are not read. `query_size` bounds its JSON by the record as the wire writes it. |

Waveform inner buckets combine completed index nodes; only two edge leaves can replay resident PCM. Its index owns
the multiresolution columns and filter checkpoints with no second PCM copy. `pcmFramesRead` exposes exact-edge work;
other query kinds read retained results and report zero. A waveform range not yet built is `Pending`; a stopped
unfinished range is `Cancelled`. A row's reason distinguishes absent mono axes (`Unsupported`), nonfinite gaps
(`NonFinite`) and finite silence (`NoSignal`); unavailable instruments retain their own reason. `summary()` and
`fc_session_summary_*` keep scalar snapshot status while omitting large measurement rows and mark
`measurementRowsIncluded=false`. The full snapshot remains available on its existing entry points.

**The lean summary.** A summary still carries every kept master whole, and a master is heavy: its two reduction
traces, its crest rows and its waveform buckets are megabytes of JSON — 1.28 MB with one 12-second master, 8.6 MB with
seven. A shell that sets `Capabilities::leanSummary` (C: `leanSummary = 1`, a field appended to
`fc_session_capabilities`, 0 or 1)
gets summaries without those rows: each master keeps its scalars — id, recipe, landing status, passes and readings, the
report's readings and hints, every cost value — with its pass log and its cost sections; its traces are absent, its
crest rows, mask and waveform buckets empty, and the summary says `masterRowsIncluded=false` (62 KB with one master,
88 KB with seven: 4.4 KB a master). One master whole is `QueryKind::MasterReport`; its traces and waveform are also the
`LimiterGr`, `PeakClipGr`, `MasterWaveform` and `MasterAxes` queries; the glue's trace (`GlueGr`), the saturation's shave (`SaturationShave`) and the loudness curves are kept with the master for the queries alone and are in no snapshot. `fc_session_snapshot_*` is the same in both
modes. A shell reads a summary after each command's answer and the full snapshot when it needs everything at once;
nothing in the session asks for a snapshot. The room for the lean masters is the session's (`sizeof (Kept)` a master,
in the master command's declared demand) and sizing or copying a summary allocates nothing. A decoded snapshot is held
to its flag: whole masters under `masterRowsIncluded=false`, or stripped ones under `true`, are refused.

`measurementStorage` exposes source, result, workspace, copy, codec and allocator demands and the maximum live
set across load and work. Native `storageFor` calls share the exact parameter records used for preparation, including
the separate loudness meter and waveform/stereo columns. Nested spectrum and band-burst storage is included only
through its parent analyzer. The whole-work estimate includes only the pump's declared streaming instruments:
loudness, clipping and programme report. They coexist with retained row buffers, one detached snapshot and its
serialization. Scalar metadata already inside the live output object is counted once. The codec reserve covers the web transport's binary f64 rows and JSON metadata. Exact scalar decimals use
the codec writer's maximum width; text uses its escaping bound. Optional plain-JSON exports query their exact
`Codec::encodedBytes` separately and are not reserved for every load. Per-analyzer prices remain available for separate preparations;
scheduling another analyzer must declare its lifetime before it runs. Burst, distinct-value and hum capacities
are bounded by the source's possible observations without changing analysis thresholds or arithmetic.
Load demand takes the maximum of old storage plus caller input, and new storage plus its input copy: old and
replacement PCM copies do not coexist. Source-name copies remain included. The largest-block demand is conservative; capabilities remain the shell's limits.
Embedded TOML views allocate nothing; runtime config/project parsing keeps its separate declared TOML budget.
There is no excursion index. Needles are re-measured over retained PCM for one ceiling, with a separate
`needlesStorage` / `fc_session_needles_bytes` demand. Preparation checks capacity before allocating;
all subsequent read and finish units allocate zero. Native run-list truncation is explicit in the snapshot.

`tools/session-codec-schema.json` is the one hand-edited codec description; the call status values, which the C header
already declares, are not copied into it — `SessionStatus` is generated from `fc_session_status`. `Codec` exchanges its named-field JSON.
Object order is immaterial; missing,
duplicate, unknown and ill-typed fields are refused. Rows are arrays. Optional edits use null; uint64 identities use
decimal strings so JavaScript loses no bits; byte counts use whole double values strictly below 2^53. Non-finite doubles
use the explicit strings `"-Infinity"`, `"Infinity"`, `"NaN"`. Finite doubles use exact decimal numbers, with bounded
integer conversion and one ties-to-even rounding on decode; signed zero survives and NaN payloads are intentionally
not represented. Numeric tokens are bounded to 1100 characters. This is a snapshot exchange codec, not a project file.

The description generates the compiled field walk (`src/CodecSchema.h`) and `snapshot.d.ts` in the build's
`modules/session/` directory. Every build verifies the checked-in walk against the description; the declaration test
compares the generated TypeScript with that same description. Generated structured bindings hold every record's arity,
field assertions hold its types, and each described enum value has its numeric assertion; an undescribed enum fails
compilation. An exhaustive generated switch with no default catches appended members: the session always compiles
with `-Werror=switch` on gcc/clang/emscripten and `/we4062` on MSVC, including consumer builds without tests. Offline Node
checks actual encoded fixtures against `snapshot.d.ts`, covering every wire mapping, optional alternative and record.
Compile controls add a field, reorder an enum, append to each described enum and request an undescribed enum; all must
fail. `tools/wasm/build.sh` also emits `snapshot.d.ts` beside the modules. Regenerate the field walk with
`cmake -DUPDATE=ON -P tools/session-codec.cmake`.

| operation | demand before work | evidence |
|---|---|---|
| step, event/query access | `stepBytes() == 0` for fixed work; needles preparation uses `needlesStorage`; batch included in create | event and needles allocation counters |
| snapshot | `snapshotBytes() = Snapshot::storageFor(buildView())`; the same view passed to `Snapshot::copy` | event suite, retained value after session destruction |
| snapshot copy | `Snapshot::storageFor(view)`; exact text, masters and rows | event suite, all array types |
| encode | `Codec::encodedBytes(view)` caller buffer; zero heap demand | exact-size and short-buffer tests |
| decode | `Codec::decodedBytes(json)`; complete validation before exact arrays | round trip, invalid-input refusal, allocation counter |

`felitronics_session_event_tests` holds every command-table cell between actual pump calls, immediate fact publication,
cancellation and continued use, stale completions, and complete event fingerprints across runs and work slicing. The
fixture is the suite's four synthetic samples at 48 kHz (source hash `0ba6b096abb7c779`) and the embedded config; regenerate its fingerprints by running
the suite and reviewing changes against those inputs. The pinned event hashes (`820a89aab5c3d074`, `9b331789613b86ea`) include all active event payload fields and
the weights version. The same executable and fixtures run native and wasm. The codec suite covers retained snapshots,
both project layers, all reading arrays, silence, gaps, finite exponent extremes, signed zero, and deterministic
binary64 samples. The object gate admits Apple's compiler-generated `__chkstk_darwin` stack probe for the bounded
numeric scratch; no clock, locale or floating parser is admitted.

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
  the stack protector, the toolchain's own markers, the sanitizer runtime on the sanitizer row, and — in an MSVC Debug
  build (/MDd) alone, the `coff-debug` scope — the debug runtime's `_CrtDbgReport` and `_dtest` and the debug STL's
  `std::_Lockit`, each by its exact decorated name (a lock around the STL's iterator bookkeeping, not a thread: the session
  creates none). A **local** definition answers no other object's call — the linker never resolves one with it — so a
  `static getpid` in one object leaves another object's `getpid` call refused. `printf`, `fopen`, `time`, `getenv`, `strtod`, `isalpha`, `rand` — or
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
file nothing compiles or includes; the config's two documents and the text's two are data, named as such. Only `modules/session/tests` is
outside it. `--build` also names the felitronics-core and felitronics-toml checkouts the lint resolves admitted headers in
(or their sibling checkouts).

Before any rule the text goes through **translation phase 2** — every backslash-newline joined, a line map kept — and
the lexer consumes identifiers and preprocessing numbers whole, so a keyword split across a continuation is read whole
and `u8'0'` is a character literal, not a digit separator. Its rules:

- **includes** on an allowlist: canonically spelled standard headers that reach no OS, file, locale, thread, clock or
  process state; felitronics headers **by name** (`FELITRONICS_ALLOWED`: core's `FlushToZero.h` sets flush-to-zero with
  no symbol and several core headers pull in `<atomic>`, so each is a reviewed one-line addition — today the module's
  own `Session.h`, `Config.h`, `Commands.h`, `Project.h` and `Text.h`, felitronics-toml's `Toml.h`, `Schema.h` and `Embedded.h`, and the three analyzers the
  config's schema asks what they admit, `LowEnd.h`, `BandCrest.h` and `StereoBandBursts.h`, which bring core's DSP and
  `FlushToZero.h` with it; the schema calls only their `storageFor()`); quoted headers inside the module, and in
  `src/Config.cpp` the three headers the build generates from the config, and in `src/Text.cpp` the two it generates from
  the text, by name;
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

`tools/fc_session_abi.h`, implemented by `tools/wasm/fc_session.cpp`, freezes **version 1**. Existing
signatures, constants, enum values and C layouts remain unchanged; additions are allowed. The facade follows
fc_master's law: handles, address checks, a status per call and permanent poison. Commands, capability enforcement
and serialization belong to the compiled C++ library, including `Wire.h`, so a desktop can use the same behavior.

`create(capabilities, configVersion)` takes the shell's exact `heapCeilingBytes` (a double below 2^53), `maxRateHz`,
`largestFreeBlockBytes`, and `offeredDevices` bit set. `fc_session_create_bytes` publishes its allocation demand before creation, even when
the ceiling cannot afford it. Creation refuses an incompatible config with `FC_SESSION_ERR_CONFIG_VERSION`.
The generated `snapshot.d.ts` declares `FC_SESSION_CONFIG_VERSION` as a literal hash; `snapshot.mjs` exports its
runtime value. Both come from the build's config gate. The library's no-argument `Session::create()` selects its
compiled config with unrestricted capabilities; the overload accepts the shell's explicit inputs.

The session counts its own live declared allocations plus each command's declared demand before work. A memory
refusal preserves state and revision, allocates nothing, and publishes `ErrorCode::Memory` with exact `needBytes`.
It precedes a load's sample scan and an import's parser. Rates above `maxRateHz` receive `Rejection::RateAboveLimit`.
A device excluded by the shell stays inactive, cannot be edited or reverted, and cannot be activated by project import.
The snapshot's `offeredDevices` tells the shell which devices it can expose; the existing target/source restrictions
still apply. Convert, Lra and Final are appended to `PhaseName` at 5, 6 and 7 and have catalog facts in ru and en.

| Entry points (`fc_session_` prefix) | Contract |
| --- | --- |
| `abi_version`, `config_version` | ABI number; config hash as low/high uint32 halves |
| `create_bytes`, `create`, `destroy` | Pre-create demand; capability/config creation; generation-checked destruction |
| `set_capacity` | Update heap ceiling and largest free block between calls |
| `command_bytes`, `load_bytes`, `import_project_bytes` | Session allocation demand and live bytes before work |
| `measurement_bytes` | Shape-only detailed measurement demand in an appended size-prefixed record |
| `command` | Named-field JSON in; accepted/rejected JSON out |
| `load` | Planar f32 pointers, channels, frames, rate and JSON metadata; owned PCM copy |
| `import_project`, `export_project_size`, `export_project_copy` | Project bytes in caller buffers |
| `step` | Work units, bounded by `kStepUnits`; zero polls; More or Done |
| `events_size`, `events_copy` | The current batch as tagged JSON and f64 rows |
| `snapshot_size`, `snapshot_copy` | Current snapshot as named JSON and f64 rows |

For example, `{"kind":"editDevice","commandId":"17","device":0,"fields":{"fq":32}}` edits the HPF frequency.
Command identities are decimal strings. Fields can arrive in any order; unknown, missing, duplicate or malformed
fields produce a `rejected` answer naming the field, with its fact (`fact`, last). Device edits omit untouched knobs or use
null; in `editTarget` a null clears the field (the target row's number again) and an absent key leaves it. `load` metadata requires
`name`, `fileRate`, `bitDepth`, and `rateKnown`. Load and import carry command identities as low/high uint32 arguments.
The page reserves `FC_SESSION_ANSWER_BYTES` before commands run; a short output cannot execute a command and then
lose its answer. `written` excludes a terminator. A domain rejection is an OK transport call with a rejected answer;
an import's syntax/schema refusal can allocate within its declared parsing demand.

Size queries and copies allocate nothing. No session pointer leaves a call. JSON row descriptors
`{byteOffset,length,stride}` point into a separate buffer aligned to 8 bytes, with capacity stated in bytes.
The page reads `Float64Array`: points have `[index,value]`, runs `[first,count,value]`, and machine differences
`[device,field,fileValue,coreValue]`. Only records such as retained recipes remain JSON arrays. Binary values preserve
IEEE-754 infinities and NaN; scalar JSON values use `"-Infinity"`, `"Infinity"`, and `"NaN"`. Indices and byte counters
remain exact below 2^53. `SessionEvent` is a generated union discriminated by `kind`. The snapshot-only recorded JSON
codec remains available for owned C++ fixtures; `Wire` supplies the transferable form to both native and wasm callers.

The header fixes the check order: poison; outputs in signature order (null, alignment, span); handle; inputs;
overlap; session checks. A non-OK status leaves every output untouched. Buffers are disjoint and caller-owned.
Size/copy pairs require no intervening mutation. Calls use one thread; callbacks must not reenter the facade.

The only mutable globals are the handle table and poison latch. Eight slots each issue 24-bit generations and retire
before wrapping. Release tests walk every generation; Debug uses a test-only seam to exercise the last generation and
retirement directly. Every status entry detects an abandoned or reentered call. A real wasm trap cannot return through
C; generated `invokeSession` maps it to `FC_SESSION_ERR_TRAP`. Subsequent calls return `FC_SESSION_ERR_POISONED` and
publish nothing; even destruction is refused. `abi_version` remains callable. Recovery uses a fresh module, the same
source, and the exported project. A reentered call also suppresses the outer call's output.

`tools/session-abi-v1.txt` is the frozen floor. `session-abi-check.mjs` generates a probe that compiles exact function
type assertions and emits constants, every public enum value, C struct sizes, alignment and field offsets. Each tier,
including wasm32, runs its own probe; codec field/type lines are frozen beside the C surface. The gate requires every
frozen line to remain and permits additions. Its control deletes and changes each line and requires rejection.
CI runs the gate and control. The Windows Debug selection includes the session ABI suites.
Every `fc_session_*` declaration is frozen whatever it returns: one the generator cannot read stops it. Two lines are
floors rather than values — a boundary struct's size and `FC_SESSION_ABI_VERSION`: after the first release, each batch
of additions that lands together in one release moves it up by one and adds one row to the header's VERSION HISTORY; a
lower number is a change. The generated `snapshot.d.ts` and `snapshot.mjs` state the version read from the header. The wire's
`SessionStatus` union is read from `fc_session_status` by both generators, and a wire record that mirrors a C struct
(`SessionCapabilities`) must carry each of its fields. The manifest itself only grows from its declared base: its line
`base v0.6.0` names the release it starts from, and on a pull request CI compares it with the base branch's
(`tools/session-abi-append-only.mjs`). Under the same declared base a removed or edited line is red; a manifest that
declares any other base is red unless the pair is in the script's own list of authorised resets — the owner's decision,
which a pull request cannot grant itself by editing the manifest. The list holds exactly one: v0.6.0 over a base branch
whose manifest declares none. It passes once — after it lands both sides declare it and append-only holds again.

**The one reset: v0.6.0 (`FC_SESSION_ABI_VERSION` 4, owner, 2026-10-01).** No project, snapshot or file of an older
version exists anywhere, the page has no project export, and the core's one consumer (the site) vendors the exact core —
so the manifest starts from a new base instead of carrying entries for data that never existed. What left it: the
project file's core stamp (`Project.core`, the `Version` record, `Rejection::ProjectCore` 27 and its fact 127), the
facts `DefaultsConverted` (9) and `SameCoreMachineDifferences` (10) — `MachineDifferences` (8) is the one fact for the
differences between a file's machine layer and today's planner, however they arose — and the saturation's output knob
(`SaturationFields.output`, field 3; the term `FieldSaturationOutput` 12 and its kit constant): the landing sets the gain
before the limiter, so any trim was undone. Live ids keep their numbers — 27, 9, 10, 127, 12 and the saturation's
field 3 now name nothing — and the snapshot and event fixtures in the manifest are the current ones. The base also takes
the capabilities struct at its current 40 bytes (`leanSummary` at 32) and the version floor at 4.

`tools/wasm/build.sh` builds `fcsession` from the facade and `modules/session/sources.txt`, audits the exact export list,
compares node/web wasm bytes, checks for threads, and runs a node scenario through the ABI and generated types.
Controls reject an extra export and verify a real allocation trap followed by permanent poison. The wire fixture
checks all six event kinds and nonempty binary rows against the generated declarations. Full native/wasm scenario
parity is a separate contract suite.

### The pure kit — `fc_kit_*`

Some answers a shell needs within one frame, on its UI thread, without a round trip to the worker that runs the session
(architecture §4.5): a published fact as text in the page's language, what a person typed into a knob's field, where a
value stands along the knob's travel and how far it is from the knob's comfortable window, mono bass's purpose zones, the
EQ curve a set of knob values would draw, a low-end curve from band energies. `felitronics::session::Kit`
(`include/felitronics/session/Kit.h`) answers them: static, stateless, allocation-free functions over the caller's spans,
each delegating to the code the session itself uses — `Text::write` behind the codec's own fact reader, `Text::parse` on
the commands' domains (`Knob::accepts`, `Rules::slope`) and the config's grid, the config's travels, green windows
(`[edit] lufs/tp green`), comfort (`[hpf] comfort`) and normal ranges (`[tilt]`/`[low] normal`, out to `hard`), the
zones (`[monoBass.zones]`), the EQ stage's `writeEq` / `eqCurve` / `eqFinding`, det-math's `log10`. The plan's comfort and
zones advice reads its comparison from `Kit::heat` and `Kit::monoZonesAt`, so a knob's colour and the advice beside it
cannot disagree. A field is its `text::Term` id (`FieldTargetLufs` … `FieldLowDb`), the id a refusal already names it by.

**The saturation's transfer curve** (`Kit::saturationCurve`, `fc_kit_saturation_curve`, slice 5): 129 inputs from −1 to +1
of full scale a 64th apart, each with what the chain's saturator settled on a held level gives for it — the stage's own
design arithmetic, `mastering::MasteringChain::clipperDesign` (the one `clipperQuietGain` reads), on the parameters the
session writes the stage with (`detail::clipperParams`, writeDynamics's): the core's WaveShaper at the type and
k = 10^(drive/20) − 1, the config's bias, its drive compensation, the dry/wet blend, no trim. For the five types a person may
pick; the transformer and tape as their static cores (the flux follows history, the emphasis frequency; at a held level
tape is its core). The drive is the shaper's own — the plan's `saturation.driveDb`, or the knob for an input peaking at
0 dBTP. Its tanh is the platform's, as the chain's is, so the curve is not in the pinned corpus: the kit suite holds it to
the core's WaveShaper bit for bit and the running stage, held at each level, to within 1e-4; the wasm check to
JavaScript's tanh within 1e-5.

The C boundary carries them as `fc_kit_text`, `fc_kit_parse`, `fc_kit_travel`, `fc_kit_position`, `fc_kit_value_at`,
`fc_kit_heat`, `fc_kit_mono_zones`, `fc_kit_mono_zones_at`, `fc_kit_eq_curve`, `fc_kit_low_end_curve` and
`fc_kit_saturation_curve` in the same
fcsession module, which a page instantiates a second time on its main thread: no handle, the poison, the argument order
and the statuses of every `fc_session_*` call, a language by its code. The kit holds no session state — it reads only the
compiled-in config and catalogue — so neither contract's scenarios move; the WAV recording records the module's hash and
the heap it observed, which every change of the module moves. `felitronics_session_kit_tests` holds each answer to the
session's own path and each export to its C++ call, and pins one hash of a corpus of answers that
`tools/wasm/session-check.mjs` reproduces on the wasm module (native == wasm, byte for byte). The entry points are in the
ABI manifest and on the module's export list; `session-check.mjs` lists them as not yet released, under version 3, until
the release that publishes them moves `FC_SESSION_ABI_VERSION`.

## The native CLI — `fcore_session`

`fcore_session version` prints the two releases and the ABI version; `fcore_session config targets|engine` prints a
document of the embedded config through felitronics-toml's canonical writer (its numbers, without the comments), and
`fcore_session config version|sound-version` its versions; `fcore_session table` prints who may do what, when — the
tables of `Commands.h`, as Markdown, which ctest holds to the block of this document. The CLI links the library as
C++, the way a desktop application does.

Knob travel and step describe the shell's slider. Commands and project import accept the domains below, including
values between steps and beyond travel. An empty edit or revert is accepted with unchanged revision. Device edits
require placement and an offered device. Panel visibility does not gate them. Low is offered on every target,
a file's machine layer is retained, and any defaults label but the current one is refused.

| Knob | Accepted domain | Reason |
| --- | --- | --- |
| Target LUFS | Any finite binary64 | Landing reports unreachable targets |
| Target ceiling | −6 through −0.1 dBTP | Product |
| HPF frequency | 0 < f < source sample rate / 2 | Device |
| HPF slope | Multiples of 6, 6 through 96 dB/oct | Device, orders 1–16 |
| Mono-bass frequency / width | 60–300 Hz / 0–1 | Product / device |
| Glue | 0–6 dB | Product |
| Saturation drive / mix | 0–12 dB / 0–1 | Product / device |
| Saturation type (by hand) | tanh, tube, transistor, transformer, tape | Product |
| Tilt / low | −6–6 dB | Product |
| Needles above ceiling | 0–6 dB | Product |

Limiter release is configuration, not a project knob. Its schema requires at least eight source samples at the
minimum supported rate. The schema checks travel within domains, defaults within domains, and positive progress-weight
sums. Project numeric fields use ordinary TOML numbers when its decimal subset can represent them exactly; other
finite binary64 values use quoted shortest round-trip decimals, parsed with the same exact numeric reader as JSON.

The C records `fc_session_capabilities`, `fc_session_sizes`, `fc_session_capacity`, `fc_session_storage` and
`fc_session_measurement_storage` start with the caller's `sizeof`. Their base sizes at v0.6.0 are 40, 12, 24, 32 and 88
bytes — the capabilities with `leanSummary` (a 32-byte record is too small, owner 2026-10-01), the measurement demand
without the retired slots; fields may only be appended. Sizes below the base and beyond the current build have distinct
statuses. A field appended later must define its absent-field meaning for callers of the base. Demand queries for commands, loads and imports use the session's own `storageFor`; capacity
may be updated between calls as a heap ceiling and largest free block. Live bytes plus demand and the largest allocation
are checked before work. Nonallocating commands remain available when capacity is reduced.

C load frames are uint32 because the wasm heap is limited to 2 GiB; native C++ PCM frames are uint64. Binary rows are
little-endian IEEE-754 f64, with unsupported byte-order builds refused at compile time. Executed encoder fixtures and
compiled layout facts freeze every event kind, snapshots, answers, row order, offsets, strides and C signatures. Local
source controls mutate each class of fact and require comparison to fail, while append-only additions pass.

### Contract scenarios: one script, two consumers

`fcore_session run <script>` reads the entire script before executing it (`-` reads stdin). Empty scripts and comments
still answer `done 0`. Syntax errors return 2 with a line number and no stdout. Runtime harness errors return 2 and
may follow an emitted prefix; a domain refusal is a successful command exchange, recorded through the codec.
`fcore_session parse <script|->` prints instructions without creating a session.

The line grammar lives in `tools/contract/grammar.json`. It generates the native parser's rules and is read directly
by the Node parser. `grammar-cases.json` holds both parsers to identical instructions and identical refusals. Spaces
inside commands are literal; leading/trailing ASCII whitespace is ignored. `#` starts a comment, including inside
JSON: encode a literal hash as `\u0023`. Names match `[a-z][a-z0-9_-]*`; command/job IDs and budgets have at most nine
decimal digits, capacity byte counts ten. Codec JSON commands retain the ABI's full string command IDs.

```text
# Fixtures are ../fixtures/<name>.pcm relative to the script.
create main 67108864
load 1 mono
step 5 work units
snapshot
cancel 2 1
load 3 mono
step 5 work units
step 5 work units
place phase 1
place phase 2
command {"kind":"setManual","commandId":"4","on":true}
export project saved
new instance
create main 67108864
load 1 mono
step 5 work units
step 5 work units
place phase 1
place phase 2
import project 2 saved
snapshot
```

- `create <name> <heapCeilingBytes>` creates and selects a session; `use <name>` switches between live sessions.
- `capacity <heapCeilingBytes> <largestFreeBlockBytes>` updates available capacity between calls.
- `load <commandId> <fixture>` copies planar synthetic PCM; `command <JSON>` forwards an ABI command unchanged.
- `step <budget> work units` makes one bounded pump call; zero polls. Subsequent commands are consumed in script
  order between pump calls. `cancel <commandId> <jobId>` is shorthand for the named JSON command.
- `drive <budget> work units` repeats bounded calls until the budget is spent or work is done. `summary` and
  `query <JSON>` copy the generated summary and bounded query transport, including f64 rows.
- `place phase 1|2` calls the session's internal transition through a contract-only seam. It keeps the slice 0
  device-edit and project scenarios executable without running the analyzers; the production measurement phase places
  the devices itself. The seam's second phase ends the source analyzers it did not run, as unavailable.
  Only the separate contract Wasm artifact exports this seam; measurement scenarios use the shipped module.
- `snapshot` copies the current snapshot. Every live session also emits a final snapshot, sorted by name.
- `export project <name>` stores canonical project bytes outside the instance; `import project <commandId> <name>`
  restores them. `import project <commandId> file <fixture>` reads a generated TOML fixture from `../fixtures`.
  `new instance` discards sessions while retaining exported project copies.
- `poison` abandons an actual facade allocation. Native uses the same terminate-handler technique as the replay
  suite and continues with a new C++ owner. Wasm uses the production facade with an armable allocator in the
  separate `contract-trap` test artifact, then reloads the shipped module. Both verify permanent poison and untouched
  outputs. No failure injection or poison reset is exported by the shipped module.

The seventeen scenarios cover load/measure, whole refusal in a forbidden state, cancel during measurement and mastering,
reset every device edit on a target change, poison/replay, interleaved sessions, memory refusal, project round trip,
knob domains and no-op edits, and a project whose machine layer differs from today's planner. Device edits work with manual mode hidden;
`low` is edited and reset across target changes, with the hand-edit count returning to zero. Domain checks include
fractional values beyond slider travel, Nyquist refusal, invalid slopes, and an empty edit/revert preserving the whole snapshot.
Memory coverage supplies a zero `heapCeilingBytes`: create refuses before allocating a session or starting work;
an adequately provisioned session then accepts a nonallocating command with capacity reduced to zero, restores its
capacity and completes measurement. Before every creation, command, load and import, each consumer queries demand.
The command/load/import queries must preserve both the event batch and snapshot. Prices and live bytes are checked
locally in each consumer because native and wasm object layouts differ; every emitted answer and event is compared
unchanged. The slice 0 placement seam preserves those scenarios' original transitions; the measurement scenarios run
real analyzers and their own status and query checks. Saved machine differences supply nonempty binary rows through
size-query/copy and `Float64Array` reads. The wasm consumer initializes every v1 record's size prefix.

Run from the repository root (use the core and TOML checkouts resolved by your CMake build):

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target fcore_session
# With emsdk on PATH and FELITRONICS_CORE_DIR / FELITRONICS_TOML_DIR set:
tools/wasm/build.sh
node tools/contract/run.mjs build-release/tools/fcore_session tools/wasm/build/fcsession.node.js
node tools/contract/run.mjs build-release/tools/fcore_session --native-only
node tools/contract/run.mjs build-release/tools/fcore_session tools/wasm/build/fcsession.node.js --controls
node tools/contract/run.mjs build/tools/fcore_session tools/wasm/build/fcsession.node.js --rebuild-recordings
```

What the comparison proves, and what it does not: the brain answers byte for byte the same natively and in wasm, and
the wasm module's C facade carries it unchanged. The native consumer calls the C++ session directly, past
`fc_session`, so the NATIVE facade's argument order, guards and status mapping are not compared here — they are held
by `felitronics_session_abi_v1_tests` and `felitronics_session_abi_tests` (`tools/tests/SessionAbiV1Tests.cpp`,
`SessionAbiTests.cpp`), natively and under the sanitizers.

Both consumers emit tab-separated session name, record kind, exact codec JSON (project text is hex), and hex row bytes.
Native calls `Wire` over its C++ session; Node drives the C ABI, copies every event batch before another mutation, and
copies snapshots using the size-query/copy entry points. It reacquires heap views after calls and reads copied row
buffers as `Float64Array`. The generated `.d.ts` validates the received unions. Comparison uses the complete output
buffers, without JSON reserialization, float tolerance, sorting, or identity removal, except native/Wasm object-layout
budget fields; source and allocator bytes remain compared. A mismatch identifies the first
differing event's zero-based index and kinds, and prints both original encodings and row bytes. Other record mismatches
name the record. Each scenario also has handwritten behavior assertions, so two agreeing empty or refused runs fail.

Fixtures contain generated, planar signed integers divided by 32768, a synthetic saved project and measured sidecar; no third-party
audio or host trigonometry.
`node tools/contract/fixtures.mjs --rebuild` rebuilds them and their manifest. The manifest records that command, the
SHA-256 of the generator and input specification, and each output hash. Every contract run verifies these before using
fixtures: changing an input without rebuilding, or damaging an output, is red. No scenario output is silently recorded as
an expected result. To extend coverage, add a `.session` and its handwritten `.expect.json` beside the existing scenarios;
all are discovered automatically. Extend the grammar and its acceptance/refusal corpus together.

`--reorder` intentionally swaps the first pair of events in one wasm batch and exits nonzero with the mismatch.
`--controls` runs that command in a child process and requires exit 1 with the diagnostic, then checks a flipped byte
in a nonempty measurement row, a valid scalar change and stale/damaged fixture refusals; its temporary
fixture mutations clean up locally. CI has a separate comparison step on every PR, and reuses the wasm artifact across
Linux x64/arm64, macOS and Windows native rows. Each run prints elapsed comparison-step time (build time is separate).

On build hosts that forbid deletion, set `FELITRONICS_WASM_KEEP_CONTROLS=1` for `tools/wasm/build.sh` so its
source-list control directory is retained. Run the contract's self-cleaning `--controls` only locally.


Live loudness arrays use a 100 ms cadence of ten rounded 10 ms analyzer sub-hops: `firstFrame == stepFrames`,
point zero ends at that frame, and point indices never change. Momentary and short-term windows cover four and thirty
hops. Before a complete window, values are NaN with TooShort; silence is negative infinity with NoSignal. The parallel
reason arrays preserve each point's status. `framesRead` and `tailFrames` distinguish an unfinished window from the
final off-grid tail; no padded point is manufactured. `reading.finished` closes stream delivery, while the report
remains pending until its own finalization completes.

Preparation reserves allocator overhead before each instrument starts and retains that reserve until its workspace
or rows are freed. A capacity reduction between load and preparation can therefore refuse before any allocation,
including MSVC Debug vector padding. A refusal is that instrument's outcome, as in the source phase: its result is
`Unavailable` for `Memory`, one `Error{Memory, Continue}` names the demand, and the job goes on to the next
preparation — capacity restored later does not retry it, and a capacity never restored still ends the job. The
report's integrated loudness and true peak alone decide the mandatory readings: without the report the loudness result
is `Unavailable` for `Memory` and the source stays unmeasured; without the meter only its rows are missing — the result
is `Ready`, its row arrays and the result itself incomplete, with one `measurementCapacity` fact. Duration-sized loudness and report stores initialize observations as written;
preparation resets counts and fixed rings without scanning those stores. `StreamingLoudnessMeter` preserves the
deterministic core v0.55 kernel and storage geometry with this offline ownership policy; direct-kernel comparisons
cover block energies, readings, damaged input, channel changes and resets on native and wasm.

Every event's phase and work counters belong to its emitting job. Phase envelopes agree with their payload;
completion and cancellation preserve that job's final progress even after its active ID is cleared. Command-level
facts, errors and rejections without a job carry zero work. Reading and report publications describe the completed
pump unit, independently of concurrent needles or mastering work.

The additive `reading.clips` scalar row groups every six values as start, length, channel, sign, level, and native
ClipEvidence. Existing `runs` retain their three-column prefix. Snapshot clipping arrays declare six columns;
total and stored counts distinguish exact aggregate counts from a truncated coordinate list. The source-damage
detector is independent of target-dependent needles. The programme result retains every native value, count, and
reason, including unconditional short-term P95 and the drained programme true peak. Load still refuses non-finite PCM;
finite input that overflows an analyzer is reported with its native damage reason.

`Measured1` comes as soon as the programme's loudness and true peak are measured (owner, 02.10) — both must be usable —
and before any of the source's own runs: the needles are asked, the devices placed, a field a run decides "not measured
yet" until it ends. The runs follow in this order: the 120 Hz and 150 Hz low-end readings first — the high-pass reads the
first, mono bass the one at the target's crossover, so a change of target finds its run next or done and nothing is
measured twice — then the findings: the configured infra-low LR4 reading, channel forensics, stereo columns, crest, hum,
Mid/Side bursts and tempo. A needed tempo goes ahead of a finding, never ahead of a low-end run a device reads. Optional
outcomes carry their own reasons. Editing needs only the placement; an export and an import also wait until no device
field is not measured yet (`PlanPending`). Master may run while the source's runs
continue, subject to what its devices read (the plan): with the panel open it waits for them (`PlanPending`), with the
panel hidden it waits in its own job, its recipe's machine fields placed again as each run ends, and sounds as one asked
after everything ended (`felitronics_session_hpf_mono_tests`, the same WAV bytes). These results alone do not establish
`Measured2`: tempo and the second-phase join remain separate work.

`mandatoryMeasurementsReady` and `devicesPlaced` are separate appended snapshot fields.
Source rows use the existing owned `MeasurementResult` transport. They remain valid in a copied
snapshot after workspace release, cancellation, source replacement and session destruction.
Target changes preserve source measurements and request only the existing target-dependent
needles job. Continue resumes the saved analyzer, PCM offset and output-copy position.

| Analyzer / array | Columns, in order |
| --- | --- |
| LowEnd / blocks | start, samples, finite samples, holes, Mid energy, Side energy, valid, index |
| LowEnd / bands | MIDI, centre Hz, width Hz, bins per band, Mid/Side/total energy, density, centroid Hz, cents, Side fraction, duty count, duty, level when on dB, margin dB, resolved |
| LowEnd / sideHistogram | count per Side-fraction bin |
| Forensics / meanPower | one column per channel, frequency bin order |
| Forensics / gridExponentHistogram | one column per channel, native exponent-bucket order |
| Stereo / columns | width, correlation, RMS; native equal-time column partition |
| Crest / blocks | linear peak and mean square for Low, LowMid, HighMid, High, Full |
| Crest / active | source activity bit for each of those five bands |
| Crest / oversampledMeanSquare | full-band power in the band-share gate's domain |
| Hum / candidates | channel, nominal Hz, base found/harmonic, fundamental Hz/observed, observed/lowest harmonic, comb without base/Hz, stretch observations/off-tolerance, frame observations, stretch/frame/intra-stretch spread Hz, stationary, passed; then base and window-peak records (found, prominent, accepted, bin, Hz, tone power, bin power, floor power, prominence dB) |
| Hum / harmonics | channel, candidate, harmonic, in band; native peak record as above |
| Hum / stretches | channel, index, start frame, end frame, selected frames |
| Bursts / midEvents, sideEvents | start, length, peak frame, peak power/baseline/excess dB/wide power, energy, hops, input damage, baseline damage, closed by finish, other-axis power/baseline/eligible/hop |
| Bursts / midIntervals, sideIntervals | interval count and lag count, lags 1 through 512 |
| Tempo / candidates | BPM, score; no time grid, because candidates describe the whole source |
| Tempo / curve | rounded seconds, smoothed BPM/confidence/present, raw BPM/confidence; grid starts at the first window centre and advances by the actual detector hop in source frames |

Every list publishes total/stored counts and completeness; capped low-end blocks and burst
lists never claim completeness. Forensics does not infer a codec from missing evidence.
Hum exports the detector's stationarity and acceptance evidence, without inventing a confidence
score. Mono burst data explicitly marks Side absent. `MeasurementCrest::view` returns the
`analysis::BandCrestResult` representation over owned rows; it requires no live analyzer.
The source activity floor is the configured maximum of -70 dB and BandCrest's own gated block
mean power minus 42 dB. Building the mask reads saved powers, never PCM a second time.
Only measurement statuses and mandatory input warnings are emitted here; device decisions and
other findings remain separate. Uncertain lowest-note evidence requests the safe target HPF floor.

Every snapshot and event field is required on decode: a snapshot never persists (the page and the core ship together),
so a missing key is a decode error, and a field that is semantically optional is nullable and present. Only a request's
own optional fields keep a declared default — a query's `masterId`, `crossoverHz`, `fromHz`, `toHz` and `spectrum`, an
edit's or a revert's tick and type. `SessionCapabilities.leanSummary` is required: the C struct's base is the whole
40-byte record (owner, 2026-10-01), so no shorter record states a default for it.

The table distinguishes measured sources whose devices are not placed or whose plan waits, including master and
stopped overlays. Mastering depends on measurements (and, with the panel open, on the plan); edit, revert, import and
adoptMachine depend on placement through their table cells.
Cached PCM reloads schedule needles from mandatory readiness. Bit-depth changes refresh unused-bit
readings from the retained exact PCM grid without allocating or repeating source analysis.

## Ready master job and audio ownership

The complete Solve memory gate is `felitronics_session_memory_gate_tests`; its separate
`felitronics_session_memory_gate_long` row renders a 60-second stereo 96→44.1 kHz source.
It charges the core allocation counter across load, measurement, each master step, cancellation,
snapshot and codec copies, bounded WAV slices, transfer, release, refusal, source replacement,
and warmed repetitions. `Session::liveBytes()` supplies the resident core claim while cumulative
allocation requests independently bound replacement peaks. Source plus delivery PCM require at
least `4 * channels * (sourceFrames + deliveryFrames)` bytes; the largest block is priced
separately. SRC owns no third complete PCM. The Wasm contract records observed linear-memory
growth and reacquires heap views after every call. Playback `ArrayBuffer`, assembled WAV and
bounded copy chunk are browser-owned buffers outside the session price; the recording lists
their sizes separately. Wasm linear memory may remain at its high-water size after release.


The additive `fc_session_master_bytes` and `fc_session_master` calls accept a current v14 `fc_master_config`
and `fc_master_params`. Their source hash and revision must match the session before the facade maps the
ready values. C++ callers use `command::Master` with `ready.version = 1`; version zero preserves the frozen
v1 command behavior. The ready call freezes the project, source, sound-config version, all topology and
parameter bits, and delivery rate in its recipe. The project can be edited while the job runs without
changing that recipe. A new source or an explicit cancel stops unfinished work and fences its old identity.
The delivery format is the target's (`targets.toml`): its `sampleRate` (0 keeps the source's rate) and its
`bitDepth` (16 or 24; dither only at 16). A ready `deliveryRateHz` or `deliveryBits` of 0 takes it and the same
value restates it; any other value, from the C facade's `fc_master_config.deliveryRate` and
`fc_master_params.dither.bits` included, is refused before any allocation with the appended
`Rejection::DeliveryFormat`. `Answer::targetBits` and `Answer::targetRate` and a `RejectedDeliveryFormat` fact event
({bits} {rate}), emitted with the rejection, name the target's format. The resolved depth also sets the chain's
dither bits, whether or not the dither stage is on. A target whose rate is not the source's (cd, cdDynamic, youtube
on a 44.1 kHz file) takes the one extra source-rate pass for the crest comparison (decision 2.3).

Ready preflight requires retained PCM and finite completed integrated loudness and true peak. It prices
the chain, renderer, converter when needed, solver, search workspace, output PCM, compact rows, and
allocator margin before any job allocation. `step` advances the search by bounded work units; one
landing has at most twelve measured passes. A safe miss retains the best verified output and its typed
reason. Unavailable mandatory readings yield no transferable PCM; a true peak above the ceiling does only where no render
kept under it, the gentlest delivered and marked `peaksAboveCeiling`. Optional
source analyzers continue independently. `canMaster` in the snapshot reports state and mandatory
readiness; capacity is reported by the preflight demand. The snapshot also exposes the pending transfer
token and PCM byte count. Every field is present on decode; a missing key is a decode error.

One session owns at most one pending delivery PCM. Its `MasterToken` names source, completion revision,
job, and master; every transfer checks all four. The retained master list owns recipe and compact pass,
limiter, and peak-clip rows, but no previous full PCM. Native callers can `copyMaster`, move it through
`takeMaster`, or `releaseMaster`. A successful take or release frees the session's PCM claim and advances
the revision; the caller owns the moved allocation after take. The C facade exposes size, copy, a scoped
view, and release. A wasm shell constructs a view from the returned address, calls `.slice()` once into
an independent `ArrayBuffer`, releases the session PCM, and transfers only that independent buffer.
The heap view expires on the next module call or `memory.grow`; the shell must discard it immediately.
Forgetting a master releases its pending PCM, while other masters keep only metadata. A poisoned wasm
instance is discarded and rebuilt by replay; ordinary cancellation is a session command.

## WAV delivery

`Session::masterWavPlan(token)` prices the canonical RIFF image without allocation. The additive
`fc_session_master_wav_size` reports its byte length and delivery bits; `fc_session_master_wav_copy`
copies at most 65536 bytes at a time into caller storage. A shell allocates one independent WAV buffer,
copies all slices while the master token is live, and stores the completed image under `masterId` before
calling `fc_session_master_audio_release`. Repeated downloads use those owned bytes and do no rendering,
metering or dither work. A cancelled, unsafe or mandatory-unavailable master has no token and no WAV.
The twelve-pass safe miss has a token and remains downloadable with its measured miss and hint facts.

The writer reads the existing planar f32 output directly and interleaves 16- or 24-bit PCM using core
Dither's `2^(bits-1)` grid and `floor(x + 0.5)` code rule; it writes no other format. The format is the
job's resolved delivery bits, the frozen target's bit depth. Session opts the solver into that grid before each meter step, so the measured
PCM, the listening transfer and the decoded WAV agree sample for sample. Direct solver callers retain
their legacy float output unless they set `LoudnessRequest::pcmBits`. Export adds no noise and never
resets or reruns the chain. Core's installed WAV
writer is checked byte for byte on grid, clamp and odd RIFF padding cases. If a shell wants another
format after release, it must retain PCM externally and use a separate future contract.

The synthetic site contract is `tools/contract/wav-input.json` plus
`tools/contract/recordings/wav-contract.json`. Regenerate and verify it with
`node tools/contract/wav-contract.mjs <native-ABI-test> <native-job-test> <fcsession.node.js>
<wasm-job-test.js> <fcore_session> --rebuild`, then omit `--rebuild` for the gate. The recording
states the retrieval call sequence, input and artifact hashes, dependency and codec versions, the
WAV header and the native/wasm digest, and cancel, unavailable, safe miss and unsafe outcomes.
The same recording includes a late source crest completion after WAV release and wasm heap growth,
with the source completion and master-keyed join events and snapshots on both sides of the join.

## Measured ready-master report

Each completed ready master carries an optional `MasterReport` beside its recipe and landing. Its LUFS,
reference true peak, PLR and suitable LRA are the solver's completed measurement of the selected delivered
PCM, including the drained tail and enabled dither. The report also gives the achieved-minus-source gain,
signed achieved-minus-target miss, target and ceiling, and a separate true-peak safety flag. A loudness miss
can keep the PCM when its reference true peak is safe; a missing mandatory reading or an unsafe peak cannot.
The two optional hint records expose measured sub-bass or presence share, limiter peak reduction, gain or
remaining loudness distance with units. `MasterReportText` renders the miss and mix suggestions as typed,
ru-first/en facts. PassLimit means the twelve-pass budget ended; it makes no claim of physical impossibility.
The landing's verdict is a fact too, one per status, published with the master ahead of the miss
(`MasterReportText::landing`): solved says the achieved loudness against the target and the landing's tolerance
(`MasterLandingSolved`, 88, "(tolerance ±0.1 LU)") — never "hit" without numbers; unreachable names the limit that held
it (89, a select on `landingLimit`: the true-peak ceiling, the limiter's GR limit, the PLR floor, the LRA loss limit, the
chain's gain bound — `LandingSummary::binding`; "one of the constraints" where the solver named none) against the
tolerance; pass limit says the budget ended against it (90); between names the two nearest levels, both beyond the
tolerance (91, `belowLufs`/`aboveLufs`); the achieved number and the gap stay the miss's own line
(`MasterLandingMiss`/`Above`, 11/23); a technical failure says so (92). The product landing the session runs ends Solved,
PassLimit or between with its master delivered (a loudness it cannot hit always returns the file at the closest level it
found, owner 01.10), or unreachable with the true-peak ceiling named when measured renders exist and none kept under the
ceiling (89; possible only for a caller's chain without the limiter). That one delivers its file too (owner, 01.10): the
render that overshoots the ceiling least, the gentlest measured, verified like any other and marked —
`LandingSummary::peaksAboveCeiling` and `MasterReport::peaksAboveCeiling` (with `peakSafe` false and `truePeakDbTp` above
`ceilingDbTp`), and `MasterPeaksAboveCeiling` (98) beside the verdict, naming the true peak and the ceiling; the miss's
line, which says the true peak held, is not said of it. Delivered means under the ceiling except in exactly this marked
case: the decoder holds a deliverable report to `peakSafe` or the mark, the mark to a deliverable report above its
ceiling and not met, and a marked landing to a deliverable `TargetUnreachable` bound by the ceiling. Unavailable and
cancelled landings say none.

**Loudness is a request, not an order** (owner, 04.10): better to fall short of the target than to reach it and make the
master unlistenable. Two parts of `engine.toml [landing]` say so, both in the sound version. `onSourceGate`: the level
the landing puts on the target is the master's mean block energy over the 400 ms blocks the source's BS.1770 gate
admitted (absolute, then relative), read on the source's momentary series (`Loudness` `momentary`, block i is reading
i + 3) and not gated again — the quiet parts the drive lifts above the master's own relative gate do not join the
average, so they no longer drive the loud part past what it needs alone. A source without that series whole (a
sidecar's facts, rows refused for memory) lands on the master's own gate. `limiterBudget`: the most the limiter may
take, the P95 of its gain reduction over the programme's windows — the very number `MasterCost::limiterP95Db` prints —
by the target's loudness, a person's edited number included: 4 dB below −10 LUFS, 7 dB from −10 to −8 (both ends),
10 dB louder (`detail::limiterBudgetDb`). `LandingSearch` reads it as a budget, not a refusal: a render over it is no
candidate and is marked in its pass record (`LimiterGainReduction`), the next drive is held under the lowest drive that
broke it, and the landing ends `TargetUnreachable` with `LimiterGainReduction` bound, its file delivered short of the
target — the loudest render that kept the budget, or the gentlest ceiling-safe one where none did. Such a verdict is
`MasterLandingBudget` (600): the target, the level landed and the budget («Цель −9,0 LUFS, сделано −9,7 LUFS: дальше
лимитеру пришлось бы срезать больше 7 дБ (P95).»), and no hint: nothing in the mix is blamed. The file is still certified
by BS.1770 (`achievedLufs`, `missLu`); where the level landed on the source's gate and that reading part by more than
the tolerance, `MasterLandingGate` (601) says both («По громкой части −9,0 LUFS, по стандарту файла −9,8 LUFS.») in the
miss's place, and the solved verdict (88) names the level landed. `targetMet` follows the status, as before. The crest's line
(`MasterReportText::crest`) goes out once: with the report when the job settles the crest (joined inside the job, or
unavailable), or from the late join when it was still pending.

`MasterCrest` stores five peak-amplitude/mean-square-power pairs per block, in Low, LowMid, HighMid, High,
Full order, plus five source activity values per block as zero/one values. Version, sample rate, hop
frames, block hops, three band corners, frame count, block count, completeness and provenance travel with
the rows. The retained row owner also holds its source measurement key, master id, captured recipe
fingerprint, and the two activity gates and configured hop duration. A late join checks the retained
identity and the complete grid before copying the source mask; equal row counts alone are insufficient.
When delivery changes rate, exactly one additional render with the chosen settings runs at the source rate
into bounded scratch for crest. The deliverable PCM and its LUFS/TP remain on the delivery rate, and
`checkPasses` records the extra render separately from the landing's budget. Pending source crest and a
final unavailable reason are distinct; either leaves a verified master available. Retained rows belong to
the master until forget or source replacement, survive PCM transfer, and are copied by owned snapshots and
the one generated codec. A late Pending-to-Ready or Pending-to-Unavailable change increments the revision
and emits a master-keyed crest fact so an event-driven shell can refresh that report. Replacing the
source removes the retained owners, while forgetting one master removes only its rows. A final source
refusal settles Pending crest and all five crest cost bands with the same reason.

## Measured master cost

`MasterReport.cost` exists only after a delivered master. It is descriptive evidence, never a delivery
gate. Every number has an optional value and a reason; `k2Reason=NotImplemented` means tonal change has
not been measured. A gain-only render has no shape or crest penalty. The source-rate check supplies the
crest rows when delivery rate differs, and `sourceRateCheck` marks that provenance.

For each of Low, LowMid, HighMid, High and Full, K1 compares only blocks whose source mask is one and
whose four linear operands are positive and finite. Its per-block loss is
`max(0, 20 log10(sourcePeak/masterPeak) - 10 log10(sourceMeanSquare/masterMeanSquare))` dB.
The cost is the weighted mean of the largest 5% of suitable blocks. With `N` blocks, the tail weighs
`0.05 N`: the largest `floor(0.05 N)` each weigh one, and the next weighs the fractional remainder.
For `N < 20`, the sole largest block receives its fractional weight and the answer is still its loss.
Equal losses have equal value, so their ordering cannot change the answer. An empty mask is `NoSignal`;
incompatible source and master grids are `Unsupported`. The five band values remain separate.

Shape uses end-stamped 0.1-second short-term LUFS readings of source and delivered PCM after the first
3 seconds. A source reading is compared only at or above `max(-70 LUFS, source integrated LUFS - 42 LU)`
with a finite delivered reading. Each compared deviation is the absolute value of
`(master short-term - source short-term) - (master integrated - source integrated)`.
The continuous shape is the empirical nearest-rank P95 of those deviations. A master shortened by more
than 0.1 second, either programme below -55 LUFS, or fewer than half the source-derived sections with
comparisons makes shape unavailable. A source reading more than 3 LU from the open section's running
mean starts a new section; exactly 3 LU stays. Sections shorter than 8 seconds join the neighbour
closest in source level, with ties joining the earlier neighbour. Adjacent sections within 3 LU then
merge. Each retained section carries source-frame bounds, source/master level and gain-removed shift.
The largest shift gets a named index only when more than two sections compare. LRA is a separate
reference measurement, not this ordered shape measure.

Glue P95 uses the same 4-ms window distribution of the compressor's trace, and glue maximum its largest tap
sample; the saturation's two cuts come from the soft clipper's own peak counters (see Glue and saturation). Each is
`NoSignal` when its stage is absent or bypassed; a snapshot without them is a decode error.
Limiter P50/P95 use the solver's 4-ms window distribution. The active fraction uses its input-gated
tap statistics; `activeWindowShare` separately counts source momentary windows passing the relative
activity floor. Pumping is RMS of the limiter's retained mean GR buckets after a second-order
Butterworth 1-Hz high-pass and 8-Hz low-pass in sequence, excluding the first two seconds. It is
unavailable below 40 buckets per second or without valid GR. Trace bucket count follows duration:
`clamp(ceil(seconds / 0.004), 1000, 65536)`, then is capped by delivered frame count. The true
bucket rate is therefore part of the retained trace; a wide bucket can make pumping unavailable.

The cost speaks line by line (`MasterReportText`), published with the report beside shape, impact and pumping, each
line only where its numbers were measured: the largest section shift and where it lies (`MasterCostSection`, 93, only
when a section is named), the sections compared (94), the limiter's median and P95 over the active windows (95), the
share of windows it works in and the share that are active (96), and the impact loss of the four bands (97) — that one
also from a late crest join when the crest was still pending at the report. A number missing is a line missing, never a
zero.

The master keeps at most 2048 waveform buckets per channel, each with actual delivered-frame bounds,
minimum, maximum, RMS and finite count. `QueryKind::MasterWaveform` returns intersecting retained
buckets, grouping them to the requested column count. The bounds in each answer are the actual
retained bounds, which can be wider than the requested zoom. For exact deep zoom after session PCM
release, the caller explicitly supplies a planar delivered-PCM chunk of at most 65536 frames covering
exactly the query range to `Session::masterWaveformChunk` or the additive C facade
`fc_session_master_waveform_chunk_*` entries. The core reads only that chunk and owns no full-song
cache. It never re-renders during a query. The chunk's shape and result allocations are priced by
the paired storage/size calls. Master cost rows, the scan meter, trace expansion, their retained
capacity and snapshot/codec copies are included in declared memory.
Waveform finalization, pumping, active-window counting, section building and merging, the shape
quantile, and each masked crest tail retain cursors across `step` calls. A single work unit visits at
most 1024 rows; late source crest joins use the same cursor with one row per unit.

Synthetic numerical controls are public in `modules/session/tests/CostTests.cpp`; the source shape
fixture's FNV-1a hash over little-endian IEEE-754 f64 input bits is `f414d88b72c8a1a5`.
Rebuild and run it with `cmake --build build --target felitronics_session_cost_tests &&
ctest --test-dir build -R '^felitronics_session_cost_tests$' --output-on-failure`.
