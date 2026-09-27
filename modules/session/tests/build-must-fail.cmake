# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# A BUILD THAT MUST FAIL, and fail for the named reason. Run by ctest (modules/session/CMakeLists.txt):
#   cmake -DBUILD_DIR=<build tree> -DTARGET=<planted control> -DCONFIG=<config> -DEXPECT=<regex> -P build-must-fail.cmake
#
# Two conditions, both required. The build must FAIL — a planted construct that compiles means the flag that forbids
# it did not reach the session library's compile line. And the failure must carry the compiler's diagnostic for THAT
# construct (EXPECT) — a build that fails for any other reason (a typo in the control, a missing include) would
# otherwise read as a working gate. The clean twin of every control is part of the ordinary build, so the setup
# itself is known to compile.
foreach(var BUILD_DIR TARGET EXPECT)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "build-must-fail.cmake: ${var} is not set")
    endif()
endforeach()
# CONFIG may be EMPTY, and that is a valid build: a single-configuration generator with no CMAKE_BUILD_TYPE has no
# configuration to name, and `--config ""` would be a malformed command line rather than a build.
if(NOT DEFINED CONFIG)
    message(FATAL_ERROR "build-must-fail.cmake: CONFIG is not passed (it may be empty, but it must be passed)")
endif()
set(config_args "")
if(NOT CONFIG STREQUAL "")
    set(config_args --config "${CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}" ${config_args}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE  err)

if(rc EQUAL 0)
    message(FATAL_ERROR "CONTROL FAILED: ${TARGET} BUILT. The construct it plants compiled with the session library's "
                        "compile options — the flag that forbids it did not reach felitronics::session.\n${out}${err}")
endif()
if(NOT "${out}${err}" MATCHES "${EXPECT}")
    message(FATAL_ERROR "CONTROL FAILED: ${TARGET} failed to build, but not with the diagnostic it plants "
                        "(expected /${EXPECT}/). A build that fails for another reason proves nothing about the flag.\n"
                        "${out}${err}")
endif()
string(REGEX MATCH "[^\n]*(${EXPECT})[^\n]*" line "${out}${err}")
message(STATUS "control ok: ${TARGET} is refused by the compiler — ${line}")
