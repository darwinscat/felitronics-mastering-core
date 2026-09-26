#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# THE SESSION-LAWS LINT'S CONTROLS. A lint that cannot fail is not a gate, so every rule of
# tools/lint/check-session-laws.mjs is planted where the lint must find it — the fixtures beside this script, copied into
# modules/session; edits of the facade and of the lists — and the lint is required to FAIL, reporting the rule on the
# planted FILE AND LINE. Planting in the real tree is the point: a fixture checked on its own would prove the matcher and
# not the wiring — that the scan still reaches modules/session, that the lists still read as they say.
#
# Run from the repository root:  tools/lint/session-controls/run.sh      (CI's lints job runs it.)
#
# Everything touched is copied aside first and copied back after each control, and every planted file is removed, on
# success, on failure and on interruption — never `git checkout --`: someone will run this in a tree with work in it.
set -euo pipefail

LINT=tools/lint/check-session-laws.mjs
HERE=tools/lint/session-controls
[ -f "$LINT" ] && [ -d "$HERE" ] || { echo "run from the root of felitronics-mastering-core"; exit 2; }

FACADE=tools/wasm/fc_session.cpp
LISTS=tools/lint/session-laws.txt
ZONEFILE=tools/lint/det-math-zone.txt
SRC=modules/session/src
INC=modules/session/include/felitronics/session

node "$LINT" > /dev/null || { echo "the tree is not clean before the controls — they would prove nothing"; node "$LINT"; exit 1; }

BK="$(mktemp -d)"
EDITED=("$FACADE" "$LISTS" "$ZONEFILE")
for i in "${!EDITED[@]}"; do cp "${EDITED[$i]}" "$BK/$i"; done
PLANTED=()
restore () {
    for i in "${!EDITED[@]}"; do cp "$BK/$i" "${EDITED[$i]}"; done
    for p in "${PLANTED[@]+"${PLANTED[@]}"}"; do rm -f "$p"; done
    PLANTED=()
}
trap 'restore; rm -rf "$BK"' EXIT

n=0
# expect <what was planted> <the substring the lint must print>
expect () {
    n=$((n + 1))
    local out
    if out=$(node "$LINT" 2>&1); then
        restore; echo "CONTROL FAILED ($n): the lint PASSED with $1 planted"; exit 1
    fi
    if ! grep -qF -- "$2" <<< "$out"; then
        restore; echo "CONTROL FAILED ($n): the lint failed with $1 planted, but did not report '$2':"; echo "$out"; exit 1
    fi
    restore
    echo "control $n ok: $1 — $(grep -m1 -F -- "$2" <<< "$out" | cut -c1-150)"
}
# plant <fixture> <destination> [whole-file] — and LINE becomes the line the fixture marks `// VIOLATION`. A fixture
# whose violation is the whole file (the zone control) marks no line and says so.
plant () {
    cp "$HERE/$1" "$2"; PLANTED+=("$2")
    LINE=$( (grep -m1 -n '// VIOLATION' "$2" || true) | cut -d: -f1)
    [ -n "$LINE" ] || [ "${3:-}" = whole-file ] || { restore; echo "CONTROL BROKEN: $1 marks no line // VIOLATION"; exit 1; }
}

# --- GLOBALS, each shape of mutable state, planted in the module
plant namespace-global.cpp      "$SRC/_control_namespace_global.cpp"
expect "a namespace-scope variable in an anonymous namespace"  "$SRC/_control_namespace_global.cpp:$LINE: [GLOBALS]"
plant class-static.h            "$INC/_control_class_static.h"
expect "a class's static data member"                         "$INC/_control_class_static.h:$LINE: [GLOBALS]"
plant function-local-static.cpp "$SRC/_control_function_local_static.cpp"
expect "a function-local static"                              "$SRC/_control_function_local_static.cpp:$LINE: [GLOBALS]"
plant pointer-array.cpp         "$SRC/_control_pointer_array.cpp"
expect "an array of mutable pointers to const characters"     "$SRC/_control_pointer_array.cpp:$LINE: [GLOBALS]"

# --- OS: a forbidden header, and a forbidden call through a header that cannot be banned
plant forbidden-include.cpp     "$SRC/_control_forbidden_include.cpp"
expect "#include <chrono>"                                    "$SRC/_control_forbidden_include.cpp:$LINE: [OS]"
plant forbidden-call.cpp        "$SRC/_control_forbidden_call.cpp"
expect "std::to_string"                                       "$SRC/_control_forbidden_call.cpp:$LINE: [OS]"

# --- ZONE: a clean file of the module that the det-math zone does not list
plant outside-zone.h            "$INC/_control_outside_zone.h" whole-file
expect "a file of modules/session missing from $ZONEFILE"     "$INC/_control_outside_zone.h: [ZONE]"

# --- THE FACADE: a third global beside the two it is allowed, and a second name hidden behind an allowed one
printf '\nnamespace { int g_planted = 0; }\n' >> "$FACADE"
LINE=$(grep -n 'g_planted' "$FACADE" | cut -d: -f1)
expect "a third global in the C boundary"                     "$FACADE:$LINE: [GLOBALS]"
sed -i.bak 's/^volatile std::uint8_t g_callState = kIdle;$/volatile std::uint8_t g_callState = kIdle, g_hidden = kIdle;/' "$FACADE"
rm -f "$FACADE.bak"
LINE=$(grep -n 'g_hidden' "$FACADE" | cut -d: -f1 || true)
[ -n "$LINE" ] || { restore; echo "CONTROL BROKEN: the sed planted nothing in $FACADE"; exit 1; }
expect "a second name declared behind the allowed poison flag" "$FACADE:$LINE: [GLOBALS]"

# --- THE LISTS: an allowance for a global that is not there, a scan of nothing, a line the parser cannot read
printf 'allow-global  %s  g_gone  planted: an allowance with nothing behind it\n' "$FACADE" >> "$LISTS"
expect "an allowance naming no global"                        "[LISTS-ROT] allow-global $FACADE g_gone"
printf 'scan  modules/no-such-module  globals  planted\n' >> "$LISTS"
expect "a scan of a path that does not exist"                 "[LISTS-ROT] scan modules/no-such-module"
printf 'scan  modules/session  globals,clocks  planted: a rule that does not exist\n' >> "$LISTS"
expect "an unknown rule"                                      "[LISTS] unknown rule(s) clocks"

# --- PARSE: a file the reader cannot follow is red, not skipped
printf 'namespace felitronics::session\n{\nvoid broken () {\n' > "$SRC/_control_unbalanced.cpp"; PLANTED+=("$SRC/_control_unbalanced.cpp")
expect "a file whose braces do not close"                     "$SRC/_control_unbalanced.cpp:2: [PARSE]"

node "$LINT" > /dev/null || { echo "CONTROL FAILED: the tree did not come back clean, so the controls proved nothing"; exit 1; }
echo "all $n session-laws controls ok: every rule fires on its plant, file and line, and the tree is clean again"
