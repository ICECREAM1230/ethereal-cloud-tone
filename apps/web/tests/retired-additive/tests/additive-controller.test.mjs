import test from 'node:test';
import assert from 'node:assert/strict';
import {RuntimeController} from '../src/runtime-controller.mjs';
import {getAdditivePreset} from '../src/additive.mjs';
import {AdditiveTestDevice} from './helpers/additive-device.mjs';
import {settle,turn} from './helpers/runtime-device.mjs';
async function connected(){const d=new AdditiveTestDevice(),c=new RuntimeController(d.transport,{now:()=>d.time,delay:ms=>d.delay(ms)});await c.connect();return {c,d};}
async function selected(){const {c,d}=await connected();assert.equal(c.mode(3),true);await settle(c);assert.equal(c.playable,true);return {c,d};}
test('additive selection retains empty owner barrier and never sends a default patch',async()=>{
  const {c,d}=await selected();assert.equal(c.modeState.applied,3);assert.equal(d.patchRequests.length,0);assert.equal(c.additiveRuntime.available,true);assert.equal(d.commands().filter(e=>e.op===5&&e.a===3).length,1);
});
test('additive patch waits for actual commit identity then rebinds empty without replay',async()=>{
  const {c,d}=await selected(),oldToken=c.token;c.keys(1);await settle(c);
  const patch=getAdditivePreset('bell'),before=d.log.length,contextId=c.additiveRuntime.contextId;
  assert.equal(c.additivePatch(patch,contextId),true);patch.partials[0].weight=0;assert.equal(c.playable,false);await settle(c);
  assert.equal(d.patchRequests.length,1);assert.deepEqual(d.patchRequests[0].patch,getAdditivePreset('bell'));assert.equal(c.status,'online');assert.notEqual(c.token,oldToken);assert.equal(c.desiredMask,0);
  const log=d.log.slice(before),received=log.findIndex(e=>e.stage==='patch-received'),committed=log.findIndex(e=>e.stage==='patch-committed');
  assert(log.slice(0,received).some(e=>e.stage==='owner'&&e.op===3&&e.a===0));assert(committed>received);assert(log.slice(committed).some(e=>e.stage==='received'&&e.op===3&&e.a===0&&e.token!==oldToken));
  assert.deepEqual(c.additiveRuntime.patch,getAdditivePreset('bell'));
});
test('a stale additive editor callback cannot submit after background binding changes',async()=>{
  const {c,d}=await selected(),contextId=c.additiveRuntime.contextId;c.suspend();await c.resume();await settle(c);
  assert.notEqual(c.additiveRuntime.contextId,contextId);assert.equal(c.additivePatch(getAdditivePreset('organ'),contextId),false);assert.equal(d.patchRequests.length,0);
});
for(const status of [400,409,503])test('additive HTTP '+status+' never retries a rejected patch',async()=>{
  const {c,d}=await selected();d.patchError=status;c.additivePatch(getAdditivePreset('bell'),c.additiveRuntime.contextId);await settle(c);
  assert.equal(c.status,'offline');assert.equal(d.patchRequests.length,1);d.patchError=0;await c.connect();await settle(c);assert.equal(d.patchRequests.length,1);
});
test('owner acceptance without additive commit times out without showing success',async()=>{
  const {c,d}=await selected();d.noCommit=true;c.additivePatch(getAdditivePreset('bell'),c.additiveRuntime.contextId);await settle(c);
  assert.equal(c.status,'offline');assert.match(c.error,/additive commit timeout/);assert.deepEqual(d.state.additive.patch,getAdditivePreset('organ'));
});
test('wrong commit binding cannot confirm additive patch',async()=>{
  const {c,d}=await selected();d.wrongCommit=true;c.additivePatch(getAdditivePreset('bell'),c.additiveRuntime.contextId);await settle(c);assert.equal(c.status,'offline');assert.equal(c.desiredMask,0);
});
test('Panic preempts additive receipt and fences delayed commit without replay',async()=>{
  const {c,d}=await selected();d.holdPatch=true;c.additivePatch(getAdditivePreset('bell'),c.additiveRuntime.contextId);
  for(let i=0;i<100&&!d.patchGate;i++)await turn();assert.ok(d.patchGate);c.panic();await settle(c);const token=c.token;d.patchGate();await turn();await settle(c);
  assert.equal(c.token,token);assert.equal(c.status,'online');assert.equal(c.desiredMask,0);assert.deepEqual(d.state.additive.patch,getAdditivePreset('organ'));
});
test('own-key release retains the unsubmitted editor identity',async()=>{
  const {c}=await selected(),identity=c.additiveRuntime.contextId;c.releaseOwn();await settle(c);assert.equal(c.additiveRuntime.contextId,identity);
});
for(const stage of ['receipt','commit'])test('own-key release during additive '+stage+' wait preserves confirmation',async()=>{
  const {c,d}=await selected();d.holdPatch=stage==='receipt';c.additivePatch(getAdditivePreset('bell'),c.additiveRuntime.contextId);
  for(let i=0;i<100&&(stage==='receipt'?!d.patchGate:!d.log.some(e=>e.stage==='patch-owner'));i++)await turn();
  assert.ok(stage==='receipt'?d.patchGate:d.log.some(e=>e.stage==='patch-owner'));c.releaseOwn();if(d.patchGate)d.patchGate();await settle(c);
  assert.equal(c.status,'online');assert.equal(c.modeWaiting,false);assert.equal(c.playable,true);assert.deepEqual(c.additiveRuntime.patch,getAdditivePreset('bell'));assert.equal(c.desiredMask,0);
});
test('release during a missing COMMIT still reaches the bounded failure path',async()=>{
  const {c,d}=await selected();d.holdPatch=true;d.noCommit=true;c.additivePatch(getAdditivePreset('bell'),c.additiveRuntime.contextId);
  for(let i=0;i<100&&!d.patchGate;i++)await turn();assert.ok(d.patchGate);c.releaseOwn();d.patchGate();await settle(c);
  assert.equal(c.status,'offline');assert.equal(c.modeWaiting,false);assert.match(c.error,/additive commit timeout/);assert.equal(c.desiredMask,0);
});
