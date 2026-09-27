# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# A CONSUMER of felitronics::session, for tests/consumer-compile-line.cmake. It does what a product may innocently do
# before adding the repositories — state its own -ffp-contract=off — and nothing else. felitronics-toml is made
# available before this repository, as core is: the session's config is embedded and read with it.
cmake_minimum_required(VERSION 3.22)
project(felitronics_session_consumer LANGUAGES CXX)

add_compile_options(-ffp-contract=off)

include(FetchContent)
FetchContent_Declare(felitronics_core SOURCE_DIR "${FELITRONICS_CORE_DIR}")
set(FELITRONICS_BUILD_TESTS OFF)
FetchContent_MakeAvailable(felitronics_core)
FetchContent_Declare(felitronics_toml SOURCE_DIR "${FELITRONICS_TOML_DIR}")
FetchContent_MakeAvailable(felitronics_toml)
FetchContent_Declare(felitronics_mastering_core SOURCE_DIR "${FELITRONICS_MASTERING_DIR}")
FetchContent_MakeAvailable(felitronics_mastering_core)
