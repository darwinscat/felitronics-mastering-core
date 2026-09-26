# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# ONE RUN OF A CLI, HELD TO ITS EXIT STATUS AND ITS STDOUT BYTE FOR BYTE. Run by ctest (tools/CMakeLists.txt):
#   cmake -DEXE=<cli> -DNARGS=<n> -DARG0=<a> ... -DEXPECT_RC=<n> -DEXPECT_STDOUT=<file> -DSTDIN=<file|-> -P cli-expect.cmake
#
# Both halves, because either alone lets a broken run through: a refusal that printed a partial result, or the right
# text with the wrong status, is what a caller scripting the CLI would trust and should not. stderr is shown on a
# failure and otherwise not judged — it is where a CLI of this repository says why. Line endings are compared as LF:
# a Windows console run writes CRLF, which is the platform's spelling of the same bytes.
foreach(var EXE NARGS EXPECT_RC EXPECT_STDOUT STDIN)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "cli-expect.cmake: ${var} is not set")
    endif()
endforeach()

set(cmd "${EXE}")
if(NARGS GREATER 0)
    math(EXPR last "${NARGS} - 1")
    foreach(i RANGE ${last})
        list(APPEND cmd "${ARG${i}}")
    endforeach()
endif()

if(STDIN STREQUAL "-")
    execute_process(COMMAND ${cmd} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
else()
    execute_process(COMMAND ${cmd} INPUT_FILE "${STDIN}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
endif()
file(READ "${EXPECT_STDOUT}" want)
string(REPLACE "\r\n" "\n" out "${out}")

string(REPLACE ";" " " shown "${cmd}")
if(NOT "${rc}" STREQUAL "${EXPECT_RC}")
    message(FATAL_ERROR "`${shown}` exited ${rc}, expected ${EXPECT_RC}.\nstdout:\n${out}\nstderr:\n${err}")
endif()
if(NOT "${out}" STREQUAL "${want}")
    message(FATAL_ERROR "`${shown}` printed a different stdout.\nexpected:\n${want}\ngot:\n${out}\nstderr:\n${err}")
endif()
message(STATUS "ok: `${shown}` -> exit ${rc}, stdout as expected${err}")
