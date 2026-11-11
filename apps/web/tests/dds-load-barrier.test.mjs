import test from 'node:test';
import assert from 'node:assert/strict';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {RuntimeDevice,settle,turn} from './helpers/runtime-device.mjs';

async function ready(){
  const d=new RuntimeDevice();
  const c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});
  await c.connect();c.mode(0);await settle(c);
  assert.equal(c.modeState.applied,0);
  return {d,c};
}
test('DDS loading confirms empty keys, closes new On/CC, and reopens without replay',async()=>{
  const {d,c}=await ready();c.keys(1);await settle(c);
  assert.equal(typeof c.beginDdsPresetLoad,'function');
  assert.equal(await c.beginDdsPresetLoad(),true);
  assert.equal(c.desiredMask,0);assert.equal(c.playable,false);
  assert.equal(c.keys(2),false);assert.equal(c.cc(74,20),false);
  assert.equal(d.commands().at(-1).a,0);
  const ticket=c.ddsPresetLoadTicket;d.moveContext();d.state.legacy.panic=(d.state.legacy.panic+1)>>>0;
  assert.equal(await c.endDdsPresetLoad(ticket,d.state.context_epoch),true);await settle(c);
  assert.equal(c.playable,true);assert.equal(c.desiredMask,0);
  assert.equal(d.commands().filter(x=>x.op===3&&x.a!==0).length,1);
});
test('global STOP can still pass the DDS loading barrier immediately',async()=>{
  const {d,c}=await ready();
  assert.equal(typeof c.beginDdsPresetLoad,'function');
  assert.equal(await c.beginDdsPresetLoad(),true);
  assert.equal(c.panic(),true);assert.equal(c.ddsPresetLoading,false);await settle(c);
  assert.ok(d.commands().some(x=>x.op===4));
  c.endDdsPresetLoad();await settle(c);
});
test('expected preset context adoption keeps the load ticket; explicit STOP invalidates it',async()=>{
  const {d,c}=await ready();await c.beginDdsPresetLoad();
  assert.ok(c.ddsPresetLoadTicket);
  const ticket=c.ddsPresetLoadTicket;d.moveContext();await c.poll();await settle(c);
  assert.equal(c.ddsPresetLoadTicket,ticket);assert.equal(c.playable,false);
  c.panic();assert.notEqual(c.ddsPresetLoadTicket,ticket);await settle(c);c.endDdsPresetLoad();
});
test('fast preset completion adopts the returned bind epoch and resumes background polling',async()=>{
  const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket;
  d.moveContext();d.state.legacy.panic=(d.state.legacy.panic+1)>>>0;await c.endDdsPresetLoad(ticket,d.state.context_epoch);
  assert.equal(c.connecting,false);assert.equal(c.polling,false);
  const count=d.requests.filter(r=>r.path==='/api/v2/state').length;await c.poll();
  assert.ok(d.requests.filter(r=>r.path==='/api/v2/state').length>count);
});
test('exclusive load completion waits for its fresh session binding before releasing its barrier',async()=>{
  const {d,c}=await ready();await c.beginDdsPresetLoad();const ticket=c.ddsPresetLoadTicket;
  const request=d.transport.request;let gate;
  d.transport.request=async(path,options)=>{const result=await request(path,options);if(path==='/api/v2/session')await new Promise(resolve=>gate=resolve);return result;};
  d.moveContext();d.state.legacy.panic=(d.state.legacy.panic+1)>>>0;
  let ended=false;const ending=c.endDdsPresetLoad(ticket,d.state.context_epoch).then(()=>ended=true);
  for(let n=0;n<30&&!gate;n++)await turn();assert.ok(gate);
  try{await turn();await turn();assert.equal(ended,false,'new context is installed, but its session has not finished binding');}
  finally{gate();await ending;await settle(c);}
  assert.equal(c.status,'online');assert.equal(c.playable,true);assert.equal(c.polling,false);
});
test('a lifecycle change during preparation does not leave the DDS input barrier set',async()=>{
  const {d,c}=await ready();d.holdWhen=x=>x.op===3&&x.a===0;const preparing=c.beginDdsPresetLoad();
  for(let n=0;n<30&&!d.gate;n++)await turn();assert.ok(d.gate);
  const gate=d.gate;d.holdWhen=null;c.epoch++;gate();assert.equal(await preparing,false);
  assert.equal(c.ddsPresetLoading,false);await settle(c);
});
