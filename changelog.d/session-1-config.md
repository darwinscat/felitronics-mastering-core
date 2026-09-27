### session · tools — the mastering config: two TOML documents compiled into `felitronics::session`, read by schema

**Every number of the mastering session now lives in the session**, in two TOML documents of the module, each number
with what it means and where it came from beside it, and the owner's decisions marked as such. `modules/session/config/
targets.toml` is the table of targets — 25 of them, each with its loudness, true-peak ceiling, mono-bass crossover (120 Hz;
vinyl 150), high-pass floor (24 Hz; vinyl 32) and slope (24 dB/oct; vinyl 12), how much the high-pass may take at the
lowest note (1 dB; club 0.3), and its delivery rate and bit depth; vinyl's +0.5 dB low shelf and its ceiling without a
peak clipper; the high-pass always placed on vinyl and on club (there it guards the subwoofers from infrasonic bursts);
the one extra pass at the source's rate of cd and cdDynamic; AES
TD1008's −14 LUFS album loudness, marked desktop-only; the default target, the main list and the travels of the hand
edit. `engine.toml` holds every other number: the input brought to −18 LUFS (a warning below −40, gain and ceiling only
below −55), the landing in series of 12, 24 and 32 passes, the limiter's 0.15 dB ceiling margin and 50 ms release with
its second release stated off (the sound depends on no default of the core's), the peak clipper's classes (its manual
threshold starts at the "between" class), the low-end measurement's geometry, the high-pass knob topping out at 50 Hz
with one comfort window (24–42 Hz, warning towards 20 and 50) and "no DC" named as the dcOffset finding's threshold,
mono bass with its width knob and ONE wide-bass warning at 6 % of side, the compressor's threshold counted from the
short-term P95 and its glue on cd, saturation, tilt, the low shelf, dither at 16 bits only, the de-esser (manual, off, not
offered), the observations' thresholds, what a master's cost is measured with — as measured, without a verdict — the
progress weights and the blind test's protocol.

**Compiled in, never read.** felitronics-toml v0.2.0 (MIT, listed in `THIRD_PARTY_NOTICES.md`) is resolved like
felitronics-core — a sibling checkout for local work, the pinned tag otherwise — and compiles both documents into the
library as constexpr data (`felitronics_toml_embed`); nothing reads a file at run time. A product that consumes this
repository makes felitronics-toml available before it, as it does core. A document the parser refuses stops the build at
its line and column.

**Read by schema — form and physics** (`<felitronics/session/Config.h>`): `Config::load()` binds the documents to typed
structs — every key with its type and its domain (a share within 0…1, a ramp whose ends cannot divide by zero, a series
that does not shrink, a value on its knob's grid counted from the travel's start and checked exactly on the written
decimals, a range another key states such as a target's loudness on the edit travel), checks across keys (a name that is
no target, a name given twice, an EQ band two devices share, a ramp law outside its domain, the limiter switched off, a
default written out), and every key nobody read reported as unknown. The blocks the config feeds an analyzer — the low
end, the crest, the sibilance-band bursts — are handed to that analyzer's own `storageFor()` at the source rates the
product accepts and refused whole where it refuses: one source of truth for its domain. A problem is data: document,
fault, key path, line and column; `Config::bind()` runs the same schema over texts.

**Every build runs the schema.** `felitronics_session_config_check`, a host tool compiled from the library's own schema,
reads the documents before `felitronics::session` is built — a consumer's build and a build without tests included, under
the emulator where the build cross-compiles, or `FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE` — so a typo is a red build at
`<file>:<line>:<column>`, and a failed gate runs again; a newer or another supplied checker, or a change of the schema's
sources, runs it again too. `tools/wasm/build.sh` runs it before linking `fcsession` and records
felitronics-toml in `BUILD-INFO`. Six controls plant mistakes in a copy and require the gate to go red at the spot; the
config suite plants over sixty more, in-process.

**The owner's decisions are pinned apart** (`felitronics_session_config_decisions_tests`): every target row field by field
(delivery rates included) and the engine's decided numbers (the landing's series, the high-pass knob, the glue knob and
its default of none, the mono-bass block, …), so changing one is a deliberate test edit; its controls plant departures the
schema admits and require them named.

**The config's versions** (`Config::versions()`): 64-bit FNV-1a hashes of both documents' normalised data — numbers as
the bits of their double (−0 as +0), tables in key order, order kept in arrays — so spelling, key order, inline-or-not,
comments and spacing move nothing; the target rows' written order counts in `all`. `all` covers every key; `sound`, what a
recipe will record, is what can change a master — when unsure a key stays in — leaving out what is only shown, what
prints a finding without switching a device (every observation threshold but polarity), what is measured after the
master, development, and the de-esser's block while no shell offers it. Both are computed from the
embedded data without allocating. The suite changes every value of both documents one at a time and requires `all` to
move each time to a value of its own and `sound` to move exactly for the values that can change a master; the sound
version is pinned to the name of the defaults, so a sound number changed without new defaults is red.

**`fcore_session config targets|engine|version|sound-version`** prints a document of the embedded config through
felitronics-toml's canonical writer, or a version; ctest holds the output byte for byte to the source documents.

**`fc_session_config_version`** joins the draft `fc_session` (still version 0, no promise): the config's `all` version in
two uint32 halves, the out-pointer checked before anything is written, nothing allocated. `fcsession` now carries the
config — 43.3 KB of wasm, 13.0 KB brotli, from 2.5 / 1.2 — and `tools/wasm/session-check.mjs --config-version` requires
its version to be the native CLI's. `tools/wasm/build.sh` embeds and gates the config with a felitronics-toml checkout:
`FELITRONICS_TOML_DIR`, or the sibling `../felitronics-toml`.
