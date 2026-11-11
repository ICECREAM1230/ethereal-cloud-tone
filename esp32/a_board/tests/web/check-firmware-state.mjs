import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateState} from './src/protocol.mjs';

// This JSON comes from production wifi_web.c's state handler, not the mock.
const state = validateState(JSON.parse(await readFile(process.argv[2], 'utf8')));
assert.equal(state.ready, true);
assert.equal(state.fpga, 'unverified');
assert.equal(state.volume, 207);
assert.deepEqual(state.processed, [0, 0, 0, 0]);
assert.deepEqual(state.keys, [0, 0, 0, 0]);
assert.equal(state.engine.available, true);
assert.equal(state.engine.valid, true);
assert.equal(state.ks_decay_ms, 3000);
console.log('HTTP state: actual firmware JSON accepted by frozen Web schema PASS');
