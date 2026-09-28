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
| **11d** memory is declared before the work | yes | `Session::createBytes()` is the demand of `create()`, counted by the expression that sizes the request, and `Session::check()` gives the demand of every command before it runs — computed by the same function `apply()` runs first; the declared-budget harness (`modules/session/tests/DeclaredBudget.h`, on core's one allocation counter) holds `create()` and every command to *declared ≥ requested* (exactly equal, where the request is one exact allocation), holds `check()`, typed-command refusals and every transition to nothing requested; import parsing and its refusals are covered by the library storage declaration in `felitronics_session_project_tests`, and is itself shown to fail on a sample that under-declares. The event suite also holds `stepBytes()`, `snapshotBytes()`, snapshot copy, and codec size queries and work to their declared demands (below). The demand is checked in C++ and `fc_session_create_bytes` publishes creation demand before creation. The shell supplies a heap ceiling; live declared bytes plus each allocating command's demand must fit before work begins. The C boundary adds nothing to it (its table is static) and keeps the poison |

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
thresholds, the landing's series, the devices' travels and rules, the observations' thresholds, what a master's cost is
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
  engine's decided numbers — the landing's series, the high-pass knob's travel and slopes and comfort window, the
  wide-bass warning, the quiet thresholds, the peak clipper's classes, the glue slider (0…3 dB, step 0.1; accepted domain 0…6 dB) with its default of none, 0.5 dB when ticked and 2.6 dB on cd, the mono-bass block, the delivery rates, and the rest. The schema would admit another number where the physics allows;
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
measurement ended: the devices are placed, a master can be made), **Measured2** (the second ended too), **MeasurementStopped** (audio, results, and unfinished work retained) — and a master
being made is an overlay on the two measured ones. A shell asks by a `Request`, one struct per command with the shell's
own id for it: `load`, `setTarget`, `editTarget`, `editDevice`, `revertEdits`, `setManual`, `master`, `cancel`, `forget`, `importProject`, `continueMeasurement`.
`Session::apply()` answers it whole — accepted, with the revision it made, or rejected with a `Rejection` code and no state change. A rejection publishes an event and advances `seq`. Every accepted command and every transition moves the revision by one; a rejection leaves it.

**Who may do what, when, is one table in code** (`Table` in `Commands.h`), and every command consults it right after
the floating-point entry check, before anything else: in each column, `yes` where the command is taken, or the rejection it gets. Mastering1 and Mastering2
are a master being made on Measured1 and on Measured2. The endings of the work are the session's own transitions, not
commands: the work that measures and renders drives them (`src/Driver.h`, the library's internal seam, named the
session's friend and not public), and they happen only where their row says so. `fcore_session table` prints both tables
from the code, and ctest holds the text between the markers below to that output byte for byte.

<!-- the table: begin -->
| command | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 | Stopped | StoppedMeasured | MasteringStopped |
|---|---|---|---|---|---|---|---|---|---|
| load | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| setTarget | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| editTarget | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| editDevice | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes |
| revertEdits | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes |
| setManual | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| master | NoSource | NotMeasured | yes | yes | Busy | Busy | NotMeasured | yes | Busy |
| cancel | NoJob | yes | yes | NoJob | yes | yes | NoJob | NoJob | yes |
| forget | NoSource | NoMaster | yes | yes | yes | yes | NoMaster | yes | yes |
| importProject | NoSource | NotPlaced | yes | yes | yes | yes | NotPlaced | yes | yes |
| continueMeasurement | NoJob | NoJob | NoJob | NoJob | NoJob | NoJob | yes | yes | yes |

| the session's own transition | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 | Stopped | StoppedMeasured | MasteringStopped |
|---|---|---|---|---|---|---|---|---|---|
| the first measurement ends | no | yes | no | no | no | no | no | no | no |
| the second measurement ends | no | no | yes | no | yes | no | no | no | no |
| the master is done | no | no | no | no | yes | yes | no | no | yes |
<!-- the table: end -->

**The checks run in one declared order, the same for every command**, and the first that fails is the answer:
1. the calling thread's floating-point environment; 2. the table; 3. what the command names — a target, a device offered for this target and source, an
id left for a new job (`load` and `master`), a load's valid UTF-8 name, the active job, a master kept; 4. the fields — each touched one in the order its struct
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
saturation, tilt, the limiter's needles, dither and low. Tilt and low are separate devices on every target.
The machine enables low only for a target with `lowDb` (today `lp`, +0.5 dB); otherwise low starts off at 0 dB.
Dither is offered through 16 bits, and mono bass except on a mono source; the shell may exclude any device.

- **The machine's layer** is placed from the config for the target and the source — the ticks from `[stages]`, what
  the target decides (the high-pass's slope and floor, the mono-bass crossover, no needles where the target has no peak
  clipper, the dither at its bit depth, the low shelf's gain, the glue its row names) and each device's own section for
  the rest; tilt starts flat. In this release it is the config's defaults for the target and the source, not a decision
  taken from a measurement: mono bass, which `[stages]` leaves off, is off. The devices are placed when the first
  measurement ends, and again on a change of target after that; before it they are unplaced — every field of the
  machine's layer at its type's zero, and the state says so. A load unplaces them again.
- **Every value the machine places is one a person could set**: inside its knob's accepted domain, which the schema
  holds for every default and `felitronics_session_state_tests` checks for every target and source.
- **A person's edits** are taken only after placement (`NotPlaced` before it), whether the panel is visible or hidden. Values must be
  finite and inside the knob's domain; travel and step guide the slider. Values between steps or outside travel are
  accepted within the domain. A number is kept with −0 written as +0, so equal values give identical project bits.
- **`setTarget(name, onEdits)`** replaces the target's numbers silently — a person's edits of them go with the old
  target — and keeps or takes back a person's device edits as `onEdits` says. The machine's layer is placed again.
  Dither edits remain stored but dormant above 16 bits: they survive export/import and become applicable again
  on an eligible target. New dither edits still return `NotOffered` there; an explicit revert can remove stored edits.
- **`setManual(bool)`** changes only panel visibility. Device edits stay effective in either mode, including in a
  master recipe. `snapshot().view().handFieldCount` counts touched device fields, including false ticks and values
  equal to the machine, for the hidden-panel marker. Target-number edits are separate. `revertEdits` and
  `setTarget(..., reset)` explicitly remove device edits.
- **`load`** checks everything first, then DISARMS — whatever ran on the old source stops, and the old source, its
  measurements and its masters go; the manual mode is switched off, and a person's device edits go with it (the mode
  does not outlive the file: its edits were decisions about the old source); the old samples are freed before the new
  are asked for — and then WRITES the new source: its samples, channel after channel, its name and its hash (64-bit
  FNV-1a of its rate, channels, frames and every sample's bits). The target and its edited numbers stay.
- **`master`** captures the recipe — the project as it is, the source's hash, the config's sound version — and starts a
  job, numbered on from the last for the session's whole life, a load included: an id is never 0 and never issued twice,
  and once the last is issued a load or master is rejected (`NoJobId`); the project may change meanwhile, and the master renders its recipe. When it is done the session keeps it under
  its job's id; `cancel(job)` stops the named measurement or master, `forget(master)` lets a kept master go.

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
An Empty or Loaded session refuses export (`NoSource` or `NotPlaced`): its unplaced zeros are not machine decisions.
Files and browser storage belong to the shell. The writer reads no filesystem and the session reads no file itself.

```toml
defaults = "2026-09"
core = "0.2.2"
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

The core carries the **current and previous defaults tables**. Today the current label is `2026-09` and the previous
slot is empty. A carried label uses its compiled defaults. Labels are strictly `YYYY-MM`, with months `01` through `12`.
A label older than every carried version is converted: written numbers are retained and omitted fields take current
defaults. Fact `DefaultsConverted` (9) owns the original label and reports the conversion in both catalog languages.
Export uses the current label; a converted project round trips without a second conversion warning. A newer label
is refused as `NewerDefaults` (28, fact 128); a malformed label is `UnknownDefaults`. A future uncarried label between
the retained versions is also `UnknownDefaults`. All these refusals leave state and revision unchanged.

After defaults selection or conversion, the file's complete machine layer always wins (omitted fields mean defaults),
with the person's touched layer over it. The machine decides again beside it. `MachineDifferences` (8) publishes the
count for a foreign core, including zero; `SameCoreMachineDifferences` (10) publishes nonzero same-core differences
and politely explains that these usually indicate a project file edited by hand. Both facts render in Russian and English.
`snapshot().view().machineDifferences` holds ordered `(device, field, fileValue, coreValue)` rows. Flags use 0/1,
choices their enum numbers, and knobs their doubles. Owned snapshots, the codec and its generated `.d.ts` carry them.
Export preserves the stamp of the core whose machine decisions the file carries. A target change explicitly places
today's machine, stamps this core and clears the comparison. Hand edits and panel visibility do not replace it.

The page draws **`snapshot().view().eqCurve`**: the summed high-pass + tilt + low response using hand-over-machine
values, independent of panel visibility. It is empty before placement, then contains 128 logarithmic `(hz, db)` points
from 20 Hz to min(20 kHz, 0.49 × source rate). Each tick bypasses its own contribution. The coefficient designs follow
core's matched filters with deterministic math; the event suite compares their response with core across rates and
all high-pass slopes. Low keeps EQ band 2, 80 Hz and Q 0.6; tilt keeps band 1 and its 1 kHz pivot.
The one codec generator carries `eqCurve` into `snapshot.d.ts`; `SessionSnapshot.eqCurve` describes transferable
little-endian f64 rows, columns `[hz, db]`, stride 2. `handFieldCount` is a JSON number in both snapshot forms.

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
  not declare — renders as its id (`Text::key`), never in another language. An argument that does not match its fact's
  declaration renders as `{name}`; a plural or a select whose own argument does not match cannot choose, and renders the
  id.
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
  builds it from a refused answer, reading the field off the request. The mapping is a switch over every code with no
  default, so a code the state machine adds and nobody maps is an error in this repository's builds (`-Wswitch`,
  `-Werror`); the suite holds the table code by code, the field terms position by position against `src/Devices.h`'s
  walk of the fields (a term exactly where a check can refuse), and renders the answers of a real session.
- **Only in words:** the wording itself — the glossary's Latin terms and the polite form, which the site's guards hold
  for its own catalogs; this catalog has no wording lint; and that a fact's user text is a view whose bytes
  its caller keeps alive while it is rendered.

The facts' ids are stable and fall in ranges (`Text.h`): 1–99 readings and the landing, 100–199 a command's rejection,
200–299 the phases of the work and 300–399 the session's errors. Adding a fact is three edits: its id in
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

`Session::step(budget)` runs live loudness, source clipping, the programme report, and the needles analyzer. Other analyzers and mastering retain their scheduling placeholders. The budget counts **work units**, never milliseconds. A call
consumes at most `min(budget, 16)` units, reports the number consumed, and returns `More` while any job remains or
`Done` when none does. Zero units poll without progress. The shell measures its own speed and converts time to
units. The library has no clock. A measurement unit prepares one instrument, reads at most 1024 source frames,
publishes at most four newly decided clipping runs, or advances report finalization by at most 1024 entries.
The report drains reference true peak and scans tail energy, integrated gates, and short-term gates without changing
summation order. A master takes priority over phase two, which resumes when the master finishes.

Every completed unit publishes a phase. Measurement progress is the cumulative weight divided by the sum of
`progress.analysis.weights`, in this order: loudness, report, lowEnd120, forensics, stereo, lowEndSweep, stereoBursts,
crest, hum, tempo. Master passes use `passWeight` against `expectedPasses * passWeight + measureWeight`; remeasurement
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
the state it resumes. Stopped, StoppedMeasured, and MasteringStopped are explicit table columns for the stopped state
before placement, after placement, and with a master running. Cancelling a master ends only that overlay.
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
grids, total/stored counters and completeness. Each source has one result per analyzer. `OwnedMeasurements::copy`
copies every name and row; snapshot copies therefore survive workspace destruction, replacement loads and session
destruction. A `Measurement` event owns the result identity, status, counters and revision; its numeric data is read
from the corresponding snapshot. The codec generator describes both forms, including transferable f64 rows.

The measurement key combines the PCM bit hash, actual native parameters, config version and core/session versions.
The target is absent from source-wide measurements. An identical load reuses PCM, saved progress and those results;
changing the target does not invalidate them. Needles have a separate source-and-ceiling identity. Each prepared analyzer stays resident across pauses and is destroyed after publishing its result. Live result arrays
belong to the session independently of analyzer scratch, so final publication needs no programme-sized copy. The live needles job consumes its
retained loudness and true-peak readings through the controller seam; see [NEEDLES.md](NEEDLES.md).

`measurementStorage` exposes source, result, workspace, copy, codec and allocator demands and the maximum live
set across load and work. Native `storageFor` calls share the exact parameter records used for preparation, including
the separate loudness meter and waveform/stereo columns. Nested spectrum and band-burst storage is included only
through its parent analyzer. The estimate conservatively includes every available workspace simultaneously, a
detached result copy and JSON serialization. Load demand also includes the previous live session, temporary input
PCM and source-name copies. The largest-block demand is conservative; capabilities remain the shell's limits.
Embedded TOML views allocate nothing; runtime config/project parsing keeps its separate declared TOML budget.
There is no excursion index. Needles are re-measured over retained PCM for one ceiling, with a separate
`needlesStorage` / `fc_session_needles_bytes` demand. Preparation checks capacity before allocating;
all subsequent read and finish units allocate zero. Native run-list truncation is explicit in the snapshot.

`tools/session-codec-schema.json` is the one hand-edited codec description. `Codec` exchanges its named-field JSON.
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
the suite and reviewing changes against those inputs. The pinned event hashes (`e2d3b1997699c4ac`, `74d299b0b8b47839`) include all active event payload fields and
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
fields produce a `rejected` answer naming the field. Edits omit untouched knobs or use null. `load` metadata requires
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

`tools/wasm/build.sh` builds `fcsession` from the facade and `modules/session/sources.txt`, audits the exact export list,
compares node/web wasm bytes, checks for threads, and runs a node scenario through the ABI and generated types.
Controls reject an extra export and verify a real allocation trap followed by permanent poison. The wire fixture
checks all six event kinds and nonempty binary rows against the generated declarations. Full native/wasm scenario
parity is a separate contract suite.

## The native CLI — `fcore_session`

`fcore_session version` prints the two releases and the ABI version; `fcore_session config targets|engine` prints a
document of the embedded config through felitronics-toml's canonical writer (its numbers, without the comments), and
`fcore_session config version|sound-version` its versions; `fcore_session table` prints who may do what, when — the
tables of `Commands.h`, as Markdown, which ctest holds to the block of this document; `fcore_session run <script>` reads
a command script (`-` is stdin) into a fresh session and prints `done <commands>`. A script carries no command: one with
none in it — empty, or comments and blank lines — answers `done 0`; a script with a command in it, and a session that
refuses to be created, are refused with exit status 2 and nothing on stdout. It links the library as C++, the way a
desktop application does.


Knob travel and step describe the shell's slider. Commands and project import accept the domains below, including
values between steps and beyond travel. An empty edit or revert is accepted with unchanged revision. Device edits
require placement and an offered device. Panel visibility does not gate them. Low is offered on every target,
saved machine layers are retained for every core stamp, and older defaults are converted with their warning.

| Knob | Accepted domain | Reason |
| --- | --- | --- |
| Target LUFS | Any finite binary64 | Landing reports unreachable targets |
| Target ceiling | −6 through −0.1 dBTP | Product |
| HPF frequency | 0 < f < source sample rate / 2 | Device |
| HPF slope | Multiples of 6, 6 through 96 dB/oct | Device, orders 1–16 |
| Mono-bass frequency / width | 60–300 Hz / 0–1 | Product / device |
| Glue | 0–6 dB | Product |
| Saturation drive / mix / output | 0–12 dB / 0–1 / −6–0 dB | Product / device / product |
| Tilt / low | −6–6 dB | Product |
| Needles above ceiling | 0–6 dB | Product |

Limiter release is configuration, not a project knob. Its schema requires at least eight source samples at the
minimum supported rate. The schema checks travel within domains, defaults within domains, and positive progress-weight
sums. Project numeric fields use ordinary TOML numbers when its decimal subset can represent them exactly; other
finite binary64 values use quoted shortest round-trip decimals, parsed with the same exact numeric reader as JSON.

The C v1 records `fc_session_capabilities`, `fc_session_sizes`, `fc_session_capacity` and `fc_session_storage` start
with the caller's `sizeof`. Their v1 prefixes are 32, 12, 24 and 32 bytes; fields may only be appended. Sizes below the
v1 prefix and beyond the current build have distinct statuses. Future suffix fields must define their absent-field
meaning for v1 callers. Demand queries for commands, loads and imports use the session's own `storageFor`; capacity
may be updated between calls as a heap ceiling and largest free block. Live bytes plus demand and the largest allocation
are checked before work. Nonallocating commands remain available when capacity is reduced.

C load frames are uint32 because the wasm heap is limited to 2 GiB; native C++ PCM frames are uint64. Binary rows are
little-endian IEEE-754 f64, with unsupported byte-order builds refused at compile time. Executed encoder fixtures and
compiled layout facts freeze every event kind, snapshots, answers, row order, offsets, strides and C signatures. Local
source controls mutate each class of fact and require comparison to fail, while append-only additions pass.


Live loudness arrays use a 100 ms cadence of ten rounded 10 ms analyzer sub-hops: `firstFrame == stepFrames`,
point zero ends at that frame, and point indices never change. Momentary and short-term windows cover four and thirty
hops. Before a complete window, values are NaN with TooShort; silence is negative infinity with NoSignal. The parallel
reason arrays preserve each point's status. `framesRead` and `tailFrames` distinguish an unfinished window from the
final off-grid tail; no padded point is manufactured. `reading.finished` closes stream delivery, while the report
remains pending until its own finalization completes.

The additive `reading.clips` scalar row groups every six values as start, length, channel, sign, level, and native
ClipEvidence. Existing `runs` retain their three-column prefix. Snapshot clipping arrays declare six columns;
total and stored counts distinguish exact aggregate counts from a truncated coordinate list. The source-damage
detector is independent of target-dependent needles. The programme result retains every native value, count, and
reason, including unconditional short-term P95 and the drained programme true peak. Load still refuses non-finite PCM;
finite input that overflows an analyzer is reported with its native damage reason.
