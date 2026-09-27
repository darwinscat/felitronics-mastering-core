### session · tools — the session's states and commands: one table of who may do what, when, and a project in two layers

**`felitronics::session` has states and commands** (`<felitronics/session/Commands.h>`). A session is Empty, Loaded (a
source, its first measurement running, the devices not placed), Measured1 (the devices placed, a master can be made) or
Measured2, and a master being made is an overlay on the measured two. A shell asks by typed requests — `load`,
`setTarget(name, onEdits)`, `editTarget`, `editDevice`, `revertEdits`, `setManual`, `master`, `cancel(job)`,
`forget(master)` — each with the shell's own id, and `Session::apply()` answers each whole: accepted with the revision it
made, or rejected with a `Rejection` code (and the field, by its place in its struct), having changed nothing — the
revision included. No text anywhere: the codes are stable values a shell's catalogue writes from.

**Who may do what, when, is one table in code** (`Table` in `Commands.h`): a row per command, a cell per column (Empty,
Loaded, Measured1, Measured2, and a master being made on either measured state) — taken, or the rejection it gets there.
Every command consults it first; the endings of the work (the first measurement, the second, a master) are the session's
own transitions with a table of their own, driven by the work through an internal seam, not by commands.
`fcore_session table` prints both tables from the code as Markdown, and ctest holds `docs/SESSION.md`'s copy to that
output byte for byte. The checks after the table run in one declared order — the thread's floating-point environment,
the table, the manual mode, what the command names, the fields, a load's audio — and the first that fails is the answer.

**The project** (`<felitronics/session/Project.h>`): the target (a row of `[targets]`) with a person's edits of its
loudness and ceiling, the manual mode, and the devices of the first release — the high-pass, mono bass, the glue,
saturation, tilt, the limiter's needles, the dither and the vinyl low shelf. Each device's fields are written once, as a
template over a field's form, and used as the machine's layer (complete), a person's layer (only what was touched) and a
revert's mask: no string names a field. The machine's layer is placed from the config for the target and the source when
the first measurement ends, and again on a change of target. A person's device edits are taken only after placement and
only in the manual mode, each value checked on its knob's travel and step exactly — the double a shell sends read as the
decimal it is, on the same grid rule the config's schema holds its own numbers to (now shared, `src/Grid.h`). A change of
target replaces the target's numbers silently and keeps or resets a person's device edits as asked; switching the manual
mode off takes back a person's device edits and nothing else; the low shelf, the dither and mono bass are offered only
where they apply. `load` checks everything first, then disarms — what ran on the old source stops, its masters go, the
manual mode is switched off and a person's device edits with it, the old samples are freed before the new are asked
for — and writes the new source with its hash. `master` captures the recipe (the project, the source's hash, the config's sound version); the master is kept
under its job's id when it is done.

**Memory is declared before every command** (law 11d): `Session::check()` runs exactly the checks `apply()` runs first
and says what the command will ask the heap for — a load its samples and its name, a master room for one more kept,
everything else nothing — and the state suite holds every command to it, exactly, through the allocation counter.
Reading the config costs nothing: the commands read the embedded documents in place, as the decimals written, and the
suite holds every number of that reading to the schema's binding of the same documents. `create()` refuses a config it
cannot read that way with `Status::Config` (`FC_SESSION_ERR_CONFIG` at the draft C boundary) — not reachable in a library
whose build ran the config's gate.

**`felitronics_session_state_tests`**: every cell of both tables, every rejection code produced with the whole session
compared before and after, the order of the checks, every knob's ends, step and non-finite values, placement, a change of
target with `keep` and `reset`, the manual mode switched off, a master's recipe, a load's disarm, the source hash pinned,
and memory declared for every command.

**`fcsession`** carries the states and the commands with its config: 61.0 KB of wasm, 17.8 KB brotli, from 43.8 / 13.4.
The draft C boundary carries none of the commands.
