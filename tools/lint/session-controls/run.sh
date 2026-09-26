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

node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}" > /dev/null \
    || { echo "the tree is not clean before the controls — they would prove nothing"; node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}"; exit 1; }

BK="$(mktemp -d)"
EDITED=("$SESSION_CPP" "$SESSION_H" "$SOURCES")
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

# --- PRAGMA: a local contraction switch, the operator spelling, a per-function optimisation attribute
append fp-pragma.inc         "$SESSION_CPP"; expect "#pragma clang fp contract(fast)"     "$SESSION_CPP:$LINE: [PRAGMA]"
append pragma-operator.inc   "$SESSION_CPP"; expect "_Pragma(...)"                        "$SESSION_CPP:$LINE: [PRAGMA]"
append optimize-attribute.inc "$SESSION_CPP"; expect "[[gnu::optimize]]"                  "$SESSION_CPP:$LINE: [PRAGMA]"

# --- EXCEPTIONS: a throw only MSVC compiles, in a branch every other row preprocesses away
append guarded-throw.inc     "$SESSION_CPP"; expect "a throw inside #if _MSC_VER"         "$SESSION_CPP:$LINE: [EXCEPTIONS]"

# --- CONDITIONAL: a platform branch in a source of the session
append conditional.inc       "$SESSION_CPP"; expect "#if defined(__APPLE__)"              "$SESSION_CPP:$LINE: [CONDITIONAL]"

# --- NOSYMBOL: an atomic, a cycle counter, an asm label
append atomic.inc            "$SESSION_CPP"; expect "__atomic_add_fetch"                  "$SESSION_CPP:$LINE: [NOSYMBOL]"
append cycle-counter.inc     "$SESSION_CPP"; expect "__builtin_readcyclecounter"          "$SESSION_CPP:$LINE: [NOSYMBOL]"
append asm-label.inc         "$SESSION_CPP"; expect "an asm label"                        "$SESSION_CPP:$LINE: [NOSYMBOL]"

# --- ORDER: an unordered container, an unstable sort
append unordered.inc         "$SESSION_CPP"; expect "std::unordered_map"                  "$SESSION_CPP:$LINE: [ORDER]"
append unstable-sort.inc     "$SESSION_CPP"; expect "std::sort"                           "$SESSION_CPP:$LINE: [ORDER]"

# --- BODY: a function body in the public header
append header-body.inc       "$SESSION_H";   expect "a body in Session.h"                 "$SESSION_H:$LINE: [BODY]"

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

node "$LINT" "${LINTARGS[@]+"${LINTARGS[@]}"}" > /dev/null \
    || { echo "CONTROL FAILED: the tree did not come back clean, so the controls proved nothing"; exit 1; }
echo "all $n session-laws controls ok: every rule fires on its plant, file and line, and the tree is clean again"
