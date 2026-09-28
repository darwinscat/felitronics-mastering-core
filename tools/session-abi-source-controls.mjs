// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Mutate an isolated source copy, regenerate, compile, execute and compare. Hosts can retain the copy.
import {cpSync, mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve, sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
// Node reports POSIX signals and Windows exception exit codes through different fields.
function trapped(result, platform = process.platform) {
    if (result.error) return false;
    if (platform === 'win32') return result.signal === null && Number.isInteger(result.status)
        && [0x80000003, 0xc000001d].includes(result.status >>> 0); // breakpoint / illegal instruction
    return result.signal === 'SIGTRAP' || result.signal === 'SIGILL';
}
if (process.argv[2] === '--self-test') {
    for (const status of [0x80000003, 0x80000003 | 0, 0xc000001d, 0xc000001d | 0])
        assert(trapped({status, signal:null}, 'win32'));
    for (const status of [0, 1, 0xc0000005, 0xc0000409, null])
        assert(!trapped({status, signal:null}, 'win32'));
    for (const signal of ['SIGTRAP', 'SIGILL']) assert(trapped({status:null, signal}, 'linux'));
    for (const signal of ['SIGSEGV', 'SIGABRT', null]) assert(!trapped({status:1, signal}, 'darwin'));
    assert(!trapped({status:0x80000003, signal:null, error:Error('spawn failed')}, 'win32'));
    console.log('source control trap detection: Windows signed/unsigned exceptions and POSIX signals; unrelated failures refused');
    process.exit(0);
}
const root = fileURLToPath(new URL('../', import.meta.url));
const cache = readFileSync(join(resolve(process.argv[2] ?? 'build'), 'CMakeCache.txt'), 'utf8');
const value = key => new RegExp(`^${key}:[^=]*=(.*)$`, 'm').exec(cache)?.[1]?.trim();
const option = name => { const i = process.argv.indexOf(name); return i < 0 ? undefined : process.argv[i + 1]; };
// Hosts can retain every artifact under their job; normal CI/local runs clean up their temporary copy.
const retain = option('--retain-work');
const temporary = mkdtempSync(join(retain ? resolve(retain) : tmpdir(), 'session-abi-source-'));
const configuration = option('--config') || value('CMAKE_BUILD_TYPE') || '';
const multi = Boolean(value('CMAKE_CONFIGURATION_TYPES'));
const buildArgs = ['--build', join(temporary, 'build'), '--config', configuration, '-j', '8', '--target'];
const source = join(temporary, 'source'), build = join(temporary, 'build');
function run(command, args) {
    const result = spawnSync(command, args, {encoding:'utf8', maxBuffer:8*1024*1024});
    assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
    return result.stdout;
}
try {
    cpSync(root, source, {recursive:true, filter:p => ! /(?:^|\/)(?:build(?:-[^/.]+)?|\.git)(?:\/|$)/.test(p.slice(root.length).split(sep).join('/'))});
    const inherited = ['CMAKE_TOOLCHAIN_FILE', 'CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER', 'CMAKE_MAKE_PROGRAM',
        'CMAKE_GENERATOR_INSTANCE', 'CMAKE_OSX_ARCHITECTURES', 'CMAKE_OSX_DEPLOYMENT_TARGET', 'CMAKE_OSX_SYSROOT',
        'CMAKE_MSVC_RUNTIME_LIBRARY', 'CMAKE_CXX_COMPILER_TARGET', 'CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN',
        'CMAKE_SYSROOT', 'CMAKE_CXX_COMPILER_LAUNCHER', 'CMAKE_INTERPROCEDURAL_OPTIMIZATION',
        'FELITRONICS_ENABLE_SANITIZERS', 'CMAKE_CXX_FLAGS', 'CMAKE_EXE_LINKER_FLAGS', 'CMAKE_STATIC_LINKER_FLAGS'];
    for (const config of ['DEBUG', 'RELEASE', 'RELWITHDEBINFO', 'MINSIZEREL'])
        for (const kind of ['CXX_FLAGS', 'EXE_LINKER_FLAGS', 'STATIC_LINKER_FLAGS']) inherited.push(`CMAKE_${kind}_${config}`);
    const configure = ['-S', source, '-B', build, '-G', value('CMAKE_GENERATOR')];
    for (const [key, flag] of [['CMAKE_GENERATOR_PLATFORM', '-A'], ['CMAKE_GENERATOR_TOOLSET', '-T']])
        if (value(key)) configure.push(flag, value(key));
    for (const key of inherited) if (value(key) !== undefined) configure.push(`-D${key}=${value(key)}`);
    run('cmake', [...configure, `-DCMAKE_BUILD_TYPE=${configuration}`, '-DFELITRONICS_MASTERING_BUILD_TESTS=ON',
        `-DFELITRONICS_MASTERING_FCORE_DIR=${value('FELITRONICS_MASTERING_CORE_SOURCE_DIR')}`,
        `-DFELITRONICS_MASTERING_TOML_DIR=${value('FELITRONICS_MASTERING_TOML_SOURCE_DIR')}`]);
    const checker = join(source,'tools/session-abi-check.mjs');
    const childCache = readFileSync(join(build, 'CMakeCache.txt'), 'utf8');
    for (const key of ['CMAKE_GENERATOR', 'CMAKE_CXX_COMPILER', 'CMAKE_TOOLCHAIN_FILE', 'CMAKE_CXX_FLAGS', 'FELITRONICS_ENABLE_SANITIZERS'])
        if (value(key) !== undefined) assert.equal(new RegExp(`^${key}:[^=]*=(.*)$`, 'm').exec(childCache)?.[1]?.trim(), value(key), `preserved ${key}`);
    assert.equal(new RegExp('^CMAKE_BUILD_TYPE:[^=]*=(.*)$', 'm').exec(childCache)?.[1]?.trim(), configuration);
    console.log(`source control toolchain preserved: ${value('CMAKE_GENERATOR')}, ${configuration || 'unconfigured'}, ${value('CMAKE_CXX_COMPILER')}`);
    const executable = name => join(build, 'tools', multi ? configuration : '', name + (process.platform === 'win32' ? '.exe' : ''));
    const probe = executable('felitronics_session_abi_probe');
    const compile = () => {
        run(process.execPath, [checker, '--generate', join(build,'tools/session-abi-probe.cpp')]);
        run('cmake', [...buildArgs, 'felitronics_session_abi_probe']);
    };
    const compare = () => spawnSync(process.execPath, [checker, probe], {encoding:'utf8', maxBuffer:2*1024*1024});
    compile(); assert.equal(compare().status, 0, 'unmodified source agrees');
    const mutations = [
        ['enum value', 'tools/fc_session_abi.h', 'FC_SESSION_ERR_TOO_SMALL = 11', 'FC_SESSION_ERR_TOO_SMALL = 42'],
        ['struct field', 'tools/fc_session_abi.h', '    uint32_t maxRateHz;\n    uint32_t offeredDevices;', '    uint32_t offeredDevices;\n    uint32_t maxRateHz;'],
        ['wire field name', 'modules/session/src/Wire.cpp', 'w.field ("seq", e.seq)', 'w.field ("sequence", e.seq)'],
        ['wire field removal', 'modules/session/src/Wire.cpp', 'w.field ("seq", e.seq)', '(void) e.seq'],
        ['wire field type', 'modules/session/src/Wire.cpp', 'w.field ("seq", e.seq)', 'w.field ("seq", double (e.seq))'],
        ['row column order', 'modules/session/src/JsonCodec.h', 'append (row.fileValue); append (row.coreValue);', 'append (row.coreValue); append (row.fileValue);'],
        ['EQ row column order', 'modules/session/src/JsonCodec.h', 'append (row.hz); append (row.db);', 'append (row.db); append (row.hz);'],
        ['device field ordinal', 'modules/session/src/Devices.h', 'v (1, knobRule (r.hpfFq), s.fq...);', 'v (0, knobRule (r.hpfFq), s.fq...);'],
        ['entry signature', 'tools/fc_session_abi.h', 'fc_session_destroy (fc_session session)', 'fc_session_destroy (uint64_t session)'],
    ];
    for (const key of ['name', 'fileRate', 'bitDepth', 'rateKnown', 'target', 'onEdits', 'on', 'jobId', 'masterId'])
        mutations.push([`parser key ${key}`, 'modules/session/src/Wire.cpp', `root.get ("${key}",`, `root.get ("changed_${key}",`]);
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
    const header = join(source,'tools/fc_session_abi.h'), originalHeader = readFileSync(header, 'utf8');
    writeFileSync(header,originalHeader.replace('FC_SESSION_DONE = 1','FC_SESSION_DONE = 1, FC_SESSION_FUTURE = 42')
        .replace('    uint32_t rowBytes;', '    uint32_t rowBytes;\n    uint32_t futureReserved;')
        .replace('uint32_t fc_session_abi_version (void);','uint32_t fc_session_abi_version (void);\nfc_session_status fc_session_future (fc_session session);'));
    compile(); const added = compare(); assert.equal(added.status,0,added.stderr);
    console.log('source control: appended struct field, enum and entry point: generation -> compilation -> comparison GREEN');
    writeFileSync(header, originalHeader);
    const codec = join(source, 'modules/session/src/JsonCodec.h'), originalCodec = readFileSync(codec, 'utf8');
    // Add a snapshot field in the actual writer, not in a fabricated output.
    const addition = 'string (name); put (\':\'); value (x);';
    assert(originalCodec.includes(addition));
    writeFileSync(codec, originalCodec.replace(addition, addition + ' if (name == \"handFieldCount\") text (",\\\"future\\\":true");'));
    compile(); const jsonAdded = compare(); assert.equal(jsonAdded.status, 0, jsonAdded.stderr);
    console.log('source control: compiled JSON field addition GREEN');
    writeFileSync(codec, originalCodec);
    // Removing overlap checks used to escape the demand-query suite entirely.
    const facade = join(source, 'tools/wasm/fc_session.cpp'), originalFacade = readFileSync(facade, 'utf8');
    run('cmake', [...buildArgs, 'felitronics_session_abi_v1_tests']);
    run(executable('felitronics_session_abi_v1_tests'), []);
    console.log('guard control: unmodified suite GREEN before planting mutations');
    for (const [entry, before, after] of [
        ...['command', 'load', 'import_project'].map(entry => [`fc_session_${entry}_bytes`,
            /if \(overlap \(out, sizeof \(\*out\), [^;]+;/, '/* overlap control removed */']),
        ['fc_session_set_capacity', 'auto* slot = lookup (session);', 'if (!capacity) return FC_SESSION_ERR_NULL;\n    auto* slot = lookup (session);']
    ]) {
        const start = originalFacade.indexOf(`FC_EXPORT fc_session_status ${entry} (`);
        const end = originalFacade.indexOf('\n}', start) + 2;
        const body = originalFacade.slice(start, end), changed = body.replace(before, after);
        assert.notEqual(changed, body, `guard anchor: ${entry}`);
        writeFileSync(facade, originalFacade.slice(0, start) + changed + originalFacade.slice(end));
        run('cmake', [...buildArgs, 'felitronics_session_abi_v1_tests']);
        const result = spawnSync(executable('felitronics_session_abi_v1_tests'), [], {encoding:'utf8'});
        assert.notEqual(result.status, 0, `${entry}: suite must reject guard mutation`);
        assert.match(result.stdout + result.stderr, /FAIL:/);
        console.log(`guard control: ${entry}: mutation RED`);
    }
    writeFileSync(facade, originalFacade);
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
    const fixture = executable('felitronics_session_contract_fixture');
    run('cmake', [...buildArgs, 'felitronics_session_contract_fixture']);
    run(fixture, []);
    for (const [name, file, before, after] of contracts) {
        const path = join(source, file), original = readFileSync(path, 'utf8');
        assert(original.includes(before), `contract anchor: ${name}`);
        writeFileSync(path, original.replaceAll(before, after));
        run('cmake', [...buildArgs, 'felitronics_session_contract_fixture']);
        const result = spawnSync(fixture, [], {encoding:'utf8'});
        assert.match(result.stdout, /contract fixture reached/);
        assert(trapped(result), `${name}: must trap, got ${result.status}/${result.signal}`);
        console.log(`contract control: ${name}: compiled corruption TRAPS`);
        writeFileSync(path, original);
    }
    // Compile-time byte-order refusal is a source control, not a synthetic byte fixture.
    const wire = join(source,'modules/session/src/Wire.cpp');
    writeFileSync(wire,readFileSync(wire,'utf8').replace('std::endian::native == std::endian::little','std::endian::native == std::endian::big'));
    const endian = spawnSync('cmake',[...buildArgs, 'felitronics_session_abi_probe'],{encoding:'utf8'});
    assert.notEqual(endian.status,0); assert.match(endian.stdout+endian.stderr,/requires little-endian f64 rows/);
    console.log('source control: unsupported byte order: compilation RED');
} finally { if (!retain) rmSync(temporary,{recursive:true,force:true}); }
