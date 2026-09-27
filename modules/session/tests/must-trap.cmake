# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

# Run the real boundary in a child process: CTest's WILL_FAIL does not accept a signal as an expected failure.
execute_process(COMMAND ${EMULATOR} "${EXECUTABLE}" --${CONTROL}-limit
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(CONTROL STREQUAL "emit" AND "${rc}" STREQUAL "77")
    message(STATUS "debug bound is disabled (NDEBUG)")
    return()
endif()
if(CONTROL STREQUAL "emit")
    set(marker "batch capacity filled")
elseif(CONTROL STREQUAL "snapshot")
    set(marker "snapshot allocation limit reached")
else()
    message(FATAL_ERROR "unknown trap control: ${CONTROL}")
endif()
if("${rc}" STREQUAL "0" OR NOT out MATCHES "${marker}")
    message(FATAL_ERROR "${CONTROL} limit did not trap after reaching its boundary (${rc}):\n${out}${err}")
endif()
message(STATUS "${CONTROL} limit trapped after reaching its boundary: ${rc}")
