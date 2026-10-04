// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Mutate an isolated source copy, regenerate, compile, execute and compare. Hosts can retain the copy.
import {copyFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, statSync, utimesSync, writeFileSync, rmSync} from 'node:fs';
import {basename, join, resolve, sep} from 'node:path';
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
// A Windows checkout may carry CRLF line endings; a multi-line anchor is written with \n and follows the file's.
const inCheckoutEol = (text, s) => text.includes('\r\n') ? s.replaceAll('\r\n', '\n').replaceAll('\n', '\r\n') : s;
const parentBuild = resolve(process.argv[2] ?? 'build');
const cache = readFileSync(join(parentBuild, 'CMakeCache.txt'), 'utf8');
const value = key => new RegExp(`^${key}:[^=]*=(.*)$`, 'm').exec(cache)?.[1]?.trim();
const option = name => { const i = process.argv.indexOf(name); return i < 0 ? undefined : process.argv[i + 1]; };
// Hosts can retain every artifact under their job; normal CI/local runs clean up their temporary copy.
// THE COPY LIVES IN THE BUILD TREE, NEVER UNDER THE SYSTEM'S TEMPORARY DIRECTORY: MSBuild's file tracking does not record
// what a compile reads there (warning MSB8029), so a mutated header left the library's objects stale and the probe linked
// the old code — the JSON addition below failed on windows-latest for exactly that. Every build refuses MSB8029.
const retain = option('--retain-work');
const temporary = mkdtempSync(join(retain ? resolve(retain) : parentBuild, 'session-abi-source-'));
const configuration = option('--config') || value('CMAKE_BUILD_TYPE') || '';
const multi = Boolean(value('CMAKE_CONFIGURATION_TYPES'));
const buildArgs = ['--build', join(temporary, 'build'), '--config', configuration, '-j', '8', '--target'];
// MSBuild compiles the sources of one project one after another unless told otherwise; the library is one project of
// some thirty sources, rebuilt after every mutation. MultiToolTask runs them in parallel without touching a compile line
// (measured on MSVC 14.44, 16 threads: the whole run 377 s -> 250 s, its first build 106 s -> 38 s).
const nativeArgs = (value('CMAKE_GENERATOR') ?? '').startsWith('Visual Studio')
    ? ['--', '/p:UseMultiToolTask=true', '/p:EnforceProcessCountAcrossBuilds=true'] : [];
const source = join(temporary, 'source'), build = join(temporary, 'build');
let builtAt = 0;
const wait = new Int32Array(new SharedArrayBuffer(4));
function stampInput(path) {
    // Make 3.81 compares whole seconds; MSBuild also tracks input/output timestamps.
    // Every mutation, restoration and generated probe must be newer than the previous
    // build, even on a coarse clock. Wait for real time instead of dating inputs in the future.
    const nextSecond = (Math.floor(builtAt / 1000) + 1) * 1000;
    while (Date.now() < nextSecond) Atomics.wait(wait, 0, 0, nextSecond - Date.now());
    const now = new Date();
    utimesSync(path, now, now);
    assert(Math.floor(statSync(path).mtimeMs / 1000) > Math.floor(builtAt / 1000), `fresh input: ${path}`);
}
function writeSource(path, text) {
    writeFileSync(path, text);
    stampInput(path);
}
function run(command, args) {
    const result = spawnSync(command, args, {encoding:'utf8', maxBuffer:8*1024*1024});
    assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
    assert(!/\bMSB8029\b/.test(result.stdout + result.stderr),
        `${command} ${args.join(' ')}: MSBuild does not track header reads under the temporary directory (MSB8029) — a mutated header would leave stale objects`);
    return result.stdout;
}
try {
    // The tree without its build directories and version control, and without the copy itself: the parent build — and
    // with it this copy — may sit inside the source tree (CI's `-B build`), which a plain recursive copy cannot skip.
    const skipped = p => p === parentBuild || p === temporary
        || /(?:^|\/)(?:build(?:-[^/.]+)?|\.git)(?:\/|$)/.test(p.slice(root.length).split(sep).join('/'));
    const copyTree = (from, to) => {
        mkdirSync(to, {recursive:true});
        for (const entry of readdirSync(from, {withFileTypes:true})) {
            const path = join(from, entry.name);
            if (skipped(path)) continue;
            if (entry.isDirectory()) copyTree(path, join(to, entry.name));
            else if (entry.isFile()) copyFileSync(path, join(to, entry.name));
            else throw Error(`source control copy: ${path} is neither a file nor a directory`);
        }
    };
    copyTree(resolve(root), source);
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
    for (const key of ['CMAKE_GENERATOR', 'CMAKE_CXX_COMPILER', 'CMAKE_TOOLCHAIN_FILE', 'CMAKE_CXX_FLAGS', 'FELITRONICS_ENABLE_SANITIZERS']) {
        if (value(key) === undefined) continue;
        const child = new RegExp(`^${key}:[^=]*=(.*)$`, 'm').exec(childCache)?.[1]?.trim();
        const requested = value(key);
        // CMake resolves an unqualified compiler name to its executable path.
        if (key === 'CMAKE_CXX_COMPILER' && ! /[/\\]/.test(requested))
            assert.equal(basename(child ?? ''), requested, `preserved ${key}`);
        else
            assert.equal(child, requested, `preserved ${key}`);
    }
    assert.equal(new RegExp('^CMAKE_BUILD_TYPE:[^=]*=(.*)$', 'm').exec(childCache)?.[1]?.trim(), configuration);
    console.log(`source control toolchain preserved: ${value('CMAKE_GENERATOR')}, ${configuration || 'unconfigured'}, ${value('CMAKE_CXX_COMPILER')}`);
    const executable = name => join(build, 'tools', multi ? configuration : '', name + (process.platform === 'win32' ? '.exe' : ''));
    const buildTarget = name => {
        const path = executable(name), previous = existsSync(path) ? statSync(path).mtimeMs : 0;
        run('cmake', [...buildArgs, name, ...nativeArgs]);
        builtAt = Date.now();
        assert(statSync(path).mtimeMs > previous, `${name}: build must update the selected executable`);
    };
    const probe = executable('felitronics_session_abi_probe');
    const compile = () => {
        const generated = join(build,'tools/session-abi-probe.cpp');
        run(process.execPath, [checker, '--generate', generated]);
        stampInput(generated);
        buildTarget('felitronics_session_abi_probe');
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
    for (const key of ['name', 'fileRate', 'bitDepth', 'rateKnown', 'target', 'on', 'jobId', 'masterId'])
        mutations.push([`parser key ${key}`, 'modules/session/src/Wire.cpp', `root.get ("${key}",`, `root.get ("changed_${key}",`]);
    for (const [name, file, rawBefore, rawAfter] of mutations) {
        const path = join(source,file), original = readFileSync(path,'utf8');
        const before = inCheckoutEol(original, rawBefore), after = inCheckoutEol(original, rawAfter);
        assert(original.includes(before), `mutation anchor: ${name}`);
        writeSource(path,original.replace(before,after));
        compile(); const result = compare();
        assert.notEqual(result.status,0, `${name}: comparison must reject compiled mutation`);
        assert.match(result.stderr,/frozen session ABI changed or disappeared/);
        console.log(`source control: ${name}: generation -> compilation -> comparison RED`);
        writeSource(path,original);
    }
    const header = join(source,'tools/fc_session_abi.h'), originalHeader = readFileSync(header, 'utf8');
    writeSource(header,originalHeader.replace('FC_SESSION_DONE = 1','FC_SESSION_DONE = 1, FC_SESSION_FUTURE = 42')
        .replace('    uint32_t rowBytes;', '    uint32_t rowBytes;\n    uint32_t futureReserved;')
        .replace('uint32_t fc_session_abi_version (void);','uint32_t fc_session_abi_version (void);\nfc_session_status fc_session_future (fc_session session);'));
    compile(); const added = compare(); assert.equal(added.status,0,added.stderr);
    const addedFacts = run(probe, []);
    for (const fact of ['enum FC_SESSION_FUTURE=42', 'field fc_session_sizes.futureReserved uint32_t',
        'offset fc_session_sizes.futureReserved=12', 'sizeof fc_session_sizes=16',
        'function fc_session_future fc_session_status(fc_session)'])
        assert(addedFacts.split(/\r?\n/).includes(fact), `compiled addition: ${fact}`);
    console.log('source control: appended struct field, enum and entry point: generation -> compilation -> comparison GREEN');
    writeSource(header, originalHeader);
    const codec = join(source, 'modules/session/src/JsonCodec.h'), originalCodec = readFileSync(codec, 'utf8');
    // Add a snapshot field in the actual writer, not in a fabricated output.
    const addition = 'string (name); put (\':\'); value (x);';
    assert(originalCodec.includes(addition));
    writeSource(codec, originalCodec.replace(addition, addition + ' if (name == \"handFieldCount\") text (",\\\"future\\\":true");'));
    compile(); const jsonAdded = compare(); assert.equal(jsonAdded.status, 0, jsonAdded.stderr);
    assert.match(run(probe, []), /"future":true/, 'compiled JSON addition');
    console.log('source control: compiled JSON field addition GREEN');
    writeSource(codec, originalCodec);
    // Removing overlap checks used to escape the demand-query suite entirely.
    const facade = join(source, 'tools/wasm/fc_session.cpp'), originalFacade = readFileSync(facade, 'utf8');
    buildTarget('felitronics_session_abi_v1_tests');
    run(executable('felitronics_session_abi_v1_tests'), []);
    console.log('guard control: unmodified suite GREEN before planting mutations');
    for (const [entry, before, after] of [
        ...['command', 'load', 'import_project'].map(entry => [`fc_session_${entry}_bytes`,
            /if \(overlap \(out, out->size, [^;]+;/, '/* overlap control removed */']),
        ['fc_session_set_capacity', 'auto* slot = lookup (session);', 'if (!capacity) return FC_SESSION_ERR_NULL;\n    auto* slot = lookup (session);'],
        // An overlap measured by this build's record instead of the caller's: a 32-byte record beside its input refused.
        ...['command', 'load_measured'].map(entry => [`fc_session_${entry}_bytes`, /out, out->size/, 'out, sizeof (*out)'])
    ]) {
        const start = originalFacade.indexOf(`FC_EXPORT fc_session_status ${entry} (`);
        const end = originalFacade.indexOf('\n}', start) + 2;
        const body = originalFacade.slice(start, end), changed = body.replace(before, after);
        assert.notEqual(changed, body, `guard anchor: ${entry}`);
        writeSource(facade, originalFacade.slice(0, start) + changed + originalFacade.slice(end));
        buildTarget('felitronics_session_abi_v1_tests');
        const result = spawnSync(executable('felitronics_session_abi_v1_tests'), [], {encoding:'utf8'});
        assert.notEqual(result.status, 0, `${entry}: suite must reject guard mutation`);
        assert.match(result.stdout + result.stderr, /FAIL:/);
        console.log(`guard control: ${entry}: mutation RED`);
    }
    writeSource(facade, originalFacade);
    // Corrupt a required embedded lookup after the build gate: never substitute a plausible value.
    const contracts = [
        ['missing string', 'modules/session/src/Text.cpp', 'row.find ("minus")', 'row.find ("absentRequiredString")'],
        ['missing grouping', 'modules/session/src/Text.cpp', 'row.find ("minimumGrouping")', 'row.find ("absentGrouping")'],
        ['bad number pattern', 'modules/session/src/Text.cpp', 'pattern.find ("{n}")', 'pattern.find ("{missing}")'],
        ['missing phase weight', 'modules/session/src/Pump.cpp', 'master.find ("passWeight")', 'master.find ("missingWeight")'],
        ['missing config number', 'modules/session/src/Rules.cpp', 'hpf.find ("hzStep")', 'hpf.find ("missingStep")'],
        ['invalid state', 'modules/session/src/Session.cpp', 'switch (state_)', 'const volatile State corruptState = static_cast<State> (255); switch (corruptState)'],
        ['invalid device', 'modules/session/src/Devices.cpp', 'switch (device)', 'const volatile Device corruptDevice = static_cast<Device> (255); (void) device; switch (corruptDevice)'],
        ['invalid schema detail', 'modules/session/src/ConfigSchema.cpp', 'if (p.detail >', 'const volatile std::uint32_t corruptDetail = 999; if (corruptDetail >'],
    ];
    const fixture = executable('felitronics_session_contract_fixture');
    buildTarget('felitronics_session_contract_fixture');
    run(fixture, []);
    for (const [name, file, rawBefore, rawAfter] of contracts) {
        const path = join(source, file), original = readFileSync(path, 'utf8');
        const before = inCheckoutEol(original, rawBefore), after = inCheckoutEol(original, rawAfter);
        assert(original.includes(before), `contract anchor: ${name}`);
        writeSource(path, original.replaceAll(before, after));
        buildTarget('felitronics_session_contract_fixture');
        const result = spawnSync(fixture, [], {encoding:'utf8'});
        assert.match(result.stdout, /contract fixture reached/);
        assert(trapped(result), `${name}: must trap, got ${result.status}/${result.signal}`);
        console.log(`contract control: ${name}: compiled corruption TRAPS`);
        writeSource(path, original);
    }
    // Compile-time byte-order refusal is a source control, not a synthetic byte fixture.
    const wire = join(source,'modules/session/src/Wire.cpp');
    writeSource(wire,readFileSync(wire,'utf8').replace('std::endian::native == std::endian::little','std::endian::native == std::endian::big'));
    const endian = spawnSync('cmake',[...buildArgs, 'felitronics_session_abi_probe', ...nativeArgs],{encoding:'utf8'});
    assert.notEqual(endian.status,0); assert.match(endian.stdout+endian.stderr,/requires little-endian f64 rows/);
    console.log('source control: unsupported byte order: compilation RED');
} finally { if (!retain) rmSync(temporary,{recursive:true,force:true}); }
