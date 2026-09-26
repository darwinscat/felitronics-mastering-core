# felitronics::session — the mastering session, the one object a shell talks to (docs/SESSION.md). The repository's
# first COMPILED target: every other module here is header-only and compiles under whatever flags its consumer
# chooses; this one is a static library, so its sources are compiled once, with the flags below, and a desktop
# application that links it directly cannot recompile the brain with its own.
add_library(felitronics_session STATIC
    src/Session.cpp
    src/BuildContract.cpp)   # the target's flags, asserted from inside the target
add_library(felitronics::session ALIAS felitronics_session)
target_include_directories(felitronics_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(felitronics_session PUBLIC cxx_std_20)

# THE BRAIN'S FLAGS — PRIVATE, so they are the library's and never its consumer's, and so a consumer's cannot reach
# its sources. They come AFTER the directory's own options on the command line (core's cmake/FelitronicsPolicy.cmake
# states -ffp-contract=on for this tree), and for gcc and clang the last spelling wins:
#   -fno-fast-math -ffp-contract=off   in THAT order. -fno-fast-math does not undo an explicit -ffp-contract=fast
#                   that came before it (measured, Apple clang 21: `-ffp-contract=fast -fno-fast-math` still emits
#                   fmadd), so the contraction switch is stated last, where nothing after it can move it.
#   -fno-exceptions -fno-rtti          a throw, a try, a typeid or a dynamic_cast in a session source is a compile
#                   error.
#   MSVC: /fp:precise (its default, stated), /EHs-c- /GR-, and /we4530 /we4541 to make a `try` and a typeid or
#   dynamic_cast errors rather than warnings. A bare `throw` still compiles under /EHs-c- (measured, MSVC 19.44), so
#   on that row the clang and gcc rows are what hold it. The consumer's default /EHsc and /GR come first and are
#   overridden, which MSVC reports as D9025 — expected, and harmless.
# src/BuildContract.cpp refuses to compile if these did not arrive, and tests/controls/ plants each forbidden
# construct and requires the build to fail.
target_compile_options(felitronics_session PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/fp:precise;/EHs-c-;/GR-;/we4530;/we4541>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-fast-math;-ffp-contract=off;-fno-exceptions;-fno-rtti>)

# The hygiene warning set of this tree, -Werror included — in THIS repository's own builds only. A consumer builds the
# library too, with whatever compiler it has, and a warning a newer compiler invents must not break its build.
if(FELITRONICS_BUILD_TESTS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(felitronics_session PRIVATE ${FELITRONICS_HYGIENE_WARNINGS})
endif()

# THE TWO RELEASES THE LIBRARY REPORTS (Session::version, Session::coreVersion): this repository's project() line, and
# felitronics-core's, which the top-level CMakeLists.txt reads from the directory that defined it (whichever core the
# graph resolved). No fallback: a library that cannot say which releases built it must not be built.
# tools/wasm/build.sh reads the same two project() lines for the wasm module.
if(NOT "${FELITRONICS_MASTERING_FCORE_VERSION}" MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "felitronics::session: no felitronics-core release (got '${FELITRONICS_MASTERING_FCORE_VERSION}')")
endif()
set(FELITRONICS_SESSION_CORE_VERSION_MAJOR ${CMAKE_MATCH_1})
set(FELITRONICS_SESSION_CORE_VERSION_MINOR ${CMAKE_MATCH_2})
set(FELITRONICS_SESSION_CORE_VERSION_PATCH ${CMAKE_MATCH_3})
if(NOT "${felitronics_mastering_core_VERSION}" MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "felitronics::session: this repository's project() states no MAJOR.MINOR.PATCH version "
                        "(got '${felitronics_mastering_core_VERSION}')")
endif()
set(FELITRONICS_SESSION_VERSION_DEFINITIONS
    FELITRONICS_SESSION_VERSION_MAJOR=${felitronics_mastering_core_VERSION_MAJOR}
    FELITRONICS_SESSION_VERSION_MINOR=${felitronics_mastering_core_VERSION_MINOR}
    FELITRONICS_SESSION_VERSION_PATCH=${felitronics_mastering_core_VERSION_PATCH}
    FELITRONICS_SESSION_CORE_VERSION_MAJOR=${FELITRONICS_SESSION_CORE_VERSION_MAJOR}
    FELITRONICS_SESSION_CORE_VERSION_MINOR=${FELITRONICS_SESSION_CORE_VERSION_MINOR}
    FELITRONICS_SESSION_CORE_VERSION_PATCH=${FELITRONICS_SESSION_CORE_VERSION_PATCH})
target_compile_definitions(felitronics_session PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})

if(FELITRONICS_BUILD_TESTS)
    # The session and its laws that a running program can check: the demand of create() against what create() asks
    # the heap for (law 11d, through the one allocation counter), the harness proven able to fail, the two versions
    # against the build's, and the contraction probe answered from inside the library while THIS translation unit is
    # compiled with contraction forced on — a consumer with other flags, which the library's answer must not follow.
    add_executable(felitronics_session_tests tests/SessionTests.cpp)
    target_link_libraries(felitronics_session_tests PRIVATE felitronics::session felitronics::test_support)
    target_compile_features(felitronics_session_tests PRIVATE cxx_std_20)
    target_compile_definitions(felitronics_session_tests PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
    target_compile_options(felitronics_session_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/fp:contract>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=fast>)
    add_test(NAME felitronics_session_tests COMMAND felitronics_session_tests)

    # THE BUILD CONTROLS — the flags above, proven able to fail. Each control is one source compiled twice with the
    # session library's OWN compile options (read off the target, not restated): clean, as part of every build, which
    # proves the setup compiles; and with the forbidden construct planted (FELITRONICS_SESSION_PLANT), outside the
    # default build, where the test below builds it and requires the build to FAIL with the compiler's diagnostic for
    # that construct. So the only difference between the build that must pass and the one that must fail is the
    # construct, and dropping a flag from the library turns its control red.
    #   throw        clang and gcc only: MSVC compiles a throw under /EHs-c- (see the flags above)
    set(FELITRONICS_SESSION_CONTROLS try typeid dynamic_cast)
    # The diagnostics, spelled per compiler (clang and em++ / gcc / MSVC) and matched as phrases: a looser pattern
    # matched a control's own FILE NAME in the build log and a flag in its command line, and passed a build that had
    # failed for any reason at all. The quotes around a keyword are matched as "not a space", not as ': gcc quotes with
    # ‘typeid’ in a UTF-8 locale and with 'typeid' in the C locale (measured on gcc 14.2, deb and the arm64 row), and
    # CI's runners are UTF-8.
    set(FELITRONICS_SESSION_CONTROL_EXPECT_try
        "cannot use [^ ]+try[^ ]+ with exceptions disabled|exception handling disabled, use|error C4530")
    set(FELITRONICS_SESSION_CONTROL_EXPECT_throw
        "cannot use [^ ]+throw[^ ]+ with exceptions disabled|exception handling disabled, use")
    set(FELITRONICS_SESSION_CONTROL_EXPECT_typeid
        "use of typeid requires -frtti|cannot use [^ ]+typeid[^ ]+ with|error C4541: [^ ]+typeid")
    set(FELITRONICS_SESSION_CONTROL_EXPECT_dynamic_cast
        "use of dynamic_cast requires -frtti|[^ ]+dynamic_cast[^ ]+ not permitted with|error C4541: [^ ]+dynamic_cast")
    if(NOT MSVC)
        list(APPEND FELITRONICS_SESSION_CONTROLS throw)
    endif()
    foreach(ctl IN LISTS FELITRONICS_SESSION_CONTROLS)
        foreach(variant clean planted)
            set(tgt felitronics_session_control_${ctl}_${variant})
            if(variant STREQUAL "clean")
                add_library(${tgt} OBJECT tests/controls/${ctl}.cpp)
            else()
                add_library(${tgt} OBJECT EXCLUDE_FROM_ALL tests/controls/${ctl}.cpp)
                target_compile_definitions(${tgt} PRIVATE FELITRONICS_SESSION_PLANT=1)
            endif()
            target_compile_features(${tgt} PRIVATE cxx_std_20)
            target_compile_options(${tgt} PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
        endforeach()
        add_test(NAME felitronics_session_control_${ctl}
                 COMMAND ${CMAKE_COMMAND}
                         -DBUILD_DIR=${CMAKE_BINARY_DIR}
                         -DTARGET=felitronics_session_control_${ctl}_planted
                         -DCONFIG=$<CONFIG>
                         "-DEXPECT=${FELITRONICS_SESSION_CONTROL_EXPECT_${ctl}}"
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/build-must-fail.cmake)
        # One build of this tree at a time: two build tools running in one build directory corrupt its state.
        set_tests_properties(felitronics_session_control_${ctl} PROPERTIES RESOURCE_LOCK felitronics_build_tree)
    endforeach()
endif()
