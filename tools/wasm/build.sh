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
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE/build}"
mkdir -p "$OUT"

command -v em++ >/dev/null || { echo "em++ not on PATH — source \$EMSDK/emsdk_env.sh first"; exit 1; }
echo "emcc: $(emcc --version | head -1)"

CORE="${FELITRONICS_CORE_DIR:-$ROOT/../felitronics-core}"
[ -f "$CORE/modules/core/include/felitronics/core/DetMath.h" ] \
    || { echo "no felitronics-core at $CORE — set FELITRONICS_CORE_DIR to a checkout (v0.53.0 or later)"; exit 1; }
CORE="$(cd "$CORE" && pwd)"
# A core that still carries these modules would put a SECOND copy of every header here on the include path,
# and which one a TU compiled would depend on the order of the -I flags below. The CMake refuses such a core
# for the same reason; so does this.
[ ! -d "$CORE/modules/mastering" ] && [ ! -f "$CORE/modules/analysis/include/felitronics/analysis/ProgrammeReport.h" ] \
    || { echo "$CORE still carries the mastering modules (felitronics-core before v0.52.0) — use a core without them"; exit 1; }
echo "felitronics-core: $CORE"

INC=(-I"$ROOT/tools"
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
MASTER_INC=(-I"$ROOT/tools"
            -I"$ROOT/modules/mastering/include"
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
export_names() { sed -nE 's/^[[:space:]]*FC_EXPORT[[:space:]]+[^(]*[^A-Za-z0-9_]([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\(.*/_\1/p' "$@" \
                 | LC_ALL=C sort -u; }

# THE FILES A MODULE'S ABI IS DECLARED IN: its translation unit and every header of its #include closure that
# declares an entry point, one per line. Every gate below used to read the .cpp alone, and that stopped being the
# whole ABI when the tempo entry points became ONE text compiled into two modules (tools/wasm/fc_tempo_entry.h):
# a grep of fc_probe.cpp would have missed ten names, every count would still have agreed, and — KEEPALIVE being
# what it is (see above) — the artifact would have exported them anyway, so nothing would have gone red while the
# list stopped being a statement of the ABI.
# THE CLOSURE IS THE COMPILER'S (-MM, with the module's own include roots), not a second parser of #include lines:
# it follows exactly the includes the compile follows, conditionals included. System headers are not in it and
# declare no entry point.
abi_files() { src="$1"; shift
    deps=$(em++ -std=c++20 "$@" -MM "$src") || { echo "*** could not list the #include closure of $src"; exit 1; }
    printf '%s\n' "$deps" | sed -e 's/^[^:]*://' -e 's/\\$//' | tr -s ' \t' '\n' | grep . \
        | while IFS= read -r f; do if grep -qE '^[[:space:]]*FC_EXPORT' "$f"; then echo "$f"; fi; done; }

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

PFILES=(); while IFS= read -r f; do PFILES+=("$f"); done < <(abi_files "$SRC" "${INC[@]}")
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
COMMON=(-std=c++20 -fno-exceptions -fno-rtti "${NUMERIC[@]}" "${INC[@]}"
        -msimd128
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
em++ "${COMMON[@]}" -O3 -sENVIRONMENT=node "$SRC" -o "$OUT/fcprobe.node.js"

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
em++ "${COMMON[@]}" -O3 -sENVIRONMENT=web,worker "$SRC" -o "$OUT/fcprobe.web.js"
same_as_node fcprobe fcprobe.web.js

echo "--- web ES module (for a module worker)"
em++ "${COMMON[@]}" -O3 -sENVIRONMENT=web,worker -sEXPORT_ES6=1 "$SRC" -o "$OUT/fcprobe.web.mjs"
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
        br=$(brotli -q 11 -c "$OUT/$f" 2>/dev/null | wc -c || echo "n/a")
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
TFILES=(); while IFS= read -r f; do TFILES+=("$f"); done < <(abi_files "$TSRC" "${INC[@]}")
TNAMES=$(export_names "${TFILES[@]}")
TFOUND=$(printf '%s\n' "$TNAMES" | grep -c . || true)
check_exports "$TFOUND" "${TFILES[@]}"
check_return_types 'int|double|std::uint32_t' "${TFILES[@]}"
TEXPORTS="$(printf '%s\n' "$TNAMES" | paste -sd, -),_malloc,_free"
echo
echo "--- fc_tempo exports: $TFOUND entry points (+ _malloc/_free), matching $TFOUND declarations in: ${TFILES[*]##*/}"

TCOMMON=(-std=c++20 -fno-exceptions -fno-rtti "${NUMERIC[@]}" "${INC[@]}"
         -msimd128
         --no-entry
         -sMODULARIZE=1
         -sEXPORT_NAME=createFcTempo
         -sALLOW_MEMORY_GROWTH=1
         -sFILESYSTEM=0
         -sMALLOC=emmalloc
         "-sEXPORTED_FUNCTIONS=[$TEXPORTS]"
         "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPF64']")

echo "--- fc_tempo node (for the parity harness)"
em++ "${TCOMMON[@]}" -O3 -sENVIRONMENT=node "$TSRC" -o "$OUT/fctempo.node.js"
echo "--- fc_tempo web ES module (for a module worker)"
em++ "${TCOMMON[@]}" -O3 -sENVIRONMENT=web,worker -sEXPORT_ES6=1 "$TSRC" -o "$OUT/fctempo.web.mjs"
same_as_node fctempo fctempo.web.mjs

echo
echo "=== no threads (fc_tempo)"
node "$CORE/tools/wasm/check-no-threads.mjs" "$OUT/fctempo.web.wasm" "$OUT/fctempo.web.mjs"

echo
echo "=== size (fc_tempo, and what it saves a page that wants only a tempo)"
sizes fctempo.web.wasm fctempo.web.mjs fcprobe.web.wasm

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
MFILES=(); while IFS= read -r f; do MFILES+=("$f"); done < <(abi_files "$MSRC" "${MASTER_INC[@]}")
MNAMES=$(export_names "${MFILES[@]}")
MFOUND=$(printf '%s\n' "$MNAMES" | grep -c . || true)
check_exports "$MFOUND" "${MFILES[@]}"
check_return_types 'void|std::uint32_t|uint32_t|fc_status' "${MFILES[@]}"
MEXPORTS="$(printf '%s\n' "$MNAMES" | paste -sd, -),_malloc,_free"
echo "--- fc_master exports: $MFOUND entry points (+ _malloc/_free), matching $MFOUND declarations in: ${MFILES[*]##*/}"

MCOMMON=(-std=c++20 -fno-exceptions -fno-rtti "${NUMERIC[@]}" "${MASTER_INC[@]}"
         -msimd128
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

# WHAT THIS WAS BUILT FROM, beside what it built. A consumer that installs these modules records which engine
# it ships, and a checkout's own `git describe` cannot say which core the modules were compiled against: the
# two repositories move separately, and a local build may use a sibling core that is not the pinned one.
describe() { git -C "$1" describe --tags --always --dirty 2>/dev/null || echo unknown; }
{
    echo "felitronics-mastering-core $(describe "$ROOT")"
    echo "felitronics-core $(describe "$CORE")"
} > "$OUT/BUILD-INFO"
echo
echo "=== built from"
cat "$OUT/BUILD-INFO"
