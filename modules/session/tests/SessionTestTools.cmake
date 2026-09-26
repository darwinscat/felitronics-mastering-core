# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# The gates felitronics::session's tests are built from, as functions.

# A BUILD THAT MUST FAIL, for the named reason: builds <target> (excluded from the default build) from inside ctest and
# requires it to fail with a diagnostic matching <expect> (tests/build-must-fail.cmake). One build of this tree at a
# time: two build tools running in one build directory corrupt its state.
function(felitronics_session_must_fail name target expect)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${target} -DCONFIG=$<CONFIG>
                     "-DEXPECT=${expect}" -P ${PROJECT_SOURCE_DIR}/modules/session/tests/build-must-fail.cmake)
    set_tests_properties(${name} PROPERTIES RESOURCE_LOCK felitronics_build_tree)
endfunction()
