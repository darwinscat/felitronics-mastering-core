### tools · wasm — fctempo: the tempo detector as a module of its own

A page that measures a tempo and nothing else no longer has to download every analyzer of `fcprobe` for it.
`tools/wasm/build.sh` builds a third module, `fctempo` (`tools/wasm/fc_tempo.cpp`): `fctempo.web.mjs` + `.wasm` (ES
module, `-sENVIRONMENT=web,worker`, factory `createFcTempo`) and `fctempo.node.js` + `.wasm`, on the probe's flags —
no threads, emmalloc, `-fno-exceptions -fno-rtti -ffp-contract=off -fno-fast-math -msimd128` — with the same gates:
the web and node `.wasm` byte-identical, the web one audited for threads. **37 441 bytes of wasm, 14 604 brotli (`-q
11`)**, against the probe's 275 056 / 76 651; the glue is 8.7 KB against 23.3 KB.

Its entry points ARE the probe's tempo entry points: the same ten `fc_probe_tempo_*` names, arguments, rows and
refusals, because both modules compile one text, `tools/wasm/fc_tempo_entry.h` — a page moves from one module to the
other without changing a call. What it does not share is the version: `fc_tempo_abi_version()` answers
`FC_TEMPO_ABI_VERSION` (**1**, `tools/fc_tempo_abi.h`), which is fc_probe ABI 1's tempo surface. Not
`fc_probe_abi_version`, because a version is a promise about a whole surface — `fc_probe_abi_version() == 1` says
the report, the hum detector and the streaming meter are there, and a page gated on it would meet a missing export
as a TypeError. Append-only, like fc_probe's; a change a caller can see in the shared tempo text moves both versions
in one commit. A page switching over loads `fctempo.web.mjs` and gates on `_fc_tempo_abi_version() >= 1`; every
tempo call stays as it is.

`fcprobe` does not change: the tempo entry points and the argument guards (`tools/wasm/fc_abi_guards.h`) moved out
of `fc_probe.cpp` verbatim, and every fcprobe and fcmaster artifact is byte-identical to the build before the move
(the checked `fcprobe.debug.wasm` once its DWARF is stripped). `build.sh` now reads a module's entry points from its
`#include` closure, not from the `.cpp` alone, so a name declared in a shared header is on every export list that
compiles it, and the one-per-line, count and return-type gates run over the whole closure. The closure is the
compiler's (`-MM`, under the module's own front-end flags, so a header included only under `-msimd128` is in it),
read as Make writes it (a path with a space survives), and a file of it that cannot be read stops the build.

Proven the way the probe is: CI diffs `fcore_measure tempo` against `fcprobe`, its checked build and `fctempo`,
every row byte for byte, and every refusal row now demands exit status 2 on both roads, where it took any failure —
a module that did not load used to count as refusing. The harnesses take the module's identity from its file name
and hold the artifact to it (`tools/wasm/module-identity.mjs`), so fcprobe handed over as fctempo is refused, not
measured. `felitronics_fctempo_abi_tests` runs the tempo ABI suite against `fc_tempo.cpp` natively (ASan, UBSan) and
on the wasm tier's checked build; `storage-probe.mjs` holds `fctempo` to its exact export set and compares the two
modules' tempo prices over 3600 quotes; its tables leave the process only once written, into a pipe as into a file.
On three real mixes (4:10–8:39, 48 kHz stereo) the two modules answer the same bits in the same time.
