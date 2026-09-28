// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Local only: mutate source in a temporary checkout, regenerate, compile, execute, compare.
import {cpSync, mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
const root = fileURLToPath(new URL('../', import.meta.url));
const cache = readFileSync(join(resolve(process.argv[2] ?? 'build'), 'CMakeCache.txt'), 'utf8');
const value = key => new RegExp(`^${key}:[^=]*=(.*)$`, 'm').exec(cache)?.[1];
const temporary = mkdtempSync(join(tmpdir(), 'session-abi-source-'));
const source = join(temporary, 'source'), build = join(temporary, 'build');
function run(command, args) {
    const result = spawnSync(command, args, {encoding:'utf8', maxBuffer:8*1024*1024});
    assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
    return result.stdout;
}
try {
    cpSync(root, source, {recursive:true, filter:p => ! /(?:^|\/)(?:build(?:-[^/.]+)?|\.git)(?:\/|$)/.test(p.slice(root.length))});
    run('cmake', ['-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DFELITRONICS_MASTERING_BUILD_TESTS=ON',
        `-DFELITRONICS_MASTERING_FCORE_DIR=${value('FELITRONICS_MASTERING_CORE_SOURCE_DIR')}`,
        `-DFELITRONICS_MASTERING_TOML_DIR=${value('FELITRONICS_MASTERING_TOML_SOURCE_DIR')}`]);
    const checker = join(source,'tools/session-abi-check.mjs');
    const probe = join(build,'tools/felitronics_session_abi_probe');
    const compile = () => {
        run(process.execPath, [checker, '--generate', join(build,'tools/session-abi-probe.cpp')]);
        run('cmake', ['--build', build, '-j', '8', '--target','felitronics_session_abi_probe']);
    };
    const compare = () => spawnSync(process.execPath, [checker, probe], {encoding:'utf8', maxBuffer:2*1024*1024});
    compile(); assert.equal(compare().status, 0, 'unmodified source agrees');
    const mutations = [
        ['enum value', 'tools/fc_session_abi.h', 'FC_SESSION_ERR_TOO_SMALL = 11', 'FC_SESSION_ERR_TOO_SMALL = 42'],
        ['struct field', 'tools/fc_session_abi.h', '    uint32_t maxRateHz;\n    uint32_t offeredDevices;', '    uint32_t offeredDevices;\n    uint32_t maxRateHz;'],
        ['wire field name', 'modules/session/src/Wire.cpp', 'w.field ("seq", e.seq)', 'w.field ("sequence", e.seq)'],
        ['row column order', 'modules/session/src/JsonCodec.h', 'append (row.fileValue); append (row.coreValue);', 'append (row.coreValue); append (row.fileValue);'],
        ['EQ row column order', 'modules/session/src/JsonCodec.h', 'append (row.hz); append (row.db);', 'append (row.db); append (row.hz);'],
        ['entry signature', 'tools/fc_session_abi.h', 'fc_session_destroy (fc_session session)', 'fc_session_destroy (uint64_t session)'],
    ];
    for (const [name, file, before, after] of mutations) {
        const path = join(source,file), original = readFileSync(path,'utf8');
        assert(original.includes(before), `mutation anchor: ${name}`);
        writeFileSync(path,original.replace(before,after));
        compile(); const result = compare();
        assert.notEqual(result.status,0, `${name}: comparison must reject compiled mutation`);
        assert.match(result.stderr,/frozen session ABI changed or disappeared/);
        console.log(`source control: ${name}: generation -> compilation -> comparison RED`);
        writeFileSync(path,original);
    }
    const header = join(source,'tools/fc_session_abi.h');
    writeFileSync(header,readFileSync(header,'utf8').replace('FC_SESSION_DONE = 1','FC_SESSION_DONE = 1, FC_SESSION_FUTURE = 2')
        .replace('    uint32_t rowBytes;', '    uint32_t rowBytes;\n    uint32_t futureReserved;')
        .replace('uint32_t fc_session_abi_version (void);','uint32_t fc_session_abi_version (void);\nfc_session_status fc_session_future (fc_session session);'));
    compile(); const added = compare(); assert.equal(added.status,0,added.stderr);
    console.log('source control: appended struct field, enum and entry point: generation -> compilation -> comparison GREEN');
    // Corrupt a required embedded lookup after the build gate: never substitute a plausible value.
    const contracts = [
        ['missing string', 'modules/session/src/Text.cpp', 'row.find ("minus")', 'row.find ("absentRequiredString")'],
        ['missing grouping', 'modules/session/src/Text.cpp', 'row.find ("minimumGrouping")', 'row.find ("absentGrouping")'],
        ['bad number pattern', 'modules/session/src/Text.cpp', 'pattern.find ("{n}")', 'pattern.find ("{missing}")'],
        ['missing phase weight', 'modules/session/src/Pump.cpp', 'weights.find ("loudness")', 'weights.find ("missingWeight")'],
        ['missing config number', 'modules/session/src/Rules.cpp', 'hpf.find ("hzDefault")', 'hpf.find ("missingDefault")'],
        ['invalid state', 'modules/session/src/Session.cpp', 'switch (state_)', 'const volatile State corruptState = static_cast<State> (255); switch (corruptState)'],
        ['invalid device', 'modules/session/src/Devices.cpp', 'switch (device)', 'const volatile Device corruptDevice = static_cast<Device> (255); (void) device; switch (corruptDevice)'],
        ['invalid schema detail', 'modules/session/src/ConfigSchema.cpp', 'if (p.detail >', 'const volatile std::uint32_t corruptDetail = 999; if (corruptDetail >'],
    ];
    const fixture = join(build, 'tools/felitronics_session_contract_fixture');
    run('cmake', ['--build', build, '-j', '8', '--target', 'felitronics_session_contract_fixture']);
    run(fixture, []);
    for (const [name, file, before, after] of contracts) {
        const path = join(source, file), original = readFileSync(path, 'utf8');
        assert(original.includes(before), `contract anchor: ${name}`);
        writeFileSync(path, original.replaceAll(before, after));
        run('cmake', ['--build', build, '-j', '8', '--target', 'felitronics_session_contract_fixture']);
        const result = spawnSync(fixture, [], {encoding:'utf8'});
        assert.match(result.stdout, /contract fixture reached/);
        assert(result.signal === 'SIGTRAP' || result.signal === 'SIGILL', `${name}: must trap, got ${result.status}/${result.signal}`);
        console.log(`contract control: ${name}: compiled corruption TRAPS`);
        writeFileSync(path, original);
    }
    // Compile-time byte-order refusal is a source control, not a synthetic byte fixture.
    const wire = join(source,'modules/session/src/Wire.cpp');
    writeFileSync(wire,readFileSync(wire,'utf8').replace('std::endian::native == std::endian::little','std::endian::native == std::endian::big'));
    const endian = spawnSync('cmake',['--build',build,'-j','8','--target','felitronics_session_abi_probe'],{encoding:'utf8'});
    assert.notEqual(endian.status,0); assert.match(endian.stdout+endian.stderr,/requires little-endian f64 rows/);
    console.log('source control: unsupported byte order: compilation RED');
} finally { rmSync(temporary,{recursive:true,force:true}); }
