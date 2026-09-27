# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#
# THE COMPILE-LINE GATE'S CONTROLS (compile-line-gate.cmake). Run by ctest:
#   cmake -DGATE=<gate> -DCOMPILE_COMMANDS=<this build's compile_commands.json> -DFLAGS_FILE=<build-flags.txt>
#         -DTARGETS=<dirs> -DSOURCES=<paths> -DPERSOURCE=<source> -DPERSOURCE_TARGET=<dir>
#         -DPERINCLUDE=<source> -DPERINCLUDE_TARGET=<dir> -DPERINCLUDE_HEADER=<header> -DSCRATCH=<dir>
#         -P compile-line-controls.cmake
#
# 1, 2. REAL PER-SOURCE OPTIONS: CMake itself wrote the compile commands of two copies of a library unit, compiled with
#    the library's options and a source property each — `-ffp-model=fast` (fs_cl_persource), and a JOINED forced include,
#    `-include<absolute path>` (fs_cl_perinclude); configured, never built. Each red, naming its option.
# 3-.. THIS BUILD'S OWN compile_commands.json, edited: a licence after the group (-fno-honor-nans), the group removed from
#    one line, a separated forced include, every other joined spelling of a forced include, pass-through, plugin or flag
#    file, a unit compiled into the target that is not a listed source, a listed source that nothing compiles — each red,
#    naming what it planted; and the same file with every `output` field removed (CMake 3.22's Ninja and Makefile
#    generators write none) and with `command` rewritten as an `arguments` array — each green, holding exactly as many
#    compile commands as the file as written.
cmake_minimum_required(VERSION 3.22)
foreach(var GATE COMPILE_COMMANDS FLAGS_FILE TARGETS SOURCES PERSOURCE PERSOURCE_TARGET PERINCLUDE PERINCLUDE_TARGET
            PERINCLUDE_HEADER SCRATCH)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "compile-line-controls.cmake: ${var} is not set")
    endif()
endforeach()
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

# run_gate <json> <targets> <sources> <out var: rc> <out var: text>
function(run_gate json tgts srcs rc_var out_var)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCOMPILE_COMMANDS=${json}" "-DFLAGS_FILE=${FLAGS_FILE}"
                            "-DTARGETS=${tgts}" "-DSOURCES=${srcs}" -P "${GATE}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(${rc_var} ${rc} PARENT_SCOPE)
    set(${out_var} "${out}${err}" PARENT_SCOPE)
endfunction()
set(n_controls 0)
function(expect_red name json tgts srcs needle)
    run_gate("${json}" "${tgts}" "${srcs}" rc out)
    if(rc EQUAL 0)
        message(FATAL_ERROR "CONTROL FAILED (${name}): the compile-line gate PASSED:\n${out}")
    endif()
    string(FIND "${out}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "CONTROL FAILED (${name}): the gate refused, but did not name `${needle}`:\n${out}")
    endif()
    message(STATUS "control ok (${name}): refused, naming `${needle}`")
endfunction()
function(expect_green name json tgts srcs count)
    run_gate("${json}" "${tgts}" "${srcs}" rc out)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "CONTROL FAILED (${name}): the gate refused a correct build:\n${out}")
    endif()
    if(NOT out MATCHES "compile line: ${count} compile command")
        message(FATAL_ERROR "CONTROL FAILED (${name}): the gate passed, but did not read ${count} compile command(s):\n${out}")
    endif()
    message(STATUS "control ok (${name}): green, ${count} compile command(s) read")
endfunction()

# json_string <value> <out> — a JSON string literal
function(json_string value out)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()
# planted <name> <edited json text> — written to SCRATCH, the path returned in PLANTED
function(planted name text)
    set(p "${SCRATCH}/${name}.json")
    file(WRITE "${p}" "${text}")
    set(PLANTED "${p}" PARENT_SCOPE)
endfunction()

# The file as written must pass, and says how many commands it holds.
run_gate("${COMPILE_COMMANDS}" "${TARGETS}" "${SOURCES}" rc out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the build's own compile line does not pass — the controls would prove nothing:\n${out}")
endif()
if(NOT out MATCHES "compile line: ([0-9]+) compile command")
    message(FATAL_ERROR "compile-line-controls.cmake: cannot read the gate's count:\n${out}")
endif()
set(real_count ${CMAKE_MATCH_1})

# 1, 2: the real per-source options
expect_red("per-source -ffp-model=fast" "${COMPILE_COMMANDS}" "${PERSOURCE_TARGET}" "${PERSOURCE}" "-ffp-model=fast")
expect_red("per-source joined -include<path>" "${COMPILE_COMMANDS}" "${PERINCLUDE_TARGET}" "${PERINCLUDE}"
           "`-include${PERINCLUDE_HEADER}`")

# The edits are made on the entries that build into TARGETS alone — the rest of the file is other targets', and editing
# a few hundred entries of JSON one call at a time is slow for nothing. That excerpt must itself pass with the same count.
file(READ "${COMPILE_COMMANDS}" full)
string(JSON nfull LENGTH "${full}")
math(EXPR lastfull "${nfull} - 1")
string(REPLACE "|" ";" target_list "${TARGETS}")
set(json "")
foreach(i RANGE ${lastfull})
    string(JSON c GET "${full}" ${i} command)
    foreach(t IN LISTS target_list)
        if(c MATCHES "[/\\\\]${t}[/\\\\]")
            string(JSON e GET "${full}" ${i})
            if(json STREQUAL "")
                set(json "${e}")
            else()
                string(APPEND json ",${e}")
            endif()
            break()
        endif()
    endforeach()
endforeach()
set(json "[${json}]")
planted(excerpt "${json}")
expect_green("the excerpt as written" "${PLANTED}" "${TARGETS}" "${SOURCES}" ${real_count})
string(JSON n LENGTH "${json}")
math(EXPR last "${n} - 1")
string(REPLACE "|" ";" source_list "${SOURCES}")
list(GET source_list 0 first_source)
file(REAL_PATH "${first_source}" first_source)

# The index of the first entry that builds <first_source> into TARGETS, and its command.
set(victim -1)
foreach(i RANGE ${last})
    string(JSON f GET "${json}" ${i} file)
    string(JSON c GET "${json}" ${i} command)
    if(EXISTS "${f}")
        file(REAL_PATH "${f}" f)
    endif()
    if(f STREQUAL first_source)
        foreach(t IN LISTS target_list)
            if(c MATCHES "[/\\\\]${t}[/\\\\]")
                set(victim ${i})
            endif()
        endforeach()
    endif()
    if(victim GREATER_EQUAL 0)
        break()
    endif()
endforeach()
if(victim LESS 0)
    message(FATAL_ERROR "compile-line-controls.cmake: no entry builds ${first_source} into ${TARGETS}")
endif()
string(JSON victim_cmd GET "${json}" ${victim} command)
string(JSON victim_file GET "${json}" ${victim} file)
string(JSON victim_entry GET "${json}" ${victim})

# 2: a licence after the group, as a per-source option would put it: before `-o`, after everything the target states
string(REPLACE " -o " " -fno-honor-nans -o " cmd2 "${victim_cmd}")
json_string("${cmd2}" q)
string(JSON j2 SET "${json}" ${victim} command "${q}")
planted(honor_nans "${j2}")
expect_red("-fno-honor-nans after the group" "${PLANTED}" "${TARGETS}" "${SOURCES}" "-fno-honor-nans")

# 3: the group taken off one line (a de-duplication, or a hand-edited build)
file(STRINGS "${FLAGS_FILE}" group_line REGEX "^-")
string(REPLACE "${group_line}" "-fno-fast-math" cmd3 "${victim_cmd}")
if(cmd3 STREQUAL victim_cmd)
    message(FATAL_ERROR "compile-line-controls.cmake: `${group_line}` is not in the command as one run of text — the control would plant nothing")
endif()
json_string("${cmd3}" q)
string(JSON j3 SET "${json}" ${victim} command "${q}")
planted(no_group "${j3}")
expect_red("the group removed" "${PLANTED}" "${TARGETS}" "${SOURCES}" "is not on its compile line")

# 4: a forced include
string(REPLACE " -o " " -include planted.h -o " cmd4 "${victim_cmd}")
json_string("${cmd4}" q)
string(JSON j4 SET "${json}" ${victim} command "${q}")
planted(forced_include "${j4}")
expect_red("a forced include" "${PLANTED}" "${TARGETS}" "${SOURCES}" "`-include`")

# ...and every joined spelling a driver accepts, of a forced include, a pass-through, a plugin or a flag file
set(joined_spellings -imacros/planted/macros.h --include=/planted/forced.h --imacros=/planted/macros.h
                     -Wp,-include,/planted/forced.h -Xclang=-ffast-math -Xpreprocessor -Xarch_arm64 -mllvm=-x
                     /FIforced.h -FIforced.h /Yuforced.h /clang:-ffast-math -fplugin=/planted/p.so
                     -fpass-plugin=/planted/p.so -specs=/planted/s.specs --specs=/planted/s.specs
                     --config=/planted/c.cfg -B/planted/bin)
set(n_joined 0)
foreach(spelling IN LISTS joined_spellings)
    string(REPLACE " -o " " ${spelling} -o " cmdj "${victim_cmd}")
    json_string("${cmdj}" q)
    string(JSON jj SET "${json}" ${victim} command "${q}")
    string(MAKE_C_IDENTIFIER "${spelling}" jname)
    planted(joined_${jname} "${jj}")
    expect_red("${spelling}" "${PLANTED}" "${TARGETS}" "${SOURCES}" "`${spelling}`")
    math(EXPR n_joined "${n_joined} + 1")
endforeach()

# 5: a unit compiled into the target that is not a listed source
get_filename_component(vdir "${victim_file}" DIRECTORY)
set(planted_file "${vdir}/PlantedUnit.cpp")
string(REPLACE "${victim_file}" "${planted_file}" cmd5 "${victim_cmd}")
get_filename_component(vname "${victim_file}" NAME)
string(REPLACE "${vname}.o" "PlantedUnit.cpp.o" cmd5 "${cmd5}")
string(REPLACE "${vname}.obj" "PlantedUnit.cpp.obj" cmd5 "${cmd5}")
set(e5 "${victim_entry}")
json_string("${cmd5}" q)
string(JSON e5 SET "${e5}" command "${q}")
json_string("${planted_file}" q)
string(JSON e5 SET "${e5}" file "${q}")
string(JSON e5 ERROR_VARIABLE noout REMOVE "${e5}" output)
string(JSON j5 SET "${json}" ${n} "${e5}")
planted(unlisted_unit "${j5}")
expect_red("a unit in the target that is not listed" "${PLANTED}" "${TARGETS}" "${SOURCES}" "PlantedUnit.cpp")

# 6: a listed source that nothing compiles
expect_red("a listed source nothing compiles" "${COMPILE_COMMANDS}" "${TARGETS}" "${SOURCES}|${vdir}/NeverCompiled.cpp" "NeverCompiled.cpp")

# 7: no `output` anywhere — the object from -o
set(j7 "${json}")
foreach(i RANGE ${last})
    string(JSON j7 ERROR_VARIABLE noout REMOVE "${j7}" ${i} output)
endforeach()
planted(no_output "${j7}")
expect_green("no output fields" "${PLANTED}" "${TARGETS}" "${SOURCES}" ${real_count})

# 8: `arguments` instead of `command`, on every entry
set(j8 "${json}")
foreach(i RANGE ${last})
    string(JSON c GET "${j8}" ${i} command)
    separate_arguments(a UNIX_COMMAND "${c}")
    set(arr "")
    foreach(x IN LISTS a)
        json_string("${x}" q)
        if(arr STREQUAL "")
            set(arr "${q}")
        else()
            string(APPEND arr ",${q}")
        endif()
    endforeach()
    string(JSON j8 SET "${j8}" ${i} arguments "[${arr}]")
    string(JSON j8 REMOVE "${j8}" ${i} command)
endforeach()
planted(arguments "${j8}")
expect_green("arguments arrays" "${PLANTED}" "${TARGETS}" "${SOURCES}" ${real_count})

file(REMOVE_RECURSE "${SCRATCH}")
message(STATUS "compile-line gate controls: ${n_joined} joined spellings and 7 other plants refused as they must be, 3 correct variants passed")
