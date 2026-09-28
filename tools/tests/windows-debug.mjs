// SPDX-License-Identifier: AGPL-3.0-or-later
// Twenty named suites, not merely a nonempty ctest selection. A removed registration must fail CI.
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';

const expected = ['analysis_abi', 'tempo_abi', 'fctempo_abi', 'master_abi', 'storage',
    'session', 'session_state', 'session_config', 'session_config_decisions', 'session_text',
    'session_abi', 'session_abi_v1', 'session_event', 'session_project', 'session_replay', 'session_measurement', 'session_live', 'session_source', 'session_needles', 'session_measurement_budget']
    .map(name => `felitronics_${name}_tests`).sort();
const selection = `^(${expected.join('|')})$`;
function check(tests) {
    assert.deepEqual(tests.map(test => test.name).sort(), expected, 'Windows Debug suite selection changed');
}

if (process.argv[2] === '--self-test') {
    check(expected.map(name => ({name})));
    for (const missing of expected)
        assert.throws(() => check(expected.filter(name => name !== missing).map(name => ({name}))));
    assert.throws(() => check([]));
    assert.throws(() => check([...expected, expected[0]].map(name => ({name}))));
    console.log('Windows Debug selection: all twenty missing-suite controls, empty and duplicate controls passed');
} else {
    const build = process.argv[2];
    if (!build) throw new Error('usage: node windows-debug.mjs <build-directory> | --self-test');
    const args = ['--test-dir', build, '-C', 'Debug', '-R', selection];
    const listed = spawnSync('ctest', [...args, '--show-only=json-v1'], {encoding: 'utf8'});
    if (listed.error) throw listed.error;
    if (listed.status !== 0) throw new Error(listed.stderr || listed.stdout);
    check(JSON.parse(listed.stdout).tests);
    console.log('Windows Debug selection: all twenty expected suites present');
    const run = spawnSync('ctest', [...args, '--output-on-failure', '--no-tests=error', '-j', '4'], {stdio: 'inherit'});
    if (run.error) throw run.error;
    process.exitCode = run.status ?? 1;
}
