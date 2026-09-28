// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// Local only. Read an archived baseline, compile the same behavior probes, and require the old behavior to fail.
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, mkdirSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
const root = fileURLToPath(new URL('../', import.meta.url));
const cache = readFileSync(join(resolve(process.argv[2] ?? 'build'), 'CMakeCache.txt'), 'utf8');
const value = key => new RegExp(`^${key}:[^=]*=(.*)$`, 'm').exec(cache)?.[1];
const revision = 'f5f9913';
const temporary = mkdtempSync(join(tmpdir(), 'session-freeze-baseline-'));
const source = join(temporary, 'source'), build = join(temporary, 'build');
const run = (command, args, options = {}) => spawnSync(command, args, {encoding:'utf8', maxBuffer:16*1024*1024, ...options});
function pass(command, args, options) {
    const r = run(command, args, options); assert.equal(r.status, 0, `${command}: ${r.stdout}\n${r.stderr}`); return r.stdout;
}
function red(item, failed) { assert(failed, `${item}: regression must be red on ${revision}`); console.log(`${item}: RED on ${revision}`); }
try {
    mkdirSync(source);
    const archive = run('git', ['archive', revision], {cwd:root, encoding:null}); assert.equal(archive.status,0);
    pass('tar', ['-xf', '-', '-C', source], {input:archive.stdout});
    for (const file of ['SessionFreezeRegressions.cpp', 'SessionContractFixture.cpp'])
        copyFileSync(join(root,'tools/tests',file),join(source,'tools/tests',file));
    let destroy = readFileSync(join(root,'tools/tests/SessionDestroyReentryTests.cpp'),'utf8');
    destroy = destroy.replace(/const fc_session_capabilities caps \{[^;]+;/,
        'fc_session_capabilities caps {}; caps.heapCeilingBytes = 9007199254740991.0; caps.maxRateHz = 48000; caps.offeredDevices = 255;')
        .replace(' || fc_session_set_capacity (0, nullptr) != FC_SESSION_ERR_POISONED','');
    writeFileSync(join(source,'tools/tests/SessionDestroyReentryTests.cpp'),destroy);
    mkdirSync(join(source,'tools/destroy-control'));
    const counter = readFileSync(join(value('FELITRONICS_MASTERING_CORE_SOURCE_DIR'),'test_support/alloc_counter.h'),'utf8');
    writeFileSync(join(source,'tools/destroy-control/alloc_counter.h'), 'namespace session_destroy_control { void onFree() noexcept; }\n'
        + counter.replaceAll('std::free (p);','session_destroy_control::onFree(); std::free (p);'));
    const cmake = join(source,'tools/CMakeLists.txt');
    writeFileSync(cmake,readFileSync(cmake,'utf8')+`
add_executable(freeze_regressions tests/SessionFreezeRegressions.cpp $<TARGET_OBJECTS:felitronics_session_facade>)
target_link_libraries(freeze_regressions PRIVATE felitronics::session)
target_include_directories(freeze_regressions PRIVATE \${CMAKE_CURRENT_SOURCE_DIR})
add_executable(contract_fixture tests/SessionContractFixture.cpp)
target_link_libraries(contract_fixture PRIVATE felitronics::session)
add_executable(destroy_regression tests/SessionDestroyReentryTests.cpp $<TARGET_OBJECTS:felitronics_session_facade>)
target_link_libraries(destroy_regression PRIVATE felitronics::session felitronics::test_support)
target_include_directories(destroy_regression BEFORE PRIVATE \${CMAKE_CURRENT_SOURCE_DIR}/destroy-control \${CMAKE_CURRENT_SOURCE_DIR})
`.replaceAll('\\$', '$'));
    pass('cmake',['-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DFELITRONICS_MASTERING_BUILD_TESTS=ON',
        `-DFELITRONICS_MASTERING_FCORE_DIR=${value('FELITRONICS_MASTERING_CORE_SOURCE_DIR')}`,
        `-DFELITRONICS_MASTERING_TOML_DIR=${value('FELITRONICS_MASTERING_TOML_SOURCE_DIR')}`]);
    const compile = (...targets) => pass('cmake',['--build',build,'-j','6','--target',...targets]);
    compile('freeze_regressions','destroy_regression','contract_fixture','felitronics_session_abi_probe');
    for (const item of ['F1','F2','F5','F7','F8','F12','A','B','C']) red(item,run(join(build,'tools/freeze_regressions'),[item]).status === 1);
    red('F6',run(join(build,'tools/destroy_regression'),[]).status === 2);
    const probe = join(build,'tools/felitronics_session_abi_probe');
    const facts = pass(probe,[]);
    red('F3',!facts.includes('row ReadingPoint element=f64') && !facts.includes('"kind":"rejected"'));
    const wire = join(source,'modules/session/src/Wire.cpp'), wireText = readFileSync(wire,'utf8');
    assert(wireText.includes('w.field ("seq", e.seq)'));
    writeFileSync(wire,wireText.replace('w.field ("seq", e.seq)','w.field ("sequence", e.seq)'));
    compile('felitronics_session_abi_probe');
    red('F4',run(process.execPath,[join(source,'tools/session-abi-check.mjs'),probe]).status === 0);
    writeFileSync(wire,wireText);
    red('F9',!wireText.includes('std::endian::native == std::endian::little'));
    const header = readFileSync(join(source,'tools/fc_session_abi.h'),'utf8');
    red('F10',!header.includes('uint64 frames use C++ Pcm'));
    const smoke = readFileSync(join(source,'tools/wasm/session-check.mjs'),'utf8');
    red('F11',!smoke.includes('machineDifferences.length > 0'));
    // Old substitutions and invalid internal values reach the same fixture as current source controls.
    for (const [file,before,after] of [
        ['Text.cpp','row.find ("minus")','row.find ("missingString")'],
        ['Text.cpp','row.find ("minimumGrouping")','row.find ("missingGrouping")'],
        ['Text.cpp','pattern.find ("{n}")','pattern.find ("{missing}")'],
        ['Pump.cpp','weights.find ("loudness")','weights.find ("missingWeight")'],
        ['Session.cpp','switch (state_)','const volatile State corruptState = static_cast<State> (255); switch (corruptState)'],
        ['Devices.cpp','switch (device)','const volatile Device corruptDevice = static_cast<Device> (255); (void) device; switch (corruptDevice)'],
        ['ConfigSchema.cpp','q.refusal = p.detail <=','const volatile std::uint32_t corruptDetail = 999; q.refusal = corruptDetail <=']]) {
        const path = join(source,'modules/session/src',file), original = readFileSync(path,'utf8');
        assert(original.includes(before));writeFileSync(path,original.replaceAll(before,after));compile('contract_fixture');
        red(`A ${before}`,run(join(build,'tools/contract_fixture'),[]).status === 0);
        writeFileSync(path,original);
    }
} finally { rmSync(temporary,{recursive:true,force:true}); }
