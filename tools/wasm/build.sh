#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# Builds the wasm artifacts of this repo's C ABIs:
#
#   fcprobe.*   — the wasm measurement spike (tools/wasm/fc_probe.cpp): every offline analyzer
#   fctempo.*   — the tempo detector ALONE (tools/fc_tempo_abi.h + tools/wasm/fc_tempo.cpp), for a page that needs
#                 nothing else: fc_probe's ten tempo entry points under the same names — one text,
#                 tools/wasm/fc_tempo_entry.h, compiled into both — at an eighth of the probe's size. See the
#                 fc_tempo section below.
#   fcmaster.*  — the mastering ABI (tools/fc_master_abi.h + tools/wasm/fc_master.cpp), which is what a
#                 browser worker links to render and to run the loudness search. Same flags, same numeric
#                 contract, same no-threads and byte-identity checks — see the fc_master section below.
#   fcsession.* — the session ABI (tools/fc_session_abi.h + tools/wasm/fc_session.cpp) over felitronics::session, the
#                 first module with a COMPILED library behind it: the sources modules/session/sources.txt lists, with the
#                 flags modules/session/build-flags.txt states and the library's releases, and its config embedded as
#                 the CMake build embeds it. See the fc_session section.
#   fcpeaq.*    — never shipped: tools/fcore_peaq.cpp for node, so CI can diff PEAQ's output against the native CLI's.
#   tierup/     — never shipped: fctempo, fcprobe and fcsession linked again with their function names, so the build
#                 proves the tempo detector's hot loops survived the optimiser (tools/wasm/tierup-check.mjs).
#
# The probe part first. Three artifacts from one source:
#
#   fcprobe.web.js/.wasm    -sENVIRONMENT=web,worker — EXACTLY what the spike specifies. This is the artifact whose
#                           size is reported and which the page loads; the no-threads claim is made about it.
#   fcprobe.web.mjs         the same line plus -sEXPORT_ES6=1, as fcmaster.web.mjs: the ES-module glue a module
#                           worker can `import`. It writes fcprobe.web.wasm too; the .js above stays for
#                           probe.html, which loads it with a classic <script>.
#   fcprobe.node.js/.wasm   -sENVIRONMENT=node — the same wasm with node glue, so the parity harness runs
#                           without a browser (and so CI can). The script verifies the two .wasm files are
#                           BYTE-IDENTICAL, which is what lets a parity result proven on one transfer to the
#                           other: -sENVIRONMENT only shapes JS glue.
#   fcprobe.debug.js/.wasm  -O1 -g -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_OVERFLOW_CHECK=2 — the libsoxr lesson.
#                           libsoxr compiled and linked cheaply under emscripten and crashed at RUNTIME; a
#                           release build that "works" proves less than a checked build that agrees with it.
#
# Requires emsdk on PATH:  source ~/emsdk/emsdk_env.sh
#
# AND A felitronics-core CHECKOUT: every module these ABIs stand on except this repository's own two lives
# there, and so does the no-threads audit. It is FELITRONICS_CORE_DIR when that is set — CI passes the one its
# CMake configure fetched (<build>/_deps/felitronics_core-src), so the wasm modules and the native references
# they are diffed against are built from ONE core — and the sibling ../felitronics-core otherwise, the same
# default the CMake uses. Nothing else is guessed: with neither, the build stops.
#
# AND A felitronics-toml CHECKOUT, for fcsession's config: FELITRONICS_TOML_DIR when that is set (CI passes the one its
# CMake configure resolved), the sibling ../felitronics-toml otherwise — the same rule, for the same reason.
#
# AND A felitronics-bands CHECKOUT, for the named EQ bands' geometry and names: FELITRONICS_BANDS_DIR when that is set (CI
# passes the one its CMake configure resolved), the sibling ../felitronics-bands otherwise.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE/build}"
mkdir -p "$OUT"

command -v em++ >/dev/null || { echo "em++ not on PATH — source \$EMSDK/emsdk_env.sh first"; exit 1; }
echo "emcc: $(emcc --version | head -1)"

CORE="${FELITRONICS_CORE_DIR:-$ROOT/../felitronics-core}"
[ -f "$CORE/modules/core/include/felitronics/core/DetMath.h" ] \
    || { echo "no felitronics-core at $CORE — set FELITRONICS_CORE_DIR to a checkout (v0.57.0 or later)"; exit 1; }
CORE="$(cd "$CORE" && pwd)"
# The sources this run compiles, digested BEFORE it compiles them: the stamp written last (below) names this digest, so
# an edit made while the build runs leaves a stamp the contract check refuses instead of one that vouches for it.
SOURCES_DIGEST="$(node "$ROOT/tools/contract/module-stamp.mjs" --sources)"
# A core that still carries these modules would put a SECOND copy of every header here on the include path,
# and which one a TU compiled would depend on the order of the -I flags below. The CMake refuses such a core
# for the same reason; so does this.
[ ! -d "$CORE/modules/mastering" ] && [ ! -f "$CORE/modules/analysis/include/felitronics/analysis/ProgrammeReport.h" ] \
    || { echo "$CORE still carries the mastering modules (felitronics-core before v0.52.0) — use a core without them"; exit 1; }
echo "felitronics-core: $CORE"
# Presence, as the CMake checks it; the WAV writer's bytes are proven by felitronics_session_wav_tests, and the
# release by the version floor below.
[ -f "$CORE/modules/io/include/felitronics/io/Wav.h" ] \
    || { echo "felitronics-core at $CORE has no WAV writer (modules/io/include/felitronics/io/Wav.h)"; exit 1; }

INC=(-I"$ROOT/modules/storage/include" -I"$ROOT/tools"
     -I"$ROOT/modules/analysis_offline/include"
     -I"$ROOT/modules/tempo/include"
     -I"$CORE/modules/core/include"
     -I"$CORE/modules/analysis/include"
     -I"$CORE/modules/oversampling/include"
     # The offline analyzers compose over the eq/stereo primitives (eq::Crossover2 is the LR4,
     # stereo::MidSide the M/S pair), so the probe needs their include roots too — what
     # felitronics::analysis_offline links, spelled as include paths.
     -I"$CORE/modules/eq/include"
     -I"$CORE/modules/stereo/include")

# The mastering ABI pulls in the whole chain. This list is `felitronics::mastering`'s own link list in
# modules/mastering/CMakeLists.txt, spelled as include paths — plus oversampling, which analysis needs.
# There is nothing to compile but the two headers' worth of templates: every one of these modules is
# header-only (INTERFACE libraries), which is why one em++ invocation is the whole build.
MASTER_INC=(-I"$ROOT/modules/storage/include" -I"$ROOT/tools"
            -I"$ROOT/modules/mastering/include"
            -I"$ROOT/modules/analysis_offline/include"
            -I"$CORE/modules/core/include"
            -I"$CORE/modules/eq/include"
            -I"$CORE/modules/dynamics/include"
            -I"$CORE/modules/dynamiceq/include"
            -I"$CORE/modules/saturation/include"
            -I"$CORE/modules/limiter/include"
            -I"$CORE/modules/stereo/include"
            -I"$CORE/modules/dither/include"
            -I"$CORE/modules/analysis/include"
            -I"$CORE/modules/oversampling/include")

# The numeric contract. -ffp-contract=off is stated explicitly on BOTH sides rather than relied on: baseline
# wasm has no scalar FMA so emscripten cannot contract anyway, but saying so keeps the two build files
# symmetric and survives a future toolchain that grows the ability.
# NOT -mrelaxed-simd, ever: f64x2.relaxed_madd is implementation-defined (fused on hosts with FMA, unfused
# elsewhere), which breaks determinism between MACHINES, not merely between tiers.
NUMERIC=(-ffp-contract=off -fno-fast-math)

SRC="$HERE/fc_probe.cpp"

# THE EXPORT LIST, READ FROM THE SOURCE — and read by NAME, not by return type. `-sEXPORTED_FUNCTIONS`
# is a whitelist, and until the storage-price entry points this grep only recognised a return type of int/double/std::uint32_t, so an
# entry point whose type drifted outside that set simply left the list — with a hand-maintained FLOOR
# (`-ge 39`) as the only guard, which five missing names would not have moved. The extraction below reads
# the IDENTIFIER in front of the `(` and does not care what precedes it, and the guard is now an EQUALITY
# against the declaration count — `grep -cE '^[[:space:]]*FC_EXPORT'`, whitespace and all: a declaration the
# extractor cannot parse fails the build instead of quietly shortening the ABI.
#
# CORRECTED WHILE DOING IT: this file used to say that a name missing from the list is dead-stripped and
# the page finds it `undefined`, and that "EMSCRIPTEN_KEEPALIVE alone does NOT save it once
# EXPORTED_FUNCTIONS is given". That is FALSE on the toolchain this repo pins. Measured on emscripten
# 6.0.9 with these exact flags at -O3: a KEEPALIVE'd function deliberately left out of
# -sEXPORTED_FUNCTIONS was still present on the Module and still returned its value — `EXPORT_KEEPALIVE`
# defaults to true in src/settings.js, and KEEPALIVE adds the symbol to the export set. So the generated
# list is a STATEMENT OF THE ABI and a sanity check that the source still has one; it is not the thing
# standing between the page and an `undefined`. The gate that actually proves reachability is
# tools/wasm/storage-probe.mjs and the parity harnesses, which call the names on the built artifact.

# Every entry point declared in $1, one per line, with the leading underscore the linker wants. The name is
# THE IDENTIFIER BEFORE THE FIRST `(` — `[^(]*` is what bounds it — and not the last `fc_…` on the line,
# which is how the first version of this read and was wrong: `FC_EXPORT int fc_probe_x (void) { return
# fc_helper (); }` yielded `_fc_helper`, a symbol that is not an entry point at all. Bounding at the first
# `(` also settles `FC_EXPORT fc_status fc_render (…)`, where the return type itself begins with `fc_`.
# Leading whitespace is allowed: an INDENTED declaration used to be invisible to all three scanners at
# once — uncounted, unextracted and untype-checked — while KEEPALIVE published it anyway.
export_names() { [ "$#" -gt 0 ] || { echo "*** export_names: no file to read" >&2; exit 1; }   # never sed's stdin
                 sed -nE 's/^[[:space:]]*FC_EXPORT[[:space:]]+[^(]*[^A-Za-z0-9_]([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\(.*/_\1/p' "$@" \
                 | LC_ALL=C sort -u; }

# THE FILES A MODULE'S ABI IS DECLARED IN: its translation unit and every header of its #include closure that
# declares an entry point, one per line. Every gate below used to read the .cpp alone, and that stopped being the
# whole ABI when the tempo entry points became ONE text compiled into two modules (tools/wasm/fc_tempo_entry.h):
# a grep of fc_probe.cpp would have missed ten names, every count would still have agreed, and — KEEPALIVE being
# what it is (see above) — the artifact would have exported them anyway, so nothing would have gone red while the
# list stopped being a statement of the ABI.
# THE CLOSURE IS THE COMPILER'S (-MM), not a second parser of #include lines, and it is taken with the module's own
# FRONT-END FLAGS — the same array its compile line starts with, -msimd128 and -fno-exceptions included. With fewer
# flags a header included under `#if defined(__wasm_simd128__)` would be compiled into the module and missing from
# the list, and every count would still agree (found by the review round, reproduced with the SDK compiler). System
# headers are not in it and declare no entry point.
# The dependency list is MAKE's format, so it is read as such: `\ ` is a space inside a path, `\#` a `#`, `$$` a `$`,
# a line ending in `\` continues. node reads it (build.sh needs node anyway, for the thread audit); splitting it on
# whitespace in the shell cut a checkout path with a space in it into pieces.
# Called as `LIST=$(abi_files ...)` and never inside a process substitution: there a failed -MM would hand the loop an
# empty list and the build would go on, where in a command substitution `set -e` stops it on the spot.
abi_files() { src="$1"; shift
    deps=$(em++ "$@" -MM -MT fc_abi_closure "$src") \
        || { echo "*** could not list the #include closure of $src" >&2; exit 1; }
    # grep's 1 is "no entry point here"; anything else is a file of the closure this could not read, and dropping it
    # would shorten the list while every count still agreed — so that stops the build instead.
    printf '%s\n' "$deps" | node -e '
        const s = require("fs").readFileSync(0, "utf8").replace(/\\\r?\n/g, " ");
        if (!s.startsWith("fc_abi_closure:")) { console.error("*** -MM printed no rule for fc_abi_closure"); process.exit(1); }
        for (const t of s.slice("fc_abi_closure:".length).match(/(?:\\.|\$\$|[^\s\\$])+/g) ?? [])
            console.log(t.replace(/\$\$/g, "$").replace(/\\(.)/g, "$1"));' \
        | while IFS= read -r f; do
              if grep -qE '^[[:space:]]*FC_EXPORT' "$f"; then echo "$f"
              elif [ $? -ne 1 ]; then echo "*** cannot read $f, a file of the #include closure of $src" >&2; exit 1; fi
          done; }

# ONE DECLARATION PER LINE, because `sed` takes one match per line and the count below is of LINES. Two on
# one line — `FC_EXPORT int fc_a (void) { … } FC_EXPORT std::uint64_t fc_b (void) { … }` — used to drop the
# SECOND from the list while the counts still agreed and the type check read only the first one's return
# type. Both gates passed; the ABI was one name short. (All three of these bypasses were found by the
# review round and reproduced against the gate functions before this was written.)
check_one_per_line() {
    doubled=$(awk '{ if (gsub(/FC_EXPORT/, "") > 1) print FILENAME ":" FNR ": " $0 }' "$@")
    [ -z "$doubled" ] || { echo "*** two entry points on one line — the extractor sees only the first:"; \
                           printf '%s\n' "$doubled"; exit 1; }
    # ...and ONE DECLARATOR per FC_EXPORT. `FC_EXPORT int fc_a (void), fc_b (void);` is one token, one
    # line and one extracted name, so every count above agrees while `_fc_b` is simply absent from the
    # list. The rule is the shape of a declarator: what follows a closed parameter list is a body or a
    # semicolon, never a comma. (Wrapped argument lists are untouched — their line has no `)` at all.)
    comma=$(grep -nE '^[[:space:]]*FC_EXPORT[^()]*\([^()]*\)[[:space:]]*,' "$@" || true)
    [ -z "$comma" ] || { echo "*** a comma declarator — every name after the first is not exported:"; \
                         printf '%s\n' "$comma"; exit 1; }; }

# The count the extractor found against the count of declarations in the file. Equal, or the build stops:
# a floor cannot see a name that fell out, and this can.
#
# `grep -c .` and NOT `wc -l` to count the names: `printf '%s\n' "$EMPTY" | wc -l` is 1, not 0, so a source
# whose single declaration the extractor could not parse would have compared 1 against 1 and passed with an
# EMPTY list. That is why the count is taken of NON-EMPTY LINES.
check_exports() { found="$1"; shift
    [ "$#" -gt 0 ] || { echo "*** no file of this module declares an entry point — refusing to link a module with no ABI"; exit 1; }
    check_one_per_line "$@"
    declared=$(cat "$@" | grep -cE '^[[:space:]]*FC_EXPORT')
    [ "$declared" -gt 0 ] || { echo "*** no FC_EXPORT declarations in $* — refusing to link a module with no ABI"; exit 1; }
    [ "$found" -eq "$declared" ] \
        || { echo "*** $* declare $declared entry points and the extractor found $found —"; \
             echo "    a declaration it cannot parse would silently shorten the ABI"; exit 1; }; }

# THE SECOND GATE, and the reason it is not the first. Making the extraction type-independent means a
# return type this boundary handles BADLY now reaches JavaScript instead of quietly falling off the list,
# which is worse, not better. Measured on the pinned toolchain: a `std::uint64_t` export does cross —
# `WASM_BIGINT` defaults to true — and arrives as a BigInt, a different numeric type from everything else
# this ABI returns: `bigint + number` throws TypeError and JSON.stringify refuses it outright. So the set
# of return types each ABI carries is written down and a new one has to be added here on purpose, with
# somebody having thought about what it looks like on the page. This gate FAILS; it does not omit.
check_return_types() { allowed="$1"; shift
    offenders=$(cat "$@" | grep -E '^[[:space:]]*FC_EXPORT' \
                | grep -vE "^[[:space:]]*FC_EXPORT[[:space:]]+($allowed)[[:space:]]+fc_" || true)
    [ -z "$offenders" ] || { echo "*** $* declare an entry point whose return type this boundary does not carry:"; \
                             printf '%s\n' "$offenders"; \
                             echo "    add it to check_return_types once you know what it looks like in JavaScript"; exit 1; }; }

# The front end of every probe-family compile line (fcprobe, fctempo) — and of the #include closure read above, so
# the list is taken under exactly the preprocessor the module is compiled under. -O3 is the release lines' level;
# the debug line's -O1 defines the same __OPTIMIZE__.
FRONT=(-std=c++20 -fno-exceptions -fno-rtti "${NUMERIC[@]}" "${INC[@]}" -msimd128)
PLIST=$(abi_files "$SRC" "${FRONT[@]}" -O3)
PFILES=(); while IFS= read -r f; do [ -z "$f" ] || PFILES+=("$f"); done <<< "$PLIST"
PNAMES=$(export_names "${PFILES[@]}")
PFOUND=$(printf '%s\n' "$PNAMES" | grep -c . || true)
check_exports "$PFOUND" "${PFILES[@]}"
check_return_types 'int|double|std::uint32_t' "${PFILES[@]}"
PEXPORTS="$(printf '%s\n' "$PNAMES" | paste -sd, -),_malloc,_free"
echo "--- fc_probe exports: $PFOUND entry points (+ _malloc/_free), matching $PFOUND declarations in: ${PFILES[*]##*/}"

# -msimd128: `core::firDot`'s wasm kernel is behind `__wasm_simd128__`, so without it this module
# silently takes the scalar one. Verified against THIS target's own acceptance, which is stricter than
# fc_master's — a byte-for-byte diff against native `fcore_measure blocks`, every number as a raw
# IEEE-754 bit pattern — and not inferred from fc_master: clean on 5 configurations (48k/2ch, 44.1k/1ch,
# 96k/2ch and 44.1k/2ch fixtures, plus a 5:21 stereo programme) in both the release and the checked
# debug build, before AND after the flag. Native runs the NEON kernel, this runs SIMD128: two different
# hand-written kernels, identical bits. On the hot path, 865 -> 435 ms on that programme.
# THE PROBE FAMILY'S RELEASE LEVEL (fcprobe, fctempo): -O3, and binaryen told not to overrule LLVM's inlining. emcc
# runs binaryen's wasm-opt -O3 after the link, and binaryen inlines EVERY function that has exactly one caller —
# whatever LLVM decided, `noinline` included: LLVM's attribute does not reach it (measured on 6.0.9 / binaryen 132,
# with --profiling-funcs: onsetFrame() marked noinline, present in the object file, gone from the module). That
# folded the tempo detector's whole analysis into the one exported call, which V8 then ran unoptimised — it
# optimises a wasm function for its NEXT call, and there is no next call inside one analysis. The first analysis of
# a 6:15 programme took 1.11 s against 0.49 s for every later one (TempoDetector.h, "WHERE THE TIME IS SPENT").
# --one-caller-inline-max-function-size=0 turns that one rule off and nothing else: binaryen still inlines what is
# tiny, and LLVM's own -O3 inlining is untouched. It is an OPTION, not a pass, so it applies although emcc appends
# the extra passes after its -O3 (a `--no-inline=<pattern>` pass placed there would run too late — checked).
# Measured on both modules: fctempo's first tempo 1.11 s -> 0.48 s, fcprobe's 0.62 -> 0.47 s (its programme report
# 1.52 -> 1.04 s, for the same reason), no analyzer slower, every output byte-identical, fcprobe 0.5 KB smaller and
# fctempo 0.3 KB larger under brotli. The debug line keeps its -O1, where emcc runs no binaryen optimiser at all.
# tierup-check.mjs, below, holds the modules to the boundaries this exists for.
RELEASE=(-O3 -sBINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=0)

COMMON=("${FRONT[@]}"
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_NAME=createFcProbe
        -sALLOW_MEMORY_GROWTH=1
        -sFILESYSTEM=0
        -sMALLOC=emmalloc
        "-sEXPORTED_FUNCTIONS=[$PEXPORTS]"
        "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPF64']")
        # _malloc/_free and the HEAP views are OPT-IN in emscripten 6.x — without these two lines
        # Module._malloc and Module.HEAPF32 are simply `undefined` and the page dies on first use.

# Node first: both web builds below write the SAME fcprobe.web.wasm, so each is compared against node's
# the moment it lands — a check run once after both would only see the second.
echo "--- node (same wasm, node glue — for the parity harness)"
em++ "${COMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=node "$SRC" -o "$OUT/fcprobe.node.js"

# same_as_node <module> <glue just built> — the module's web .wasm against its node .wasm.
same_as_node () {
    echo "=== $1.web.wasm from $2 must be byte-identical to node's (ENVIRONMENT/EXPORT_ES6 shape glue, not code)"
    local a b
    a=$(shasum -a 256 "$OUT/$1.web.wasm"  | cut -d' ' -f1)
    b=$(shasum -a 256 "$OUT/$1.node.wasm" | cut -d' ' -f1)
    echo "  web  $a"
    echo "  node $b"
    [ "$a" = "$b" ] && echo "  IDENTICAL" || { echo "  *** DIFFER — a parity result on one does not transfer to the other"; exit 1; }
}

echo "--- web (the spike's artifact, classic glue for probe.html)"
em++ "${COMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=web,worker "$SRC" -o "$OUT/fcprobe.web.js"
same_as_node fcprobe fcprobe.web.js

echo "--- web ES module (for a module worker)"
em++ "${COMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=web,worker -sEXPORT_ES6=1 "$SRC" -o "$OUT/fcprobe.web.mjs"
same_as_node fcprobe fcprobe.web.mjs

echo "--- debug (SAFE_HEAP + assertions + stack checks)"
em++ "${COMMON[@]}" -O1 -g -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_OVERFLOW_CHECK=2 \
     -sENVIRONMENT=node "$SRC" -o "$OUT/fcprobe.debug.js"

echo
echo "=== no threads, proven from the artifact rather than from the page loading"
# Parses the wasm memory section directly, so this checks something on every machine — the earlier
# wasm-objdump step silently checked NOTHING wherever that tool was not installed, which is most machines.
node "$CORE/tools/wasm/check-no-threads.mjs" "$OUT/fcprobe.web.wasm" "$OUT/fcprobe.web.js"
node "$CORE/tools/wasm/check-no-threads.mjs" "$OUT/fcprobe.web.wasm" "$OUT/fcprobe.web.mjs"

# sizes <file>... — raw, gzip -9 and brotli -q 11, what a page actually downloads.
sizes () {
    printf "  %-22s %10s %10s %10s\n" file raw gzip brotli
    local f raw gz br
    for f in "$@"; do
        raw=$(wc -c < "$OUT/$f")
        gz=$(gzip -9 -c "$OUT/$f" | wc -c)
        # Without brotli installed the old `brotli | wc -c || echo n/a` printed "0" AND "n/a" (pipefail): ask first.
        if command -v brotli > /dev/null; then br=$(brotli -q 11 -c "$OUT/$f" | wc -c); else br="n/a"; fi
        printf "  %-22s %10s %10s %10s\n" "$f" "$raw" "$gz" "$br"
    done
}

echo
echo "=== size (acceptance criterion 2)"
sizes fcprobe.web.wasm fcprobe.web.js fcprobe.web.mjs

#==================================================================================================
# fc_tempo — the TEMPO DETECTOR ALONE (tools/fc_tempo_abi.h, tools/wasm/fc_tempo.cpp)
#
# The page's BPM tool needs one analyzer of the probe's twelve and used to download all of them. This module is the
# one: fc_probe's ten tempo entry points under the same names — the same text, tools/wasm/fc_tempo_entry.h, compiled
# here too — plus fc_tempo_abi_version. Built on the probe's line and for the probe's reasons (numeric contract,
# -msimd128, emmalloc, no filesystem, growable memory, the HEAP views); what differs, and why:
#
#  1. -sEXPORT_NAME=createFcTempo, so a page that loads it beside another module gets its own factory.
#  2. TWO artifacts, not four: the ES-module web glue a module worker imports, and node's for the parity harness.
#     No classic .js (that one exists for probe.html alone). No debug variant — the checked configuration of this
#     TU already exists and is stronger: felitronics_fctempo_abi_tests runs fc_tempo.cpp natively under ASan and
#     UBSan, and on the wasm tier's checked build (SAFE_HEAP, ASSERTIONS=2, stack checks), in CI.
#  3. The export list is this TU's include closure, like the others — fc_tempo.cpp declares one name, and the ten
#     it publishes besides are declared in fc_tempo_entry.h, which is exactly the case the closure exists for.
#==================================================================================================
TSRC="$HERE/fc_tempo.cpp"
TLIST=$(abi_files "$TSRC" "${FRONT[@]}" -O3)
TFILES=(); while IFS= read -r f; do [ -z "$f" ] || TFILES+=("$f"); done <<< "$TLIST"
TNAMES=$(export_names "${TFILES[@]}")
TFOUND=$(printf '%s\n' "$TNAMES" | grep -c . || true)
check_exports "$TFOUND" "${TFILES[@]}"
check_return_types 'int|double|std::uint32_t' "${TFILES[@]}"
TEXPORTS="$(printf '%s\n' "$TNAMES" | paste -sd, -),_malloc,_free"
echo
echo "--- fc_tempo exports: $TFOUND entry points (+ _malloc/_free), matching $TFOUND declarations in: ${TFILES[*]##*/}"

TCOMMON=("${FRONT[@]}"
         --no-entry
         -sMODULARIZE=1
         -sEXPORT_NAME=createFcTempo
         -sALLOW_MEMORY_GROWTH=1
         -sFILESYSTEM=0
         -sMALLOC=emmalloc
         "-sEXPORTED_FUNCTIONS=[$TEXPORTS]"
         "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPF64']")

echo "--- fc_tempo node (for the parity harness)"
em++ "${TCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=node "$TSRC" -o "$OUT/fctempo.node.js"
echo "--- fc_tempo web ES module (for a module worker)"
em++ "${TCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=web,worker -sEXPORT_ES6=1 "$TSRC" -o "$OUT/fctempo.web.mjs"
same_as_node fctempo fctempo.web.mjs

echo
echo "=== no threads (fc_tempo)"
node "$CORE/tools/wasm/check-no-threads.mjs" "$OUT/fctempo.web.wasm" "$OUT/fctempo.web.mjs"

echo
echo "=== size (fc_tempo, and what it saves a page that wants only a tempo)"
sizes fctempo.web.wasm fctempo.web.mjs fcprobe.web.wasm

# fcpeaq.node.js — tools/fcore_peaq.cpp, the PEAQ CLI itself, compiled for wasm and run by node on the host's files
# (NODERAWFS). Never shipped: it exists so CI can diff its stdout against the native fcore_peaq's — analysis::Peaq
# promises the same bits on both rows, and this is where the promise is checked. The probe's front flags (contract off,
# no fast math, SIMD128), plus core's io for the WAV reader.
echo
echo "--- fc_peaq node (the PEAQ CLI, diffed against native fcore_peaq)"
em++ "${FRONT[@]}" -I"$CORE/modules/io/include" -O3 -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
     "$ROOT/tools/fcore_peaq.cpp" -o "$OUT/fcpeaq.node.js"

#==================================================================================================
# THE FIRST ANALYSIS — the tempo detector's hot loops must be functions of their own in every module that carries it
# (RELEASE above says why, TempoDetector.h "WHERE THE TIME IS SPENT" says which). Losing one changes no answer, only
# how long the first one takes, so no parity diff can see it; this reads it from the artifact instead of timing it.
# Each module gets a NAMED TWIN — its node line plus --profiling-funcs, into $OUT/tierup/ — and tierup-check.mjs
# proves the twin has the shipped module's function bodies (the same count, sizes within a few bytes: the twin orders
# its functions differently, so an index may take one more LEB128 byte) before it believes its names.
#
# AND THE CHECK IS SHOWN TO SEE WHAT IT GUARDS: the same twin linked WITHOUT the binaryen option must fail it,
# naming the functions binaryen folded. If a toolchain bump makes that control pass, binaryen no longer inlines a
# lone caller — re-measure the first call before deciding the option is dead weight; do not just delete the control.
#==================================================================================================
TIERUP="$OUT/tierup"
mkdir -p "$TIERUP"
HOT=('felitronics::tempo::TempoDetector::mixIn('        # the mix, once per frame
     'felitronics::tempo::TempoDetector::onsetFrame('   # window, transform, magnitudes, flux: once per frame
     'felitronics::tempo::TempoDetector::lagSum('       # the autocorrelation, once per lag
     'felitronics::tempo::TempoDetector::analyzeWindow(')   # once per window of the curve
echo
echo "=== the first analysis: the tempo detector's hot loops are functions of their own"
em++ "${TCOMMON[@]}" "${RELEASE[@]}" --profiling-funcs -sENVIRONMENT=node "$TSRC" -o "$TIERUP/fctempo.names.js"
echo "  fctempo:"
node "$HERE/tierup-check.mjs" "$OUT/fctempo.node.wasm" "$TIERUP/fctempo.names.wasm" "${HOT[@]}"
em++ "${COMMON[@]}" "${RELEASE[@]}" --profiling-funcs -sENVIRONMENT=node "$SRC" -o "$TIERUP/fcprobe.names.js"
echo "  fcprobe:"
node "$HERE/tierup-check.mjs" "$OUT/fcprobe.node.wasm" "$TIERUP/fcprobe.names.wasm" "${HOT[@]}"
echo "  control — fctempo linked at plain -O3, which the check must refuse:"
em++ "${TCOMMON[@]}" -O3 --profiling-funcs -sENVIRONMENT=node "$TSRC" -o "$TIERUP/fctempo.control.js"
if node "$HERE/tierup-check.mjs" "$TIERUP/fctempo.control.wasm" "$TIERUP/fctempo.control.wasm" "${HOT[@]}" \
        > "$TIERUP/control.txt" 2>&1; then
    cat "$TIERUP/control.txt"
    echo "*** CONTROL: without --one-caller-inline-max-function-size=0 every boundary survived — read the note above"; exit 1
fi
grep -qF 'MISSING  felitronics::tempo::TempoDetector::onsetFrame(' "$TIERUP/control.txt" \
    || { cat "$TIERUP/control.txt"; echo "*** CONTROL: the check failed, but not on the folded onsetFrame()"; exit 1; }
echo "  control ok: $(grep -c MISSING "$TIERUP/control.txt") of ${#HOT[@]} boundaries folded without the option, and the check said so"

#==================================================================================================
# fc_master — the MASTERING ABI (tools/fc_master_abi.h, implemented by tools/wasm/fc_master.cpp)
#
# Built exactly like fc_probe above and for the same reasons; only the four things below differ, and each
# is a consequence of what this ABI is rather than a preference:
#
#  1. THE EXPORT LIST IS GENERATED FROM THE SOURCE, not typed here — the same `export_names` /
#     `check_exports` pair the probe uses above, so an entry point added to this ABI is exported by the
#     fact of existing and cannot be forgotten here. (This block used to claim that KEEPALIVE does not
#     save a name left out of -sEXPORTED_FUNCTIONS. It does, on the pinned toolchain — measured; see the
#     corrected note above the probe's list.)
#  2. -sSTACK_SIZE=8388608. The repository's CMakeLists gives this to every emscripten build of this
#     tree and says why (a blown wasm stack does not reliably trap — it produced WRONG ANSWERS before
#     it produced an out-of-bounds). `MasteringChain` heap-allocates, but the solver's per-pass work
#     and this file's own planar pointer tables are automatic, and 64 KB — emscripten's default — is
#     not a number anybody chose for them.
#  3. -msimd128, which is NOT a hopeful flag here. Since v0.31.0 the three polyphase loops of this tree
#     are one function, `core::firDot`, with four hand-written kernels; the wasm one is behind
#     `__wasm_simd128__` and is simply absent without this flag. Measured on a 5:21 stereo programme,
#     one render pass: 8748 ms at v0.30.0 -> 4339 ms on v0.31.0's repacking alone -> **2802 ms** with the
#     kernel, i.e. 3.1x, and a full solve 29.3 s -> 10.6 s.
#     IT IS NUMERICALLY FREE, and that is measured rather than taken on trust: the same programme
#     through the scalar and the SIMD module agrees in ALL 30 877 716 delivered samples BIT FOR BIT, and
#     so do the gain, the ceiling, the integrated loudness and the true peak to 17 digits. That is the
#     kernels' own design — four partial accumulators, (s0+s1)+(s2+s3), every operation rounded
#     separately, identical in all four — and the core gates it with
#     `felitronics_core_polyphasefir_tests`. It went on THIS target first, for that reason: the probe's
#     acceptance is its own, and a flag is not added to a contract it was not measured on. The probe
#     carries it now too — see the -msimd128 note above its own COMMON flags, which records the separate
#     measurement that earned it there.
#  4. -sEXPORT_NAME=createFcMaster, so a page that loads BOTH modules gets two factories rather than
#     one name overwriting the other. Everything else — the numeric contract, emmalloc, no filesystem,
#     growable memory, the HEAPF32/HEAPF64 opt-in — is the probe's line for the probe's reasons.
#
# NOT built: a debug variant. fc_probe has one because libsoxr taught that a release build that "works"
# proves less than a checked one that agrees with it; the checked configuration of THIS ABI already
# exists and is stronger — `felitronics_master_abi_tests`, `felitronics_master_abi_reentry_tests` and
# `fcore_master selftest` run the same fc_master.cpp under ctest with ASan and UBSan.
#==================================================================================================
MSRC="$HERE/fc_master.cpp"

# The whitelist, read from the source — the TU's #include closure, as the probe's — by the same extractor. The old grep here had the
# probe's defect twice over: a return-type whitelist, and `fc_[a-z_]+` for the NAME, which excludes digits
# — an entry point carrying a version number would not have left the list, it would have been TRUNCATED
# into it: `fc_render_v2` becomes `fc_render_v`, and the link then fails on a symbol nobody declared.
# _malloc/_free are the page's own, and are opt-in in emscripten 6.x.
MFRONT=(-std=c++20 -fno-exceptions -fno-rtti "${NUMERIC[@]}" "${MASTER_INC[@]}" -msimd128)
MLIST=$(abi_files "$MSRC" "${MFRONT[@]}" -O3)
MFILES=(); while IFS= read -r f; do [ -z "$f" ] || MFILES+=("$f"); done <<< "$MLIST"
MNAMES=$(export_names "${MFILES[@]}")
MFOUND=$(printf '%s\n' "$MNAMES" | grep -c . || true)
check_exports "$MFOUND" "${MFILES[@]}"
check_return_types 'void|std::uint32_t|uint32_t|fc_status' "${MFILES[@]}"
MEXPORTS="$(printf '%s\n' "$MNAMES" | paste -sd, -),_malloc,_free"
echo "--- fc_master exports: $MFOUND entry points (+ _malloc/_free), matching $MFOUND declarations in: ${MFILES[*]##*/}"

MCOMMON=("${MFRONT[@]}"
         --no-entry
         -sMODULARIZE=1
         -sEXPORT_NAME=createFcMaster
         -sALLOW_MEMORY_GROWTH=1
         -sSTACK_SIZE=8388608
         -sFILESYSTEM=0
         -sMALLOC=emmalloc
         "-sEXPORTED_FUNCTIONS=[$MEXPORTS]"
         "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPF64']")

# -sEXPORT_ES6 on the WEB variant only. The page loads this from a module worker, where an ES module
# with a default export is the only shape an `import` can take; node's harness here keeps the plain
# MODULARIZE factory so `createRequire` still works. It shapes GLUE and nothing else — the byte-identity
# check below is what says so rather than the flag's documentation.
echo "--- fc_master web (ES module, for a module worker)"
em++ "${MCOMMON[@]}" -O3 -sENVIRONMENT=web,worker -sEXPORT_ES6=1 "$MSRC" -o "$OUT/fcmaster.web.mjs"

echo "--- fc_master node (same wasm, node glue — for the parity harness)"
em++ "${MCOMMON[@]}" -O3 -sENVIRONMENT=node "$MSRC" -o "$OUT/fcmaster.node.js"

echo
echo "=== the two fc_master .wasm must be byte-identical, same rule as above"
a=$(shasum -a 256 "$OUT/fcmaster.web.wasm"  | cut -d' ' -f1)
b=$(shasum -a 256 "$OUT/fcmaster.node.wasm" | cut -d' ' -f1)
echo "  web  $a"
echo "  node $b"
[ "$a" = "$b" ] && echo "  IDENTICAL" || { echo "  *** DIFFER"; exit 1; }

echo
echo "=== no threads (fc_master)"
node "$CORE/tools/wasm/check-no-threads.mjs" "$OUT/fcmaster.web.wasm" "$OUT/fcmaster.web.mjs"

echo
echo "=== size"
sizes fcmaster.web.wasm fcmaster.web.mjs

#==================================================================================================
# fc_session — THE SESSION ABI (tools/fc_session_abi.h, tools/wasm/fc_session.cpp) over felitronics::session
#
# The first module with a COMPILED library behind it. Natively felitronics::session is a static library built by
# modules/session/CMakeLists.txt; here its sources go into the module's one em++ line beside the facade, and what that
# brings is the difference from the modules above:
#  1. THE LIBRARY'S FLAGS, READ FROM THE FILE THE TARGET READS — modules/session/build-flags.txt, one line, one group:
#     the native library and this module are compiled with one definition of the flags, not two copies of it. One em++
#     line gives every translation unit those flags, the facade's included — as tools/CMakeLists.txt gives it natively —
#     and the facade includes the library's src/BuildGuards.h first, as the library's own units do (-I modules/session/src).
#  2. THE TWO RELEASES THE LIBRARY REPORTS (Session::version, Session::coreVersion), read from the project() lines of
#     this repository and of the core it is built against — the same two lines the CMake build reads. The library
#     refuses to compile without them.
#  3. THE LIBRARY'S SOURCES FROM THE LIST THE TARGET COMPILES — modules/session/sources.txt — and a refusal if any .cpp
#     under modules/session/src, in any subdirectory, is not on it: a source the module left out would be a library the
#     page does not run. The check is shown to refuse such a tree before it is trusted.
#  4. -Wl,--wrap=pthread_create, as felitronics-core's policy links every emscripten test of this tree: a thread on a
#     live path is then a LINK error here too, not an ENOTSUP stub the no-threads audit cannot see.
#  5. -sEXPORT_NAME=createFcSession; two artifacts, as fctempo: the ES-module web glue a module worker imports, and node's
#     for tools/wasm/session-check.mjs, which holds the module to its EXACT export set — every export of the artifact
#     against the ABI and the runtime's own — runs its surface, and walks one slot to its last generation. And a CONTROL
#     copy with one callable the ABI does not declare (session-controls/extra_export.cpp), which the check must refuse.
#     No debug variant: felitronics_session_abi_tests runs this translation unit natively under ASan and UBSan, and on
#     the wasm tier's checked build (SAFE_HEAP, ASSERTIONS=2) in CI.
#  6. THE CONFIG, embedded and gated as modules/session/CMakeLists.txt embeds and gates it: felitronics-toml's own tool,
#     compiled here for wasm and run through node on the host's files, turns modules/session/config/*.toml into the two
#     headers src/Config.cpp includes, under the same names — a document the parser refuses stops the build at its line
#     and column — and the session's own gate (modules/session/tests/ConfigCheck.cpp with src/ConfigSchema.cpp, the
#     library's schema) reads both by schema before anything is linked: a typo stops the module's build at its line.
#     session-check.mjs then compares the module's config version with the native CLI's.
#  7. THE TEXT, the same way: modules/session/text/catalog.toml and format.toml become the two headers src/Text.cpp
#     includes, and the text's gate (modules/session/tests/TextCheck.cpp with src/TextSchema.cpp) checks both before
#     anything is linked: a message missing in a declared language, or a stray placeholder, stops the module's build.
#==================================================================================================
# project_version <CMakeLists.txt> <project name> — "MAJOR MINOR PATCH" of that project() line, or nothing.
project_version () { sed -nE "s/^project\\($2 VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)[ )].*/\\1 \\2 \\3/p" "$1" | head -1; }
read -r SV_MAJOR SV_MINOR SV_PATCH <<< "$(project_version "$ROOT/CMakeLists.txt" felitronics_mastering_core)" || true
read -r CV_MAJOR CV_MINOR CV_PATCH <<< "$(project_version "$CORE/CMakeLists.txt" felitronics_core)" || true
[ -n "${SV_PATCH:-}" ] || { echo "*** no project(felitronics_mastering_core VERSION x.y.z ...) line in $ROOT/CMakeLists.txt"; exit 1; }
[ -n "${CV_PATCH:-}" ] || { echo "*** no project(felitronics_core VERSION x.y.z ...) line in $CORE/CMakeLists.txt"; exit 1; }
if [ "$CV_MAJOR" -eq 0 ] && [ "$CV_MINOR" -lt 57 ]; then
    echo "*** felitronics-core must be v0.57.0 or later"; exit 1
fi
echo
echo "--- fc_session: felitronics-mastering-core $SV_MAJOR.$SV_MINOR.$SV_PATCH over felitronics-core $CV_MAJOR.$CV_MINOR.$CV_PATCH"

# session_sources <module dir> — the translation units sources.txt lists, one absolute path per line; refuses (exit 1) if
# a listed file is missing or if any .cpp/.cc/.cxx under <module>/src, at any depth, is not listed.
session_sources () { mod="$1"
    listed=$(sed -e 's/#.*//' -e 's/[[:space:]]*$//' "$mod/sources.txt" | grep -v '^$' | LC_ALL=C sort) \
        || { echo "*** $mod/sources.txt lists no source" >&2; return 1; }
    found=$(cd "$mod" && find src -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) | LC_ALL=C sort)
    if [ "$listed" != "$found" ]; then
        echo "*** $mod: the translation units under src/ and the ones sources.txt lists differ:" >&2
        diff <(printf '%s\n' "$listed") <(printf '%s\n' "$found") >&2 || true
        return 1
    fi
    printf '%s\n' "$listed" | sed "s|^|$mod/|"; }

# ...and before it is trusted: a tree with one unlisted source in a SUBDIRECTORY of src/ must be refused.
if [ "${FELITRONICS_WASM_KEEP_CONTROLS:-0}" = 1 ]; then
    SCTL="$(mktemp -d "$OUT/source-control.XXXXXX")"
else
    SCTL="$(mktemp -d)"
fi
mkdir -p "$SCTL/src/sub"
cp "$ROOT/modules/session/sources.txt" "$SCTL/"
while IFS= read -r f; do mkdir -p "$SCTL/$(dirname "$f")"; : > "$SCTL/$f"; done \
    < <(sed -e 's/#.*//' -e 's/[[:space:]]*$//' "$ROOT/modules/session/sources.txt" | grep -v '^$')
: > "$SCTL/src/sub/unlisted.cpp"
if session_sources "$SCTL" > /dev/null 2>&1; then
    [ "${FELITRONICS_WASM_KEEP_CONTROLS:-0}" = 1 ] || rm -rf "$SCTL"
    echo "*** CONTROL: a source in src/sub/ that sources.txt does not list was not refused"; exit 1
fi
if [ "${FELITRONICS_WASM_KEEP_CONTROLS:-0}" = 1 ]; then
    printf 'source-list control retained: %s\n' "$SCTL"
else
    rm -rf "$SCTL"
fi
echo "    control ok: an unlisted source in a subdirectory of src/ is refused"

SESSION_SRCS=(); while IFS= read -r f; do SESSION_SRCS+=("$f"); done < <(session_sources "$ROOT/modules/session")
[ "${#SESSION_SRCS[@]}" -gt 0 ] || { echo "*** no translation unit for felitronics::session"; exit 1; }
SESSION_FLAGS_LINE=$(grep -E '^-' "$ROOT/modules/session/build-flags.txt")
[ "$(printf '%s\n' "$SESSION_FLAGS_LINE" | grep -c .)" -eq 1 ] || { echo "*** modules/session/build-flags.txt must hold one line of flags"; exit 1; }
read -r -a SESSION_FLAGS <<< "$SESSION_FLAGS_LINE"

TOML="${FELITRONICS_TOML_DIR:-$ROOT/../felitronics-toml}"
[ -f "$TOML/include/felitronics/toml/Embedded.h" ] && [ -f "$TOML/tools/toml2cpp.cpp" ] \
    || { echo "no felitronics-toml at $TOML — set FELITRONICS_TOML_DIR to a checkout (v0.3.0 or later)"; exit 1; }
TOML="$(cd "$TOML" && pwd)"
# felitronics-toml states its version on a line of its own inside project( ... ), so it is read from that block.
read -r TV_MAJOR TV_MINOR TV_PATCH <<< "$(sed -nE '/^project\(felitronics_toml/,/\)/ s/.*VERSION ([0-9]+)\.([0-9]+)\.([0-9]+).*/\1 \2 \3/p' \
                                         "$TOML/CMakeLists.txt" | head -1)" || true
[ -n "${TV_PATCH:-}" ] && { [ "$TV_MAJOR" -gt 0 ] || [ "$TV_MINOR" -ge 3 ]; } \
    || { echo "*** felitronics-toml at $TOML is not v0.3.0 or later (${TV_MAJOR:-?}.${TV_MINOR:-?}.${TV_PATCH:-?})"; exit 1; }
echo "--- fc_session config: felitronics-toml $TV_MAJOR.$TV_MINOR.$TV_PATCH at $TOML"
BANDS="${FELITRONICS_BANDS_DIR:-$ROOT/../felitronics-bands}"
[ -f "$BANDS/bands.toml" ] && [ -f "$BANDS/languages.toml" ] \
    || { echo "no felitronics-bands at $BANDS — set FELITRONICS_BANDS_DIR to a checkout"; exit 1; }
BANDS="$(cd "$BANDS" && pwd)"
echo "--- fc_session config: felitronics-bands at $BANDS"

SGEN="$OUT/session-config"
mkdir -p "$SGEN/embedded"
em++ -std=c++20 -O1 -I"$TOML/include" "$TOML/tools/toml2cpp.cpp" \
     -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -o "$SGEN/toml2cpp.js"
for doc in targets engine; do
    node "$SGEN/toml2cpp.js" "$ROOT/modules/session/config/$doc.toml" "$SGEN/embedded/$doc.h" \
         felitronics::session::config::embedded "$doc"
done
for doc in catalog format; do
    node "$SGEN/toml2cpp.js" "$ROOT/modules/session/text/$doc.toml" "$SGEN/embedded/$doc.h" \
         felitronics::session::text::embedded "$doc"
done
# felitronics-bands: bands.toml beside the config, each text/<code>.toml as embedded/band-text/<code>.h, and the header
# that finds a language's (modules/session/band-texts.cmake — the one the CMake build runs).
node "$SGEN/toml2cpp.js" "$BANDS/bands.toml" "$SGEN/embedded/bands.h" felitronics::session::config::embedded bands
mkdir -p "$SGEN/embedded/band-text"
for f in "$BANDS"/text/*.toml; do
    code="$(basename "$f" .toml)"
    node "$SGEN/toml2cpp.js" "$f" "$SGEN/embedded/band-text/$code.h" felitronics::session::text::embedded::bandText "$code"
done
cmake -DBANDS="$BANDS" -DOUTPUT="$SGEN/embedded/band-texts.h" -P "$ROOT/modules/session/band-texts.cmake"
# The analyzers' include roots too (INC): the schema asks them what they admit (their storageFor). The gate compiles the
# library's schema with this front end as well — src/BuildGuards.h, its first include, refuses any other.
SFRONT=(-std=c++20 "${SESSION_FLAGS[@]}"
        -I"$ROOT/tools" -I"$ROOT/modules/session/include" -I"$ROOT/modules/session/src" -I"$TOML/include" -I"$SGEN" "${INC[@]}" "${MASTER_INC[@]}" -msimd128
        -DFELITRONICS_SESSION_VERSION_MAJOR="$SV_MAJOR" -DFELITRONICS_SESSION_VERSION_MINOR="$SV_MINOR"
        -DFELITRONICS_SESSION_VERSION_PATCH="$SV_PATCH"
        -DFELITRONICS_SESSION_CORE_VERSION_MAJOR="$CV_MAJOR" -DFELITRONICS_SESSION_CORE_VERSION_MINOR="$CV_MINOR"
        -DFELITRONICS_SESSION_CORE_VERSION_PATCH="$CV_PATCH")
em++ "${SFRONT[@]}" -O1 "$ROOT/modules/session/tests/ConfigCheck.cpp" "$ROOT/modules/session/src/ConfigSchema.cpp" \
     "$ROOT/modules/session/src/BuildContract.cpp" \
     -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -o "$SGEN/config_check.js"
node "$SGEN/config_check.js" --expect "$ROOT/modules/session/config/targets.toml" "$ROOT/modules/session/config/engine.toml" "$BANDS/bands.toml" "$SGEN/embedded" \
    || { echo "*** the session's config breaks its schema — see above"; exit 1; }
echo "--- fc_session config: read by schema, no problem"
cmake -DOUTPUT="$OUT/snapshot.d.ts" -DVERSION_FILE="$SGEN/embedded/version.txt" -P "$ROOT/tools/session-codec.cmake"
node "$ROOT/tools/session-abi-check.mjs" --generate "$SGEN/abi-probe.cpp"
em++ "${SFRONT[@]}" -O1 "$SGEN/abi-probe.cpp" "${SESSION_SRCS[@]}" -sSTACK_SIZE=8388608 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -o "$SGEN/abi-probe.js"
node "$ROOT/tools/session-abi-check.mjs" node "$SGEN/abi-probe.js"
node "$ROOT/tools/session-abi-check.mjs" --self-test
em++ "${SFRONT[@]}" -O1 "$ROOT/modules/session/tests/TextCheck.cpp" "$ROOT/modules/session/src/TextSchema.cpp" \
     -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -o "$SGEN/text_check.js"
node "$SGEN/text_check.js" "$ROOT/modules/session/text/catalog.toml" "$ROOT/modules/session/text/format.toml" "$BANDS" \
    || { echo "*** the session's text catalog or formatting table breaks its gate — see above"; exit 1; }
echo "--- fc_session text: the catalog and the formatting table checked, no problem"

SSRC="$HERE/fc_session.cpp"
SLIST=$(abi_files "$SSRC" "${SFRONT[@]}" -O3)
SFILES=(); while IFS= read -r f; do [ -z "$f" ] || SFILES+=("$f"); done <<< "$SLIST"
SNAMES=$(export_names "${SFILES[@]}")
SFOUND=$(printf '%s\n' "$SNAMES" | grep -c . || true)
check_exports "$SFOUND" "${SFILES[@]}"
check_return_types 'std::uint32_t|fc_session_status' "${SFILES[@]}"
SEXACT="_fc_kit_eq_curve _fc_kit_eq_curve_bands _fc_kit_heat _fc_kit_low_end_curve _fc_kit_mono_zones _fc_kit_mono_zones_at _fc_kit_parse _fc_kit_position _fc_kit_saturation_curve _fc_kit_text _fc_kit_travel _fc_kit_value_at _fc_session_abi_version _fc_session_attach_audio _fc_session_attach_audio_bytes _fc_session_command _fc_session_command_bytes _fc_session_config_version _fc_session_create _fc_session_create_bytes _fc_session_destroy _fc_session_events_copy _fc_session_events_size _fc_session_export_project_copy _fc_session_export_project_size _fc_session_import_project _fc_session_import_project_bytes _fc_session_load _fc_session_load_bytes _fc_session_load_measured _fc_session_load_measured_bytes _fc_session_master _fc_session_master_audio_copy _fc_session_master_audio_release _fc_session_master_audio_size _fc_session_master_audio_view _fc_session_master_bytes _fc_session_master_wav_copy _fc_session_master_wav_size _fc_session_master_waveform_chunk_bytes _fc_session_master_waveform_chunk_copy _fc_session_master_waveform_chunk_size _fc_session_measurement_bytes _fc_session_needles_bytes _fc_session_query_bytes _fc_session_query_copy _fc_session_query_size _fc_session_set_capacity _fc_session_snapshot_copy _fc_session_snapshot_size _fc_session_step _fc_session_summary_copy _fc_session_summary_size"
[ "$(printf '%s\n' "$SNAMES" | LC_ALL=C sort | paste -sd' ' -)" = "$SEXACT" ] \
    || { echo "*** fc_session exports differ from the frozen v1 list and its declared additions"; exit 1; }
SEXPORTS="$(printf '%s\n' "$SNAMES" | paste -sd, -),_malloc,_free"
echo "--- fc_session exports: $SFOUND entry points (+ _malloc/_free), matching $SFOUND declarations in: ${SFILES[*]##*/}"
echo "    library flags (modules/session/build-flags.txt): ${SESSION_FLAGS[*]}"
echo "    library sources (modules/session/sources.txt): ${SESSION_SRCS[*]##*/}"

SCOMMON=("${SFRONT[@]}"
         -sSTACK_SIZE=8388608
         --no-entry
         -sMODULARIZE=1
         -sEXPORT_NAME=createFcSession
         -sALLOW_MEMORY_GROWTH=1
         -sFILESYSTEM=0
         -sMALLOC=emmalloc
         -Wl,--wrap=pthread_create
         "-sEXPORTED_FUNCTIONS=[$SEXPORTS]"
         "-sEXPORTED_RUNTIME_METHODS=['HEAPU32']")

echo "--- fc_session node (for session-check.mjs)"
em++ "${SCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=node "$SSRC" "${SESSION_SRCS[@]}" -o "$OUT/fcsession.node.js"
echo "--- bounded WAV writer against the installed core WAV reader and writer"
em++ "${SFRONT[@]}" -I"$CORE/modules/io/include" -I"$CORE/test_support" -O3 \
     -sENVIRONMENT=node -sEXIT_RUNTIME=1 \
     "$ROOT/modules/session/tests/WavTests.cpp" "$ROOT/modules/session/src/Wav.cpp" \
     -o "$OUT/session-wav-tests.js"
node "$OUT/session-wav-tests.js"
echo "--- master, cancellation, refusal, safe miss and unavailable WAV eligibility"
em++ "${SFRONT[@]}" -I"$CORE/test_support" -O3 -sENVIRONMENT=node -sSTACK_SIZE=8388608 \
     -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -Wl,--wrap=pthread_create \
     "$ROOT/modules/session/tests/MasterJobTests.cpp" "${SESSION_SRCS[@]}" \
     -o "$OUT/session-master-job-tests.js"
node "$OUT/session-master-job-tests.js"
echo "--- direct landing engine against the Session bridge (FP contraction off)"
em++ "${SFRONT[@]}" -I"$CORE/test_support" -O3 -sENVIRONMENT=node -sSTACK_SIZE=8388608 \
     -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -Wl,--wrap=pthread_create \
     "$ROOT/modules/session/tests/SessionDirectOracleTests.cpp" "${SESSION_SRCS[@]}" \
     -o "$OUT/session-direct-oracle.js"
node "$OUT/session-direct-oracle.js"
if node "$OUT/session-direct-oracle.js" --fault > "$OUT/session-direct-oracle-control.txt" 2>&1; then
    cat "$OUT/session-direct-oracle-control.txt"
    echo "*** direct oracle accepted a one-bit PCM fault"; exit 1
fi
grep -q 'one-bit PCM fault must fail' "$OUT/session-direct-oracle-control.txt" \
    || { cat "$OUT/session-direct-oracle-control.txt"; echo "*** direct oracle fault did not reach comparison"; exit 1; }
echo "    control ok: one-bit PCM fault makes the direct oracle red"
echo "--- saved whole-call solver against PCM-grid measurements (FP contraction off)"
em++ "${SFRONT[@]}" -I"$CORE/test_support" -O3 -sENVIRONMENT=node -sSTACK_SIZE=8388608 \
     -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -Wl,--wrap=pthread_create \
     "$ROOT/modules/mastering/tests/SolverPassDifferentialTests.cpp" \
     -o "$OUT/session-solver-pass-differential.js"
node "$OUT/session-solver-pass-differential.js"
if node "$OUT/session-solver-pass-differential.js" --quantizer-fault \
    > "$OUT/session-quantizer-oracle-control.txt" 2>&1; then
    cat "$OUT/session-quantizer-oracle-control.txt"
    echo "*** saved whole-call oracle accepted a changed sample"; exit 1
fi
grep -q 'a changed old-path sample must be detected' "$OUT/session-quantizer-oracle-control.txt" \
    || { cat "$OUT/session-quantizer-oracle-control.txt"; echo "*** quantizer fault missed the oracle"; exit 1; }
echo "    control ok: changed old-path sample makes the quantizer oracle red"
echo "--- measured ready-master report against native and the old whole renderer"
em++ "${SFRONT[@]}" -I"$CORE/test_support" -O3 -sENVIRONMENT=node -sSTACK_SIZE=8388608 \
     -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -Wl,--wrap=pthread_create \
     "$ROOT/modules/session/tests/MasterReportTests.cpp" "${SESSION_SRCS[@]}" \
     -o "$OUT/session-master-report.js"
node "$OUT/session-master-report.js" > "$OUT/session-master-report.txt"
cat "$OUT/session-master-report.txt"
node "$HERE/master-report-parity.mjs" "$OUT/session-master-report.txt"
echo "--- the scenario end to end: load, measure, plan, hand, master, export, import, master — against its native lines"
em++ "${SFRONT[@]}" -I"$CORE/test_support" -O3 -sENVIRONMENT=node -sSTACK_SIZE=8388608 \
     -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -Wl,--wrap=pthread_create \
     "$ROOT/modules/session/tests/ScenarioTests.cpp" "${SESSION_SRCS[@]}" \
     -o "$OUT/session-scenario.js"
node "$OUT/session-scenario.js" > "$OUT/session-scenario.txt"
cat "$OUT/session-scenario.txt"
node "$HERE/scenario-parity.mjs" "$OUT/session-scenario.txt"
echo "--- complete Solve memory gate (short and long lifecycles)"
em++ "${SFRONT[@]}" -I"$CORE/test_support" -O3 -sENVIRONMENT=node -sSTACK_SIZE=8388608 \
     -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -Wl,--wrap=pthread_create \
     "$ROOT/modules/session/tests/MemoryGateTests.cpp" "${SESSION_SRCS[@]}" \
     -o "$OUT/session-memory-gate.js"
node "$OUT/session-memory-gate.js"
node "$OUT/session-memory-gate.js" --long
echo "--- fc_session web ES module (for a module worker)"
em++ "${SCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=web,worker -sEXPORT_ES6=1 "$SSRC" "${SESSION_SRCS[@]}" -o "$OUT/fcsession.web.mjs"
same_as_node fcsession fcsession.web.mjs

echo "--- fc_session named twin: first analysis keeps the four tempo loops"
em++ "${SCOMMON[@]}" "${RELEASE[@]}" --profiling-funcs -sENVIRONMENT=node "$SSRC" "${SESSION_SRCS[@]}" -o "$TIERUP/fcsession.names.js"
node "$HERE/tierup-check.mjs" "$OUT/fcsession.node.wasm" "$TIERUP/fcsession.names.wasm" "${HOT[@]}"

echo
echo "=== no threads (fc_session)"
node "$CORE/tools/wasm/check-no-threads.mjs" "$OUT/fcsession.web.wasm" "$OUT/fcsession.web.mjs"

echo
echo "=== the module on the artifact: its exact exports, its surface, its wrap boundary (session-check.mjs)"
node "$HERE/session-check.mjs" "$OUT/fcsession.node.js"
echo "  control — the same module with one callable the ABI does not declare, which the check must refuse:"
mkdir -p "$OUT/controls"
em++ "${SCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=node "$SSRC" "${SESSION_SRCS[@]}" "$HERE/session-controls/extra_export.cpp" \
     -o "$OUT/controls/fcsession.node.js"
if node "$HERE/session-check.mjs" "$OUT/controls/fcsession.node.js" > "$OUT/controls/session-check.txt" 2>&1; then
    cat "$OUT/controls/session-check.txt"; echo "*** CONTROL: session-check passed a module that exports debug_probe"; exit 1
fi
grep -q 'debug_probe' "$OUT/controls/session-check.txt" \
    || { cat "$OUT/controls/session-check.txt"; echo "*** CONTROL: session-check failed, but did not name debug_probe"; exit 1; }
echo "  control ok: $(grep -m1 'debug_probe' "$OUT/controls/session-check.txt")"

echo
echo "=== allocation trap and permanent poison (fc_session)"
mkdir -p "$OUT/trap"
em++ "${SCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=node "$SSRC" "${SESSION_SRCS[@]}" "$HERE/session-controls/trap_allocation.cpp" -I"$CORE/test_support" \
     -o "$OUT/trap/fcsession.node.js"
node "$HERE/session-trap-check.mjs" "$OUT/trap/fcsession.node.js" "$OUT/snapshot.mjs"

# The slice 0 script contract uses a separate poisonable instance for its trap
# scenario. The production module's exports remain the frozen v1 set.
SCONTRACT=("${SCOMMON[@]}")
SCONTRACT[${#SCONTRACT[@]}-2]="-sEXPORTED_FUNCTIONS=[$SEXPORTS,_contract_arm_trap,_contract_place]"
mkdir -p "$OUT/contract-trap"
em++ "${SCONTRACT[@]}" "${RELEASE[@]}" -sENVIRONMENT=node "$SSRC" "${SESSION_SRCS[@]}" \
     "$HERE/session-controls/contract_trap.cpp" "$HERE/session-controls/contract_place.cpp" \
     -I"$CORE/test_support" -o "$OUT/contract-trap/fcsession.node.js"

echo "--- fc_session checked contract module (SAFE_HEAP, assertions, stack checks)"
mkdir -p "$OUT/checked/contract-trap"
cp "$OUT/snapshot.mjs" "$OUT/snapshot.d.ts" "$OUT/checked/"
em++ "${SCOMMON[@]}" "${RELEASE[@]}" -sENVIRONMENT=node -sSAFE_HEAP=1 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2 \
     "$SSRC" "${SESSION_SRCS[@]}" -o "$OUT/checked/fcsession.node.js"
em++ "${SCONTRACT[@]}" "${RELEASE[@]}" -sENVIRONMENT=node -sSAFE_HEAP=1 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2 \
     "$SSRC" "${SESSION_SRCS[@]}" "$HERE/session-controls/contract_trap.cpp" "$HERE/session-controls/contract_place.cpp" -I"$CORE/test_support" \
     -o "$OUT/checked/contract-trap/fcsession.node.js"

echo "=== size (fc_session)"
sizes fcsession.web.wasm fcsession.web.mjs

# WHAT THIS WAS BUILT FROM, beside what it built. A consumer that installs these modules records which engine
# it ships, and a checkout's own `git describe` cannot say which core the modules were compiled against: the
# two repositories move separately, and a local build may use a sibling core that is not the pinned one.
# felitronics-toml is recorded too: the config and the text fcsession carries were embedded and gated with it.
describe() { git -C "$1" describe --tags --always --dirty 2>/dev/null || echo unknown; }
TOML_DESCRIBED="$(describe "$TOML")"
[ "$TOML_DESCRIBED" != unknown ] || TOML_DESCRIBED="v$TV_MAJOR.$TV_MINOR.$TV_PATCH (no git checkout)"
{
    echo "felitronics-mastering-core $(describe "$ROOT")"
    echo "felitronics-core $(describe "$CORE")"
    echo "felitronics-toml $TOML_DESCRIBED"
} > "$OUT/BUILD-INFO"
echo
echo "=== built from"
cat "$OUT/BUILD-INFO"
# ONE BUILD: the production module and its contract-trap copy, each pair as this run wrote it, and the sources digest
# taken before it compiled. tools/contract/run.mjs refuses a pair the stamp does not name (tools/contract/module-stamp.mjs).
node "$ROOT/tools/contract/module-stamp.mjs" "$OUT" "$SOURCES_DIGEST"
node "$ROOT/tools/contract/module-stamp.mjs" "$OUT/checked" "$SOURCES_DIGEST"
echo "stamped: $OUT/contract-modules.sha256, $OUT/checked/contract-modules.sha256"
