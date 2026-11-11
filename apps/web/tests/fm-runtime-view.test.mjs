import test from 'node:test';
import assert from 'node:assert/strict';
import {currentCatalogFixture as fixture} from '../mock/fixture-catalog.mjs';
import { createFmRuntimeView, resolveFmParameterIntent } from '../src/fm-runtime-view.mjs';
const pair = () => [fixture('v2-live-capabilities'), fixture('v2-live-state')];
const writable = {writable:true,sessionIdentity:'client:0/token:15/fence:1'};
const edit = view => ({contextId:view.contextId,target:{backendId:'1',voiceId:null,operatorId:'patch-operator:0'},parameterId:'18',value:30});
const attack = view => view.model.patchOperators[0].parameters.find(p=>p.id==='18');
const allParameters = view => [...view.model.parameters,...view.model.patchOperators.flatMap(o=>o.parameters),...view.model.voices.flatMap(v=>[...v.parameters,...v.operators.flatMap(o=>o.parameters)])];

test('runtime view exports its two independent pure boundaries',()=>{
  assert.equal(typeof createFmRuntimeView,'function');
  assert.equal(typeof resolveFmParameterIntent,'function');
});

test('real handler layout uses actual paired 32 by 4 voices and four shared operators with one read cache',()=>{
  const [caps,state]=pair(); const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view?.model,'matching live runtime must create a panel');
  assert.equal(view.profileId,1); assert.equal(view.backendId,1); assert.equal(view.model.backend.id,'1');
  assert.equal(view.model.voices.length,32); assert.equal(view.model.patchOperators.length,4);
  assert.equal(view.model.selectedVoiceId,'voice:0');
  for (let index=0;index<32;index++) {
    const voice=view.model.voices[index];assert.equal(voice.id,'voice:'+index);
    assert.deepEqual(voice.operators.map(o=>o.id),['operator:0','operator:1','operator:2','operator:3']);
    assert.deepEqual(voice.parameters,[]);assert.equal(voice.algorithm,undefined);
    for(const operator of voice.operators)assert.deepEqual(operator.parameters,[]);
  }
  assert.deepEqual(view.model.parameters.map(p=>p.id),['1','2']);
  assert.equal(allParameters(view).length,46);
  const known=attack(view);assert.equal(known.value,31);assert.equal(known.editable,true);
  assert.deepEqual([known.kind,known.min,known.max,known.step,known.unit],['number',0,31,1,'芯片代码']);
  assert.match(known.hint,/next_note/);assert.match(known.hint,/下次起音/);assert.match(known.hint,/完成缓存/);
  for(const parameter of allParameters(view).filter(p=>p!==known)) {
    assert.equal(parameter.value,null);assert.equal(parameter.editable,false);assert.ok(parameter.reason);
  }
});
test('preset metadata exposes no values and allows only explicit current-profile action',()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view?.model);assert.equal(view.presetEditable,true);
  assert.deepEqual(view.presets.map(p=>p.id),[0,1,2,3,4]);
  for(const preset of view.presets)assert.deepEqual(Object.keys(preset).sort(),['description','id','name']);
});
test('AE350 FMC1 bridge permits shared patch editing without direct per-voice command features',()=>{
  const [caps,state]=pair();
  state.fm.features=16|32|256|512;
  caps.cpu_mode={version:1,route_only:true,modes:[0,1,2],channel_routes:true};
  state.cpu_mode={version:1,route_only:true,valid:true,ready:true,busy:false,fault:false,pending:false,
    sequence:3,desired:2,applied:2,channel_routes:true};
  state.fm.source_rate=state.fm.output_rate={numerator:91015625,denominator:948};
  const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view?.model);assert.equal(view.model.editable,true);
  assert.equal(attack(view).editable,true);assert.equal(view.presetEditable,true);
  assert.deepEqual(view.presets.map(p=>p.id),[0,1,2,3,4]);
  assert.deepEqual(resolveFmParameterIntent(caps,state,view,edit(view)),{parameterId:18,targetWord:4294901760,valueU32:30});
  for(const mutate of [(c,s)=>s.cpu_mode.fault=true,(c,s)=>delete c.cpu_mode,(c,s)=>s.cpu_mode.valid=false]) {
    const c=structuredClone(caps),s=structuredClone(state);mutate(c,s);
    assert.equal(createFmRuntimeView(c,s,writable).model,null);
  }
});
test('latest confirmed read parameter resolves to three HTTP integers without sequence or native mapping',()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view?.model);assert.deepEqual(resolveFmParameterIntent(caps,state,view,edit(view)),{parameterId:18,targetWord:4294901760,valueU32:30});
  for(const value of [0,31])assert.equal(resolveFmParameterIntent(caps,state,view,{...edit(view),value})?.valueU32,value);
});
test('shared PATCH intent uses NONE for both target fields and no per-voice algorithm',()=>{
  const [caps,state]=pair();state.fm.parameter_values.push({parameter_id:1,target_word:4294967295,value_u32:7,sequence_id:6});
  const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);
  const event={contextId:view.contextId,target:{backendId:'1',voiceId:null,operatorId:null},parameterId:'1',value:4};
  assert.deepEqual(resolveFmParameterIntent(caps,state,view,event),{parameterId:1,targetWord:4294967295,valueU32:4});
});
for(const [name,mutate] of Object.entries({
  'no FM':(c,s)=>s.fm.available=false,'bad schema':(c,s)=>c.schema_sha256='bad',
  'bad profile SHA':(c,s)=>s.fm.profile_sha256='bad','bad contract SHA':(c,s)=>c.contract_sha256='bad',
  'bad voice dimensions':(c,s)=>s.fm.voice_count=16,'bad operator dimensions':(c,s)=>s.fm.operators_per_voice[0]=5,
  'bad patch dimensions':(c,s)=>s.fm.patch_operator_count=5,'RESET phase':(c,s)=>s.fm.phase=0,
  'native reset':(c,s)=>s.fm.session_id=0,'no SELECTED':(c,s)=>s.fm.flags=1,
  'no READY':(c,s)=>s.fm.flags=2,'FAULT':(c,s)=>s.fm.flags=19
}))test('no guessed layout or writes for '+name,()=>{
  const [caps,state]=pair();mutate(caps,state);const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view);assert.equal(view.presetEditable,false);
  assert.equal(view.model?.editable??false,false);
  if(!view.runtime.profile) {assert.equal(view.model,null);assert.equal(view.contextId,null);}
  assert.ok(view.reason);assert.equal(resolveFmParameterIntent(caps,state,view,{contextId:'forged'}),null);
});
test('MUTED retains the matched layout read-only and rejects both current and old parameter callbacks',()=>{
  const [caps,state]=pair();const before=createFmRuntimeView(caps,state,writable);const oldEvent=edit(before);
  state.fm.flags=7;const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view.model);assert.equal(view.model.voices.length,32);assert.equal(view.model.patchOperators.length,4);
  assert.equal(view.model.editable,false);assert.equal(view.runtime.selectable,true);assert.equal(view.runtime.playable,false);
  assert.equal(view.runtime.editable,false);assert.equal(view.presetEditable,false);assert.equal(view.reason,'engine_muted');
  assert.equal(view.contextId,before.contextId);assert.equal(attack(view).value,31);assert.equal(attack(view).editable,false);
  assert.equal(resolveFmParameterIntent(caps,state,before,oldEvent),null);
  assert.equal(resolveFmParameterIntent(caps,state,view,edit(view)),null);
});
test('musical BUSY retains performance and matched layout while closing parameter and preset edits',()=>{
  const [caps,state]=pair();const before=createFmRuntimeView(caps,state,writable);
  state.fm.flags=11;const view=createFmRuntimeView(caps,state,writable);
  assert.ok(view?.model);assert.equal(view.model.waiting,true);assert.equal(view.model.editable,false);
  assert.equal(view.runtime.playable,true);assert.equal(view.runtime.editable,false);assert.equal(view.presetEditable,false);
  assert.equal(view.contextId,before.contextId);assert.equal(attack(view).editable,false);
  assert.equal(resolveFmParameterIntent(caps,state,before,edit(before)),null);
  for(const mutate of [s=>s.fm.flags=15,s=>s.fm.flags=27,s=>s.fm.flags=9,s=>s.fm.session_id=0,s=>s.fm.patch_operator_count=3,s=>s.mode.recovering=true,s=>s.fm.phase=3]) {
    const broken=structuredClone(state);mutate(broken);assert.equal(createFmRuntimeView(caps,broken,writable).model,null);
  }
  const bad=structuredClone(caps);bad.profiles_sha256='bad';assert.equal(createFmRuntimeView(bad,state,writable).model,null);
});
for(const [name,options] of [['missing options',undefined],['read only',{writable:false}],['nonboolean writable',{writable:1}]])test(name+' cannot grant editing',()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,options);
  assert.ok(view?.model);assert.equal(view.model.editable,false);assert.equal(attack(view).editable,false);assert.equal(view.presetEditable,false);
  assert.equal(resolveFmParameterIntent(caps,state,view,edit(view)),null);
});
test('SET_PARAMETER, NEXT_NOTE and APPLY_PRESET gates are independent',()=>{
  for(const [features,parameter,preset] of [[1007,false,true],[767,false,true],[991,true,false]]) {
    const [caps,state]=pair();state.fm.features=features;const view=createFmRuntimeView(caps,state,writable);
    assert.ok(view?.model);assert.equal(attack(view).editable,parameter);assert.equal(view.presetEditable,preset);
    assert.equal(resolveFmParameterIntent(caps,state,view,edit(view))!==null,parameter);
  }
});
test('context preserves compatible refreshes and ordinary per-voice NoteOn generations for shared patches',()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);
  const updated=structuredClone(state);updated.fm.voice_generations[0]=10;updated.fm.voice_generations[7]=11;
  updated.fm.accepted.sequence_id=11;updated.fm.completed.sequence_id=10;updated.fm.result.sequence_id=10;
  updated.fm.parameter_values[0].value_u32=29;updated.fm.parameter_values[0].sequence_id=10;
  const refreshed=createFmRuntimeView(caps,updated,writable);assert.equal(refreshed.contextId,view.contextId);
  assert.deepEqual(resolveFmParameterIntent(caps,updated,view,edit(view)),{parameterId:18,targetWord:4294901760,valueU32:30});
});
for(const [name,mutate] of Object.entries({
  'HTTP epoch':(c,s)=>{c.context_epoch=11;s.context_epoch=11;},'native session':(c,s)=>s.fm.session_id=43,
  'native epoch':(c,s)=>s.fm.engine_epoch=5,'topology':(c,s)=>s.fm.topology_generation=2,
  'backend':(c,s)=>s.fm.backend_id=2,'profile':(c,s)=>s.fm.profile_id=2,
  'parameter feature':(c,s)=>s.fm.features=1007,'next note feature':(c,s)=>s.fm.features=767,
  'unknown current value':(c,s)=>s.fm.parameter_values=[],'mode switch':(c,s)=>s.mode.switching=true
}))test('resolver rejects old callbacks after '+name,()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);const event=edit(view);
  mutate(caps,state);assert.equal(resolveFmParameterIntent(caps,state,view,event),null);
});
test('local token fence, selected voice and session identities reject earlier context callbacks',()=>{
  const [caps,state]=pair();const first=createFmRuntimeView(caps,state,writable);assert.ok(first?.model);
  for(const options of [{...writable,sessionIdentity:'client:0/token:16/fence:2'},{...writable,selectedVoiceIndex:1}]) {
    const next=createFmRuntimeView(caps,state,options);assert.notEqual(next.contextId,first.contextId);
    assert.equal(resolveFmParameterIntent(caps,state,next,edit(first)),null);
  }
});
test('invalid selected voice falls back safely and never creates an out-of-bounds target',()=>{
  const [caps,state]=pair();
  for(const selectedVoiceIndex of [-1,32,Infinity,0.5,'7',null]) {
    const view=createFmRuntimeView(caps,state,{...writable,selectedVoiceIndex});assert.ok(view?.model);
    assert.equal(view.model.selectedVoiceId,'voice:0');assert.ok(view.reason);assert.equal(view.model.voices.length,32);
  }
});
test('resolver rejects malformed logical IDs, wrong scopes, unread values, and non-u32/range values',()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);
  const event=edit(view);
  for(const parameterId of ['018','18suffix','+18','-18',18,'999','1',null])assert.equal(resolveFmParameterIntent(caps,state,view,{...event,parameterId}),null);
  for(const operatorId of ['operator:0','patch-operator:00','patch-operator:+0','patch-operator:-0','patch-operator:0suffix','patch-operator:4',null,0])assert.equal(resolveFmParameterIntent(caps,state,view,{...event,target:{...event.target,operatorId}}),null);
  for(const voiceId of ['voice:0','voice:8','voice:00',undefined])assert.equal(resolveFmParameterIntent(caps,state,view,{...event,target:{...event.target,voiceId}}),null);
  for(const backendId of ['01','1suffix','+1',1,'2',null])assert.equal(resolveFmParameterIntent(caps,state,view,{...event,target:{...event.target,backendId}}),null);
  for(const value of [-1,32,0.5,4294967296,'30',NaN,Infinity,null,true])assert.equal(resolveFmParameterIntent(caps,state,view,{...event,value}),null);
  for(const malformed of [null,{},[],{...event,contextId:null},{...event,target:null}])assert.equal(resolveFmParameterIntent(caps,state,view,malformed),null);
});
test('cache duplicates poison the affected value regardless of order without discarding other legal values',()=>{
  for(const duplicate of [{value_u32:30},{value_u32:31},{sequence_id:-1}]) {
    for(const reverse of [false,true]) {
      const [caps,state]=pair();const second={...state.fm.parameter_values[0],...duplicate};
      state.fm.parameter_values.push(second,{parameter_id:2,target_word:4294967295,value_u32:3,sequence_id:6});
      if(reverse)state.fm.parameter_values.reverse();
      const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);
      assert.equal(attack(view).value,null);assert.equal(attack(view).editable,false);assert.ok(attack(view).reason);
      assert.equal(view.model.parameters.find(p=>p.id==='2').value,3);assert.equal(view.model.parameters.find(p=>p.id==='2').editable,true);
      assert.equal(resolveFmParameterIntent(caps,state,view,edit(view)),null);
    }
  }
});
for(const [name,patch] of Object.entries({
  'wrong scope target':{target_word:0},'patch operator boundary':{target_word:4294901764},
  'unknown ID':{parameter_id:999},'wrong target type':{target_word:'4294901760'},
  'invalid value type':{value_u32:'31'},'invalid range':{value_u32:32},'invalid uint32':{value_u32:-1},
  'invalid sequence':{sequence_id:-1},'invalid sequence type':{sequence_id:'6'},
  'extra field':{pending:true},'missing field':{sequence_id:undefined}
}))test('cache '+name+' never exposes a read/write value',()=>{
  const [caps,state]=pair();Object.assign(state.fm.parameter_values[0],patch);
  state.fm.parameter_values.push({parameter_id:2,target_word:4294967295,value_u32:3,sequence_id:6});
  const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);
  assert.equal(attack(view).value,null);assert.equal(attack(view).editable,false);assert.ok(view.reason);
  assert.equal(view.model.parameters.find(p=>p.id==='2').value,3);
});
test('sparse and non-object cache entries never become read values or erase valid independent cache',()=>{
  const [caps,state]=pair();state.fm.parameter_values.push(null,42,{},'bad');state.fm.parameter_values.length++;
  const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);assert.equal(attack(view).value,31);assert.ok(view.reason);
});
test('sequence uint32 boundaries permit completed cache without borrowing current native result',()=>{
  const [caps,state]=pair();state.fm.parameter_values[0].sequence_id=4294967295;
  state.fm.result={session_id:0,engine_epoch:0,sequence_id:0,code:0};
  const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);assert.equal(attack(view).value,31);
  state.fm.parameter_values[0].sequence_id=0;assert.equal(attack(createFmRuntimeView(caps,state,writable)).value,31);
});
test('return values are detached and forged or mutated views cannot enable a write',()=>{
  const [caps,state]=pair();const view=createFmRuntimeView(caps,state,writable);assert.ok(view?.model);
  const clone=structuredClone(view);assert.equal(resolveFmParameterIntent(caps,state,clone,edit(clone)),null);
  view.runtime.profile.parameters[0].minimum=999;view.runtime.operatorsPerVoice[0]=1;view.presets[0].name='changed';
  const again=createFmRuntimeView(caps,state,writable);assert.equal(again.model.voices[0].operators.length,4);assert.equal(again.presets[0].name,'diagnostic_sine');
  assert.equal(state.fm.operators_per_voice[0],4);
  const readonly=createFmRuntimeView(caps,state);readonly.model.editable=true;attack(readonly).editable=true;
  assert.equal(resolveFmParameterIntent(caps,state,readonly,edit(readonly)),null);
  const unknown=view.model.parameters[0];unknown.editable=true;unknown.value=7;
  assert.equal(resolveFmParameterIntent(caps,state,view,{...edit(view),parameterId:'1',target:{backendId:'1',voiceId:null,operatorId:null}}),null);
  view.model.contextId='forged';assert.equal(resolveFmParameterIntent(caps,state,view,edit(view)),null);
});
test('visible parameter and ancestor restrictions reject waiting, unavailable and malformed flags',()=>{
  const [caps,state]=pair();
  for(const mutate of [
    v=>attack(v).waiting=true,v=>attack(v).available=false,v=>attack(v).waiting='false',
    v=>v.model.backend.editable='true',v=>v.model.patchOperators[0].available='true',
    v=>v.model.patchOperators[0].waiting=true,v=>attack(v).value='31'
  ]) {
    const view=createFmRuntimeView(caps,state,writable);const event=edit(view);mutate(view);
    assert.equal(resolveFmParameterIntent(caps,state,view,event),null);
  }
});
