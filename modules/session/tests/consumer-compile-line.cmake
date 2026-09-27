# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# A CONSUMER'S BUILD OF felitronics::session, and its compile line held to the library's flags. Configures tests/consumer/
# (a parent project that states `add_compile_options(-ffp-contract=off)` before making felitronics-core and this
# repository available — the case in which CMake's option de-duplication once removed the library's own final
# `-ffp-contract=off`) into BUILD, then runs compile-line-gate.cmake on the compile commands it wrote. Configured only:
# the compile line is the question, and it is fully decided at generation.
cmake_minimum_required(VERSION 3.22)
foreach(var GENERATOR CXX CONSUMER BUILD CORE MASTERING GATE FLAGS_FILE SOURCES)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "consumer-compile-line.cmake: ${var} is not set")
    endif()
endforeach()
file(REMOVE_RECURSE "${BUILD}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${CONSUMER}" -B "${BUILD}" -G "${GENERATOR}"
            "-DCMAKE_CXX_COMPILER=${CXX}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            "-DFELITRONICS_CORE_DIR=${CORE}" "-DFELITRONICS_MASTERING_DIR=${MASTERING}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the consumer project did not configure:\n${out}${err}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DCOMPILE_COMMANDS=${BUILD}/compile_commands.json -DTARGETS=felitronics_session.dir
            "-DFLAGS_FILE=${FLAGS_FILE}" "-DSOURCES=${SOURCES}" -P "${GATE}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "IN A CONSUMER'S BUILD (${BUILD}):\n${out}${err}")
endif()
message(STATUS "a consumer that states -ffp-contract=off first: ${out}")
