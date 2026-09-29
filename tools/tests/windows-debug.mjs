// SPDX-License-Identifier: AGPL-3.0-or-later
// Every named suite, not merely a nonempty ctest selection. A removed registration must fail CI.
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const expected = ['analysis_abi', 'tempo_abi', 'fctempo_abi', 'master_abi', 'storage',
    'session', 'session_state', 'session_config', 'session_config_decisions', 'session_text',
    'session_abi', 'session_abi_v1', 'session_event', 'session_project', 'session_replay', 'session_measurement', 'session_live', 'session_source', 'session_needles', 'session_measurement_budget', 'session_master_job', 'session_wav', 'session_master_abi', 'session_master_report', 'session_memory_gate', 'session_cost', 'session_sidecar', 'session_query', 'waveform_index']
    .map(name => `felitronics_${name}_tests`).sort();
const selection = `^(${expected.join('|')})$`;
function check(tests) {
    assert.deepEqual(tests.map(test => test.name).sort(), expected, 'Windows Debug suite selection changed');
}
function checkBuildTargets(workflow) {
    const job = workflow.replace(/\r\n/g, '\n').split('  windows-debug:\n')[1]?.split('\n  # Against core')[0];
    const build = job?.split('      - name: Build allocation and session suites\n')[1]?.split('      - name:')[0];
    assert.ok(build, 'Windows Debug build step missing');
    const targets = new Set(build.match(/felitronics_[a-z0-9_]+_tests/g) ?? []);
    for (const name of expected) assert.ok(targets.has(name), `Windows Debug does not build ${name}`);
}

if (process.argv[2] === '--self-test') {
    const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
    checkBuildTargets(workflow);
    checkBuildTargets(workflow.replace(/\r?\n/g, '\r\n'));
    assert.throws(() => checkBuildTargets(workflow.replace('felitronics_session_sidecar_tests', '')));
    check(expected.map(name => ({name})));
    for (const missing of expected)
        assert.throws(() => check(expected.filter(name => name !== missing).map(name => ({name}))));
    assert.throws(() => check([]));
    assert.throws(() => check([...expected, expected[0]].map(name => ({name}))));
    console.log(`Windows Debug selection: all ${expected.length} missing-suite controls, empty and duplicate controls passed`);
} else {
    const build = process.argv[2];
    if (!build) throw new Error('usage: node windows-debug.mjs <build-directory> | --self-test');
    const args = ['--test-dir', build, '-C', 'Debug', '-R', selection];
    const listed = spawnSync('ctest', [...args, '--show-only=json-v1'], {encoding: 'utf8'});
    if (listed.error) throw listed.error;
    if (listed.status !== 0) throw new Error(listed.stderr || listed.stdout);
    check(JSON.parse(listed.stdout).tests);
    console.log(`Windows Debug selection: all ${expected.length} expected suites present`);
    const run = spawnSync('ctest', [...args, '--output-on-failure', '--no-tests=error', '-j', '4'], {stdio: 'inherit'});
    if (run.error) throw run.error;
    process.exitCode = run.status ?? 1;
}
