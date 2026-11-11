import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {getProfile} from '../src/generated/fm-contract.mjs';
import {getFmRuntime} from '../src/v2-protocol.mjs';
import {createFmRuntimeView} from '../src/fm-runtime-view.mjs';
const pair=()=>['capabilities','state'].map(n=>JSON.parse(readFileSync(new URL('./fixtures/fm32/'+n+'.json',import.meta.url))));
test('paired FM32 current descriptor and catalog agree on32x4 without replacing the FMC contract',()=>{
 const [caps,state]=pair(),runtime=getFmRuntime(caps,state),profile=getProfile(1);
 assert.equal(runtime.playable,true,runtime.reason);assert.equal(runtime.voiceCount,32);
 assert.deepEqual(runtime.operatorsPerVoice,Array(32).fill(4));assert.equal(profile.voice_count,32);
 assert.equal(state.fm.voice_generations.length,32);assert.equal(state.fm.patch_operator_count,4);
 assert.deepEqual(profile.presets.map(p=>p.id),[0,1,2,3,4]);
 assert.equal(caps.cpu_fm,undefined,'retired preset-only API stays absent');
});
test('paired build rejects an8voice or truncated descriptor rather than negotiating capacity',()=>{
 for(const change of [s=>{s.fm.voice_count=8;s.fm.operators_per_voice=Array(8).fill(4);s.fm.voice_generations=Array(8).fill(0);},s=>s.fm.operators_per_voice.pop(),s=>s.fm.operators_per_voice[31]=3]){
  const [caps,state]=pair();change(state);assert.equal(getFmRuntime(caps,state).playable,false);
 }
});
test('voice31 has four operator slots while the shared46value patch and preset identities are retained',()=>{
 const [caps,state]=pair(),view=createFmRuntimeView(caps,state,{writable:true,sessionIdentity:'fm32',selectedVoiceIndex:31});
 assert.equal(view.model.selectedVoiceId,'voice:31');assert.equal(view.model.voices[31].operators.length,4);
 assert.equal(view.model.parameters.length+view.model.patchOperators.flatMap(o=>o.parameters).length,46);
 assert.deepEqual(view.presets.map(p=>p.id),[0,1,2,3,4]);
});
