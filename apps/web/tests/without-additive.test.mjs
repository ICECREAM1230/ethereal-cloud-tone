import test from 'node:test';
import assert from 'node:assert/strict';
import {validateV2Capabilities,validateV2State,encodeV2Command} from '../src/v2-protocol.mjs';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {RuntimeDevice,settle} from './helpers/runtime-device.mjs';

test('retired engine 3 cannot reenter discovery through an old extension descriptor',()=>{
  const d=new RuntimeDevice();
  d.caps.engines.push({id:3,available:true});d.caps.additive={contract_version:1};
  assert.throws(()=>validateV2Capabilities(d.caps),/invalid v2 engines/);
});
test('retired mode 3 cannot report an unblocked state through an old extension descriptor',()=>{
  const d=new RuntimeDevice();
  d.state.mode.requested=d.state.mode.applied=3;d.state.additive={contract_version:1};
  assert.throws(()=>validateV2State(d.state),/invalid v2 mode/);
});
test('blocked mode 3 remains observable so the user can explicitly return to a supported engine',async()=>{
  const d=new RuntimeDevice();d.state.mode={requested:3,applied:3,switching:true,recovering:false};
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.status,'online');assert.equal(c.modeState.applied,3);
  assert.equal(c.playable,false);assert.equal(c.editable,false);assert.equal(c.keys(1),false);
  assert.equal(c.cc(7,100),false);assert.equal(c.rawMidi([0x90,60,100]),false);assert.equal(c.mode(3),false);
  assert.equal(c.mode(0),true);await settle(c);assert.equal(c.modeState.applied,0);assert.equal(c.playable,true);
});
test('retired mode selection cannot be encoded using a stale editor runtime',()=>{
  const stale={kind:'additive',available:true,selectable:true,identity:{contextEpoch:10}};
  assert.throws(()=>encodeV2Command(100,2,10,5,3,0,0,stale),/outside v2 HTTP contract/);
});
for(const mode of [0,1,2])test('three-engine firmware keeps mode '+mode+' connected and rejects mode 3 without a request',async()=>{
  const d=new RuntimeDevice();
  d.state.mode.requested=d.state.mode.applied=mode;
  if(mode<2){d.state.legacy.ready=true;d.state.fm.available=false;d.caps.engines[2].available=false;}
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();assert.equal(c.status,'online');assert.equal(c.playable,true);
  const before=d.requests.length;assert.equal(c.canSelectMode(3),false);assert.equal(c.mode(3),false);
  await settle(c);assert.equal(d.requests.length,before);
  c.keys(1);await settle(c);assert.equal(d.commands().at(-1).a,1);
  c.keys(0);await settle(c);assert.equal(d.commands().at(-1).a,0);
});
