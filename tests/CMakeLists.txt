# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

# Header hygiene and the -fno-exceptions / -fno-rtti probe — Clang/GCC only, as in core (MSVC maps the
# warnings differently and spells the probe /EHs-c-, where it warns rather than errors).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_executable(felitronics_mastering_header_hygiene HeaderHygiene.cpp)
    target_link_libraries(felitronics_mastering_header_hygiene PRIVATE felitronics::mastering felitronics::analysis_offline)
    target_compile_options(felitronics_mastering_header_hygiene PRIVATE ${FELITRONICS_HYGIENE_WARNINGS})
    add_test(NAME felitronics_mastering_header_hygiene COMMAND felitronics_mastering_header_hygiene)

    # Building it IS the assertion.
    add_library(felitronics_mastering_no_exceptions_probe OBJECT HeaderHygiene.cpp)
    target_link_libraries(felitronics_mastering_no_exceptions_probe PRIVATE felitronics::mastering felitronics::analysis_offline)
    target_compile_options(felitronics_mastering_no_exceptions_probe PRIVATE -fno-exceptions -fno-rtti)
endif()
