# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

# The wire's SessionStatus union is fc_session_status itself: a status appended to the header reaches snapshot.d.ts
# with no second edit. The generator runs over a shadow copy of the header, never the real one.
file(READ "${HEADER}" header)
string(REPLACE "FC_SESSION_ERR_NOT_PLACED = 18" "FC_SESSION_ERR_NOT_PLACED = 18,\n    FC_SESSION_ERR_CODEC_CONTROL = 19" shadow "${header}")
if(shadow STREQUAL header)
    message(FATAL_ERROR "control anchor FC_SESSION_ERR_NOT_PLACED = 18 not found in ${HEADER}")
endif()
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/fc_session_abi.h" "${shadow}")
file(REMOVE "${WORK}/snapshot.d.ts")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSESSION_ABI_HEADER=${WORK}/fc_session_abi.h" "-DOUTPUT=${WORK}/snapshot.d.ts"
                        -P "${GENERATOR}"
                RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the codec generator refused the shadow header: ${error}")
endif()
file(READ "${WORK}/snapshot.d.ts" ts)
if(NOT ts MATCHES "export type SessionStatus = 0 \\| 1 \\| [^;\n]*\\| 18 \\| 19;")
    message(FATAL_ERROR "an appended fc_session_status value is missing from the generated SessionStatus union")
endif()
message(STATUS "codec control: an appended fc_session_status value reaches the generated SessionStatus union")
