# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# ONE CONTROL OF THE CONFIG CHECK: a planted mistake must turn it red, at the mistake. Run by ctest
# (modules/session/CMakeLists.txt):
#   cmake -DCHECK=<config_check> -DCONFIG=<dir of targets.toml, engine.toml> -DBANDS=<dir of bands.toml>
#         -DWORK=<scratch dir> -DDOC=<targets|engine|bands>
#         -DFROM=<text> -DTO=<text> -DAT=<text> -DEXPECT=<fault and key path> -P config-must-fail.cmake
#
# Both documents are copied to WORK, and the check must pass on the copies first — so the only difference between the
# run that passes and the run that must fail is the plant. Then the one occurrence of FROM in DOC is replaced by TO, and
# the check must exit 1 and print `<WORK>/<DOC>.toml:<line>:<column>: error: <EXPECT>`, where line and column are those
# of AT, searched from the plant onwards. A FROM that is not in the document exactly once is a control that has rotted.
foreach(var CHECK CONFIG BANDS WORK DOC FROM TO AT EXPECT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "config-must-fail.cmake: ${var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${CONFIG}/targets.toml" "${CONFIG}/engine.toml" "${BANDS}/bands.toml" DESTINATION "${WORK}")

execute_process(COMMAND "${CHECK}" "${WORK}/targets.toml" "${WORK}/engine.toml" "${WORK}/bands.toml"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "PRECONDITION: the check must pass on the unplanted copies; it exited ${rc}:\n${err}")
endif()

file(READ "${WORK}/${DOC}.toml" text)
string(FIND "${text}" "${FROM}" first)
string(REPLACE "${FROM}" "" without "${text}")
string(LENGTH "${text}" n)
string(LENGTH "${without}" m)
string(LENGTH "${FROM}" k)
math(EXPR occurrences "(${n} - ${m}) / ${k}")
if(first EQUAL -1 OR NOT occurrences EQUAL 1)
    message(FATAL_ERROR "the control has rotted: '${FROM}' occurs ${occurrences} times in ${DOC}.toml, not once")
endif()
string(SUBSTRING "${text}" 0 ${first} head)
math(EXPR rest "${first} + ${k}")
string(SUBSTRING "${text}" ${rest} -1 tail)
set(planted "${head}${TO}${tail}")
file(WRITE "${WORK}/${DOC}.toml" "${planted}")

# Where AT begins, from the plant onwards: the line, and the column in characters (the lines planted into are ASCII up to
# AT, so bytes are characters there).
string(SUBSTRING "${planted}" ${first} -1 fromPlant)
string(FIND "${fromPlant}" "${AT}" offset)
if(offset EQUAL -1)
    message(FATAL_ERROR "the control has rotted: '${AT}' does not follow the plant")
endif()
math(EXPR at "${first} + ${offset}")
string(SUBSTRING "${planted}" 0 ${at} before)
string(REGEX MATCHALL "\n" newlines "${before}")
list(LENGTH newlines line)
math(EXPR line "${line} + 1")
string(FIND "${before}" "\n" lastNewline REVERSE)
math(EXPR column "${at} - ${lastNewline}")

execute_process(COMMAND "${CHECK}" "${WORK}/targets.toml" "${WORK}/engine.toml" "${WORK}/bands.toml"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(want "${WORK}/${DOC}.toml:${line}:${column}: error: ${EXPECT}")
if(NOT rc EQUAL 1)
    message(FATAL_ERROR "the check did not go red on '${TO}' (exit ${rc}):\n${err}")
endif()
string(FIND "${err}" "${want}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the check went red, but not with\n  ${want}\nit printed:\n${err}")
endif()
message(STATUS "red, as it must be: ${want}")
