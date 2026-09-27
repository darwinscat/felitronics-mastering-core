# felitronics::session — the mastering session, the object a shell talks to (docs/SESSION.md). The repository's first
# COMPILED target: every other module here is header-only and compiles under whatever flags its consumer chooses; this
# one is a static library, so its sources are compiled once, with the flags below. That holds for what the library's
# own objects contain — not for header-inline code the application's translation units compile too (from the standard
# library, from core): the linker may keep the application's copy. So a program that links felitronics::session compiles
# EVERY translation unit with the session's FP flags — no contraction, no fast-math (docs/SESSION.md, "What the flags do
# not reach"). The wasm modules are built whole by this repository and are not affected.

# THE SOURCES — sources.txt, the one list (CMake compiles it, tools/wasm/build.sh compiles it into the fcsession module,
# the session-laws lint scans from it).
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/sources.txt FELITRONICS_SESSION_SOURCES REGEX "^[^#]")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_SOURCE_DIR}/sources.txt ${CMAKE_CURRENT_SOURCE_DIR}/build-flags.txt)
add_library(felitronics_session STATIC ${FELITRONICS_SESSION_SOURCES})
add_library(felitronics::session ALIAS felitronics_session)
target_include_directories(felitronics_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(felitronics_session PUBLIC cxx_std_20)
# The analyzers the config feeds admit its blocks themselves (their storageFor, called by the schema); header-only.
target_link_libraries(felitronics_session PRIVATE felitronics::analysis_offline)

# THE CONFIG — config/targets.toml and config/engine.toml, compiled into the library as constexpr data by felitronics-toml's
# felitronics_toml_embed: nothing reads a file at run time. A document that does not parse stops the build at its line and
# column here; one that parses but breaks the schema stops it at the gate below. The generated headers and felitronics::toml
# are PRIVATE: Config.h, the public face, speaks typed structs only.
set(FELITRONICS_SESSION_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config)
felitronics_toml_embed(felitronics_session INPUT config/targets.toml
                       NAMESPACE felitronics::session::config::embedded NAME targets HEADER embedded/targets.h)
felitronics_toml_embed(felitronics_session INPUT config/engine.toml
                       NAMESPACE felitronics::session::config::embedded NAME engine HEADER embedded/engine.h)

# THE GATE — the config's schema over the two documents, run by EVERY build of the library before it is built, a
# consumer's and a build without tests included: a typo in a document is a red build at `<file>:<line>:<column>`, never a
# setting silently ignored. felitronics_session_config_check is a HOST tool compiled from tests/ConfigCheck.cpp and the
# library's own src/ConfigSchema.cpp — one schema, one text — so it does not link the library it gates. Where the build
# cross-compiles it runs under the build's emulator (node, on the wasm tier), as felitronics_toml2cpp does; a cross build
# without one names a host build of it in FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE. The step writes its stamp only
# when the check passed, so a failed gate is run again by the next build, whichever generator runs it.
add_executable(felitronics_session_config_check tests/ConfigCheck.cpp src/ConfigSchema.cpp)
target_include_directories(felitronics_session_config_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                                                    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(felitronics_session_config_check PRIVATE felitronics::toml felitronics::analysis_offline)
target_compile_features(felitronics_session_config_check PRIVATE cxx_std_20)
if(EMSCRIPTEN)
    # Run through node at build time, on the host's files, with its exit status.
    target_link_options(felitronics_session_config_check PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
endif()
if(FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE)
    set(FELITRONICS_SESSION_CONFIG_GATE "${FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE}")
    set(FELITRONICS_SESSION_CONFIG_GATE_DEPENDS "")
elseif(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(FATAL_ERROR "felitronics::session: this cross build cannot run its config gate — build "
                        "felitronics_session_config_check for the host and set FELITRONICS_SESSION_CONFIG_CHECK_EXECUTABLE")
else()
    set(FELITRONICS_SESSION_CONFIG_GATE felitronics_session_config_check)
    set(FELITRONICS_SESSION_CONFIG_GATE_DEPENDS felitronics_session_config_check)
endif()
set(FELITRONICS_SESSION_CONFIG_STAMP ${CMAKE_CURRENT_BINARY_DIR}/felitronics_session_config.checked)
add_custom_command(OUTPUT ${FELITRONICS_SESSION_CONFIG_STAMP}
    COMMAND ${FELITRONICS_SESSION_CONFIG_GATE} ${FELITRONICS_SESSION_CONFIG}/targets.toml ${FELITRONICS_SESSION_CONFIG}/engine.toml
    COMMAND ${CMAKE_COMMAND} -E touch ${FELITRONICS_SESSION_CONFIG_STAMP}
    DEPENDS ${FELITRONICS_SESSION_CONFIG_GATE_DEPENDS}
            ${FELITRONICS_SESSION_CONFIG}/targets.toml ${FELITRONICS_SESSION_CONFIG}/engine.toml
    COMMENT "felitronics::session: the config, read by schema"
    VERBATIM)
target_sources(felitronics_session PRIVATE ${FELITRONICS_SESSION_CONFIG_STAMP})

# THE BRAIN'S FLAGS — PRIVATE, so they are the library's and never its consumer's, and one SHELL: group, so CMake's
# option de-duplication cannot take a member of it away. That is not a theoretical guard: a parent that states
# `add_compile_options(-ffp-contract=off)` before this repository (an innocent thing to do) made CMake drop the
# library's own later `-ffp-contract=off` as a duplicate, which left core's directory-level `-ffp-contract=on` last on
# the line — contraction on. tests/consumer/ is that parent, and a test holds the compile line it produces.
#   gcc, clang, emscripten: build-flags.txt, read here and by tools/wasm/build.sh — one definition, two readers.
#   MSVC 2022+: /fp:precise, /EHs-c- /EHa- (no C++ and no asynchronous exceptions), /GR-, and /we4530 /we4541, which
#     make a `try`, a typeid and a dynamic_cast errors. A bare `throw` compiles under /EHs-c-: the session-laws lint
#     refuses the token in every source and every #if branch, so no row has to be the one that catches it. The consumer's
#     default /EHsc and /GR come first and are overridden, which MSVC reports as D9025 — expected.
# Any other compiler, or a compiler in MSVC-compatible mode (clang-cl, icx-cl), or Intel's icx (fast floating point by
# default): refused here, loudly. Their spelling of these guarantees is unverified in this repository, and a library
# built with the wrong spelling would compile, link and decide differently.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(MSVC_VERSION LESS 1930)
        message(FATAL_ERROR "felitronics::session needs MSVC 2022 (19.30) or later (this is ${MSVC_VERSION})")
    endif()
    set(FELITRONICS_SESSION_COMPILE_OPTIONS "SHELL:/fp:precise /EHs-c- /EHa- /GR- /we4530 /we4541")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
       AND NOT "${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "MSVC")
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/build-flags.txt FELITRONICS_SESSION_GNU_FLAGS REGEX "^-")
    list(LENGTH FELITRONICS_SESSION_GNU_FLAGS FELITRONICS_SESSION_GNU_FLAGS_LINES)
    if(NOT FELITRONICS_SESSION_GNU_FLAGS_LINES EQUAL 1)
        message(FATAL_ERROR "modules/session/build-flags.txt must hold exactly one line of flags")
    endif()
    set(FELITRONICS_SESSION_COMPILE_OPTIONS "SHELL:${FELITRONICS_SESSION_GNU_FLAGS}")
else()
    message(FATAL_ERROR
        "felitronics::session is not built with ${CMAKE_CXX_COMPILER_ID} (frontend "
        "'${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}'): its FP-contraction, fast-math, exception and RTTI switches are verified "
        "for MSVC 2022+, and for gcc, clang, Apple clang and emscripten in their GNU spelling. clang-cl, icx-cl and icx "
        "(fast floating point by default) are not — a library built with the wrong spelling would compile and decide "
        "differently. Use a supported compiler for this repository.")
endif()
target_compile_options(felitronics_session PRIVATE ${FELITRONICS_SESSION_COMPILE_OPTIONS})

# The hygiene warning set of this tree, -Werror included — in THIS repository's own builds only. A consumer builds the
# library too, with whatever compiler it has, and a warning a newer compiler invents must not break its build.
if(FELITRONICS_BUILD_TESTS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(felitronics_session PRIVATE ${FELITRONICS_HYGIENE_WARNINGS})
endif()

# THE TWO RELEASES THE LIBRARY REPORTS (Session::version, Session::coreVersion): this repository's project() line, and
# felitronics-core's, which the top-level CMakeLists.txt reads from the directory that defined it. No fallback.
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
    include(${CMAKE_CURRENT_SOURCE_DIR}/tests/SessionTestTools.cmake)

    # The FMA-capable rows, where a contraction that was allowed happens and a missing -ffp-contract=off is observable:
    # every arm64 row. (x86-64 has FMA only with -mfma / /arch:AVX2, which this tree does not set.) The suites below
    # REQUIRE their positive controls to fuse on these rows, so a control that went quietly dead is red.
    set(FELITRONICS_SESSION_CONTRACT_FAST $<IF:$<CXX_COMPILER_ID:MSVC>,/fp:contract,-ffp-contract=fast>)
    # The positive controls below — a licence that DOES change the answer — are optimiser transformations: an
    # unoptimised build fuses nothing and reassociates nothing, and a build with no CMAKE_BUILD_TYPE is unoptimised. So the
    # targets that carry a positive control are compiled -O2 whatever the build type says (last on their line, it wins),
    # and a configuration-less build is green for the right reason. MSVC gets /O2 in every configuration but Debug, whose
    # /RTC1 /O2 is refused beside; in an MSVC Debug build the tests say the positive controls are not reachable.
    set(FELITRONICS_SESSION_OPTIMISED $<IF:$<CXX_COMPILER_ID:MSVC>,$<$<NOT:$<CONFIG:Debug>>:/O2>,-O2>)

    # The session and its laws that a running program can check: the demand of create() against what create() asks the
    # heap for (law 11d, through the one allocation counter), the harness proven able to fail, the two versions, the
    # floating-point environment refused at create, and the library's build probes answered from inside it while THIS
    # translation unit is compiled with contraction forced on.
    add_executable(felitronics_session_tests tests/SessionTests.cpp)
    target_link_libraries(felitronics_session_tests PRIVATE felitronics::session felitronics::test_support)
    target_include_directories(felitronics_session_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(felitronics_session_tests PRIVATE cxx_std_20)
    target_compile_definitions(felitronics_session_tests PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
    target_compile_options(felitronics_session_tests PRIVATE ${FELITRONICS_SESSION_CONTRACT_FAST} ${FELITRONICS_SESSION_OPTIMISED})
    add_test(NAME felitronics_session_tests COMMAND felitronics_session_tests)

    # HOSTILE FLAGS AHEAD OF THE LIBRARY'S. The library's sources compiled again, into test binaries, with a licence the
    # session refuses PREPENDED to the target's own options — the position a consumer's CMAKE_CXX_FLAGS or directory
    # options take. The library's options come after and must win: every build probe answers IEEE. Each binary also
    # compiles the probes with the licence alone and requires them to change on its row — so the licence is live, and
    # "the library's flags won" is a statement about something that would otherwise have happened.
    #   contract   -ffp-contract=fast (/fp:contract) — live on gcc 13 too, whose `on` is `off`
    #   fastmath   -freciprocal-math -fassociative-math -fno-signed-zeros -fno-trapping-math (/fp:fast)
    set(FELITRONICS_SESSION_HOSTILE_contract ${FELITRONICS_SESSION_CONTRACT_FAST})
    set(FELITRONICS_SESSION_HOSTILE_fastmath
        $<IF:$<CXX_COMPILER_ID:MSVC>,/fp:fast,-freciprocal-math;-fassociative-math;-fno-signed-zeros;-fno-trapping-math>)
    # MSVC HAS NO WAY BACK FROM /fp:contract: a later /fp:precise selects the model and leaves contraction on, and there
    # is no /fp:contract- to state. So on MSVC a consumer's /fp:contract ahead of the library's options is not
    # overridden — it is REFUSED: src/BuildGuards.h sees _M_FP_CONTRACT, and that build must fail on the guard's message.
    foreach(licence contract fastmath)
        if(MSVC AND licence STREQUAL "contract")
            add_library(fs_hostile_contract OBJECT EXCLUDE_FROM_ALL ${FELITRONICS_SESSION_SOURCES})
            target_include_directories(fs_hostile_contract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
            target_compile_features(fs_hostile_contract PRIVATE cxx_std_20)
            target_compile_definitions(fs_hostile_contract PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
            target_compile_options(fs_hostile_contract PRIVATE /fp:contract $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
            felitronics_session_must_fail(felitronics_session_hostile_contract_refused fs_hostile_contract
                                          "must be compiled without /fp:contract")
            continue()
        endif()
        add_library(felitronics_session_hostile_${licence} OBJECT ${FELITRONICS_SESSION_SOURCES})
        target_include_directories(felitronics_session_hostile_${licence} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_features(felitronics_session_hostile_${licence} PRIVATE cxx_std_20)
        target_compile_definitions(felitronics_session_hostile_${licence} PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
        target_compile_options(felitronics_session_hostile_${licence} PRIVATE
            "${FELITRONICS_SESSION_HOSTILE_${licence}}" $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>
            ${FELITRONICS_SESSION_OPTIMISED})
        add_executable(felitronics_session_hostile_${licence}_tests tests/HostileFlagsTests.cpp
                       $<TARGET_OBJECTS:felitronics_session_hostile_${licence}>)
        target_link_libraries(felitronics_session_hostile_${licence}_tests PRIVATE felitronics::test_support)
        target_include_directories(felitronics_session_hostile_${licence}_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_features(felitronics_session_hostile_${licence}_tests PRIVATE cxx_std_20)
        target_compile_definitions(felitronics_session_hostile_${licence}_tests PRIVATE
            FELITRONICS_SESSION_HOSTILE_LICENCE_${licence}=1)
        target_compile_options(felitronics_session_hostile_${licence}_tests PRIVATE "${FELITRONICS_SESSION_HOSTILE_${licence}}"
                               ${FELITRONICS_SESSION_OPTIMISED})
        add_test(NAME felitronics_session_hostile_${licence}_tests COMMAND felitronics_session_hostile_${licence}_tests)
    endforeach()

    # The config: bound with no problem and equal to its sources, the schema's controls in-process (every kind of problem,
    # at its line and column), and the versions — normalised, moved by every single value, `sound` by what can change a
    # master only.
    add_executable(felitronics_session_config_tests tests/ConfigTests.cpp)
    target_link_libraries(felitronics_session_config_tests PRIVATE
        felitronics::session felitronics::toml felitronics::test_support)
    target_compile_features(felitronics_session_config_tests PRIVATE cxx_std_20)
    if(EMSCRIPTEN)
        target_link_options(felitronics_session_config_tests PRIVATE -sNODERAWFS=1)
    endif()
    # The suite hashes the embedded data itself, changed node by node: it embeds its own copy of the documents (by the same
    # tool, from the same files) and reads src/ConfigVersion.h, the walk config::versions() is.
    felitronics_toml_embed(felitronics_session_config_tests INPUT config/targets.toml
                           NAMESPACE felitronics::session::config::testcopy NAME targets HEADER testcopy/targets.h)
    felitronics_toml_embed(felitronics_session_config_tests INPUT config/engine.toml
                           NAMESPACE felitronics::session::config::testcopy NAME engine HEADER testcopy/engine.h)
    target_include_directories(felitronics_session_config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME felitronics_session_config_tests
             COMMAND felitronics_session_config_tests ${FELITRONICS_SESSION_CONFIG}/targets.toml
                                                      ${FELITRONICS_SESSION_CONFIG}/engine.toml)

    # THE OWNER'S DECISIONS, apart from the schema: every target row field by field and the engine's decided numbers, and
    # the version of the sound pinned to the name of the defaults — so changing a decision, or a sound number without
    # naming new defaults, is a deliberate edit of this suite.
    add_executable(felitronics_session_config_decisions_tests tests/ConfigDecisionsTests.cpp)
    target_link_libraries(felitronics_session_config_decisions_tests PRIVATE
        felitronics::session felitronics::toml felitronics::test_support)
    target_compile_features(felitronics_session_config_decisions_tests PRIVATE cxx_std_20)
    if(EMSCRIPTEN)
        target_link_options(felitronics_session_config_decisions_tests PRIVATE -sNODERAWFS=1)
    endif()
    add_test(NAME felitronics_session_config_decisions_tests
             COMMAND felitronics_session_config_decisions_tests ${FELITRONICS_SESSION_CONFIG}/targets.toml
                                                                ${FELITRONICS_SESSION_CONFIG}/engine.toml)

    # THE CHECK ITSELF, PROVEN ABLE TO FAIL: a copy of the documents with one mistake planted must turn the check red
    # with that mistake's line, column, fault and key path, and the same copy without the plant must pass
    # (tests/config-must-fail.cmake). Not on the wasm tier: the in-process controls above run there.
    #   name | document | planted: from | to | the problem points at | the line the check must print
    if(NOT EMSCRIPTEN)
        set(FELITRONICS_SESSION_CONFIG_CONTROLS
            "unknown_key|engine|releaseMs = 50|releseMs = 50|releseMs|UnknownKey limiter.releseMs"
            "out_of_range|engine|hzMax = 50|hzMax = 500|500|OutOfRange hpf.hzMax"
            "wrong_type|targets|default = \"allStreaming\"|default = 14|14|WrongType default"
            "crest_corners|engine|bandEdgesHz = [120, 2000, 6000]|bandEdgesHz = []|[]|Refused crest.bandEdgesHz"
            "geometric_offset|engine|to = -9, law = \"linear\"|to = -9, law = \"geometric\"|\"geometric\"|Refused glue.threshOffset.law"
            "shaping_above_dither|engine|shapingUpToBits = 16|shapingUpToBits = 24|24|Refused dither.shapingUpToBits")
        foreach(ctl IN LISTS FELITRONICS_SESSION_CONFIG_CONTROLS)
            string(REPLACE "|" ";" f "${ctl}")
            list(GET f 0 name)
            list(GET f 1 doc)
            list(GET f 2 from)
            list(GET f 3 to)
            list(GET f 4 at)
            list(GET f 5 expect)
            add_test(NAME felitronics_session_config_control_${name}
                     COMMAND ${CMAKE_COMMAND}
                             -DCHECK=$<TARGET_FILE:felitronics_session_config_check>
                             -DCONFIG=${FELITRONICS_SESSION_CONFIG}
                             -DWORK=${CMAKE_CURRENT_BINARY_DIR}/config-controls/${name}
                             -DDOC=${doc} "-DFROM=${from}" "-DTO=${to}" "-DAT=${at}" "-DEXPECT=${expect}"
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/config-must-fail.cmake)
        endforeach()
    endif()

    # THE BUILD CONTROLS — each forbidden construct, and each forbidden licence, proven to fail the build.
    #  1. Constructs (tests/controls/): one source compiled with the library's OWN compile options (read off the target),
    #     clean as part of every build and with the construct planted (FELITRONICS_SESSION_PLANT) in a build that must
    #     fail with that construct's diagnostic. The diagnostics are matched as phrases, per compiler (clang and em++ /
    #     gcc / MSVC); a keyword's quotes as "not a space", because gcc prints ‘typeid’ in a UTF-8 locale and 'typeid'
    #     in the C locale.
    #     THE WASM TIER: its preset compiles EVERYTHING with -fno-exceptions -fno-rtti, so these planted builds fail there
    #     whatever the library's options say — on that tier they prove the preset, and the native rows prove the library.
    #  2. Licences: src/BuildContract.cpp compiled with the library's options and then a flag APPENDED after them — the
    #     position of a per-source override — which must fail on src/BuildGuards.h's own #error for it.
    set(FELITRONICS_SESSION_CONTROL_EXPECT_try
        "cannot use [^ ]+try[^ ]+ with exceptions disabled|exception handling disabled, use|error C4530")
    set(FELITRONICS_SESSION_CONTROL_EXPECT_throw
        "cannot use [^ ]+throw[^ ]+ with exceptions disabled|exception handling disabled, use")
    set(FELITRONICS_SESSION_CONTROL_EXPECT_typeid
        "use of typeid requires -frtti|cannot use [^ ]+typeid[^ ]+ with|error C4541: [^ ]+typeid")
    set(FELITRONICS_SESSION_CONTROL_EXPECT_dynamic_cast
        "use of dynamic_cast requires -frtti|[^ ]+dynamic_cast[^ ]+ not permitted with|error C4541: [^ ]+dynamic_cast")
    set(FELITRONICS_SESSION_CONTROLS try typeid dynamic_cast)
    if(NOT MSVC)
        list(APPEND FELITRONICS_SESSION_CONTROLS throw)   # MSVC compiles a bare throw under /EHs-c- (the lint holds it)
    endif()
    foreach(ctl IN LISTS FELITRONICS_SESSION_CONTROLS)
        foreach(variant clean planted)
            set(tgt fs_ctl_${ctl}_${variant})
            if(variant STREQUAL "clean")
                add_library(${tgt} OBJECT tests/controls/${ctl}.cpp)
            else()
                add_library(${tgt} OBJECT EXCLUDE_FROM_ALL tests/controls/${ctl}.cpp)
                target_compile_definitions(${tgt} PRIVATE FELITRONICS_SESSION_PLANT=1)
            endif()
            target_compile_features(${tgt} PRIVATE cxx_std_20)
            target_compile_options(${tgt} PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
        endforeach()
        felitronics_session_must_fail(felitronics_session_control_${ctl} fs_ctl_${ctl}_planted
                                      "${FELITRONICS_SESSION_CONTROL_EXPECT_${ctl}}")
    endforeach()

    if(MSVC)
        set(FELITRONICS_SESSION_APPEND_fastmath   /fp:fast)
        set(FELITRONICS_SESSION_APPEND_exceptions /EHsc)
        set(FELITRONICS_SESSION_APPEND_rtti       /GR)
        set(FELITRONICS_SESSION_APPEND_EXPECT_fastmath "must be compiled with /fp:precise on MSVC")
    else()
        set(FELITRONICS_SESSION_APPEND_fastmath   -ffast-math)
        set(FELITRONICS_SESSION_APPEND_exceptions -fexceptions)
        set(FELITRONICS_SESSION_APPEND_rtti       -frtti)
        set(FELITRONICS_SESSION_APPEND_EXPECT_fastmath "must be compiled without fast-math")
    endif()
    set(FELITRONICS_SESSION_APPEND_EXPECT_exceptions "must be compiled without exceptions")
    set(FELITRONICS_SESSION_APPEND_EXPECT_rtti       "must be compiled without RTTI")
    foreach(licence fastmath exceptions rtti)
        set(tgt fs_append_${licence})
        add_library(${tgt} OBJECT EXCLUDE_FROM_ALL src/BuildContract.cpp)
        target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_features(${tgt} PRIVATE cxx_std_20)
        target_compile_definitions(${tgt} PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
        target_compile_options(${tgt} PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>
                                              ${FELITRONICS_SESSION_APPEND_${licence}})
        if(licence STREQUAL "exceptions" AND CMAKE_GENERATOR MATCHES "Visual Studio")
            # MEASURED (MSVC 19.44, the Visual Studio generator): /EHsc becomes the project's ExceptionHandling property
            # and the library's /EHs-c- /EHa- stay in AdditionalOptions, which MSBuild places AFTER every property on the
            # command line — so an appended /EHsc cannot turn exceptions back on, and the build stays guarded-clean. That
            # is the property asserted here; with a generator that keeps the order (Ninja + cl), the appended flag wins
            # and the guard must fire, as on every other row.
            felitronics_session_must_build(felitronics_session_control_append_${licence} ${tgt})
        else()
            felitronics_session_must_fail(felitronics_session_control_append_${licence} ${tgt}
                                          "${FELITRONICS_SESSION_APPEND_EXPECT_${licence}}")
        endif()
    endforeach()

    # THE OBJECT-FILE GATES' CONTROLS. The gate itself — over the library's objects and the C boundary's — is in
    # tools/CMakeLists.txt, next to the boundary. Here: sources that each break the law in one shape, compiled with the
    # library's own options into objects nobody links, and each run through the gate, which must refuse it and name the
    # symbol (tests/object-controls/ has one file per shape, and says which review found it).
    # name | what the gate's refusal must name — regexes, `&&&` between several that must all appear, each matching a
    # refused symbol's WHOLE name (its raw or demangled name, or the last identifier of it: `lambdaCounter` of a static
    # local, `to_string` of `std::__1::to_string(double)`) — or PASS, for the control the gate must ACCEPT: a constexpr
    # table of pointers, relocated constant data, built position-independent so it lands in .data.rel.ro (ELF, wasm) and
    # __DATA,__const (Mach-O), where pointer_array — the same table with mutable pointers — must be refused. The
    # alternatives in a call's pattern are that call's spelling per C library: glibc's __ctype_b_loc, Apple's __maskrune,
    # the UCRT's __stdio_common_vfprintf behind an inline printf, libstdc++'s vsnprintf behind an inline to_string.
    set(FELITRONICS_SESSION_OBJECT_CONTROLS
        "constexpr_pointer_table|PASS"
        "direct_init_global|directInitCounter"
        "lambda_static|lambdaCounter"
        "macro_global|g_macroCounter"
        "class_static|journalEntries"
        "function_local_static|rememberedPeak"
        "pointer_array|kPhaseNames"
        "thread_local|perThreadCount"
        "const_dynamic_init|kStartupValue"
        "forbidden_calls|printf|iprintf|__printf_chk|__stdio_common_vfprintf&&&fopen|fopen64&&&time|time64|_time64&&&getenv&&&strtod&&&rand&&&isalpha|__maskrune|_DefaultRuneLocale|__ctype_b_loc|__istype|_isctype|_isctype_l|_chvalidator|__pctype_func"
        "string_via_pointer|to_string|vsnprintf|__vsnprintf_chk|__stdio_common_vsprintf|__stdio_common_vsprintf_s|_scprintf|sprintf_s"
        "stod_rand|stod|strtod&&&rand"
        "local_definition|getpid")
    # A global in a section of its own naming; on ELF a second one in a section NAMED .rodata.*, which the object still
    # marks writable. (wasm segments carry no write flag at all — the linear memory is writable as a whole — so there the
    # read-only places are known by name, and the session-laws lint refuses the section attribute that could choose one.)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
        list(APPEND FELITRONICS_SESSION_OBJECT_CONTROLS "custom_section|sectionCounter&&&rodataNamedCounter")
    else()
        list(APPEND FELITRONICS_SESSION_OBJECT_CONTROLS "custom_section|sectionCounter")
    endif()
    set(FELITRONICS_SESSION_OBJECT_CONTROL_SOURCES_local_definition
        tests/object-controls/local_definition_call.cpp tests/object-controls/local_definition_static.cpp)
    if(NOT MSVC)
        list(APPEND FELITRONICS_SESSION_OBJECT_CONTROLS "asm_label|asmCounter")   # MSVC has no asm labels
    endif()
    foreach(entry IN LISTS FELITRONICS_SESSION_OBJECT_CONTROLS)
        string(FIND "${entry}" "|" bar)
        string(SUBSTRING "${entry}" 0 ${bar} ctl)
        math(EXPR bar "${bar} + 1")
        string(SUBSTRING "${entry}" ${bar} -1 expect)
        set(tgt fs_objctl_${ctl})   # short: MSBuild writes a target's name twice into one log path, under 260 characters
        if(DEFINED FELITRONICS_SESSION_OBJECT_CONTROL_SOURCES_${ctl})
            add_library(${tgt} OBJECT ${FELITRONICS_SESSION_OBJECT_CONTROL_SOURCES_${ctl}})
        else()
            add_library(${tgt} OBJECT tests/object-controls/${ctl}.cpp)
        endif()
        target_compile_features(${tgt} PRIVATE cxx_std_20)
        target_compile_options(${tgt} PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
        if(expect STREQUAL "PASS")
            set_target_properties(${tgt} PROPERTIES POSITION_INDEPENDENT_CODE ON)
            felitronics_session_object_gate(NAME felitronics_session_object_control_${ctl} LIBRARY ${tgt})
        else()
            felitronics_session_object_gate(NAME felitronics_session_object_control_${ctl}
                                            LIBRARY ${tgt} EXPECT "${expect}")
        endif()
    endforeach()

    # THE COMPILE LINE, READ BACK (tests/compile-line-gate.cmake). What no preprocessor macro announces — contraction, and
    # licences like -fno-honor-nans or -ffp-model=fast, on clang and gcc — is read from the compile commands this build and
    # a consumer's build actually produce: for every unit of the library, and the C boundary's, the library's flag group
    # (build-flags.txt) is the last word on floating point, exceptions and RTTI, and nothing forces an include or passes
    # flags through. A per-source override, or the group de-duplicated away, is red. Needs compile_commands.json, which
    # the Makefile and Ninja generators write (CMAKE_EXPORT_COMPILE_COMMANDS is on at the top level).
    # NOT ON MSVC, whatever the generator: cl.exe announces /fp:precise, /fp:contract, exceptions and RTTI to the
    # preprocessor, and src/BuildGuards.h refuses a wrong unit from inside it (the append controls above prove that).
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles" AND NOT MSVC)
        set(FELITRONICS_SESSION_LINE_TARGETS felitronics_session.dir)
        set(FELITRONICS_SESSION_LINE_SOURCES "")
        foreach(src IN LISTS FELITRONICS_SESSION_SOURCES)
            list(APPEND FELITRONICS_SESSION_LINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${src})
        endforeach()
        set(FELITRONICS_SESSION_LIBRARY_LINE_SOURCES ${FELITRONICS_SESSION_LINE_SOURCES})
        if(PROJECT_IS_TOP_LEVEL)   # tools/ — and the C boundary's object library with it — is a top-level build's
            list(APPEND FELITRONICS_SESSION_LINE_TARGETS felitronics_session_facade.dir)
            list(APPEND FELITRONICS_SESSION_LINE_SOURCES ${PROJECT_SOURCE_DIR}/tools/wasm/fc_session.cpp)
        endif()
        string(REPLACE ";" "|" FELITRONICS_SESSION_LINE_TARGETS "${FELITRONICS_SESSION_LINE_TARGETS}")
        string(REPLACE ";" "|" FELITRONICS_SESSION_LINE_SOURCES "${FELITRONICS_SESSION_LINE_SOURCES}")
        string(REPLACE ";" "|" FELITRONICS_SESSION_LIBRARY_LINE_SOURCES "${FELITRONICS_SESSION_LIBRARY_LINE_SOURCES}")
        add_test(NAME felitronics_session_compile_line
                 COMMAND ${CMAKE_COMMAND} -DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
                         -DFLAGS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/build-flags.txt
                         "-DTARGETS=${FELITRONICS_SESSION_LINE_TARGETS}" "-DSOURCES=${FELITRONICS_SESSION_LINE_SOURCES}"
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile-line-gate.cmake)
        # ITS CONTROLS (tests/compile-line-controls.cmake). Two are real: copies of a library unit, compiled with the
        # library's options and a per-source `-ffp-model=fast`, or a joined `-include<path>` — the property on a COPY,
        # because a source property is the directory's and would reach the library's own unit — configured into this
        # build's compile commands and never built. The others edit this build's own compile_commands.json.
        set(FELITRONICS_SESSION_PERSOURCE ${CMAKE_CURRENT_BINARY_DIR}/compile-line-control/PerSourceOverride.cpp)
        configure_file(src/BuildContract.cpp ${FELITRONICS_SESSION_PERSOURCE} COPYONLY)
        add_library(fs_cl_persource OBJECT EXCLUDE_FROM_ALL ${FELITRONICS_SESSION_PERSOURCE})
        target_include_directories(fs_cl_persource PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_features(fs_cl_persource PRIVATE cxx_std_20)
        target_compile_definitions(fs_cl_persource PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
        target_compile_options(fs_cl_persource PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
        set_source_files_properties(${FELITRONICS_SESSION_PERSOURCE} PROPERTIES COMPILE_OPTIONS -ffp-model=fast)
        # ...and a second copy with a JOINED forced include as its source property, `-include<absolute path>` — the
        # spelling a separated-only match missed. The header says what such a header could do; nothing compiles it.
        set(FELITRONICS_SESSION_PERINCLUDE ${CMAKE_CURRENT_BINARY_DIR}/compile-line-control/PerSourceInclude.cpp)
        set(FELITRONICS_SESSION_PERINCLUDE_HEADER ${CMAKE_CURRENT_BINARY_DIR}/compile-line-control/forced.h)
        configure_file(src/BuildContract.cpp ${FELITRONICS_SESSION_PERINCLUDE} COPYONLY)
        file(CONFIGURE OUTPUT ${FELITRONICS_SESSION_PERINCLUDE_HEADER}
             CONTENT "// a forced include can switch contraction on for the whole unit, past every flag\n#pragma clang fp contract(fast)\n")
        add_library(fs_cl_perinclude OBJECT EXCLUDE_FROM_ALL ${FELITRONICS_SESSION_PERINCLUDE})
        target_include_directories(fs_cl_perinclude PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_features(fs_cl_perinclude PRIVATE cxx_std_20)
        target_compile_definitions(fs_cl_perinclude PRIVATE ${FELITRONICS_SESSION_VERSION_DEFINITIONS})
        target_compile_options(fs_cl_perinclude PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
        set_source_files_properties(${FELITRONICS_SESSION_PERINCLUDE} PROPERTIES
                                    COMPILE_OPTIONS "-include${FELITRONICS_SESSION_PERINCLUDE_HEADER}")
        add_test(NAME felitronics_session_compile_line_controls
                 COMMAND ${CMAKE_COMMAND} -DGATE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compile-line-gate.cmake
                         -DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
                         -DFLAGS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/build-flags.txt
                         "-DTARGETS=${FELITRONICS_SESSION_LINE_TARGETS}" "-DSOURCES=${FELITRONICS_SESSION_LINE_SOURCES}"
                         -DPERSOURCE=${FELITRONICS_SESSION_PERSOURCE} -DPERSOURCE_TARGET=fs_cl_persource.dir
                         -DPERINCLUDE=${FELITRONICS_SESSION_PERINCLUDE} -DPERINCLUDE_TARGET=fs_cl_perinclude.dir
                         -DPERINCLUDE_HEADER=${FELITRONICS_SESSION_PERINCLUDE_HEADER}
                         -DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/compile-line-controls
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile-line-controls.cmake)
        # ...and in a CONSUMER's build: tests/consumer/ is a parent project that states `add_compile_options(-ffp-contract=off)`
        # and then makes felitronics-core and this repository available. Configured, not built.
        if(NOT EMSCRIPTEN)
            add_test(NAME felitronics_session_consumer_compile_line
                     COMMAND ${CMAKE_COMMAND}
                             -DGENERATOR=${CMAKE_GENERATOR}
                             -DCXX=${CMAKE_CXX_COMPILER}
                             -DCONSUMER=${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer
                             -DBUILD=${CMAKE_CURRENT_BINARY_DIR}/consumer
                             -DCORE=${FELITRONICS_MASTERING_CORE_SOURCE_DIR}
                             -DMASTERING=${PROJECT_SOURCE_DIR}
                             -DGATE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compile-line-gate.cmake
                             -DFLAGS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/build-flags.txt
                             "-DSOURCES=${FELITRONICS_SESSION_LIBRARY_LINE_SOURCES}"
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer-compile-line.cmake)
        endif()
    endif()
endif()
