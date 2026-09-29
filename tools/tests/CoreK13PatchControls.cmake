# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

# Reconstruct the pinned pristine header from the independent pre-tap test oracle.
if(NOT DEFINED PATCH_SCRIPT)
    set(PATCH_SCRIPT "${MASTERING_SOURCE}/tools/core-k13-tap.cmake")
endif()
file(READ "${MASTERING_SOURCE}/modules/mastering/tests/PreviousTruePeakLimiter.h" pristine)
string(REPLACE "\r\n" "\n" pristine "${pristine}")
string(REPLACE
    "// Independent whole-call oracle copied from felitronics-core v0.55.0 (6666832).\n// Only the enclosing namespace was renamed to allow both versions in one test.\n\n"
    "" pristine "${pristine}")
string(REPLACE "namespace felitronics::previous_limiter" "namespace felitronics::limiter" pristine "${pristine}")
string(SHA256 base "${pristine}")
if(NOT base STREQUAL "4aecef82d3e9879f1dacab2618059e4c4df09181a0aa901ee86a30e97d8d6a19")
    message(FATAL_ERROR "The pre-tap oracle no longer reconstructs pinned felitronics-core v0.55.0")
endif()

foreach(line_ending IN ITEMS LF CRLF)
    set(core "${CONTROL_DIR}/${line_ending}")
    set(header "${core}/modules/limiter/include/felitronics/limiter/TruePeakLimiter.h")
    get_filename_component(parent "${header}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    set(input "${pristine}")
    if(line_ending STREQUAL "CRLF")
        string(REPLACE "\n" "\r\n" input "${input}")
    endif()
    file(WRITE "${header}" "${input}")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCORE_SOURCE=${core}"
        -P "${PATCH_SCRIPT}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${line_ending} core tap patch failed: ${error}")
    endif()
    file(READ "${header}" patched_source)
    string(REPLACE "\r\n" "\n" patched_source "${patched_source}")
    string(SHA256 patched "${patched_source}")
    if(NOT patched STREQUAL "fef7aad6e4f522d93fa8d9dfacd77ee9d90f74c90147559fc8a180e1b820df7d")
        message(FATAL_ERROR "${line_ending} core tap patch changed the pinned implementation")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCORE_SOURCE=${core}"
        -P "${PATCH_SCRIPT}"
        RESULT_VARIABLE repeated ERROR_VARIABLE repeat_error)
    file(READ "${header}" repeated_source)
    string(REPLACE "\r\n" "\n" repeated_source "${repeated_source}")
    string(SHA256 repeated_hash "${repeated_source}")
    if(NOT repeated EQUAL 0 OR NOT repeated_hash STREQUAL patched)
        message(FATAL_ERROR "${line_ending} core tap patch is not idempotent: ${repeat_error}")
    endif()
endforeach()
