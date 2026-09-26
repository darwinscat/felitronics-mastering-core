### session · tools — the mastering config: two TOML documents compiled into `felitronics::session`, read by schema

**Every number of the mastering session now lives in the session**, in two TOML documents of the module, each number
with what it means and where it came from beside it. `modules/session/config/targets.toml` is the table of targets —
25 of them, each with its loudness, true-peak ceiling, mono-bass crossover (120 Hz; vinyl 150), high-pass floor (24 Hz;
vinyl 32) and slope (24 dB/oct; vinyl 12), how much the high-pass may take at the lowest note (1 dB; club 0.3), and its
delivery rate and bit depth; vinyl's +0.5 dB low shelf, its ceiling without a peak clipper and its high-pass that is
always placed; the one extra pass at the source's rate of cd and cdDynamic; AES TD1008's −14 LUFS album loudness,
marked desktop-only; the default target, the main list and the travels of the hand edit. `engine.toml` holds every
other number: the input brought to −18 LUFS (a warning below −40, gain and ceiling only below −55), the landing in
series of 12, 24 and 32 passes, the limiter's 0.15 dB ceiling margin and 50 ms release, the peak clipper's classes, the
low-end measurement's geometry, the high-pass knob topping out at 50 Hz, mono bass with ONE wide-bass warning at 6 % of
side, the compressor's threshold counted from the short-term P95 and its glue on cd, saturation, tilt, the low shelf,
dither at 16 bits only, the de-esser's numbers (manual, off), the observations' thresholds, what a master's cost is
measured with, the progress weights and the blind test's.

**Compiled in, never read.** felitronics-toml v0.2.0 (MIT) is resolved like felitronics-core — a sibling checkout for
local work, the pinned tag otherwise — and compiles both documents into the library as constexpr data
(`felitronics_toml_embed`); nothing reads a file at run time. A product that consumes this repository makes
felitronics-toml available before it, as it does core. A document the parser refuses stops the build at its line and
column.

**Read by schema** (`<felitronics/session/Config.h>`): `config::load()` binds the documents to typed structs — every
key with its type and range (a range another key states included, such as a target's loudness on the edit travel),
checks across keys (a name that is no target, an EQ band two devices share, a minimum above its maximum, the limiter
switched off), and every key nobody read reported as unknown. A problem is data: document, fault, key path, line and
column; `config::bind()` runs the same schema over texts. In this repository's builds `felitronics_session_config_check`
runs the schema over the embedded config right after it is linked, so a typo is a red build at `<file>:<line>:<column>`;
three controls plant an unknown key, a wrong type and a value out of range in a copy and require it to go red at the
spot, and the config suite plants twenty more, one of every kind of problem.

**The config version** (`config::version()`), what a recipe will record: a 64-bit FNV-1a hash of both documents' data in
document order — comments, spacing and positions are not data — computed from the embedded data without allocating.
The suite changes every value of both documents one at a time, through their text and through the embedded data, and
requires a version of its own each time.

**`fcore_session config targets|engine|version`** prints a document of the embedded config through felitronics-toml's
canonical writer, or its version; ctest holds the output byte for byte to the source documents.

**`fc_session_config_version`** joins the draft `fc_session` (still version 0, no promise): the module's config version in
two uint32 halves, the out-pointer checked before anything is written, nothing allocated. `fcsession` now carries the
config — 40.7 KB of wasm, 11.9 KB brotli, from 2.5 / 1.2 — and `tools/wasm/session-check.mjs --config-version` requires
its version to be the native CLI's. `tools/wasm/build.sh` embeds the config with a felitronics-toml checkout:
`FELITRONICS_TOML_DIR`, or the sibling `../felitronics-toml`.
