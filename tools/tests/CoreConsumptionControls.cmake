# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

# A core that supplies the older targets but no K13 tap must fail during configuration
# whether this repository resolves it or a parent supplies it.
file(TO_CMAKE_PATH "${MASTERING_SOURCE}" MASTERING_SOURCE)
file(TO_CMAKE_PATH "${TOML_SOURCE}" TOML_SOURCE)
file(TO_CMAKE_PATH "${CORE_SOURCE}" CORE_SOURCE)
file(TO_CMAKE_PATH "${CONTROL_DIR}" CONTROL_DIR)
set(fake "${CONTROL_DIR}/pristine-core")
file(MAKE_DIRECTORY "${fake}/cmake" "${fake}/modules/limiter/include/felitronics/limiter")
file(WRITE "${fake}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(felitronics_core VERSION 0.55.0 LANGUAGES CXX)
add_library(core_control INTERFACE)
add_library(felitronics::core ALIAS core_control)
add_library(support_control INTERFACE)
add_library(felitronics::test_support ALIAS support_control)
]=])
file(WRITE "${fake}/cmake/FelitronicsPolicy.cmake" "# Core policy fixture.\n")
file(WRITE "${fake}/modules/limiter/include/felitronics/limiter/TruePeakLimiter.h"
    "struct TruePeakLimiterTap { float* gainReductionDb = nullptr; };\n")

set(generator_args -G "${CONTROL_GENERATOR}")
if(CONTROL_PLATFORM)
    list(APPEND generator_args -A "${CONTROL_PLATFORM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${MASTERING_SOURCE}" -B "${CONTROL_DIR}/top"
    ${generator_args} -DFELITRONICS_MASTERING_FCORE_DIR=${fake}
    -DFELITRONICS_MASTERING_TOML_DIR=${TOML_SOURCE}
    -DFELITRONICS_MASTERING_BUILD_TESTS=OFF
    RESULT_VARIABLE top_result OUTPUT_VARIABLE top_output ERROR_VARIABLE top_error)
if(top_result EQUAL 0 OR NOT "${top_output}${top_error}" MATCHES "lacks[\n ]+peakClipReductionDb")
    message(FATAL_ERROR "A pristine local core did not fail the K13 preflight: ${top_output}${top_error}")
endif()

set(parent_source "${CONTROL_DIR}/parent")
file(MAKE_DIRECTORY "${parent_source}")
set(FAKE_CORE "${fake}")
set(parent_text [=[
cmake_minimum_required(VERSION 3.22)
project(core_parent_control LANGUAGES CXX)
add_subdirectory("@FAKE_CORE@" core)
set(felitronics_core_SOURCE_DIR "@FAKE_CORE@")
set(FELITRONICS_MASTERING_BUILD_TESTS OFF)
add_subdirectory("@MASTERING_SOURCE@" mastering)
]=])
string(CONFIGURE "${parent_text}" parent_text @ONLY)
file(WRITE "${parent_source}/CMakeLists.txt" "${parent_text}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${parent_source}" -B "${CONTROL_DIR}/embedded"
    ${generator_args}
    RESULT_VARIABLE embedded_result OUTPUT_VARIABLE embedded_output ERROR_VARIABLE embedded_error)
if(embedded_result EQUAL 0 OR NOT "${embedded_output}${embedded_error}" MATCHES "lacks[\n ]+peakClipReductionDb")
    message(FATAL_ERROR "A pristine parent core did not fail the K13 preflight: ${embedded_output}${embedded_error}")
endif()

# The WAV writer is checked for presence: a core without it fails during configuration from both paths.
file(COPY "${CORE_SOURCE}/modules/limiter/include/felitronics/limiter/TruePeakLimiter.h"
     DESTINATION "${fake}/modules/limiter/include/felitronics/limiter")
file(REMOVE "${fake}/modules/io/include/felitronics/io/Wav.h")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${MASTERING_SOURCE}" -B "${CONTROL_DIR}/top-wav"
    ${generator_args} -DFELITRONICS_MASTERING_FCORE_DIR=${fake}
    -DFELITRONICS_MASTERING_TOML_DIR=${TOML_SOURCE}
    -DFELITRONICS_MASTERING_BUILD_TESTS=OFF
    RESULT_VARIABLE wav_result OUTPUT_VARIABLE wav_output ERROR_VARIABLE wav_error)
if(wav_result EQUAL 0 OR NOT "${wav_output}${wav_error}" MATCHES "requires the released felitronics-core[\n ]+WAV[\n ]+implementation")
    message(FATAL_ERROR "A local core without a WAV writer passed preflight: ${wav_output}${wav_error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${parent_source}" -B "${CONTROL_DIR}/embedded-wav"
    ${generator_args}
    RESULT_VARIABLE wav_parent_result OUTPUT_VARIABLE wav_parent_output ERROR_VARIABLE wav_parent_error)
if(wav_parent_result EQUAL 0 OR NOT "${wav_parent_output}${wav_parent_error}" MATCHES "requires the released felitronics-core[\n ]+WAV[\n ]+implementation")
    message(FATAL_ERROR "A parent core without a WAV writer passed preflight: ${wav_parent_output}${wav_parent_error}")
endif()

# ...and presence is all the text is held to: the resolved release with a comment added to both headers configures.
# A whole-file pin would refuse it, and a release that rewords a comment is the same release to every byte-level suite.
set(reworded "${CONTROL_DIR}/reworded-core")
file(REMOVE_RECURSE "${reworded}")
file(MAKE_DIRECTORY "${reworded}")
file(COPY "${CORE_SOURCE}/CMakeLists.txt" "${CORE_SOURCE}/cmake" "${CORE_SOURCE}/modules" "${CORE_SOURCE}/test_support"
     DESTINATION "${reworded}")
foreach(header modules/limiter/include/felitronics/limiter/TruePeakLimiter.h modules/io/include/felitronics/io/Wav.h)
    file(APPEND "${reworded}/${header}" "\n// A comment a later release may add.\n")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${MASTERING_SOURCE}" -B "${CONTROL_DIR}/top-reworded"
    ${generator_args} -DFELITRONICS_MASTERING_FCORE_DIR=${reworded}
    -DFELITRONICS_MASTERING_TOML_DIR=${TOML_SOURCE}
    -DFELITRONICS_MASTERING_BUILD_TESTS=OFF
    RESULT_VARIABLE reworded_result OUTPUT_VARIABLE reworded_output ERROR_VARIABLE reworded_error)
if(NOT reworded_result EQUAL 0)
    message(FATAL_ERROR "A released core with a reworded comment was refused at configure: ${reworded_output}${reworded_error}")
endif()
