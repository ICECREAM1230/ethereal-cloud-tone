import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {getPreset,packTarget,FM_TARGET_NONE} from '../src/generated/fm-contract.mjs';
import {createFmRuntimeView} from '../src/fm-runtime-view.mjs';
const pair=()=>['capabilities','state'].map(n=>JSON.parse(readFileSync(new URL('./fixtures/fm32/'+n+'.json',import.meta.url))));
const cache=(preset,sequence=0)=>getPreset(1,preset).values.map(v=>({parameter_id:v.parameter_id,target_word:packTarget(FM_TARGET_NONE,v.operator_index),value_u32:v.value_u32,sequence_id:sequence}));
const view=(caps,state)=>createFmRuntimeView(caps,state,{writable:true,sessionIdentity:'default-epiano'});
for(const preset of [0,1,2,3,4])test('all46 confirmed parameters identify current preset '+preset+' without guessing from boot or version',()=>{
 const [caps,state]=pair();state.fm.parameter_values=cache(preset,preset?0:7);
 assert.equal(view(caps,state).matchedPresetId,preset);
});
for(const [name,change] of Object.entries({partial:s=>s.fm.parameter_values.pop(),duplicate:s=>s.fm.parameter_values.push({...s.fm.parameter_values[0]}),custom:s=>s.fm.parameter_values.find(p=>p.parameter_id===17).value_u32=42,unknown:s=>s.fm.parameter_values.push({parameter_id:999,target_word:4294967295,value_u32:0,sequence_id:0})}))test(name+' cache never claims the boot electric piano preset',()=>{
 const [caps,state]=pair();state.fm.parameter_values=cache(3);change(state);
 assert.equal(view(caps,state).matchedPresetId,null);
});
test('unavailable or lost FM cache never restores an inferred default',()=>{
 const [caps,state]=pair();state.fm.parameter_values=[];assert.equal(view(caps,state).matchedPresetId,null);
 state.fm.available=false;assert.equal(view(caps,state).matchedPresetId,null);
});
