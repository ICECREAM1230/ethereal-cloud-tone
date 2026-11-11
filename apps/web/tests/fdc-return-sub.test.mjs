import test from 'node:test';
import assert from 'node:assert/strict';
import {FdcController,calibrationWords,validateFdcState} from '../src/fdc.mjs';
import {mountFdcPanel} from '../src/fdc-panel.mjs';

function fixture(tag){
 const state={api_major:2,contract_version:1,available:true,valid:true,fresh:true,status_word:(tag<<16|0x0103)>>>0,revision:1,
  audio_engine:tag===0xfdc1?'subtractive':'fm',timbre:tag===0xfdc1?'current_subtractive_patch':'electric_piano_tine_sustained',continuous:true,
  eligible:true,requested_enable:false,driver_enabled:false,busy:false,driver_fault:false,local_error:false,user_config_active:false,
  pending:false,request:null,result:null,confirmation:'transport_only'};
 const owner={apiMajor:2,status:'online',suspended:false,rearming:false,modeWaiting:false,transition:null,
  token:100,sequence:3,epoch:1,inputVersion:1,runtimeState:{context_epoch:9},now:()=>0,notify(){},kick(){}};
 const fdc=new FdcController(owner);owner.fdc=fdc;fdc.state=validateFdcState(state);
 return {owner,fdc,state};
}
for(const tag of [0xfdc1,0xfdc2]){
 test(tag.toString(16)+' allows enable/calibration while retaining existing safety gates',()=>{
  for(const op of [1,3]){
   const {fdc}=fixture(tag);assert.equal(fdc.can(op),true);
   const words=op===3?calibrationWords({pitch_far:1000,pitch_near:2000,volume_far:3000,volume_near:1000,pitch_sigma:1,volume_sigma:2}):undefined;
   assert.equal(fdc.queue(op,words),true);assert.equal(fdc.due(),true);
   fdc.state.driver_fault=true;assert.equal(fdc.due(),false);
  }
  for(const change of [{eligible:false},{busy:true},{fresh:false},{valid:false},{driver_fault:true}]){
   const {fdc}=fixture(tag);Object.assign(fdc.state,change);assert.equal(fdc.can(1),false);assert.equal(fdc.can(3),false);
  }
 });
 test(tag.toString(16)+' panel displays actual engine and mounting sends no controls',()=>{
  const {owner,fdc}=fixture(tag);let commands=0;const nodes=new Map();
  const node=id=>{if(!nodes.has(id))nodes.set(id,{disabled:false,textContent:'',addEventListener(){}});return nodes.get(id);};
  const root={querySelector:selector=>node(selector),ownerDocument:{getElementById:id=>node('#'+id)}};
  fdc.queue=()=>{commands++;return true;};
  mountFdcPanel(root,owner).update();
  assert.equal(commands,0);assert.equal(node('#fdcEnable').disabled,false);assert.equal(node('#fdcApply').disabled,false);
  assert.match(node('#thereminVoice').textContent,tag===0xfdc1?/减法合成器.*当前减法音色.*持续发声/:/FM 电钢琴.*持续发声/);
  assert.doesNotMatch(node('#fdcHealth').textContent,/旧特雷门|更新配套FM/);
 });
}
test('engine identity still rejects a mismatched tag rather than accepting UI labels',()=>{
 const {state}=fixture(0xfdc1);assert.throws(()=>validateFdcState({...state,audio_engine:'fm'}),/invalid_fdc_engine/);
 assert.throws(()=>validateFdcState({...state,status_word:0xfdec0103,audio_engine:'unknown'}),/invalid_fdc_capability/);
});
