# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# The two kinds of gate felitronics::session's tests are built from, as functions — included by modules/session and by
# tools/ (the C boundary's gate lives next to the boundary). Included once per directory that uses them.

# A BUILD THAT MUST FAIL, for the named reason: builds <target> (excluded from the default build) from inside ctest and
# requires it to fail with a diagnostic matching <expect> (tests/build-must-fail.cmake). One build of this tree at a
# time: two build tools running in one build directory corrupt its state.
function(felitronics_session_must_fail name target expect)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${target} -DCONFIG=$<CONFIG>
                     "-DEXPECT=${expect}" -P ${PROJECT_SOURCE_DIR}/modules/session/tests/build-must-fail.cmake)
    set_tests_properties(${name} PROPERTIES RESOURCE_LOCK felitronics_build_tree)
endfunction()

# A BUILD THAT MUST SUCCEED: builds <target> (excluded from the default build) from inside ctest and requires it to build —
# for the one control whose licence the library's own options are measured to win against.
function(felitronics_session_must_build name target)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target} --config $<CONFIG>)
    set_tests_properties(${name} PROPERTIES RESOURCE_LOCK felitronics_build_tree)
endfunction()

# THE OBJECT-FILE GATE (tests/object-gates.cmake) — the question the object file answers exactly, asked of compiled
# objects: which symbols live in writable memory, and which symbols are called that nothing in the set defines. The
# tool that reads the symbol table is this row's own, and writable is what the object says of the section:
#   ELF (Linux)      readelf -S -s          the section's A and W flags (.data.rel.ro* read-only), COMMON
#   Mach-O (Apple)   objdump -t             every section but __TEXT,*, __DATA_CONST,*, __DATA,__const
#   COFF (MSVC)      dumpbin /headers       the section's IMAGE_SCN_MEM_WRITE
#                           /symbols
#   wasm             llvm-readobj           every data segment but .rodata* and .data.rel.ro* (names demangled by the
#                                           SDK's llvm-cxxfilt beside it)
# No tool, no gate: configuring the tests without one is an error, not a skipped check.
if(EMSCRIPTEN)
    set(FELITRONICS_SESSION_OBJECT_FORMAT wasm)
    # The SDK's own LLVM, beside the compiler: <emsdk>/upstream/emscripten/em++ and <emsdk>/upstream/bin/llvm-readobj.
    get_filename_component(_fs_ccdir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(FELITRONICS_SESSION_OBJECT_TOOL llvm-readobj
                 HINTS "${EMSCRIPTEN_ROOT_PATH}/../bin" "${_fs_ccdir}/../bin" NO_DEFAULT_PATH)
elseif(MSVC)
    set(FELITRONICS_SESSION_OBJECT_FORMAT coff)
    get_filename_component(_fs_tooldir "${CMAKE_LINKER}" DIRECTORY)
    get_filename_component(_fs_ccdir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(FELITRONICS_SESSION_OBJECT_TOOL dumpbin HINTS "${_fs_tooldir}" "${_fs_ccdir}")
elseif(APPLE)
    set(FELITRONICS_SESSION_OBJECT_FORMAT macho)
    set(FELITRONICS_SESSION_OBJECT_TOOL "${CMAKE_OBJDUMP}")
else()
    set(FELITRONICS_SESSION_OBJECT_FORMAT elf)
    set(FELITRONICS_SESSION_OBJECT_TOOL "${CMAKE_READELF}")
endif()
if(NOT FELITRONICS_SESSION_OBJECT_TOOL OR NOT EXISTS "${FELITRONICS_SESSION_OBJECT_TOOL}")
    if(FELITRONICS_SESSION_OBJECT_FORMAT STREQUAL "elf")
        find_program(FELITRONICS_SESSION_OBJECT_TOOL_FALLBACK NAMES readelf llvm-readelf)
    elseif(FELITRONICS_SESSION_OBJECT_FORMAT STREQUAL "macho")
        find_program(FELITRONICS_SESSION_OBJECT_TOOL_FALLBACK NAMES objdump llvm-objdump)
    endif()
    if(FELITRONICS_SESSION_OBJECT_TOOL_FALLBACK)
        set(FELITRONICS_SESSION_OBJECT_TOOL "${FELITRONICS_SESSION_OBJECT_TOOL_FALLBACK}")
    else()
        message(FATAL_ERROR "felitronics::session's object-file gates need a symbol-table reader for "
                            "${FELITRONICS_SESSION_OBJECT_FORMAT} objects on this row (readelf / objdump / dumpbin / "
                            "llvm-readobj), and found none. The gates are tests of this repository; without the tool "
                            "they cannot run, and a gate that does not run must not read as passed.")
    endif()
endif()

# felitronics_session_object_gate(NAME <test> LIBRARY <object-bearing targets...> [FACADE <targets...>] [EXPECT <re>])
#   LIBRARY  targets whose objects are held to the session's law: no writable data at all.
#   FACADE   targets whose objects may keep exactly the globals tools/lint/session-objects.txt allows them by name.
#   EXPECT   a control: the gate must REFUSE, and name each `&&&`-separated regex among the symbols it refuses.
function(felitronics_session_object_gate)
    cmake_parse_arguments(G "" "NAME;EXPECT" "LIBRARY;FACADE" ${ARGN})
    set(content "")
    foreach(role LIBRARY FACADE)
        string(TOLOWER ${role} r)
        foreach(t IN LISTS G_${role})
            string(APPEND content "$<$<BOOL:$<TARGET_OBJECTS:${t}>>:${r}|$<JOIN:$<TARGET_OBJECTS:${t}>,\n${r}|>\n>")
        endforeach()
    endforeach()
    set(list_file ${CMAKE_CURRENT_BINARY_DIR}/${G_NAME}-objects-$<CONFIG>.txt)
    file(GENERATE OUTPUT ${list_file} CONTENT "${content}")
    set(sanitizers OFF)
    if(FELITRONICS_ENABLE_SANITIZERS)
        set(sanitizers ON)
    endif()
    # The `coff-debug` lines apply to an MSVC build in its Debug configuration alone (/MDd, the debug runtime).
    if(MSVC)
        set(msvc_debug $<IF:$<CONFIG:Debug>,ON,OFF>)
    else()
        set(msvc_debug OFF)
    endif()
    add_test(NAME ${G_NAME}
             COMMAND ${CMAKE_COMMAND}
                     -DFORMAT=${FELITRONICS_SESSION_OBJECT_FORMAT}
                     -DTOOL=${FELITRONICS_SESSION_OBJECT_TOOL}
                     -DOBJECTS_FILE=${list_file}
                     -DLISTS=${PROJECT_SOURCE_DIR}/tools/lint/session-objects.txt
                     -DSANITIZERS=${sanitizers}
                     -DMSVC_DEBUG=${msvc_debug}
                     "-DEXPECT=${G_EXPECT}"
                     -P ${PROJECT_SOURCE_DIR}/modules/session/tests/object-gates.cmake)
endfunction()
