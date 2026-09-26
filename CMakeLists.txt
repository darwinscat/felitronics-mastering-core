# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

cmake_minimum_required(VERSION 3.22)
project(felitronics_mastering_core VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------------------------------------
# felitronics-mastering-core — the mastering chain and the offline programme analyzers of the Felitronics
# family, over felitronics-core. JUCE-free C++20, header-only, under core's laws (felitronics-core
# docs/DSP-ARCHITECTURE.md §2).
#
# Module            Target                          What                                               Deps
#   mastering         felitronics::mastering          the chain, the loudness solver, delivery, render   core stages + analysis
#   analysis_offline  felitronics::analysis_offline   the offline programme analyzers (namespace         analysis, eq, stereo
#                                                     felitronics::analysis, headers <felitronics/analysis/...>)
#   tempo             felitronics::tempo              the tempo of a programme and the tempo over time     core
#                                                     (a port of the site's dsp/tempo.js)
#
# tools/ (top-level builds only): the C ABIs of both (fc_master, fc_probe), their native CLIs
# (fcore_master, fcore_measure) and suites, and tools/wasm/build.sh, which builds the wasm modules.
#
# Consume — felitronics-core FIRST, then this; ONE core is resolved by the product that owns the graph:
#   FetchContent_Declare(felitronics_core GIT_REPOSITORY <url> GIT_TAG <tag>)
#   FetchContent_MakeAvailable(felitronics_core)
#   FetchContent_Declare(felitronics_mastering_core GIT_REPOSITORY <url> GIT_TAG <tag>)
#   FetchContent_MakeAvailable(felitronics_mastering_core)
#   target_link_libraries(app PRIVATE felitronics::mastering felitronics::analysis_offline)
# ----------------------------------------------------------------------------------------------------

option(FELITRONICS_MASTERING_BUILD_TESTS "Build felitronics-mastering-core self-tests" "${PROJECT_IS_TOP_LEVEL}")

# --- felitronics-core, resolved by whoever owns the graph ---------------------------------------------
# Top-level: this repository IS the owner, so it resolves core itself — a sibling checkout when there is
# one (local development), the pinned tag otherwise (what CI gets: it checks out this repository alone, so
# a green CI run proves the PIN, not somebody's working copy).
if(PROJECT_IS_TOP_LEVEL)
    include(FetchContent)
    set(FELITRONICS_MASTERING_FCORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-core" CACHE PATH
        "Local felitronics-core checkout (used if present); else the pinned release is fetched")
    set(FELITRONICS_MASTERING_FCORE_TAG "v0.53.0" CACHE STRING
        "felitronics-core git tag to fetch when no local checkout is present")
    if(FELITRONICS_MASTERING_FCORE_DIR AND EXISTS "${FELITRONICS_MASTERING_FCORE_DIR}/CMakeLists.txt")
        message(STATUS "felitronics-core: local checkout — ${FELITRONICS_MASTERING_FCORE_DIR}")
        FetchContent_Declare(felitronics_core SOURCE_DIR "${FELITRONICS_MASTERING_FCORE_DIR}")
    else()
        message(STATUS "felitronics-core: fetching ${FELITRONICS_MASTERING_FCORE_TAG} from GitHub")
        FetchContent_Declare(felitronics_core
            GIT_REPOSITORY https://github.com/darwinscat/felitronics-core.git
            GIT_TAG        ${FELITRONICS_MASTERING_FCORE_TAG}
            GIT_SHALLOW    TRUE)
    endif()
    # Core's own suites are core's business. A NORMAL variable, which core's option() honours (CMP0077),
    # so this does not rewrite a cache entry the developer set.
    set(FELITRONICS_BUILD_TESTS OFF)
    FetchContent_MakeAvailable(felitronics_core)
endif()

# Consumed, not top-level: core must already be there. A nested fetch here is how a product ends up with
# two cores at two versions. felitronics::test_support and cmake/FelitronicsPolicy.cmake are also the
# minimum-version check: both first shipped in v0.51.0.
if(NOT TARGET felitronics::core OR NOT TARGET felitronics::test_support
   OR NOT EXISTS "${felitronics_core_SOURCE_DIR}/cmake/FelitronicsPolicy.cmake")
    message(FATAL_ERROR
        "felitronics-mastering-core needs felitronics-core v0.53.0 or later, made available BEFORE this "
        "repository (FetchContent_MakeAvailable(felitronics_core) first), so ONE core is resolved by the "
        "product that owns the graph.")
endif()
# ...and a core that still carries these modules is the other half of that check: both repositories would
# define felitronics::mastering, and CMake would stop on the second definition with a message that names
# neither. v0.51.0 is such a core.
if(TARGET felitronics::mastering OR TARGET felitronics::analysis_offline)
    message(FATAL_ERROR
        "this felitronics-core (${felitronics_core_SOURCE_DIR}) still defines felitronics::mastering / "
        "felitronics::analysis_offline, which live here now: felitronics-mastering-core needs felitronics-core "
        "v0.53.0 or later. Point FELITRONICS_MASTERING_FCORE_DIR at such a checkout, or remove the sibling "
        "checkout to fetch FELITRONICS_MASTERING_FCORE_TAG.")
endif()

# Core's build policy — FP contraction (law 10), the MSVC stack + UTF-8, the emscripten physics, the
# sanitizer option, the hygiene warning set — stated for THIS directory by the same file core uses. It is
# load-bearing: the suites here pin bits that depend on FP contraction, and without it they would compile
# under the consumer's (gcc's default is `fast`), not the `on` they compiled under in core.
include(${felitronics_core_SOURCE_DIR}/cmake/FelitronicsPolicy.cmake)

# WHICH core this build resolved, recorded where a script can read it: tools/wasm/build.sh and core's lints are
# not CMake, and each must use the SAME core as the native build it is compared with (CI reads it back with
#   sed -n 's/^FELITRONICS_MASTERING_CORE_SOURCE_DIR:INTERNAL=//p' <build>/CMakeCache.txt ).
set(FELITRONICS_MASTERING_CORE_SOURCE_DIR "${felitronics_core_SOURCE_DIR}" CACHE INTERNAL
    "the felitronics-core checkout this build resolved")

# modules/ and tools/ came from core verbatim and build their suites on FELITRONICS_BUILD_TESTS, the name
# the switch has there. Set here as a NORMAL variable, after core is configured, so it never reaches core.
set(FELITRONICS_BUILD_TESTS ${FELITRONICS_MASTERING_BUILD_TESTS})
if(FELITRONICS_MASTERING_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(modules/analysis_offline)   # the offline analyzers; analysis + eq + stereo
add_subdirectory(modules/mastering)          # the chain; every core stage it composes + analysis
add_subdirectory(modules/tempo)              # the tempo detector; core alone

# The C ABIs, their CLIs and the wasm plumbing — top-level builds only, as in core.
if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tools)
endif()

if(FELITRONICS_MASTERING_BUILD_TESTS)
    add_subdirectory(tests)   # law 11b for the chain and the renderer, header hygiene, the no-exceptions probe
endif()
