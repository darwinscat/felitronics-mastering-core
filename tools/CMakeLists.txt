# felitronics-mastering-core tools (built only when this is the top-level project): the C ABIs (fc_master, fc_probe,
# fc_tempo — the probe's tempo surface alone — and fc_session, over the mastering session), their native CLIs and suites. Used to validate the DSP against reference implementations (ffmpeg) on real audio, and as the
# native side of every native-vs-wasm comparison.
# The FP-contraction contract for everything that goes through fcore_probe.h. Baseline wasm has NO scalar FMA
# instruction, so emscripten cannot contract a*b+c; a contracting native build therefore disagrees with wasm —
# measured at 6.75e-7 dB on the true-peak answer, which is 675x the tolerance the wasm spike is held to. GCC is
# the sharper edge here: it defaults to -ffp-contract=fast in gnu++ modes, so a Linux/GCC reference contracts
# even where clang would not. felitronics_probe_tests asserts the flag actually took effect, so dropping this
# line turns a silent parity failure into a red test.
# (Not applied on MSVC: a Windows build is not part of the parity contract, and /fp: has no equivalent knob.)
set(FCORE_NO_FP_CONTRACT $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>)

add_executable(fcore_measure fcore_measure.cpp)
target_link_libraries(fcore_measure PRIVATE felitronics::analysis_offline felitronics::tempo felitronics::oversampling)
# The OFFLINE analyzer modes compose over eq::Crossover2 / stereo::MidSide; felitronics::analysis_offline
# carries both (modules/analysis_offline/CMakeLists.txt).
target_compile_features(fcore_measure PRIVATE cxx_std_20)
target_compile_options(fcore_measure PRIVATE ${FCORE_NO_FP_CONTRACT})

if(FELITRONICS_BUILD_TESTS)
    # fcore::Probe is a body shared verbatim with the wasm shim, so it is one of the two pieces of tools/ that
    # carry a correctness claim rather than convenience — and it gets a real suite. (The other is
    # fcore::ClipProbe, whose suite is felitronics_clips_exposure_tests, further down.)
    add_executable(felitronics_probe_tests tests/ProbeTests.cpp)
    target_link_libraries(felitronics_probe_tests PRIVATE felitronics::analysis_offline felitronics::oversampling felitronics::test_support)
    target_include_directories(felitronics_probe_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_probe_tests PRIVATE cxx_std_20)
    target_compile_options(felitronics_probe_tests PRIVATE ${FCORE_NO_FP_CONTRACT})
    add_test(NAME felitronics_probe_tests COMMAND felitronics_probe_tests)

    # The wasm C ABI is the one surface fed by untrusted input (whatever a page's JavaScript computed), so its
    # guards are tested — natively, because fc_probe.cpp compiles anywhere (EMSCRIPTEN_KEEPALIVE degrades to a
    # plain extern "C"). That puts the validation and the planar addressing under ctest, ASan and UBSan.
    add_executable(felitronics_abi_tests tests/AbiTests.cpp wasm/fc_probe.cpp)
    target_link_libraries(felitronics_abi_tests PRIVATE felitronics::analysis_offline felitronics::tempo felitronics::oversampling felitronics::test_support)
    target_include_directories(felitronics_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_abi_tests PRIVATE cxx_std_20)
    target_compile_options(felitronics_abi_tests PRIVATE ${FCORE_NO_FP_CONTRACT})
    add_test(NAME felitronics_abi_tests COMMAND felitronics_abi_tests)

    # The EXPOSURE of analysis::ClipDetector: fcore::ClipProbe, the `clips` text format and the clips
    # entry points of the wasm ABI. Same arrangement as felitronics_abi_tests one block up — fc_probe.cpp is
    # compiled natively, so the ABI's validation and packing run under ctest, ASan and UBSan.
    #
    # FCORE_NO_FP_CONTRACT IS LOAD-BEARING ON THIS TARGET, not inherited decoration. The repository declares
    # -ffp-contract=on (top-level CMakeLists, law 10), and the one expression of this measurement a fused
    # multiply-add can reach is ClipDetector's smooth-crest bound V = (tau + q)*stretch^2 + q — which gates a
    # strict >=, so a 1-ulp difference creates or destroys a whole RUN rather than moving a last digit. The
    # numbers this suite pins are the numbers `fcore_measure clips` and the wasm module produce, and both of
    # those are built contract-off; a suite built contract-on would be pinning a third answer. -fno-fast-math
    # is stated for the same reason it is stated in tools/wasm/build.sh: nothing here may be reassociated.
    # The offline analyzers' ABI surface under adversarial arguments — natively, so ASan/UBSan see it. fc_probe.cpp
    # compiles anywhere; EMSCRIPTEN_KEEPALIVE degrades to nothing off the wasm tier.
    add_executable(felitronics_analysis_abi_tests tests/AnalysisAbiTests.cpp wasm/fc_probe.cpp)
    target_link_libraries(felitronics_analysis_abi_tests PRIVATE felitronics::analysis_offline felitronics::tempo felitronics::oversampling felitronics::test_support)
    target_include_directories(felitronics_analysis_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_analysis_abi_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_analysis_abi_tests COMMAND felitronics_analysis_abi_tests)

    add_executable(felitronics_clips_exposure_tests tests/ClipsExposureTests.cpp wasm/fc_probe.cpp)
    target_link_libraries(felitronics_clips_exposure_tests PRIVATE felitronics::analysis_offline felitronics::tempo felitronics::oversampling felitronics::test_support)
    target_include_directories(felitronics_clips_exposure_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_clips_exposure_tests PRIVATE cxx_std_20)
    target_compile_options(felitronics_clips_exposure_tests PRIVATE ${FCORE_NO_FP_CONTRACT}
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-fast-math>)
    add_test(NAME felitronics_clips_exposure_tests COMMAND felitronics_clips_exposure_tests)

    # The streaming surface of the same ABI: the fc_stream_* handles over fcore::StreamProbe, fed in pieces,
    # against the same two instruments handed the whole buffer. Contract-off for the clips suite's reason — the
    # detector's smooth-crest bound decides whether a run exists, and both parity roads are built this way.
    add_executable(felitronics_stream_abi_tests tests/StreamAbiTests.cpp wasm/fc_probe.cpp)
    target_link_libraries(felitronics_stream_abi_tests PRIVATE felitronics::analysis_offline felitronics::tempo felitronics::oversampling felitronics::test_support)
    target_include_directories(felitronics_stream_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_stream_abi_tests PRIVATE cxx_std_20)
    target_compile_options(felitronics_stream_abi_tests PRIVATE ${FCORE_NO_FP_CONTRACT}
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-fast-math>)
    add_test(NAME felitronics_stream_abi_tests COMMAND felitronics_stream_abi_tests)

    # The tempo surface of the same ABI (fc_probe_tempo_*): the ABI against a direct TempoDetector call, bit for
    # bit; every getter under the canary fence; the price against the core's storageFor() and against what the
    # FIRST run asks the heap for; the refusals the run and the price must agree on. Its own binary because each
    # analyzer behind this ABI is a function-local static, and the allocation oracle needs a module whose tempo
    # detector has never run.
    add_executable(felitronics_tempo_abi_tests tests/TempoAbiTests.cpp wasm/fc_probe.cpp)
    target_link_libraries(felitronics_tempo_abi_tests PRIVATE felitronics::analysis_offline felitronics::tempo felitronics::oversampling felitronics::test_support)
    target_include_directories(felitronics_tempo_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_tempo_abi_tests PRIVATE cxx_std_20)
    # Contract-off, as the wasm module it stands in for is built: the class and the ABI in this binary then compute
    # at the parity roads' arithmetic. (Nothing on the tempo path contracts at `on` either — measured — so this is
    # the convention, not a fix.)
    target_compile_options(felitronics_tempo_abi_tests PRIVATE ${FCORE_NO_FP_CONTRACT}
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-fast-math>)
    add_test(NAME felitronics_tempo_abi_tests COMMAND felitronics_tempo_abi_tests)

    # THE SAME SUITE AGAINST THE fctempo MODULE's translation unit (tools/wasm/fc_tempo.cpp: the tempo detector alone,
    # the ten fc_probe_tempo_* entry points from the one text both modules compile, and fc_tempo_abi_version). The
    # suite is the probe's, unchanged, so every claim it makes of the probe it makes of this module; FC_TEMPO_MODULE
    # adds the version. On the wasm tier this is also the module's checked build — SAFE_HEAP and ASSERTIONS=2 in CI —
    # which is why build.sh builds no debug variant of fctempo. Same flags as the target above, for its reasons.
    add_executable(felitronics_fctempo_abi_tests tests/TempoAbiTests.cpp wasm/fc_tempo.cpp)
    target_link_libraries(felitronics_fctempo_abi_tests PRIVATE felitronics::tempo felitronics::test_support)
    target_include_directories(felitronics_fctempo_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_fctempo_abi_tests PRIVATE cxx_std_20)
    target_compile_definitions(felitronics_fctempo_abi_tests PRIVATE FC_TEMPO_MODULE=1)
    target_compile_options(felitronics_fctempo_abi_tests PRIVATE ${FCORE_NO_FP_CONTRACT}
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-fast-math>)
    add_test(NAME felitronics_fctempo_abi_tests COMMAND felitronics_fctempo_abi_tests)

    # The mastering C ABI. Same arrangement as felitronics_abi_tests one block up — the shim compiles
    # natively, so its validation, ownership and mapping layer runs under ctest, ASan and UBSan. NOT
    # given FCORE_NO_FP_CONTRACT, for the reason spelled out at fcore_master above: this suite and the
    # `mastering` module suite must be the same arithmetic or a comparison between them means nothing.
    add_executable(felitronics_master_abi_tests tests/MasterAbiTests.cpp wasm/fc_master.cpp)
    target_link_libraries(felitronics_master_abi_tests PRIVATE felitronics::mastering felitronics::analysis felitronics::test_support)
    target_include_directories(felitronics_master_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_master_abi_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_master_abi_tests COMMAND felitronics_master_abi_tests)

    # Re-entry poisons the module, for good — a binary of its own because the poison is permanent by contract and
    # the suite above spends its one poisoning on a call that never returned. It needs no exceptions, so the wasm
    # tier builds and runs it too.
    add_executable(felitronics_master_abi_reentry_tests tests/MasterAbiReentryTests.cpp wasm/fc_master.cpp)
    target_link_libraries(felitronics_master_abi_reentry_tests PRIVATE felitronics::mastering felitronics::analysis felitronics::test_support)
    target_include_directories(felitronics_master_abi_reentry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_master_abi_reentry_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_master_abi_reentry_tests COMMAND felitronics_master_abi_reentry_tests)

    # `FC_DOMAINS` of tools/wasm/fc-master-layout.mjs against the running ABI, one real call per bound. The
    # table is the test's ARGUMENT and every probe is derived from its numbers. Same arrangement as the suites
    # above — the shim compiles natively, so this runs under ASan too.
    # NOT ON THE WASM TIER: the suite reads the layout file off the host filesystem, which the node runner the
    # emscripten tests go through does not have. layout-check.mjs holds the same table against the wasm build.
    if(NOT EMSCRIPTEN)
        add_executable(felitronics_master_domains_tests tests/MasterDomainsTests.cpp wasm/fc_master.cpp)
        target_link_libraries(felitronics_master_domains_tests PRIVATE felitronics::mastering felitronics::analysis felitronics::test_support)
        target_include_directories(felitronics_master_domains_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_features(felitronics_master_domains_tests PRIVATE cxx_std_20)
        add_test(NAME felitronics_master_domains_tests
                 COMMAND felitronics_master_domains_tests ${CMAKE_CURRENT_SOURCE_DIR}/wasm/fc-master-layout.mjs)
    endif()

    add_executable(felitronics_master_abi_progress_tests tests/MasterAbiProgressTests.cpp wasm/fc_master.cpp)
    target_link_libraries(felitronics_master_abi_progress_tests PRIVATE felitronics::mastering felitronics::analysis felitronics::test_support)
    target_include_directories(felitronics_master_abi_progress_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_master_abi_progress_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_master_abi_progress_tests COMMAND felitronics_master_abi_progress_tests)

    # The bit-exactness acceptance itself, run as a test rather than by hand: the same programme through
    # the C ABI and through a direct C++ call, one binary, one machine, compared bit for bit — plus
    # block-independence through the ABI and the reset discipline.
    add_test(NAME felitronics_master_abi_selftest COMMAND fcore_master selftest)

    # The JavaScript layout against the compiler, field by field (tools/wasm/layout-check.mjs). On the wasm tier the
    # argument is the emscripten build of fcore_master, so the offsets checked are wasm32's — the ones a page uses.
    # Needs node; a desktop row without it skips the test and says so rather than passing it.
    find_program(FELITRONICS_NODE node)
    if(FELITRONICS_NODE)
        add_test(NAME felitronics_master_layout_check
                 COMMAND ${FELITRONICS_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/wasm/layout-check.mjs $<TARGET_FILE:fcore_master>)
        # And the two refusals the layout file makes for a page — the version gate and the write past a stamp.
        add_test(NAME felitronics_master_layout_gate_tests
                 COMMAND ${FELITRONICS_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/wasm/layout-gate-test.mjs)
    else()
        message(STATUS "felitronics_master_layout_check: node not found — the JS layout is not checked on this row")
    endif()
endif()

# fcore_master — the C-ABI facade over `felitronics::mastering` (tools/fc_master_abi.h +
# tools/wasm/fc_master.cpp) and the native CLI that drives it. Two things to notice, both load-bearing:
#
#  1. IT DELIBERATELY DOES **NOT** TAKE `FCORE_NO_FP_CONTRACT`. Every other target in this file carries
#     `-ffp-contract=off` because it is part of the wasm probe's cross-toolchain contract; this one is the
#     reference for the C++ API the DESKTOP app links, so it takes the repository's declared
#     `-ffp-contract=on` (law 10 — the flag is STATED, never inherited by accident). Measured on one
#     source, the two flavours differ in 203269 of 288000 samples (worst 2.03e-6). Mixing them WITHIN a
#     binary is worse than either: two TUs built with different flags measured as AGREEING, because the
#     linker merged the header-only instantiations and whichever TU came first won — agreement by link
#     order, which is exactly the false-green `selftest` exists to prevent. So the whole target, both
#     halves of the comparison included, is built one way and that way is stated here.
#  2. THE CLI AND ITS ABI ARE SEPARATE TUs ON PURPOSE. `selftest` compares a render through the C entry
#     points against a direct C++ call; inlining the facade into the caller would make the "ABI" half
#     stop being an ABI at all.
add_executable(fcore_master fcore_master.cpp wasm/fc_master.cpp)
target_link_libraries(fcore_master PRIVATE felitronics::mastering felitronics::analysis)
target_include_directories(fcore_master PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fcore_master PRIVATE cxx_std_20)

# THE C BOUNDARY'S TRANSLATION UNIT, built once as an object library with the session library's OWN compile options and
# definitions (read off the target, not restated): no exceptions, no RTTI, no contraction, no fast-math — so the facade
# is held to the brain's flags on every native row as it is in the wasm module (tools/wasm/build.sh), and its object is
# the one the object-file gate reads. Its first include is the library's src/BuildGuards.h, as every unit of the library's
# is, so a per-source flag the compile line does not show is refused by the preprocessor here too. fcore_session and the
# ABI suite link this object rather than compiling the source again.
add_library(felitronics_session_facade OBJECT wasm/fc_session.cpp)
target_link_libraries(felitronics_session_facade PRIVATE felitronics::session)
target_include_directories(felitronics_session_facade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                              ${PROJECT_SOURCE_DIR}/modules/session/src)
target_compile_features(felitronics_session_facade PRIVATE cxx_std_20)
target_compile_options(felitronics_session_facade PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
target_compile_definitions(felitronics_session_facade PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_DEFINITIONS>)

# fcore_session — the native CLI over `felitronics::session` (tools/fcore_session.cpp): the library linked AS C++, the
# way a desktop application links it, plus the fc_session ABI's object beside it, so the ABI version it prints is the
# facade's own answer. The two are separate translation units for fcore_master's reason: the facade stays a facade.
add_executable(fcore_session fcore_session.cpp $<TARGET_OBJECTS:felitronics_session_facade>)
target_link_libraries(fcore_session PRIVATE felitronics::session)
target_include_directories(fcore_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fcore_session PRIVATE cxx_std_20)

if(FELITRONICS_BUILD_TESTS)
    # fc_session, draft version 0 (tools/fc_session_abi.h): the out-pointer's checks, the handles and their generations, the table's
    # capacity, what a create allocates against the session's declared demand, and the poison — natively, under ctest,
    # ASan and UBSan, and on the wasm tier. One binary: the poison is its last group, because it is for good.
    add_executable(felitronics_session_abi_tests tests/SessionAbiTests.cpp $<TARGET_OBJECTS:felitronics_session_facade>)
    target_link_libraries(felitronics_session_abi_tests PRIVATE felitronics::session felitronics::test_support)
    target_include_directories(felitronics_session_abi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_session_abi_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_session_abi_tests COMMAND felitronics_session_abi_tests)
    # The 16.7-million-generation walk alone: one test -j cannot split, so an instrumented build can leave it out.
    add_test(NAME felitronics_session_abi_generation_tests COMMAND felitronics_session_abi_tests --generation-walk)

    add_library(felitronics_session_replay_facade OBJECT tests/SessionReplayFacade.cpp)
    target_link_libraries(felitronics_session_replay_facade PRIVATE felitronics::session)
    target_include_directories(felitronics_session_replay_facade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/modules/session/src)
    target_compile_features(felitronics_session_replay_facade PRIVATE cxx_std_20)
    target_compile_options(felitronics_session_replay_facade PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
    target_compile_definitions(felitronics_session_replay_facade PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_DEFINITIONS>)
    add_executable(felitronics_session_replay_tests tests/SessionReplayTests.cpp $<TARGET_OBJECTS:felitronics_session_replay_facade>)
    target_link_libraries(felitronics_session_replay_tests PRIVATE felitronics::session felitronics::test_support)
    target_include_directories(felitronics_session_replay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(felitronics_session_replay_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_session_replay_tests COMMAND felitronics_session_replay_tests)

    # THE OBJECT-FILE GATE over felitronics::session and its C boundary (modules/session/tests/object-gates.cmake): every
    # object of the library holds no writable data and calls nothing outside the list in tools/lint/session-objects.txt;
    # the facade's object keeps exactly its handle table and its poison flag. Read with this row's own symbol-table reader
    # — readelf on ELF, objdump on Mach-O, dumpbin on COFF, llvm-readobj on wasm — so every row of the matrix, the sanitizer row
    # and the wasm tier included, asks the question of the objects it actually built. Its controls are in
    # modules/session/CMakeLists.txt.
    include(${PROJECT_SOURCE_DIR}/modules/session/tests/SessionTestTools.cmake)
    felitronics_session_object_gate(NAME felitronics_session_object_gate
                                    LIBRARY felitronics_session FACADE felitronics_session_facade)
    # ...and the FACADE role's own controls: the boundary exactly as it ships, compiled as the facade is, with a third
    # global planted (it must be refused while each allowed global is found once), and with an allowed global renamed
    # (the allowance names a symbol that is not there — rot, refused as [ALLOWANCE]). Sources in
    # modules/session/tests/object-controls/, beside the library's controls.
    foreach(entry "facade_third_global|g_planted" "facade_renamed|g_callState")
        string(REPLACE "|" ";" f "${entry}")
        list(GET f 0 ctl)
        list(GET f 1 expect)
        add_library(fs_objctl_${ctl} OBJECT ${PROJECT_SOURCE_DIR}/modules/session/tests/object-controls/${ctl}.cpp)
        target_link_libraries(fs_objctl_${ctl} PRIVATE felitronics::session)
        target_include_directories(fs_objctl_${ctl} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/wasm
                                                           ${PROJECT_SOURCE_DIR}/modules/session/src)
        target_compile_features(fs_objctl_${ctl} PRIVATE cxx_std_20)
        target_compile_options(fs_objctl_${ctl} PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_OPTIONS>)
        target_compile_definitions(fs_objctl_${ctl} PRIVATE $<TARGET_PROPERTY:felitronics_session,COMPILE_DEFINITIONS>)
        felitronics_session_object_gate(NAME felitronics_session_object_control_${ctl}
                                        LIBRARY felitronics_session FACADE fs_objctl_${ctl} EXPECT "${expect}")
    endforeach()

    # THE CLI'S CONTRACT, byte for byte on stdout and by exit status (tools/tests/cli-expect.cmake): the versions it
    # prints are the ones this build was configured with; the table it prints is the one docs/SESSION.md shows; an
    # empty script and a script of comments are accepted with `done 0`, from a file and from stdin; the config it prints
    # is its sources in canonical form, and its version theirs; a script with a command in it, a script that is not there
    # and a malformed command line are refused with 2 and nothing on stdout.
    # NOT ON THE WASM TIER: the CLI reads its script from the host's filesystem, which the node runner the emscripten
    # tests go through does not have. The wasm road into the session is the fcsession module, and CI checks that.
    if(NOT EMSCRIPTEN)
        file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/fc_session_abi.h FCORE_SESSION_ABI_LINE
             REGEX "^#define FC_SESSION_ABI_VERSION [0-9]+u$")
        if(NOT FCORE_SESSION_ABI_LINE MATCHES "^#define FC_SESSION_ABI_VERSION ([0-9]+)u$")
            message(FATAL_ERROR "tools/fc_session_abi.h: no FC_SESSION_ABI_VERSION line")
        endif()
        set(FCORE_SESSION_EXPECT ${CMAKE_CURRENT_BINARY_DIR}/fcore_session-expect)
        file(WRITE ${FCORE_SESSION_EXPECT}/version.txt
             "felitronics-mastering-core ${felitronics_mastering_core_VERSION}\n"
             "felitronics-core ${FELITRONICS_MASTERING_FCORE_VERSION}\n"
             "fc_session_abi ${CMAKE_MATCH_1}\n")
        file(WRITE ${FCORE_SESSION_EXPECT}/done0.txt "done 0\n")
        file(WRITE ${FCORE_SESSION_EXPECT}/nothing.txt "")
        # THE CONFIG IT PRINTS is the source documents in canonical form, and its version is theirs: the expected outputs
        # are written at build time from modules/session/config by the session's own config check (--expect), so a
        # library whose embedded config is not its sources fails here as well as in its own suite.
        set(FCORE_SESSION_CONFIG ${PROJECT_SOURCE_DIR}/modules/session/config)
        set(FCORE_SESSION_CONFIG_EXPECT ${FCORE_SESSION_EXPECT}/config)
        file(MAKE_DIRECTORY ${FCORE_SESSION_CONFIG_EXPECT})
        add_custom_command(
            OUTPUT ${FCORE_SESSION_CONFIG_EXPECT}/targets.toml ${FCORE_SESSION_CONFIG_EXPECT}/engine.toml
                   ${FCORE_SESSION_CONFIG_EXPECT}/version.txt ${FCORE_SESSION_CONFIG_EXPECT}/sound-version.txt
            COMMAND felitronics_session_config_check --expect ${FCORE_SESSION_CONFIG}/targets.toml
                    ${FCORE_SESSION_CONFIG}/engine.toml ${FCORE_SESSION_CONFIG_EXPECT}
            DEPENDS felitronics_session_config_check ${FCORE_SESSION_CONFIG}/targets.toml ${FCORE_SESSION_CONFIG}/engine.toml
            COMMENT "fcore_session: what `config` must print"
            VERBATIM)
        add_custom_target(fcore_session_config_expect ALL DEPENDS
            ${FCORE_SESSION_CONFIG_EXPECT}/targets.toml ${FCORE_SESSION_CONFIG_EXPECT}/engine.toml
            ${FCORE_SESSION_CONFIG_EXPECT}/version.txt ${FCORE_SESSION_CONFIG_EXPECT}/sound-version.txt)
        # WHO MAY DO WHAT, WHEN, IN THE DOCS IS THE TABLE IN THE CODE: `fcore_session table` prints Commands.h's tables, and
        # the text docs/SESSION.md carries between its markers must be exactly that output. Read here at configure time,
        # line endings as LF (a Windows checkout may have rewritten them); an edit of the document configures again.
        set(FCORE_SESSION_DOC ${PROJECT_SOURCE_DIR}/docs/SESSION.md)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FCORE_SESSION_DOC})
        file(READ ${FCORE_SESSION_DOC} FCORE_SESSION_DOC_TEXT)
        string(REPLACE "\r\n" "\n" FCORE_SESSION_DOC_TEXT "${FCORE_SESSION_DOC_TEXT}")
        set(FCORE_SESSION_TABLE_BEGIN "<!-- the table: begin -->\n")
        string(FIND "${FCORE_SESSION_DOC_TEXT}" "${FCORE_SESSION_TABLE_BEGIN}" FCORE_SESSION_TABLE_AT)
        string(FIND "${FCORE_SESSION_DOC_TEXT}" "<!-- the table: end -->" FCORE_SESSION_TABLE_END)
        if(FCORE_SESSION_TABLE_AT EQUAL -1 OR FCORE_SESSION_TABLE_END EQUAL -1)
            message(FATAL_ERROR "docs/SESSION.md: the table's markers (<!-- the table: begin --> / end) are not there")
        endif()
        string(LENGTH "${FCORE_SESSION_TABLE_BEGIN}" FCORE_SESSION_TABLE_SKIP)
        math(EXPR FCORE_SESSION_TABLE_AT "${FCORE_SESSION_TABLE_AT} + ${FCORE_SESSION_TABLE_SKIP}")
        math(EXPR FCORE_SESSION_TABLE_LENGTH "${FCORE_SESSION_TABLE_END} - ${FCORE_SESSION_TABLE_AT}")
        string(SUBSTRING "${FCORE_SESSION_DOC_TEXT}" ${FCORE_SESSION_TABLE_AT} ${FCORE_SESSION_TABLE_LENGTH} FCORE_SESSION_TABLE)
        file(WRITE ${FCORE_SESSION_EXPECT}/table.md "${FCORE_SESSION_TABLE}")
        set(FCORE_SESSION_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/session-scripts)
        # name | expected exit | expected stdout | stdin file ("-" none) | arguments...
        set(FCORE_SESSION_CASES
            "version|0|version.txt|-|version"
            "table|0|table.md|-|table"
            "empty|0|done0.txt|-|run|${FCORE_SESSION_SCRIPTS}/empty.txt"
            "comments|0|done0.txt|-|run|${FCORE_SESSION_SCRIPTS}/comments.txt"
            "stdin|0|done0.txt|${FCORE_SESSION_SCRIPTS}/comments.txt|run|-"
            "command|2|nothing.txt|-|run|${FCORE_SESSION_SCRIPTS}/command.txt"
            "missing|2|nothing.txt|-|run|${FCORE_SESSION_SCRIPTS}/no-such-script.txt"
            "noargs|2|nothing.txt|-"
            "bogus|2|nothing.txt|-|bogus"
            "extra|2|nothing.txt|-|version|extra"
            "config_targets|0|config/targets.toml|-|config|targets"
            "config_engine|0|config/engine.toml|-|config|engine"
            "config_version|0|config/version.txt|-|config|version"
            "config_sound_version|0|config/sound-version.txt|-|config|sound-version"
            "config_bogus|2|nothing.txt|-|config|bogus"
            "config_bare|2|nothing.txt|-|config")
        foreach(case IN LISTS FCORE_SESSION_CASES)
            string(REPLACE "|" ";" f "${case}")
            list(GET f 0 name)
            list(GET f 1 rc)
            list(GET f 2 out)
            list(GET f 3 in)
            set(cliArgs "")
            list(LENGTH f nFields)
            if(nFields GREATER 4)
                list(SUBLIST f 4 -1 cliArgs)
            endif()
            list(LENGTH cliArgs nArgs)
            set(argDefs "")
            set(i 0)
            foreach(a IN LISTS cliArgs)
                list(APPEND argDefs "-DARG${i}=${a}")
                math(EXPR i "${i} + 1")
            endforeach()
            add_test(NAME fcore_session_${name}
                     COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:fcore_session> -DNARGS=${nArgs} ${argDefs}
                             -DEXPECT_RC=${rc} -DEXPECT_STDOUT=${FCORE_SESSION_EXPECT}/${out} -DSTDIN=${in}
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli-expect.cmake)
        endforeach()
    endif()
endif()
