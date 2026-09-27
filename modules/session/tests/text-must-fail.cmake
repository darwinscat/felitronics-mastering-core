# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# ONE CONTROL OF THE TEXT'S GATE: a planted mistake must turn it red, at the mistake. Run by ctest
# (modules/session/CMakeLists.txt):
#   cmake -DCHECK=<text_check> -DTEXT=<dir of catalog.toml, format.toml> -DWORK=<scratch dir> -DDOC=<catalog|format>
#         -DFROM=<text> -DTO=<text> -DAT=<text> -DEXPECT=<fault, key path and rule> -P text-must-fail.cmake
#
# Both documents are copied to WORK, and the gate must pass on the copies first — so the only difference between the run
# that passes and the run that must fail is the plant. Then the one occurrence of FROM in DOC is replaced by TO, and the
# gate must exit 1 and print `<WORK>/<DOC>.toml:<line>:<column>: error: <EXPECT>`, where line and column are those of AT
# in the planted document — AT occurs there exactly once: the key, the quote of the value or the [header] the problem
# points at. A FROM or an AT that is not in the document exactly once is a control that has rotted.
foreach(var CHECK TEXT WORK DOC FROM TO AT EXPECT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "text-must-fail.cmake: ${var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${TEXT}/catalog.toml" "${TEXT}/format.toml" DESTINATION "${WORK}")

execute_process(COMMAND "${CHECK}" "${WORK}/catalog.toml" "${WORK}/format.toml"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "PRECONDITION: the gate must pass on the unplanted copies; it exited ${rc}:\n${err}")
endif()

# How many times `needle` occurs in `text`.
function(occurrences text needle result)
    string(REPLACE "${needle}" "" without "${text}")
    string(LENGTH "${text}" n)
    string(LENGTH "${without}" m)
    string(LENGTH "${needle}" k)
    math(EXPR count "(${n} - ${m}) / ${k}")
    set(${result} ${count} PARENT_SCOPE)
endfunction()

file(READ "${WORK}/${DOC}.toml" text)
occurrences("${text}" "${FROM}" count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "the control has rotted: '${FROM}' occurs ${count} times in ${DOC}.toml, not once")
endif()
string(REPLACE "${FROM}" "${TO}" planted "${text}")
file(WRITE "${WORK}/${DOC}.toml" "${planted}")

occurrences("${planted}" "${AT}" count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "the control has rotted: '${AT}' occurs ${count} times in the planted ${DOC}.toml, not once")
endif()
# The line of AT, and its column in characters (the controls plant into lines that are ASCII up to AT, so there bytes are
# characters).
string(FIND "${planted}" "${AT}" at)
string(SUBSTRING "${planted}" 0 ${at} before)
string(REGEX MATCHALL "\n" newlines "${before}")
list(LENGTH newlines line)
math(EXPR line "${line} + 1")
string(FIND "${before}" "\n" lastNewline REVERSE)
math(EXPR column "${at} - ${lastNewline}")

execute_process(COMMAND "${CHECK}" "${WORK}/catalog.toml" "${WORK}/format.toml"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(want "${WORK}/${DOC}.toml:${line}:${column}: error: ${EXPECT}")
if(NOT rc EQUAL 1)
    message(FATAL_ERROR "the gate did not go red on '${TO}' (exit ${rc}):\n${err}")
endif()
string(FIND "${err}" "${want}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the gate went red, but not with\n  ${want}\nit printed:\n${err}")
endif()
message(STATUS "red, as it must be: ${want}")
