# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

cmake_minimum_required(VERSION 3.22)
project(felitronics_mastering_core VERSION 0.17.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------------------------------------
# felitronics-mastering-core — the mastering chain, the offline programme analyzers and the mastering session of the
# Felitronics family, over felitronics-core. JUCE-free C++20 under core's laws (felitronics-core
# docs/DSP-ARCHITECTURE.md §2). Header-only, except `session`: a STATIC library compiled with its own PRIVATE flags,
# under the laws of docs/SESSION.md.
#
# Module            Target                          What                                               Deps
#   mastering         felitronics::mastering          the chain, the loudness solver, delivery, render   core stages + analysis
#   analysis_offline  felitronics::analysis_offline   the offline programme analyzers (namespace         analysis, eq, stereo
#                                                     felitronics::analysis, headers <felitronics/analysis/...>)
#   tempo             felitronics::tempo              the tempo of a programme and the tempo over time     core
#                                                     (a port of the site's dsp/tempo.js)
#   session           felitronics::session            the mastering session, the object a shell talks to.  felitronics-toml,
#                                                     COMPILED, with its own flags                        felitronics-bands
#                                                                                                         (build time)
#
# tools/ (top-level builds only): the C ABIs (fc_master, fc_probe, fc_tempo, fc_session), their native CLIs
# (fcore_master, fcore_measure, fcore_session) and suites, and tools/wasm/build.sh, which builds the wasm modules.
#
# Consume — felitronics-core and felitronics-toml FIRST, then this; ONE of each is resolved by the product that owns
# the graph:
#   FetchContent_Declare(felitronics_core GIT_REPOSITORY <url> GIT_TAG <tag>)
#   FetchContent_Declare(felitronics_toml GIT_REPOSITORY <url> GIT_TAG <tag>)
#   FetchContent_MakeAvailable(felitronics_core felitronics_toml)
#   FetchContent_Declare(felitronics_mastering_core GIT_REPOSITORY <url> GIT_TAG <tag>)
#   FetchContent_MakeAvailable(felitronics_mastering_core)
#   target_link_libraries(app PRIVATE felitronics::mastering felitronics::analysis_offline felitronics::session)
# felitronics-bands (the named EQ bands: geometry and names, data only) is fetched by its pinned tag when the product has
# not declared it first; a product that names bands itself declares felitronics_bands before this, so both read one copy.
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
    set(FELITRONICS_MASTERING_FCORE_TAG "v0.59.0" CACHE STRING
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

    # felitronics-toml — the strict TOML subset felitronics::session reads its config with, and the tool that
    # compiles a document into the library as constexpr data (felitronics_toml_embed). Resolved the same way and
    # for the same reason as core: a sibling checkout when there is one, the pinned tag otherwise. Its suites and
    # install rules stay off: both default to "top level only" there.
    set(FELITRONICS_MASTERING_TOML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-toml" CACHE PATH
        "Local felitronics-toml checkout (used if present); else the pinned release is fetched")
    set(FELITRONICS_MASTERING_TOML_TAG "v0.3.0" CACHE STRING
        "felitronics-toml git tag to fetch when no local checkout is present")
    if(FELITRONICS_MASTERING_TOML_DIR AND EXISTS "${FELITRONICS_MASTERING_TOML_DIR}/CMakeLists.txt")
        message(STATUS "felitronics-toml: local checkout — ${FELITRONICS_MASTERING_TOML_DIR}")
        FetchContent_Declare(felitronics_toml SOURCE_DIR "${FELITRONICS_MASTERING_TOML_DIR}")
    else()
        message(STATUS "felitronics-toml: fetching ${FELITRONICS_MASTERING_TOML_TAG} from GitHub")
        FetchContent_Declare(felitronics_toml
            GIT_REPOSITORY https://github.com/darwinscat/felitronics-toml.git
            GIT_TAG        ${FELITRONICS_MASTERING_TOML_TAG}
            GIT_SHALLOW    TRUE)
    endif()
    FetchContent_MakeAvailable(felitronics_toml)

    # felitronics-bands — the named EQ bands every Felitronics product shares: bands.toml (each band's type, hz, q),
    # languages.toml and text/<lang>.toml (each band's name). Data, no CMake: the session embeds the files at build time
    # (modules/session/CMakeLists.txt). Resolved as above: a sibling checkout when there is one, the pinned tag otherwise.
    set(FELITRONICS_MASTERING_BANDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-bands" CACHE PATH
        "Local felitronics-bands checkout (used if present); else the pinned release is fetched")
    if(FELITRONICS_MASTERING_BANDS_DIR AND EXISTS "${FELITRONICS_MASTERING_BANDS_DIR}/bands.toml")
        message(STATUS "felitronics-bands: local checkout — ${FELITRONICS_MASTERING_BANDS_DIR}")
        FetchContent_Declare(felitronics_bands SOURCE_DIR "${FELITRONICS_MASTERING_BANDS_DIR}")
    endif()
endif()

# felitronics-bands by its pinned tag, unless whoever owns the graph declared it first (FetchContent: the first
# declaration wins) — the local checkout above, or a product that names the bands itself.
set(FELITRONICS_MASTERING_BANDS_TAG "v0.1.0" CACHE STRING "felitronics-bands git tag to fetch when nothing else declared it")
include(FetchContent)
FetchContent_Declare(felitronics_bands
    GIT_REPOSITORY https://github.com/darwinscat/felitronics-bands.git
    GIT_TAG        ${FELITRONICS_MASTERING_BANDS_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(felitronics_bands)
if(NOT EXISTS "${felitronics_bands_SOURCE_DIR}/bands.toml" OR NOT EXISTS "${felitronics_bands_SOURCE_DIR}/languages.toml")
    message(FATAL_ERROR "felitronics-bands at '${felitronics_bands_SOURCE_DIR}' has no bands.toml and languages.toml")
endif()
message(STATUS "felitronics-bands: ${felitronics_bands_SOURCE_DIR}")
# The checkout tools/wasm/build.sh must embed — the one this build embedded (CI reads it back like the toml one).
set(FELITRONICS_MASTERING_BANDS_SOURCE_DIR "${felitronics_bands_SOURCE_DIR}" CACHE INTERNAL
    "the felitronics-bands checkout this build resolved")

# Consumed, not top-level: core must already be there. A nested fetch here is how a product ends up with
# two cores at two versions. felitronics::test_support and cmake/FelitronicsPolicy.cmake are also the
# minimum-version check: both first shipped in v0.51.0.
if(NOT TARGET felitronics::core OR NOT TARGET felitronics::test_support
   OR NOT EXISTS "${felitronics_core_SOURCE_DIR}/cmake/FelitronicsPolicy.cmake")
    message(FATAL_ERROR
        "felitronics-mastering-core needs felitronics-core v0.57.0 or later, made available BEFORE this "
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
        "v0.57.0 or later. Point FELITRONICS_MASTERING_FCORE_DIR at such a checkout, or remove the sibling "
        "checkout to fetch FELITRONICS_MASTERING_FCORE_TAG.")
endif()

# The core is resolved above for a top-level build and by the parent for an embedded build. PRESENCE checks only, at
# configure time and before any consumer compiles: the K13 limiter tap the traces read, and the WAV writer the session's
# WAV export is checked against. What they DO is proven by bytes — felitronics_mastering_limiter_trace_tests and
# felitronics_session_wav_tests against the installed core — not by pinning the files' text, which would refuse a
# release that only reworded a comment; the version floor below is what names the release.
set(FELITRONICS_MASTERING_CORE_LIMITER_HEADER
    "${felitronics_core_SOURCE_DIR}/modules/limiter/include/felitronics/limiter/TruePeakLimiter.h")
if(NOT EXISTS "${FELITRONICS_MASTERING_CORE_LIMITER_HEADER}")
    message(FATAL_ERROR "felitronics-mastering-core requires felitronics-core with the K13 limiter tap")
endif()
file(STRINGS "${FELITRONICS_MASTERING_CORE_LIMITER_HEADER}" FELITRONICS_MASTERING_CORE_TAP_DECL
    REGEX "^[ \t]*float\\* peakClipReductionDb[ \t]*=[ \t]*nullptr;")
if(NOT FELITRONICS_MASTERING_CORE_TAP_DECL)
    message(FATAL_ERROR
        "felitronics-mastering-core requires felitronics-core with the K13 limiter tap; "
        "the resolved core at ${felitronics_core_SOURCE_DIR} lacks peakClipReductionDb")
endif()
if(NOT EXISTS "${felitronics_core_SOURCE_DIR}/modules/io/include/felitronics/io/Wav.h")
    message(FATAL_ERROR "felitronics-mastering-core requires the released felitronics-core WAV implementation")
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

# ...and which RELEASE it is, as core's own project() line states it — read from the directory that ran that line, so
# it is the core the graph resolved, whoever resolved it. felitronics::session reports it (Session::coreVersion) and
# fcore_session prints it. No fallback: a build that cannot say which core it compiled against must not pretend to.
get_directory_property(FELITRONICS_MASTERING_FCORE_VERSION DIRECTORY "${felitronics_core_SOURCE_DIR}"
                       DEFINITION felitronics_core_VERSION)
if(NOT FELITRONICS_MASTERING_FCORE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "cannot read felitronics-core's release from ${felitronics_core_SOURCE_DIR} "
                        "(its project() line gave '${FELITRONICS_MASTERING_FCORE_VERSION}')")
endif()
if(FELITRONICS_MASTERING_FCORE_VERSION VERSION_LESS "0.57.0")
    message(FATAL_ERROR "felitronics-mastering-core requires felitronics-core v0.57.0 or later")
endif()

# felitronics-toml, by the same rule: made available before this repository by whoever owns the graph (above, when
# that is this repository). v0.3.0 adds the parse and schema storage declaration used by project import; the config
# still uses felitronics_toml_embed() and the schema reader. Its release is read from the directory that ran its
# project() line, like core's; a package found by find_package states it in felitronics_toml_VERSION instead.
if(NOT TARGET felitronics::toml OR NOT COMMAND felitronics_toml_embed)
    message(FATAL_ERROR
        "felitronics-mastering-core needs felitronics-toml v0.3.0 or later (felitronics::toml and "
        "felitronics_toml_embed()), made available BEFORE this repository, so ONE copy is resolved by the product "
        "that owns the graph.")
endif()
if(DEFINED felitronics_toml_SOURCE_DIR)
    get_directory_property(FELITRONICS_MASTERING_TOML_VERSION DIRECTORY "${felitronics_toml_SOURCE_DIR}"
                           DEFINITION felitronics_toml_VERSION)
else()
    set(FELITRONICS_MASTERING_TOML_VERSION "${felitronics_toml_VERSION}")
endif()
if(NOT FELITRONICS_MASTERING_TOML_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$"
   OR FELITRONICS_MASTERING_TOML_VERSION VERSION_LESS 0.3.0)
    message(FATAL_ERROR "felitronics-mastering-core needs felitronics-toml v0.3.0 or later "
                        "(this graph resolved '${FELITRONICS_MASTERING_TOML_VERSION}')")
endif()
# The checkout tools/wasm/build.sh must embed the config with — the same one this build compiled (CI reads it back with
#   sed -n 's/^FELITRONICS_MASTERING_TOML_SOURCE_DIR:INTERNAL=//p' <build>/CMakeCache.txt ).
set(FELITRONICS_MASTERING_TOML_SOURCE_DIR "${felitronics_toml_SOURCE_DIR}" CACHE INTERNAL
    "the felitronics-toml checkout this build resolved")

# modules/ and tools/ came from core verbatim and build their suites on FELITRONICS_BUILD_TESTS, the name
# the switch has there. Set here as a NORMAL variable, after core is configured, so it never reaches core.
set(FELITRONICS_BUILD_TESTS ${FELITRONICS_MASTERING_BUILD_TESTS})
if(FELITRONICS_MASTERING_BUILD_TESTS)
    enable_testing()
endif()

add_library(felitronics_storage INTERFACE)
target_include_directories(felitronics_storage INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/modules/storage/include)

add_subdirectory(modules/analysis_offline)   # the offline analyzers; analysis + eq + stereo
add_subdirectory(modules/mastering)          # the chain; every core stage it composes + analysis
add_subdirectory(modules/tempo)              # the tempo detector; core alone
add_subdirectory(modules/session)            # the mastering session — COMPILED, with its own flags

# The C ABIs, their CLIs and the wasm plumbing — top-level builds only, as in core.
if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tools)
endif()

if(FELITRONICS_MASTERING_BUILD_TESTS)
    add_subdirectory(tests)   # law 11b for the chain and the renderer, header hygiene, the no-exceptions probe
endif()
