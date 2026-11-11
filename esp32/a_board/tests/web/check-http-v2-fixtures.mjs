import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {
  FM_HTTP_API_MAJOR, FM_HTTP_API_MINOR, FM_SCHEMA_SHA256, FM_PROFILES_SHA256,
  FM_CONTRACT_SHA256, FM_ENGINE_FM, FM_PHASE_UNAVAILABLE,
  getProfile, getParameter, getPreset,
} from '../../contract/fm/generated/js/fm-contract.mjs';
import {validateState} from './src/protocol.mjs';

const folder = process.argv[2];
const read = async name => JSON.parse(await readFile(`${folder}/${name}.json`, 'utf8'));
const [caps, state, first, second, profile, presets] = await Promise.all(
  ['v2-capabilities', 'v2-state', 'v2-catalog', 'v2-catalog-page-8', 'v2-profile', 'v2-presets'].map(read));
assert.equal(caps.api_major, FM_HTTP_API_MAJOR);
assert.equal(caps.api_minor, FM_HTTP_API_MINOR);
assert.equal(caps.schema_sha256, FM_SCHEMA_SHA256);
assert.equal(caps.profiles_sha256, FM_PROFILES_SHA256);
assert.equal(caps.contract_sha256, FM_CONTRACT_SHA256);
assert.equal(caps.engines.find(e => e.id === FM_ENGINE_FM).available, false);
assert.equal(state.context_epoch, caps.context_epoch);
assert.equal(state.clients.length, 4);
assert.equal(state.fm.phase, FM_PHASE_UNAVAILABLE);
assert.equal(state.fm.available, false);
assert.equal(state.fm.accepted, null); // Unobserved is not a synthetic FPGA ACK.
assert.equal(state.fm.completed, null);
validateState(state.legacy);
const generated = getProfile(profile.profile_id);
for (const key of ['backend_id', 'profile_version', 'voice_count', 'patch_operator_count', 'parameter_count', 'preset_count'])
  assert.equal(profile[key], generated[key]);
assert.deepEqual(profile.operators_per_voice, generated.operators_per_voice);
assert.deepEqual(profile.operator_labels, generated.operator_labels);
assert.equal(first.profiles_sha256, FM_PROFILES_SHA256);
assert.equal(first.next_offset, 8);
assert.equal(second.next_offset, null);
assert.equal(first.items.length + second.items.length, generated.parameter_count);
for (const parameter of [...first.items, ...second.items]) {
  const expected = getParameter(profile.profile_id, parameter.id);
  assert(expected);
  for (const [key, value] of Object.entries(parameter)) assert.equal(value, expected[key], `${parameter.id}.${key}`);
}
for (const preset of presets.items) {
  const expected = getPreset(profile.profile_id, preset.id);
  assert.equal(preset.name, expected.name);
  assert.equal(preset.key_on_logical_mask, expected.key_on_logical_mask);
  assert.equal(preset.values_count, expected.values.length);
}
console.log('HTTP v2 JSON: actual C handler fixtures match shared generated JS identities, 13 parameters, presets, layout and unavailable state PASS');
const [liveCaps, live] = await Promise.all(['v2-live-capabilities', 'v2-live-state'].map(read));
assert.equal(liveCaps.schema_sha256, FM_SCHEMA_SHA256);
assert.equal(liveCaps.engines.find(e=>e.id===FM_ENGINE_FM).available,true);
assert.equal(live.mode.requested,FM_ENGINE_FM);
assert.equal(live.mode.applied,FM_ENGINE_FM);
assert.equal(live.fm.profile_version,getProfile(live.fm.profile_id).profile_version);
assert.deepEqual(live.fm.operators_per_voice,[4,4,4,4,4,4,4,4]);
assert.deepEqual(live.fm.accepted,{session_id:42,engine_epoch:4,sequence_id:7});
assert.deepEqual(live.fm.completed,{session_id:42,engine_epoch:4,sequence_id:6});
assert.deepEqual(live.fm.result,{session_id:42,engine_epoch:4,sequence_id:6,code:0});
assert.deepEqual(live.fm.parameter_values,[{parameter_id:18,target_word:4294901760,value_u32:31,sequence_id:6}]);
assert.equal(live.legacy.ready,false);
validateState(live.legacy);
console.log('HTTP v2 observed-FM serialization: full-width target, separate original accepted/completed identities, confirmed cache, v1 readiness guard PASS (host input only)');
