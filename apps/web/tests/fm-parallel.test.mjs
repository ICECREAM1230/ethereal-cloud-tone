import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {getFmRuntime,encodeV2Command} from '../src/v2-protocol.mjs';
import {RuntimeController} from '../src/runtime-controller.mjs';

const pair=()=>['capabilities','state'].map(name=>JSON.parse(readFileSync(new URL('./fixtures/fm32/'+name+'.json',import.meta.url))));
function controller(channel=2,mutate=()=>{}) {
  const [caps,state]=pair();state.clients[0].midi_channel=channel;mutate(caps,state);
  const c=new RuntimeController({request:()=>{throw Error('No network in snapshot gate test');}});
  c._major=2;c._caps=caps;c._runtime=state;c.state=state.legacy;
  c.client=0;c.token=100;c.status='online';c.kick=()=>null;c.refreshView();
  return c;
}
const edit=c=>({contextId:c.fmView.contextId,target:{backendId:'1',voiceId:null,operatorId:'patch-operator:0'},parameterId:'18',value:30});

test('real AE350 global mode0 keeps independent JT51 configuration available',()=>{
  const [caps,state]=pair();assert.equal(state.mode.applied,0);
  const fm=getFmRuntime(caps,state);
  assert.equal(fm.playable,true,fm.reason);assert.equal(fm.editable,true,fm.reason);
  for(const id of [3,4])assert.equal(encodeV2Command(100,1,state.context_epoch,8,id,0,0,fm).split(' ')[4],String(id));
});
for(const id of [3,4])test('current FM channel can apply actual preset '+id+' with global mode0',()=>{
  const c=controller();assert.equal(c.webMidiChannel,2);assert.equal(c.playable,true);
  assert.equal(c.fmView.presetEditable,true,c.fmView.reason);
  assert.equal(c.preset(id),true);assert.equal(c.transition.op,8);assert.equal(c.transition.a,id);
});
test('FM operator edits use FM panel capability instead of DDS control editability',()=>{
  const c=controller();assert.equal(c.editable,false,'DDS controls stay closed on the FM channel');
  assert.equal(c.fmParameter(edit(c)),true);
  assert.equal(c.parameters.size,1);assert.equal([...c.parameters.values()][0].op,7);
});
for(const channel of [0,1])test('other parallel channel '+channel+' keeps FM preset and parameter actions closed',()=>{
  const c=controller(channel);assert.equal(c.fmView.presetEditable,false);
  assert.equal(c.preset(3),false);assert.equal(c.fmParameter(edit(c)),false);
});
for(const property of ['modeWaiting','transition'])test(property+' blocks FM edits during a client transition',()=>{
  const c=controller();c[property]=property==='transition'?{op:8}:true;c.refreshView();
  assert.equal(c.fmView.presetEditable,false);assert.equal(c.fmParameter(edit(c)),false);
});
for(const [name,mutate] of Object.entries({
  'CPU fault':(c,s)=>s.cpu_mode.fault=true,
  'CPU invalid':(c,s)=>s.cpu_mode.valid=false,
  'missing FM':(c,s)=>s.fm.available=false,
  'FM invalid':(c,s)=>s.fm.valid=false,
  'FM busy':(c,s)=>s.fm.flags|=4,
  'FM fault':(c,s)=>s.fm.flags|=16,
  'input fault':(c,s)=>s.legacy.inputs.ready=false,
  'wrong profile':(c,s)=>s.fm.profile_sha256='wrong',
}))test(name+' still blocks parallel FM configuration',()=>{
  const c=controller(2,mutate);assert.equal(c.fmView.presetEditable,false);
  assert.equal(c.preset(3),false);assert.equal(c.fmParameter(edit(c)),false);
});
test('nonparallel CPU routing still requires the actual global FM selection',()=>{
  const [caps,state]=pair();caps.cpu_mode.channel_routes=false;state.cpu_mode.channel_routes=false;
  assert.equal(getFmRuntime(caps,state).playable,false);
});
