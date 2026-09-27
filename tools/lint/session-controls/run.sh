#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# THE SESSION-LAWS LINT'S CONTROLS. A lint that cannot fail is not a gate, so every rule of
# tools/lint/check-session-laws.mjs is planted where the lint must find it — the fixtures beside this script, appended to
# the library's own sources or copied into modules/session — and the lint is required to FAIL, reporting the rule on the
# planted FILE AND LINE. Planting in the real tree is the point: a fixture checked on its own would prove the matcher and
# not the wiring — that the scan still starts from the target, follows its includes and reaches the file.
# (The laws the OBJECT FILE answers — writable state, calls to the operating system, the locale, the clock — have their
# own controls in ctest: modules/session/tests/object-controls/.)
#
# Run from the repository root:  tools/lint/session-controls/run.sh [<build dir>]    (CI's lints job runs it)
#
# Everything touched is copied aside first and copied back after each control, and every planted file is removed — on
# success, on failure and on interruption. Never `git checkout --`: someone will run this in a tree with work in it.
set -euo pipefail

LINT=tools/lint/check-session-laws.mjs
HERE=tools/lint/session-controls
BUILD="${1:-build}"
[ -f "$LINT" ] && [ -d "$HERE" ] || { echo "run from the root of felitronics-mastering-core"; exit 2; }
LINTARGS=()
[ -f "$BUILD/compile_commands.json" ] && LINTARGS=(--build "$BUILD")

SESSION_CPP=modules/session/src/Session.cpp
SESSION_H=modules/session/include/felitronics/session/Session.h
SOURCES=modules/session/sources.txt
SRC=modules/session/src
FACADE=tools/wasm/fc_session.cpp
ZONE=tools/lint/det-math-zone.txt

node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}" > /dev/null \
    || { echo "the tree is not clean before the controls — they would prove nothing"; node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}"; exit 1; }

BK="$(mktemp -d)"
EDITED=("$SESSION_CPP" "$SESSION_H" "$SOURCES" "$FACADE" "$ZONE")
for i in "${!EDITED[@]}"; do cp "${EDITED[$i]}" "$BK/$i"; done
PLANTED=()
restore () {
    for i in "${!EDITED[@]}"; do cp "$BK/$i" "${EDITED[$i]}"; done
    for p in "${PLANTED[@]+"${PLANTED[@]}"}"; do rm -rf "$p"; done
    PLANTED=()
}
trap 'restore; rm -rf "$BK"' EXIT

n=0
# expect <what was planted> <the substring the lint must print>
expect () {
    n=$((n + 1))
    local out
    if out=$(node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}" 2>&1); then
        restore; echo "CONTROL FAILED ($n): the lint PASSED with $1 planted"; exit 1
    fi
    if ! grep -qF -- "$2" <<< "$out"; then
        restore; echo "CONTROL FAILED ($n): the lint failed with $1 planted, but did not report '$2':"; echo "$out"; exit 1
    fi
    restore
    echo "control $n ok: $1 — $(grep -m1 -F -- "$2" <<< "$out" | cut -c1-150)"
}
# expect_clean <what was changed> — the lint must PASS (a correct variant it must not refuse)
expect_clean () {
    n=$((n + 1))
    local out
    if ! out=$(node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}" 2>&1); then
        restore; echo "CONTROL FAILED ($n): the lint refused $1:"; echo "$out"; exit 1
    fi
    restore
    echo "control $n ok: $1 — clean"
}
# append <fixture> <file> — the fixture's lines added at the end of <file>; LINE becomes the line it marks // VIOLATION
append () {
    local before
    before=$(wc -l < "$2")
    cat "$HERE/$1" >> "$2"
    LINE=$(( before + $(grep -m1 -n '// VIOLATION' "$HERE/$1" | cut -d: -f1) ))
}
# plant <fixture> <destination> — a copy; LINE becomes the line it marks // VIOLATION, if it marks one
plant () {
    mkdir -p "$(dirname "$2")"
    cp "$HERE/$1" "$2"; PLANTED+=("$2")
    LINE=$( (grep -m1 -n '// VIOLATION' "$2" || true) | cut -d: -f1)
}

# --- INCLUDE: the clock, a forbidden header behind `./`, a standard header in the wrong case
append clock-include.inc     "$SESSION_CPP"; expect "#include <chrono>"                   "$SESSION_CPP:$LINE: [INCLUDE]"
append dot-include.inc       "$SESSION_CPP"; expect "#include <./chrono>" "$SESSION_CPP:$LINE: [INCLUDE]"
append case-include.inc      "$SESSION_CPP"; expect "#include <Memory>"                   "$SESSION_CPP:$LINE: [INCLUDE]"

# --- INCLUDE: a felitronics header that is not admitted by name
append core-header.inc       "$SESSION_CPP"; expect "#include <felitronics/core/FlushToZero.h>" "$SESSION_CPP:$LINE: [INCLUDE]"

# --- PRAGMA: a local contraction switch, the operator spelling
append fp-pragma.inc         "$SESSION_CPP"; expect "#pragma clang fp contract(fast)"     "$SESSION_CPP:$LINE: [PRAGMA]"
append pragma-operator.inc   "$SESSION_CPP"; expect "_Pragma(...)"                        "$SESSION_CPP:$LINE: [PRAGMA]"

# --- ATTRIBUTE: per-function optimisation, alone and second in a list; a section, in three spellings
append optimize-attribute.inc "$SESSION_CPP"; expect "[[gnu::optimize]]"                  "$SESSION_CPP:$LINE: [ATTRIBUTE]"
append attribute-list.inc    "$SESSION_CPP"; expect "[[nodiscard, gnu::optimize]]"        "$SESSION_CPP:$LINE: [ATTRIBUTE]"
append section-attribute.inc "$SESSION_CPP"; expect "[[gnu::section]]"                    "$SESSION_CPP:$LINE: [ATTRIBUTE]"
append gnu-attribute.inc     "$SESSION_CPP"; expect "__attribute__((__section__))"        "$SESSION_CPP:$LINE: [ATTRIBUTE]"
append using-attribute.inc   "$SESSION_CPP"; expect "[[using gnu: section]]"              "$SESSION_CPP:$LINE: [ATTRIBUTE]"

# --- MACRO, DIGRAPH, DIRECTIVE: a #define, token pasting, a directive behind %:, #line
append macro-define.inc      "$SESSION_CPP"; expect "#define"                             "$SESSION_CPP:$LINE: [MACRO]"
append token-paste.inc       "$SESSION_CPP"; expect "a ## b"                              "$SESSION_CPP:$LINE: [MACRO]"
append digraph.inc           "$SESSION_CPP"; expect "%:define"                            "$SESSION_CPP:$LINE: [DIGRAPH]"
append line-directive.inc    "$SESSION_CPP"; expect "#line"                               "$SESSION_CPP:$LINE: [DIRECTIVE]"

# --- the lexer: a keyword split by a backslash-newline, a u8 character literal before a throw
append line-splice.inc       "$SESSION_CPP"; expect "thr\\<newline>ow"                     "$SESSION_CPP:$LINE: [EXCEPTIONS]"
append digit-separator.inc   "$SESSION_CPP"; expect "u8'0' before a throw"                "$SESSION_CPP:$LINE: [EXCEPTIONS]"

# --- EXCEPTIONS: a throw only MSVC compiles, in a branch every other row preprocesses away
append guarded-throw.inc     "$SESSION_CPP"; expect "a throw inside #if _MSC_VER"         "$SESSION_CPP:$LINE: [EXCEPTIONS]"

# --- CONDITIONAL: a platform branch in a source of the session
append conditional.inc       "$SESSION_CPP"; expect "#if defined(__APPLE__)"              "$SESSION_CPP:$LINE: [CONDITIONAL]"

# --- NOSYMBOL: an atomic, a cycle counter, an asm label
append atomic.inc            "$SESSION_CPP"; expect "__atomic_add_fetch"                  "$SESSION_CPP:$LINE: [NOSYMBOL]"
append cycle-counter.inc     "$SESSION_CPP"; expect "__builtin_readcyclecounter"          "$SESSION_CPP:$LINE: [NOSYMBOL]"
append asm-label.inc         "$SESSION_CPP"; expect "an asm label"                        "$SESSION_CPP:$LINE: [NOSYMBOL]"

# --- ORDER: an unordered container, an unstable sort, hash<> unqualified, a using-directive
append unordered.inc         "$SESSION_CPP"; expect "std::unordered_map"                  "$SESSION_CPP:$LINE: [ORDER]"
append unstable-sort.inc     "$SESSION_CPP"; expect "std::sort"                           "$SESSION_CPP:$LINE: [ORDER]"
append unqualified-hash.inc  "$SESSION_CPP"; expect "hash<int> after using std::hash"     "$SESSION_CPP:$LINE: [ORDER]"
append using-namespace.inc   "$SESSION_CPP"; expect "using namespace std"                 "$SESSION_CPP:$LINE: [ORDER]"

# --- BODY and PUBLIC: a function body in the public header; a variable it defines, at namespace scope and as a member
append header-body.inc       "$SESSION_H";   expect "a body in Session.h"                 "$SESSION_H:$LINE: [BODY]"
append header-inline-var.inc "$SESSION_H";   expect "inline int in Session.h"             "$SESSION_H:$LINE: [PUBLIC]"
append header-static-member.inc "$SESSION_H"; expect "inline static member in Session.h"  "$SESSION_H:$LINE: [PUBLIC]"

# --- MUTABLE: a mutable member of a constexpr object in the public header; a mutable lambda in a source
append header-mutable.inc    "$SESSION_H";   expect "a mutable member of a constexpr object" "$SESSION_H:$LINE: [MUTABLE]"
append lambda-mutable.inc    "$SESSION_CPP"; expect "a mutable lambda in Session.cpp"      "$SESSION_CPP:$LINE: [MUTABLE]"

# --- THE C BOUNDARY, under the same rules and no further than its allowance: a second macro, a second platform branch,
# --- the console, its build guards taken away, its zone line taken away
append facade-define.inc     "$FACADE";      expect "a #define in fc_session.cpp"         "$FACADE:$LINE: [MACRO]"
append facade-conditional.inc "$FACADE";     expect "#if defined(__APPLE__) in fc_session.cpp" "$FACADE:$LINE: [CONDITIONAL]"
append facade-include.inc    "$FACADE";      expect "#include <cstdio> in fc_session.cpp" "$FACADE:$LINE: [INCLUDE]"
LINE=$(grep -n -m1 '^#include "BuildGuards.h"' "$FACADE" | cut -d: -f1)
sed -i.bak '/^#include "BuildGuards.h"/d' "$FACADE"; rm -f "$FACADE.bak"
expect "fc_session.cpp without BuildGuards.h first" "$FACADE:$LINE: [GUARD]"
sed -i.bak '/^zone  *tools\/wasm\/fc_session.cpp /d' "$ZONE"; rm -f "$ZONE.bak"
expect "fc_session.cpp out of the det-math zone" "$FACADE: [ZONE]"

# --- GUARD: a translation unit, on the target's list, whose first include is not the build guards
plant unguarded.cpp "$SRC/_control_unguarded.cpp"
printf 'src/_control_unguarded.cpp\n' >> "$SOURCES"
LINTARGS_SAVE=("${LINTARGS[@]+"${LINTARGS[@]}"}"); LINTARGS=()   # the build tree does not know this unit
expect "a unit without BuildGuards.h first" "$SRC/_control_unguarded.cpp:$LINE: [GUARD]"
LINTARGS=("${LINTARGS_SAVE[@]+"${LINTARGS_SAVE[@]}"}")

# --- ZONE: an implementation file under an extension no walk took, reached through #include
plant private.tpp "$SRC/private.tpp"
printf '#include "private.tpp"\n' >> "$SESSION_CPP"
expect "src/private.tpp, included by Session.cpp" "$SRC/private.tpp: [ZONE]"

# --- FILES: a unit sources.txt does not list, in a subdirectory; an orphan header; an unknown type
plant subdir-source.cpp "$SRC/sub/_control_subdir.cpp"; PLANTED+=("$SRC/sub")
expect "a .cpp in src/sub/ that sources.txt does not list" "$SRC/sub/_control_subdir.cpp: [FILES] a translation unit that sources.txt does not list"
plant orphan.h "$SRC/_control_orphan.h"
expect "a header nothing includes" "$SRC/_control_orphan.h: [FILES] nothing in the library compiles or includes this file"
plant unknown-type.foo "$SRC/_control.foo"
expect "a file of an unknown type" "$SRC/_control.foo: [FILES] a file of an unknown type"

# --- FILES, THE CROSS-CHECK: the build's own compile_commands.json, edited — a unit compiled into felitronics_session
# --- that sources.txt does not list, a listed unit the build does not compile; and the file with no `output` fields (CMake
# --- 3.22's Ninja and Makefile generators write none), which must still cross-check, and pass.
if [ ${#LINTARGS[@]} -gt 0 ]; then
    fake () {   # fake <name> <node expression over `cc`, the parsed entries>
        mkdir -p "$BK/$1"
        cp "$BUILD/CMakeCache.txt" "$BK/$1/"
        node -e "const fs = require('fs'); let cc = JSON.parse(fs.readFileSync(process.argv[1], 'utf8')); cc = ($2); fs.writeFileSync(process.argv[2], JSON.stringify(cc))" \
            "$BUILD/compile_commands.json" "$BK/$1/compile_commands.json"
        LINTARGS=(--build "$BK/$1")
    }
    SESSION_ENTRY="e => /felitronics_session\\.dir/.test(e.output || e.command) && /Session\\.cpp\$/.test(e.file)"
    fake cc-extra "cc.concat(cc.filter($SESSION_ENTRY).map(e => ({ ...e, file: e.file.replace(/Session\\.cpp\$/, 'Planted.cpp'), output: (e.output || '').replace('Session.cpp', 'Planted.cpp'), command: e.command.split('Session.cpp').join('Planted.cpp') })))"
    expect "a unit compiled into felitronics_session that sources.txt does not list" "[FILES] felitronics_session compiles $SRC/Planted.cpp, which sources.txt does not list"
    fake cc-missing "cc.filter(e => ! (/felitronics_session\\.dir/.test(e.output || e.command) && /BuildContract\\.cpp\$/.test(e.file)))"
    expect "a listed unit the build does not compile" "$SRC/BuildContract.cpp is on sources.txt but the build does not compile it into felitronics_session"
    fake cc-no-output "cc.map(({ output, ...e }) => e)"
    expect_clean "compile commands without \`output\` fields"
    LINTARGS=(--build "$BUILD")
fi

node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}" > /dev/null \
    || { echo "CONTROL FAILED: the tree did not come back clean, so the controls proved nothing"; exit 1; }
echo "all $n session-laws controls ok: every rule fires on its plant, file and line, and the tree is clean again"
